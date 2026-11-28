/*
 * CPU backend: portable scalar baseline plus runtime-dispatched AVX2 paths.
 * The same kernels act as the reference implementation for every GPU test.
 */

#include "core/internal.h"

#include <immintrin.h>
#include <intrin.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace hcs {
namespace {

uint64_t alignUp(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

struct CpuInfo {
    char brand[64];
    char vendor[16];
    bool avx2;
    uint32_t logical_threads;
};

CpuInfo queryCpu()
{
    CpuInfo info{};
#if defined(_MSC_VER)
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 0x80000000);
    const unsigned maxExt = static_cast<unsigned>(regs[0]);
    std::memset(info.brand, 0, sizeof(info.brand));
    if (maxExt >= 0x80000004u) {
        unsigned* dst = reinterpret_cast<unsigned*>(info.brand);
        for (int i = 0; i < 3; ++i) {
            __cpuid(regs, 0x80000002 + i);
            std::memcpy(dst + i * 4, regs, 16);
        }
        /* Drop leading spaces that CPUID vendors include. */
        char* start = info.brand;
        while (*start == ' ') {
            ++start;
        }
        std::memmove(info.brand, start, std::strlen(start) + 1);
        size_t len = std::strlen(info.brand);
        while (len > 0 && info.brand[len - 1] == ' ') {
            info.brand[--len] = '\0';
        }
    }
    __cpuid(regs, 0);
    /* Vendor string order is EBX, EDX, ECX (not EBX, ECX, EDX). */
    std::memcpy(info.vendor, &regs[1], 4);
    std::memcpy(info.vendor + 4, &regs[3], 4);
    std::memcpy(info.vendor + 8, &regs[2], 4);
    info.vendor[12] = '\0';

    __cpuid(regs, 1);
    const bool osxsave = (regs[2] & (1 << 27)) != 0;
    bool ymm_ok = false;
    if (osxsave) {
        const unsigned long long xcr0 = _xgetbv(0);
        ymm_ok = (xcr0 & 0x6) == 0x6;
    }
    __cpuidex(regs, 7, 0);
    info.avx2 = ymm_ok && ((regs[1] & (1 << 5)) != 0);
#else
    std::snprintf(info.brand, sizeof(info.brand), "Generic x86-64 CPU");
    std::snprintf(info.vendor, sizeof(info.vendor), "Unknown");
    info.avx2 = false;
#endif
    info.logical_threads =
        std::max(1u, static_cast<uint32_t>(std::thread::hardware_concurrency()));
    return info;
}

const CpuInfo& cpuInfo()
{
    static const CpuInfo info = queryCpu();
    return info;
}

uint32_t workerCount()
{
    const uint32_t threads = cpuInfo().logical_threads;
    return std::max(1u, std::min(threads, 32u));
}

/* Runs fn(begin,end) over [0,count) split across at most workerCount threads. */
template <typename Fn>
void parallelFor(uint64_t count, uint64_t grain, Fn&& fn)
{
    if (count == 0) {
        return;
    }
    const uint64_t threadsWanted = std::min<uint64_t>(
        workerCount(), (count + grain - 1) / grain);
    if (threadsWanted <= 1) {
        fn(uint64_t{0}, count);
        return;
    }
    const uint64_t chunk = (count + threadsWanted - 1) / threadsWanted;
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(threadsWanted));
    for (uint64_t t = 1; t < threadsWanted; ++t) {
        const uint64_t begin = t * chunk;
        if (begin >= count) {
            break;
        }
        const uint64_t end = std::min(count, begin + chunk);
        pool.emplace_back([&fn, begin, end]() { fn(begin, end); });
    }
    fn(uint64_t{0}, std::min(count, chunk));
    for (auto& thread : pool) {
        thread.join();
    }
}

void vecaddScalar(const float* a, const float* b, float* c, uint64_t n, float alpha)
{
    for (uint64_t i = 0; i < n; ++i) {
        c[i] = a[i] + alpha * b[i];
    }
}

void vecaddAvx2(const float* a, const float* b, float* c, uint64_t n, float alpha)
{
    const __m256 va = _mm256_set1_ps(alpha);
    uint64_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 x = _mm256_loadu_ps(a + i);
        const __m256 y = _mm256_loadu_ps(b + i);
        _mm256_storeu_ps(c + i, _mm256_fmadd_ps(va, y, x));
    }
    for (; i < n; ++i) {
        c[i] = a[i] + alpha * b[i];
    }
}

