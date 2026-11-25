/*
 * Runtime entry points of the stable C ABI: lifetime, enumeration, handle
 * validation, and dispatch routing to the selected backend.
 */

#include "core/internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

std::mutex g_mutex;
bool g_initialized = false;
std::vector<std::unique_ptr<hcs_device_t>> g_devices;

thread_local hcsErrorInfo g_lastError{};
thread_local bool g_hasError = false;
std::atomic<uint64_t> g_errorCounter{0};

const hcs::KernelDef kKernels[] = {
    {"vecadd_f32", hcs::KernelId::VecAddF32, 64,
     "c[i] = a[i] + alpha * b[i]"},
    {"scale_f32", hcs::KernelId::ScaleF32, 64,
     "y[i] = alpha * x[i]"},
    {"reduce_sum_f32", hcs::KernelId::ReduceSumF32, 256,
     "out[group] = sum of a contiguous chunk of x"},
    {"matmul_f32", hcs::KernelId::MatMulF32, 16,
     "c[m,n] = a[m,k] * b[k,n], row major, 16x16 tiles"},
};

constexpr uint32_t kKernelCount = sizeof(kKernels) / sizeof(kKernels[0]);

void makeCorrelationId(char* out, size_t size)
{
    const uint64_t counter = g_errorCounter.fetch_add(1) + 1;
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::snprintf(out, size, "hcs-%llx-%llx",
                  static_cast<unsigned long long>(now & 0xFFFFFFFFull),
                  static_cast<unsigned long long>(counter));
}

} // namespace

namespace hcs {

const KernelDef* kernelTable(uint32_t* count)
{
    if (count) {
        *count = kKernelCount;
    }
    return kKernels;
}

const KernelDef* findKernel(const char* name)
{
    if (!name) {
        return nullptr;
    }
    for (uint32_t i = 0; i < kKernelCount; ++i) {
        if (std::strcmp(kKernels[i].name, name) == 0) {
            return &kKernels[i];
        }
    }
    return nullptr;
}

const KernelDef* kernelById(KernelId id)
{
    for (uint32_t i = 0; i < kKernelCount; ++i) {
        if (kKernels[i].id == id) {
            return &kKernels[i];
        }
    }
    return nullptr;
}

void setLastError(hcsResult code, const char* operation, const std::string& message,
                  int backendResult)
{
    g_lastError = hcsErrorInfo{};
    g_lastError.struct_size = sizeof(g_lastError);
    g_lastError.code = static_cast<int32_t>(code);
    g_lastError.backend_result = backendResult;
    std::snprintf(g_lastError.message, sizeof(g_lastError.message), "%s", message.c_str());
    std::snprintf(g_lastError.operation, sizeof(g_lastError.operation), "%s",
                  operation ? operation : "unknown");
    makeCorrelationId(g_lastError.correlation_id, sizeof(g_lastError.correlation_id));
    g_hasError = true;
}

hcsResult fail(hcsResult code, const char* operation, const std::string& message,
               int backendResult)
{
    setLastError(code, operation, message, backendResult);
    return code;
}

} // namespace hcs

