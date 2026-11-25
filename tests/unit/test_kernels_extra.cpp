#include "test_framework.h"
#include "test_helpers.h"

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
