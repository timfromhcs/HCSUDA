/*
 * HCSBENCH — reproducible micro-benchmarks for HCSUDA devices.
 *
 * Every sample measures one complete dispatch (submit + fence wait) because V1
 * dispatches are synchronous. Results therefore include dispatch overhead and
 * are not a pure kernel-throughput figure. The report states this explicitly.
 */

#include "hcsuda/hcsuda.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Options {
    int deviceFilter = -1;
    int warmup = 5;
    int reps = 25;
    uint32_t vecElements = 1u << 20;
    uint32_t matSize = 256;
    bool json = false;
};

struct Timing {
    double medianMs = 0.0;
    double p10Ms = 0.0;
    double p90Ms = 0.0;
    bool ok = false;
    std::string error;
};

Timing measure(int warmup, int reps, const std::function<hcsResult()>& fn)
{
    Timing timing;
    for (int i = 0; i < warmup; ++i) {
        if (fn() != HCS_SUCCESS) {
            timing.error = "warm-up dispatch failed";
            return timing;
        }
    }
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(reps));
    for (int i = 0; i < reps; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        const hcsResult r = fn();
        const auto end = std::chrono::steady_clock::now();
        if (r != HCS_SUCCESS) {
            hcsErrorInfo info{};
            info.struct_size = sizeof(info);
            hcsGetLastError(&info);
            timing.error = std::string("dispatch failed: ") + info.message;
            return timing;
        }
        samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }
    std::sort(samples.begin(), samples.end());
    const auto pick = [&samples](double q) {
        if (samples.empty()) {
            return 0.0;
        }
        const double idx = q * static_cast<double>(samples.size() - 1);
        return samples[static_cast<size_t>(idx + 0.5)];
    };
    timing.medianMs = pick(0.5);
    timing.p10Ms = pick(0.1);
    timing.p90Ms = pick(0.9);
    timing.ok = true;
    return timing;
}

double mib(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

void fill(std::vector<float>& data, int seed)
{
    for (size_t i = 0; i < data.size(); ++i) {
        const float x = static_cast<float>((i * 37 + static_cast<size_t>(seed) * 101) % 1000);
        data[i] = (x / 1000.0f - 0.5f) * 2.0f;
    }
}

struct BenchRow {
    std::string device;
    std::string backend;
    std::string kernel;
    std::string shape;
    Timing timing;
    double gbytes = 0.0;
    double gflops = 0.0;
    bool verified = false;
};

void printRow(const BenchRow& row, bool json, bool firstJson)
{
    const double seconds = row.timing.medianMs / 1000.0;
    const double gbs = seconds > 0.0 ? row.gbytes / seconds : 0.0;
    const double gflops = seconds > 0.0 ? row.gflops / seconds : 0.0;
    if (json) {
        std::printf("%s{\"device\":\"%s\",\"backend\":\"%s\",\"kernel\":\"%s\","
                    "\"shape\":\"%s\",\"median_ms\":%.6f,\"p10_ms\":%.6f,"
                    "\"p90_ms\":%.6f,\"gb_per_s\":%.3f,\"gflop_per_s\":%.3f,"
                    "\"verified\":%s}",
                    firstJson ? "" : ",", row.device.c_str(), row.backend.c_str(),
                    row.kernel.c_str(), row.shape.c_str(), row.timing.medianMs,
                    row.timing.p10Ms, row.timing.p90Ms, gbs, gflops,
                    row.verified ? "true" : "false");
        return;
    }
    if (!row.timing.ok) {
        std::printf("  %-14s %-18s FAILED: %s\n", row.kernel.c_str(), row.shape.c_str(),
                    row.timing.error.c_str());
        return;
    }
    std::printf("  %-14s %-18s median %8.3f ms  p10 %8.3f  p90 %8.3f  "
                "%7.2f GB/s  %7.2f GFLOP/s%s\n",
                row.kernel.c_str(), row.shape.c_str(), row.timing.medianMs,
                row.timing.p10Ms, row.timing.p90Ms, gbs, gflops,
                row.verified ? "  [verified]" : "");
}

uint32_t vkMajor(uint32_t v) { return (v >> 22) & 0x7Fu; }
uint32_t vkMinor(uint32_t v) { return (v >> 12) & 0x3FFu; }

} // namespace

