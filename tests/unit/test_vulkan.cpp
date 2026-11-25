#include "hcsuda/hcsuda.hpp"
#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace testutil;

namespace {

constexpr uint32_t kVulkan = HCS_BACKEND_VULKAN;

bool openVulkan(DeviceSession* session)
{
    if (!session->open(kVulkan)) {
        std::printf("  (no Vulkan device or context available)\n");
        return false;
    }
    return true;
}

hcsBuffer upload(hcsContext context, const std::vector<float>& data)
{
    hcsBuffer buffer = nullptr;
    if (makeBuffer(context, data.size() * sizeof(float), data.data(), &buffer) != HCS_SUCCESS) {
        return nullptr;
    }
    return buffer;
}

std::vector<float> download(hcsBuffer buffer, size_t count)
{
    std::vector<float> out(count, 0.0f);
    if (hcsBufferDownload(buffer, 0, out.data(), out.size() * sizeof(float)) != HCS_SUCCESS) {
        out.clear();
    }
    return out;
}

} // namespace

HCS_TEST(vulkan_device_report_is_truthful)
{
    DeviceSession session;
    if (!openVulkan(&session)) {
        return;
    }
    hcsDeviceCaps caps{};
    caps.struct_size = sizeof(caps);
    HCS_REQUIRE_OK(hcsGetDeviceCaps(session.device, &caps));
    CHECK_MSG((caps.capabilities & HCS_CAP_BASE_COMPUTE) != 0, "base compute must be set");
    CHECK_MSG(caps.max_workgroup_invocations >= 64, "workgroup limit looks invalid");
    CHECK_MSG(caps.max_storage_buffer_range > 0, "storage buffer range must be positive");

    hcsMemoryBudget budget{};
    budget.struct_size = sizeof(budget);
    HCS_REQUIRE_OK(hcsGetMemoryBudget(session.device, &budget));
    CHECK_MSG(budget.heap_bytes > 0, "device-local heap size must be reported");
    CHECK_MSG(budget.budget_bytes > 0, "memory budget must be reported");
    std::printf("  device heap %.1f MiB, budget %.1f MiB, used %.1f MiB\n",
                static_cast<double>(budget.heap_bytes) / (1024.0 * 1024.0),
                static_cast<double>(budget.budget_bytes) / (1024.0 * 1024.0),
                static_cast<double>(budget.usage_bytes) / (1024.0 * 1024.0));
}

HCS_TEST(vulkan_vecadd_matches_cpu_reference)
{
    DeviceSession session;
    if (!openVulkan(&session)) {
        return;
    }
    const uint32_t n = 1u << 16;
    std::vector<float> a(n), b(n), expected(n);
    fillPattern(a, 10.0f, 21);
    fillPattern(b, 4.0f, 22);
    const float alpha = 1.25f;
    for (uint32_t i = 0; i < n; ++i) {
        expected[i] = static_cast<float>(static_cast<double>(a[i]) +
                                         static_cast<double>(alpha) * b[i]);
    }

    hcsBuffer bufA = upload(session.context, a);
    hcsBuffer bufB = upload(session.context, b);
    hcsBuffer bufC = upload(session.context, std::vector<float>(n, 0.0f));
    CHECK(bufA && bufB && bufC);
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    PushConsts push{n, alpha, 0, 0};
    auto desc = makeDispatch({bufA, bufB, bufC}, (n + 63) / 64, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));

    double worst = 0.0;
    CHECK_MSG(approxEqual(expected, download(bufC, n), 1e-5, 1e-6, &worst),
              "Vulkan vecadd diverged from the reference");
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(bufA);
    hcsBufferDestroy(bufB);
    hcsBufferDestroy(bufC);
}

HCS_TEST(vulkan_reduce_matches_cpu_reference)
{
    DeviceSession session;
    if (!openVulkan(&session)) {
        return;
    }
    const uint32_t n = 100000;
    const uint32_t groups = 64;
    std::vector<float> x(n);
    fillPattern(x, 2.0f, 31);
    const uint32_t chunk = (n + groups - 1) / groups;
    std::vector<double> expected(groups, 0.0);
    for (uint32_t g = 0; g < groups; ++g) {
        const uint32_t first = g * chunk;
        const uint32_t last = std::min(n, first + chunk);
        double sum = 0.0;
        for (uint32_t i = first; i < last; ++i) {
            sum += x[i];
        }
        expected[g] = sum;
    }

    hcsBuffer in = upload(session.context, x);
    std::vector<float> zeros(groups, 0.0f);
    hcsBuffer out = upload(session.context, zeros);
    CHECK(in && out);
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "reduce_sum_f32", &kernel));
    ReducePush push{n, groups, 0, 0};
    auto desc = makeDispatch({in, out}, groups, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    const std::vector<float> actual = download(out, groups);
    CHECK_EQ(actual.size(), static_cast<size_t>(groups));
    for (uint32_t g = 0; g < groups; ++g) {
        CHECK_NEAR(actual[g], expected[g], std::fabs(expected[g]) * 2e-4 + 1e-3);
    }
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(in);
    hcsBufferDestroy(out);
}

HCS_TEST(vulkan_matmul_matches_cpu_reference)
{
    DeviceSession session;
    if (!openVulkan(&session)) {
        return;
    }
    const uint32_t m = 64, n = 64, k = 64;
    std::vector<float> a(static_cast<size_t>(m) * k), b(static_cast<size_t>(k) * n);
    fillPattern(a, 2.0f, 41);
    fillPattern(b, 2.0f, 42);
    std::vector<float> expected(static_cast<size_t>(m) * n, 0.0f);
    for (uint32_t i = 0; i < m; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            double acc = 0.0;
            for (uint32_t kk = 0; kk < k; ++kk) {
                acc += static_cast<double>(a[i * k + kk]) *
                       static_cast<double>(b[kk * n + j]);
            }
            expected[i * n + j] = static_cast<float>(acc);
        }
    }

    hcsBuffer bufA = upload(session.context, a);
    hcsBuffer bufB = upload(session.context, b);
    hcsBuffer bufC = upload(session.context, std::vector<float>(expected.size(), 0.0f));
    CHECK(bufA && bufB && bufC);
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "matmul_f32", &kernel));
    MatMulPush push{m, n, k, 0};
    auto desc = makeDispatch({bufA, bufB, bufC}, (n + 15) / 16, &push, sizeof(push));
    desc.group_count_y = (m + 15) / 16;
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    double worst = 0.0;
    CHECK_MSG(approxEqual(expected, download(bufC, expected.size()), 1e-4, 1e-3, &worst),
              "Vulkan matmul diverged from the reference");
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(bufA);
    hcsBufferDestroy(bufB);
    hcsBufferDestroy(bufC);
}

HCS_TEST(vulkan_memory_cap_is_enforced)
{
    /* 4 MiB cap: a 16 MiB allocation must fail cleanly, not exhaust the UMA heap. */
    DeviceSession capped;
    if (!capped.open(kVulkan, 4ull << 20)) {
        return;
    }
    hcsBufferDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.size = 16ull << 20;
    hcsBuffer buffer = nullptr;
    CHECK_EQ(hcsBufferCreate(capped.context, &desc, &buffer), HCS_E_OUT_OF_MEMORY);
    CHECK(buffer == nullptr);
}
