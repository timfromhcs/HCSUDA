# HCSPORT — supported translation subset (Wave 7)

`hcsport --convert` translates **exactly one** CUDA C++ kernel shape. Everything else fails closed
with `HCS_E_TRANSLATION_UNSUPPORTED` and no output file is written. This is a migration aid for
source the user has rights to, not a CUDA compiler.

## Accepted source shape

```cpp
__global__ void name(float* a, float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { c[i] = a[i] + b[i]; }
}

/* or, with a scalar factor: */
__global__ void name(float* a, float* b, float* c, float alpha, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) { c[i] = alpha * b[i] + a[i]; }
}
```

Rules:

* Exactly one `__global__ void` function is translated; the first one found is used.
* Exactly three `float*` parameters and one integer count parameter are required. A `float`
  scalar parameter is optional and becomes an HCSIR `param`; otherwise a constant `1.0` is used.
* The canonical 1-D index expression must appear verbatim after whitespace removal.
* The assignment must be exactly `c[i] = a[i] + b[i];` or `c[i] = alpha * b[i] + a[i];`.
* `if (i < n)` is documented but **not translated**: the caller must dispatch exactly
  `ceil(n/64)` workgroups so buffer bounds checks provide the effect.
* Atomics, `cuda*` API calls, `__shared__`, `__syncthreads`, loops, `malloc`/`new`, cuBLAS and
  cuDNN are rejected.

## Output

* `--output file.hcsir` receives an HCSIR v1 module. It can be verified with the unit tests,
  executed by the CPU interpreter, or compiled by `hcscc` and loaded on Vulkan.
* `--report file.json` receives a classification summary: `translated: 1`, plus the counts of
  manual and unsupported host constructs found by the compatibility scanner.
* A warning on stderr states that manual review is still required.

## Evidence

`tests/unit/test_translate.cpp`: the checked-in MIT fixture is translated, the emitted HCSIR is
compiled and executed against a CPU reference over 1001 elements; the FMA variant emits `fma`;
an `atomicAdd` kernel fails closed with `HCS_E_TRANSLATION_UNSUPPORTED`. CLI check:

```
hcsport --convert tests/fixtures/cuda/vecadd.cu --output out.hcsir --report report.json
```

## Not implemented

* Multiple kernels per file, arbitrary CUDA C++, PTX text, CUBIN/SASS, CUDA library APIs.
* Inference of grid geometry from `if (i < n)`; calling code must be ported manually.
* Differential diff output against the original source (planned with the conformance corpus).