extern "C" {

const char* hcsResultString(hcsResult result)
{
    switch (result) {
    case HCS_SUCCESS: return "HCS_SUCCESS";
    case HCS_E_INVALID_ARGUMENT: return "HCS_E_INVALID_ARGUMENT";
    case HCS_E_NOT_INITIALIZED: return "HCS_E_NOT_INITIALIZED";
    case HCS_E_ALREADY_INITIALIZED: return "HCS_E_ALREADY_INITIALIZED";
    case HCS_E_NO_DEVICE: return "HCS_E_NO_DEVICE";
    case HCS_E_UNSUPPORTED_API_VERSION: return "HCS_E_UNSUPPORTED_API_VERSION";
    case HCS_E_UNSUPPORTED_FEATURE: return "HCS_E_UNSUPPORTED_FEATURE";
    case HCS_E_OUT_OF_MEMORY: return "HCS_E_OUT_OF_MEMORY";
    case HCS_E_DEVICE_LOST: return "HCS_E_DEVICE_LOST";
    case HCS_E_SHADER_COMPILE: return "HCS_E_SHADER_COMPILE";
    case HCS_E_SPIRV_INVALID: return "HCS_E_SPIRV_INVALID";
    case HCS_E_KERNEL_NOT_FOUND: return "HCS_E_KERNEL_NOT_FOUND";
    case HCS_E_TIMEOUT: return "HCS_E_TIMEOUT";
    case HCS_E_IO: return "HCS_E_IO";
    case HCS_E_MODEL_CORRUPT: return "HCS_E_MODEL_CORRUPT";
    case HCS_E_TRANSLATION_UNSUPPORTED: return "HCS_E_TRANSLATION_UNSUPPORTED";
    case HCS_E_SECURITY_POLICY: return "HCS_E_SECURITY_POLICY";
    case HCS_E_INTERNAL: return "HCS_E_INTERNAL";
    default: return "HCS_E_UNKNOWN";
    }
}

hcsResult hcsGetLastError(hcsErrorInfo* out_info)
{
    if (!out_info || out_info->struct_size < sizeof(hcsErrorInfo)) {
        return HCS_E_INVALID_ARGUMENT;
    }
    *out_info = g_lastError;
    out_info->struct_size = sizeof(hcsErrorInfo);
    return g_hasError ? static_cast<hcsResult>(g_lastError.code) : HCS_SUCCESS;
}

hcsResult hcsInit(const hcsInitDesc* desc)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        return hcs::fail(HCS_E_ALREADY_INITIALIZED, "hcsInit",
                         "HCSUDA runtime is already initialized");
    }
    if (desc && desc->struct_size != sizeof(hcsInitDesc)) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsInit",
                         "hcsInitDesc.struct_size does not match this ABI");
    }

    g_devices.clear();
    hcs::enumerateCpuDevices(g_devices);
    hcs::enumerateVulkanDevices(g_devices);

    /* Stable order: highest capability score first, then device id. */
    std::stable_sort(g_devices.begin(), g_devices.end(),
                     [](const std::unique_ptr<hcs_device_t>& a,
                        const std::unique_ptr<hcs_device_t>& b) {
                         if (a->score != b->score) {
                             return a->score > b->score;
                         }
                         return a->id < b->id;
                     });

    g_initialized = true;
    g_hasError = false;
    return HCS_SUCCESS;
}

hcsResult hcsShutdown(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return HCS_SUCCESS;
    }
    g_devices.clear();
    hcs::shutdownVulkanGlobal();
    g_initialized = false;
    return HCS_SUCCESS;
}

const char* hcsGetVersionString(void) { return HCS_VERSION_STRING; }

uint32_t hcsGetApiVersion(void) { return HCS_API_VERSION; }

hcsResult hcsEnumerateDevices(hcsDeviceInfo* out, uint32_t* count)
{
    if (!count) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsEnumerateDevices",
                         "count pointer must not be null");
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return hcs::fail(HCS_E_NOT_INITIALIZED, "hcsEnumerateDevices",
                         "call hcsInit before enumerating devices");
    }
    const uint32_t available = static_cast<uint32_t>(g_devices.size());
    if (!out) {
        *count = available;
        return HCS_SUCCESS;
    }
    const uint32_t capacity = *count;
    const uint32_t written = std::min(capacity, available);
    for (uint32_t i = 0; i < written; ++i) {
        out[i] = g_devices[i]->info;
        out[i].struct_size = sizeof(hcsDeviceInfo);
    }
    *count = written;
    return written < available ? HCS_E_INVALID_ARGUMENT : HCS_SUCCESS;
}

hcsResult hcsGetDevice(uint32_t index, hcsDevice* out_device)
{
    if (!out_device) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGetDevice",
                         "output device pointer must not be null");
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return hcs::fail(HCS_E_NOT_INITIALIZED, "hcsGetDevice",
                         "call hcsInit before requesting a device");
    }
    if (index >= g_devices.size()) {
        return hcs::fail(HCS_E_NO_DEVICE, "hcsGetDevice", "device index out of range");
    }
    *out_device = g_devices[index].get();
    return HCS_SUCCESS;
}

hcsResult hcsGetDeviceCaps(hcsDevice device, hcsDeviceCaps* caps)
{
    if (!device || !caps || caps->struct_size < sizeof(hcsDeviceCaps)) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGetDeviceCaps",
                         "device or caps must not be null");
    }
    *caps = device->caps;
    caps->struct_size = sizeof(hcsDeviceCaps);
    return HCS_SUCCESS;
}

