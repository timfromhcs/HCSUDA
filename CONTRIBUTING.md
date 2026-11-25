# Contributing

Thanks for helping build HCSUDA. This project values correctness and honest compatibility
reporting over feature count.

## Ground rules

1. **No false compatibility claims.** Never describe HCSUDA as a CUDA replacement, a driver,
   or as executing CUBIN/SASS on non-NVIDIA hardware.
2. **No capability assumptions.** Query Vulkan features at runtime; add a fallback or report
   the feature as unsupported.
3. **Tests before claims.** A feature is "supported" only when a test exercises it on a real
   backend and the test is part of `ctest`.
4. **No secrets or model weights in Git.**
5. **Small commits.** Conventional Commit messages (`feat:`, `fix:`, `docs:`, `test:`).

## Building

```powershell
cmake --preset windows-tests
cmake --build --preset windows-tests --parallel
ctest --preset windows-tests --output-on-failure
```

CPU-only configuration (no Vulkan SDK):

```powershell
cmake --preset windows-cpu-only
cmake --build --preset windows-cpu-only --parallel
```

## Adding a kernel

1. Add the GLSL source in `shaders/hksl/<name>.comp`.
2. Register it in `CMakeLists.txt` (`HCS_SHADERS`), in `src/backends/vulkan/generated_shaders.h`,
   in `hcs::KernelId`, and in the kernel table in `src/core/runtime.cpp`.
3. Implement the CPU reference in `src/backends/cpu/cpu_backend.cpp`.
4. Document the push-constant layout in `docs/api/kernels.md`.
5. Add a CPU test and a Vulkan test that compare against a double-precision reference with an
   explicit tolerance, including an odd, non-multiple-of-workgroup size.
6. Run `ctest`, then update `docs/TEST_MATRIX.md`.

## Adding a dependency

Open an ADR, verify the license permits redistribution, pin the version, add it to
`THIRD_PARTY_NOTICES.md`, and keep it out of the runtime path unless absolutely required.

## Code style

* C++20, 4 spaces, 100-column soft limit, `namespace hcs` for internals.
* No exceptions across the C ABI. Every public function returns `hcsResult`.
* Every failure sets structured context via `hcs::fail(...)`.
* RAII for OS and Vulkan handles.

## Reporting bugs

Include `hcsdoctor --json`, `vulkaninfo --summary`, driver version, and the failing command.
Do not include model weights or personal file paths.
