# HCSUDA — Autonomous Build Specification

> **Project:** HCSUDA — HCS Unified Compute Runtime  
> **Tagline:** *Made with love by HCSMedia & timfromhcs*  
> **Target:** Windows 11 x64; first reference machine: AMD Ryzen 7 7735HS + Radeon 680M (RDNA 2, integrated UMA GPU), with model files stored on a user-selected HDD.  
> **Primary backend:** Vulkan compute. **Secondary backend:** CPU SIMD/scalar fallback.  
> **Release goal:** a real, tested, one-file Windows installer and a public GitHub repository, with honest compatibility claims.

---

## 0. Your role, authority, and non-negotiable rules

You are the autonomous principal engineer, build engineer, test engineer, UX engineer, documentation writer, and release engineer for HCSUDA. You start in an empty repository directory. Assume you know nothing about the project until you have read this file. This document is the product specification and execution contract.

Your job is to build a working product, not to produce a mockup, a slide deck, a proof-of-concept that only compiles, or a README claiming features that do not exist.

### 0.1 Hard rules

1. **Never claim full CUDA compatibility.** HCSUDA is an independent cross-vendor compute runtime. It can provide a CUDA-like programming model, translate a defined subset of CUDA source and textual PTX, and implement selected CUDA-compatible APIs. It cannot universally execute NVIDIA CUBIN/SASS binaries or every proprietary CUDA application on AMD hardware. Make unsupported cases explicit and actionable.
2. **Do not install or replace a kernel-mode GPU driver.** HCSUDA V1 is a user-mode compute runtime, Vulkan backend, compatibility/migration tool, local service, and desktop application. The existing AMD/Intel/NVIDIA display/compute driver remains the authority for hardware access. Do not create a `.sys` driver, edit the Windows Driver Store, replace `vulkan-1.dll`, install a fake `nvcuda.dll` globally, or change system DLL search paths.
3. **Do not impersonate NVIDIA.** Do not use NVIDIA logos, copy proprietary CUDA headers/binaries, claim NVIDIA endorsement, or label HCSUDA as an official CUDA implementation. CUDA is a NVIDIA trademark. Use CUDA references only for descriptive compatibility/migration explanations and include a clear non-affiliation notice. Before public branding, run a trademark/confusion review. Preserve the requested HCSUDA name provisionally; if legal review identifies an unacceptable conflict, stop publication and propose a safer name rather than silently renaming it.
4. **No false hardware features.** Detect Vulkan capabilities at runtime. Never assume a particular subgroup size, FP16 support, subgroup operation, atomics capability, queue family, memory type, or GPU vendor. Optional features must have a tested fallback or be reported unsupported.
5. **No fake benchmarks or screenshots.** Measurements must come from a real build on identified hardware, with command, configuration, warm-up, repetitions, median and dispersion recorded. Simulated data must be labeled “demo/synthetic”.
6. **No machine damage.** Do not change BIOS, firmware, power limits, clocks, voltages, GPU driver installation, Windows security settings, antivirus exclusions, or system pagefile configuration. Do not deliberately exhaust UMA memory. Stress tests must use bounded allocations and cancellation.
7. **No hidden network activity.** HCSUDA is local-first. Model downloads, update checks, telemetry, and remote API binding are opt-in. No model download begins without user action. Default API/MCP network scope is local-only.
8. **No secrets in Git.** Never commit access tokens, API keys, user paths containing personal identifiers, model files, private logs, crash dumps, or credentials. Use GitHub Actions secrets only when the user/repository owner has explicitly configured them.
9. **Respect licenses.** Maintain a third-party notices file and SBOM. Do not bundle a dependency unless its license and redistribution terms permit it. Pin source dependencies and record exact versions/commits.
10. **Build, test, fix, repeat.** Continue in small vertical slices until the defined V1 gates pass or a concrete blocker is documented. Never mark a gate green because code “looks right”.
11. **No scope theater.** A menu item that does nothing is not a feature. Hide incomplete features behind an explicit “Experimental” label or remove them from the release UI.
12. **Keep project state durable.** Maintain `docs/AGENT_STATUS.md`, `docs/BUILD_LOG.md`, `docs/TEST_MATRIX.md`, and `docs/KNOWN_LIMITATIONS.md`. Update them after each wave.

### 0.2 How to handle uncertainty

- Verify current APIs against their official documentation before coding. Do not invent Vulkan, Windows, Qt, MCP, compiler, or model-runtime symbols.
- Prefer capabilities and version checks over assumptions.
- If an upstream dependency changes, pin a stable compatible version and document why. Do not blindly upgrade during a release candidate.
- If a feature cannot be completed safely or correctly, reduce the declared feature scope, add a clear diagnostic, and keep the rest of the product working.
- Never silently swallow errors. Every failure must have a structured error code, user-readable message, and diagnostic context without secrets.

---

## 1. Product definition and truthful scope

### 1.1 What HCSUDA is

HCSUDA is a **portable GPU compute runtime and developer platform**. It provides:

- A vendor-neutral device discovery layer built around Vulkan compute.
- A versioned HCSUDA host API and C ABI.
- A portable HCSUDA intermediate representation (HCSIR) and virtual instruction vocabulary (HCS-PIR).
- A compute shader compiler/lowering path that targets SPIR-V for Vulkan.
- A CPU backend for portability, validation, and fallback.
- Streams/queues, events, graph execution, buffers, memory pools, synchronization, and diagnostics, with explicitly documented V1 support levels.
- A CUDA-source migration tool for a tested subset of CUDA C++.
- An experimental textual PTX importer for a small verified opcode subset; not a generic NVIDIA binary translator.
- AI model discovery and local model storage, with HDD-aware staging and memory budgeting.
- A desktop settings/control panel, performance visualizer, benchmark runner, REST API, and MCP server.
- A single-file Windows installer that installs only HCSUDA user-mode files and never replaces the graphics driver.

### 1.2 What HCSUDA is not

HCSUDA V1 is **not**:

- A replacement AMD/Intel/NVIDIA Windows display driver.
- A universal CUDA emulator.
- A way to execute NVIDIA `sm_XX` CUBIN/SASS instructions on AMD hardware.
- A binary translator for arbitrary CUDA `.exe`, `.dll`, `.fatbin`, or `.cubin` files.
- A drop-in replacement for `nvcuda.dll`, cuDNN, cuBLAS, TensorRT, NCCL, or all CUDA Driver/Runtime APIs.
- A promise that every Vulkan GPU can execute every shader precision or AI model.
- A promise that an HDD is equivalent to RAM or GPU memory.
- A claim that GPU execution is always faster than CPU execution.

The product must state this in its UI, README, compatibility page, installer notes, and migration reports.

### 1.3 Compatibility vocabulary

Use the following statuses consistently:

| Status | Meaning |
|---|---|
| **Native / Stable** | HCSUDA-owned API or function with unit + integration coverage and a documented guarantee for the relevant feature tier. |
| **Supported via Vulkan** | Function mapped to Vulkan core functionality on devices that expose the required base features. |
| **Optional capability** | Only available when detected Vulkan feature/extension exists; fallback or explicit “not available” state required. |
| **Translated / Supported subset** | CUDA source/PTX construct translated and validated within the documented subset. |
| **Experimental** | Implemented but limited; visible warning, diagnostics, no V1 compatibility guarantee. |
| **Unsupported** | Not implemented or impossible on the target hardware; report exact reason. |

Never use “100% CUDA”, “full CUDA support”, “all CUDA programs run”, “works on every Vulkan card”, or “faster than CUDA” in user-facing copy. The accurate phrase is: **“HCSUDA offers a Vulkan-based, cross-vendor compute runtime with an expanding CUDA-source migration and compatibility layer.”**

---

## 2. Reference hardware and operating assumptions

### 2.1 First target machine

Treat the following as the primary performance and QA profile, not as assumptions for all users:

- CPU: AMD Ryzen 7 7735HS, Zen 3+, 8 cores / 16 threads.
- GPU: AMD Radeon 680M integrated graphics, RDNA 2, 12 CUs / 768 shaders, up to 2.2 GHz listed graphics frequency.
- Memory: system DDR5 or LPDDR5 depending on laptop configuration; the processor specification lists dual-channel memory and platform-dependent speeds/capacity.
- OS: Windows 11 x64.
- Storage: HDD available as a model-library location.

The AMD specification page currently identifies the Radeon 680M and lists 12 graphics cores, 768 shaders, 2.2 GHz graphics frequency, WDDM 2.7, and a 2 GB GPU-memory figure. Do not interpret that single marketing/specification field as a universal runtime allocation limit or as the total amount of UMA memory available. At runtime, use Vulkan and Windows-reported budgets. The real usable memory budget varies with firmware, driver, Windows, system RAM, other processes, and current load.

Official reference: <https://www.amd.com/en/products/processors/laptop/ryzen/7000-series/amd-ryzen-7-7735hs.html>

### 2.2 UMA memory model

The Radeon 680M is integrated. On an UMA device, host and device access may refer to shared physical system memory. Vulkan distinguishes device-local and host-visible memory properties; on an integrated GPU these may overlap. This does **not** mean all installed system RAM is safely available to HCSUDA.

Implementation requirements:

