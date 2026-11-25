#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum hcsBackendKind {
    HCS_BACKEND_NONE = 0,
    HCS_BACKEND_CPU = 1,
    HCS_BACKEND_VULKAN = 2
} hcsBackendKind;

typedef enum hcsDeviceKind {
    HCS_DEVICE_KIND_CPU = 0,
    HCS_DEVICE_KIND_INTEGRATED_GPU = 1,
    HCS_DEVICE_KIND_DISCRETE_GPU = 2,
    HCS_DEVICE_KIND_VIRTUAL_GPU = 3,
    HCS_DEVICE_KIND_OTHER_GPU = 4
} hcsDeviceKind;

typedef enum hcsCapability {
    HCS_CAP_BASE_COMPUTE = 1u << 0,
    HCS_CAP_MEMORY_BUDGET = 1u << 1,
    HCS_CAP_ASYNC = 1u << 2,
    HCS_CAP_SUBGROUP = 1u << 3,
    HCS_CAP_FP16 = 1u << 4,
    HCS_CAP_INT8 = 1u << 5,
    HCS_CAP_MATRIX = 1u << 6,
    HCS_CAP_AVX2 = 1u << 7,
    HCS_CAP_TIMELINE_SEMAPHORE = 1u << 8
} hcsCapability;

typedef struct hcsDeviceInfo {
    uint32_t struct_size;
    uint32_t backend;
    uint32_t kind;
    uint32_t flags;
    uint64_t device_id;
    uint32_t vendor_id;
    uint32_t device_number;
    uint32_t api_version;
    uint32_t driver_version;
    char name[256];
    char vendor[64];
    char driver_info[256];
} hcsDeviceInfo;

#define HCS_DEVICE_FLAG_INTEGRATED 1u

typedef struct hcsDeviceCaps {
    uint32_t struct_size;
    uint32_t capabilities;
    uint32_t compute_queue_count;
    uint32_t max_workgroup_invocations;
    uint32_t max_workgroup_size[3];
    uint32_t subgroup_size;
    uint32_t reserved0;
    uint64_t max_storage_buffer_range;
    uint64_t max_memory_allocation_count;
    uint64_t shared_memory_bytes;
    uint64_t total_memory_bytes;
} hcsDeviceCaps;

typedef struct hcsMemoryBudget {
    uint32_t struct_size;
    uint32_t flags; /* bit 0: integrated/unified memory device */
    uint64_t heap_bytes;
    uint64_t budget_bytes;
    uint64_t usage_bytes;
    uint64_t available_bytes;
    uint64_t reserve_bytes;
} hcsMemoryBudget;

#define HCS_MEMORY_FLAG_INTEGRATED 1u

typedef struct hcsInitDesc {
    uint32_t struct_size;
    uint32_t flags;
    const char* app_name;
    uint32_t app_version;
    uint32_t reserved;
} hcsInitDesc;

typedef struct hcsContextDesc {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t memory_cap_bytes; /* 0 = backend default (conservative) */
} hcsContextDesc;

typedef enum hcsBufferUsage {
    HCS_BUFFER_USAGE_STORAGE = 1u << 0,
    HCS_BUFFER_USAGE_HOST_VISIBLE = 1u << 1,
    HCS_BUFFER_USAGE_TRANSFER_SRC = 1u << 2,
    HCS_BUFFER_USAGE_TRANSFER_DST = 1u << 3
} hcsBufferUsage;

typedef struct hcsBufferDesc {
    uint32_t struct_size;
    uint32_t usage;
    uint64_t size;
    uint64_t reserved;
} hcsBufferDesc;

typedef struct hcsFlowDesc {
    uint32_t struct_size;
    uint32_t flags;
} hcsFlowDesc;

/*
 * Dispatch description.
 *
 * buffers are bound in declaration order to consecutive storage-buffer
 * bindings starting at 0. push_constants are copied verbatim; each built-in
 * kernel documents its layout (see docs/api/kernels.md).
 */
typedef struct hcsDispatchDesc {
    uint32_t struct_size;
    uint32_t buffer_count;
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
    uint32_t push_constants_size;
    uint32_t reserved;
    uint64_t reserved2;
    struct hcs_buffer_t* buffers[8];
    uint8_t push_constants[128];
} hcsDispatchDesc;

#ifdef __cplusplus
}
#endif
