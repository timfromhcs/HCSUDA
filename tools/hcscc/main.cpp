/*
 * HCSCC — HCSIR compiler front-end.
 *
 * Compiles a HCSIR v1 text module to a GLSL 450 compute shader and verifies the module
 * with the same parser/verifier the CPU interpreter uses. It does not invoke glslc; the
 * build does that separately so a compiler failure is a build failure, not a silent no-op.
 */

#include "ir/hcsir.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool readFile(const char* path, std::string* out)
{
    std::FILE* file = std::fopen(path, "rb");
    if (!file) {
        return false;
    }
    char chunk[8192];
    size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        out->append(chunk, read);
        if (out->size() > hcs::ir::kMaxSourceBytes + 1) {
            std::fclose(file);
            return false;
        }
    }
    std::fclose(file);
    return true;
}

void printError(const char* operation, hcsResult result)
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    std::fprintf(stderr, "%s failed: %s (%s)\n", operation, hcsResultString(result),
                 info.message);
}

} // namespace

int main(int argc, char** argv)
{
    const char* input = nullptr;
    const char* output = nullptr;
    bool showStats = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: hcscc [--input file.hcsir] [--output file.comp] [--stats]\n"
                        "       (reads stdin and writes stdout when paths are omitted)\n");
            return 0;
        } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strcmp(argv[i], "--stats") == 0) {
            showStats = true;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    std::string source;
    if (input) {
        if (!readFile(input, &source)) {
            std::fprintf(stderr, "cannot read input: %s\n", input);
            return 1;
        }
    } else {
        char chunk[8192];
        size_t read = 0;
        while ((read = std::fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
            source.append(chunk, read);
            if (source.size() > hcs::ir::kMaxSourceBytes + 1) {
                std::fprintf(stderr, "input exceeds the HCSIR size limit\n");
                return 1;
            }
        }
    }

    hcs::ir::Module module;
    hcsResult result = hcs::ir::compile(source.data(), source.size(), &module);
    if (result != HCS_SUCCESS) {
        printError("HCSIR verification", result);
        return 1;
    }

    std::string glsl;
    result = hcs::ir::lowerGlsl(module, &glsl);
    if (result != HCS_SUCCESS) {
        printError("HCSIR -> GLSL lowering", result);
        return 1;
    }

    if (output) {
        std::FILE* file = std::fopen(output, "wb");
        if (!file) {
            std::fprintf(stderr, "cannot write output: %s\n", output);
            return 1;
        }
        std::fwrite(glsl.data(), 1, glsl.size(), file);
        std::fclose(file);
    } else {
        std::fwrite(glsl.data(), 1, glsl.size(), stdout);
    }

    if (showStats) {
        std::fprintf(stderr,
                     "kernel=%s local_size=%u blocks=%zu instructions=%u buffers=%zu "
                     "params=%zu constants=%zu glsl_bytes=%zu\n",
                     module.kernel.c_str(), module.localSize, module.blocks.size(),
                     module.instructionCount, module.buffers.size(), module.params.size(),
                     module.constants.size(), glsl.size());
    }
    return 0;
}
