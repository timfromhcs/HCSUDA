#include "test_framework.h"
#include "core/internal.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct VecAddCase {
    hcsContext context = nullptr;
    hcsBuffer a = nullptr, b = nullptr, c = nullptr;
    std::vector<float> hostA, hostB, hostC;
    uint32_t n = 1024;

    bool create(const char* label)
    {
        hostA.resize(n);
        hostB.resize(n);
        hostC.resize(n, 0.0f);
        testutil::fillPattern(hostA, 1.0f, 31);
        testutil::fillPattern(hostB, 1.0f, 32);
        if (testutil::makeBuffer(context, n * sizeof(float), hostA.data(), &a) != HCS_SUCCESS ||
            testutil::makeBuffer(context, n * sizeof(float), hostB.data(), &b) != HCS_SUCCESS ||
            testutil::makeBuffer(context, n * sizeof(float), nullptr, &c) != HCS_SUCCESS) {
            hcstest::reportFailure(__FILE__, __LINE__, label, "buffer creation failed");
            return false;
        }
        return true;
    }

    hcsDispatchDesc desc(float alpha) const
    {
        testutil::PushConsts push{};
        push.u0 = n;
        push.f0 = alpha;
        return testutil::makeDispatch({a, b, c}, n / 64, &push, sizeof(push));
    }

    bool verify(float alpha, const char* label)
    {
        if (hcsBufferDownload(c, 0, hostC.data(), n * sizeof(float)) != HCS_SUCCESS) {
            hcstest::reportFailure(__FILE__, __LINE__, label, "download failed");
            return false;
        }
        for (uint32_t i = 0; i < n; ++i) {
            const double expected = static_cast<double>(hostA[i]) +
                                    static_cast<double>(alpha) * static_cast<double>(hostB[i]);
            if (std::fabs(static_cast<double>(hostC[i]) - expected) > 2e-5) {
                char text[160];
                std::snprintf(text, sizeof(text), "%s: i=%u got=%.9g want=%.9g", label, i,
                              hostC[i], expected);
                hcstest::reportFailure(__FILE__, __LINE__, label, text);
                return false;
            }
        }
        return true;
    }

    void destroy()
    {
        if (a) hcsBufferDestroy(a);
        if (b) hcsBufferDestroy(b);
        if (c) hcsBufferDestroy(c);
    }
};

} // namespace

HCS_TEST(flow_async_dispatches_and_event_wait)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_VULKAN)) {
        hcstest::reportSkip("no Vulkan device available");
        return;
    }
    VecAddCase data;
    data.context = session.context;
    if (!data.create("flow_async_dispatches_and_event_wait")) {
        return;
    }

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    hcsFlow flow = nullptr;
    HCS_REQUIRE_OK(hcsFlowCreate(session.context, nullptr, &flow));
    hcsEvent event = nullptr;
    HCS_REQUIRE_OK(hcsEventCreate(session.context, &event));

    const float lastAlpha = 1.0f + 7.0f;
    for (uint32_t i = 0; i < 8; ++i) {
        const auto desc = data.desc(1.0f + static_cast<float>(i));
        HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, flow));
    }
    HCS_REQUIRE_OK(hcsEventRecord(event, flow));
    /* The event observes fence status directly; no explicit synchronize is needed. */
    HCS_REQUIRE_OK(hcsEventWait(event, 10ull * 1000ull * 1000ull * 1000ull));
    HCS_REQUIRE_OK(hcsFlowSynchronize(flow, 10ull * 1000ull * 1000ull * 1000ull));
    CHECK(data.verify(lastAlpha, "flow_async_dispatches_and_event_wait"));
    hcsEventDestroy(event);
    hcsFlowDestroy(flow);
    hcsKernelDestroy(kernel);
    data.destroy();
}

