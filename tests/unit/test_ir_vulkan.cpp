#include "test_framework.h"
#include "test_helpers.h"

#include <cmath>
#include <cstring>
#include <vector>

/*
 * The SPIR-V bytes below are generated at build time by:
 *   hcscc tests/fixtures/ir/vecadd.hcsir -> GLSL -> glslc -> spv_ir_vecadd.cpp
 * This test therefore proves that an HCSIR module can reach real Vulkan execution,
 * not merely that a shader compiles.
 */
extern const unsigned char hcs_spv_ir_vecadd[];
extern const unsigned int hcs_spv_ir_vecadd_len;

HCS_TEST(hcsir_generated_spirv_runs_on_vulkan)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_VULKAN)) {
        hcstest::reportSkip("no Vulkan device available");
        return;
    }

    hcsProgram program = nullptr;
    HCS_REQUIRE_OK(hcsProgramCreateSpirv(
        session.context, hcs_spv_ir_vecadd, hcs_spv_ir_vecadd_len, &program));
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelFromProgram(program, &kernel));

    const uint32_t n = 4096;
    std::vector<float> a(n), b(n), c(n, 0.0f);
    testutil::fillPattern(a, 1.0f, 11);
    testutil::fillPattern(b, 1.0f, 12);

    hcsBuffer bufs[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), nullptr, &bufs[i]));
    }
    HCS_REQUIRE_OK(hcsBufferUpload(bufs[0], 0, a.data(), n * sizeof(float)));
    HCS_REQUIRE_OK(hcsBufferUpload(bufs[1], 0, b.data(), n * sizeof(float)));

    const float alpha = 1.25f;
    auto desc = testutil::makeDispatch({bufs[0], bufs[1], bufs[2]}, n / 64, &alpha, sizeof(alpha));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    HCS_REQUIRE_OK(hcsBufferDownload(bufs[2], 0, c.data(), n * sizeof(float)));

    for (uint32_t i = 0; i < n; ++i) {
        const float picked = b[i] > 0.0f ? a[i] : b[i];
        const double expected = static_cast<double>(alpha) * static_cast<double>(b[i]) +
                                static_cast<double>(picked);
        CHECK_NEAR(c[i], expected, 2e-5);
    }

    hcsKernelDestroy(kernel);
    hcsProgramDestroy(program);
    for (hcsBuffer buffer : bufs) {
        hcsBufferDestroy(buffer);
    }
}

HCS_TEST(hcsir_program_rejects_invalid_spirv)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_VULKAN)) {
        hcstest::reportSkip("no Vulkan device available");
        return;
    }

    hcsProgram program = nullptr;
    const uint32_t badMagic = 0xDEADBEEFu;
    CHECK_EQ(hcsProgramCreateSpirv(session.context, &badMagic, sizeof(badMagic), &program),
             HCS_E_SPIRV_INVALID);
    const unsigned char unaligned[5] = {1, 2, 3, 4, 5};
    CHECK_EQ(hcsProgramCreateSpirv(session.context, unaligned, sizeof(unaligned), &program),
             HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcsProgramCreateSpirv(session.context, nullptr, 0, &program),
             HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcsKernelFromProgram(nullptr, nullptr), HCS_E_INVALID_ARGUMENT);
}

HCS_TEST(hcsir_program_is_unsupported_on_cpu_backend)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_CPU)) {
        hcstest::reportSkip("no CPU device available");
        return;
    }
    hcsProgram program = nullptr;
    const hcsResult result = hcsProgramCreateSpirv(
        session.context, hcs_spv_ir_vecadd, hcs_spv_ir_vecadd_len, &program);
    CHECK_EQ(result, HCS_E_UNSUPPORTED_FEATURE);
}
