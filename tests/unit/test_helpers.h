#pragma once

#include "hcsuda/hcsuda.h"
#include "test_framework.h"

#include <cstring>
#include <initializer_list>
#include <algorithm>
#include <vector>

namespace testutil {

inline hcsResult findDevice(uint32_t backend, hcsDevice* out)
{
    uint32_t count = 0;
    hcsResult r = hcsEnumerateDevices(nullptr, &count);
    if (r != HCS_SUCCESS) {
        return r;
    }
    std::vector<hcsDeviceInfo> infos(count);
    r = hcsEnumerateDevices(infos.data(), &count);
    if (r != HCS_SUCCESS) {
        return r;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (infos[i].backend == backend) {
            return hcsGetDevice(i, out);
        }
    }
    return HCS_E_NO_DEVICE;
}

struct PushConsts {
    uint32_t u0;
    float f0;
    uint32_t u1;
    uint32_t u2;
};

struct MatMulPush {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t pad;
};

struct ReducePush {
    uint32_t n;
    uint32_t groups;
    uint32_t pad0;
    uint32_t pad1;
};

/* Owns runtime + device + context for the duration of a test. */
struct DeviceSession {
    bool initialized = false;
    hcsDevice device = nullptr;
    hcsContext context = nullptr;

    ~DeviceSession() { close(); }

    bool open(uint32_t backend, uint64_t memoryCap = 0)
    {
        if (hcsInit(nullptr) != HCS_SUCCESS) {
            return false;
        }
        initialized = true;
        if (findDevice(backend, &device) != HCS_SUCCESS) {
            return false;
        }
        hcsContextDesc desc{};
        desc.struct_size = sizeof(desc);
        desc.memory_cap_bytes = memoryCap;
        return hcsCreateContext(device, &desc, &context) == HCS_SUCCESS;
    }

    void close()
    {
        if (context) {
            hcsDestroyContext(context);
            context = nullptr;
        }
        if (initialized) {
            hcsShutdown();
            initialized = false;
        }
    }
};

inline hcsResult makeBuffer(hcsContext context, uint64_t bytes, const void* initial,
                            hcsBuffer* out)
{
    hcsBufferDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.size = bytes;
    desc.usage = HCS_BUFFER_USAGE_STORAGE | HCS_BUFFER_USAGE_HOST_VISIBLE;
    hcsResult r = hcsBufferCreate(context, &desc, out);
    if (r != HCS_SUCCESS || !initial) {
        return r;
    }
    return hcsBufferUpload(*out, 0, initial, bytes);
}

inline hcsDispatchDesc makeDispatch(std::initializer_list<hcsBuffer> buffers, uint32_t groups,
                                    const void* push, uint32_t pushSize)
{
    hcsDispatchDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.group_count_x = groups;
    desc.group_count_y = 1;
    desc.group_count_z = 1;
    desc.push_constants_size = pushSize;
    uint32_t i = 0;
    for (hcsBuffer buffer : buffers) {
        if (i < 8) {
            desc.buffers[i++] = buffer;
        }
    }
    desc.buffer_count = i;
    if (push && pushSize <= sizeof(desc.push_constants)) {
        std::memcpy(desc.push_constants, push, pushSize);
    }
    return desc;
}

/* Deterministic pseudo-random input, no test-order dependence. */
inline void fillPattern(std::vector<float>& values, float scale, int seed)
{
    for (size_t i = 0; i < values.size(); ++i) {
        const float x = static_cast<float>((i * 37 + static_cast<size_t>(seed) * 101) % 1000);
        values[i] = (x / 1000.0f - 0.5f) * scale;
    }
}

inline double maxAbs(const std::vector<float>& values)
{
    double m = 0.0;
    for (float v : values) {
        const double a = v < 0 ? -static_cast<double>(v) : static_cast<double>(v);
        if (a > m) {
            m = a;
        }
    }
    return m;
}

inline bool approxEqual(const std::vector<float>& a, const std::vector<float>& b,
                        double relTol, double absTol, double* outWorst)
{
    if (a.size() != b.size()) {
        if (outWorst) {
            *outWorst = 1e9;
        }
        return false;
    }
    const double scale = std::max(1.0, std::max(maxAbs(a), maxAbs(b)));
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double diff = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        const double allowed = absTol + relTol * scale;
        if (diff > allowed) {
            if (outWorst) {
                *outWorst = diff;
            }
            return false;
        }
        if (diff > worst) {
            worst = diff;
        }
    }
    if (outWorst) {
        *outWorst = worst;
    }
    return true;
}

} // namespace testutil
