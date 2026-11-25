# CUDA compatibility matrix (conservative, as implemented)

This matrix is the contract for what HCSUDA may claim. Anything not implemented is marked
unsupported and must stay unsupported in user-facing copy until a test exists.

Status vocabulary: Native / Supported via Vulkan / Experimental / Unsupported / Not planned
for V1.

| CUDA area | HCSUDA status today | Notes |
|---|---|---|
| Device enumeration | **Native (HCS API)** | `hcsEnumerateDevices`; not ABI-compatible with `cudaGetDeviceProperties` |
| Compute capability reporting | **Not emulated** | HCSUDA reports capability bits and workgroup limits; it never fabricates an NVIDIA compute capability |
| Memory allocation/free | **Native (HCS API)** | `hcsBufferCreate`/`Destroy`; no `cudaMalloc` symbol compatibility |
| Host/device copies | **Native (HCS API)** | `hcsBufferUpload`/`Download`; host-visible buffers in v1 |
| Kernel launch (`<<<>>>`) | **Unsupported** | No source compiler yet; Wave 7 adds a subset translator |
| CUDA C++ source (`.cu`) | **Unsupported (planned subset)** | `hcsport` not implemented |
| Textual PTX | **Unsupported** | Textual subset importer planned as experimental |
| CUBIN / SASS | **Unsupported, permanent** | NVIDIA GPU machine code cannot execute on AMD hardware |
| CUDA fatbin | **Unsupported** | No extractor implemented |
| Streams | **Partial (HCSFlow, synchronous)** | Single ordered flow per context; no overlap |
| Events | **Unsupported** | `HEvent` planned in Wave 5 |
| CUDA Graphs | **Unsupported** | HCSGraph planned after Wave 5 |
| Unified/Managed Memory | **Not ABI-compatible** | UMA-aware HCS memory model instead; no `cudaMallocManaged` |
| Atomics | **Unsupported** | No atomic kernels; capability bits gated but unused |
| `__syncthreads()` / workgroup barriers | **Supported in shaders** | Barrier exists inside built-in GLSL kernels only |
| Cooperative Groups | **Unsupported** | Subgroup size is queried but no cooperative-group API exists |
| Dynamic parallelism | **Unsupported** | Not planned for V1 |
| Tensor Cores / WMMA / MMA | **Unsupported** | No matrix instructions claimed; no FP16 tensor path |
| cuBLAS | **Unsupported** | Built-in matmul kernel only, not an API replacement |
| cuDNN / TensorRT | **Unsupported** | Not planned for V1 ABI compatibility |
| NCCL / NVSHMEM / GPUDirect | **Unsupported** | Not planned for V1 |
| `nvcuda.dll` replacement | **Never** | HCSUDA does not install or spoof NVIDIA system libraries |
| NVML / GPU identity spoofing | **Never** | Would cause incorrect library/kernel decisions |

## Migration reporting rules (Wave 7 acceptance)

`hcsport` reports must classify each construct as one of:

* **Translated** — supported and covered by a conformance test.
* **Host code, unchanged** — valid C++ outside the device kernel.
* **Manual port required** — expressible in HCS but not automatically translatable.
* **Unsupported** — no correctness-preserving mapping; must fail closed.

The tool must never emit "best effort" code that silently changes semantics. Unsupported
constructs are a hard error with a source location.

## Evidence required before any status changes

1. A conformance test comparing CPU reference output with Vulkan output.
2. A negative test proving unsupported constructs fail with a clear diagnostic.
3. The README and this matrix updated in the same commit.

No CUDA library may be described as compatible based on a single example program.
