# Built-in kernel contracts (v1)

All kernels are compiled from `shaders/hksl/*.comp` at build time and embedded as SPIR-V.
The CPU backend implements the same semantics in `src/backends/cpu/cpu_backend.cpp`.
Push constants are copied verbatim into a 128-byte block; the layouts below are the
authoritative contract.

A dispatch description must satisfy:

* `group_count_x >= 1`, `group_count_y >= 1`, `group_count_z >= 1`
* `buffer_count` matches the kernel's required arity
* every buffer belongs to the kernel's context
* all buffers are host-visible in v1 (create without special flags is sufficient)

## vecadd_f32

`c[i] = a[i] + alpha * b[i]`

| Item | Value |
|---|---|
| Buffers | 0: `a` (read), 1: `b` (read), 2: `c` (write) |
| Local size | 64 |
| Groups | `ceil(n / 64)` |

Push constants:

| Offset | Type | Name |
|---|---|---|
| 0 | `u32` | `n` element count |
| 4 | `f32` | `alpha` |
| 8 | `u32` | padding |
| 12 | `u32` | padding |

## scale_f32

`y[i] = alpha * x[i]`

| Item | Value |
|---|---|
| Buffers | 0: `x` (read), 1: `y` (write) |
| Local size | 64 |
| Groups | `ceil(n / 64)` |

Push constants: offset 0 `u32 n`, offset 4 `f32 alpha`, 8/12 padding.

## reduce_sum_f32

Per-workgroup partial sums:

`out[g] = sum(x[g*chunk .. min(n, (g+1)*chunk)))`, `chunk = ceil(n / groups)`

| Item | Value |
|---|---|
| Buffers | 0: `x` (read), 1: `out` (write, at least `groups` floats) |
| Local size | 256 |
| Groups | `groups` (also the `group_count_x`) |

Push constants: offset 0 `u32 n`, offset 4 `u32 groups`, 8/12 padding.

The final reduction can be done on the host or on the device with `reduce_final_f32` below.
Keeping the two stages separate makes the partial result deterministic and testable.

## matmul_f32

`C = A * B`, row-major, `A` is `m x k`, `B` is `k x n`, `C` is `m x n`.

| Item | Value |
|---|---|
| Buffers | 0: `A`, 1: `B`, 2: `C` |
| Local size | 16 x 16 |
| Groups | `x = ceil(n/16)`, `y = ceil(m/16)`, `z = 1` |

Push constants: offset 0 `u32 m`, 4 `u32 n`, 8 `u32 k`, 12 padding.

## scan_f32

Exclusive prefix sum inside each contiguous chunk. `chunk = ceil(n / groups)`; chunk `g`
covers `[g*chunk, min(n, (g+1)*chunk))`. Chunks longer than one workgroup are processed in
256-element blocks with a running prefix, so arbitrary `n` and `groups` are supported. The caller
must add the chunk totals for a global scan (see `scan_add_offsets_f32`).

| Item | Value |
|---|---|
| Buffers | 0: input `x` (read), 1: output `y` (write) |
| Local size | 256 |
| Groups | `group_count_x = groups` |

Push constants: offset 0 `u32 n`, offset 4 `u32 groups`, 8/12 padding.

## transpose_f32

Row-major transpose: `B[col,row] = A[row,col]`, with `A` sized `rows x cols` and `B` sized
`cols x rows`. Uses a 16x16 shared-memory tile.

| Item | Value |
|---|---|
| Buffers | 0: `A` (read), 1: `B` (write) |
| Local size | 16 x 16 |
| Groups | `x = ceil(cols/16)`, `y = ceil(rows/16)`, `z = 1` |

Push constants: offset 0 `u32 rows`, offset 4 `u32 cols`, 8/12 padding.

## reduce_final_f32

Single-workgroup final reduction: `out[0] = sum(partials[0 .. n-1])`.

| Item | Value |
|---|---|
| Buffers | 0: `partials` (read), 1: `out` (write, one float) |
| Local size | 256 |
| Groups | **exactly 1** |

Push constants: offset 0 `u32 n`, 4/8/12 padding.

## scan_add_offsets_f32

Final stage of the global exclusive scan:
`result[i] = local_scan[i] + offsets[group]`.

| Item | Value |
|---|---|
| Buffers | 0: `local_scan` (read), 1: `offsets` (read), 2: `result` (write) |
| Local size | 256 |
| Groups | `groups` (the original chunk count) |

Push constants: offset 0 `u32 n`, offset 4 `u32 groups`, 8/12 padding. The caller first runs
`scan_f32` on the input, `reduce_sum_f32` for chunk totals and `scan_f32` on those totals.

## histogram_u32

256-bin histogram: `hist[value & 255]++` for each u32 input element.

| Item | Value |
|---|---|
| Buffers | 0: `values` (read), 1: `hist` (write, 256 u32 bins) |
| Local size | 256 |
| Groups | **exactly 1** (bin reset must not race with atomic updates) |

Push constants: offset 0 `u32 n`, 4/8/12 padding. More than one group is rejected with
`HCS_E_INVALID_ARGUMENT` on the Vulkan backend.

## Numerical expectations

* FP32 accumulation; comparisons against the CPU backend use relative tolerance 1e-4 to
  1e-5 depending on reduction order.
* No fast-math is enabled for the shaders.
* The CPU reference accumulates reductions in `double` for stability, then narrows to `f32`.
  This is why the tolerance in reduction tests is asymmetric.

## Error behaviour

| Condition | Result |
|---|---|
| Unknown kernel name | `HCS_E_KERNEL_NOT_FOUND` |
| Wrong buffer count | `HCS_E_INVALID_ARGUMENT` |
| Buffer from another context | `HCS_E_INVALID_ARGUMENT` |
| Group count zero | `HCS_E_INVALID_ARGUMENT` |
| Push constants > 128 bytes | `HCS_E_INVALID_ARGUMENT` |
| Allocation above the context cap | `HCS_E_OUT_OF_MEMORY` |
| `VK_ERROR_DEVICE_LOST` | `HCS_E_DEVICE_LOST` |
