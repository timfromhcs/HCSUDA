#pragma once

/*
 * HCSUDA stable C ABI (HCS_API_VERSION 1).
 *
 * All handles are opaque. Structs carry struct_size and are versioned.
 * No Vulkan handle is ever exposed through this header.
 */

#include "hcsuda_errors.h"
#include "hcsuda_types.h"
#include "hcsuda_version.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HCS_API
#define HCS_API
#endif

typedef struct hcs_device_t hcs_device_t;
typedef struct hcs_context_t hcs_context_t;
typedef struct hcs_buffer_t hcs_buffer_t;
typedef struct hcs_flow_t hcs_flow_t;
typedef struct hcs_kernel_t hcs_kernel_t;
typedef struct hcs_event_t hcs_event_t;

typedef hcs_device_t* hcsDevice;
typedef hcs_context_t* hcsContext;
typedef hcs_buffer_t* hcsBuffer;
typedef hcs_flow_t* hcsFlow;
typedef hcs_kernel_t* hcsKernel;
typedef hcs_event_t* hcsEvent;

/* Lifetime ---------------------------------------------------------------- */

HCS_API hcsResult hcsInit(const hcsInitDesc* desc);
HCS_API hcsResult hcsShutdown(void);
HCS_API const char* hcsGetVersionString(void);
HCS_API uint32_t hcsGetApiVersion(void);

/* Devices ----------------------------------------------------------------- */

/*
 * Enumerates devices. On entry *count holds the capacity of `out`; on success
 * it holds the number written. Passing NULL for `out` queries the count.
 */
HCS_API hcsResult hcsEnumerateDevices(hcsDeviceInfo* out, uint32_t* count);
HCS_API hcsResult hcsGetDevice(uint32_t index, hcsDevice* out_device);
HCS_API hcsResult hcsGetDeviceCaps(hcsDevice device, hcsDeviceCaps* caps);
HCS_API hcsResult hcsGetMemoryBudget(hcsDevice device, hcsMemoryBudget* out);

/* Contexts ---------------------------------------------------------------- */

HCS_API hcsResult hcsCreateContext(hcsDevice device, const hcsContextDesc* desc,
                                   hcsContext* out_context);
HCS_API void hcsDestroyContext(hcsContext context);

/* Buffers ----------------------------------------------------------------- */

HCS_API hcsResult hcsBufferCreate(hcsContext context, const hcsBufferDesc* desc,
                                  hcsBuffer* out_buffer);
HCS_API hcsResult hcsBufferUpload(hcsBuffer buffer, uint64_t offset, const void* src,
                                  uint64_t size);
HCS_API hcsResult hcsBufferDownload(hcsBuffer buffer, uint64_t offset, void* dst,
                                    uint64_t size);
HCS_API hcsResult hcsBufferClear(hcsBuffer buffer, uint64_t offset, uint64_t size);
HCS_API void hcsBufferDestroy(hcsBuffer buffer);
HCS_API uint64_t hcsBufferSize(hcsBuffer buffer);

/* Flows ------------------------------------------------------------------- */

HCS_API hcsResult hcsFlowCreate(hcsContext context, const hcsFlowDesc* desc,
                                hcsFlow* out_flow);
HCS_API hcsResult hcsFlowSynchronize(hcsFlow flow, uint64_t timeout_ns);
HCS_API void hcsFlowDestroy(hcsFlow flow);

/* Kernels and dispatch ---------------------------------------------------- */

/*
 * Creates a handle to a built-in, verified kernel. The supported set is
 * published in docs/api/kernels.md; unknown names return HCS_E_KERNEL_NOT_FOUND.
 */
HCS_API hcsResult hcsKernelCreate(hcsContext context, const char* name,
                                  hcsKernel* out_kernel);
HCS_API void hcsKernelDestroy(hcsKernel kernel);
HCS_API hcsResult hcsDispatch(hcsKernel kernel, const hcsDispatchDesc* desc,
                              hcsFlow flow);

/* Diagnostics ------------------------------------------------------------- */

HCS_API const char* hcsBackendName(uint32_t backend_kind);
HCS_API const char* hcsDeviceKindName(uint32_t device_kind);

#ifdef __cplusplus
}
#endif
