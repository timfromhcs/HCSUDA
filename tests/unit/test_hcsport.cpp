#include "test_framework.h"

#include "translation/cuda_source/scanner.h"

#include <cstring>
#include <string>

namespace {

const char* kSample = R"(#include <cuda_runtime.h>
#include <cstdio>

__global__ void vec_add(float* a, float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
    __syncthreads();
}

int main() {
    float *d_a, *d_b, *d_c;
    cudaMalloc(&d_a, 1024);
    cudaMalloc(&d_b, 1024);
    cudaMalloc(&d_c, 1024);
    vec_add<<<4, 256>>>(d_a, d_b, d_c, 1024);
    cudaDeviceSynchronize();
    atomicAdd(&d_c[0], 1.0f);
    cudaFree(d_a);
    return 0;
}
)";

hcsResult scanText(const char* text, hcs::cudascan::Report* report)
{
    return hcs::cudascan::scan(text, std::strlen(text), report);
}

std::string lastMessage()
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    return info.message;
}

} // namespace

HCS_TEST(hcsport_scan_reports_constructs_without_claiming_translation)
{
    hcs::cudascan::Report report;
    HCS_REQUIRE_OK(scanText(kSample, &report));
    CHECK(report.lines >= 15);
    CHECK(report.findings.size() >= 10);
    CHECK(report.manualCount > 0);
    CHECK_EQ(report.unsupportedCount, 1u); /* only atomicAdd in this sample */

    bool sawLaunch = false;
    bool sawAtomic = false;
    for (const auto& finding : report.findings) {
        if (finding.construct == "<<<") {
            sawLaunch = true;
            CHECK_EQ(finding.classification, hcs::cudascan::Classification::ManualPort);
            CHECK(finding.line > 0 && finding.column > 0);
        }
        if (finding.construct == "atomicAdd") {
            sawAtomic = true;
            CHECK_EQ(finding.classification, hcs::cudascan::Classification::Unsupported);
        }
    }
    CHECK(sawLaunch);
    CHECK(sawAtomic);

    const std::string json = hcs::cudascan::toJson(report);
    CHECK_MSG(json.find("\"translated\":0") != std::string::npos, "must not claim translations");
    CHECK_MSG(json.find("\"unsupported\":1") != std::string::npos, json);
    CHECK_MSG(json.find("\"classification\":\"unsupported\"") != std::string::npos, json);
}

HCS_TEST(hcsport_rejects_binary_and_oversized_input)
{
    hcs::cudascan::Report report;
    const char binary[] = {'a', '\0', 'b'};
    CHECK_EQ(hcs::cudascan::scan(binary, sizeof(binary), &report), HCS_E_INVALID_ARGUMENT);
    CHECK_MSG(lastMessage().find("binary input") != std::string::npos, lastMessage());

    std::string huge(hcs::cudascan::kMaxSourceBytes + 1, 'x');
    CHECK_EQ(hcs::cudascan::scan(huge.data(), huge.size(), &report), HCS_E_SECURITY_POLICY);
    CHECK_EQ(hcs::cudascan::scan("", 0, &report), HCS_E_SECURITY_POLICY);
    CHECK_EQ(hcs::cudascan::scan(nullptr, 4, &report), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcs::cudascan::scan("int x;", 6, nullptr), HCS_E_INVALID_ARGUMENT);
}

HCS_TEST(hcsport_plain_host_code_has_no_findings)
{
    const char* host = "int main() { return 0; }\n";
    hcs::cudascan::Report report;
    HCS_REQUIRE_OK(scanText(host, &report));
    CHECK_EQ(report.findings.size(), std::size_t{0});
    CHECK_EQ(report.manualCount, 0u);
    CHECK_EQ(report.unsupportedCount, 0u);
}
