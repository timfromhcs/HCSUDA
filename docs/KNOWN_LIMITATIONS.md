# Known limitations (0.1.0 development build)

This list is part of the release contract. Nothing here is a footnote to hide behind; it is
the current truth of the code in this repository.

## Runtime and compute

* **V1 dispatches are synchronous.** `hcsDispatch` submits and waits on a fence. No
  asynchronous overlap, no multi-queue scheduling, no timeline semaphores yet (Wave 5).
* **Buffers are host-visible and coherent.** This is the native path on integrated/UMA GPUs
  (the reference 680M) and a documented slower fallback on discrete GPUs. Device-local
  allocation with staging is planned.
* **One descriptor set layout with 8 storage-buffer bindings.** No images, samplers,
  specialization constants, or push-descriptor support yet.
* **No shader compilation at runtime.** Only the four built-in SPDC/SPIR-V kernels compiled
  from `shaders/hksl/` at build time are available. `hcsProgramCreateSpirv` is not exposed.
* **HCSIR v1 runs only on the CPU interpreter.** The parser, verifier and interpreter exist
  and are tested (`ir_*` tests), but there is no HCSIR → GLSL/SPIR-V lowering yet, so an HCSIR
  module cannot execute on a GPU. HCSIR is internal and not part of the C ABI.
* **HCSIR covers an elementwise subset only.** No integer/bitwise opcodes, no atomics, no
  subgroup or matrix operations, no structured loops, no module serialization.
* **Reduce is two-stage by design.** `reduce_sum_f32` writes one partial sum per workgroup;
  the caller performs the final reduction. There is no single-call full reduction kernel yet.
* **Single device per context.** Multi-GPU and peer access are out of scope for V1.
* **No timestamp queries yet**, so `hcsbench` measures CPU wall-clock around submit+wait.
  Numbers therefore include dispatch overhead and are not pure kernel durations.

## Memory

* The default context cap is 75% of the driver-reported budget, additionally clamped to 6 GiB.
  This is deliberately conservative; it may be too small for large future AI models.
* `VK_EXT_memory_budget` heap usage can read 0 on some AMD drivers even while memory is in
  use. HCSUDA reports what the driver returns and does not invent a number.
* There is no allocator pooling yet; every buffer is a separate `vkAllocateMemory`.

## Translation

* No CUDA source translator and no PTX importer are shipped. `hcsport` is Wave 7.
* CUBIN/SASS execution on AMD hardware is explicitly unsupported and will remain so.
* `docs/compatibility/cuda-api-matrix.md` therefore marks nearly everything as unsupported.

## Platform and validation

* **Only AMD Radeon 680M has been tested.** Intel Arc/Iris and NVIDIA Vulkan devices are
  unverified. Cross-vendor support is capability-driven but not yet evidenced.
* No Vulkan validation-layer run is wired into CTest yet.
* No Intel/NVIDIA-specific ICD probing or software-adapter filtering policy beyond
  compute-queue presence and scoring.
* Windows-only. No Linux or macOS backend in this build.

## Product surface

* No desktop UI (Qt absent; WebView2 strategy drafted in ADR-0003).
* No REST API, no MCP server, no model vault, no inference adapter, no installer.
* No code signing, SBOM generation, or release artifacts yet.
* No telemetry, and none is planned by default.

## Security posture

* API/MCP are not implemented yet; when they are, they must be localhost-only, token
  authenticated, request-size limited, and must never expose arbitrary command execution.
* Parsers for HCSIR/PTX/CUDA source do not exist yet; their threat model is pre-written in
  ADR-0004 and SECURITY.md so implementation cannot silently skip it.

## What would block a V1 release today

Every unchecked item in `README.md` (models, UI, API/MCP, installer) plus Wave 12 stress and
security gates. The current build is a correct, tested kernel runtime — not a finished
product.
