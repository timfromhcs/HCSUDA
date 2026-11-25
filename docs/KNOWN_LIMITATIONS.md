# Known limitations (0.1.0 development build)

This list is part of the release contract. Nothing here is a footnote to hide behind; it is
the current truth of the code in this repository.

## Runtime and compute

* **Async dispatch requires a flow.** `hcsDispatch(kernel, desc, NULL)` still submits and
  waits synchronously. Passing an `hcsFlow` submits without waiting; the caller must call
  `hcsFlowSynchronize` (or wait on a recorded `hcsEvent`) before reading host-visible output.
* **HEvent is a flow-order marker**, not a timeline semaphore. Waiting polls fence status in
  submission order. `HCS_CAP_TIMELINE_SEMAPHORE` is not used yet.
* **HCSGraph is a linear-chain subset.** Nodes must be added in topological order with
  dependencies pointing to earlier nodes; replay is sequential on one flow. No parallel DAG
  scheduling, no automatic dependency inference, no buffer lifetime tracking.
* **Buffers are host-visible and coherent.** This is the native path on integrated/UMA GPUs
  (the reference 680M) and a documented slower fallback on discrete GPUs. Device-local
  allocation with staging is planned.
* **One descriptor set layout with 8 storage-buffer bindings.** No images, samplers,
  specialization constants, or push-descriptor support yet.
* **No shader compilation at runtime.** Only the four built-in SPDC/SPIR-V kernels compiled
  from `shaders/hksl/` at build time are available. `hcsProgramCreateSpirv` is not exposed.
* **Only a narrow HCSIR subset executes on the GPU.** The verified vecadd-style module runs
  both in the CPU interpreter and as HCSCC-generated Vulkan SPIR-V (test IR-18). The program
  API is an explicitly experimental extension, loads only modules produced by the build-time
  HCSCC path, does not reflect entry points, and returns `HCS_E_UNSUPPORTED_FEATURE` on CPU.
* **No HCSIR serialization/caching.** Modules are recompiled by `hcscc` at build time; there
  is no stable binary module format yet.
* **HCSIR covers an elementwise subset only.** No integer/bitwise opcodes, no atomics, no
  subgroup or matrix operations, no structured loops, no module serialization.
* **Scan is chunk-local.** `scan_f32` writes per-chunk exclusive prefixes; a global scan needs
  a host-side pass over the chunk totals. There is no global scan kernel yet.
* **No histogram or image-transform kernels.** They are explicitly out of V1 scope, not hidden behind a menu item.
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

* `hcsport` translates exactly one documented vecadd kernel shape and fails closed
  otherwise. It is not a CUDA compiler; PTX text, CUBIN/SASS, multi-kernel files and CUDA
  library APIs remain unsupported.
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

## Model store (updated Wave 8)

* The catalog indexes and hashes GGUF/ONNX/Safetensors/PyTorch files but **does not run any of
  them**. `runnable` is always false until a verified adapter exists.
* GGUF version is checked (2/3), but tensor metadata is not parsed yet; there is no memory
  estimate and no layer plan.
* No resume-capable downloader, no SSD cache tier, and no SSD/HDD performance policy beyond
  the documented warning that HDD is storage, not execution memory.

## API surface (updated Wave 9)

* MCP supports stdio only. There is no Streamable HTTP transport and no remote binding.
* The REST API is local-only and token-authenticated with five read-only endpoints. There is
  no OpenAPI document, no rate limiting beyond bounded bodies, and no model/benchmark/
  translation endpoint yet.
* MCP tool arguments do not currently open model files; `hcsuda_list_models` is intentionally
  absent until path scopes are configured in a service layer.

## UI (updated Wave 10)

* The V1 UI is a self-contained page served by the local REST service. There is no Qt or
  WebView2 native shell in this build (see ADR-0003).
* It currently has Dashboard, Devices, Memory budget and Compatibility sections. Settings,
  Models, Translate, Benchmarks, Visualizer, API/MCP, Diagnostics and About pages are not
  present; they are not advertised as working features.
* No automated browser screenshot exists, so the README contains no product screenshot.
