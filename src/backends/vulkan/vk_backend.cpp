/*
 * Vulkan compute backend.
 *
 * Design notes (see docs/architecture/ADR-0002-vulkan-backend.md):
 *  - vulkan-1.dll is loaded dynamically. HCSUDA never ships or replaces it.
 *  - Every capability is queried, never assumed.
 *  - Buffers are host-visible + coherent in V1. On integrated GPUs this is the
 *    native path; on discrete GPUs it is a documented, slower fallback.
 *  - Dispatches are synchronous per submission in V1 (submit + fence wait).
 */

#include "core/internal.h"

#if defined(HCS_ENABLE_VULKAN) && HCS_ENABLE_VULKAN

#include "backends/vulkan/generated_shaders.h"

#include <vulkan/vulkan.h>
#include <windows.h>

#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace hcs {
namespace {

#define HCS_VK_GLOBAL_FUNCS(X)                 \
    X(vkEnumerateInstanceVersion)              \
    X(vkEnumerateInstanceExtensionProperties)  \
    X(vkEnumerateInstanceLayerProperties)      \
    X(vkCreateInstance)

#define HCS_VK_INSTANCE_FUNCS(X)                              \
    X(vkDestroyInstance)                                      \
    X(vkEnumeratePhysicalDevices)                             \
    X(vkGetPhysicalDeviceProperties)                          \
    X(vkGetPhysicalDeviceProperties2)                         \
    X(vkGetPhysicalDeviceFeatures)                            \
    X(vkGetPhysicalDeviceFeatures2)                           \
    X(vkGetPhysicalDeviceMemoryProperties)                    \
    X(vkGetPhysicalDeviceMemoryProperties2)                   \
    X(vkGetPhysicalDeviceQueueFamilyProperties)               \
    X(vkEnumerateDeviceExtensionProperties)                   \
    X(vkCreateDevice)

#define HCS_VK_DEVICE_FUNCS(X)                                \
    X(vkDestroyDevice)                                        \
    X(vkGetDeviceQueue)                                       \
    X(vkDeviceWaitIdle)                                       \
    X(vkCreateCommandPool)                                    \
    X(vkDestroyCommandPool)                                   \
    X(vkAllocateCommandBuffers)                               \
    X(vkBeginCommandBuffer)                                   \
    X(vkEndCommandBuffer)                                     \
    X(vkCreateShaderModule)                                   \
    X(vkDestroyShaderModule)                                  \
    X(vkCreateDescriptorSetLayout)                            \
    X(vkDestroyDescriptorSetLayout)                           \
    X(vkCreatePipelineLayout)                                 \
    X(vkDestroyPipelineLayout)                                \
    X(vkCreateComputePipelines)                               \
    X(vkDestroyPipeline)                                      \
    X(vkCreateDescriptorPool)                                 \
    X(vkDestroyDescriptorPool)                                \
    X(vkAllocateDescriptorSets)                               \
    X(vkFreeDescriptorSets)                                   \
    X(vkResetDescriptorPool)                                  \
    X(vkUpdateDescriptorSets)                                 \
    X(vkCreateBuffer)                                         \
    X(vkDestroyBuffer)                                        \
    X(vkGetBufferMemoryRequirements)                          \
    X(vkAllocateMemory)                                       \
    X(vkFreeMemory)                                           \
    X(vkBindBufferMemory)                                     \
    X(vkMapMemory)                                            \
    X(vkUnmapMemory)                                          \
    X(vkCreateFence)                                          \
    X(vkDestroyFence)                                         \
    X(vkFreeCommandBuffers)                                   \
    X(vkWaitForFences)                                        \
    X(vkGetFenceStatus)                                       \
    X(vkResetFences)                                          \
    X(vkQueueSubmit)                                          \
    X(vkCmdBindPipeline)                                      \
    X(vkCmdBindDescriptorSets)                                \
    X(vkCmdPushConstants)                                     \
    X(vkCmdDispatch)

struct VkApi {
    HMODULE module = nullptr;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;

#define HCS_VK_DECL(name) PFN_##name name = nullptr;
    HCS_VK_GLOBAL_FUNCS(HCS_VK_DECL)
    HCS_VK_INSTANCE_FUNCS(HCS_VK_DECL)
    HCS_VK_DEVICE_FUNCS(HCS_VK_DECL)
#undef HCS_VK_DECL

    bool loadGlobal()
    {
        module = LoadLibraryW(L"vulkan-1.dll");
        if (!module) {
            return false;
        }
        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(module, "vkGetInstanceProcAddr"));
        if (!vkGetInstanceProcAddr) {
            return false;
        }
#define HCS_VK_LOAD_GLOBAL(name) \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name));
        HCS_VK_GLOBAL_FUNCS(HCS_VK_LOAD_GLOBAL)
#undef HCS_VK_LOAD_GLOBAL
        /* vkEnumerateInstanceVersion is optional: absent means Vulkan 1.0. */
        return vkCreateInstance != nullptr;
    }

    bool loadInstance(VkInstance instance)
    {
#define HCS_VK_LOAD_INSTANCE(name)                                         \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)); \
    if (!name) {                                                           \
        return false;                                                      \
    }
        HCS_VK_INSTANCE_FUNCS(HCS_VK_LOAD_INSTANCE)
#undef HCS_VK_LOAD_INSTANCE
        vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
        return vkGetDeviceProcAddr != nullptr;
    }