hcsResult hcsGetMemoryBudget(hcsDevice device, hcsMemoryBudget* out)
{
    if (!device || !out || out->struct_size < sizeof(hcsMemoryBudget)) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGetMemoryBudget",
                         "device or budget must not be null");
    }
    out->struct_size = sizeof(hcsMemoryBudget);
    out->flags = 0;
    out->heap_bytes = device->caps.total_memory_bytes;
    out->budget_bytes = device->budget_bytes;
    if (device->backend == HCS_BACKEND_CPU) {
        out->usage_bytes = hcs::hostPhysicalMemory() - hcs::hostAvailableMemory();
        out->available_bytes = hcs::hostAvailableMemory();
        out->reserve_bytes = 0;
        out->flags = HCS_MEMORY_FLAG_INTEGRATED;
        return HCS_SUCCESS;
    }
    if (device->backend == HCS_BACKEND_VULKAN) {
        out->flags = (device->info.flags & HCS_DEVICE_FLAG_INTEGRATED)
                         ? HCS_MEMORY_FLAG_INTEGRATED
                         : 0;
        hcsMemoryBudget live = *out;
        hcsResult r = hcs::hcsVulkanQueryBudget(device, &live);
        if (r == HCS_SUCCESS) {
            *out = live;
            out->struct_size = sizeof(hcsMemoryBudget);
            return HCS_SUCCESS;
        }
    }
    out->usage_bytes = 0;
    out->available_bytes = 0;
    out->reserve_bytes = 0;
    return HCS_SUCCESS;
}

hcsResult hcsCreateContext(hcsDevice device, const hcsContextDesc* desc,
                           hcsContext* out_context)
{
    if (!device || !out_context) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsCreateContext",
                         "device and output context must not be null");
    }
    if (desc && desc->struct_size != sizeof(hcsContextDesc)) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsCreateContext",
                         "hcsContextDesc.struct_size does not match this ABI");
    }
    const uint64_t cap = desc ? desc->memory_cap_bytes : 0;
    auto context = std::make_unique<hcs_context_t>();
    context->device = device;
    if (device->backend == HCS_BACKEND_CPU) {
        context->impl = hcs::createCpuContext(device, cap);
    } else if (device->backend == HCS_BACKEND_VULKAN) {
        context->impl = hcs::createVulkanContext(device, cap);
    }
    if (!context->impl) {
        return hcs::fail(HCS_E_NO_DEVICE, "hcsCreateContext",
                         "backend context could not be created");
    }
    *out_context = context.release();
    return HCS_SUCCESS;
}

void hcsDestroyContext(hcsContext context)
{
    delete context;
}

hcsResult hcsBufferCreate(hcsContext context, const hcsBufferDesc* desc,
                          hcsBuffer* out_buffer)
{
    if (!context || !desc || !out_buffer) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferCreate",
                         "context, description and output must not be null");
    }
    if (desc->struct_size != sizeof(hcsBufferDesc)) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferCreate",
                         "hcsBufferDesc.struct_size does not match this ABI");
    }
    if (desc->size == 0) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferCreate",
                         "buffer size must be greater than zero");
    }
    auto buffer = std::make_unique<hcs_buffer_t>();
    buffer->context = context;
    buffer->size = desc->size;
    buffer->usage = desc->usage ? desc->usage
                                : (HCS_BUFFER_USAGE_STORAGE | HCS_BUFFER_USAGE_HOST_VISIBLE);
    hcsResult result = context->impl->createBuffer(buffer.get());
    if (result != HCS_SUCCESS) {
        return result;
    }
    *out_buffer = buffer.release();
    return HCS_SUCCESS;
}

hcsResult hcsBufferUpload(hcsBuffer buffer, uint64_t offset, const void* src,
                          uint64_t size)
{
    if (!buffer || !src) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferUpload",
                         "buffer and source must not be null");
    }
    if (offset > buffer->size || size > buffer->size - offset) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferUpload",
                         "upload range exceeds the buffer size");
    }
    std::memcpy(static_cast<uint8_t*>(buffer->host) + offset, src,
                static_cast<size_t>(size));
    return HCS_SUCCESS;
}

