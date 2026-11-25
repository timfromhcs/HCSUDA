#include "test_framework.h"
#include "test_helpers.h"

#include "api/mcp/mcp_server.h"
#include "api/rest/rest_server.h"
#include "ir/hcsir.h"
#include "translation/cuda_source/scanner.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint32_t nextRandom(uint32_t* seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

} // namespace

HCS_TEST(stress_repeated_cpu_dispatch_reuses_buffers)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_CPU)) {
        hcstest::reportSkip("no CPU device available");
        return;
    }
    const uint32_t n = 256;
    std::vector<float> a(n, 0.25f), b(n, 0.5f), c(n, 0.0f);
    hcsBuffer bufs[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        const void* initial = i == 2 ? nullptr : (i == 0 ? static_cast<const void*>(a.data())
                                                        : static_cast<const void*>(b.data()));
        HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), initial, &bufs[i]));
    }
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    testutil::PushConsts push{};
    push.u0 = n;
    push.f0 = 1.5f;
    const auto desc = testutil::makeDispatch({bufs[0], bufs[1], bufs[2]}, n / 64, &push,
                                             sizeof(push));
    for (uint32_t i = 0; i < 2000; ++i) {
        HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    }
    HCS_REQUIRE_OK(hcsBufferDownload(bufs[2], 0, c.data(), n * sizeof(float)));
    CHECK_NEAR(c[0], 0.25 + 1.5 * 0.5, 1e-6);
    hcsKernelDestroy(kernel);
    for (hcsBuffer buffer : bufs) {
        hcsBufferDestroy(buffer);
    }
}

HCS_TEST(stress_async_flow_many_submissions)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_VULKAN)) {
        hcstest::reportSkip("no Vulkan device available");
        return;
    }
    const uint32_t n = 1024;
    std::vector<float> a(n, 0.25f), b(n, 0.5f), c(n, 0.0f);
    hcsBuffer bufs[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        const void* initial = i == 2 ? nullptr : (i == 0 ? static_cast<const void*>(a.data())
                                                        : static_cast<const void*>(b.data()));
        HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), initial, &bufs[i]));
    }
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    hcsFlow flow = nullptr;
    HCS_REQUIRE_OK(hcsFlowCreate(session.context, nullptr, &flow));
    testutil::PushConsts push{};
    push.u0 = n;
    push.f0 = 1.5f;
    const auto desc = testutil::makeDispatch({bufs[0], bufs[1], bufs[2]}, n / 64, &push,
                                             sizeof(push));
    for (uint32_t i = 0; i < 500; ++i) {
        HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, flow));
    }
    HCS_REQUIRE_OK(hcsFlowSynchronize(flow, UINT64_MAX));
    HCS_REQUIRE_OK(hcsBufferDownload(bufs[2], 0, c.data(), n * sizeof(float)));
    CHECK_NEAR(c[0], 0.25 + 1.5 * 0.5, 1e-6);
    hcsFlowDestroy(flow);
    hcsKernelDestroy(kernel);
    for (hcsBuffer buffer : bufs) {
        hcsBufferDestroy(buffer);
    }
}

HCS_TEST(fuzz_bounded_parsers_reject_random_input)
{
    uint32_t seed = 0xC0FFEEu;
    for (uint32_t iteration = 0; iteration < 200; ++iteration) {
        const size_t bytes = nextRandom(&seed) % 1024;
        std::string input(bytes, '\0');
        for (size_t i = 0; i < bytes; ++i) {
            input[i] = static_cast<char>(nextRandom(&seed) & 0xFF);
        }

        hcs::ir::Module module;
        (void)hcs::ir::compile(input.data(), input.size(), &module);

        hcs::cudascan::Report report;
        (void)hcs::cudascan::scan(input.data(), input.size(), &report);

        (void)hcs::mcp::handle(input);

        hcs::rest::RestConfig config;
        config.token = "fuzz-token";
        (void)hcs::rest::handleRequest("GET", "/api/v1/health", input, input, config);
    }
}
