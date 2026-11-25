# HCSUDA

**HCS Unified Compute Runtime** — a Vulkan-based, cross-vendor GPU compute runtime with a
portable C ABI, a CPU reference backend, and an expanding CUDA-source migration path.

> **Status: 0.1.0 development build.** Wave 0–3 of the build plan are implemented and tested
> on real hardware (AMD Radeon 680M, Vulkan 1.4.315). There is no installer, desktop UI, AI
> model vault, or CUDA transpiler yet. Everything listed below is backed by code in this
> repository and by the test run recorded in `docs/TEST_MATRIX.md`.

HCSUDA is an independent project. It is **not** affiliated with, endorsed by, or sponsored by
NVIDIA Corporation. "CUDA" is a trademark of NVIDIA and appears here only to describe
migration tooling.

---

## What works today

| Capability | Status | Evidence |
|---|---|---|
| Runtime init/shutdown, stable C ABI (v1) | **Native / Stable** | `tests/unit/test_api.cpp` |
| Device enumeration: Vulkan + CPU | **Native / Stable** | `hcsdoctor` on Radeon 680M |
| Capability reporting (workgroups, subgroup, memory budget) | **Supported via Vulkan** | `hcsdoctor`, `vulkaninfo` cross-check |
| FP16 / INT8 / matrix claims | **Not claimed** unless the queried feature exists | capability bits |
| Buffer create/upload/download/clear | **Native / Stable** | unit + integration tests |
| Compute dispatch: vector add, scale, reduce-sum, FP32 matmul | **Native / Stable** | CPU + Vulkan tests |
| CPU backend (scalar + runtime AVX2/FMA) | **Native / Stable** | `cpu_*` tests |
| Memory cap and clean OOM error | **Native / Stable** | `vulkan_memory_cap_is_enforced` |
| HCSFlow (synchronous submit/wait in V1) | **Native / Beta** | dispatch tests |
| Diagnostics CLI (`hcsdoctor`) | **Native / Stable** | this repo |
| Benchmarks (`hcsbench`) | **Native / Stable** | `benchmarks/baselines/` |
| HCSIR v1 parser/verifier/interpreter (CPU only, internal) | **Experimental** | `ir_*` tests, `docs/api/hcsir.md` |
| CUDA source migration (`hcsport`) | **Planned (Wave 7)** | — |
| Model vault / GGUF inference | **Planned (Wave 8)** | — |
| REST API / MCP server | **Planned (Wave 9)** | — |
| Desktop UI | **Planned (Wave 10)** | — |
| One-file installer | **Planned (Wave 11)** | — |

## What HCSUDA is not

* Not a GPU kernel-mode driver. It never installs, replaces, or patches an AMD/Intel/NVIDIA
  display driver, `vulkan-1.dll`, or the Windows Driver Store.
* Not a universal CUDA emulator. NVIDIA CUBIN/SASS binaries cannot execute on AMD hardware.
* Not a drop-in `nvcuda.dll` replacement.
* Not a claim that every Vulkan GPU supports every precision or AI model.
* Not a claim that the GPU is always faster than the CPU.

## Requirements

* Windows 11 x64 (tested: Windows 11 Pro 10.0.26200).
* A vendor GPU driver with a Vulkan 1.0+ ICD for GPU execution. If Vulkan is unavailable the
  CPU backend still runs.
* Build only: Visual Studio 2022 Build Tools (MSVC 14.4x), CMake >= 3.28, Ninja optional,
  Vulkan SDK (headers + `glslc`). None of these are required on end-user machines.

## Build from source (verified)

```powershell
cmake --preset windows-tests
cmake --build --preset windows-tests --parallel
ctest --preset windows-tests --output-on-failure

# CPU-only build (no Vulkan SDK required)
cmake --preset windows-cpu-only
cmake --build --preset windows-cpu-only --parallel
```

Outputs land in `build/<preset>/RelWithDebInfo/`:

* `hcsdoctor.exe` — device, capability and memory-budget report (`--json` supported)
* `hcsbench.exe` — reproducible micro-benchmarks (`--json`, `--device`, `--reps`)
* `hcs_tests.exe` — unit/integration suite

## Quick start: run the tools

```powershell
build\windows-tests\RelWithDebInfo\hcsdoctor.exe
build\windows-tests\RelWithDebInfo\hcsbench.exe --device 0 --reps 25
```

Example output on the reference machine:

```
[0] AMD Radeon(TM) Graphics
    backend      : Vulkan
    kind         : Integrated GPU (unified memory)
    api/driver   : 1.4.315 / 0x00800161
    capabilities : base memory_budget subgroup
    workgroup    : 1024 invocations, max 1024x1024x1024, shared 32 KiB
    subgroup     : 64
    memory       : heap 9569.9 MiB, budget 9091.4 MiB, used 0.0 MiB
```