hcsResult hcsBufferDownload(hcsBuffer buffer, uint64_t offset, void* dst, uint64_t size)
{
    if (!buffer || !dst) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferDownload",
                         "buffer and destination must not be null");
    }
    if (offset > buffer->size || size > buffer->size - offset) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferDownload",
                         "download range exceeds the buffer size");
    }
    std::memcpy(dst, static_cast<uint8_t*>(buffer->host) + offset,
                static_cast<size_t>(size));
    return HCS_SUCCESS;
}

hcsResult hcsBufferClear(hcsBuffer buffer, uint64_t offset, uint64_t size)
{
    if (!buffer) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferClear",
                         "buffer must not be null");
    }
    if (offset > buffer->size || size > buffer->size - offset) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsBufferClear",
                         "clear range exceeds the buffer size");
    }
    std::memset(static_cast<uint8_t*>(buffer->host) + offset, 0,
                static_cast<size_t>(size));
    return HCS_SUCCESS;
}

void hcsBufferDestroy(hcsBuffer buffer)
{
    if (!buffer) {
        return;
    }
    if (buffer->context && buffer->context->impl) {
        buffer->context->impl->destroyBuffer(buffer);
    }
    delete buffer;
}

uint64_t hcsBufferSize(hcsBuffer buffer) { return buffer ? buffer->size : 0; }

hcsResult hcsFlowCreate(hcsContext context, const hcsFlowDesc* desc, hcsFlow* out_flow)
{
    if (!context || !out_flow) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsFlowCreate",
                         "context and output flow must not be null");
    }
    if (desc && desc->struct_size != sizeof(hcsFlowDesc)) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsFlowCreate",
                         "hcsFlowDesc.struct_size does not match this ABI");
    }
    auto flow = std::make_unique<hcs_flow_t>();
    flow->context = context;
    flow->impl = context->impl->createFlow(flow.get());
    if (!flow->impl) {
        return hcs::fail(HCS_E_INTERNAL, "hcsFlowCreate",
                         "backend did not create a flow implementation");
    }
    *out_flow = flow.release();
    return HCS_SUCCESS;
}

hcsResult hcsFlowSynchronize(hcsFlow flow, uint64_t timeout_ns)
{
    if (!flow || !flow->impl) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsFlowSynchronize",
                         "flow must not be null");
    }
    const hcsResult result = flow->impl->synchronize(timeout_ns);
    if (result == HCS_SUCCESS) {
        flow->submissions.store(0, std::memory_order_release);
    }
    return result;
}

void hcsFlowDestroy(hcsFlow flow)
{
    if (!flow) {
        return;
    }
    if (flow->context && flow->context->impl) {
        flow->context->impl->destroyFlow(flow);
    }
    delete flow;
}

hcsResult hcsEventCreate(hcsContext context, hcsEvent* out_event)
{
    if (!context || !out_event) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsEventCreate",
                         "context and output event must not be null");
    }
    auto event = std::make_unique<hcs_event_t>();
    event->context = context;
    *out_event = event.release();
    return HCS_SUCCESS;
}

hcsResult hcsEventRecord(hcsEvent event, hcsFlow flow)
{
    if (!event || !flow || !flow->impl || event->context != flow->context) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsEventRecord",
                         "event and flow must refer to the same valid context");
    }
    event->flow = flow;
    event->target = flow->submissions.load(std::memory_order_acquire);
    event->recorded = true;
    return HCS_SUCCESS;
}

hcsResult hcsEventWait(hcsEvent event, uint64_t timeout_ns)
{
    if (!event || !event->recorded || !event->flow || !event->flow->impl) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsEventWait",
                         "event has not been recorded on a valid flow");
    }
    const auto begin = std::chrono::steady_clock::now();
    for (;;) {
        const uint64_t completed = event->flow->impl->completed();
        if (completed >= event->target) {
            return HCS_SUCCESS;
        }
        if (timeout_ns != UINT64_MAX) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin);
            if (elapsed.count() >= static_cast<int64_t>(timeout_ns)) {
                return hcs::fail(HCS_E_TIMEOUT, "hcsEventWait",
                                 "event wait timed out");
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void hcsEventDestroy(hcsEvent event) { delete event; }

namespace {

struct GraphNode {
    hcsKernel kernel = nullptr;
    hcsDispatchDesc desc{};
    uint32_t dependencies[8]{};
    uint32_t dependency_count = 0;
};

} // namespace

struct hcs_graph_t {
    hcsContext context = nullptr;
    std::vector<GraphNode> nodes;
    bool compiled = false;
};

hcsResult hcsGraphCreate(hcsContext context, hcsGraph* out_graph)
{
    if (!context || !out_graph) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphCreate",
                         "context and output graph must not be null");
    }
    auto graph = std::make_unique<hcs_graph_t>();
    graph->context = context;
    *out_graph = graph.release();
    return HCS_SUCCESS;
}

