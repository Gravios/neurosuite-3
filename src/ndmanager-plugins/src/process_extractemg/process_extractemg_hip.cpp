/***************************************************************************
    process_extractemg_hip.cpp
    --------------------------
    HIP + rocBLAS implementation of the FastICA pow3 inner step.

    Mirror of process_extractemg_cuda.cu line-for-line; only the runtime
    type names and library calls change (cuda* → hip*, cublas* →
    rocblas*).  See the CUDA file for algorithmic / layout commentary.

    rocBLAS is column-major just like cuBLAS, so the same M = Z^T trick
    works without modification.

    copyright  (C) 2026 neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifdef USE_HIP

#include "process_extractemg_gpu.h"

#include <hip/hip_runtime.h>
#include <rocblas/rocblas.h>

#include <cstdio>
#include <cstring>

namespace {

__global__ void cube_kernel(double *y, int N)
{
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t < N) {
        const double v = y[t];
        y[t] = v * v * v;
    }
}

#define HIP_CHECK(call) do {                                           \
        hipError_t _e = (call);                                        \
        if (_e != hipSuccess) {                                        \
            std::fprintf(stderr,                                       \
                "process_extractemg(hip): %s -> %s\n",                 \
                #call, hipGetErrorString(_e));                         \
            return false;                                              \
        }                                                              \
    } while (0)

#define ROCBLAS_CHECK(call) do {                                       \
        rocblas_status _s = (call);                                    \
        if (_s != rocblas_status_success) {                            \
            std::fprintf(stderr,                                       \
                "process_extractemg(hip): %s -> rocBLAS status %d\n",  \
                #call, (int)_s);                                       \
            return false;                                              \
        }                                                              \
    } while (0)

constexpr long kGpuMinNxNch = 100'000;

class Pow3KernelHip final : public Pow3Kernel {
public:
    ~Pow3KernelHip() override { destroy(); }

    bool prepare(const std::vector<double> &Z, int nch, int N) override
    {
        if ((long)N * nch < kGpuMinNxNch) {
            std::fprintf(stderr,
                "process_extractemg(hip): N*nch = %ld < %ld; falling back to CPU\n",
                (long)N * nch, kGpuMinNxNch);
            return false;
        }
        m_nch = nch;
        m_N   = N;

        if (!m_handle) {
            ROCBLAS_CHECK(rocblas_create_handle(&m_handle));
        }

        const size_t bytesZ = (size_t)N * nch * sizeof(double);
        const size_t bytesW = (size_t)nch * sizeof(double);
        const size_t bytesY = (size_t)N * sizeof(double);

        HIP_CHECK(hipMalloc(&d_Z,    bytesZ));
        HIP_CHECK(hipMalloc(&d_w,    bytesW));
        HIP_CHECK(hipMalloc(&d_newW, bytesW));
        HIP_CHECK(hipMalloc(&d_y,    bytesY));

        HIP_CHECK(hipMemcpy(d_Z, Z.data(), bytesZ, hipMemcpyHostToDevice));
        return true;
    }

    bool step(const double *w, double *newW) override
    {
        const int    nch      = m_nch;
        const int    N        = m_N;
        const double oneOverN = 1.0 / (double)N;
        const double zero     = 0.0;
        const double minus3   = -3.0;

        HIP_CHECK(hipMemcpy(d_w, w, (size_t)nch * sizeof(double),
                            hipMemcpyHostToDevice));

        // y = Z @ w  ≡  M^T @ w (cuBLAS-style, M = nch×N col-major view of Z)
        {
            const double alpha = 1.0;
            ROCBLAS_CHECK(rocblas_dgemv(m_handle, rocblas_operation_transpose,
                                        nch, N, &alpha,
                                        d_Z, nch, d_w, 1,
                                        &zero, d_y, 1));
        }

        // y = y^3
        {
            const int blockSz = 256;
            const int gridSz  = (N + blockSz - 1) / blockSz;
            hipLaunchKernelGGL(cube_kernel,
                               dim3(gridSz), dim3(blockSz), 0, 0,
                               d_y, N);
            HIP_CHECK(hipGetLastError());
        }

        // newW = Z^T @ y / N − 3 * w
        {
            ROCBLAS_CHECK(rocblas_dgemv(m_handle, rocblas_operation_none,
                                        nch, N, &oneOverN,
                                        d_Z, nch, d_y, 1,
                                        &zero, d_newW, 1));
            ROCBLAS_CHECK(rocblas_daxpy(m_handle, nch,
                                        &minus3, d_w, 1, d_newW, 1));
        }

        HIP_CHECK(hipMemcpy(newW, d_newW, (size_t)nch * sizeof(double),
                            hipMemcpyDeviceToHost));
        HIP_CHECK(hipDeviceSynchronize());
        return true;
    }

    void destroy() override
    {
        if (d_Z)    { hipFree(d_Z);    d_Z    = nullptr; }
        if (d_w)    { hipFree(d_w);    d_w    = nullptr; }
        if (d_newW) { hipFree(d_newW); d_newW = nullptr; }
        if (d_y)    { hipFree(d_y);    d_y    = nullptr; }
        if (m_handle) {
            rocblas_destroy_handle(m_handle);
            m_handle = nullptr;
        }
    }

    IcaBackend backend() const override { return IcaBackend::Hip; }

private:
    rocblas_handle m_handle = nullptr;
    double *d_Z    = nullptr;
    double *d_w    = nullptr;
    double *d_newW = nullptr;
    double *d_y    = nullptr;
    int     m_nch  = 0;
    int     m_N    = 0;
};

} // namespace

std::unique_ptr<Pow3Kernel> makePow3KernelHip()
{
    return std::unique_ptr<Pow3Kernel>(new Pow3KernelHip());
}

bool gpuHipRuntimeAvailable()
{
    int count = 0;
    if (hipGetDeviceCount(&count) != hipSuccess) {
        (void)hipGetLastError();
        return false;
    }
    return count > 0;
}

#endif // USE_HIP
