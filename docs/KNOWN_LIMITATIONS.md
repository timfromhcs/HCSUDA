# Known limitations (0.1.0 release candidate)

This list is part of the release contract. Nothing here is a footnote to hide behind; it is
the current truth of the code in this repository. It is kept consistent with
`docs/AGENT_STATUS.md` and `docs/TEST_MATRIX.md`.

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
* **No arbitrary shader compilation at runtime.** The built-in kernels are compiled from
  `shaders/hksl/` at build time. The experimental `hcsProgramCreateSpirv` API only accepts
  SPIR-V produced by the build-time HCSCC path, with no entry-point reflection.
* **Only a narrow HCSIR subset executes on the GPU.** The verified vecadd-style module runs
  both in the CPU interpreter and as HCSCC-generated Vulkan SPIR-V (test IR-18). The program
  API is an explicitly experimental extension, loads only modules produced by the build-time
  HCSCC path, does not reflect entry points, and returns `HCS_E_UNSUPPORTED_FEATURE` on CPU.
* **Persistent cache covers Vulkan pipelines, not HCSIR modules.** A bounded, device-fingerprinted
  on-disk `VkPipelineCache` is used for compute pipelines (VK-06); corrupt/stale/oversized files
  are ignored. HCSIR→GLSL lowering still happens at build time, and HCSIR binary serialization
  (IR-21..IR-23) is not used as a runtime cache.
* **HCSIR covers an elementwise subset only.** No integer/bitwise opcodes, no atomics, no
  subgroup or matrix operations, no structured loops. Binary serialization is implemented and
  tested; it does not make the IR a general-purpose compiler.
* **Global scan is a four-stage pipeline, not a single kernel.** `scan_f32` (local prefixes) →
  `reduce_sum_f32` (chunk totals) → `scan_f32` (offsets) → `scan_add_offsets_f32` (add offsets),
  all on the selected backend (KC-09/KC-10). No host-side pass over chunk totals is required, but
  there is still no one-call global scan primitive.
* **Histogram is a 256-bin single-workgroup kernel** (`histogram_u32`, KC-11/KC-12). Image
  transforms remain explicitly out of V1 scope, not hidden behind a menu item.
* **Reduce remains two-stage.** `reduce_sum_f32` writes one partial sum per workgroup and
  `reduce_final_f32` reduces those partials to `out[0]` in one workgroup (KC-07/KC-08). There is
  still no single-kernel full reduction over the original input; both stages are required.
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

* `hcsport --scan` reports constructs; `hcsport --convert` translates exactly one documented
  vecadd kernel shape and fails closed otherwise (PORT-01..03, TR-01..03). It is not a CUDA
  compiler; PTX text, CUBIN/SASS, multi-kernel files and CUDA library APIs remain unsupported.
* CUBIN/SASS execution on AMD hardware is explicitly unsupported and will remain so.
* `docs/compatibility/cuda-api-matrix.md` marks everything except the tested vecadd subset as unsupported.

## Platform and validation

* **Only AMD Radeon 680M has been tested.** Intel Arc/Iris and NVIDIA Vulkan devices are
  unverified. Cross-vendor support is capability-driven but not yet evidenced.
* Vulkan validation layers run in the dedicated `windows-validation` preset with zero errors;
  hosted runners usually lack the SDK, so real GPU validation remains a self-hosted concern.
* No Intel/NVIDIA-specific ICD probing or software-adapter filtering policy beyond
  compute-queue presence and scoring.
* Windows-only. No Linux or macOS backend in this build.

## Product surface

* The UI is a self-contained local browser page served by REST; there is no Qt/WebView2 shell
  and no automated screenshot evidence (ADR-0003).
* REST, MCP, model catalog and a single-file installer exist but cover only the V1 scope listed
  in `docs/release/v1-scope.md`; there is no inference adapter.
* A CycloneDX SBOM exists; the installer is unsigned because no certificate is configured.
* No telemetry, and none is planned by default.

## Security posture

* REST binds only to 127.0.0.1 with a random bearer token, bounded bodies and Origin checks;
  MCP stdio exposes four read-only tools and no shell/file/download capability.
* HCSIR and CUDA-source parsers are bounded and fuzz-tested. A PTX parser does not exist in V1.

## What would block a final V1 release today

Only external items remain: clean-VM install testing, code signing (no certificate), a manually
approved GitHub Release, and cross-vendor hardware verification. Same-version and cross-version
upgrade are already verified on the reference machine (INS-03/INS-04). See `docs/AGENT_STATUS.md`.

## Model store (updated Wave 8)

* The catalog indexes and hashes GGUF/ONNX/Safetensors/PyTorch files but **does not run any of
  them**. `runnable` is always false until a verified adapter exists.
* GGUF version is checked (2/3), but tensor metadata is not parsed yet; there is no memory
  estimate and no layer plan.
* No resume-capable downloader, no SSD cache tier, and no SSD/HDD performance policy beyond
  the documented warning that HDD is storage, not execution memory.

## API surface (updated Wave 9)

* MCP supports stdio only. There is no Streamable HTTP transport and no remote binding. The five
  read-only tools include `hcsuda_list_models`, which returns catalog metadata only (id, name,
  format, size, hash flag, `runnable:false`) and no filesystem paths or file contents.
* The REST API is local-only and token-authenticated. An OpenAPI 3.1 contract exists at
  `docs/api/openapi.json`; settings endpoints are implemented and UI-tested (SET-05). A
  process-wide fixed-window limit (120 requests / 10 s, HTTP 429) applies to all endpoints except
  health (REST-06). `GET /api/v1/models` exposes safe catalog metadata only (REST-07); benchmark
  and translation endpoints remain out of V1 scope.
* MCP tool arguments do not open model files or perform downloads; `hcsuda_list_models` reads only
  the already-indexed catalog and exposes no paths (MCP-07).

## UI (updated Wave 10)

* The V1 UI is a self-contained page served by the local REST service. There is no Qt or
  WebView2 native shell in this build (see ADR-0003).
* It currently has Dashboard, Devices, Memory budget, Settings and Compatibility sections.
  Models, Translate, Benchmarks, Visualizer, API/MCP, Diagnostics and About pages are not
  present; they are not advertised as working features.
* A real console screenshot exists (`docs/screenshots/ui-dashboard.png`, UI-04) and an automated
  CTest capture/regression check exists (`hcs_ui_capture`, UI-05). On machines without Chrome or
  Edge that test reports SKIP rather than failing.

## Installer (updated Wave 11)

* The single EXE is **unsigned** (no publisher certificate configured). The published SHA-256 is
  the verification mechanism.
* No clean-VM install or code-signing test has been performed. Same-version in-place upgrade
  and cross-version upgrade with two real EXEs passed on the reference machine (INS-03/INS-04).
* The UI, REST and MCP tools are installed as binaries but are not started automatically; there
  is no Windows service and no "start with Windows" default.
