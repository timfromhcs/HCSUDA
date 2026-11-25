# ADR-0002: Vulkan backend design for V1

* Status: accepted
* Date: 2026-11-25

## Context

The runtime must execute real compute on cross-vendor GPUs without shipping a driver or
assuming capabilities. The first target is an AMD Radeon 680M (RDNA 2, UMA), but the design
must not encode AMD-specific assumptions.

## Decision

1. **Dynamic loader.** `vulkan-1.dll` is loaded via `LoadLibraryW` and entry points via
   `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`. HCSUDA never ships or replaces the loader
   and never links `vulkan-1.lib`.
2. **Query everything.** Device properties, limits, subgroup properties, FP16/INT8 features
   (`VkPhysicalDeviceVulkan12Features` when instance API >= 1.2) and memory budget
   (`VK_EXT_memory_budget`) are queried. A missing feature clears the capability bit; nothing
   is inferred from the vendor string or architecture name.
3. **Host-visible coherent buffers in V1.** On UMA devices this is the native path and avoids
   a staging copy. On discrete GPUs it is correct but slower; this is documented rather than
   hidden. Device-local + staging is a later optimization gated on measurements.
4. **Synchronous submission.** Each dispatch records a one-time command buffer, submits to a
   single compute queue and waits on a fence. This removes an entire class of races from V1
   and makes benchmark semantics unambiguous.
5. **Built-in SPIR-V only.** Shaders live in `shaders/hksl/*.comp`, are compiled by `glslc`
   during the build and embedded. Runtime shader compilation is not exposed.
6. **Explicit memory cap.** Contexts default to 75% of the reported budget, clamped to 6 GiB.
   Allocation checks happen before `vkAllocateMemory`, and failures return
   `HCS_E_OUT_OF_MEMORY` with context.
7. **Device-loss mapping.** `VK_ERROR_DEVICE_LOST` maps to `HCS_E_DEVICE_LOST`; the context
   becomes unusable and callers must recreate it. No stale Vulkan object is reused.

## Consequences

* Adding async execution (Wave 5) requires per-context command pools, multiple in-flight
  frames, and timeline semaphores where available; the current single command buffer will be
  replaced, not patched in place.
* Descriptor sets are reset per dispatch. This is simple and safe; descriptor buffers or
  persistent sets are a measured optimization later.
* The backend currently exposes 8 storage-buffer bindings, matching the descriptor layout.

## Validation strategy

* Every kernel is compared against the CPU reference over random and odd-sized inputs.
* A 4 MiB-capped context must reject a 16 MiB allocation (test VK-05).
* Validation layers are a developer dependency and will be wired into a dedicated CTest
  profile in Wave 5, not enabled in release builds.