hcsResult hcsGraphAddNode(hcsGraph graph, const hcsGraphNodeDesc* node)
{
    if (!graph || !node || node->struct_size != sizeof(hcsGraphNodeDesc) || !node->kernel ||
        !node->dispatch) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphAddNode",
                         "graph, node, kernel and dispatch description must be valid");
    }
    if (node->kernel->context != graph->context) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphAddNode",
                         "kernel belongs to a different context");
    }
    if (node->dependency_count > 8) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphAddNode",
                         "at most 8 dependencies per node are supported");
    }
    /* Dependencies must point backwards. That makes every accepted graph a DAG and keeps the
     * replay order identical to insertion order. */
    for (uint32_t i = 0; i < node->dependency_count; ++i) {
        if (node->dependencies[i] >= graph->nodes.size()) {
            return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphAddNode",
                             "dependency index refers to a node that does not exist yet");
        }
    }
    const hcsDispatchDesc& desc = *node->dispatch;
    if (desc.struct_size != sizeof(hcsDispatchDesc) || desc.buffer_count > hcs::kMaxBuffers ||
        desc.group_count_x == 0 || desc.group_count_y == 0 || desc.group_count_z == 0 ||
        desc.push_constants_size > hcs::kMaxPushConstants) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphAddNode",
                         "dispatch description is invalid");
    }
    for (uint32_t i = 0; i < desc.buffer_count; ++i) {
        if (!desc.buffers[i] || desc.buffers[i]->context != graph->context) {
            return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphAddNode",
                             "buffer is null or belongs to a different context");
        }
    }

    GraphNode stored;
    stored.kernel = node->kernel;
    stored.desc = desc;
    stored.dependency_count = node->dependency_count;
    std::memcpy(stored.dependencies, node->dependencies,
                node->dependency_count * sizeof(uint32_t));
    graph->nodes.push_back(stored);
    graph->compiled = false;
    return HCS_SUCCESS;
}

hcsResult hcsGraphCompile(hcsGraph graph)
{
    if (!graph) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphCompile", "graph must not be null");
    }
    if (graph->nodes.empty()) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphCompile", "graph has no nodes");
    }
    for (size_t i = 0; i < graph->nodes.size(); ++i) {
        const GraphNode& node = graph->nodes[i];
        for (uint32_t d = 0; d < node.dependency_count; ++d) {
            if (node.dependencies[d] >= i) {
                return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphCompile",
                                 "dependency is not strictly earlier in topological order");
            }
        }
    }
    graph->compiled = true;
    return HCS_SUCCESS;
}

hcsResult hcsGraphRun(hcsGraph graph, hcsFlow flow)
{
    if (!graph || !graph->compiled) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphRun",
                         "graph is null or has not been compiled");
    }
    if (flow && flow->context != graph->context) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsGraphRun",
                         "flow belongs to a different context");
    }
    for (const GraphNode& node : graph->nodes) {
        const hcsResult result = hcsDispatch(node.kernel, &node.desc, flow);
        if (result != HCS_SUCCESS) {
            return result;
        }
    }
    return HCS_SUCCESS;
}

void hcsGraphDestroy(hcsGraph graph) { delete graph; }

hcsResult hcsKernelCreate(hcsContext context, const char* name, hcsKernel* out_kernel)
{
    if (!context || !name || !out_kernel) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsKernelCreate",
                         "context, name and output must not be null");
    }
    const hcs::KernelDef* def = hcs::findKernel(name);
    if (!def) {
        return hcs::fail(HCS_E_KERNEL_NOT_FOUND, "hcsKernelCreate",
                         std::string("unknown built-in kernel: ") + name);
    }
    auto kernel = std::make_unique<hcs_kernel_t>();
    kernel->context = context;
    kernel->def = def;
    *out_kernel = kernel.release();
    return HCS_SUCCESS;
}

