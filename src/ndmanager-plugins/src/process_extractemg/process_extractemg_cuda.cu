/***************************************************************************
    process_extractemg_cuda.cu
    --------------------------
    CUDA + cuBLAS implementation of the FastICA pow3 inner step.

    Numerical contract (identical to the CPU reference):
        y[t]    = ( Z @ w )[t]   = Σ_c Z[t,c] * w[c]      (length N)
        y[t]    = y[t]^3
        newW[c] = ( Z^T @ y )[c] / N   −   3 * w[c]       (length nch)

    Implementation
    --------------
      1. prepare()  uploads Z (N × nch row-major) into d_Z and allocates
                    d_w, d_newW, d_y.  Layout in device memory matches
                    the host: row-major, i.e. column-major nch×N when
                    viewed through cuBLAS (which is column-major
                    natively).  This is convenient: the same memory is
                    M = Z^T as a cuBLAS matrix of shape (nch, N) with
                    leading dimension nch.

         Step 1:  y = Z @ w  in row-major ≡  M^T @ w  in col-major,
                  which is cublasDgemv(CUBLAS_OP_T, ...).
         Step 3:  newW = Z^T @ y / N  − 3 * w   ≡  M @ y * 1/N + (-3) * w_in
                  via cublasDgemv(CUBLAS_OP_N, ..., alpha=1/N, beta=0)
                  followed by daxpy(-3, w, newW), or equivalently a
                  single dgemv with beta=-3·N/1 if newW is pre-loaded
                  with w.

      2. step()  uploads w (nch doubles), runs the three kernels, copies
                 newW back.  The host blocks on completion; the FastICA
                 outer loop is strictly serial through this interface.

      3. destroy()  frees device memory and the cuBLAS handle.

    Tiny problems (N·nch < ~1e5) skip GPU and request a CPU fallback by
    returning false from prepare(); the per-iter launch overhead would
    otherwise dominate.

    copyright  (C) 2026 neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifdef USE_CUDA

#include "process_extractemg_gpu.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>

#include <cstdio>
#include <cstring>

namespace {

// Kernel: y[t] = y[t]^3   (in-place)
__global__ void cube_kernel(double *y, int N)
{
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t < N) {
        const double v = y[t];
        y[t] = v * v * v;
    }
}

#define CUDA_CHECK(call) do {                                          \
        cudaError_t _e = (call);                                       \
        if (_e != cudaSuccess) {                                       \
            std::fprintf(stderr,                                       \
                "process_extractemg(cuda): %s -> %s\n",                \
                #call, cudaGetErrorString(_e));                        \
            return false;                                              \
        }                                                              \
    } while (0)

#define CUBLAS_CHECK(call) do {                                        \
        cublasStatus_t _s = (call);                                    \
        if (_s != CUBLAS_STATUS_SUCCESS) {                             \
            std::fprintf(stderr,                                       \
                "process_extractemg(cuda): %s -> cuBLAS status %d\n",  \
                #call, (int)_s);                                       \
            return false;                                              \
        }                                                              \
    } while (0)

// Below this size, the GPU launch overhead × thousands of FastICA
// iterations dominates.  Empirically ~30 µs per launch + transfers; for
// N*nch < 1e5 the CPU is faster.
constexpr long kGpuMinNxNch = 100'000;

class Pow3KernelCuda final : public Pow3Kernel {
public:
    ~Pow3KernelCuda() override { destroy(); }

    bool prepare(const std::vector<double> &Z, int nch, int N) override
    {
        // Decide whether to even bother with GPU for this size.
        if ((long)N * nch < kGpuMinNxNch) {
            std::fprintf(stderr,
                "process_extractemg(cuda): N*nch = %ld < %ld; falling back to CPU\n",
                (long)N * nch, kGpuMinNxNch);
            return false;
        }

        m_nch = nch;
        m_N   = N;

        if (!m_handle) {
            CUBLAS_CHECK(cublasCreate(&m_handle));
        }

        // Allocate device buffers
        const size_t bytesZ    = (size_t)N * nch * sizeof(double);
        const size_t bytesW    = (size_t)nch * sizeof(double);
        const size_t bytesY    = (size_t)N * sizeof(double);

        CUDA_CHECK(cudaMalloc(&d_Z,    bytesZ));
        CUDA_CHECK(cudaMalloc(&d_w,    bytesW));
        CUDA_CHECK(cudaMalloc(&d_newW, bytesW));
        CUDA_CHECK(cudaMalloc(&d_y,    bytesY));

        // One-time upload of Z
        CUDA_CHECK(cudaMemcpy(d_Z, Z.data(), bytesZ, cudaMemcpyHostToDevice));

        return true;
    }

    bool step(const double *w, double *newW) override
    {
        const int    nch  = m_nch;
        const int    N    = m_N;
        const double oneOverN = 1.0 / (double)N;
        const double zero     = 0.0;
        const double minus3   = -3.0;

        // Upload w (length nch)
        CUDA_CHECK(cudaMemcpy(d_w, w, (size_t)nch * sizeof(double),
                              cudaMemcpyHostToDevice));

        // Step 1: y = Z @ w
        //
        // d_Z is row-major (N × nch).  Viewed as cuBLAS column-major
        // it is a matrix M of shape (nch, N) with leading dimension nch.
        // The desired y = Z @ w corresponds to M^T @ w.
        //
        //     cublasDgemv(handle, CUBLAS_OP_T, nch, N,
        //                 &alpha, d_Z, nch, d_w, 1, &beta, d_y, 1)
        {
            const double alpha = 1.0;
            CUBLAS_CHECK(cublasDgemv(m_handle, CUBLAS_OP_T,
                                     nch, N, &alpha,
                                     d_Z, nch, d_w, 1,
                                     &zero, d_y, 1));
        }

        // Step 2: y = y^3
        {
            const int blockSz = 256;
            const int gridSz  = (N + blockSz - 1) / blockSz;
            cube_kernel<<<gridSz, blockSz>>>(d_y, N);
            CUDA_CHECK(cudaGetLastError());
        }

        // Step 3: newW = Z^T @ y / N − 3 * w
        //
        // Z^T @ y in row-major  ≡  M @ y in col-major (M is nch×N).
        //     cublasDgemv(handle, CUBLAS_OP_N, nch, N,
        //                 &alpha=1/N, d_Z, nch, d_y, 1, &beta=0, d_newW, 1)
        // then daxpy: newW += -3 * w
        {
            CUBLAS_CHECK(cublasDgemv(m_handle, CUBLAS_OP_N,
                                     nch, N, &oneOverN,
                                     d_Z, nch, d_y, 1,
                                     &zero, d_newW, 1));
            CUBLAS_CHECK(cublasDaxpy(m_handle, nch,
                                     &minus3, d_w, 1, d_newW, 1));
        }

        // Download newW (length nch)
        CUDA_CHECK(cudaMemcpy(newW, d_newW, (size_t)nch * sizeof(double),
                              cudaMemcpyDeviceToHost));

        // Block until everything completed (so the next host operation sees
        // the result and any error path surfaces here, not later).
        CUDA_CHECK(cudaDeviceSynchronize());
        return true;
    }

    void destroy() override
    {
        if (d_Z)    { cudaFree(d_Z);    d_Z    = nullptr; }
        if (d_w)    { cudaFree(d_w);    d_w    = nullptr; }
        if (d_newW) { cudaFree(d_newW); d_newW = nullptr; }
        if (d_y)    { cudaFree(d_y);    d_y    = nullptr; }
        if (m_handle) {
            cublasDestroy(m_handle);
            m_handle = nullptr;
        }
    }

    IcaBackend backend() const override { return IcaBackend::Cuda; }

private:
    cublasHandle_t m_handle = nullptr;
    double *d_Z    = nullptr;
    double *d_w    = nullptr;
    double *d_newW = nullptr;
    double *d_y    = nullptr;
    int     m_nch  = 0;
    int     m_N    = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// Public factory + runtime probe
// ---------------------------------------------------------------------------

std::unique_ptr<Pow3Kernel> makePow3KernelCuda()
{
    return std::unique_ptr<Pow3Kernel>(new Pow3KernelCuda());
}

bool gpuCudaRuntimeAvailable()
{
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) {
        // Clear the sticky error so subsequent runtime calls don't see it.
        (void)cudaGetLastError();
        return false;
    }
    return count > 0;
}

#endif // USE_CUDA
