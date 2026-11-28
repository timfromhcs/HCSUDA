# Agent status

Updated: 2026-11-28 (release candidate; Waves 0–13 implemented, only external blockers remain).
Limitless Work loop active: `docs/LIMITLESS_WORK_PLAN.md`, continuous build/test via
`scripts/loop-build-test.ps1`, 2-minute chat pulse.

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
| 1 | Core API + CPU reference backend | **Done** | 84 registered tests, all passing (81 in CPU-only build) |
| 2 | Vulkan discovery + diagnostics | **Done** | `hcsdoctor` reports real 680M caps/budget |
| 3 | Minimal Vulkan compute | **Done** | vecadd/scale/reduce/matmul verified vs CPU |
| 4 | HCSIR/HCS-PIR verifier + compiler path | **Done** | parser, verifier, CPU interpreter, GLSL lowering, generated SPIR-V executing on Vulkan; experimental program API |
| 5 | HCSFlow/HEvent/HCSGraph async | **Done** | async flows, HEvent, bounded linear HCSGraph; validation profile green; fence batches verified by stress tests |
| 6 | Math kernels + tuning | **Done** | reduce, matmul, scan, transpose verified on CPU + Vulkan; benchmark rows recorded; histogram/image primitives deliberately out of V1 scope |
| 7 | CUDA source migration (`hcsport`) | **Scope reduced** | scanner + one conformance-tested vecadd translation; PTX/CUBIN explicitly out of V1 (`docs/release/v1-scope.md`) |
| 8 | Model vault + GGUF (llama.cpp Vulkan) | **Scope reduced** | catalog/checksum/path safety tested; GGUF inference explicitly omitted from V1 (`docs/release/v1-scope.md`) |
| 9 | REST API + MCP server | **Done for V1 scope** | MCP stdio, local-only REST with token auth, OpenAPI 3.1 and settings endpoints; model/benchmark/translation endpoints out of V1 scope |
| 10 | Desktop UI | **Done for V1 scope** | local browser UI with live data + persisted settings; native Qt/WebView2 shell and screenshots explicitly out of V1 scope |
| 11 | Installer (one EXE) | **Done on reference machine** | native single EXE, silent per-user install, install/uninstall + same-version AND cross-version upgrade tested (INS-01..04); clean VM unverified |
| 12 | Stress/security/performance hardening | **Done for V1 scope** | stress/fuzz, 60-min soak, validation layers, secret scan and MSVC /analyze pass |
| 13 | Docs, CI, release candidate | **Partial** | OpenAPI, SBOM, scope doc, public repo pushed, hosted CI green; GitHub Release and signing pending |

## Implemented surface

* C ABI v1: init/shutdown, enumeration, `hcsGetDevice`, caps, memory budget, contexts,
  buffers, flows, kernels, dispatch, structured errors, version identity.
* CPU backend: scalar + AVX2/FMA with runtime CPUID gate, bounded thread use, deterministic
  clamping to the smallest bound.
* Vulkan backend: dynamic loader, capability probing (incl. subgroup size and memory budget),
  host-visible coherent buffers, compute pipelines, synchronous dispatch, device-loss mapping,
  and a bounded, device-fingerprinted on-disk `VkPipelineCache` configured via settings.
* Built-in kernels: `vecadd_f32`, `scale_f32`, `reduce_sum_f32`, `reduce_final_f32`,
  `matmul_f32`, `scan_f32`, `scan_add_offsets_f32` (global scan pipeline), `transpose_f32`,
  `histogram_u32` (256 bins, single workgroup).
* Tools: `hcsdoctor`, `hcsbench`, `hcscc`, `hcsport`, `hcsbridge-mcp`, `hcsbridge-rest`, native single-file setup; SPIR-V compiled at build time by `glslc` and embedded.
* HCSIR v1: bounded parser, verifier, CPU interpreter and versioned binary serialization
  with canonical round-trip and fuzz-tested deserialization for the elementwise subset
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
* Services: local-only REST with random bearer token, rate limit and settings persistence, MCP
  stdio with five read-only tools (including safe model-catalog metadata), self-contained browser
  UI with live data.
* Tests: 96 HCS unit/integration tests plus an automated live-UI capture test (`hcs_ui_capture`),
  covering API contract, CPU/Vulkan correctness, memory caps, HCSIR accept/reject/serialize,
  generated GLSL + GPU execution, flows/events/graphs, scan/transpose, CUDA scan/translation,
  catalog/checksums, REST/MCP/UI, settings, stress/fuzz and installer gates.

## Verification summary

Validation layers are wired into `windows-validation` and pass with zero errors. Hosted CI passed
all seven jobs on commit c5f3d4c. On 2026-11-28 all three local profiles were re-verified green:
`windows-tests` 96/96, `windows-cpu-only` 93/93, `windows-validation` 96/96 with zero validation
errors. A process-wide REST rate limit (120 requests / 10 s, HTTP 429) was added and tested
(REST-06). `ctest` presets now exist for all profiles. `hcsport --convert` was exercised end-to-end
(PORT-04) and a real UI screenshot was captured from the live REST service (UI-04/UI-05). A bounded,
device-fingerprinted on-disk `VkPipelineCache` is now implemented and tested (VK-06), configured
through the existing settings value without changing the stable C ABI. HCSIR binary serialization
remains available (IR-21..23) but is not used as a runtime module cache.

## Remaining external blockers

See `docs/KNOWN_LIMITATIONS.md` and `docs/release/v1-scope.md`. Only external items remain:
clean-VM testing, code signing (no certificate), a manually approved GitHub Release, and
cross-vendor hardware verification (only Radeon 680M + CPU tested). Cross-version upgrade is
now verified on the reference machine (INS-04).
