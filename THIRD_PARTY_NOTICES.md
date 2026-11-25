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

## Runtime (redistributed with the application)

HCSUDA itself loads, but does not redistribute, the following system components:

* `vulkan-1.dll` — Khronos Vulkan Loader, supplied by the GPU vendor or the
  Windows Vulkan runtime. HCSUDA never ships or replaces this file.
* The vendor Vulkan ICD (AMD/Intel/NVIDIA) installed by the GPU driver.

No NVIDIA CUDA headers, libraries, binaries, or compiler components are
bundled with this project.

## Planned dependencies (not yet integrated)

These are evaluated but not currently linked. When integrated, their license
texts will be added here and to the installer's license page.

| Component | Planned usage | License (to verify) |
|---|---|---|
| Vulkan Memory Allocator (VMA) | Allocation pooling | MIT |
| llama.cpp (Vulkan backend) | GGUF inference adapter | MIT |
| Qt 6 Quick | Desktop UI (alternative under evaluation) | LGPLv3 / commercial |
| WebView2 runtime | Desktop UI host (system component) | Microsoft distribution terms |

**Status:** no runtime third-party binary is currently distributed with the
`0.1.0` development build. The build produces HCSUDA-owned executables plus
embedded SPIR-V compiled from HCSUDA-owned GLSL sources.