void scaleAvx2(const float* x, float* y, uint64_t n, float alpha)
{
    const __m256 va = _mm256_set1_ps(alpha);
    uint64_t i = 0;
    for (; i + 8 <= n; i += 8) {
        _mm256_storeu_ps(y + i, _mm256_mul_ps(va, _mm256_loadu_ps(x + i)));
    }
    for (; i < n; ++i) {
        y[i] = alpha * x[i];
    }
}

uint32_t pcU32(const hcsDispatchDesc* desc, uint32_t index)
{
    uint32_t value = 0;
    const size_t offset = static_cast<size_t>(index) * 4;
    if (offset + 4 <= desc->push_constants_size) {
        std::memcpy(&value, desc->push_constants + offset, 4);
    }
    return value;
}

float pcF32(const hcsDispatchDesc* desc, uint32_t index)
{
    float value = 0.0f;
    const size_t offset = static_cast<size_t>(index) * 4;
    if (offset + 4 <= desc->push_constants_size) {
        std::memcpy(&value, desc->push_constants + offset, 4);
    }
    return value;
}

class CpuFlow final : public BackendFlow {
public:
    explicit CpuFlow(hcs_flow_t* flow) : flow_(flow) {}

    hcsResult synchronize(uint64_t) override { return HCS_SUCCESS; }

    uint64_t completed() const override
    {
        return flow_ ? flow_->submissions.load(std::memory_order_acquire) : 0;
    }

private:
    hcs_flow_t* flow_ = nullptr;
};

class CpuContext final : public BackendContext {
public:
    CpuContext(hcs_device_t* device, uint64_t memoryCap)
        : device_(device), memory_cap_(memoryCap)
    {
        if (memory_cap_ == 0) {
            const uint64_t available = hostAvailableMemory();
            memory_cap_ = available > 0 ? (available / 2) : (uint64_t{2} << 30);
        }
    }

    ~CpuContext() override = default;

    const char* name() const override { return "cpu"; }

    hcsResult createBuffer(hcs_buffer_t* buffer) override
    {
        if (buffer->size == 0) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.bufferCreate",
                        "buffer size must be greater than zero");
        }
        const uint64_t aligned = alignUp(buffer->size, 64);
        if (allocated_ + aligned > memory_cap_) {
            return fail(HCS_E_OUT_OF_MEMORY, "cpu.bufferCreate",
                        "allocation would exceed the configured memory cap");
        }
        void* memory = _aligned_malloc(static_cast<size_t>(aligned), 64);
        if (!memory) {
            return fail(HCS_E_OUT_OF_MEMORY, "cpu.bufferCreate",
                        "host allocation failed");
        }
        std::memset(memory, 0, static_cast<size_t>(aligned));
        buffer->host = memory;
        allocated_ += aligned;
        return HCS_SUCCESS;
    }

    void destroyBuffer(hcs_buffer_t* buffer) override
    {
        if (buffer->host) {
            _aligned_free(buffer->host);
            buffer->host = nullptr;
            allocated_ -= alignUp(buffer->size, 64);
        }
    }

    hcsResult createProgram(hcs_program_t*, const void*, size_t) override
    {
        return fail(HCS_E_UNSUPPORTED_FEATURE, "cpu.createProgram",
                    "the CPU backend does not decode SPIR-V; run the HCSIR module "
                    "through the interpreter instead");
    }

    hcsResult createProgramKernel(hcs_program_t*, hcs_kernel_t*) override
    {
        return fail(HCS_E_UNSUPPORTED_FEATURE, "cpu.createProgramKernel",
                    "the CPU backend does not decode SPIR-V");
    }

    std::unique_ptr<BackendFlow> createFlow(hcs_flow_t* flow) override
    {
        return std::make_unique<CpuFlow>(flow);
    }

    void destroyFlow(hcs_flow_t*) override {}

    hcsResult dispatch(hcs_kernel_t* kernel, const hcsDispatchDesc* desc, hcsFlow) override
    {
        if (!kernel->def) {
            return fail(HCS_E_UNSUPPORTED_FEATURE, "cpu.dispatch",
                        "program kernels require a backend that supports SPIR-V");
        }
        switch (kernel->def->id) {
        case KernelId::VecAddF32:
            return dispatchVecAdd(desc);
        case KernelId::ScaleF32:
            return dispatchScale(desc);
        case KernelId::ReduceSumF32:
            return dispatchReduce(desc);
        case KernelId::ReduceFinalF32:
            return dispatchReduceFinal(desc);
        case KernelId::ScanAddOffsetsF32:
            return dispatchScanAddOffsets(desc);
        case KernelId::MatMulF32:
            return dispatchMatMul(desc);
        case KernelId::ScanF32:
            return dispatchScan(desc);
        case KernelId::TransposeF32:
            return dispatchTranspose(desc);
        default:
            return fail(HCS_E_UNSUPPORTED_FEATURE, "cpu.dispatch",
                        "kernel is not implemented on the CPU backend");
        }
    }

    hcsResult synchronize() override { return HCS_SUCCESS; }