void hcsKernelDestroy(hcsKernel kernel) { delete kernel; }

hcsResult hcsProgramCreateSpirv(hcsContext context, const void* spirv, size_t byte_size,
                                hcsProgram* out_program)
{
    if (!context || !spirv || !out_program || byte_size == 0) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsProgramCreateSpirv",
                         "context, SPIR-V data and output must not be null/empty");
    }
    if (byte_size > 8u * 1024u * 1024u || byte_size % 4u != 0) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsProgramCreateSpirv",
                         "SPIR-V must be a 4-byte-aligned module of at most 8 MiB");
    }
    const uint32_t magic = *static_cast<const uint32_t*>(spirv);
    if (magic != 0x07230203u) {
        return hcs::fail(HCS_E_SPIRV_INVALID, "hcsProgramCreateSpirv",
                         "missing SPIR-V magic number 0x07230203");
    }
    auto program = std::make_unique<hcs_program_t>();
    program->context = context;
    const hcsResult result = context->impl->createProgram(program.get(), spirv, byte_size);
    if (result != HCS_SUCCESS) {
        return result;
    }
    *out_program = program.release();
    return HCS_SUCCESS;
}

void hcsProgramDestroy(hcsProgram program) { delete program; }

hcsResult hcsKernelFromProgram(hcsProgram program, hcsKernel* out_kernel)
{
    if (!program || !program->context || !out_kernel) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsKernelFromProgram",
                         "program and output must not be null");
    }
    auto kernel = std::make_unique<hcs_kernel_t>();
    kernel->context = program->context;
    kernel->program = program;
    static std::atomic<uint64_t> nextUid{1};
    kernel->uid = nextUid.fetch_add(1, std::memory_order_relaxed);
    const hcsResult result =
        program->context->impl->createProgramKernel(program, kernel.get());
    if (result != HCS_SUCCESS) {
        return result;
    }
    *out_kernel = kernel.release();
    return HCS_SUCCESS;
}


hcsResult hcsDispatch(hcsKernel kernel, const hcsDispatchDesc* desc, hcsFlow flow)
{
    if (!kernel || !desc) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsDispatch",
                         "kernel and dispatch description must not be null");
    }
    if (desc->struct_size != sizeof(hcsDispatchDesc)) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsDispatch",
                         "hcsDispatchDesc.struct_size does not match this ABI");
    }
    if (desc->buffer_count > hcs::kMaxBuffers) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsDispatch",
                         "buffer_count exceeds the supported maximum");
    }
    if (desc->group_count_x == 0 || desc->group_count_y == 0 || desc->group_count_z == 0 ||
        desc->push_constants_size > hcs::kMaxPushConstants) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsDispatch",
                         "group counts must be >= 1 and push constants <= 128 bytes");
    }
    for (uint32_t i = 0; i < desc->buffer_count; ++i) {
        if (!desc->buffers[i]) {
            return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsDispatch",
                             "dispatch references a null buffer");
        }
        if (desc->buffers[i]->context != kernel->context) {
            return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsDispatch",
                             "buffer belongs to a different context");
        }
    }
    if (flow && flow->context != kernel->context) {
        return hcs::fail(HCS_E_INVALID_ARGUMENT, "hcsDispatch",
                         "flow belongs to a different context");
    }
    hcsResult result = kernel->context->impl->dispatch(kernel, desc, flow);
    if (result == HCS_SUCCESS && flow) {
        flow->submissions.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

const char* hcsBackendName(uint32_t backend_kind)
{
    switch (backend_kind) {
    case HCS_BACKEND_CPU: return "CPU";
    case HCS_BACKEND_VULKAN: return "Vulkan";
    default: return "None";
    }
}

const char* hcsDeviceKindName(uint32_t device_kind)
{
    switch (device_kind) {
    case HCS_DEVICE_KIND_CPU: return "CPU";
    case HCS_DEVICE_KIND_INTEGRATED_GPU: return "Integrated GPU";
    case HCS_DEVICE_KIND_DISCRETE_GPU: return "Discrete GPU";
    case HCS_DEVICE_KIND_VIRTUAL_GPU: return "Virtual GPU";
    case HCS_DEVICE_KIND_OTHER_GPU: return "Other GPU";
    default: return "Unknown";
    }
}

} // extern "C"
