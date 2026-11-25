#pragma once

/*
 * Optional thin C++ facade over the stable C ABI. Header-only, no exceptions
 * cross the ABI boundary; callers check hcsResult explicitly.
 */

#include "hcsuda.h"

#include <cstring>
#include <string>

namespace hcs {

inline uint32_t apiVersion() { return hcsGetApiVersion(); }
inline std::string versionString() { return std::string(hcsGetVersionString()); }

inline std::string lastErrorMessage()
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    if (hcsGetLastError(&info) != HCS_SUCCESS) {
        return std::string("unknown error");
    }
    return std::string(info.message);
}

/* RAII helper for handles. Move-only; never copies. */
template <typename Handle, void (*DestroyFn)(Handle)>
class HandleGuard {
public:
    HandleGuard() = default;
    explicit HandleGuard(Handle h) : handle_(h) {}
    ~HandleGuard() { reset(); }

    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    HandleGuard(HandleGuard&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }
    HandleGuard& operator=(HandleGuard&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void reset()
    {
        if (handle_) {
            DestroyFn(handle_);
            handle_ = nullptr;
        }
    }

    Handle get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    Handle handle_ = nullptr;
};

using BufferGuard = HandleGuard<hcsBuffer, hcsBufferDestroy>;
using FlowGuard = HandleGuard<hcsFlow, hcsFlowDestroy>;
using KernelGuard = HandleGuard<hcsKernel, hcsKernelDestroy>;
using ContextGuard = HandleGuard<hcsContext, hcsDestroyContext>;

} // namespace hcs
