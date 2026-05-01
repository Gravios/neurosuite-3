/***************************************************************************
    process_extractemg_gpu_cpu.cpp
    ------------------------------
    CPU implementation of the FastICA pow3 inner-step kernel, plus the
    backend factory and string-parsing helpers shared across backends.

    Always built — provides the universal CPU fallback when no GPU
    toolkit is selected at configure time, and serves as the reference
    implementation against which CUDA/HIP/SYCL backends are validated.

    copyright  (C) 2026 neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "process_extractemg_gpu.h"

#include <cstdio>
#include <cstring>

#ifdef _OPENMP
#  include <omp.h>
#endif

// Forward declarations of the optional GPU factories (real ones live in
// _cuda.cu / _hip.cpp / _sycl.cpp; weak-link CPU stubs below are used
// when those translation units are not part of the build).
std::unique_ptr<Pow3Kernel> makePow3KernelCuda();
std::unique_ptr<Pow3Kernel> makePow3KernelHip();
std::unique_ptr<Pow3Kernel> makePow3KernelSycl();

bool gpuCudaRuntimeAvailable();
bool gpuHipRuntimeAvailable();
bool gpuSyclRuntimeAvailable();

// ---------------------------------------------------------------------------
// Pow3KernelCpu — OpenMP-parallel reduction with thread-local buffers.
// ---------------------------------------------------------------------------
namespace {

class Pow3KernelCpu final : public Pow3Kernel {
public:
    bool prepare(const std::vector<double> &Z, int nch, int N) override
    {
        m_Z.assign(Z.begin(), Z.end());
        m_nch = nch;
        m_N   = N;
        return true;
    }

    bool step(const double *w, double *newW) override
    {
        const int nch = m_nch;
        const int N   = m_N;
        const double *Z = m_Z.data();

        for (int c = 0; c < nch; ++c) newW[c] = 0.0;

        #pragma omp parallel
        {
            std::vector<double> localW((size_t)nch, 0.0);
            #pragma omp for nowait schedule(static)
            for (int t = 0; t < N; ++t) {
                const double *zrow = Z + (size_t)t * nch;
                double s = 0.0;
                for (int c = 0; c < nch; ++c) s += zrow[c] * w[c];
                const double yt = s * s * s;
                for (int c = 0; c < nch; ++c) localW[(size_t)c] += zrow[c] * yt;
            }
            #pragma omp critical
            for (int c = 0; c < nch; ++c) newW[c] += localW[(size_t)c];
        }

        const double invN = 1.0 / (double)N;
        for (int c = 0; c < nch; ++c) newW[c] = newW[c] * invN - 3.0 * w[c];
        return true;
    }

    void destroy() override { m_Z.clear(); m_Z.shrink_to_fit(); }
    IcaBackend backend() const override { return IcaBackend::Cpu; }

private:
    std::vector<double> m_Z;
    int                 m_nch = 0;
    int                 m_N   = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// Backend availability + parsing
// ---------------------------------------------------------------------------

const char *icaBackendName(IcaBackend b)
{
    switch (b) {
        case IcaBackend::Auto: return "auto";
        case IcaBackend::Cpu:  return "cpu";
        case IcaBackend::Cuda: return "cuda";
        case IcaBackend::Hip:  return "hip";
        case IcaBackend::Sycl: return "sycl";
    }
    return "?";
}

IcaBackend icaBackendFromString(const std::string &s, std::string *err)
{
    if (s == "auto") return IcaBackend::Auto;
    if (s == "cpu")  return IcaBackend::Cpu;
    if (s == "cuda" || s == "nvidia")        return IcaBackend::Cuda;
    if (s == "hip"  || s == "rocm" || s == "amd") return IcaBackend::Hip;
    if (s == "sycl" || s == "intel" || s == "oneapi") return IcaBackend::Sycl;
    if (err) *err = "unknown backend '" + s + "'; expected auto|cpu|cuda|hip|sycl";
    return IcaBackend::Cpu;
}

bool icaBackendAvailable(IcaBackend b)
{
    switch (b) {
        case IcaBackend::Auto: return true;
        case IcaBackend::Cpu:  return true;
        case IcaBackend::Cuda:
#ifdef USE_CUDA
            return gpuCudaRuntimeAvailable();
#else
            return false;
#endif
        case IcaBackend::Hip:
#ifdef USE_HIP
            return gpuHipRuntimeAvailable();
#else
            return false;
#endif
        case IcaBackend::Sycl:
#ifdef USE_SYCL
            return gpuSyclRuntimeAvailable();
#else
            return false;
#endif
    }
    return false;
}

IcaBackend icaPickBackend(IcaBackend requested)
{
    if (requested != IcaBackend::Auto) {
        if (icaBackendAvailable(requested)) return requested;
        std::fprintf(stderr,
            "process_extractemg: backend '%s' unavailable, falling back to CPU\n",
            icaBackendName(requested));
        return IcaBackend::Cpu;
    }
    if (icaBackendAvailable(IcaBackend::Cuda)) return IcaBackend::Cuda;
    if (icaBackendAvailable(IcaBackend::Hip))  return IcaBackend::Hip;
    if (icaBackendAvailable(IcaBackend::Sycl)) return IcaBackend::Sycl;
    return IcaBackend::Cpu;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<Pow3Kernel> makePow3Kernel(IcaBackend backend)
{
    const IcaBackend chosen = icaPickBackend(backend);
    std::unique_ptr<Pow3Kernel> k;

    switch (chosen) {
        case IcaBackend::Cuda:
#ifdef USE_CUDA
            k = makePow3KernelCuda();
            if (k) return k;
#endif
            break;
        case IcaBackend::Hip:
#ifdef USE_HIP
            k = makePow3KernelHip();
            if (k) return k;
#endif
            break;
        case IcaBackend::Sycl:
#ifdef USE_SYCL
            k = makePow3KernelSycl();
            if (k) return k;
#endif
            break;
        default: break;
    }
    return std::unique_ptr<Pow3Kernel>(new Pow3KernelCpu());
}

// ---------------------------------------------------------------------------
// Weak-link stubs for the per-backend symbols.  Real implementations,
// when present, override these by linking first; this prevents undefined
// references when a backend is compiled out.
// ---------------------------------------------------------------------------
#ifndef USE_CUDA
std::unique_ptr<Pow3Kernel> makePow3KernelCuda() { return {}; }
bool gpuCudaRuntimeAvailable() { return false; }
#endif
#ifndef USE_HIP
std::unique_ptr<Pow3Kernel> makePow3KernelHip()  { return {}; }
bool gpuHipRuntimeAvailable()  { return false; }
#endif
#ifndef USE_SYCL
std::unique_ptr<Pow3Kernel> makePow3KernelSycl() { return {}; }
bool gpuSyclRuntimeAvailable() { return false; }
#endif
