# V1 scope decisions (release candidate)

The build contract allows reducing declared scope when a feature cannot be completed safely or
verified. These decisions are explicit so that no README or UI claim exceeds the code.

## In V1 (implemented and tested)

* C ABI v1: init/shutdown, device enumeration, caps, memory budget, buffers, flows, events,
  graphs, kernels, dispatch, structured errors.
* CPU backend (scalar + runtime AVX2/FMA) and Vulkan backend (dynamic loader, capability probing,
  host-visible buffers, compute pipelines, async flows with fence fallback).
* Built-in kernels: vecadd, scale, reduce-sum, chunked scan, transpose, FP32 matmul.
* HCSIR v1 verifier/interpreter, HCSCC GLSL lowering, and GPU execution of generated SPIR-V via
  an experimental program API.
* Narrow CUDA source scan plus one conformance-tested vecadd translation; all other constructs
  fail closed.
* Model catalog with SHA-256, path allowlist and format inspection (storage only).
* Local-only REST API with token auth and a self-contained browser UI; MCP stdio server with
  four read-only tools.
* Native single-file EXE installer with silent per-user install/uninstall.

## Explicitly out of V1 scope (not "coming soon" claims)

* **GGUF inference through llama.cpp.** No adapter is integrated, no model was downloaded, and no
  token-generation benchmark exists. The ModelVault stores and hashes files but reports every
  model as `runnable: false`. Any AI feature claim is removed from V1 copy.
* **Textual PTX importer.** `hcsport` handles a narrow CUDA source subset only. PTX text, CUBIN
  and SASS are unsupported; there is no whitelist to publish because no importer exists.
* **Qt/WebView2 native desktop shell.** The V1 UI is a local browser page served by REST. No Qt
  license review or WebView2 COM host is required for V1.
* **Streamable HTTP MCP and remote API binding.** stdio MCP and 127.0.0.1 REST only.
* **Histogram, image transforms, timeline semaphores, device-local staging, multi-GPU, dynamic
  parallelism, CUDA library ABI compatibility.**
* **Full UI settings pages.** Dashboard, Devices, Memory and Compatibility exist; other
  navigation sections are not advertised as working.

## Release still requires (honest open items)

* Clean-VM install/upgrade test and optional code signing (no certificate configured).
* A manually approved GitHub Release/tag. Repository publication and secret scan are already
  done (`docs/release/publication.md`); only the release approval is outstanding.
* Any cross-vendor claim beyond the tested AMD Radeon 680M + CPU backend.

These are listed as open, not silently treated as passed. The current build is a tested
development/release-candidate runtime, not a fully certified product.