- Query `VkPhysicalDeviceMemoryProperties` and, when available, `VK_EXT_memory_budget`.
- Report total heap size, estimated heap budget, current heap usage, allocation limits, and whether the device is integrated/discrete. Do not invent a “VRAM size”.
- Re-query budget before large allocations and periodically during sustained model work.
- Keep a safety reserve for Windows, display compositing, other applications, and the driver.
- Allow the user to set a conservative HCSUDA memory cap. Default to a conservative fraction of the currently reported budget; make the cap visible and editable.
- Never allocate the whole reported heap during startup.
- Separate `model disk bytes`, `model mapped bytes`, `host resident bytes`, `GPU allocation bytes`, `shader cache bytes`, and `temporary staging bytes` in UI and logs.
- Use explicit memory types and synchronization. Host-coherent memory does not remove the need to understand Vulkan memory visibility and execution dependencies.
- Prefer large reusable allocations/arenas over many tiny Vulkan allocations. Use a tested allocator (e.g. Vulkan Memory Allocator, VMA) if its license/version is approved.
- Support memory-pressure handling: stop admitting new allocations, release eligible caches, reduce batch size / model offload, and return a clear out-of-memory error. Never crash or force system-wide memory exhaustion.

Reference: <https://docs.vulkan.org/guide/latest/memory_allocation.html> and <https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html>.

### 2.3 HDD model storage policy

HDD is persistent model storage, **not execution memory**. The application must not promise that a model can be executed directly from the HDD at GPU speed.

Build a model manager with these stages:

1. Model file stored at the user-selected HDD location.
2. Validate path, file size, extension, manifest, and SHA-256 when available.
3. Use memory mapping or bounded asynchronous file reads where appropriate.
4. Prefetch the next model chunk/layer into host memory.
5. Stage the active data into Vulkan-accessible memory according to device memory properties and budget.
6. Release staging buffers and caches when no longer required.
7. Support cancellation, resume for downloads, file-lock errors, missing drives, and corrupted files.

A fast SSD cache may be offered as an optional performance feature, but it is never required for basic operation. The user can configure separate paths for model library, temporary staging cache, shader cache, logs, and application settings. Never write large caches into the Git repository.

For LLM support, use GGUF through a maintained backend adapter (initially `llama.cpp` with its Vulkan backend) rather than pretending to implement all transformer operators from scratch in V1. Run a real Vulkan build and validate it on the Radeon 680M. The upstream project documents Windows Vulkan builds: <https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md>.

---

## 3. Architecture at a glance

### 3.1 Required system layers

```text
┌──────────────────────────────────────────────────────────────┐
│ HCSUDA Desktop UI (Qt Quick/QML)                              │
│ Dashboard · Devices · Models · Translate · Benchmarks          │
│ Visualizer · API/MCP · Settings · Diagnostics · About          │
└──────────────────────────────┬───────────────────────────────┘
                               │ local IPC / versioned C API
┌──────────────────────────────▼───────────────────────────────┐
│ HCSUDA Service / Application Host                             │
│ Configuration · Job Scheduler · Model Catalog · REST · MCP   │
│ Auth/Permissions · Logging · Crash-safe shutdown              │
└──────────────────────────────┬───────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────┐
│ HCSUDA Core                                                   │
│ Device registry · HCS API · HCSIR · HCS-PIR · Memory manager  │
│ Streams · Events · Graphs · Compiler front-end · Validation   │
│ CPU backend · Vulkan backend · profiling hooks                │
└───────────────┬────────────────────────────────┬─────────────┘
                │                                │
┌───────────────▼───────────────┐   ┌────────────▼────────────┐
│ Vulkan backend                │   │ CPU backend             │
│ SPIR-V pipeline / dispatch    │   │ scalar + AVX2/FMA       │
│ Vulkan memory / queues        │   │ runtime CPUID dispatch  │
│ synchronization / barriers   │   │ thread pool             │
└───────────────┬───────────────┘   └────────────┬────────────┘
                │                                │
        Vulkan loader + ICD              Windows CPU / OS
                │
        Vendor Vulkan driver
                │
     AMD / Intel / NVIDIA / other
          Vulkan compute GPU
```

### 3.2 Critical Windows distinction

“Install as a driver” in the original product idea must be implemented as **installing a user-mode compute platform**, not as replacing a kernel driver. Vulkan applications use the system Vulkan loader and an installed vendor ICD. HCSUDA talks to Vulkan through that supported interface. The AMD graphics driver remains installed and controls Radeon hardware.

The one-file EXE is the HCSUDA application installer. It is not a GPU driver package.

### 3.3 Recommended implementation stack

Use a conservative, maintainable stack. Verify exact current versions and licenses during the first setup wave; pin them in manifests/lockfiles.

- Language: C++20 for core, runtime, Vulkan backend, and stable C ABI.
- Build: CMake + Ninja; MSVC x64 for official Windows release.
- GPU API: Vulkan C API (or carefully scoped Vulkan-Hpp); Vulkan 1.0 compute baseline where feasible, optional higher versions/extensions discovered at runtime.
- Shader path: GLSL/HCSL → shaderc/glslang → SPIR-V; validate/optimize with SPIRV-Tools. Package only runtime components legally distributable; build SDK tools are not assumed installed on the end-user system.
- CPU backend: portable scalar baseline plus AVX2/FMA implementation selected after CPUID checks. Do not compile the entire executable with `-march=native`.
- GUI: Qt 6 Quick/QML for modern, responsive desktop UI. Bundle required redistributable Qt libraries/plugins and provide required license notices. If the chosen Qt license/deployment model becomes problematic, stop and document an alternative before replacing the UI framework.
- Settings/database: SQLite with schema migrations; JSON for exportable configuration.
- HTTP: a maintained small HTTP server library or Boost.Beast. Pin and license-check it.
- JSON: a maintained JSON library with strict parsing and size/depth limits.
- MCP: implement the required JSON-RPC 2.0 messages against the current official MCP specification, or use a maintained SDK with a compatible license. Keep stdio transport clean; logs go to stderr, never stdout.
- Packaging: Inno Setup (or an equally maintainable, redistributable packager) to produce one self-contained `HCSUDA-Setup-x64.exe`.
- Tests: CTest + unit-test framework + PowerShell installer tests + Vulkan validation layers in Debug/CI where available.
- Inference adapter: pinned `llama.cpp` Vulkan backend for GGUF LLM inference; optional ONNX/Windows ML adapter only after verified compatibility. Do not make DirectML the primary architecture; it is a separate Windows API/backend and does not implement HCSUDA’s Vulkan compute runtime.

### 3.4 Repository structure

Create and maintain this structure (small, justified changes are allowed; keep module boundaries):

```text
HCSUDA/
├── AGENTS.md
├── README.md
├── LICENSE
├── NOTICE
├── THIRD_PARTY_NOTICES.md
├── SECURITY.md
├── CONTRIBUTING.md
├── CODE_OF_CONDUCT.md
├── CITATION.cff
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── deps/                      # lockfiles/manifest only; no vendored unlicensed blobs
├── include/hcsuda/
│   ├── hcsuda.h               # stable C ABI
│   ├── hcsuda.hpp             # C++ API
│   ├── hcsuda_types.h
│   ├── hcsuda_errors.h
│   └── hcsuda_version.h
├── src/
│   ├── core/
│   ├── device/
│   ├── memory/
│   ├── execution/
│   ├── compiler/
│   ├── backends/vulkan/
│   ├── backends/cpu/
│   ├── translation/cuda_source/
│   ├── translation/ptx_experimental/
│   ├── model_store/
│   ├── inference/llama_adapter/
│   ├── api/rest/
│   ├── api/mcp/
│   ├── service/
│   └── app/
├── shaders/
│   ├── hksl/
│   └── generated_spirv/       # generated build output is not source of truth
├── ui/
│   ├── qml/
│   ├── assets/
│   └── themes/
├── tools/
│   ├── hcscc/
│   ├── hcsport/
│   ├── hcsbench/
│   └── hcsdoctor/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── vulkan/
│   ├── cpu/
│   ├── translation/
│   ├── mcp/
│   ├── api/
│   ├── ui/
│   ├── installer/
│   └── stress/
├── benchmarks/
│   ├── kernels/
│   ├── models/
│   └── baselines/
├── docs/
│   ├── architecture/
│   ├── api/
│   ├── compatibility/
│   ├── design/
│   ├── release/
│   ├── AGENT_STATUS.md
│   ├── BUILD_LOG.md
│   ├── TEST_MATRIX.md
│   └── KNOWN_LIMITATIONS.md
├── installer/
├── scripts/
├── .github/
│   ├── workflows/
│   ├── ISSUE_TEMPLATE/
│   └── PULL_REQUEST_TEMPLATE.md
└── artifacts/                 # ignored; release output only
```

---

## 4. HCSUDA naming and programming model

HCSUDA must have its own consistent names, namespaces, symbols, diagnostics, and parallel vocabulary. Do not copy NVIDIA names as if they were HCSUDA-owned features. Compatibility aliases may exist only in an explicitly named migration header/namespace.

### 4.1 Brand and terminology

- Product: **HCSUDA**.
- Full product description: **HCS Unified Compute Runtime**.
- Core runtime: **HCS-Core**.
- Portable intermediate representation: **HCSIR**.
- Portable virtual instruction vocabulary: **HCS-PIR**.
- High-level kernel language: **HKL (HCS Kernel Language)**.
- Compiler: **HCSCC**.
- CUDA source migration utility: **HCSPORT**.
- Device utility: **HCSDOCTOR**.
- Benchmark utility: **HCSBENCH**.
- Stream/queue abstraction: **HCSFlow**.
- Graph execution abstraction: **HCSGraph**.
- Device memory allocator/pools: **HCSMemory**.
- Model catalog: **HCSModelVault**.
- Local REST service: **HCSBridge API**.
- MCP server: **HCSBridge MCP**.

Use `hcs` as the C/C++ namespace and `hcs*` as the C API prefix.

### 4.2 HCS parallel vocabulary