HCS_TEST(flow_two_flows_are_independent)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_VULKAN)) {
        hcstest::reportSkip("no Vulkan device available");
        return;
    }

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    hcsFlow left = nullptr, right = nullptr;
    HCS_REQUIRE_OK(hcsFlowCreate(session.context, nullptr, &left));
    HCS_REQUIRE_OK(hcsFlowCreate(session.context, nullptr, &right));

    VecAddCase dataL, dataR;
    dataL.context = session.context;
    dataR.context = session.context;
    CHECK(dataL.create("flow_two_flows_left"));
    CHECK(dataR.create("flow_two_flows_right"));

    const float alphaL = 2.5f;
    const float alphaR = -0.75f;
    const auto descL = dataL.desc(alphaL);
    const auto descR = dataR.desc(alphaR);
    HCS_REQUIRE_OK(hcsDispatch(kernel, &descL, left));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &descR, right));
    /* Synchronize in the opposite order from submission. */
    HCS_REQUIRE_OK(hcsFlowSynchronize(right, UINT64_MAX));
    HCS_REQUIRE_OK(hcsFlowSynchronize(left, UINT64_MAX));
    CHECK(dataL.verify(alphaL, "flow_two_flows_left"));
    CHECK(dataR.verify(alphaR, "flow_two_flows_right"));

    hcsFlowDestroy(left);
    hcsFlowDestroy(right);
    hcsKernelDestroy(kernel);
    dataL.destroy();
    dataR.destroy();
}

HCS_TEST(flow_event_timeout_and_validation)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_VULKAN)) {
        hcstest::reportSkip("no Vulkan device available");
        return;
    }
    hcsFlow flow = nullptr;
    HCS_REQUIRE_OK(hcsFlowCreate(session.context, nullptr, &flow));
    hcsEvent event = nullptr;
    HCS_REQUIRE_OK(hcsEventCreate(session.context, &event));

    CHECK_EQ(hcsEventWait(event, 0), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcsEventRecord(event, nullptr), HCS_E_INVALID_ARGUMENT);
    /* A marker on an idle flow is already satisfied. */
    HCS_REQUIRE_OK(hcsEventRecord(event, flow));
    HCS_REQUIRE_OK(hcsEventWait(event, 0));
    CHECK_EQ(hcsFlowSynchronize(nullptr, 0), HCS_E_INVALID_ARGUMENT);

    hcsEventDestroy(event);
    hcsFlowDestroy(flow);
}

HCS_TEST(flow_cpu_backend_completes_immediately)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_CPU)) {
        hcstest::reportSkip("no CPU device available");
        return;
    }
    VecAddCase data;
    data.context = session.context;
    CHECK(data.create("flow_cpu_backend_completes_immediately"));

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    hcsFlow flow = nullptr;
    HCS_REQUIRE_OK(hcsFlowCreate(session.context, nullptr, &flow));
    hcsEvent event = nullptr;
    HCS_REQUIRE_OK(hcsEventCreate(session.context, &event));

    const float alpha = 3.25f;
    const auto desc = data.desc(alpha);
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, flow));
    HCS_REQUIRE_OK(hcsEventRecord(event, flow));
    HCS_REQUIRE_OK(hcsEventWait(event, 0)); /* CPU dispatch is synchronous */
    HCS_REQUIRE_OK(hcsFlowSynchronize(flow, 0));
    CHECK(data.verify(alpha, "flow_cpu_backend_completes_immediately"));

    hcsEventDestroy(event);
    hcsFlowDestroy(flow);
    hcsKernelDestroy(kernel);
    data.destroy();
}

namespace {

class NeverCompletesFlow final : public hcs::BackendFlow {
public:
    hcsResult synchronize(uint64_t) override { return HCS_E_TIMEOUT; }
    uint64_t completed() const override { return 0; }
};

} // namespace

HCS_TEST(flow_event_wait_timeout_path)
{
    /* Unit-level timeout proof without depending on GPU scheduling: a backend flow whose
     * completion counter never advances must produce HCS_E_TIMEOUT. */
    hcs_context_t context;
    hcs_flow_t flow;
    flow.context = &context;
    flow.impl = std::make_unique<NeverCompletesFlow>();
    hcs_event_t event;
    event.context = &context;
    event.flow = &flow;
    event.target = 1;
    event.recorded = true;
    CHECK_EQ(hcsEventWait(&event, 1'000'000), HCS_E_TIMEOUT);
}
