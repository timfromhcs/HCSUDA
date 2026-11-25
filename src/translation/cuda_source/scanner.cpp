#include "translation/cuda_source/scanner.h"

#include "core/internal.h"

#include <algorithm>
#include <cstring>

namespace hcs::cudascan {
namespace {

struct Rule {
    const char* token;
    Classification classification;
    const char* detail;
};

const Rule kRules[] = {
    {"<<<", Classification::ManualPort,
     "kernel launch syntax must become an explicit hcsKernelCreate/hcsDispatch pair"},
    {"__global__", Classification::ManualPort,
     "device entry point must be lowered to HCSIR/HKL and compiled by hcscc"},
    {"__device__", Classification::ManualPort, "device function requires manual HCSIR lowering"},
    {"__shared__", Classification::ManualPort,
     "shared memory maps to HCSIR workgroup scratch, which has no source-level syntax yet"},
    {"threadIdx", Classification::ManualPort,
     "HCS exposes invocation/workgroup ids through HCSIR, not CUDA built-ins"},
    {"blockIdx", Classification::ManualPort, "map to HCSIR workgroup id"},
    {"blockDim", Classification::ManualPort, "map to HCSIR local size / workgroup dimensions"},
    {"gridDim", Classification::ManualPort, "map to dispatch group counts"},
    {"cudaMalloc", Classification::ManualPort, "use hcsBufferCreate with an explicit memory cap"},
    {"cudaFree", Classification::ManualPort, "use hcsBufferDestroy"},
    {"cudaMemcpy", Classification::ManualPort,
     "use hcsBufferUpload/Download; host-visible buffers in V1"},
    {"cudaDeviceSynchronize", Classification::ManualPort, "use hcsFlowSynchronize"},
    {"cudaStreamCreate", Classification::ManualPort, "use hcsFlowCreate"},
    {"cudaEventCreate", Classification::ManualPort, "use hcsEventCreate"},
    {"__syncthreads", Classification::ManualPort,
     "workgroup barrier is expressible only inside a generated shader"},
    {"#include <cuda", Classification::ManualPort,
     "CUDA headers are proprietary/foreign; do not bundle them"},
    {"atomicAdd", Classification::Unsupported, "HCS atomics are not implemented in V1"},
    {"atomicCAS", Classification::Unsupported, "HCS atomics are not implemented in V1"},
    {"atomicMin", Classification::Unsupported, "HCS atomics are not implemented in V1"},
    {"atomicMax", Classification::Unsupported, "HCS atomics are not implemented in V1"},
    {"cublas", Classification::Unsupported, "cuBLAS ABI compatibility is not provided"},
    {"cudnn", Classification::Unsupported, "cuDNN ABI compatibility is not provided"},
    {"nvrtc", Classification::Unsupported, "runtime CUDA compilation is not provided"},
    {"cudaGraphics", Classification::Unsupported,
     "graphics interop is not part of the V1 compute runtime"},
};

std::string escapeJson(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void addFinding(Report* report, uint32_t line, uint32_t column, const Rule& rule)
{
    if (report->findings.size() >= kMaxFindings) {
        return;
    }
    Finding finding;
    finding.line = line;
    finding.column = column;
    finding.construct = rule.token;
    finding.detail = rule.detail;
    finding.classification = rule.classification;
    if (rule.classification == Classification::Unsupported) {
        ++report->unsupportedCount;
    } else {
        ++report->manualCount;
    }
    report->findings.push_back(std::move(finding));
}

} // namespace

const char* classificationName(Classification value)
{
    switch (value) {
    case Classification::HostUnchanged: return "host_unchanged";
    case Classification::ManualPort: return "manual_port_required";
    case Classification::Unsupported: return "unsupported";
    default: return "unknown";
    }
}

hcsResult scan(const char* text, size_t size, Report* out)
{
    if (!text || !out) {
        return fail(HCS_E_INVALID_ARGUMENT, "hcsport.scan", "text and report must not be null");
    }
    if (size == 0 || size > kMaxSourceBytes) {
        return fail(HCS_E_SECURITY_POLICY, "hcsport.scan",
                    "source size must be between 1 byte and 4 MiB");
    }
    if (std::memchr(text, 0, size) != nullptr) {
        return fail(HCS_E_INVALID_ARGUMENT, "hcsport.scan",
                    "source contains NUL bytes; binary input is not accepted");
    }

    Report report;
    report.bytes = size;
    size_t start = 0;
    while (start < size) {
        size_t end = std::min(start + kMaxLineBytes,
                              size); /* bounded even without a newline */
        const void* newline = std::memchr(text + start, '\n', end - start);
        if (newline) {
            end = static_cast<const char*>(newline) - text;
        }
        const std::string line(text + start, end - start);
        ++report.lines;
        for (const Rule& rule : kRules) {
            size_t position = 0;
            while ((position = line.find(rule.token, position)) != std::string::npos) {
                addFinding(&report, report.lines, static_cast<uint32_t>(position + 1), rule);
                position += std::max<size_t>(1, rule.token[0] ? std::strlen(rule.token) : 1);
            }
        }
        if (end == size) {
            break;
        }
        start = end + 1;
    }

    *out = std::move(report);
    return HCS_SUCCESS;
}

std::string toJson(const Report& report)
{
    std::string json = "{\"scanner\":\"hcsport/1\",\"translated\":0,\"host_unchanged\":0,";
    json += "\"manual_port_required\":" + std::to_string(report.manualCount) + ",";
    json += "\"unsupported\":" + std::to_string(report.unsupportedCount) + ",";
    json += "\"lines\":" + std::to_string(report.lines) + ",\"bytes\":" +
            std::to_string(report.bytes) + ",\"findings\":[";
    for (size_t i = 0; i < report.findings.size(); ++i) {
        const Finding& finding = report.findings[i];
        if (i) {
            json += ",";
        }
        json += "{\"line\":" + std::to_string(finding.line) +
                ",\"column\":" + std::to_string(finding.column) + ",\"construct\":\"" +
                escapeJson(finding.construct) + "\",\"classification\":\"" +
                classificationName(finding.classification) + "\",\"detail\":\"" +
                escapeJson(finding.detail) + "\"}";
    }
    json += "]}";
    return json;
}

} // namespace hcs::cudascan
