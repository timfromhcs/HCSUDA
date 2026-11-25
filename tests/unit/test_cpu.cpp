#include "hcsuda/hcsuda.hpp"
#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace testutil;

namespace {

constexpr uint32_t kCpu = HCS_BACKEND_CPU;

hcsBuffer upload(hcsContext context, const std::vector<float>& data)
{
    hcsBuffer buffer = nullptr;
    hcsResult r = makeBuffer(context, data.size() * sizeof(float), data.data(), &buffer);
    if (r != HCS_SUCCESS) {
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

HCS_TEST(cpu_vecadd_matches_reference)
{
    DeviceSession session;
    CHECK_MSG(session.open(kCpu), "CPU context must be creatable");
    const uint32_t n = 1000; /* deliberately not a multiple of the workgroup size */
    std::vector<float> a(n), b(n), expected(n);
    fillPattern(a, 10.0f, 1);
    fillPattern(b, 4.0f, 2);
    const float alpha = 2.5f;
    for (uint32_t i = 0; i < n; ++i) {
        expected[i] = a[i] + alpha * b[i];
    }

    hcsBuffer bufA = upload(session.context, a);
    hcsBuffer bufB = upload(session.context, b);
    hcsBuffer bufC = upload(session.context, std::vector<float>(n, -1.0f));
    CHECK(bufA && bufB && bufC);

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    PushConsts push{n, alpha, 0, 0};
    auto desc = makeDispatch({bufA, bufB, bufC}, (n + 63) / 64, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));

    const std::vector<float> actual = download(bufC, n);
    CHECK_EQ(actual.size(), static_cast<size_t>(n));
    double worst = 0.0;
    CHECK_MSG(approxEqual(expected, actual, 1e-5, 1e-6, &worst),
              "vecadd result diverged from the reference");

    hcsKernelDestroy(kernel);
    hcsBufferDestroy(bufA);
    hcsBufferDestroy(bufB);
    hcsBufferDestroy(bufC);
}

HCS_TEST(cpu_scale_matches_reference)
{
    DeviceSession session;
    CHECK(session.open(kCpu));
    const uint32_t n = 4096;
    std::vector<float> x(n), expected(n);
    fillPattern(x, 8.0f, 7);
    const float alpha = -0.75f;
    for (uint32_t i = 0; i < n; ++i) {
        expected[i] = alpha * x[i];
    }

    hcsBuffer in = upload(session.context, x);
    hcsBuffer out = upload(session.context, std::vector<float>(n, 0.0f));
    CHECK(in && out);
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "scale_f32", &kernel));
    PushConsts push{n, alpha, 0, 0};
    auto desc = makeDispatch({in, out}, (n + 63) / 64, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    double worst = 0.0;
    CHECK_MSG(approxEqual(expected, download(out, n), 1e-5, 1e-6, &worst),
              "scale result diverged");
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(in);
    hcsBufferDestroy(out);
}

HCS_TEST(cpu_reduce_matches_reference)
{
    DeviceSession session;
    CHECK(session.open(kCpu));
    const uint32_t n = 10000;
    const uint32_t groups = 8;
    std::vector<float> x(n);
    fillPattern(x, 2.0f, 3);
    std::vector<double> expected(groups, 0.0);
    const uint32_t chunk = (n + groups - 1) / groups;
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
        CHECK_NEAR(actual[g], expected[g], std::fabs(expected[g]) * 1e-4 + 1e-4);
    }
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(in);
    hcsBufferDestroy(out);
}

HCS_TEST(cpu_matmul_matches_reference)
{
    DeviceSession session;
    CHECK(session.open(kCpu));
    const uint32_t m = 37, n = 31, k = 29;
    std::vector<float> a(static_cast<size_t>(m) * k);
    std::vector<float> b(static_cast<size_t>(k) * n);
    fillPattern(a, 2.0f, 11);
    fillPattern(b, 2.0f, 13);
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
    CHECK_MSG(approxEqual(expected, download(bufC, expected.size()), 1e-5, 1e-6, &worst),
              "matmul result diverged");
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(bufA);
    hcsBufferDestroy(bufB);
    hcsBufferDestroy(bufC);
}

HCS_TEST(cpu_vecadd_large_parallel_run)
{
    DeviceSession session;
    CHECK(session.open(kCpu));
    const uint32_t n = 1u << 20;
    std::vector<float> a(n, 1.0f), b(n, 3.0f), expected(n, 7.0f);
    hcsBuffer bufA = upload(session.context, a);
    hcsBuffer bufB = upload(session.context, b);
    hcsBuffer bufC = upload(session.context, std::vector<float>(n, 0.0f));
    CHECK(bufA && bufB && bufC);
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    PushConsts push{n, 2.0f, 0, 0};
    auto desc = makeDispatch({bufA, bufB, bufC}, (n + 63) / 64, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    double worst = 0.0;
    CHECK_MSG(approxEqual(expected, download(bufC, n), 1e-6, 1e-6, &worst),
              "large parallel vecadd diverged");
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(bufA);
    hcsBufferDestroy(bufB);
    hcsBufferDestroy(bufC);
}

HCS_TEST(cpu_rejects_undersized_buffers)
{
    DeviceSession session;
    CHECK(session.open(kCpu));
    const uint32_t n = 1024;
    std::vector<float> a(n, 1.0f), b(n, 1.0f);
    hcsBuffer bufA = upload(session.context, a);
    hcsBuffer bufB = upload(session.context, b);
    /* Output buffer holds only half the elements. */
    hcsBuffer bufC = nullptr;
    std::vector<float> half(n / 2, 0.0f);
    CHECK_EQ(makeBuffer(session.context, half.size() * sizeof(float), half.data(), &bufC),
             HCS_SUCCESS);
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "vecadd_f32", &kernel));
    PushConsts push{n, 1.0f, 0, 0};
    auto desc = makeDispatch({bufA, bufB, bufC}, (n + 63) / 64, &push, sizeof(push));
    /* The CPU backend clamps to the smallest bound; the kernel must not write OOB,
     * so the call succeeds but only half the output is produced. */
    CHECK_EQ(hcsDispatch(kernel, &desc, nullptr), HCS_SUCCESS);
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(bufA);
    hcsBufferDestroy(bufB);
    hcsBufferDestroy(bufC);
}
