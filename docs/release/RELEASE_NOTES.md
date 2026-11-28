# HCSUDA 0.1.0-rc.3 — release notes

These notes describe the current `main` state at tagging time: local single-file installer plus an
expanded public WebGPU/WASM browser demo covering all seven built-in kernel shapes with
configurable inputs, JSON/CSV export, measured visualisations and benchmarks. These are
release-candidate notes, not a claim that every V1 gate from `AGENTS.md` has passed. Earlier
`v0.1.0-rc.1` and `v0.1.0-rc.2` tags remain available.

**Local artifact:** `HCSUDA-Setup-x64.exe`, unsigned, SHA-256
`1893992BC683CAF633E7E706D3514BCFFF8150EDCEF70B79D5F0CE26399047D2`.
**Cloud artifact:** <https://hcsuda-webgpu-demo.vercel.app> (standalone demo, not the native runtime).

## What is included

* **Runtime:** C ABI v1; device discovery/capability reporting; host-visible buffers with a
  conservative memory cap; synchronous and asynchronous dispatch; HEvent flow markers;
  bounded HCSGraph replay.
* **Backends:** Vulkan (dynamic `vulkan-1.dll` load, no bundled loader/driver) and CPU
  (scalar + runtime AVX2/FMA), with explicit backend reporting.
* **Kernels:** vecadd, scale, two-stage reduce, global scan pipeline (local scan → chunk totals →
  offsets → add offsets), 16x16 transpose, FP32 matmul and a 256-bin histogram, each compared
  against a CPU reference on odd and boundary sizes.
* **Compiler path:** HCSIR v1 verifier/interpreter, HCSCC GLSL lowering, build-time `glslc`
  validation, and GPU execution of generated SPIR-V via an experimental program API.
* **Migration:** `hcsport --scan` plus a conformance-tested vecadd source translation subset.
  Unsupported constructs fail closed.
* **Models:** local catalog with SHA-256, path allowlist and format inspection — **storage only**;
  every model is reported `runnable: false`.
* **Local services:** REST on `127.0.0.1` with random bearer token, bounded bodies and rate limit;
  MCP stdio with five read-only tools (including safe model-catalog metadata); self-contained
  browser UI with live data and persisted settings.
* **Packaging:** unsigned single-file native EXE, silent per-user install/uninstall, SHA-256.

## Evidence

* 96/96 unit tests on the reference machine; 93/93 in CPU-only builds (three GPU-only
  tests are correctly excluded); zero Vulkan validation errors; hosted CI all seven jobs green.
* 60-minute soak: 1418 iterations, zero failed runs and zero unverified benchmark rows on the
  current build (an earlier run reached 1610 iterations). Performance drift was observed in the
  earlier run without a diagnosed cause, so no performance claim is made.
* Install, uninstall, same-version in-place upgrade and cross-version upgrade with two real EXEs
  tested on the reference machine (INS-01..04); system Vulkan loader unchanged.
* Secret scan passed over all tracked files (125); OpenAPI 3.1 contract gate passed; every
  benchmark row is verified against a host reference.

## Explicitly not part of this RC

GGUF inference adapter, PTX importer, Qt/WebView2 shell, Streamable HTTP MCP, histogram/image
kernels, timeline semaphores, multi-GPU, CUDA library ABI compatibility, code signing and a
GitHub Release. See `docs/release/v1-scope.md`.

## Remaining verification before a final `v0.1.0` tag

Clean-VM install/upgrade, code signing with a publisher certificate, cross-vendor GPU hardware,
and a manually approved GitHub Release. These are tracked in `docs/AGENT_STATUS.md`.
