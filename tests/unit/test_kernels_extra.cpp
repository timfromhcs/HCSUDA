#include "test_framework.h"
#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {

void checkScan(uint32_t backend, uint32_t n, uint32_t groups, const char* label)
{
    testutil::DeviceSession session;
    if (!session.open(backend)) {
        hcstest::reportSkip("device not available");
        return;
    }
    std::vector<float> input(n), output(n, 0.0f);
    testutil::fillPattern(input, 2.0f, 61);
    hcsBuffer in = nullptr, out = nullptr;
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), input.data(), &in));
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), nullptr, &out));

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "scan_f32", &kernel));
    testutil::PushConsts push{};
    push.u0 = n;
    /* The second push slot is read as u32 by the shader. */
    std::memcpy(&push.f0, &groups, sizeof(groups));
    auto desc = testutil::makeDispatch({in, out}, groups, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    HCS_REQUIRE_OK(hcsBufferDownload(out, 0, output.data(), n * sizeof(float)));

    const uint64_t chunk = (static_cast<uint64_t>(n) + groups - 1) / groups;
    for (uint32_t g = 0; g < groups; ++g) {
        const uint64_t first = static_cast<uint64_t>(g) * chunk;
        const uint64_t last = std::min<uint64_t>(n, first + chunk);
        double prefix = 0.0;
        for (uint64_t i = first; i < last; ++i) {
            if (std::fabs(static_cast<double>(output[i]) - prefix) > 5e-4) {
                char text[160];
                std::snprintf(text, sizeof(text), "i=%llu got=%.9g want=%.9g group=%u",
                              static_cast<unsigned long long>(i), output[i], prefix, g);
                hcstest::reportFailure(__FILE__, __LINE__, label, text);
                hcsBufferDestroy(in);
                hcsBufferDestroy(out);
                hcsKernelDestroy(kernel);
                return;
            }
            prefix += input[i];
        }
    }
    hcsBufferDestroy(in);
    hcsBufferDestroy(out);
    hcsKernelDestroy(kernel);
}

void checkTranspose(uint32_t backend, uint32_t rows, uint32_t cols, const char* label)
{
    testutil::DeviceSession session;
    if (!session.open(backend)) {
        hcstest::reportSkip("device not available");
        return;
    }
    const size_t count = static_cast<size_t>(rows) * cols;
    std::vector<float> input(count), output(count, 0.0f);
    testutil::fillPattern(input, 1.5f, 73);
    hcsBuffer in = nullptr, out = nullptr;
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, count * sizeof(float), input.data(), &in));
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, count * sizeof(float), nullptr, &out));

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "transpose_f32", &kernel));
    testutil::PushConsts push{};
    push.u0 = rows;
    std::memcpy(&push.f0, &cols, sizeof(cols));
    auto desc = testutil::makeDispatch({in, out}, (cols + 15) / 16, &push, sizeof(push));
    desc.group_count_y = (rows + 15) / 16;
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));
    HCS_REQUIRE_OK(hcsBufferDownload(out, 0, output.data(), count * sizeof(float)));

    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t col = 0; col < cols; ++col) {
            const float expected = input[static_cast<size_t>(row) * cols + col];
            const float actual = output[static_cast<size_t>(col) * rows + row];
            if (actual != expected) {
                char text[160];
                std::snprintf(text, sizeof(text), "row=%u col=%u got=%.9g want=%.9g", row, col,
                              actual, expected);
                hcstest::reportFailure(__FILE__, __LINE__, label, text);
                hcsBufferDestroy(in);
                hcsBufferDestroy(out);
                hcsKernelDestroy(kernel);
                return;
            }
        }
    }
    hcsBufferDestroy(in);
    hcsBufferDestroy(out);
    hcsKernelDestroy(kernel);
}

} // namespace

HCS_TEST(scan_cpu_odd_size_matches_reference)
{
    checkScan(HCS_BACKEND_CPU, 10000, 64, "scan_cpu_odd_size_matches_reference");
}

HCS_TEST(scan_vulkan_odd_size_matches_reference)
{
    checkScan(HCS_BACKEND_VULKAN, 10000, 64, "scan_vulkan_odd_size_matches_reference");
}

HCS_TEST(scan_single_element)
{
    checkScan(HCS_BACKEND_CPU, 1, 1, "scan_single_element");
    checkScan(HCS_BACKEND_VULKAN, 1, 1, "scan_single_element_vulkan");
}

