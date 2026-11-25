# ADR-0001: Language, build system and dependency policy

* Status: accepted
* Date: 2026-11-25

## Context

The runtime needs a stable C ABI, predictable performance, and a Windows-first build that
works on a clean developer machine. The reference machine has MSVC 2022, CMake 4.4, Ninja and
the Vulkan SDK 1.4.357 but no Qt and no Inno Setup.

## Decision

* C++20 for the runtime, compiled with MSVC x64 in RelWithDebInfo for testing and Release for
  distribution.
* CMake (>= 3.28) with presets: `windows-debug`, `windows-release`,
  `windows-relwithdebinfo`, `windows-cpu-only`, `windows-vulkan`, `windows-tests`.
* No third-party runtime library is linked in 0.1.0. Shaders are compiled at build time by
  `glslc` and embedded as byte arrays, so the runtime has no shader-compiler dependency.
* Vulkan is accessed through `vulkan-1.dll` loaded with `LoadLibraryW`; there is no import
  library requirement and no bundled loader.
* Tests use a dependency-free harness registered at static-init time. This avoids a network
  dependency in CI and keeps the test binary inspectable.

## Consequences

* Building the Vulkan backend requires the Vulkan SDK; without it CMake falls back to a
  CPU-only build with a warning.
* Adding a dependency later (VMA, llama.cpp, HTTP) requires a license review and an entry in
  `THIRD_PARTY_NOTICES.md` before the first commit that links it.
* MSVC-only for now. Clang/GCC support is unverified and must not be claimed.

## Alternatives considered

* Ninja + vcvars: faster incremental builds, but requires an initialized developer shell.
  The Visual Studio generator works from a plain terminal, which makes CI and agent runs
  reproducible. Ninja remains usable manually.
* Header-only Vulkan-Hpp: rejected for now to keep compile times and C ABI boundaries simple.