    bool loadDevice(VkDevice device)
    {
#define HCS_VK_LOAD_DEVICE(name)                                        \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); \
    if (!name) {                                                        \
        return false;                                                   \
    }
        HCS_VK_DEVICE_FUNCS(HCS_VK_LOAD_DEVICE)
#undef HCS_VK_LOAD_DEVICE
        return true;
    }
};

VkApi g_api;
VkInstance g_instance = VK_NULL_HANDLE;
uint32_t g_instanceApiVersion = VK_API_VERSION_1_0;
std::mutex g_initMutex;
bool g_initialized = false;

const char* vkVendorName(uint32_t vendorId)
{
    switch (vendorId) {
    case 0x1002: return "AMD";
    case 0x10DE: return "NVIDIA";
    case 0x8086: return "Intel";
    case 0x106B: return "Apple";
    case 0x13B5: return "Arm";
    case 0x5143: return "Qualcomm";
    default: return "Unknown";
    }
}

const char* vkResultName(VkResult result)
{
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default: return "VK_ERROR_UNKNOWN";
    }
}

hcsResult mapVkResult(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS) {
        return HCS_SUCCESS;
    }
    hcsResult mapped = HCS_E_INTERNAL;
    if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY) {
        mapped = HCS_E_OUT_OF_MEMORY;
    } else if (result == VK_ERROR_DEVICE_LOST) {
        mapped = HCS_E_DEVICE_LOST;
    } else if (result == VK_TIMEOUT) {
        mapped = HCS_E_TIMEOUT;
    } else if (result == VK_ERROR_FEATURE_NOT_PRESENT) {
        mapped = HCS_E_UNSUPPORTED_FEATURE;
    }
    return fail(mapped, operation,
                std::string("Vulkan call failed: ") + vkResultName(result),
                static_cast<int>(result));
}

bool initVulkanGlobal()
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initialized) {
        return true;
    }
    if (!g_api.module && !g_api.loadGlobal()) {
        return false;
    }
    uint32_t apiVersion = VK_API_VERSION_1_0;
    if (g_api.vkEnumerateInstanceVersion) {
        if (g_api.vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS) {
            apiVersion = VK_API_VERSION_1_0;
        }
    }
    const uint32_t wanted =
        apiVersion < VK_API_VERSION_1_1 ? apiVersion : VK_API_VERSION_1_1;
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "HCSUDA";
    app.applicationVersion = VK_MAKE_VERSION(HCS_VERSION_MAJOR, HCS_VERSION_MINOR, 0);
    app.pEngineName = "HCS-Core";
    app.engineVersion = VK_MAKE_VERSION(HCS_VERSION_MAJOR, HCS_VERSION_MINOR, 0);
    app.apiVersion = wanted;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
#if defined(HCS_USE_VALIDATION_LAYERS) && HCS_USE_VALIDATION_LAYERS
    uint32_t layerCount = 0;
    if (g_api.vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "[hcs] cannot enumerate Vulkan instance layers\n");
        return false;
    }
    std::vector<VkLayerProperties> layers(layerCount);
    if (layerCount > 0 && g_api.vkEnumerateInstanceLayerProperties(&layerCount, layers.data()) !=
                              VK_SUCCESS) {
        return false;
    }
    bool found = false;
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, validationLayer) == 0) {
            found = true;
        }
    }
    if (!found) {
        std::fprintf(stderr,
                     "[hcs] validation build requested but VK_LAYER_KHRONOS_validation "
                     "is not installed\n");
        return false;
    }
    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = &validationLayer;
    std::fprintf(stderr, "[hcs] Vulkan validation layer enabled\n");
#else
    (void)validationLayer;
#endif

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult result = g_api.vkCreateInstance(&info, nullptr, &instance);
    if (result != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[hcs] Vulkan instance creation failed: %s\n",
                     vkResultName(result));
        return false;
    }
    if (!g_api.loadInstance(instance)) {
        std::fprintf(stderr, "[hcs] Vulkan instance functions unavailable\n");
        g_api.vkDestroyInstance(instance, nullptr);
        return false;
    }
    g_instance = instance;
    g_instanceApiVersion = wanted;
    g_initialized = true;
    return true;
}