HCS_TEST(scan_chunk_larger_than_workgroup)
{
    /* chunk=2500 forces the GPU shader through ten 256-element blocks. */
    checkScan(HCS_BACKEND_CPU, 10000, 4, "scan_large_chunk_cpu");
    checkScan(HCS_BACKEND_VULKAN, 10000, 4, "scan_large_chunk_vulkan");
}

HCS_TEST(transpose_cpu_odd_dimensions)
{
    checkTranspose(HCS_BACKEND_CPU, 37, 29, "transpose_cpu_odd_dimensions");
}

HCS_TEST(transpose_vulkan_odd_dimensions)
{
    checkTranspose(HCS_BACKEND_VULKAN, 37, 29, "transpose_vulkan_odd_dimensions");
}

HCS_TEST(transpose_single_element_and_tile_edge)
{
    checkTranspose(HCS_BACKEND_CPU, 1, 1, "transpose_single");
    checkTranspose(HCS_BACKEND_VULKAN, 17, 16, "transpose_tile_edge");
}

void checkReduceFinal(uint32_t backend, uint32_t groups, const char* label)
{
    testutil::DeviceSession session;
    if (!session.open(backend)) {
        hcstest::reportSkip("device not available");
        return;
    }
    std::vector<float> partials(groups, 0.0f);
    testutil::fillPattern(partials, 3.0f, 7);
    double expected = 0.0;
    for (float value : partials) {
        expected += static_cast<double>(value);
    }
    hcsBuffer in = nullptr, out = nullptr;
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, groups * sizeof(float),
                                       partials.data(), &in));
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, sizeof(float), nullptr, &out));
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "reduce_final_f32", &kernel));
    testutil::PushConsts push{};
    push.u0 = groups;
    auto desc = testutil::makeDispatch({in, out}, 1, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));

    float actual = 0.0f;
    HCS_REQUIRE_OK(hcsBufferDownload(out, 0, &actual, sizeof(actual)));
    const double tolerance = 2e-4 * std::max(1.0, std::fabs(expected));
    if (std::fabs(static_cast<double>(actual) - expected) > tolerance) {
        char text[160];
        std::snprintf(text, sizeof(text), "got=%.9g want=%.9g groups=%u",
                      static_cast<double>(actual), expected, groups);
        hcstest::reportFailure(__FILE__, __LINE__, label, text);
    }
    hcsBufferDestroy(in);
    hcsBufferDestroy(out);
    hcsKernelDestroy(kernel);
}

HCS_TEST(reduce_final_cpu_matches_reference)
{
    checkReduceFinal(HCS_BACKEND_CPU, 37, "reduce_final_cpu_matches_reference");
}

HCS_TEST(reduce_final_vulkan_matches_reference)
{
    checkReduceFinal(HCS_BACKEND_VULKAN, 37, "reduce_final_vulkan_matches_reference");
}

void checkGlobalScan(uint32_t backend, uint32_t n, uint32_t groups, const char* label)
{
    testutil::DeviceSession session;
    if (!session.open(backend)) {
        hcstest::reportSkip("device not available");
        return;
    }
    std::vector<float> input(n), local(n, 0.0f), totals(groups, 0.0f);
    std::vector<float> offsets(groups, 0.0f), result(n, 0.0f);
    testutil::fillPattern(input, 2.0f, 61);

    hcsBuffer in = nullptr, loc = nullptr, tot = nullptr, off = nullptr, out = nullptr;
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), input.data(), &in));
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), nullptr, &loc));
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, groups * sizeof(float), nullptr, &tot));
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, groups * sizeof(float), nullptr, &off));
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(float), nullptr, &out));

    testutil::ReducePush push{n, groups, 0, 0};
    hcsKernel scan = nullptr, reduce = nullptr, add = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "scan_f32", &scan));
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "reduce_sum_f32", &reduce));
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "scan_add_offsets_f32", &add));

    auto localDesc = testutil::makeDispatch({in, loc}, groups, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(scan, &localDesc, nullptr));
    auto totalDesc = testutil::makeDispatch({in, tot}, groups, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(reduce, &totalDesc, nullptr));
    testutil::ReducePush offsetPush{groups, 1, 0, 0};
    auto offsetDesc = testutil::makeDispatch({tot, off}, 1, &offsetPush, sizeof(offsetPush));
    HCS_REQUIRE_OK(hcsDispatch(scan, &offsetDesc, nullptr));
    auto finalDesc = testutil::makeDispatch({loc, off, out}, groups, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(add, &finalDesc, nullptr));

    HCS_REQUIRE_OK(hcsBufferDownload(out, 0, result.data(), n * sizeof(float)));
    double prefix = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        const double want = prefix;
        prefix += static_cast<double>(input[i]);
        if (std::fabs(static_cast<double>(result[i]) - want) >
            8e-4 * std::max(1.0, std::fabs(want))) {
            char text[160];
            std::snprintf(text, sizeof(text), "i=%u got=%.9g want=%.9g", i,
                          static_cast<double>(result[i]), want);
            hcstest::reportFailure(__FILE__, __LINE__, label, text);
            break;
        }
    }

    hcsKernelDestroy(scan);
    hcsKernelDestroy(reduce);
    hcsKernelDestroy(add);
    hcsBufferDestroy(in);
    hcsBufferDestroy(loc);
    hcsBufferDestroy(tot);
    hcsBufferDestroy(off);
    hcsBufferDestroy(out);
}

