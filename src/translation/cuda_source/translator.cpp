#include "translation/cuda_source/translator.h"

#include "core/internal.h"
#include "translation/cuda_source/scanner.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace hcs::cudatranslate {
namespace {

std::string stripComments(const std::string& source)
{
    std::string out;
    out.reserve(source.size());
    bool lineComment = false;
    bool blockComment = false;
    for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (lineComment) {
            if (c == '\n') {
                lineComment = false;
                out.push_back(c);
            }
            continue;
        }
        if (blockComment) {
            if (c == '*' && i + 1 < source.size() && source[i + 1] == '/') {
                blockComment = false;
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            lineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            blockComment = true;
            ++i;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string noSpaces(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    return out;
}

hcsResult unsupported(const std::string& detail)
{
    return fail(HCS_E_TRANSLATION_UNSUPPORTED, "hcsport.convert", detail);
}

std::string lastIdentifier(const std::string& text)
{
    size_t end = text.size();
    while (end > 0 && !(std::isalnum(static_cast<unsigned char>(text[end - 1])) ||
                        text[end - 1] == '_')) {
        --end;
    }
    size_t start = end;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
                         text[start - 1] == '_')) {
        --start;
    }
    return text.substr(start, end - start);
}

bool extractBlock(const std::string& source, size_t begin, std::string* signature,
                  std::string* body)
{
    const size_t openParen = source.find('(', begin);
    if (openParen == std::string::npos || openParen - begin > 128) {
        return false;
    }
    int depth = 0;
    size_t closeParen = std::string::npos;
    for (size_t i = openParen; i < source.size(); ++i) {
        if (source[i] == '(') {
            ++depth;
        } else if (source[i] == ')') {
            if (--depth == 0) {
                closeParen = i;
                break;
            }
        }
    }
    if (closeParen == std::string::npos) {
        return false;
    }
    const size_t openBrace = source.find('{', closeParen);
    if (openBrace == std::string::npos || openBrace - closeParen > 64) {
        return false;
    }
    depth = 0;
    size_t closeBrace = std::string::npos;
    for (size_t i = openBrace; i < source.size(); ++i) {
        if (source[i] == '{') {
            ++depth;
        } else if (source[i] == '}') {
            if (--depth == 0) {
                closeBrace = i;
                break;
            }
        }
    }
    if (closeBrace == std::string::npos || closeBrace - openBrace > kMaxKernelBytes) {
        return false;
    }
    *signature = source.substr(begin, closeParen - begin + 1);
    *body = source.substr(openBrace + 1, closeBrace - openBrace - 1);
    return true;
}

} // namespace

hcsResult translate(const char* source, size_t size, std::string* outHcsir,
                    std::string* outReportJson)
{
    if (!source || !outHcsir) {
        return fail(HCS_E_INVALID_ARGUMENT, "hcsport.convert", "source and output are required");
    }
    if (size == 0 || size > 4u << 20) {
        return fail(HCS_E_SECURITY_POLICY, "hcsport.convert", "source size is out of range");
    }
    const std::string clean = stripComments(std::string(source, size));
    const size_t global = clean.find("__global__");
    if (global == std::string::npos) {
        return unsupported("no __global__ kernel found");
    }
    const size_t voidPos = clean.find("void", global + 10);
    if (voidPos == std::string::npos || voidPos - global > 64) {
        return unsupported("only '__global__ void <name>(...)' kernels are supported");
    }
    size_t nameStart = voidPos + 4;
    while (nameStart < clean.size() && std::isspace(static_cast<unsigned char>(clean[nameStart]))) {
        ++nameStart;
    }
    size_t nameEnd = nameStart;
    while (nameEnd < clean.size() &&
           (std::isalnum(static_cast<unsigned char>(clean[nameEnd])) || clean[nameEnd] == '_')) {
        ++nameEnd;
    }
    if (nameEnd == nameStart) {
        return unsupported("kernel name is missing");
    }
    std::string kernelName = clean.substr(nameStart, nameEnd - nameStart);

    std::string signature;
    std::string body;
    if (!extractBlock(clean, nameEnd, &signature, &body)) {
        return unsupported("kernel signature or body could not be parsed");
    }

    const size_t paramsOpen = signature.find('(');
    std::string params = paramsOpen == std::string::npos
                             ? std::string()
                             : signature.substr(paramsOpen + 1);
    params.erase(std::remove(params.begin(), params.end(), ')'), params.end());
    std::vector<std::string> pointerNames;
    std::string alphaName;
    std::string countName;
    size_t start = 0;
    while (start <= params.size()) {
        const size_t comma = params.find(',', start);
        const std::string part = params.substr(start, comma == std::string::npos
                                                         ? std::string::npos
                                                         : comma - start);
        const std::string compact = noSpaces(part);
        if (!compact.empty()) {
            const size_t star = compact.rfind('*');
            const std::string identifier = lastIdentifier(compact);
            if (star != std::string::npos && !identifier.empty()) {
                if (compact.find("float") == std::string::npos) {
                    return unsupported("only float* storage buffers are supported");
                }
                pointerNames.push_back(identifier);
            } else if (compact.find("float") == 0 && compact.size() > 5) {
                alphaName = compact.substr(5); /* "floatalpha" -> "alpha" */
            } else if (!identifier.empty()) {
                countName = compact.find("int") == 0      ? compact.substr(3)
                            : compact.find("size_t") == 0 ? compact.substr(6)
                                                            : identifier;
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    if (pointerNames.size() != 3 || countName.empty()) {
        return unsupported("expected three float* buffers and one integer element-count parameter");
    }

    const std::string flat = noSpaces(body);
    static const char* blocked[] = {"atomic", "cuda", "__shared__", "cublas", "cudnn",
                                    "__syncthreads", "while", "for(", "malloc", "new"};
    for (const char* token : blocked) {
        if (flat.find(token) != std::string::npos) {
            return unsupported(std::string("unsupported construct in kernel body: ") + token);
        }
    }
    if (flat.find("blockIdx.x*blockDim.x+threadIdx.x") == std::string::npos) {
        return unsupported("only the canonical 1-D index expression is supported");
    }

    const std::string& out = pointerNames[2];
    const std::string& lhs = pointerNames[0];
    const std::string& rhs = pointerNames[1];
    const std::string addPattern = out + "[i]=" + lhs + "[i]+" + rhs + "[i];";
    const std::string fmaPattern = alphaName.empty()
                                       ? std::string()
                                       : out + "[i]=" + alphaName + "*" + rhs + "[i]+" + lhs +
                                             "[i];";
    const bool isAdd = flat.find(addPattern) != std::string::npos;
    const bool isFma = !fmaPattern.empty() && flat.find(fmaPattern) != std::string::npos;
    if (!isAdd && !isFma) {
        return unsupported("kernel body is not a supported vecadd/scalar-multiply statement");
    }

    std::string hcsir = "hcsir 1\nkernel " + kernelName + "\n";
    hcsir += "buffer 0 " + pointerNames[0] + " : f32\n";
    hcsir += "buffer 1 " + pointerNames[1] + " : f32\n";
    hcsir += "buffer 2 " + pointerNames[2] + " : f32\n";
    hcsir += alphaName.empty() ? "const alpha : f32 = 1.0\n"
                               : "param " + alphaName + " : f32\n";
    hcsir += "local_size 64\n\nentry:\n";
    hcsir += "  i = invocation\n";
    hcsir += "  va = load " + pointerNames[0] + ", i\n";
    hcsir += "  vb = load " + pointerNames[1] + ", i\n";
    hcsir += isFma ? "  sum = fma " + alphaName + ", vb, va\n"
                   : "  sum = add va, vb\n";
    hcsir += "  store " + pointerNames[2] + ", i, sum\n";
    hcsir += "  ret\n";
    *outHcsir = hcsir;

    if (outReportJson) {
        cudascan::Report scan;
        const hcsResult scanResult = cudascan::scan(source, size, &scan);
        std::string json = "{\"translator\":\"hcsport/1\",\"translated\":1,";
        json += "\"translated_construct\":\"" + kernelName + "\",";
        json += "\"host_manual\":" +
                std::to_string(scanResult == HCS_SUCCESS ? scan.manualCount : 0) + ",";
        json += "\"host_unsupported\":" +
                std::to_string(scanResult == HCS_SUCCESS ? scan.unsupportedCount : 0) +
                ",\"note\":\"manual review required for host code and for the i<n guard; "
                "dispatch group count must be ceil(n/64)\"}";
        *outReportJson = json;
    }
    return HCS_SUCCESS;
}

} // namespace hcs::cudatranslate
