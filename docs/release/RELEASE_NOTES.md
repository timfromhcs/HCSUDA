# HCSUDA 0.1.0 release candidate — notes

These notes describe the actual state of commit `a5ca96c` and later CI-verified commits. They
are release-candidate notes, not a claim that every V1 gate from `AGENTS.md` has passed.

## What is included

* **Runtime:** C ABI v1; device discovery/capability reporting; host-visible buffers with a
  conservative memory cap; synchronous and asynchronous dispatch; HEvent flow markers;
  bounded HCSGraph replay.
* **Backends:** Vulkan (dynamic `vulkan-1.dll` load, no bundled loader/driver) and CPU
  (scalar + runtime AVX2/FMA), with explicit backend reporting.
* **Kernels:** vecadd, scale, reduce-sum, chunked scan, 16x16 transpose and FP32 matmul, each
  compared against a CPU reference on odd and boundary sizes.
* **Compiler path:** HCSIR v1 verifier/interpreter, HCSCC GLSL lowering, build-time `glslc`
  validation, and GPU execution of generated SPIR-V via an experimental program API.
* **Migration:** `hcsport --scan` plus a conformance-tested vecadd source translation subset.
  Unsupported constructs fail closed.
* **Models:** local catalog with SHA-256, path allowlist and format inspection — **storage only**;
  every model is reported `runnable: false`.
* **Local services:** REST on `127.0.0.1` with random bearer token and bounded bodies; MCP stdio
  with four read-only tools; self-contained browser UI with live data and persisted settings.
* **Packaging:** unsigned single-file native EXE, silent per-user install/uninstall, SHA-256.

## Evidence

* 85/85 unit tests on the reference machine; 82/82 in CPU-only builds (three GPU-only
  tests are correctly excluded); zero Vulkan validation errors; hosted CI all seven jobs green.
* 60-minute soak: 1610 iterations, zero failures. Performance drift was observed and is
  documented without a performance claim.
* Install, uninstall, same-version in-place upgrade and cross-version upgrade with two real EXEs
  tested on the reference machine (INS-01..04); system Vulkan loader unchanged.
* Secret scan passed over all tracked files.

## Explicitly not part of this RC

GGUF inference adapter, PTX importer, Qt/WebView2 shell, Streamable HTTP MCP, histogram/image
kernels, timeline semaphores, multi-GPU, CUDA library ABI compatibility, code signing and a
GitHub Release. See `docs/release/v1-scope.md`.

## Remaining verification before a final `v0.1.0` tag

Clean-VM install/upgrade, code signing with a publisher certificate, cross-vendor GPU hardware,
and a manually approved GitHub Release. These are tracked in `docs/AGENT_STATUS.md`.
