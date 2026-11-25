#pragma once

#include "hcsuda/hcsuda.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hcs {

constexpr uint32_t kMaxBuffers = 8;
constexpr uint32_t kMaxPushConstants = 128;

/* Internal kernel ids. Public names are resolved through the registry. */
enum class KernelId : int {
    Invalid = 0,
    VecAddF32 = 1,
    ScaleF32 = 2,
    ReduceSumF32 = 3,
    MatMulF32 = 4,
    ScanF32 = 5,
    TransposeF32 = 6,
    Count = 7
};

struct KernelDef {
    const char* name;
    KernelId id;
    uint32_t local_size_x;
    const char* description;
};

const KernelDef* findKernel(const char* name);
const KernelDef* kernelById(KernelId id);
const KernelDef* kernelTable(uint32_t* count);

/* Per-flow backend state. V1 uses fences; timeline semaphores are a later tier. */
class BackendFlow {
public:
    virtual ~BackendFlow() = default;
    virtual hcsResult synchronize(uint64_t timeout_ns) = 0;
    virtual uint64_t completed() const = 0;
};

/* Backend context interface. One implementation per backend. */
class BackendContext {
public:
    virtual ~BackendContext() = default;

    virtual hcsResult createBuffer(hcs_buffer_t* buffer) = 0;
    virtual void destroyBuffer(hcs_buffer_t* buffer) = 0;
    virtual hcsResult dispatch(hcs_kernel_t* kernel, const hcsDispatchDesc* desc,
                               hcsFlow flow) = 0;
    virtual std::unique_ptr<BackendFlow> createFlow(hcs_flow_t* flow) = 0;
    virtual void destroyFlow(hcs_flow_t* flow) = 0;
    virtual hcsResult createProgram(hcs_program_t* program, const void* data,
                                    size_t byteSize) = 0;
    virtual hcsResult createProgramKernel(hcs_program_t* program, hcs_kernel_t* kernel) = 0;
    virtual hcsResult synchronize() = 0;
    virtual const char* name() const = 0;
};

std::unique_ptr<BackendContext> createCpuContext(hcs_device_t* device, uint64_t memory_cap);
std::unique_ptr<BackendContext> createVulkanContext(hcs_device_t* device, uint64_t memory_cap);

void enumerateCpuDevices(std::vector<std::unique_ptr<hcs_device_t>>& out);
void enumerateVulkanDevices(std::vector<std::unique_ptr<hcs_device_t>>& out);
void shutdownVulkanGlobal();
hcsResult hcsVulkanQueryBudget(hcs_device_t* device, hcsMemoryBudget* io);

/* Structured error reporting, thread local. */
void setLastError(hcsResult code, const char* operation, const std::string& message,
                  int backend_result = 0);
hcsResult fail(hcsResult code, const char* operation, const std::string& message,
               int backend_result = 0);

/* Utility: host-visible physical memory in bytes (0 if unavailable). */
uint64_t hostPhysicalMemory();
uint64_t hostAvailableMemory();

} // namespace hcs

struct hcs_device_t {
    uint64_t id = 0;
    hcsDeviceInfo info{};
    hcsDeviceCaps caps{};
    int backend = 0;
    uint32_t backend_index = 0;
    void* native = nullptr; /* VkPhysicalDevice; valid while runtime is initialized */
    uint32_t score = 0;
    uint64_t budget_bytes = 0;
};

struct hcs_context_t {
    hcs_device_t* device = nullptr;
    std::unique_ptr<hcs::BackendContext> impl;
};

struct hcs_buffer_t {
    hcs_context_t* context = nullptr;
    uint64_t size = 0;
    uint32_t usage = 0;
    void* host = nullptr;      /* host-visible pointer (CPU heap or mapped Vulkan memory) */
    void* vk_buffer = nullptr; /* VkBuffer, Vulkan only */
    void* vk_memory = nullptr; /* VkDeviceMemory, Vulkan only */
};

struct hcs_flow_t {
    hcs_context_t* context = nullptr;
    std::atomic<uint64_t> submissions{0};
    std::unique_ptr<hcs::BackendFlow> impl;
};

struct hcs_program_t {
    hcs_context_t* context = nullptr;
    std::vector<unsigned char> spirv;
};

struct hcs_event_t {
    hcs_context_t* context = nullptr;
    hcsFlow flow = nullptr;
    uint64_t target = 0;
    bool recorded = false;
};

struct hcs_kernel_t {
    hcs_context_t* context = nullptr;
    const hcs::KernelDef* def = nullptr;
    hcs_program_t* program = nullptr; /* set only for hcsKernelFromProgram kernels */
    uint64_t uid = 0;                 /* unique pipeline key, never reused */
};