## Minimal C API example

```c
#include <hcsuda/hcsuda.h>

hcsInit(NULL);
hcsDevice device;
hcsGetDevice(0, &device);                 /* 0 = highest-capability device */

hcsContextDesc cdesc = { sizeof(cdesc), 0, 0 };
hcsContext ctx;
hcsCreateContext(device, &cdesc, &ctx);

hcsBufferDesc bdesc = { sizeof(bdesc), HCS_BUFFER_USAGE_STORAGE, 4096, 0 };
hcsBuffer a, b, c;
hcsBufferCreate(ctx, &bdesc, &a);
hcsBufferCreate(ctx, &bdesc, &b);
hcsBufferCreate(ctx, &bdesc, &c);

struct { uint32_t n; float alpha; uint32_t pad0, pad1; } pc = { 1024, 1.5f, 0, 0 };
hcsDispatchDesc d = { 0 };
d.struct_size = sizeof(d);
d.buffer_count = 3;
d.buffers[0] = a; d.buffers[1] = b; d.buffers[2] = c;
d.group_count_x = (1024 + 63) / 64;
d.group_count_y = 1; d.group_count_z = 1;
d.push_constants_size = sizeof(pc);
/* memcpy(&d.push_constants, &pc, sizeof(pc)); then dispatch */

hcsKernel kernel;
hcsKernelCreate(ctx, "vecadd_f32", &kernel);
hcsDispatch(kernel, &d, NULL);            /* synchronous in V1 */

hcsKernelDestroy(kernel);
hcsBufferDestroy(a); hcsBufferDestroy(b); hcsBufferDestroy(c);
hcsDestroyContext(ctx);
hcsShutdown();
```

Buffer upload/download are host-visible operations in V1. `group_count_x/y/z` must all be
`>= 1`; a zero count is rejected instead of silently doing nothing.

## Benchmarks (reference machine, real measurements)

Hardware: AMD Ryzen 7 7735HS, Radeon 680M (integrated, UMA), Windows 11 26200,
AMD driver 32.0.21043.12001, Vulkan 1.4.315, HCSUDA 0.1.0 RelWithDebInfo.

Method: warm-up 5, 25 repetitions, each sample is **one dispatch including submit + fence
wait** (V1 dispatches are synchronous). Median and p10/p90 reported. Raw JSON:
`benchmarks/baselines/radeon680m-2026-11-25.json`.

| Kernel | Size | Vulkan median | Vulkan GB/s | CPU median | CPU GB/s |
|---|---|---|---|---|---|
| vecadd_f32 | 1,048,576 | 0.271 ms | 46.5 | 1.287 ms | 9.8 |
| scale_f32 | 1,048,576 | 0.220 ms | 38.1 | 1.264 ms | 6.6 |
| reduce_sum_f32 | 1,048,576 (256 groups) | 0.150 ms | 28.0 | 1.173 ms | 3.6 |
| matmul_f32 | 256³ | 0.208 ms (161 GFLOP/s) | 3.8 | 1.918 ms (17.5 GFLOP/s) | 0.4 |

These are single-run medians on one machine, not SOTA claims. On UMA hardware "GB/s" refers
to shared system memory traffic. Buffers are host-visible in V1; a device-local + staging
path is planned.

## Memory model (UMA honesty)

The Radeon 680M shares system memory. HCSUDA queries `VkPhysicalDeviceMemoryProperties` and
`VK_EXT_memory_budget`, reports the real heap/budget/usage values the driver exposes, and
applies a conservative allocation cap (3/4 of the reported budget, capped at 6 GiB for the
default context). It never assumes that all installed RAM is available, and it never reports
a fabricated "VRAM" size.

## CUDA migration status

No automatic CUDA translation is shipped yet. `hcsport` is planned for Wave 7 and will emit a
report that classifies each construct as translated, manual port required, or unsupported.
CUBIN/SASS translation is explicitly out of scope on AMD hardware. See
`docs/compatibility/cuda-api-matrix.md` for the current, deliberately conservative matrix.

## Project documents

* `docs/AGENT_STATUS.md` — wave-by-wave status and next actions
* `docs/BUILD_LOG.md` — build commands and outcomes
* `docs/TEST_MATRIX.md` — test IDs, environment, results
* `docs/KNOWN_LIMITATIONS.md` — what does not work yet
* `docs/api/kernels.md` — built-in kernel contracts
* `docs/architecture/` — ADRs
* `THIRD_PARTY_NOTICES.md`, `SECURITY.md`, `CONTRIBUTING.md`

## License

MIT — see `LICENSE` and `NOTICE`.

**Made with love by HCSMedia & timfromhcs.**