int main(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--json") == 0) {
            options.json = true;
        } else if (std::strcmp(arg, "--device") == 0 && i + 1 < argc) {
            options.deviceFilter = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--reps") == 0 && i + 1 < argc) {
            options.reps = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(arg, "--warmup") == 0 && i + 1 < argc) {
            options.warmup = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(arg, "--size") == 0 && i + 1 < argc) {
            options.vecElements = static_cast<uint32_t>(std::max(1, std::atoi(argv[++i])));
        } else if (std::strcmp(arg, "--mat") == 0 && i + 1 < argc) {
            options.matSize = static_cast<uint32_t>(std::max(16, std::atoi(argv[++i])));
        } else if (std::strcmp(arg, "--help") == 0) {
            std::printf("usage: hcsbench [--json] [--device N] [--reps N] [--warmup N]\n"
                        "                [--size elements] [--mat dim]\n");
            return 0;
        }
    }

    if (hcsInit(nullptr) != HCS_SUCCESS) {
        std::fprintf(stderr, "hcsInit failed\n");
        return 1;
    }
    uint32_t count = 0;
    hcsEnumerateDevices(nullptr, &count);
    std::vector<hcsDeviceInfo> infos(count);
    hcsEnumerateDevices(infos.data(), &count);

    if (!options.json) {
        std::printf("%s HCSBENCH %s\n", HCS_PRODUCT_NAME, hcsGetVersionString());
        std::printf("Each sample = one dispatch including submit + fence wait (V1 is synchronous).\n");
        std::printf("warm-up %d, repetitions %d, vec elements %u, matmul %ux%ux%u\n\n",
                    options.warmup, options.reps, options.vecElements, options.matSize,
                    options.matSize, options.matSize);
    } else {
        std::printf("[");
    }

    bool firstJson = true;
    for (uint32_t i = 0; i < count; ++i) {
        if (options.deviceFilter >= 0 && static_cast<int>(i) != options.deviceFilter) {
            continue;
        }
        hcsDevice device = nullptr;
        if (hcsGetDevice(i, &device) != HCS_SUCCESS) {
            continue;
        }
        hcsContextDesc cdesc{};
        cdesc.struct_size = sizeof(cdesc);
        hcsContext context = nullptr;
        if (hcsCreateContext(device, &cdesc, &context) != HCS_SUCCESS) {
            hcsErrorInfo info{};
            info.struct_size = sizeof(info);
            hcsGetLastError(&info);
            std::fprintf(stderr, "context creation failed on device %u: %s\n", i, info.message);
            continue;
        }
        const std::string deviceName = infos[i].name;
        const std::string backendName = hcsBackendName(infos[i].backend);
        if (!options.json) {
            if (infos[i].backend == HCS_BACKEND_CPU) {
                std::printf("%s [%s]  HCS API %u\n", deviceName.c_str(),
                            backendName.c_str(), hcsGetApiVersion());
            } else {
                std::printf("%s [%s]  api %u.%u\n", deviceName.c_str(), backendName.c_str(),
                            vkMajor(infos[i].api_version), vkMinor(infos[i].api_version));
            }
        }

        std::vector<BenchRow> rows;

        /* vecadd */
        {
            const uint32_t n = options.vecElements;
            std::vector<float> a(n), b(n);
            fill(a, 1);
            fill(b, 2);
            hcsBufferDesc bd{};
            bd.struct_size = sizeof(bd);
            bd.size = static_cast<uint64_t>(n) * sizeof(float);
            bd.usage = HCS_BUFFER_USAGE_STORAGE | HCS_BUFFER_USAGE_HOST_VISIBLE;
            hcsBuffer bufA = nullptr, bufB = nullptr, bufC = nullptr;
            if (hcsBufferCreate(context, &bd, &bufA) == HCS_SUCCESS &&
                hcsBufferCreate(context, &bd, &bufB) == HCS_SUCCESS &&
                hcsBufferCreate(context, &bd, &bufC) == HCS_SUCCESS &&
                hcsBufferUpload(bufA, 0, a.data(), bd.size) == HCS_SUCCESS &&
                hcsBufferUpload(bufB, 0, b.data(), bd.size) == HCS_SUCCESS) {
                hcsKernel kernel = nullptr;
                if (hcsKernelCreate(context, "vecadd_f32", &kernel) == HCS_SUCCESS) {
                    struct {
                        uint32_t n;
                        float alpha;
                        uint32_t p0, p1;
                    } push{n, 1.5f, 0, 0};
                    hcsDispatchDesc desc{};
                    desc.struct_size = sizeof(desc);
                    desc.buffer_count = 3;
                    desc.buffers[0] = bufA;
                    desc.buffers[1] = bufB;
                    desc.buffers[2] = bufC;
                    desc.group_count_x = (n + 63) / 64;
                    desc.group_count_y = 1;
                    desc.group_count_z = 1;
                    desc.push_constants_size = sizeof(push);
                    std::memcpy(desc.push_constants, &push, sizeof(push));
                    BenchRow row;
                    row.device = deviceName;
                    row.backend = backendName;
                    row.kernel = "vecadd_f32";
                    row.shape = "n=" + std::to_string(n);
                    row.gbytes = 3.0 * n * sizeof(float) / 1e9;
                    row.gflops = 2.0 * n / 1e9;
                    row.timing = measure(options.warmup, options.reps, [&]() {
                        return hcsDispatch(kernel, &desc, nullptr);
                    });
                    /* Verify against a double-precision host reference. */
                    if (row.timing.ok) {
                        std::vector<float> out(n, 0.0f);
                        if (hcsBufferDownload(bufC, 0, out.data(), bd.size) == HCS_SUCCESS) {
                            bool good = true;
                            float worst = 0.0f;
                            for (uint32_t e = 0; e < n; ++e) {
                                const float ref = static_cast<float>(
                                    static_cast<double>(a[e]) + 1.5 * static_cast<double>(b[e]));
                                const float diff = std::fabs(out[e] - ref);
                                worst = std::max(worst, diff);
                                if (diff > 1e-4f) {
                                    good = false;
                                }
                            }
                            if (!good) {
                                std::fprintf(stderr,
                                             "[verify] vecadd mismatch on %s, worst %.3g\n",
                                             deviceName.c_str(), static_cast<double>(worst));
                            }
                            row.verified = good;
                        }
                    }
                    rows.push_back(row);
                    hcsKernelDestroy(kernel);
                }
            }
            if (bufA) hcsBufferDestroy(bufA);
            if (bufB) hcsBufferDestroy(bufB);
            if (bufC) hcsBufferDestroy(bufC);
        }

        /* scale */
        {
            const uint32_t n = options.vecElements;
            std::vector<float> x(n);
            fill(x, 3);
            hcsBufferDesc bd{};
            bd.struct_size = sizeof(bd);
            bd.size = static_cast<uint64_t>(n) * sizeof(float);
            hcsBuffer in = nullptr, out = nullptr;
            if (hcsBufferCreate(context, &bd, &in) == HCS_SUCCESS &&
                hcsBufferCreate(context, &bd, &out) == HCS_SUCCESS &&
                hcsBufferUpload(in, 0, x.data(), bd.size) == HCS_SUCCESS) {
                hcsKernel kernel = nullptr;
                if (hcsKernelCreate(context, "scale_f32", &kernel) == HCS_SUCCESS) {
                    struct {
                        uint32_t n;
                        float alpha;
                        uint32_t p0, p1;
                    } push{n, 0.5f, 0, 0};
                    hcsDispatchDesc desc{};
                    desc.struct_size = sizeof(desc);
                    desc.buffer_count = 2;
                    desc.buffers[0] = in;
                    desc.buffers[1] = out;
                    desc.group_count_x = (n + 63) / 64;
                    desc.group_count_y = 1;
                    desc.group_count_z = 1;
                    desc.push_constants_size = sizeof(push);
                    std::memcpy(desc.push_constants, &push, sizeof(push));
                    BenchRow row;
                    row.device = deviceName;
                    row.backend = backendName;
                    row.kernel = "scale_f32";
                    row.shape = "n=" + std::to_string(n);
                    row.gbytes = 2.0 * n * sizeof(float) / 1e9;
                    row.gflops = n / 1e9;
                    row.timing = measure(options.warmup, options.reps, [&]() {
                        return hcsDispatch(kernel, &desc, nullptr);
                    });
                    rows.push_back(row);
                    hcsKernelDestroy(kernel);
                }
            }
            if (in) hcsBufferDestroy(in);
            if (out) hcsBufferDestroy(out);
        }

        /* reduce */
        {
            const uint32_t n = options.vecElements;
            const uint32_t groups = 256;
            std::vector<float> x(n);
            fill(x, 4);
            hcsBufferDesc bd{};
            bd.struct_size = sizeof(bd);
            bd.size = static_cast<uint64_t>(n) * sizeof(float);
            hcsBuffer in = nullptr, out = nullptr;
            hcsBufferDesc od{};
            od.struct_size = sizeof(od);
            od.size = groups * sizeof(float);
            if (hcsBufferCreate(context, &bd, &in) == HCS_SUCCESS &&
                hcsBufferCreate(context, &od, &out) == HCS_SUCCESS &&
                hcsBufferUpload(in, 0, x.data(), bd.size) == HCS_SUCCESS) {
                hcsKernel kernel = nullptr;
                if (hcsKernelCreate(context, "reduce_sum_f32", &kernel) == HCS_SUCCESS) {
                    struct {
                        uint32_t n;
                        uint32_t groups;
                        uint32_t p0, p1;
                    } push{n, groups, 0, 0};
                    hcsDispatchDesc desc{};
                    desc.struct_size = sizeof(desc);
                    desc.buffer_count = 2;
                    desc.buffers[0] = in;
                    desc.buffers[1] = out;
                    desc.group_count_x = groups;
                    desc.group_count_y = 1;
                    desc.group_count_z = 1;
                    desc.push_constants_size = sizeof(push);
                    std::memcpy(desc.push_constants, &push, sizeof(push));
                    BenchRow row;
                    row.device = deviceName;
                    row.backend = backendName;
                    row.kernel = "reduce_sum_f32";
                    row.shape = "n=" + std::to_string(n) + ", g=" + std::to_string(groups);
                    row.gbytes = n * sizeof(float) / 1e9;
                    row.gflops = n / 1e9;
                    row.timing = measure(options.warmup, options.reps, [&]() {
                        return hcsDispatch(kernel, &desc, nullptr);
                    });
                    rows.push_back(row);
                    hcsKernelDestroy(kernel);
                }
            }
            if (in) hcsBufferDestroy(in);
            if (out) hcsBufferDestroy(out);
        }

        /* matmul */
        {
            const uint32_t d = options.matSize;
            std::vector<float> a(static_cast<size_t>(d) * d);
            std::vector<float> b(static_cast<size_t>(d) * d);
            fill(a, 5);
            fill(b, 6);
            hcsBufferDesc bd{};
            bd.struct_size = sizeof(bd);
            bd.size = static_cast<uint64_t>(d) * d * sizeof(float);
            hcsBuffer bufA = nullptr, bufB = nullptr, bufC = nullptr;
            if (hcsBufferCreate(context, &bd, &bufA) == HCS_SUCCESS &&
                hcsBufferCreate(context, &bd, &bufB) == HCS_SUCCESS &&
                hcsBufferCreate(context, &bd, &bufC) == HCS_SUCCESS &&
                hcsBufferUpload(bufA, 0, a.data(), bd.size) == HCS_SUCCESS &&
                hcsBufferUpload(bufB, 0, b.data(), bd.size) == HCS_SUCCESS) {
                hcsKernel kernel = nullptr;
                if (hcsKernelCreate(context, "matmul_f32", &kernel) == HCS_SUCCESS) {
                    struct {
                        uint32_t m, n, k, pad;
                    } push{d, d, d, 0};
                    hcsDispatchDesc desc{};
                    desc.struct_size = sizeof(desc);
                    desc.buffer_count = 3;
                    desc.buffers[0] = bufA;
                    desc.buffers[1] = bufB;
                    desc.buffers[2] = bufC;
                    desc.group_count_x = (d + 15) / 16;
                    desc.group_count_y = (d + 15) / 16;
                    desc.group_count_z = 1;
                    desc.push_constants_size = sizeof(push);
                    std::memcpy(desc.push_constants, &push, sizeof(push));
                    BenchRow row;
                    row.device = deviceName;
                    row.backend = backendName;
                    row.kernel = "matmul_f32";
                    row.shape = std::to_string(d) + "x" + std::to_string(d) + "x" +
                                std::to_string(d);
                    row.gbytes = (2.0 * d * d + static_cast<double>(d) * d) * sizeof(float) / 1e9;
                    row.gflops = 2.0 * d * d * d / 1e9;
                    row.timing = measure(options.warmup, options.reps, [&]() {
                        return hcsDispatch(kernel, &desc, nullptr);
                    });
                    rows.push_back(row);
                    hcsKernelDestroy(kernel);
                }
            }
            if (bufA) hcsBufferDestroy(bufA);
            if (bufB) hcsBufferDestroy(bufB);
            if (bufC) hcsBufferDestroy(bufC);
        }

        /* scan: exclusive chunked prefix sum */
        {
            const uint32_t n = options.vecElements;
            const uint32_t groups = 256;
            std::vector<float> x(n);
            fill(x, 7);
            hcsBufferDesc inDesc{};
            inDesc.struct_size = sizeof(inDesc);
            inDesc.size = static_cast<uint64_t>(n) * sizeof(float);
            hcsBufferDesc outDesc = inDesc;
            hcsBuffer in = nullptr, out = nullptr;
            if (hcsBufferCreate(context, &inDesc, &in) == HCS_SUCCESS &&
                hcsBufferCreate(context, &outDesc, &out) == HCS_SUCCESS &&
                hcsBufferUpload(in, 0, x.data(), inDesc.size) == HCS_SUCCESS) {
                hcsKernel kernel = nullptr;
                if (hcsKernelCreate(context, "scan_f32", &kernel) == HCS_SUCCESS) {
                    struct {
                        uint32_t n;
                        uint32_t groups;
                        uint32_t p0, p1;
                    } push{n, groups, 0, 0};
                    hcsDispatchDesc desc{};
                    desc.struct_size = sizeof(desc);
                    desc.buffer_count = 2;
                    desc.buffers[0] = in;
                    desc.buffers[1] = out;
                    desc.group_count_x = groups;
                    desc.group_count_y = 1;
                    desc.group_count_z = 1;
                    desc.push_constants_size = sizeof(push);
                    std::memcpy(desc.push_constants, &push, sizeof(push));
                    BenchRow row;
                    row.device = deviceName;
                    row.backend = backendName;
                    row.kernel = "scan_f32";
                    row.shape = "n=" + std::to_string(n) + ", g=" + std::to_string(groups);
                    row.gbytes = 2.0 * n * sizeof(float) / 1e9;
                    row.gflops = n / 1e9;
                    row.timing = measure(options.warmup, options.reps, [&]() {
                        return hcsDispatch(kernel, &desc, nullptr);
                    });
                    rows.push_back(row);
                    hcsKernelDestroy(kernel);
                }
            }
            if (in) hcsBufferDestroy(in);
            if (out) hcsBufferDestroy(out);
        }

        /* transpose: 16x16 tiled row-major transpose */
        {
            const uint32_t d = options.matSize;
            const uint64_t elements = static_cast<uint64_t>(d) * d;
            std::vector<float> a(static_cast<size_t>(elements));
            fill(a, 8);
            hcsBufferDesc bd{};
            bd.struct_size = sizeof(bd);
            bd.size = elements * sizeof(float);
            hcsBuffer in = nullptr, out = nullptr;
            if (hcsBufferCreate(context, &bd, &in) == HCS_SUCCESS &&
                hcsBufferCreate(context, &bd, &out) == HCS_SUCCESS &&
                hcsBufferUpload(in, 0, a.data(), bd.size) == HCS_SUCCESS) {
                hcsKernel kernel = nullptr;
                if (hcsKernelCreate(context, "transpose_f32", &kernel) == HCS_SUCCESS) {
                    struct {
                        uint32_t rows;
                        uint32_t cols;
                        uint32_t p0, p1;
                    } push{d, d, 0, 0};
                    hcsDispatchDesc desc{};
                    desc.struct_size = sizeof(desc);
                    desc.buffer_count = 2;
                    desc.buffers[0] = in;
                    desc.buffers[1] = out;
                    desc.group_count_x = (d + 15) / 16;
                    desc.group_count_y = (d + 15) / 16;
                    desc.group_count_z = 1;
                    desc.push_constants_size = sizeof(push);
                    std::memcpy(desc.push_constants, &push, sizeof(push));
                    BenchRow row;
                    row.device = deviceName;
                    row.backend = backendName;
                    row.kernel = "transpose_f32";
                    row.shape = std::to_string(d) + "x" + std::to_string(d);
                    row.gbytes = 2.0 * static_cast<double>(elements) * sizeof(float) / 1e9;
                    row.gflops = 0.0;
                    row.timing = measure(options.warmup, options.reps, [&]() {
                        return hcsDispatch(kernel, &desc, nullptr);
                    });
                    rows.push_back(row);
                    hcsKernelDestroy(kernel);
                }
            }
            if (in) hcsBufferDestroy(in);
            if (out) hcsBufferDestroy(out);
        }

        for (const auto& row : rows) {
            printRow(row, options.json, firstJson);
            firstJson = false;
        }
        if (!options.json) {
            std::printf("\n");
        }
        hcsDestroyContext(context);
    }

    if (options.json) {
        std::printf("]\n");
    } else {
        std::printf("Done. Report generated from real dispatches on the devices above.\n");
    }
    hcsShutdown();
    return 0;
}