| HCSUDA term | Meaning | Vulkan mapping |
|---|---|---|
| HDevice | Physical compute device | `VkPhysicalDevice` + logical device |
| HContext | Runtime resource context | Internal context/device state |
| HKernel | Compute kernel/program | Compute pipeline entry point |
| HGrid | Total dispatch grid | `vkCmdDispatch` group counts |
| HWorkgroup | Cooperative workgroup | Vulkan local workgroup |
| HLane | One shader invocation/lane | SPIR-V invocation |
| HFlow | Ordered command stream | Vulkan queue + command submission ordering |
| HEvent | Timeline/fence marker | Vulkan fence/semaphore/timeline semaphore if available |
| HGraph | Reusable execution DAG | Recorded command buffers / internal graph scheduler |
| HTile | Logical data tile | Workgroup-tiled algorithm/data region |
| HShared | Workgroup-shared scratch memory | Workgroup storage class / shared memory |
| HGlobal | Device-visible storage | Storage buffer/device memory |
| HBarrier | Synchronization primitive | SPIR-V/Vulkan barriers and synchronization2 when available |
| HAtomic | Atomic operation | SPIR-V atomic operation if supported |
| HMemoryPool | Reusable allocations | Vulkan memory allocator/arena |

**Important:** HCS terms are abstractions. A Vulkan subgroup is not guaranteed to have the same size as a CUDA warp. Never hardcode “32 H-lanes per wave”. Query subgroup properties and provide workgroup/shared-memory fallback algorithms. Vulkan documentation explicitly notes that subgroup sizes can vary by implementation. Reference: <https://docs.vulkan.org/guide/latest/subgroups.html>.

### 4.3 HCS-PIR virtual operations

Define an initial versioned set of virtual operations. These are compiler IR opcodes, **not claims about native hardware instructions**.

- Arithmetic: `HADD.F32`, `HSUB.F32`, `HMUL.F32`, `HDIV.F32`, `HFMA.F32`, `HMIN.F32`, `HMAX.F32`.
- Integer: `HADD.I32`, `HSUB.I32`, `HMUL.I32`, `HAND.I32`, `HOR.I32`, `HXOR.I32`, `HSHL.I32`, `HSHR.I32`.
- Conversion: `HCONVERT`, `HCAST`, `HROUND`, `HSATURATE` where semantics are specified.
- Memory: `HLOAD`, `HSTORE`, `HLOAD.VEC`, `HSTORE.VEC`, `HPREFETCH` (hint only, may lower to no-op).
- Control: `HBRANCH`, `HSELECT`, `HPHI`, `HRETURN`, `HLOOP`.
- Synchronization: `HBARRIER.WORKGROUP`, `HMEMBAR.DEVICE` only when mapped to a valid memory model.
- Atomics: `HATOM.ADD`, `HATOM.MIN`, `HATOM.MAX`, `HATOM.CAS` only when device feature and ordering are supported.
- Collective operations: `HREDUCE.ADD`, `HREDUCE.MIN`, `HPREFIX.SUM`, only with proven subgroup/workgroup implementation.
- Matrix/tile operations: `HTILE.LOAD`, `HTILE.STORE`, `HTILE.MMA` only if the selected backend supports a tested implementation; otherwise lower to ordinary scalar/vector operations or return `HCS_E_UNSUPPORTED_FEATURE`.

Define exact semantics: signedness, overflow, NaN handling, denormals, memory scope, alignment, vector width, rounding, and error behavior. Version the IR (`HCSIR v1`) and write a verifier before adding more opcodes. Never claim FP8/BF16/FP16 hardware execution just because the parser accepts a type; gate each format on real device capability and a correctness test.

### 4.4 C API outline

Create a stable C ABI with opaque handles, explicit struct sizes/version fields, and a consistent error model. Proposed function groups (final spelling must be consistent):

```c
hcsResult hcsInit(const hcsInitDesc* desc);
hcsResult hcsShutdown(void);
hcsResult hcsEnumerateDevices(hcsDeviceInfo* out, uint32_t* count);
hcsResult hcsGetDeviceCaps(hcsDevice device, hcsDeviceCaps* caps);
hcsResult hcsCreateContext(hcsDevice device, const hcsContextDesc*, hcsContext* out);
hcsResult hcsDestroyContext(hcsContext context);
hcsResult hcsBufferCreate(hcsContext, const hcsBufferDesc*, hcsBuffer* out);
hcsResult hcsBufferUpload(hcsBuffer, const void*, uint64_t, hcsFlow);
hcsResult hcsBufferDownload(hcsBuffer, void*, uint64_t, hcsFlow);
hcsResult hcsBufferDestroy(hcsBuffer);
hcsResult hcsProgramCreateSpirv(hcsContext, const void*, size_t, hcsProgram* out);
hcsResult hcsKernelGet(hcsProgram, const char* name, hcsKernel* out);
hcsResult hcsFlowCreate(hcsContext, const hcsFlowDesc*, hcsFlow* out);
hcsResult hcsDispatch(hcsKernel, const hcsDispatchDesc*, hcsFlow);
hcsResult hcsFlowSynchronize(hcsFlow, uint64_t timeout_ns);
hcsResult hcsEventCreate(hcsContext, hcsEvent* out);
hcsResult hcsEventRecord(hcsEvent, hcsFlow);
hcsResult hcsEventWait(hcsEvent, uint64_t timeout_ns);
hcsResult hcsGraphCreate(hcsContext, hcsGraph* out);
hcsResult hcsGraphAddDispatch(hcsGraph, const hcsDispatchDesc*);
hcsResult hcsGraphCompile(hcsGraph);
hcsResult hcsGraphRun(hcsGraph, hcsFlow);
hcsResult hcsGetMemoryBudget(hcsDevice, hcsMemoryBudget* out);
hcsResult hcsGetLastError(hcsErrorInfo* out);
```

This is an API design target, not a claim that every symbol already exists. Implement only symbols that have tests. Use symbol export/version maps, semantic versioning, ABI tests, and a `HCS_API_VERSION` macro. Never expose Vulkan handles directly in the stable public ABI unless behind an explicitly unstable interop API.

### 4.5 Error model

Provide stable error categories including:

- `HCS_SUCCESS`
- `HCS_E_INVALID_ARGUMENT`
- `HCS_E_NOT_INITIALIZED`
- `HCS_E_NO_DEVICE`
- `HCS_E_UNSUPPORTED_API_VERSION`
- `HCS_E_UNSUPPORTED_FEATURE`
- `HCS_E_OUT_OF_MEMORY`
- `HCS_E_DEVICE_LOST`
- `HCS_E_SHADER_COMPILE`
- `HCS_E_SPIRV_INVALID`
- `HCS_E_KERNEL_NOT_FOUND`
- `HCS_E_TIMEOUT`
- `HCS_E_IO`
- `HCS_E_MODEL_CORRUPT`
- `HCS_E_TRANSLATION_UNSUPPORTED`
- `HCS_E_SECURITY_POLICY`
- `HCS_E_INTERNAL`

Every error includes a stable code, short message, optional backend result, operation name, and correlation ID. Do not return raw unbounded driver/compiler dumps to the UI; store bounded logs and provide “copy diagnostic report” with sensitive paths redacted by default.

---

## 5. Vulkan backend requirements

### 5.1 Discovery and selection

At startup:

1. Load the system Vulkan loader using the documented platform loader path. Do not ship a fake loader or replace the system one.
2. Create a Vulkan instance with only required extensions/layers.
3. Enumerate physical devices.
4. Query properties, features, memory properties, queue families, device extensions, subgroup properties, driver properties where available, and memory-budget extension where available.
5. Identify at least one compute-capable queue family (`VK_QUEUE_COMPUTE_BIT`).
6. Score devices by capabilities and user preferences, not by vendor string alone.
7. Provide manual device selection and an “Auto (recommended)” option.
8. Create a logical device with only features that were positively detected.
9. Record the exact driver version, Vulkan API version, device name, vendor ID, device ID, and capability fingerprint.

Support multiple Vulkan devices where the backend permits. Do not assume the first physical device is the desired GPU; Windows systems may expose a software adapter, iGPU, dGPU, or multiple ICDs.

### 5.2 Feature tiers

Implement a capability-driven tier system:

- **HCS Base Compute:** compute queue, SPIR-V compute shader, storage buffers, push constants/uniform parameters as appropriate, basic FP32/int operations, workgroup barriers, device-local or host-visible memory according to device properties.
- **HCS Async:** timeline semaphores, synchronization2, async transfer queues, descriptor indexing, memory budget/priority, when available.
- **HCS Subgroup:** subgroup operations and subgroup-size controls only when queried and validated.
- **HCS Matrix:** cooperative matrix/matrix instructions only with appropriate extension/feature and reference-test results. Do not infer it from AMD architecture names.
- **HCS FP16 / BF16 / INT8:** each separately gated based on Vulkan shader features, format support, and shader compiler support.
- **HCS Advanced Memory:** device-address, external memory, sparse resources, memory priority, or other features only when explicitly available.

A Vulkan device can be “supported” for base compute while missing advanced tiers. This is expected.

### 5.3 Core implementation details

- Use command pools per thread/queue as required by Vulkan external synchronization rules.
- Use descriptor sets or descriptor buffers only where the device feature is available and the implementation is stable. Start with descriptor sets for broad support.
- Use storage buffers for portable compute data paths.
- Implement bounds checks for all kernels.
- Align buffers and respect `minStorageBufferOffsetAlignment`, `maxStorageBufferRange`, descriptor limits, max workgroup size, max workgroup invocations, and dispatch limits.
- Use pipeline cache with a versioned cache key: HCSUDA version, device UUID/driver fingerprint, compiler version, shader hash, specialization constants, target capabilities. Invalidate safely on changes.
- Use SPIR-V validation in debug/test builds and before first use of generated code in production where feasible.
- Use `vkQueueSubmit`/appropriate synchronization primitives with explicit dependencies. Do not assume queue order across different queues.
- Add validation-layer runs to CI/manual QA. Validation layers are a development/testing dependency, not a runtime prerequisite.
- Handle device loss and driver reset: mark affected resources invalid, stop dispatch, preserve model files/configuration, show recovery prompt, allow CPU fallback, and log the Vulkan result.

