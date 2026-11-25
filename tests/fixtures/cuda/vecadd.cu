/*
 * Hand-written test fixture, MIT licensed as part of HCSUDA.
 * Used only to exercise the hcsport compatibility scanner; never bundled as a model or binary.
 */
#include <cuda_runtime.h>

__global__ void vec_add_f32(const float* a, const float* b, float* c, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        c[i] = a[i] + b[i];
    }
    __syncthreads();
}

int launch(float* a, float* b, float* c, int n)
{
    float *da = nullptr, *db = nullptr, *dc = nullptr;
    cudaMalloc(&da, static_cast<size_t>(n) * sizeof(float));
    cudaMalloc(&db, static_cast<size_t>(n) * sizeof(float));
    cudaMalloc(&dc, static_cast<size_t>(n) * sizeof(float));
    vec_add_f32<<<4, 256>>>(da, db, dc, n);
    cudaDeviceSynchronize();
    atomicAdd(&dc[0], 1.0f);
    cudaFree(da);
    cudaFree(db);
    cudaFree(dc);
    return 0;
}