bool deviceHasExtension(VkPhysicalDevice physical, const char* name)
{
    uint32_t count = 0;
    if (g_api.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) !=
        VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0 &&
        g_api.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count,
                                                   extensions.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& ext : extensions) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool queryMemoryBudget(VkPhysicalDevice physical, VkPhysicalDeviceMemoryProperties* outProps,
                       VkPhysicalDeviceMemoryBudgetPropertiesEXT* outBudget, bool* hasBudget)
{
    VkPhysicalDeviceMemoryProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

    const bool extension = g_instanceApiVersion >= VK_API_VERSION_1_1 &&
                           deviceHasExtension(physical, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (extension) {
        props2.pNext = &budget;
    }
    if (g_instanceApiVersion >= VK_API_VERSION_1_1 && g_api.vkGetPhysicalDeviceMemoryProperties2) {
        g_api.vkGetPhysicalDeviceMemoryProperties2(physical, &props2);
    } else if (g_api.vkGetPhysicalDeviceMemoryProperties) {
        g_api.vkGetPhysicalDeviceMemoryProperties(physical, &props2.memoryProperties);
    } else {
        return false;
    }
    if (outProps) {
        *outProps = props2.memoryProperties;
    }
    if (outBudget) {
        *outBudget = budget;
    }
    if (hasBudget) {
        *hasBudget = extension;
    }
    return true;
}

uint64_t heapTotals(const VkPhysicalDeviceMemoryProperties& props,
                    const VkPhysicalDeviceMemoryBudgetPropertiesEXT* budget, bool hasBudget,
                    uint64_t* outUsage)
{
    uint64_t total = 0;
    uint64_t usage = 0;
    for (uint32_t i = 0; i < props.memoryHeapCount; ++i) {
        if ((props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
            continue;
        }
        total += props.memoryHeaps[i].size;
        if (hasBudget && budget) {
            usage += budget->heapUsage[i];
        }
    }
    if (outUsage) {
        *outUsage = usage;
    }
    return total;
}

void fillDeviceCapabilities(hcs_device_t* device, VkPhysicalDevice physical,
                            const VkPhysicalDeviceProperties& props)
{
    hcsDeviceCaps& caps = device->caps;
    caps.struct_size = sizeof(caps);
    caps.capabilities = HCS_CAP_BASE_COMPUTE;
    caps.max_workgroup_invocations = props.limits.maxComputeWorkGroupInvocations;
    caps.max_workgroup_size[0] = props.limits.maxComputeWorkGroupSize[0];
    caps.max_workgroup_size[1] = props.limits.maxComputeWorkGroupSize[1];
    caps.max_workgroup_size[2] = props.limits.maxComputeWorkGroupSize[2];
    caps.max_storage_buffer_range = props.limits.maxStorageBufferRange;
    caps.max_memory_allocation_count = props.limits.maxMemoryAllocationCount;
    caps.shared_memory_bytes = props.limits.maxComputeSharedMemorySize;

    if (g_instanceApiVersion >= VK_API_VERSION_1_2 && g_api.vkGetPhysicalDeviceFeatures2) {
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;
        g_api.vkGetPhysicalDeviceFeatures2(physical, &features2);
        if (features12.shaderFloat16) {
            caps.capabilities |= HCS_CAP_FP16;
        }
        if (features12.shaderInt8) {
            caps.capabilities |= HCS_CAP_INT8;
        }
    }

    if (g_instanceApiVersion >= VK_API_VERSION_1_1 && g_api.vkGetPhysicalDeviceProperties2) {
        VkPhysicalDeviceSubgroupProperties subgroup{};
        subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &subgroup;
        g_api.vkGetPhysicalDeviceProperties2(physical, &props2);
        if ((subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0 &&
            subgroup.subgroupSize > 0) {
            caps.capabilities |= HCS_CAP_SUBGROUP;
            caps.subgroup_size = subgroup.subgroupSize;
        }
    }

    VkPhysicalDeviceMemoryProperties memProps{};
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    bool hasBudget = false;
    if (queryMemoryBudget(physical, &memProps, &budget, &hasBudget)) {
        uint64_t usage = 0;
        caps.total_memory_bytes = heapTotals(memProps, &budget, hasBudget, &usage);
        if (hasBudget) {
            caps.capabilities |= HCS_CAP_MEMORY_BUDGET;
        }
        uint64_t budgetSum = 0;
        if (hasBudget) {
            for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
                if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                    budgetSum += budget.heapBudget[i];
                }
            }
        }
        device->budget_bytes = budgetSum > 0 ? budgetSum : caps.total_memory_bytes / 2;
    }
}

bool hasComputeQueue(VkPhysicalDevice physical, uint32_t* familyIndex)
{
    uint32_t count = 0;
    g_api.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    if (count == 0) {
        return false;
    }
    std::vector<VkQueueFamilyProperties> families(count);
    g_api.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    for (uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
            if (familyIndex) {
                *familyIndex = i;
            }
            return true;
        }
    }
    return false;
}

void populateDeviceRecord(hcs_device_t* device, VkPhysicalDevice physical, uint32_t index)
{
    device->backend = HCS_BACKEND_VULKAN;
    device->backend_index = index;
    device->native = reinterpret_cast<void*>(physical);
    device->id = (static_cast<uint64_t>(HCS_BACKEND_VULKAN) << 32) | (index + 1u);

    VkPhysicalDeviceProperties props{};
    g_api.vkGetPhysicalDeviceProperties(physical, &props);

    hcsDeviceInfo& info = device->info;
    info.struct_size = sizeof(info);
    info.backend = HCS_BACKEND_VULKAN;
    info.device_id = device->id;
    info.vendor_id = props.vendorID;
    info.device_number = props.deviceID;
    info.api_version = props.apiVersion;
    info.driver_version = props.driverVersion;
    std::snprintf(info.name, sizeof(info.name), "%s", props.deviceName);
    std::snprintf(info.vendor, sizeof(info.vendor), "%s", vkVendorName(props.vendorID));
    std::snprintf(info.driver_info, sizeof(info.driver_info),
                  "Vulkan %u.%u.%u, driver 0x%08x",
                  VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                  VK_VERSION_PATCH(props.apiVersion), props.driverVersion);

    switch (props.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        info.kind = HCS_DEVICE_KIND_INTEGRATED_GPU;
        info.flags |= HCS_DEVICE_FLAG_INTEGRATED;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        info.kind = HCS_DEVICE_KIND_DISCRETE_GPU;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        info.kind = HCS_DEVICE_KIND_VIRTUAL_GPU;
        break;
    default:
        info.kind = HCS_DEVICE_KIND_OTHER_GPU;
        break;
    }

    fillDeviceCapabilities(device, physical, props);

    uint32_t family = 0;
    if (hasComputeQueue(physical, &family)) {
        device->caps.compute_queue_count = 1;
    }

    device->score = 50;
    if (info.kind == HCS_DEVICE_KIND_DISCRETE_GPU) {
        device->score += 30;
    } else if (info.kind == HCS_DEVICE_KIND_INTEGRATED_GPU) {
        device->score += 10;
    }
    if (device->caps.capabilities & HCS_CAP_MEMORY_BUDGET) {
        device->score += 5;
    }
    if (device->caps.capabilities & HCS_CAP_SUBGROUP) {
        device->score += 3;
    }
}