### 5.4 Shader and compiler path

V1 compiler plan:

1. Define HKL/HCSIR semantics and a small supported kernel subset.
2. Build a compiler front-end that emits verified HCSIR.
3. Lower HCSIR to GLSL compute shader or a documented shader intermediate form.
4. Compile to SPIR-V with a pinned shaderc/glslang toolchain.
5. Validate and optionally optimize with SPIRV-Tools.
6. Load SPIR-V and create Vulkan compute pipelines.
7. Run CPU reference implementation and Vulkan result comparison in tests.

Never claim that arbitrary C++ is automatically GPU-compilable. General C++ features such as exceptions, RTTI, dynamic allocation, arbitrary recursion, virtual dispatch, OS APIs, unrestricted pointer behavior, and host-only libraries must be rejected or handled only in host code.

### 5.5 CPU backend

The CPU backend must be real and tested, not a placeholder:

- Scalar fallback is mandatory.
- Detect AVX2/FMA and other safe features at runtime; select a specialized implementation only when the CPU supports it.
- Do not assume all x86-64 processors support AVX2.
- Use a bounded thread pool with user-configurable worker count (default auto, with a safe cap).
- Ensure same HCSIR operation semantics and validation as Vulkan, within declared floating-point tolerance.
- Provide a “CPU only” mode for machines without Vulkan or faulty GPU drivers.
- Report CPU backend use conspicuously; never silently claim GPU acceleration while executing CPU code.

---

## 6. CUDA source migration and compatibility plan

### 6.1 Why binary compatibility is not promised

NVIDIA documents that CUDA application binaries can contain architecture-specific CUBIN code and/or PTX. CUBIN is NVIDIA GPU machine code and PTX is NVIDIA’s virtual ISA compiled/JIT-compiled by NVIDIA’s toolchain for NVIDIA targets. AMD Radeon hardware does not execute NVIDIA SASS/CUBIN. A binary containing only CUBIN and no portable source/PTX cannot be transparently turned into a Vulkan shader by the Windows Vulkan driver.

Reference: <https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/cuda-platform.html>.

### 6.2 V1 migration paths

**Path A — CUDA source migration (primary):**

- Input: `.cu` / `.cuh` source and required source headers; user must have rights to the source/dependencies.
- Tool: `hcsport` CLI (final command: `hcsport`).
- Parse with Clang-based tooling where feasible; do not rely solely on regex for semantic translation.
- Convert only the explicitly documented syntax/API subset.
- Emit native HCSUDA C++/HKL source and a machine-readable migration report.
- Preserve source comments where possible and produce a diff.
- Require manual review for unsupported constructs.

**Path B — Textual PTX import (experimental):**

- Input is human-readable textual PTX only, not raw CUBIN/SASS.
- Implement a tokenizer/parser, grammar version declaration, control-flow validation, register/address-space model, and opcode table.
- Initially support only a tested subset: common integer/FP32 arithmetic, simple comparisons/branches, basic global/shared loads/stores, and selected conversions. Every opcode must have a unit test and a semantic mapping.
- Reject unsupported architecture-specific instructions, tensor/matrix operations, inline hardware-specific instructions, special barrier protocols, unsupported memory semantics, and unknown PTX versions.
- Lower accepted instructions through HCSIR; validate SPIR-V and compare against a CPU reference.
- Mark PTX import “Experimental” until conformance tests exist.

**Path C — CUDA Runtime compatibility shim (post-V1 or feature-gated):**

- Do not ship a global replacement `nvcuda.dll` or overwrite any NVIDIA/AMD system file.
- If implementing selected `cudart` ABI compatibility, put it behind an opt-in, per-application isolated launcher and exact DLL version selection. Implement only a documented subset and expose a compatibility report before launch.
- Applications may import `cudart`, `nvcuda`, cuBLAS, cuDNN, NVRTC, TensorRT, or use undocumented assumptions. Do not claim they will load simply because a few symbols exist.
- Do not spoof NVIDIA GPU identity or CUDA device properties; that can lead to incorrect kernel/library behavior.
- For Driver API compatibility, separate contexts/modules/streams/memory from HCSUDA objects and map only semantics that can be preserved. If not, return `CUDA_ERROR_NOT_SUPPORTED` in the compatibility shim and explain the limitation.

### 6.3 CUDA API compatibility matrix

Create `docs/compatibility/cuda-api-matrix.md`. Start with these groups and fill in exact function coverage:

| CUDA family | HCSUDA V1 target |
|---|---|
| Basic device enumeration | HCS device enumeration, not identical NVIDIA device semantics |
| Device properties / compute capability | Translate to HCS capability report; no fake CUDA compute capability |
| Basic allocation/free | HCSMemory equivalent for migrated source |
| Host/device copy | HCS buffer upload/download and mapped memory where available |
| Kernel launch syntax | Source translator subset only |
| Streams/events | HCSFlow/HEvent mapped semantics, tested subset |
| Graphs | HCSGraph DAG subset; not claimed ABI-equivalent |
| Unified Memory | UMA-aware HCSMemory model, not CUDA Managed Memory ABI-equivalence |
| Atomics/barriers | Feature- and semantics-gated |
| Cooperative Groups | Only explicit subset if group behavior can be preserved |
| Dynamic Parallelism | Not V1 |
| Tensor Cores / WMMA / MMA | No generic AMD mapping; optional future matrix path only if target Vulkan features exist |
| cuBLAS | No complete ABI compatibility; provide HCS math library API for supported kernels |
| cuDNN | Not V1 ABI-compatible |
| TensorRT | Not V1 ABI-compatible |
| NCCL / NVSHMEM / GPUDirect | Not V1 unless implemented with independent cross-vendor semantics and hardware support |
| CUBIN / SASS | Unsupported on AMD |
| PTX text | Experimental, opcode whitelist only |
| CUDA fatbin | Extract text PTX only if a validated extractor is implemented; otherwise unsupported |

The migration report must show each construct as translated, unchanged host code, requires manual port, or unsupported. Never “best effort” compile unsupported code into incorrect output.

### 6.4 Legal and security rules for translation

- Do not bundle NVIDIA compiler binaries, proprietary headers, or NVIDIA libraries.
- Do not decompile proprietary binaries or bypass license checks.
- Accept user-provided source only; inform users that they must have rights to translate it.
- Use public specifications and clean, independently authored code.
- Treat all source code, PTX, SPIR-V, model metadata, and shader files as untrusted input.
- Limit parser memory, recursion, file size, compile time, and generated instruction count.
- Translation must be offline by default.

---

## 7. Runtime feature catalogue for V1 and roadmap

Implement the product by feature families. Add an API symbol only after its test exists.

### 7.1 V1 Stable feature families

1. **Device management:** enumerate Vulkan devices, query caps, select preferred device, CPU-only mode.
2. **Memory:** allocate/free buffers, upload/download, reusable pools/arenas, map/unmap when supported, memory budget reporting, allocation failure recovery.
3. **Execution:** compile/load SPIR-V, create compute pipeline, dispatch, wait, error check.
4. **HCSFlow:** ordered compute queue abstraction, submit, synchronize, query status.
5. **HEvent:** event/timeline abstraction with fence fallback.
6. **HCSIR/HCS-PIR:** verifier, serialization version, typed operations, deterministic unsupported-feature errors.
7. **Math kernels:** vector add/multiply, scale, reductions, prefix sum/scan where verified, tiled FP32 matrix multiply, elementwise activation functions, image/tensor copy/transform primitives.
8. **Compiler cache:** content-addressed, device/driver-aware, atomic writes, bounded size.
9. **CPU backend:** scalar + AVX2/FMA dispatch.
10. **Profiling basics:** wall time, queue submit/dispatch time where measurable, transfer sizes, allocation count, CPU/GPU backend indicator; no fabricated hardware counters.
11. **Diagnostics:** logs, device information, shader validation errors, crash-safe reports.
12. **Model catalog:** GGUF indexing, checksums, model metadata, HDD path, bounded staging.
13. **LLM inference adapter:** llama.cpp Vulkan backend integration with documented limits and verified Radeon 680M test where hardware is available.
14. **UI:** all navigation and settings listed below must work.
15. **REST API + MCP:** documented, authenticated/localhost-bounded, integration tested.
16. **Installer/uninstaller:** single EXE, clean install, upgrade, rollback-safe behavior, no driver modification.

### 7.2 Experimental in V1 only if implemented and tests exist

- Textual PTX subset translator.
- CUDA source compatibility for supported constructs.
- HCSGraph recording/replay subset.
- Advanced subgroup operations.
- Optional matrix extensions.
- ONNX inference adapter using supported Windows ML execution providers.

### 7.3 Roadmap, not V1 commitments

- Full CUDA Driver API ABI compatibility (not promised).
- Full CUDA Runtime API ABI compatibility (not promised).
- cuBLAS/cuDNN/TensorRT-compatible libraries.
- General fatbin/CUBIN/SASS translation (not a realistic cross-vendor promise).
- Multi-GPU peer-to-peer across arbitrary vendors.
- distributed collectives equivalent to NCCL.
- Dynamic parallelism, cooperative multi-device launch, GPUDirect-like paths.
- Full source-level C++ CUDA compiler with unrestricted device C++.

