#include "test_framework.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct GraphCase {
    hcsContext context = nullptr;
    hcsBuffer a = nullptr, b = nullptr, c = nullptr, d = nullptr;
    std::vector<float> hostA, hostB, hostC, hostD;
    uint32_t n = 1024;

    bool create(const char* label)
    {
        hostA.resize(n);
        hostB.resize(n);
        hostC.resize(n, 0.0f);
        hostD.resize(n, 0.0f);
        testutil::fillPattern(hostA, 1.0f, 41);
        testutil::fillPattern(hostB, 1.0f, 42);
        const uint64_t bytes = static_cast<uint64_t>(n) * sizeof(float);
        if (testutil::makeBuffer(context, bytes, hostA.data(), &a) != HCS_SUCCESS ||
            testutil::makeBuffer(context, bytes, hostB.data(), &b) != HCS_SUCCESS ||
            testutil::makeBuffer(context, bytes, nullptr, &c) != HCS_SUCCESS ||
            testutil::makeBuffer(context, bytes, nullptr, &d) != HCS_SUCCESS) {
            hcstest::reportFailure(__FILE__, __LINE__, label, "buffer creation failed");
            return false;
        }
        return true;
    }

    hcsDispatchDesc dispatch(hcsBuffer output, hcsBuffer input, float alpha) const
    {
        testutil::PushConsts push{};
        push.u0 = n;
        push.f0 = alpha;
        return testutil::makeDispatch({input, b, output}, n / 64, &push, sizeof(push));
    }

    bool verify(const char* label)
    {
        if (hcsBufferDownload(c, 0, hostC.data(), n * sizeof(float)) != HCS_SUCCESS ||
            hcsBufferDownload(d, 0, hostD.data(), n * sizeof(float)) != HCS_SUCCESS) {
            hcstest::reportFailure(__FILE__, __LINE__, label, "download failed");
            return false;
        }
        for (uint32_t i = 0; i < n; ++i) {
            const double first = static_cast<double>(hostA[i]) + 1.5 * static_cast<double>(hostB[i]);
            const double second = first + 0.25 * static_cast<double>(hostB[i]);
            if (std::fabs(static_cast<double>(hostC[i]) - first) > 2e-5 ||
                std::fabs(static_cast<double>(hostD[i]) - second) > 4e-5) {
                char text[192];
                std::snprintf(text, sizeof(text), "i=%u c=%.9g/%.9g d=%.9g/%.9g", i,
                              hostC[i], first, hostD[i], second);
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
        if (d) hcsBufferDestroy(d);
    }
};

void runGraphCase(uint32_t backend, const char* label)
{
    testutil::DeviceSession session;
    if (!session.open(backend)) {
        hcstest::reportSkip("device not available");
        return;
    }
    GraphCase data;
    data.context = session.context;
    CHECK(data.create(label));

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    hcsFlow flow = nullptr;
    HCS_REQUIRE_OK(hcsFlowCreate(session.context, nullptr, &flow));
    hcsGraph graph = nullptr;
    HCS_REQUIRE_OK(hcsGraphCreate(session.context, &graph));

    const auto first = data.dispatch(data.c, data.a, 1.5f);
    hcsGraphNodeDesc node0{};
    node0.struct_size = sizeof(node0);
    node0.kernel = kernel;
    node0.dispatch = &first;
    HCS_REQUIRE_OK(hcsGraphAddNode(graph, &node0));

    const auto second = data.dispatch(data.d, data.c, 0.25f);
    hcsGraphNodeDesc node1{};
    node1.struct_size = sizeof(node1);
    node1.kernel = kernel;
    node1.dispatch = &second;
    node1.dependency_count = 1;
    node1.dependencies[0] = 0;
    HCS_REQUIRE_OK(hcsGraphAddNode(graph, &node1));

    HCS_REQUIRE_OK(hcsGraphCompile(graph));
    HCS_REQUIRE_OK(hcsGraphRun(graph, flow));
    HCS_REQUIRE_OK(hcsFlowSynchronize(flow, UINT64_MAX));
    CHECK(data.verify(label));

    hcsGraphDestroy(graph);
    hcsFlowDestroy(flow);
    hcsKernelDestroy(kernel);
    data.destroy();
}

} // namespace

HCS_TEST(graph_runs_on_cpu_backend)
{
    runGraphCase(HCS_BACKEND_CPU, "graph_runs_on_cpu_backend");
}

HCS_TEST(graph_runs_on_vulkan_backend)
{
    runGraphCase(HCS_BACKEND_VULKAN, "graph_runs_on_vulkan_backend");
}

HCS_TEST(graph_rejects_invalid_dependencies)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_CPU)) {
        hcstest::reportSkip("no CPU device available");
        return;
    }
    GraphCase data;
    data.context = session.context;
    CHECK(data.create("graph_rejects_invalid_dependencies"));
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));

    hcsGraph graph = nullptr;
    HCS_REQUIRE_OK(hcsGraphCreate(session.context, &graph));
    CHECK_EQ(hcsGraphCompile(graph), HCS_E_INVALID_ARGUMENT); /* empty */
    CHECK_EQ(hcsGraphRun(graph, nullptr), HCS_E_INVALID_ARGUMENT);

    const auto desc = data.dispatch(data.c, data.a, 1.0f);
    hcsGraphNodeDesc future{};
    future.struct_size = sizeof(future);
    future.kernel = kernel;
    future.dispatch = &desc;
    future.dependency_count = 1;
    future.dependencies[0] = 1; /* refers to a node that does not exist yet */
    CHECK_EQ(hcsGraphAddNode(graph, &future), HCS_E_INVALID_ARGUMENT);

    hcsGraphNodeDesc badGroup{};
    badGroup.struct_size = sizeof(badGroup);
    badGroup.kernel = kernel;
    auto invalidDesc = desc;
    invalidDesc.group_count_x = 0;
    badGroup.dispatch = &invalidDesc;
    CHECK_EQ(hcsGraphAddNode(graph, &badGroup), HCS_E_INVALID_ARGUMENT);

    hcsGraphNodeDesc valid{};
    valid.struct_size = sizeof(valid);
    valid.kernel = kernel;
    valid.dispatch = &desc;
    CHECK_EQ(hcsGraphAddNode(graph, &valid), HCS_SUCCESS);
    HCS_REQUIRE_OK(hcsGraphCompile(graph));

    hcsGraphNodeDesc after{};
    after.struct_size = sizeof(after);
    after.kernel = kernel;
    after.dispatch = &desc;
    after.dependency_count = 9;
    CHECK_EQ(hcsGraphAddNode(graph, &after), HCS_E_INVALID_ARGUMENT);

    hcsGraphDestroy(graph);
    hcsKernelDestroy(kernel);
    data.destroy();
}

HCS_TEST(graph_add_node_validates_context)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_CPU)) {
        hcstest::reportSkip("no CPU device available");
        return;
    }
    hcsGraph graph = nullptr;
    HCS_REQUIRE_OK(hcsGraphCreate(session.context, &graph));
    hcsGraphNodeDesc node{};
    node.struct_size = sizeof(node);
    CHECK_EQ(hcsGraphAddNode(graph, &node), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcsGraphAddNode(nullptr, &node), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcsGraphCompile(nullptr), HCS_E_INVALID_ARGUMENT);
    hcsGraphDestroy(graph);
}