private:
    static bool validBuffers(const hcsDispatchDesc* desc, uint32_t expected)
    {
        if (desc->buffer_count != expected) {
            fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch",
                 "kernel requires a different number of buffers");
            return false;
        }
        for (uint32_t i = 0; i < expected; ++i) {
            if (!desc->buffers[i] || !desc->buffers[i]->host) {
                fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch",
                     "buffer handle is null or not host visible");
                return false;
            }
        }
        return true;
    }

    hcsResult dispatchVecAdd(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 3)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        const uint32_t n = pcU32(desc, 0);
        const float alpha = pcF32(desc, 1);
        const auto* a = static_cast<const float*>(desc->buffers[0]->host);
        const auto* b = static_cast<const float*>(desc->buffers[1]->host);
        auto* c = static_cast<float*>(desc->buffers[2]->host);
        const uint64_t elements = std::min<uint64_t>(
            n, std::min(desc->buffers[0]->size, std::min(desc->buffers[1]->size,
                                                         desc->buffers[2]->size)) /
                   sizeof(float));
        if (elements == 0 && n != 0) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch.vecadd",
                        "buffer is smaller than the element count");
        }
        const bool useAvx2 = cpuInfo().avx2;
        parallelFor(elements, 1u << 16, [&](uint64_t begin, uint64_t end) {
            if (useAvx2) {
                vecaddAvx2(a + begin, b + begin, c + begin, end - begin, alpha);
            } else {
                vecaddScalar(a + begin, b + begin, c + begin, end - begin, alpha);
            }
        });
        return HCS_SUCCESS;
    }

    hcsResult dispatchScale(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 2)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        const uint32_t n = pcU32(desc, 0);
        const float alpha = pcF32(desc, 1);
        const auto* x = static_cast<const float*>(desc->buffers[0]->host);
        auto* y = static_cast<float*>(desc->buffers[1]->host);
        const uint64_t elements = std::min<uint64_t>(
            n, std::min(desc->buffers[0]->size, desc->buffers[1]->size) / sizeof(float));
        const bool useAvx2 = cpuInfo().avx2;
        parallelFor(elements, 1u << 16, [&](uint64_t begin, uint64_t end) {
            if (useAvx2) {
                scaleAvx2(x + begin, y + begin, end - begin, alpha);
            } else {
                for (uint64_t i = begin; i < end; ++i) {
                    y[i] = alpha * x[i];
                }
            }
        });
        return HCS_SUCCESS;
    }

    hcsResult dispatchReduce(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 2)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        const uint32_t n = pcU32(desc, 0);
        const uint32_t groups = std::max(1u, pcU32(desc, 1));
        const auto* x = static_cast<const float*>(desc->buffers[0]->host);
        auto* out = static_cast<float*>(desc->buffers[1]->host);
        const uint64_t elements = std::min<uint64_t>(
            n, desc->buffers[0]->size / sizeof(float));
        const uint64_t outCount = std::min<uint64_t>(
            groups, desc->buffers[1]->size / sizeof(float));
        if (outCount < groups) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch.reduce",
                        "output buffer is too small for the requested group count");
        }
        const uint64_t chunk = (elements + groups - 1) / groups;
        parallelFor(groups, 1, [&](uint64_t begin, uint64_t end) {
            for (uint64_t g = begin; g < end; ++g) {
                const uint64_t first = g * chunk;
                const uint64_t last = std::min<uint64_t>(elements, first + chunk);
                double sum = 0.0;
                for (uint64_t i = first; i < last; ++i) {
                    sum += x[i];
                }
                out[g] = static_cast<float>(sum);
            }
        });
        return HCS_SUCCESS;
    }

    hcsResult dispatchReduceFinal(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 2)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        if (desc->buffers[1]->size < sizeof(float)) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch.reduce_final",
                        "output buffer must hold at least one float");
        }
        const uint64_t available = desc->buffers[0]->size / sizeof(float);
        const uint64_t n = std::min<uint64_t>(pcU32(desc, 0), available);
        const auto* partials = static_cast<const float*>(desc->buffers[0]->host);
        auto* out = static_cast<float*>(desc->buffers[1]->host);
        double sum = 0.0;
        for (uint64_t i = 0; i < n; ++i) {
            sum += partials[i];
        }
        out[0] = static_cast<float>(sum);
        return HCS_SUCCESS;
    }

    hcsResult dispatchScanAddOffsets(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 3)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        const uint64_t n = pcU32(desc, 0);
        const uint64_t groups = std::max(1u, pcU32(desc, 1));
        if (desc->buffers[1]->size / sizeof(float) < groups) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch.scan_add_offsets",
                        "offset buffer is too small for the requested group count");
        }
        const uint64_t available = std::min<uint64_t>(
            n, std::min<uint64_t>(desc->buffers[0]->size, desc->buffers[2]->size) /
                   sizeof(float));
        const uint64_t chunk = (n + groups - 1) / groups;
        const auto* local = static_cast<const float*>(desc->buffers[0]->host);
        const auto* offsets = static_cast<const float*>(desc->buffers[1]->host);
        auto* result = static_cast<float*>(desc->buffers[2]->host);
        parallelFor(groups, 1, [&](uint64_t begin, uint64_t end) {
            for (uint64_t g = begin; g < end; ++g) {
                const uint64_t first = g * chunk;
                const uint64_t last = std::min<uint64_t>(available, first + chunk);
                const float offset = offsets[g];
                for (uint64_t i = first; i < last; ++i) {
                    result[i] = local[i] + offset;
                }
            }
        });
        return HCS_SUCCESS;
    }

    hcsResult dispatchMatMul(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 3)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        const uint32_t m = pcU32(desc, 0);
        const uint32_t n = pcU32(desc, 1);
        const uint32_t k = pcU32(desc, 2);
        const auto* a = static_cast<const float*>(desc->buffers[0]->host);
        const auto* b = static_cast<const float*>(desc->buffers[1]->host);
        auto* c = static_cast<float*>(desc->buffers[2]->host);
        if (desc->buffers[0]->size < static_cast<uint64_t>(m) * k * sizeof(float) ||
            desc->buffers[1]->size < static_cast<uint64_t>(k) * n * sizeof(float) ||
            desc->buffers[2]->size < static_cast<uint64_t>(m) * n * sizeof(float)) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch.matmul",
                        "buffer is too small for the requested matrix dimensions");
        }
        parallelFor(m, 1, [&](uint64_t rowBegin, uint64_t rowEnd) {
            for (uint64_t row = rowBegin; row < rowEnd; ++row) {
                float* crow = c + row * n;
                std::memset(crow, 0, sizeof(float) * n);
                const float* arow = a + row * k;
                for (uint32_t kk = 0; kk < k; ++kk) {
                    const float aval = arow[kk];
                    const float* brow = b + static_cast<uint64_t>(kk) * n;
                    for (uint32_t col = 0; col < n; ++col) {
                        crow[col] += aval * brow[col];
                    }
                }
            }
        });
        return HCS_SUCCESS;
    }

    hcsResult dispatchScan(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 2)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        const uint32_t n = pcU32(desc, 0);
        const uint32_t groups = std::max(1u, pcU32(desc, 1));
        const auto* input = static_cast<const float*>(desc->buffers[0]->host);
        auto* output = static_cast<float*>(desc->buffers[1]->host);
        const uint64_t elements = std::min<uint64_t>(n, desc->buffers[0]->size / sizeof(float));
        if (desc->buffers[1]->size / sizeof(float) < elements) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch.scan",
                        "output buffer is too small");
        }
        const uint64_t chunk = (elements + groups - 1) / groups;
        parallelFor(groups, 1, [&](uint64_t begin, uint64_t end) {
            for (uint64_t g = begin; g < end; ++g) {
                const uint64_t first = g * chunk;
                const uint64_t last = std::min<uint64_t>(elements, first + chunk);
                float prefix = 0.0f;
                for (uint64_t i = first; i < last; ++i) {
                    output[i] = prefix;
                    prefix += input[i];
                }
            }
        });
        return HCS_SUCCESS;
    }

    hcsResult dispatchTranspose(const hcsDispatchDesc* desc)
    {
        if (!validBuffers(desc, 2)) {
            return HCS_E_INVALID_ARGUMENT;
        }
        const uint64_t rows = pcU32(desc, 0);
        const uint64_t cols = pcU32(desc, 1);
        const auto* input = static_cast<const float*>(desc->buffers[0]->host);
        auto* output = static_cast<float*>(desc->buffers[1]->host);
        if (desc->buffers[0]->size < rows * cols * sizeof(float) ||
            desc->buffers[1]->size < cols * rows * sizeof(float)) {
            return fail(HCS_E_INVALID_ARGUMENT, "cpu.dispatch.transpose",
                        "buffer is too small for the requested matrix dimensions");
        }
        parallelFor(rows, 1, [&](uint64_t rowBegin, uint64_t rowEnd) {
            for (uint64_t row = rowBegin; row < rowEnd; ++row) {
                for (uint64_t col = 0; col < cols; ++col) {
                    output[col * rows + row] = input[row * cols + col];
                }
            }
        });
        return HCS_SUCCESS;
    }

    hcs_device_t* device_ = nullptr;
    uint64_t memory_cap_ = 0;
    uint64_t allocated_ = 0;
};

} // namespace

