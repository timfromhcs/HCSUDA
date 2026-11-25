# Agent status

Updated: 2026-11-25 (Wave 0–6 complete, Waves 7–9 in progress)

## Environment (measured, not assumed)

| Item | Value |
|---|---|
| OS | Windows 11 Pro 10.0.26200 |
| CPU | AMD Ryzen 7 7735HS, 8C/16T |
| GPU | AMD Radeon(TM) Graphics (680M), device id 0x1681, integrated UMA |
| Driver | 32.0.21043.12001, Vulkan 1.4.315 |
| Vulkan loader | `C:\Windows\System32\vulkan-1.dll` |
| Vulkan SDK | 1.4.357.0 (`glslc`, headers) |
| Compiler | MSVC 19.44.35228 (VS 2022 Community 17.14) |
| CMake / Ninja | 4.4.0 / 1.13.2 |
| Qt | not installed |
| Inno Setup | not installed |
| WebView2 runtime | present |
| RAM / free disk | 32 GB / 233 GB (D:) |

## Wave status

| Wave | Scope | Status | Notes |
|---|---|---|---|
| 0 | Reconnaissance, project contract, build skeleton | **Done** | MSVC + Vulkan SDK verified; presets build |
| 1 | Core API + CPU reference backend | **Done** | 69 registered tests, all passing |
| 2 | Vulkan discovery + diagnostics | **Done** | `hcsdoctor` reports real 680M caps/budget |
| 3 | Minimal Vulkan compute | **Done** | vecadd/scale/reduce/matmul verified vs CPU |
| 4 | HCSIR/HCS-PIR verifier + compiler path | **Almost** | parser, verifier, CPU interpreter, GLSL lowering with build-time `glslc` check, 34/34 tests; GPU execution of HCSIR via the C ABI still missing |
| 5 | HCSFlow/HEvent/HCSGraph async | **Done** | async flows, HEvent, bounded linear HCSGraph; validation profile green; 46/46 tests |
| 6 | Math kernels + tuning | **Done** | reduce, matmul, scan, transpose verified on CPU + Vulkan; benchmark rows recorded; histogram/image primitives deliberately out of V1 scope |
| 7 | CUDA source migration (`hcsport`) | **Partial** | bounded compatibility scanner reports manual/unsupported constructs; no translator, no PTX importer |
| 8 | Model vault + GGUF (llama.cpp Vulkan) | **Partial** | catalog, SHA-256, path safety and format inspection tested; no inference adapter yet |
| 9 | REST API + MCP server | **Partial** | MCP stdio (MCP-01..06) and local-only REST with token auth (REST-01..05) pass; model/benchmark/translation endpoints and OpenAPI generation pending |
| 10 | Desktop UI | Pending | Qt absent; WebView2 strategy drafted |
| 11 | Installer (one EXE) | Pending | Inno Setup absent |
| 12 | Stress/security/performance hardening | Pending | |
| 13 | Docs, CI, release candidate | Pending | CI workflow present, not yet run on GitHub |

## Implemented surface

* C ABI v1: init/shutdown, enumeration, `hcsGetDevice`, caps, memory budget, contexts,
  buffers, flows, kernels, dispatch, structured errors, version identity.
* CPU backend: scalar + AVX2/FMA with runtime CPUID gate, bounded thread use, deterministic
  clamping to the smallest bound.
* Vulkan backend: dynamic loader, capability probing (incl. subgroup size and memory budget),
  host-visible coherent buffers, compute pipelines, synchronous dispatch, device-loss mapping.
* Built-in kernels: `vecadd_f32`, `scale_f32`, `reduce_sum_f32`, `matmul_f32`.
* Tools: `hcsdoctor`, `hcsbench`; SPIR-V compiled at build time by `glslc` and embedded.
* HCSIR v1: bounded parser, verifier and CPU interpreter for the elementwise subset
  (`load`/`store`/`add`/`sub`/`mul`/`fma`/`min`/`max`/comparisons/`select`/`branch`/`jump`),
  with `const` declarations and source-line diagnostics. Internal only — not in the C ABI.
* HCSCC: lowers verified HCSIR to GLSL 450 compute shaders (state-machine CFG, bounds
  checks, push constants); the build compiles the generated shader with `glslc`.
* Experimental program API: HCSCC-generated SPIR-V can be loaded into a Vulkan pipeline
  and dispatched. The CPU backend reports this as unsupported rather than decoding SPIR-V.
* Async HCSFlow: per-submission Vulkan command buffers, descriptor sets and fences; flows
  own in-flight work and support timeout-aware synchronize. HEvent is a flow-order marker
  backed by fence status; the CPU backend provides the same semantics synchronously.
* Experimental bounded HCSGraph: nodes added in topological order, dependencies must be
  earlier nodes, compile validates, run replays sequentially on one flow. No parallel DAG
  scheduling is claimed.
* Math kernels: exclusive chunked scan (256-lane workgroup scan with lane masking) and
  16x16 tiled transpose, each with a CPU reference and odd-size/boundary tests.
* Tests: 52 tests covering API contract, CPU correctness, Vulkan correctness against CPU
  reference, memory cap enforcement, device report sanity, HCSIR accept/reject behaviour,
  generated-GLSL properties, GPU execution of a generated HCSIR module, flow/event ordering,
  and graph replay on both backends.

## Immediate next actions (Wave 6)

1. Add verified scan/prefix-sum and transpose kernels (CPU reference + Vulkan + odd sizes).
2. Add the benchmark harness entries for the new kernels and save raw measurements.
3. Only then consider timeline semaphores where `HCS_CAP_TIMELINE_SEMAPHORE` is present; the
   fence implementation remains the tested default.

Validation layers are wired into `windows-validation` and pass with zero errors.

Deferred from Wave 4 (not a release blocker): HCSIR serialization/round-trip caching.

## Known gaps that block a V1 release

See `docs/KNOWN_LIMITATIONS.md`. The largest are: no async execution, no UI, no installer,
no CUDA migration, no model runtime, no REST/MCP, single-vendor hardware validation only
(AMD 680M; Intel/NVIDIA not tested).