struct SpvBlob {
    const unsigned char* data;
    unsigned int size;
};

SpvBlob blobFor(KernelId id)
{
    switch (id) {
    case KernelId::VecAddF32: return {hcs_spv_vecadd, hcs_spv_vecadd_len};
    case KernelId::ScaleF32: return {hcs_spv_scale, hcs_spv_scale_len};
    case KernelId::ReduceSumF32: return {hcs_spv_reduce_sum, hcs_spv_reduce_sum_len};
    case KernelId::MatMulF32: return {hcs_spv_matmul, hcs_spv_matmul_len};
    case KernelId::ScanF32: return {hcs_spv_scan, hcs_spv_scan_len};
    case KernelId::TransposeF32: return {hcs_spv_transpose, hcs_spv_transpose_len};
    default: return {nullptr, 0};
    }
}

struct PendingSubmission {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

/* A flow owns the fences of all submissions made through it. Completion is a monotone
 * counter; HEvent waits against that counter without needing a second Vulkan object. */
class VulkanFlow final : public BackendFlow {
public:
    VulkanFlow(VkDevice device, std::function<void(const PendingSubmission&)> release)
        : device_(device), release_(std::move(release))
    {
    }

    ~VulkanFlow() override { detach(); }

    void attach(PendingSubmission item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) {
            pending_.push_back(item);
        } else {
            release_(item);
        }
    }

    hcsResult synchronize(uint64_t timeout_ns) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || !device_) {
            return fail(HCS_E_NOT_INITIALIZED, "vulkan.flowSync", "flow is detached");
        }
        std::vector<VkFence> fences;
        fences.reserve(pending_.size());
        for (const auto& item : pending_) {
            fences.push_back(item.fence);
        }
        if (!fences.empty()) {
            const VkResult vk = g_api.vkWaitForFences(device_, static_cast<uint32_t>(fences.size()),
                                                     fences.data(), VK_TRUE, timeout_ns);
            if (vk == VK_TIMEOUT) {
                return fail(HCS_E_TIMEOUT, "vulkan.flowSync", "fence wait timed out");
            }
            if (vk != VK_SUCCESS) {
                return mapVkResult(vk, "vulkan.flowSync");
            }
        }
        const uint64_t finished = pending_.size();
        for (const auto& item : pending_) {
            release_(item);
        }
        pending_.clear();
        completed_.fetch_add(finished, std::memory_order_release);
        return HCS_SUCCESS;
    }

    uint64_t completed() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t ready = completed_.load(std::memory_order_acquire);
        /* Submissions on one queue complete in submission order, so the count of ready
         * fences is a prefix. This lets HEvent wait without an explicit flow sync. */
        for (const auto& item : pending_) {
            if (g_api.vkGetFenceStatus(device_, item.fence) != VK_SUCCESS) {
                break;
            }
            ++ready;
        }
        return ready;
    }

    /* Called only while the owning context holds its mutex and has waited for device idle. */
    void dropPendingAfterPoolReset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
    }

    void detach()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) {
            return;
        }
        if (device_ && !pending_.empty()) {
            std::vector<VkFence> fences;
            fences.reserve(pending_.size());
            for (const auto& item : pending_) {
                fences.push_back(item.fence);
            }
            g_api.vkWaitForFences(device_, static_cast<uint32_t>(fences.size()), fences.data(),
                                  VK_TRUE, UINT64_MAX);
        }
        for (const auto& item : pending_) {
            release_(item);
        }
        pending_.clear();
        active_ = false;
        device_ = VK_NULL_HANDLE;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::function<void(const PendingSubmission&)> release_;
    std::vector<PendingSubmission> pending_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> completed_{0};
    bool active_ = true;
};

class VulkanContext final : public BackendContext {
public:
    VulkanContext(hcs_device_t* device, uint64_t memoryCap)
        : deviceRecord_(device), memoryCap_(memoryCap)
    {
    }

    ~VulkanContext() override { destroy(); }

    const char* name() const override { return "vulkan"; }

