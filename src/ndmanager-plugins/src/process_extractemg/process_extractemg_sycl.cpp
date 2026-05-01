/***************************************************************************
    process_extractemg_sycl.cpp
    ---------------------------
    SYCL + oneMKL implementation of the FastICA pow3 inner step.

    Mirror of process_extractemg_cuda.cu but routed through oneMKL's
    column-major BLAS (oneapi::mkl::blas::column_major::gemv) and a
    parallel_for for the elementwise cube.  oneMKL is column-major like
    cuBLAS, so the same M = Z^T-as-col-major trick works.

    Built only when USE_SYCL is defined; the surrounding ifdef makes the
    file safe to include in non-SYCL builds.

    copyright  (C) 2026 neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifdef USE_SYCL

#include "process_extractemg_gpu.h"

#include <sycl/sycl.hpp>
#include <oneapi/mkl/blas.hpp>

#include <cstdio>
#include <cstring>

namespace {

constexpr long kGpuMinNxNch = 100'000;

class Pow3KernelSycl final : public Pow3Kernel {
public:
    ~Pow3KernelSycl() override { destroy(); }

    bool prepare(const std::vector<double> &Z, int nch, int N) override
    {
        if ((long)N * nch < kGpuMinNxNch) {
            std::fprintf(stderr,
                "process_extractemg(sycl): N*nch = %ld < %ld; falling back to CPU\n",
                (long)N * nch, kGpuMinNxNch);
            return false;
        }
        m_nch = nch;
        m_N   = N;

        try {
            m_q = sycl::queue(sycl::gpu_selector_v);
        } catch (const sycl::exception &e) {
            std::fprintf(stderr,
                "process_extractemg(sycl): no SYCL GPU device found (%s)\n",
                e.what());
            return false;
        }

        d_Z    = sycl::malloc_device<double>((size_t)N * nch, m_q);
        d_w    = sycl::malloc_device<double>((size_t)nch,     m_q);
        d_newW = sycl::malloc_device<double>((size_t)nch,     m_q);
        d_y    = sycl::malloc_device<double>((size_t)N,       m_q);
        if (!d_Z || !d_w || !d_newW || !d_y) {
            std::fprintf(stderr,
                "process_extractemg(sycl): device malloc failed\n");
            destroy();
            return false;
        }

        m_q.memcpy(d_Z, Z.data(), (size_t)N * nch * sizeof(double)).wait();
        return true;
    }

    bool step(const double *w, double *newW) override
    {
        const int    nch      = m_nch;
        const int    N        = m_N;
        const double oneOverN = 1.0 / (double)N;
        const double zero     = 0.0;
        const double minus3   = -3.0;

        try {
            m_q.memcpy(d_w, w, (size_t)nch * sizeof(double)).wait();

            // y = Z @ w  ≡  M^T @ w (column-major M = nch×N view of d_Z)
            oneapi::mkl::blas::column_major::gemv(
                m_q, oneapi::mkl::transpose::trans,
                nch, N, 1.0,
                d_Z, nch,
                d_w, 1,
                zero,
                d_y, 1).wait();

            // y = y^3
            {
                double *y = d_y;
                m_q.parallel_for(sycl::range<1>((size_t)N),
                                 [=](sycl::id<1> i) {
                    const double v = y[i];
                    y[i] = v * v * v;
                }).wait();
            }

            // newW = Z^T @ y / N
            oneapi::mkl::blas::column_major::gemv(
                m_q, oneapi::mkl::transpose::nontrans,
                nch, N, oneOverN,
                d_Z, nch,
                d_y, 1,
                zero,
                d_newW, 1).wait();

            // newW += -3 * w
            oneapi::mkl::blas::column_major::axpy(
                m_q, nch, minus3, d_w, 1, d_newW, 1).wait();

            m_q.memcpy(newW, d_newW, (size_t)nch * sizeof(double)).wait();
        } catch (const sycl::exception &e) {
            std::fprintf(stderr,
                "process_extractemg(sycl): SYCL exception: %s\n", e.what());
            return false;
        }
        return true;
    }

    void destroy() override
    {
        if (d_Z)    { sycl::free(d_Z,    m_q); d_Z    = nullptr; }
        if (d_w)    { sycl::free(d_w,    m_q); d_w    = nullptr; }
        if (d_newW) { sycl::free(d_newW, m_q); d_newW = nullptr; }
        if (d_y)    { sycl::free(d_y,    m_q); d_y    = nullptr; }
    }

    IcaBackend backend() const override { return IcaBackend::Sycl; }

private:
    sycl::queue m_q;
    double *d_Z    = nullptr;
    double *d_w    = nullptr;
    double *d_newW = nullptr;
    double *d_y    = nullptr;
    int     m_nch  = 0;
    int     m_N    = 0;
};

} // namespace

std::unique_ptr<Pow3Kernel> makePow3KernelSycl()
{
    return std::unique_ptr<Pow3Kernel>(new Pow3KernelSycl());
}

bool gpuSyclRuntimeAvailable()
{
    try {
        sycl::queue q(sycl::gpu_selector_v);
        (void)q;
        return true;
    } catch (...) {
        return false;
    }
}

#endif // USE_SYCL