HCS_TEST(global_scan_cpu_matches_reference)
{
    checkGlobalScan(HCS_BACKEND_CPU, 10001, 64, "global_scan_cpu_matches_reference");
}

HCS_TEST(global_scan_vulkan_matches_reference)
{
    checkGlobalScan(HCS_BACKEND_VULKAN, 10001, 64, "global_scan_vulkan_matches_reference");
}

void checkHistogram(uint32_t backend, uint32_t n, const char* label)
{
    testutil::DeviceSession session;
    if (!session.open(backend)) {
        hcstest::reportSkip("device not available");
        return;
    }
    std::vector<uint32_t> input(n);
    std::vector<uint32_t> expected(256, 0);
    for (uint32_t i = 0; i < n; ++i) {
        input[i] = (i * 37u + 11u) & 255u;
        ++expected[input[i]];
    }
    hcsBuffer in = nullptr, out = nullptr;
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, n * sizeof(uint32_t), input.data(), &in));
    std::vector<uint32_t> zeros(256, 0);
    HCS_REQUIRE_OK(testutil::makeBuffer(session.context, zeros.size() * sizeof(uint32_t),
                                       zeros.data(), &out));
    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(session.context, "histogram_u32", &kernel));
    testutil::ReducePush push{n, 0, 0, 0};
    auto desc = testutil::makeDispatch({in, out}, 1, &push, sizeof(push));
    HCS_REQUIRE_OK(hcsDispatch(kernel, &desc, nullptr));

    std::vector<uint32_t> actual(256, 0);
    HCS_REQUIRE_OK(hcsBufferDownload(out, 0, actual.data(), actual.size() * sizeof(uint32_t)));
    for (uint32_t bin = 0; bin < 256; ++bin) {
        if (actual[bin] != expected[bin]) {
            char text[160];
            std::snprintf(text, sizeof(text), "bin=%u got=%u want=%u", bin, actual[bin],
                          expected[bin]);
            hcstest::reportFailure(__FILE__, __LINE__, label, text);
            break;
        }
    }
    hcsKernelDestroy(kernel);
    hcsBufferDestroy(in);
    hcsBufferDestroy(out);
}

HCS_TEST(histogram_cpu_matches_reference)
{
    checkHistogram(HCS_BACKEND_CPU, 5000, "histogram_cpu_matches_reference");
}

HCS_TEST(histogram_vulkan_matches_reference)
{
    checkHistogram(HCS_BACKEND_VULKAN, 5000, "histogram_vulkan_matches_reference");
}

HCS_TEST(histogram_rejects_multiple_workgroups)
{
    for (uint32_t backend : {HCS_BACKEND_CPU, HCS_BACKEND_VULKAN}) {
        testutil::DeviceSession session;
        if (!session.open(backend)) {
            continue; /* CPU-only builds have no Vulkan device */
        }
        std::vector<uint32_t> values(16, 7u);
        std::vector<uint32_t> bins(256, 0u);
        hcsBuffer in = nullptr, out = nullptr;
        HCS_REQUIRE_OK(testutil::makeBuffer(session.context, values.size() * sizeof(uint32_t),
                                            values.data(), &in));
        HCS_REQUIRE_OK(testutil::makeBuffer(session.context, bins.size() * sizeof(uint32_t),
                                            bins.data(), &out));
        hcsKernel kernel = nullptr;
        HCS_REQUIRE_OK(hcsKernelCreate(session.context, "histogram_u32", &kernel));
        testutil::ReducePush push{static_cast<uint32_t>(values.size()), 0, 0, 0};
        auto desc = testutil::makeDispatch({in, out}, 2, &push, sizeof(push));
        CHECK_EQ(hcsDispatch(kernel, &desc, nullptr), HCS_E_INVALID_ARGUMENT);
        hcsKernelDestroy(kernel);
        hcsBufferDestroy(in);
        hcsBufferDestroy(out);
    }
}