    bool init()
    {
        physical_ = reinterpret_cast<VkPhysicalDevice>(deviceRecord_->native);
        if (!physical_ || g_instance == VK_NULL_HANDLE) {
            fail(HCS_E_NO_DEVICE, "vulkan.init", "no usable Vulkan physical device");
            return false;
        }
        if (!hasComputeQueue(physical_, &queueFamily_)) {
            fail(HCS_E_UNSUPPORTED_FEATURE, "vulkan.init",
                 "device has no compute-capable queue family");
            return false;
        }

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily_;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;

        VkPhysicalDeviceFeatures features{};
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.pEnabledFeatures = &features;

        VkResult result =
            g_api.vkCreateDevice(physical_, &deviceInfo, nullptr, &device_);
        if (result != VK_SUCCESS) {
            mapVkResult(result, "vulkan.createDevice");
            return false;
        }
        if (!g_api.loadDevice(device_)) {
            auto destroy = reinterpret_cast<PFN_vkDestroyDevice>(
                g_api.vkGetDeviceProcAddr(device_, "vkDestroyDevice"));
            if (destroy) {
                destroy(device_, nullptr);
            }
            device_ = VK_NULL_HANDLE;
            fail(HCS_E_INTERNAL, "vulkan.loadDevice",
                 "required Vulkan device functions are unavailable");
            return false;
        }
        g_api.vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        if (g_api.vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) !=
            VK_SUCCESS) {
            fail(HCS_E_INTERNAL, "vulkan.createCommandPool", "command pool creation failed");
            return false;
        }

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = commandPool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (g_api.vkAllocateCommandBuffers(device_, &alloc, &commandBuffer_) != VK_SUCCESS) {
            fail(HCS_E_INTERNAL, "vulkan.allocateCommandBuffers",
                 "command buffer allocation failed");
            return false;
        }

        if (!createDescriptorLayouts()) {
            return false;
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (g_api.vkCreateFence(device_, &fenceInfo, nullptr, &fence_) != VK_SUCCESS) {
            fail(HCS_E_INTERNAL, "vulkan.createFence", "fence creation failed");
            return false;
        }

        if (memoryCap_ == 0) {
            const uint64_t budget = deviceRecord_->budget_bytes;
            memoryCap_ = budget > 0 ? (budget / 4) * 3 : (uint64_t{1} << 30);
            /* Never claim or consume an unbounded amount on an UMA device. */
            if (memoryCap_ > (uint64_t{6} << 30)) {
                memoryCap_ = (uint64_t{6} << 30);
            }
        }
        return true;
    }