---

## 8. Built-in algorithms, AI, and model vault

### 8.1 Required HCS math primitives

Implement and test the following as small kernels with CPU references:

- Fill, copy, clear, scale, add, subtract, multiply, divide, clamp, min/max.
- Dot product.
- Reduction sum/min/max.
- Prefix sum/scan (exclusive and inclusive if included).
- Histogram with contention-aware algorithm.
- FP32 tiled matrix multiplication.
- Transpose.
- Basic image transforms (if included in UI, test them).
- Quantized tensor dequantization only if implemented and validated for named formats.

Start with robust FP32/int32. Add FP16/BF16/INT8 only after checking feature support and accuracy. Do not write “Tensor Core acceleration” for an implementation that merely uses standard shader arithmetic.

### 8.2 AI model formats and engines

V1 must distinguish **stored model formats** from **runnable model formats**:

- GGUF: supported through the pinned llama.cpp Vulkan/CPU adapter, with exact tested quantization formats recorded.
- ONNX: optional adapter via a verified ONNX Runtime/Windows ML execution provider. GPU support is device/EP-dependent; CPU fallback is allowed and must be visible.
- Safetensors: catalog/store/inspect only unless an explicit inference engine is integrated and tested.
- PyTorch checkpoints / `.pt` / `.pth`: store/inspect only in V1; do not claim direct execution.
- `.safetensors`, `.bin`, `.pt`, `.pth`, and `.onnx` are not interchangeable.

Use the model runtime's official API; do not hand-parse arbitrary pickle-based Python model files.

### 8.3 HCSModelVault UI and behavior

- Choose model directory on any local or mounted drive.
- Show filename, model family, format, size, quantization, hash status, source URL if provided by user, and runnable/stored-only status.
- Add/import local model; optionally download from a user-selected approved URL with explicit consent.
- Enforce path traversal protection and safe filenames.
- Downloads: HTTPS, range/resume only if server supports it, checksum verification, cancellation, temp file + atomic rename.
- Do not redistribute weights in the repo or installer.
- Model load preview estimates host/GPU memory. State that estimates are approximate and depend on context length, batch size, quantization, backend, allocator, and driver.
- Provide model unload, cache cleanup, and “open model folder”.
- HDD performance warning: initial loading and random access will be slower than SSD; model bytes are staged into RAM/GPU-accessible memory as required.

### 8.4 Inference controls

Expose only settings genuinely supported by the selected model/backend:

- Model path.
- Context length.
- Batch/microbatch size.
- GPU layer/offload count where supported.
- CPU thread count.
- Backend selection: Auto / Vulkan / CPU.
- Memory cap.
- KV-cache format only when backend supports it.
- Temperature/top-p/repetition penalty and generation settings in the inference UI.
- Cancellation and safe unload.

Every inference result must report the actual backend used, model format, device name, and whether any layers fell back to CPU.

---

## 9. Desktop UI/UX specification

The UI must be polished, modern, consistent, accessible, and functional. Avoid an overdecorated “gaming dashboard”. Use HCSMedia/HCSUDA branding with a restrained dark/light theme and clear status colors.

### 9.1 Navigation

Required sections:

1. **Dashboard** — runtime status, selected device, feature tier, memory budget, active jobs, recent benchmark, quick actions.
2. **Devices** — all Vulkan devices, CPU backend, selected adapter, API/driver versions, compute queue, workgroup limits, subgroup info, heaps/budgets, detected optional features, compatibility badge.
3. **Models** — model vault, HDD path, add/import, scan, metadata, size, format, model health, load/unload, inference launch.
4. **Compute** — HCS kernel examples, run verified sample kernels, job list, backend, input sizes, output verification.
5. **Translate** — CUDA source/PTX input selection, compatibility scan, construct coverage, generated files, unsupported-op report, diff preview, export.
6. **Benchmarks** — kernel microbenchmarks, model inference benchmarks, warm-up/repetitions, CPU vs Vulkan comparison, export JSON/CSV/Markdown.
7. **Visualizer** — live job timeline, memory budget/usage graphs, transfer bytes, kernel durations, queue states, model staging. Distinguish measured values from estimated/unsupported values.
8. **API & MCP** — enable/disable local REST, endpoint address, auth token reset/copy, API docs, MCP transport configuration, connected-client events.
9. **Settings** — device selection, backend policy, performance/memory limits, paths, shader cache, API/MCP, privacy, logs, startup behavior, update channel.
10. **Diagnostics** — run self-check, Vulkan loader/ICD checks, driver status, shader compiler checks, export sanitized report.
11. **About** — product version, attribution, licenses, documentation, non-affiliation disclaimer.

### 9.2 Settings requirements

Every visible setting must persist, validate input, have a default, describe its effect, and have a reset option. Include:

- Automatic / manual GPU selection.
- Auto / Vulkan / CPU backend policy.
- Maximum HCSUDA GPU memory budget (safe default).
- CPU worker thread count.
- Model directory (HDD allowed).
- Temporary staging directory.
- Shader/pipeline cache directory and maximum size.
- Cache cleanup.
- Model prefetch policy (off / conservative / adaptive).
- Logging verbosity and log retention.
- API enablement, port, authentication token, local-only binding.
- MCP stdio and Streamable HTTP options; HTTP local-only and authenticated.
- Start with Windows (off by default).
- Update check (off or consent-based; no telemetry).
- Benchmark safety caps and power/thermal warnings.
- UI theme, scale, language (English V1; German can be added if complete).

### 9.3 Visualizer correctness

Do not display invented GPU utilization or temperature values. If the generic Vulkan backend cannot retrieve a metric, show “Not exposed by this driver/API”. A vendor-specific provider can be added only with documented interfaces, license review, and feature detection. Show actual dispatch durations measured with supported timestamp queries only when `timestampValidBits` and queue capabilities allow it. Mark CPU wall-clock timings separately.

### 9.4 Accessibility and resilience

- Keyboard navigation and visible focus.
- High contrast text and scalable UI.
- Tooltips and non-color-only status meaning.
- Long-running tasks cancellable.
- UI remains responsive during model scans, compilation, benchmarks, and inference.
- Device loss or model errors must not freeze the app.
- No hidden modal dialogs; show actionable error details.

---

## 10. HCSBridge REST API and MCP

### 10.1 REST API

Provide a versioned local API. Publish OpenAPI 3.1 JSON/YAML and examples.

Suggested endpoints:

- `GET /api/v1/health`
- `GET /api/v1/version`
- `GET /api/v1/devices`
- `GET /api/v1/devices/{id}/capabilities`
- `GET /api/v1/memory/budget`
- `GET /api/v1/runtime/status`
- `GET /api/v1/jobs`
- `POST /api/v1/jobs/compute` (only predefined safe sample/HCS program IDs in V1; no arbitrary code execution by default)
- `POST /api/v1/benchmarks` (bounded and user-authorized)
- `GET /api/v1/benchmarks/{id}`
- `GET /api/v1/models`
- `POST /api/v1/models/import`
- `POST /api/v1/models/{id}/load`
- `POST /api/v1/models/{id}/unload`
- `POST /api/v1/inference` (through verified adapters)
- `POST /api/v1/translation/scan`
- `POST /api/v1/translation/convert` (source file paths must be explicitly granted and sandboxed)
- `GET /api/v1/diagnostics`

API rules:

- Bind only to `127.0.0.1` / `::1` by default.
- Generate a random local API token; never use a hardcoded default.
- Use constant-time token comparison.
- Limit request body size, path length, JSON nesting, and upload size.
- Validate model and source paths against explicit allowed directories.
- Rate-limit expensive endpoints.
- Use structured JSON errors and correlation IDs.
- Never offer arbitrary shell execution, PowerShell execution, DLL loading, or unrestricted filesystem access as an API feature.

### 10.2 MCP server

Implement a local MCP server using current official MCP protocol requirements. At minimum support:

- `initialize` and negotiated protocol version.
- `notifications/initialized`.
- `tools/list` and `tools/call`.
- `resources/list` and `resources/read`.
- `prompts/list` and `prompts/get` if prompts are exposed.
- Correct JSON-RPC errors and protocol error codes.
- stdio transport as the primary mode.
- Streamable HTTP as an optional mode, bound to localhost, with Origin validation and authentication.

MCP tools (initial safe set):

- `hcsuda_status` — runtime, version, actual backend.
- `hcsuda_list_devices` — device inventory and capability tiers.
- `hcsuda_get_memory_budget` — budgets and current usage.
- `hcsuda_list_models` — indexed local models.
- `hcsuda_inspect_model` — safe metadata only.
- `hcsuda_benchmark_sample` — run a bounded predefined benchmark only after policy/consent.
- `hcsuda_translate_scan` — analyze a user-authorized CUDA source path.
- `hcsuda_get_compatibility_report` — read translation results.
- `hcsuda_get_diagnostics` — sanitized diagnostics.

Do **not** expose unrestricted `run_command`, arbitrary kernel compilation from untrusted MCP input, arbitrary file reads, or unrestricted downloads. Resource reads must be bounded and path-scoped. Log tool calls without recording secrets or full user model content.

MCP transport reference: <https://modelcontextprotocol.io/specification/2025-11-25/basic/transports> (verify the latest stable version during implementation; the stdio server must write only valid protocol messages to stdout, and Streamable HTTP must validate Origin and bind locally by default).

---

## 11. Security, privacy, and reliability

### 11.1 Threat model

Assume untrusted inputs can arrive from model files, PTX, CUDA source, shader sources, API requests, MCP clients, downloaded manifests, and malformed Vulkan drivers.

Protect against:

- Path traversal and symlink escape.
- Malicious shader/compiler input.
- Parser denial of service.
- Huge allocations and GPU hangs.
- Localhost DNS rebinding against the HTTP API.
- CSRF-like browser access to local endpoints.
- Unauthorized MCP tool calls.
- Command injection in model paths or CLI process launches.
- DLL search-order hijacking.
- Unsafe auto-update and unsigned binaries.
- Sensitive information in logs and diagnostics.

### 11.2 Required controls

- Use safe path canonicalization and allowlisted folders.
- Avoid invoking a shell for subprocess execution; pass argv arrays.
- Use per-user install by default; request elevation only for an explicitly justified installation step.
- Do not install a Windows service unless V1 has a demonstrated requirement. Prefer a per-user app process.
- Use local-only API binding, random tokens, strict Origin checks, bounded request sizes, and explicit opt-in for remote binding.
- Keep model content outside the application install directory.
- Verify downloaded files with SHA-256 and, when available, trusted publisher signatures.
- Write files atomically and recover from interrupted cache writes.
- Use Windows ACLs appropriate for per-user application data.
- No telemetry by default. If optional telemetry is ever added, document exactly what is sent and provide a clear off switch.
- Keep crash logs sanitized and bounded.
- Use ASLR/DEP/CFG and current MSVC security flags where supported.
- Run static analysis and dependency vulnerability scans.
- Produce an SBOM for every release.

---

## 12. Installer and update design

### 12.1 Deliverable

Create exactly one primary user-downloadable installer for V1:

`HCSUDA-Setup-x64.exe`

This is a self-contained installer that embeds HCSUDA binaries, runtime dependencies permitted for redistribution, Qt assets/plugins, required model adapter binaries (if licensed), product assets, license notices, and uninstaller metadata.

The installer must not require the user to install CMake, Visual Studio, Vulkan SDK, Python, Node, Git, or a CUDA Toolkit.

The machine must already have a compatible Vulkan driver/ICD for GPU acceleration. If absent, install CPU-only mode and show a link to the GPU vendor's official driver page. Never silently download or replace a graphics driver.

### 12.2 Install behavior

- Default install path: per-user `%LOCALAPPDATA%\Programs\HCSUDA`.
- User-selectable model directory, defaulting to a clear path under the user's profile but allowing HDD selection.
- Keep config under `%LOCALAPPDATA%\HCSUDA` and logs under `%LOCALAPPDATA%\HCSUDA\Logs`.
- Keep caches separate from model storage.
- Create Start Menu shortcuts; desktop shortcut optional.
- Start on login off by default.
- Do not modify global PATH or system DLL folders.
- Do not write to `System32`, `DriverStore`, Vulkan ICD registry keys, or NVIDIA driver folders.
- Check for already-running instance before upgrade; request graceful shutdown.
- Preserve user models and settings on upgrade/uninstall unless the user explicitly chooses to remove them.
- Support quiet install/uninstall flags for enterprise/CI testing and document them.
- Show detected Vulkan status and actual GPU names in a pre-install summary.

### 12.3 Packaging and supply chain

- Build release in clean Windows x64 CI using a pinned toolchain.
- Produce installer, portable ZIP for debugging (optional), SHA-256 checksum, SBOM (CycloneDX or SPDX), build metadata, and GitHub release notes.
- Code-sign the installer if a valid publisher certificate is configured. Never fake a signature. If unsigned, state that plainly and publish checksums.
- Do not claim reproducible builds unless two independent clean builds produce matching hashes or documented normalized artifacts.
- Scan the final EXE and packaged DLLs for unexpected imports, debug symbols, secrets, and unlicensed files.

### 12.4 Uninstaller tests

Verify that uninstall:

- Removes only HCSUDA-installed binaries and shortcuts.
- Does not remove AMD graphics drivers, Vulkan loader/ICD, user models, user data, or shared third-party runtimes.
- Offers a separate explicit choice for deleting model/cache/config data.
- Leaves no fake driver, DLL override, PATH entry, firewall rule, or system-wide registry hook.

---

## 13. Visual identity and README requirements

### 13.1 Original brand assets

Create an original HCSUDA icon and logo in SVG (and export PNG/ICO as needed). The mark should be technically modern, minimal, and identifiable at 16–64 px. It must not imitate NVIDIA's eye/green logo or any other vendor logo. Include text: “HCSUDA” and the attribution “Made with love by HCSMedia & timfromhcs”.

Use one coherent palette, readable typography, and consistent spacing. Support dark and light UI themes. Provide SVG source in `ui/assets/brand/`.

### 13.2 README in English

The final public `README.md` must be written in professional English and include:

1. Original logo and one-sentence value proposition.
2. Accurate badges: CI status, latest release, license. Badges must link to real project URLs; never display fake downloads, fake stars, fake coverage, or fake performance.
3. Short feature list separated into Stable / Experimental / Roadmap.
4. Clear compatibility matrix (Vulkan GPU requirements, CPU fallback, tested Radeon 680M status, CUDA source/PTX limitations, unsupported CUBIN/SASS).
5. System requirements and supported Windows versions.
6. Screenshots from the real V1 application, with alt text.
7. Download/install instructions for the single EXE.
8. First-run guide and “check my GPU” instructions.
9. Model-storage and HDD performance explanation.
10. Quickstart for HCS API and a small verified kernel.
11. CUDA source migration example and report screenshot/text.
12. REST API and MCP setup examples, including local-only security defaults.
13. Benchmarks with methodology, exact hardware/driver/build, and raw CSV/JSON artifacts.
14. Build-from-source steps for contributors.
15. Troubleshooting and diagnostics instructions.
16. License, third-party notices, security policy, and contribution links.
17. NVIDIA non-affiliation/trademark statement.
18. Known limitations; never hide material gaps in a footnote.

Do not use placeholder claims such as “all GPUs”, “full CUDA”, “SOTA performance”, “10x faster”, or “CUDA replacement”. If a screenshot or benchmark has not been generated from the actual V1 build, do not include it as product evidence.

---

## 14. Build setup from an empty directory

The first agent run must inspect the environment and record:

- `git --version`
- `cmake --version`
- `ninja --version`
- MSVC toolchain and Windows SDK versions
- Vulkan loader availability and `vulkaninfo` output when available
- installed GPU(s), driver versions, Vulkan API versions, queue and memory properties
- available RAM and disk free space
- Qt/toolchain/dependency versions

Do not assume the Vulkan SDK is installed on end-user machines. It is a developer/build dependency only.

### 14.1 First setup commands (PowerShell)

Document and use commands equivalent to:

```powershell
cmake --version
ninja --version
git --version
where.exe cl
where.exe vulkaninfo
vulkaninfo --summary
```

If `vulkaninfo` is absent, record “not installed” rather than treating it as proof that the GPU has no Vulkan support. The Vulkan loader and installed ICD are runtime dependencies provided by the vendor driver; the SDK supplies build/validation tools.

### 14.2 CMake presets

Create presets for at least:

- `windows-debug`
- `windows-release`
- `windows-relwithdebinfo`
- `windows-cpu-only`
- `windows-vulkan`
- `windows-tests`

Use explicit architecture `x64`. Keep release and debug build directories separate. Generated code belongs under the build tree.

### 14.3 Reproducible developer commands

The following project-level commands must work after implementation:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-tests --output-on-failure

cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-tests --output-on-failure
```

If the actual CMake preset schema requires a slightly different build/test preset arrangement, implement valid presets and update this documentation accordingly. Do not publish untested commands.

---

## 15. Test strategy and proof requirements

A V1 release is blocked until every required gate has evidence. Use test IDs and store concise output summaries in `docs/TEST_MATRIX.md`.

### 15.1 Unit tests

Required coverage:

- HCSIR parser, verifier, serializer/deserializer, version rejection.
- HCS-PIR typing and opcode semantics.
- Device capability scoring.
- Memory alignment and allocation bookkeeping.
- Memory budget reserve/cap policy.
- Flow/event state transitions.
- Graph dependency validation.
- Shader cache keys, corruption recovery, atomic writes.
- CUDA-source translator AST mapping and unsupported diagnostics.
- PTX parser malformed-input handling and opcode whitelist.
- Model manifest parsing, path safety, hash/checksum verification.
- API schemas, auth, rate limits, request limits.
- MCP JSON-RPC framing, protocol initialization, tool permissions.
- Settings migrations and corrupt-config recovery.
- Error string and ABI version consistency.

### 15.2 Correctness tests

- Compare every supported Vulkan kernel against a CPU reference over random and boundary inputs.
- Test zero-length, one-element, odd sizes, non-multiples of workgroup size, very large indices, unaligned inputs where API allows, and overflow boundaries.
- For floating-point results, specify and enforce a numerical tolerance and document fast-math behavior.
- Test NaN, infinity, signed zero, denormals, overflow and underflow where relevant.
- Test races and synchronization with Vulkan validation layers and repeated randomized schedules.
- Test CPU-only, Vulkan-only and automatic backend selection.
- If Vulkan unavailable, app must open in CPU mode with clear status.
- If device lost, no stale object may be reused; app should recover or exit cleanly.

### 15.3 Translation conformance tests

- Maintain a small corpus of permissively licensed CUDA examples or hand-written tests with license metadata.
- For each supported translation construct, compare CPU reference output with HCSUDA Vulkan output.
- Include unsupported CUDA constructs to ensure the translator fails clearly instead of emitting incorrect code.
- Include PTX parser fuzzing with malformed and oversized inputs.
- Test unknown PTX versions/opcodes; they must be rejected.
- Never mark a CUDA library or binary family supported based only on a single source example.

### 15.4 Integration tests

- Clean launch and shutdown.
- Enumerate Radeon 680M and show the correct vendor/device name when available.
- Run vector-add, reduction, matrix-multiply and memory-copy samples.
- Run multiple independent flows with valid dependencies.
- Test API auth and local-only bind.
- Test MCP stdio protocol with a reference client/harness.
- Test HTTP Origin validation and auth.
- Import/load/unload a small legally redistributable test GGUF model through llama.cpp Vulkan and CPU adapter if hardware allows; test cancellation and memory cap.
- Test model on a secondary drive and a path containing spaces/non-ASCII characters.
- Test missing/disconnected HDD and corrupted model.

### 15.5 Stress and soak tests

Bounded test profile (adjust to hardware and document exact parameters):

- 10,000+ short dispatches and repeated pipeline reuse.
- 1,000+ buffer allocations/frees through the pool, with leak checks.
- 30–60 minute mixed compute/inference soak with conservative memory cap.
- Repeated model load/unload.
- Concurrent UI monitoring + API queries + one compute job.
- Cancellation during shader compile, model prefetch, benchmark, and inference.
- Low-disk-space behavior in a disposable test directory.
- Restart/reboot persistence for settings and model catalog.
- Upgrade from prior test build and uninstall.

Do not run unconstrained “allocate all RAM/VRAM” tests on the user's everyday computer. Use a test VM or controlled machine for destructive/install scenarios.

### 15.6 Performance benchmark methodology

Every benchmark report must include:

- CPU model, GPU model, RAM type/capacity if known, Windows build, GPU driver, Vulkan API/driver version, HCSUDA commit/version, compiler/build mode.
- Backend and selected device.
- Input sizes, datatype, precision, workload, warm-up count, repetition count.
- Median, p10/p90 or standard deviation, and raw samples.
- CPU reference implementation settings.
- Whether time includes data transfer, shader compilation, model load, or only kernel execution.
- Power/thermal state if measured; otherwise mark unknown.
- Result validation status.

Required V1 benchmark suite:

- Dispatch overhead / no-op kernel.
- Memory copy/upload/download.
- Vector add and scale at multiple sizes.
- Reduction.
- FP32 matrix multiplication at small/medium sizes.
- End-to-end GGUF inference: prompt processing and token generation separately, with tested model, quantization, context, batch, and offload settings.
- CPU vs Vulkan comparison on the same machine.

A GPU may lose to CPU for small workloads due to dispatch/transfer overhead. Report that honestly. No hard minimum speedup is required for V1; correctness, stability, and reproducible measurements are required.

### 15.7 UI and visual regression tests

- Screenshot every major page in light/dark themes at supported window sizes.
- Test keyboard navigation and scaling.
- Test loading/error/empty states, not just successful populated screens.
- Test that each visible button performs its advertised action.
- Test that every settings value persists after application restart.
- Test long paths, large model lists, missing GPU and no network.
- Ensure charts use real values with units and clear unavailable states.

### 15.8 Installer tests

Run on a clean Windows 11 VM or a disposable clean Windows test machine:

1. Install as standard user when possible.
2. First launch.
3. Vulkan detection.
4. CPU-only operation.
5. Install/upgrade over a previous HCSUDA build.
6. Uninstall with preserve-data selection.
7. Uninstall with explicit data-removal selection in disposable VM.
8. Confirm AMD graphics driver, Vulkan loader/ICD and other GPU software remain intact.
9. Confirm no global DLL or driver modifications.
10. Confirm installer checksum matches published checksum.

---

## 16. Autonomous build-test-fix loop (“waves”)

Do not jump straight to the complete application. Build in vertical slices. Every wave ends with a clean build and its relevant tests. If a test fails, root-cause and fix it before moving forward unless the blocker is documented and the affected feature is disabled.

### Wave 0 — Reconnaissance and project contract

- Read this file fully.
- Inspect the empty directory and available environment.
- Verify toolchain and Vulkan availability.
- Create architecture decision records (ADRs) for major choices.
- Create project skeleton, license, third-party policy, and CI skeleton.
- Write `docs/AGENT_STATUS.md`, `docs/BUILD_LOG.md`, `docs/TEST_MATRIX.md`, and `docs/KNOWN_LIMITATIONS.md`.
- Do not write feature claims yet.

**Gate 0:** clean configure/build of a minimal executable; CI runs; project contract committed.

### Wave 1 — Core API and CPU reference backend

- Implement version/types/errors.
- Implement device abstraction and CPU backend.
- Implement vector add/scale/reduction CPU kernels.
- Implement tests and C ABI smoke test.

**Gate 1:** CPU-only sample passes on clean Windows 11 x64; error handling and ABI tests pass.

### Wave 2 — Vulkan discovery and diagnostics

- Implement loader detection, enumeration, feature/queue/memory queries.
- Implement detailed device report and `hcsdoctor`.
- Implement “no Vulkan” fallback.
- Implement memory budget and cap policy.

**Gate 2:** device report is accurate on available hardware; no unsupported feature is enabled by assumption; CPU fallback works.

### Wave 3 — Minimal Vulkan compute

- Implement Vulkan instance/device/queue/context.
- Implement storage buffers and memory allocator.
- Implement one SPIR-V compute shader (vector add).
- Add validation-layer debug run, bounds checks, result comparison.
- Add safe device-loss behavior.

**Gate 3:** Vulkan vector-add passes on at least one real Vulkan GPU; for product’s reference hardware, require a Radeon 680M run before claiming Radeon 680M tested.

### Wave 4 — HCSIR/HCS-PIR and compiler path

- Specify and verify IR semantics.
- Implement minimal compiler and shader lowering.
- Add SPIR-V validation and content-addressed cache.
- Add kernel API and workgroup dispatch config.

**Gate 4:** the same test corpus passes on CPU and Vulkan; unsupported IR is rejected predictably.

### Wave 5 — HCSFlow, HEvent, HCSGraph

- Add queue/stream abstraction and synchronization.
- Add event timing/query.
- Add graph DAG and dependency validation.
- Implement fallback where Vulkan feature is missing.

**Gate 5:** repeated asynchronous runs are race-free under validation and stress harness; ordering tests pass.

### Wave 6 — Math kernels and tuning

- Implement reductions, scans if feasible, tiled FP32 GEMM, and verified vector/image primitives.
- Add benchmark harness.
- Profile; optimize only after correctness.

**Gate 6:** correctness suite passes, benchmark data saved, no claim of SOTA or speedup without evidence.

### Wave 7 — CUDA migration tool

- Implement source scanner and AST migration subset.
- Generate migration report and diff.
- Add unsupported diagnostics.
- Only then add experimental raw PTX parser/opcode subset if time and conformance tests allow.

**Gate 7:** supported subset tests pass; unsupported constructs fail closed; compatibility matrix matches actual implementation.

### Wave 8 — AI model vault and inference adapter

- Implement SQLite catalog, user HDD path, scan/import/checksum.
- Integrate pinned llama.cpp Vulkan adapter and CPU fallback.
- Implement budget-aware loading, cancellation, safe unload.
- Test one small permissively redistributable test model; do not include weights in release.

**Gate 8:** real local inference is demonstrated on a supported backend; backend, memory use and speed are reported honestly. If Radeon 680M hardware is not available to CI, record manual QA as outstanding.

### Wave 9 — REST API and MCP

- Implement local REST API and OpenAPI contract.
- Implement MCP stdio protocol and safe tool registry.
- Add optional authenticated Streamable HTTP with strict localhost/Origin policy.
- Add client/harness tests.

**Gate 9:** protocol tests pass, API is local-only by default, no arbitrary command/file-execution tools exist.

### Wave 10 — Full UI/UX

- Implement all navigation pages and settings.
- Connect each control to real core/service state.
- Add visualizer with actual measured metrics only.
- Add accessibility, empty/error/loading states.
- Run UI automation/screenshot tests.

**Gate 10:** no dead UI controls, settings persist, UI remains responsive, screenshots reflect real runtime data.

### Wave 11 — Installer and update lifecycle

- Build single-file installer.
- Add runtime prerequisite checks and CPU-only path.
- Add upgrade/uninstall preservation rules.
- Test clean VM install/uninstall.
- Scan final package and verify third-party notices.

**Gate 11:** installer is one EXE, installs without developer tools, never changes GPU driver, and passes clean VM tests.

### Wave 12 — Reliability, security and performance

- Complete unit/integration/stress/soak runs.
- Fuzz parsers and APIs.
- Run static analysis and dependency scanning.
- Run CPU and Vulkan benchmark suites.
- Fix regressions; repeat all affected tests.

**Gate 12:** no known critical/high security issue; no known crash/data corruption in required scenarios; all V1 release criteria are met or explicitly scoped out.

### Wave 13 — Documentation, GitHub and release candidate

- Write accurate English README from actual feature matrix.
- Create original SVG logo, real screenshots, architecture diagrams.
- Add LICENSE, NOTICE, THIRD_PARTY_NOTICES, SECURITY, CONTRIBUTING, CODE_OF_CONDUCT, CITATION.cff, issue templates.
- Add GitHub Actions CI and release workflow.
- Build signed installer if certificate configured; otherwise unsigned + checksum.
- Generate SBOM and provenance/attestation when available.
- Tag `v1.0.0-rc.1`, run release-candidate checks, fix, repeat.

**Gate 13:** public release is not published until all release blockers are cleared and repository ownership/authentication are confirmed.

### Loop rules for every wave

For each task:

1. Re-read relevant requirements and current status.
2. Write a small implementation plan with acceptance tests.
3. Implement the smallest end-to-end slice.
4. Format and run static analysis.
5. Build the affected targets in Debug and Release as appropriate.
6. Run unit tests.
7. Run relevant integration/device/UI/API tests.
8. Inspect logs and actual output.
9. If failure: reproduce, identify root cause, add regression test, fix, rerun.
10. Update build/test logs and compatibility matrix.
11. Review the diff for accidental scope changes, secrets, generated files, license concerns, false claims, and incomplete controls.
12. Commit only coherent, tested changes.

Do not loop indefinitely on an unsupported hardware feature. Record the blocker, disable the feature, and continue with a correct fallback. A stable smaller product is better than a broad broken product.

---

## 17. Continuous integration and repository hygiene

### 17.1 CI matrix

Set up GitHub Actions for:

- Windows Server x64 Debug configure/build.
- Windows Server x64 Release configure/build.
- CPU-only unit/integration tests.
- Vulkan shader compile and SPIR-V validation.
- Static analysis/format checks.
- Dependency and secret scanning.
- License/SBOM check.
- Installer build (after installer phase).
- Optional self-hosted GPU test runner for actual Vulkan device tests; label its GPU/driver and do not confuse it with hosted CPU CI.

Public hosted CI may not expose a suitable physical AMD Vulkan GPU. Use mocks/CPU tests in hosted CI and a documented self-hosted AMD GPU workflow for real hardware tests. Never mark a physical GPU test as passed if it was skipped.

### 17.2 Code quality

- Use `.editorconfig`, `.gitignore`, `.gitattributes`, clang-format, clang-tidy where applicable.
- Warnings are errors in project-owned code for the stable build where practical; third-party warnings may be scoped separately.
- No giant monolithic source files; maintain clear interfaces.
- No undocumented global state for device contexts.
- No hidden background thread that outlives the app/service.
- All owned allocations have explicit lifetime and cleanup.
- Use RAII for Vulkan and OS handles.
- Avoid exceptions crossing the public C ABI.
- Use semantic versioning and keep changelog updated.

### 17.3 Git discipline

- Use small descriptive commits (Conventional Commits preferred).
- Do not commit build directories, generated `.spv` binaries except intentional documented test fixtures, local model files, logs, installers, or credentials.
- Add binary artifact output patterns to `.gitignore`.
- Protect `main` with CI checks when repository settings permit.
- Use pull requests for substantial work if multiple contributors/agents are active.

---

## 18. V1 release criteria — stable means actually stable

V1 is releasable only when all mandatory items below are true:

### Runtime and compute

- [ ] HCSUDA launches on clean supported Windows 11 x64 without requiring developer tools.
- [ ] CPU fallback works when Vulkan is unavailable.
- [ ] Vulkan devices are discovered and selected by capabilities.
- [ ] Device/driver/API/memory report is accurate.
- [ ] Memory budget cap and graceful OOM behavior work.
- [ ] Verified kernels run correctly on Vulkan and CPU reference backend.
- [ ] Streams/events or their V1 HCSFlow/HEvent equivalents have deterministic tests.
- [ ] No false support for unavailable FP16/BF16/matrix/subgroup features.
- [ ] Pipeline/shader cache survives restart and recovers from corruption.

### Translation

- [ ] Source migration report distinguishes translated/manual/unsupported code.
- [ ] Every advertised source construct is covered by conformance tests.
- [ ] If PTX is advertised, the exact PTX version/opcode subset is published and tested.
- [ ] CUBIN/SASS and unsupported CUDA libraries are clearly marked unsupported.
- [ ] No global CUDA DLL replacement or GPU driver modification exists.

### Models and AI

- [ ] User can select HDD model path.
- [ ] Model scan/import/checksum/path safety work.
- [ ] Active memory is staged under a cap; HDD is never described as GPU memory.
- [ ] At least one supported GGUF model runs through a verified adapter or the AI module is omitted from V1 feature claims.
- [ ] Backend selection and actual offload/fallback are visible.
- [ ] Model files are not bundled into installer/repository without explicit redistribution rights.

### UI/API/MCP

- [ ] Every visible navigation item works.
- [ ] All settings persist and have validation/default/reset.
- [ ] Visualizer shows only real measured/queried data or “unavailable”.
- [ ] REST API is local-only by default, authenticated and documented.
- [ ] MCP stdio is protocol-tested; HTTP mode is secure or omitted.
- [ ] No unrestricted code execution, shell tool, or arbitrary filesystem read is exposed.

### Installer/release

- [ ] Single `HCSUDA-Setup-x64.exe` is produced.
- [ ] Clean VM install, launch, upgrade and uninstall pass.
- [ ] No AMD/Vulkan/NVIDIA system driver files/registries are changed.
- [ ] Third-party licenses and notices are complete.
- [ ] Final EXE is scanned; checksum and SBOM exist.
- [ ] README and compatibility matrix match the actual build.
- [ ] Release evidence links to real CI and real test reports.
- [ ] No P0/P1 bugs and no known critical/high security issue.

### Hardware QA

- [ ] Radeon 680M reference test run completed and recorded before claiming it is tested.
- [ ] At least one additional Vulkan vendor/device tested before claiming cross-vendor validated.
- [ ] Generic Vulkan support is described as capability-dependent, not universal.

If a hardware item cannot be tested, it remains explicitly “Not verified” and the release copy must not overstate it.

---

## 19. Public GitHub publication

### 19.1 Repository target

First inspect `git remote -v` and authenticated GitHub identity. If the repository already has a valid remote, use it. Otherwise propose `HCSMedia/HCSUDA` only if that namespace is actually owned/authorized and the repository name is available. Do not assume the username or organization from the attribution line.

The repository should be public only after:

- The owner has authenticated GitHub access.
- The target repository/organization is confirmed.
- License, trademark, third-party redistribution, and model asset review are complete.
- No secrets/private paths/test model weights are present.

Never invent a remote URL that appears to have been pushed successfully. If GitHub authentication/connection is unavailable, finish the local repository and give exact push/release commands; clearly report that publication was not performed.

### 19.2 Initial push workflow

After authentication and target confirmation:

```powershell
git init
git branch -M main
git add .
git status --short
git diff --cached --check
git commit -m "chore: initialize HCSUDA compute runtime"
git remote add origin <confirmed-repository-url>
git push -u origin main
```

Do not use a placeholder URL. Review `git status` and staged diff for secrets and large files before commit.

### 19.3 Release workflow

- Tag only after V1 gates pass: `v1.0.0`.
- Use a GitHub Actions release workflow triggered by a signed/annotated tag or manually approved release event.
- Attach `HCSUDA-Setup-x64.exe`, SHA-256 checksum file, SBOM, build metadata, and release notes.
- Add a GitHub Release with stable/experimental/unsupported feature lists.
- Include screenshots and test evidence in the release body or documentation.
- Do not write download counts or performance claims unless generated by GitHub or measured.

---

## 20. Reference documentation and upstream sources

Check these references before implementation and re-check their current versions during Wave 0. They are technical references, not permission to copy proprietary code.

### AMD and reference hardware

- AMD Ryzen 7 7735HS specifications: <https://www.amd.com/en/products/processors/laptop/ryzen/7000-series/amd-ryzen-7-7735hs.html>
- AMD Software / driver installation guidance: <https://www.amd.com/en/resources/support-articles/faqs/rsx2-install.html>
- AMD Vulkan driver support and extension release notes: <https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-VULKAN.html>

### Vulkan and tooling

- Vulkan specification: <https://docs.vulkan.org/spec/latest/>
- Vulkan guide — memory allocation: <https://docs.vulkan.org/guide/latest/memory_allocation.html>
- Vulkan `VK_EXT_memory_budget`: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html>
- Vulkan subgroups: <https://docs.vulkan.org/guide/latest/subgroups.html>
- LunarG Vulkan SDK: <https://www.lunarg.com/products/vulkan-sdk/>

### CUDA compatibility references

- NVIDIA CUDA platform, PTX and CUBIN overview: <https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/cuda-platform.html>
- AMD HIP porting guide: <https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_porting_guide.html>
- AMD HIPIFY limitations: <https://rocm.docs.amd.com/projects/HIPIFY/en/latest/index.html>

### AI runtime and MCP

- llama.cpp build documentation (Vulkan): <https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md>
- llama.cpp operation/backend support table: <https://github.com/ggml-org/llama.cpp/blob/master/docs/ops.md>
- ONNX Runtime Windows guidance: <https://onnxruntime.ai/docs/get-started/with-windows.html>
- ONNX Runtime DirectML provider status/limitations: <https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html>
- MCP specification: <https://modelcontextprotocol.io/specification/2025-11-25>

### Release engineering

- GitHub Actions documentation: <https://docs.github.com/actions>
- GitHub Releases documentation: <https://docs.github.com/repositories/releasing-projects-on-github/about-releases>
- SPDX: <https://spdx.dev/>
- CycloneDX: <https://cyclonedx.org/>

Always verify upstream pages and versions. If a reference changes, update the documentation and pin an appropriate tested version.

---

## 21. Final operating instruction to the agent

Start at Wave 0. Build HCSUDA as a real, modest, dependable cross-vendor compute platform first. Prioritize **correctness, portability, observable behavior, memory safety, and clear compatibility reporting** over headline feature count.

The reference Radeon 680M system is a valuable first target, but the runtime must remain capability-driven and must not assume all Vulkan GPUs are equivalent. Keep the HDD as the model vault, not as execution memory. Keep the AMD driver intact. Use Vulkan for hardware execution and CPU fallback for portability.

The final V1 deliverable is not complete until the installer, runtime, UI, APIs, MCP, diagnostics, tests, English README, license notices, release assets, and compatibility statements all agree with the actual code and actual test evidence.

**Made with love by HCSMedia & timfromhcs.**
