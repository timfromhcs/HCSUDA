# HCSFlow and HEvent — implemented semantics (Wave 5)

This documents the behaviour of the code that exists today, not a roadmap.

## hcsFlow

* `hcsFlowCreate(context, desc, &flow)` creates a backend flow. Descriptors may be null; a
  non-null descriptor must have `struct_size == sizeof(hcsFlowDesc)`.
* `hcsDispatch(kernel, desc, flow)`:
  * with `flow == NULL`: submit and wait on the fence immediately (the original V1 behaviour);
  * with a flow: allocate a command buffer, descriptor set and fence per dispatch, submit to
    the compute queue, hand ownership of the fence to the flow, and return without waiting.
* `hcsFlowSynchronize(flow, timeout_ns)` waits for all fences owned by the flow. `UINT64_MAX`
  means wait indefinitely. On timeout it returns `HCS_E_TIMEOUT` and the work stays pending.
  On success the flow's submission counter is reset for the next batch.
* `hcsFlowDestroy(flow)` waits for any remaining in-flight work and releases it.

Buffers are host-visible and coherent in this build. **Do not download a buffer that is still
in flight on a flow.** Synchronize the flow or wait on an event recorded after the dispatch.

Descriptor sets are never reset while a submission may still read them. If the pool is exhausted,
the runtime waits for device idle, resets the pool once and clears the flows' pending bookkeeping.

## hcsEvent

* `hcsEventCreate(context, &event)` creates an unrecorded marker.
* `hcsEventRecord(event, flow)` captures the flow's submission counter at that point. An event
  recorded on an idle flow is immediately satisfied.
* `hcsEventWait(event, timeout_ns)` waits until the flow's monotone completion counter reaches
  the captured value. The Vulkan counter is derived from `vkGetFenceStatus` on pending fences
  in submission order; no explicit `hcsFlowSynchronize` is required. The CPU backend's counter
  advances as soon as each synchronous dispatch returns.
* `hcsEventDestroy(event)` releases the marker. Events do not own GPU objects.

HEvent is deliberately implemented as a **flow-order marker with fence fallback**, not as a
timeline semaphore. `HCS_CAP_TIMELINE_SEMAPHORE` exists in the capability vocabulary but is not
used by this implementation.

## Guarantees and non-guarantees

Guaranteed:

* submissions on the same flow execute in submission order (single compute queue);
* different flows may be synchronized in any order;
* completion is monotone: an event that was satisfied stays satisfied;
* misuse (null flow, event from another context, waiting on an unrecorded event) returns
  `HCS_E_INVALID_ARGUMENT` with structured diagnostics.

Not guaranteed:

* parallel execution across multiple queues (there is one compute queue per context);
* timeline-semaphore semantics, device-wide event sharing, or host event signaling;
* Parallel HCSGraph DAG scheduling (V1 supports sequential replay only);
* overlap between a flow and a synchronous (`flow == NULL`) dispatch issued concurrently on the
  same context.

## Evidence

`tests/unit/test_flows.cpp`: eight async dispatches plus event wait, two independent flows
synchronized in reverse order, idle-flow and misuse handling, CPU flow completion, and a mock
backend flow that forces `HCS_E_TIMEOUT`. All pass on the reference Radeon 680M and CPU backend.