    hcsResult createBuffer(hcs_buffer_t* buffer) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!device_) {
            return fail(HCS_E_NOT_INITIALIZED, "vulkan.bufferCreate", "context is not ready");
        }

        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = buffer->size;
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer vkBuffer = VK_NULL_HANDLE;
        VkResult result = g_api.vkCreateBuffer(device_, &info, nullptr, &vkBuffer);
        if (result != VK_SUCCESS) {
            return mapVkResult(result, "vulkan.bufferCreate");
        }

        VkMemoryRequirements requirements{};
        g_api.vkGetBufferMemoryRequirements(device_, vkBuffer, &requirements);
        if (memoryCap_ > 0 && allocated_ + requirements.size > memoryCap_) {
            g_api.vkDestroyBuffer(device_, vkBuffer, nullptr);
            return fail(HCS_E_OUT_OF_MEMORY, "vulkan.bufferCreate",
                        "allocation would exceed the configured memory cap");
        }

        VkPhysicalDeviceMemoryProperties memProps{};
        g_api.vkGetPhysicalDeviceMemoryProperties(physical_, &memProps);
        const uint32_t typeIndex = findMemoryType(memProps, requirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (typeIndex == UINT32_MAX) {
            g_api.vkDestroyBuffer(device_, vkBuffer, nullptr);
            return fail(HCS_E_UNSUPPORTED_FEATURE, "vulkan.bufferCreate",
                        "no host-visible coherent memory type available");
        }

        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = typeIndex;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        result = g_api.vkAllocateMemory(device_, &alloc, nullptr, &memory);
        if (result != VK_SUCCESS) {
            g_api.vkDestroyBuffer(device_, vkBuffer, nullptr);
            return mapVkResult(result, "vulkan.allocateMemory");
        }
        result = g_api.vkBindBufferMemory(device_, vkBuffer, memory, 0);
        if (result != VK_SUCCESS) {
            g_api.vkFreeMemory(device_, memory, nullptr);
            g_api.vkDestroyBuffer(device_, vkBuffer, nullptr);
            return mapVkResult(result, "vulkan.bindBufferMemory");
        }

        void* mapped = nullptr;
        result = g_api.vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (result != VK_SUCCESS) {
            g_api.vkFreeMemory(device_, memory, nullptr);
            g_api.vkDestroyBuffer(device_, vkBuffer, nullptr);
            return mapVkResult(result, "vulkan.mapMemory");
        }

        buffer->vk_buffer = reinterpret_cast<void*>(vkBuffer);
        buffer->vk_memory = reinterpret_cast<void*>(memory);
        buffer->host = mapped;
        allocated_ += requirements.size;
        return HCS_SUCCESS;
    }

    void destroyBuffer(hcs_buffer_t* buffer) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto vkBuffer = reinterpret_cast<VkBuffer>(buffer->vk_buffer);
        auto memory = reinterpret_cast<VkDeviceMemory>(buffer->vk_memory);
        if (device_) {
            if (buffer->host && memory) {
                g_api.vkUnmapMemory(device_, memory);
            }
            if (vkBuffer) {
                g_api.vkDestroyBuffer(device_, vkBuffer, nullptr);
            }
            if (memory) {
                g_api.vkFreeMemory(device_, memory, nullptr);
            }
        }
        buffer->vk_buffer = nullptr;
        buffer->vk_memory = nullptr;
        buffer->host = nullptr;
    }

    std::unique_ptr<BackendFlow> createFlow(hcs_flow_t* flow) override
    {
        auto impl = std::make_unique<VulkanFlow>(
            device_, [this](const PendingSubmission& item) { releaseSubmission(item); });
        std::lock_guard<std::mutex> lock(mutex_);
        flows_.push_back({flow, impl.get()});
        return impl;
    }

    void destroyFlow(hcs_flow_t* flow) override
    {
        /* The hcs_flow_t object itself is being destroyed; drop the registration so the
         * context never touches a dangling pointer at shutdown. Its BackendFlow impl still
         * drains any in-flight work in its own destructor. */
        std::lock_guard<std::mutex> lock(mutex_);
        flows_.erase(std::remove_if(flows_.begin(), flows_.end(),
                                    [flow](const auto& entry) { return entry.first == flow; }),
                     flows_.end());
    }

    void releaseSubmission(const PendingSubmission& item)
    {
        if (!device_) {
            return;
        }
        if (item.fence) {
            g_api.vkDestroyFence(device_, item.fence, nullptr);
        }
        if (item.descriptorSet) {
            g_api.vkFreeDescriptorSets(device_, descriptorPool_, 1, &item.descriptorSet);
        }
        if (item.commandBuffer) {
            g_api.vkFreeCommandBuffers(device_, commandPool_, 1, &item.commandBuffer);
        }
    }

    hcsResult dispatch(hcs_kernel_t* kernel, const hcsDispatchDesc* desc,
                       hcsFlow flow) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!device_) {
            return fail(HCS_E_NOT_INITIALIZED, "vulkan.dispatch", "context is not ready");
        }
        if (desc->buffer_count > 8) {
            return fail(HCS_E_INVALID_ARGUMENT, "vulkan.dispatch",
                        "at most 8 storage buffers are supported");
        }
        for (uint32_t i = 0; i < desc->buffer_count; ++i) {
            if (!desc->buffers[i]->vk_buffer) {
                return fail(HCS_E_INVALID_ARGUMENT, "vulkan.dispatch",
                            "buffer has no Vulkan storage binding");
            }
        }

        VkPipeline pipeline = VK_NULL_HANDLE;
        hcsResult result = pipelineFor(kernel, &pipeline);
        if (result != HCS_SUCCESS) {
            return result;
        }

        VkCommandBufferAllocateInfo commandAlloc{};
        commandAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAlloc.commandPool = commandPool_;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkResult vk = g_api.vkAllocateCommandBuffers(device_, &commandAlloc, &commandBuffer);
        if (vk != VK_SUCCESS) {
            return mapVkResult(vk, "vulkan.allocateCommandBuffer");
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorLayout_;
        VkDescriptorSet set = VK_NULL_HANDLE;
        vk = g_api.vkAllocateDescriptorSets(device_, &setInfo, &set);
        if (vk != VK_SUCCESS) {
            /* No in-flight set is ever reset; if the pool fills up, wait for the device,
             * reset it once, and retry. Never disturb a submission that is still reading. */
            g_api.vkDeviceWaitIdle(device_);
            g_api.vkResetDescriptorPool(device_, descriptorPool_, 0);
            for (auto& entry : flows_) {
                entry.second->dropPendingAfterPoolReset();
            }
            vk = g_api.vkAllocateDescriptorSets(device_, &setInfo, &set);
            if (vk != VK_SUCCESS) {
                g_api.vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
                return mapVkResult(vk, "vulkan.allocateDescriptorSet");
            }
        }

        VkDescriptorBufferInfo bufferInfos[8]{};
        VkWriteDescriptorSet writes[8]{};
        for (uint32_t i = 0; i < desc->buffer_count; ++i) {
            bufferInfos[i].buffer = reinterpret_cast<VkBuffer>(desc->buffers[i]->vk_buffer);
            bufferInfos[i].offset = 0;
            bufferInfos[i].range = VK_WHOLE_SIZE;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &bufferInfos[i];
        }
        if (desc->buffer_count > 0) {
            g_api.vkUpdateDescriptorSets(device_, desc->buffer_count, writes, 0, nullptr);
        }

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk = g_api.vkBeginCommandBuffer(commandBuffer, &begin);
        if (vk != VK_SUCCESS) {
            return mapVkResult(vk, "vulkan.beginCommandBuffer");
        }
        g_api.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        if (desc->buffer_count > 0) {
            g_api.vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                          pipelineLayout_, 0, 1, &set, 0, nullptr);
        }
        if (desc->push_constants_size > 0) {
            g_api.vkCmdPushConstants(commandBuffer, pipelineLayout_,
                                     VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     desc->push_constants_size, desc->push_constants);
        }
        g_api.vkCmdDispatch(commandBuffer, desc->group_count_x, desc->group_count_y,
                            desc->group_count_z);
        vk = g_api.vkEndCommandBuffer(commandBuffer);
        if (vk != VK_SUCCESS) {
            return mapVkResult(vk, "vulkan.endCommandBuffer");
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        vk = g_api.vkCreateFence(device_, &fenceInfo, nullptr, &fence);
        if (vk != VK_SUCCESS) {
            return mapVkResult(vk, "vulkan.createFence");
        }

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffer;
        vk = g_api.vkQueueSubmit(queue_, 1, &submit, fence);
        if (vk != VK_SUCCESS) {
            g_api.vkDestroyFence(device_, fence, nullptr);
            g_api.vkFreeDescriptorSets(device_, descriptorPool_, 1, &set);
            g_api.vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
            return mapVkResult(vk, "vulkan.queueSubmit");
        }

        PendingSubmission item{commandBuffer, fence, set};
        VulkanFlow* targetFlow = flow && flow->impl
                                     ? dynamic_cast<VulkanFlow*>(flow->impl.get())
                                     : nullptr;
        if (targetFlow) {
            targetFlow->attach(item);
            return HCS_SUCCESS; /* asynchronous: completion is observed via the flow */
        }
        /* No flow supplied: preserve the original synchronous semantics. */
        vk = g_api.vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        if (vk != VK_SUCCESS) {
            return mapVkResult(vk, "vulkan.waitForFence");
        }
        releaseSubmission(item);
        return HCS_SUCCESS;
    }

    hcsResult synchronize() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!device_) {
            return HCS_E_NOT_INITIALIZED;
        }
        return mapVkResult(g_api.vkDeviceWaitIdle(device_), "vulkan.deviceWaitIdle");
    }

