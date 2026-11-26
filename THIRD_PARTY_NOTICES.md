# Third-party notices

HCSUDA links against or builds with the following components. Each entry lists
the component, the usage, and the redistributed form. This file is updated
whenever a dependency is added.

## Build-time only (not redistributed)

| Component | Version used | License | Usage |
|---|---|---|---|
| Vulkan SDK / Vulkan-Headers | 1.4.357 | Apache-2.0 | API headers for compilation only |
| glslc (Vulkan SDK) | 1.4.357 | Apache-2.0 | Compiles GLSL compute shaders to SPIR-V during the build |
| CMake | >= 3.28 | BSD-3-Clause | Build system |
| Ninja | 1.13.x | Apache-2.0 | Build tool |
| MSVC (Visual Studio Build Tools) | 14.4x | Microsoft license | Compiler and runtime |
| Python 3 | system-installed | PSF License | Generates the embedded installer payload (`installer/embed_payload.py`); not shipped |
| Windows PowerShell | system-provided | Microsoft license | Runs the installer/uninstaller scripts; not shipped |

## Runtime (redistributed with the application)

HCSUDA itself loads, but does not redistribute, the following system components:

* `vulkan-1.dll` — Khronos Vulkan Loader, supplied by the GPU vendor or the
  Windows Vulkan runtime. HCSUDA never ships or replaces this file.
* The vendor Vulkan ICD (AMD/Intel/NVIDIA) installed by the GPU driver.

No NVIDIA CUDA headers, libraries, binaries, or compiler components are
bundled with this project.

## Evaluated but not integrated (not V1 commitments)

These components were evaluated and are **not** linked, bundled or promised for V1. If one is
integrated in a future version, its exact version, license text and redistribution terms must be
added here before that change is merged.

| Component | Why it was not integrated | License (to verify if ever used) |
|---|---|---|
| Vulkan Memory Allocator (VMA) | explicit cap + per-buffer allocations are tested; pooling deferred | MIT |
| llama.cpp (Vulkan backend) | GGUF inference explicitly omitted from V1 claims | MIT |
| Qt 6 Quick | licensing/redistribution review would be required; local browser UI used instead | LGPLv3 / commercial |
| WebView2 runtime | native COM host rejected; UI served locally over REST | Microsoft distribution terms |

**Status:** no runtime third-party binary is currently distributed with the
`0.1.0` development build. The build produces HCSUDA-owned executables plus
embedded SPIR-V compiled from HCSUDA-owned GLSL sources.

## Components evaluated but NOT shipped

IExpress and 7-Zip SFX were evaluated while building the single-file installer. Both were
rejected (modal prompt / elevation / silent-execute failure on the reference machine) and are
**not** part of the build or the distributed installer. The final installer is a HCSUDA-owned
native executable with an embedded generated payload.
