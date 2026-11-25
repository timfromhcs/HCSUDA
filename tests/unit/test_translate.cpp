#include "test_framework.h"

#include "ir/hcsir.h"
#include "translation/cuda_source/translator.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string fixture()
{
    std::ifstream file(std::string(HCS_TEST_SOURCE_DIR) + "/tests/fixtures/cuda/vecadd.cu",
                       std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::string lastMessage()
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    return info.message;
}

} // namespace

HCS_TEST(translate_vecadd_subset_produces_runnable_hcsir)
{
    const std::string source = fixture();
    std::string hcsir;
    std::string report;
    HCS_REQUIRE_OK(hcs::cudatranslate::translate(source.data(), source.size(), &hcsir, &report));
    CHECK_MSG(hcsir.find("kernel vec_add_f32") != std::string::npos, hcsir);
    CHECK_MSG(hcsir.find("buffer 0 a : f32") != std::string::npos, hcsir);
    CHECK_MSG(hcsir.find("sum = add va, vb") != std::string::npos, hcsir);
    CHECK_MSG(report.find("\"translated\":1") != std::string::npos, report);

    hcs::ir::Module module;
    HCS_REQUIRE_OK(hcs::ir::compile(hcsir.data(), hcsir.size(), &module));

    const uint32_t n = 1001;
    std::vector<float> a(n), b(n), c(n, 0.0f);
    for (uint32_t i = 0; i < n; ++i) {
        a[i] = static_cast<float>(i % 17) * 0.25f;
        b[i] = static_cast<float>(i % 13) * 0.5f;
    }
    hcs::ir::RunArgs args;
    args.bufferCount = 3;
    args.buffers[0] = a.data();
    args.sizes[0] = n * sizeof(float);
    args.buffers[1] = b.data();
    args.sizes[1] = n * sizeof(float);
    args.buffers[2] = c.data();
    args.sizes[2] = n * sizeof(float);
    args.paramCount = 1;
    args.params[0] = 1.0;
    HCS_REQUIRE_OK(hcs::ir::run(module, args, n));
    for (uint32_t i = 0; i < n; ++i) {
        CHECK_NEAR(c[i], static_cast<double>(a[i]) + static_cast<double>(b[i]), 1e-5);
    }
}

HCS_TEST(translate_fma_variant_uses_fma_opcode)
{
    const char* source = R"(
__global__ void scaled(float* a, float* b, float* c, float alpha, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { c[i] = alpha * b[i] + a[i]; }
}
)";
    std::string hcsir;
    HCS_REQUIRE_OK(hcs::cudatranslate::translate(source, std::strlen(source), &hcsir, nullptr));
    CHECK_MSG(hcsir.find("kernel scaled") != std::string::npos, hcsir);
    CHECK_MSG(hcsir.find("sum = fma alpha, vb, va") != std::string::npos, hcsir);
}

HCS_TEST(translate_fails_closed_on_unsupported_constructs)
{
    const char* atomic = R"(
__global__ void bad(float* a, float* b, float* c, float alpha, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    c[i] = a[i] + b[i];
    atomicAdd(&c[0], 1.0f);
}
)";
    std::string hcsir;
    CHECK_EQ(hcs::cudatranslate::translate(atomic, std::strlen(atomic), &hcsir, nullptr),
             HCS_E_TRANSLATION_UNSUPPORTED);
    CHECK_MSG(lastMessage().find("atomic") != std::string::npos, lastMessage());
    CHECK(hcsir.empty());

    const char* noGlobal = "int main() { return 0; }\n";
    CHECK_EQ(hcs::cudatranslate::translate(noGlobal, std::strlen(noGlobal), &hcsir, nullptr),
             HCS_E_TRANSLATION_UNSUPPORTED);
    CHECK_EQ(hcs::cudatranslate::translate(nullptr, 0, &hcsir, nullptr), HCS_E_INVALID_ARGUMENT);
}