private:
    void destroy()
    {
        if (!device_) {
            return;
        }
        g_api.vkDeviceWaitIdle(device_);
        for (auto& entry : flows_) {
            entry.second->detach();
        }
        flows_.clear();
        for (auto& entry : pipelines_) {
            g_api.vkDestroyPipeline(device_, entry.second, nullptr);
        }
        pipelines_.clear();
        if (descriptorPool_) {
            g_api.vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        if (pipelineLayout_) {
            g_api.vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (descriptorLayout_) {
            g_api.vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
        }
        if (fence_) {
            g_api.vkDestroyFence(device_, fence_, nullptr);
        }
        if (commandPool_) {
            g_api.vkDestroyCommandPool(device_, commandPool_, nullptr);
        }
        g_api.vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    static uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& props,
                                   uint32_t typeBits, VkMemoryPropertyFlags required)
    {
        uint32_t fallback = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) == 0) {
                continue;
            }
            const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
            if ((flags & required) == required) {
                if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                    return i;
                }
                if (fallback == UINT32_MAX) {
                    fallback = i;
                }
            }
        }
        return fallback;
    }

    bool createDescriptorLayouts()
    {
        VkDescriptorSetLayoutBinding bindings[8]{};
        for (uint32_t i = 0; i < 8; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 8;
        layoutInfo.pBindings = bindings;
        if (g_api.vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
                                              &descriptorLayout_) != VK_SUCCESS) {
            fail(HCS_E_INTERNAL, "vulkan.createDescriptorSetLayout",
                 "descriptor set layout creation failed");
            return false;
        }

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push.offset = 0;
        push.size = kMaxPushConstants;

        VkPipelineLayoutCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineInfo.setLayoutCount = 1;
        pipelineInfo.pSetLayouts = &descriptorLayout_;
        pipelineInfo.pushConstantRangeCount = 1;
        pipelineInfo.pPushConstantRanges = &push;
        if (g_api.vkCreatePipelineLayout(device_, &pipelineInfo, nullptr, &pipelineLayout_) !=
            VK_SUCCESS) {
            fail(HCS_E_INTERNAL, "vulkan.createPipelineLayout",
                 "pipeline layout creation failed");
            return false;
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 4096;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        /* Per-submission descriptor sets are freed individually after their fence completes. */
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 512;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (g_api.vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) !=
            VK_SUCCESS) {
            fail(HCS_E_INTERNAL, "vulkan.createDescriptorPool",
                 "descriptor pool creation failed");
            return false;
        }
        return true;
    }

    hcsResult createProgram(hcs_program_t* program, const void* data, size_t byteSize) override
    {
        if (!program || !data || byteSize == 0) {
            return fail(HCS_E_INVALID_ARGUMENT, "vulkan.createProgram",
                        "program, data and size must be valid");
        }
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        program->spirv.assign(bytes, bytes + byteSize);
        return HCS_SUCCESS;
    }

    hcsResult createProgramKernel(hcs_program_t* program, hcs_kernel_t*) override
    {
        if (!program || program->spirv.empty()) {
            return fail(HCS_E_INVALID_ARGUMENT, "vulkan.createProgramKernel",
                        "program has no SPIR-V module");
        }
        return HCS_SUCCESS;
    }

    hcsResult pipelineFor(hcs_kernel_t* kernel, VkPipeline* out)
    {
        const uint64_t key = kernel->def ? (0x10000ull + static_cast<uint64_t>(kernel->def->id))
                                         : kernel->uid;
        auto it = pipelines_.find(key);
        if (it != pipelines_.end()) {
            *out = it->second;
            return HCS_SUCCESS;
        }
        const unsigned char* bytes = nullptr;
        size_t byteCount = 0;
        SpvBlob blob{nullptr, 0};
        if (kernel->def) {
            blob = blobFor(kernel->def->id);
            bytes = blob.data;
            byteCount = blob.size;
        } else if (kernel->program) {
            bytes = kernel->program->spirv.data();
            byteCount = kernel->program->spirv.size();
        }
        if (!bytes || byteCount == 0) {
            return fail(HCS_E_KERNEL_NOT_FOUND, "vulkan.pipeline",
                        "no SPIR-V module for this kernel");
        }
        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = byteCount;
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(bytes);
        VkShaderModule module = VK_NULL_HANDLE;
        VkResult vk =
            g_api.vkCreateShaderModule(device_, &moduleInfo, nullptr, &module);
        if (vk != VK_SUCCESS) {
            return mapVkResult(vk, "vulkan.createShaderModule");
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipelineLayout_;
        VkPipeline pipeline = VK_NULL_HANDLE;
        vk = g_api.vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                            nullptr, &pipeline);
        g_api.vkDestroyShaderModule(device_, module, nullptr);
        if (vk != VK_SUCCESS) {
            return mapVkResult(vk, "vulkan.createComputePipeline");
        }
        pipelines_[key] = pipeline;
        *out = pipeline;
        return HCS_SUCCESS;
    }

    hcs_device_t* deviceRecord_ = nullptr;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkPipeline> pipelines_;
    std::vector<std::pair<hcs_flow_t*, VulkanFlow*>> flows_;
    std::mutex mutex_;
    uint64_t memoryCap_ = 0;
    uint64_t allocated_ = 0;
};

} // namespace