uint64_t hostPhysicalMemory()
{
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<uint64_t>(status.ullTotalPhys);
    }
#endif
    return 0;
}

uint64_t hostAvailableMemory()
{
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<uint64_t>(status.ullAvailPhys);
    }
#endif
    return 0;
}

void enumerateCpuDevices(std::vector<std::unique_ptr<hcs_device_t>>& out)
{
    const CpuInfo& info = cpuInfo();
    auto device = std::make_unique<hcs_device_t>();
    device->id = (static_cast<uint64_t>(HCS_BACKEND_CPU) << 32) | 1u;
    device->backend = HCS_BACKEND_CPU;
    device->backend_index = 0;
    device->score = 10; /* always available: the portability floor */

    hcsDeviceInfo& di = device->info;
    di.struct_size = sizeof(di);
    di.backend = HCS_BACKEND_CPU;
    di.kind = HCS_DEVICE_KIND_CPU;
    di.device_id = device->id;
    di.vendor_id = 0;
    di.device_number = 0;
    di.api_version = HCS_API_VERSION;
    di.driver_version = 0;
    std::snprintf(di.name, sizeof(di.name), "%s", info.brand[0] ? info.brand : "CPU");
    std::snprintf(di.vendor, sizeof(di.vendor), "%s",
                  info.vendor[0] ? info.vendor : "Unknown");
    std::snprintf(di.driver_info, sizeof(di.driver_info),
                  "HCS CPU backend (scalar%s)", info.avx2 ? " + AVX2/FMA" : "");

    hcsDeviceCaps& caps = device->caps;
    caps.struct_size = sizeof(caps);
    caps.capabilities = HCS_CAP_BASE_COMPUTE | (info.avx2 ? HCS_CAP_AVX2 : 0);
    caps.compute_queue_count = info.logical_threads;
    caps.max_workgroup_invocations = 1024;
    caps.max_workgroup_size[0] = 1024;
    caps.max_workgroup_size[1] = 1024;
    caps.max_workgroup_size[2] = 1024;
    caps.subgroup_size = 1;
    caps.max_storage_buffer_range = static_cast<uint64_t>(1) << 40;
    caps.max_memory_allocation_count = static_cast<uint64_t>(1) << 20;
    caps.shared_memory_bytes = 64 * 1024; /* emulated, reported as such */
    caps.total_memory_bytes = hostPhysicalMemory();
    device->budget_bytes = hostAvailableMemory();

    out.push_back(std::move(device));
}

std::unique_ptr<BackendContext> createCpuContext(hcs_device_t* device, uint64_t memoryCap)
{
    return std::make_unique<CpuContext>(device, memoryCap);
}

} // namespace hcs