void enumerateVulkanDevices(std::vector<std::unique_ptr<hcs_device_t>>& out)
{
    if (!initVulkanGlobal()) {
        std::fprintf(stderr,
                     "[hcs] Vulkan loader not usable; CPU backend remains available\n");
        return;
    }
    uint32_t count = 0;
    if (g_api.vkEnumeratePhysicalDevices(g_instance, &count, nullptr) != VK_SUCCESS ||
        count == 0) {
        return;
    }
    std::vector<VkPhysicalDevice> physical(count);
    if (g_api.vkEnumeratePhysicalDevices(g_instance, &count, physical.data()) !=
        VK_SUCCESS) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t family = 0;
        if (!hasComputeQueue(physical[i], &family)) {
            continue;
        }
        auto device = std::make_unique<hcs_device_t>();
        populateDeviceRecord(device.get(), physical[i], i);
        out.push_back(std::move(device));
    }
}

void shutdownVulkanGlobal()
{
    if (g_initialized && g_instance != VK_NULL_HANDLE) {
        g_api.vkDestroyInstance(g_instance, nullptr);
        g_instance = VK_NULL_HANDLE;
        g_initialized = false;
    }
    /* The DLL handle is intentionally kept for the process lifetime. */
}

hcsResult hcsVulkanQueryBudget(hcs_device_t* device, hcsMemoryBudget* io)
{
    if (!device || !device->native || !g_initialized) {
        return HCS_E_NOT_INITIALIZED;
    }
    auto physical = reinterpret_cast<VkPhysicalDevice>(device->native);
    VkPhysicalDeviceMemoryProperties props{};
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    bool hasBudget = false;
    if (!queryMemoryBudget(physical, &props, &budget, &hasBudget)) {
        return HCS_E_UNSUPPORTED_FEATURE;
    }
    uint64_t usage = 0;
    io->heap_bytes = heapTotals(props, &budget, hasBudget, &usage);
    io->usage_bytes = usage;
    if (hasBudget) {
        uint64_t budgetSum = 0;
        for (uint32_t i = 0; i < props.memoryHeapCount; ++i) {
            if (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                budgetSum += budget.heapBudget[i];
            }
        }
        io->budget_bytes = budgetSum;
        io->available_bytes = budgetSum > usage ? budgetSum - usage : 0;
    } else {
        io->budget_bytes = io->heap_bytes;
        io->available_bytes =
            io->heap_bytes > usage ? io->heap_bytes - usage : 0;
    }
    return HCS_SUCCESS;
}

std::unique_ptr<BackendContext> createVulkanContext(hcs_device_t* device, uint64_t memoryCap)
{
    auto context = std::make_unique<VulkanContext>(device, memoryCap);
    if (!context->init()) {
        return nullptr;
    }
    return context;
}

} // namespace hcs

#else /* HCS_ENABLE_VULKAN */

namespace hcs {

void enumerateVulkanDevices(std::vector<std::unique_ptr<hcs_device_t>>& out)
{
    (void)out;
}

void shutdownVulkanGlobal() {}

hcsResult hcsVulkanQueryBudget(hcs_device_t* device, hcsMemoryBudget* io)
{
    (void)device;
    (void)io;
    return HCS_E_UNSUPPORTED_FEATURE;
}

std::unique_ptr<BackendContext> createVulkanContext(hcs_device_t* device, uint64_t memoryCap)
{
    (void)device;
    (void)memoryCap;
    fail(HCS_E_UNSUPPORTED_FEATURE, "createVulkanContext",
         "this build was configured without the Vulkan backend");
    return nullptr;
}

} // namespace hcs

#endif
