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

#include <algorithm>
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
std::unique_ptr<Pow3Kernel> makePow3KernelAccelerate();

bool gpuCudaRuntimeAvailable();
bool gpuHipRuntimeAvailable();
bool gpuSyclRuntimeAvailable();

// ---------------------------------------------------------------------------
// Pow3KernelCpu — pow3StepCpu on its own copy of Z (the reference kernel
// the pow3 test checks).
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
        pow3StepCpu(m_Z.data(), m_nch, m_N, w, newW, m_partials);
        return true;
    }

    void destroy() override
    {
        m_Z.clear(); m_Z.shrink_to_fit();
        m_partials.clear(); m_partials.shrink_to_fit();
    }
    IcaBackend backend() const override { return IcaBackend::Cpu; }

private:
    std::vector<double> m_Z;
    std::vector<double> m_partials;
    int                 m_nch = 0;
    int                 m_N   = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// pow3StepCpu — fixed blocks, block sums added in order (see the header).
// ---------------------------------------------------------------------------
void pow3StepCpu(const double *Z, int nch, int N, const double *w, double *newW,
                 std::vector<double> &partials)
{
    const int nBlocks = (N + kPow3BlockRows - 1) / kPow3BlockRows;
    partials.resize((size_t)nBlocks * nch);
    double *part = partials.data();

    #pragma omp parallel for schedule(static)
    for (int b = 0; b < nBlocks; ++b) {
        double *pb = part + (size_t)b * nch;
        for (int c = 0; c < nch; ++c) pb[c] = 0.0;
        const int t0 = b * kPow3BlockRows;
        const int t1 = std::min(N, t0 + kPow3BlockRows);
        for (int t = t0; t < t1; ++t) {
            const double *zrow = Z + (size_t)t * nch;
            double s = 0.0;
            for (int c = 0; c < nch; ++c) s += zrow[c] * w[c];
            const double yt = s * s * s;
            for (int c = 0; c < nch; ++c) pb[c] += zrow[c] * yt;
        }
    }

    for (int c = 0; c < nch; ++c) newW[c] = 0.0;
    for (int b = 0; b < nBlocks; ++b) {
        const double *pb = part + (size_t)b * nch;
        for (int c = 0; c < nch; ++c) newW[c] += pb[c];
    }
    const double invN = 1.0 / (double)N;
    for (int c = 0; c < nch; ++c) newW[c] = newW[c] * invN - 3.0 * w[c];
}

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
        case IcaBackend::Accelerate: return "accelerate";
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
    if (s == "accelerate") return IcaBackend::Accelerate;
    if (err) *err = "unknown backend '" + s + "'; expected auto|cpu|cuda|hip|sycl|accelerate";
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
        case IcaBackend::Accelerate:
#ifdef USE_ACCELERATE
            return true;
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
    // Not Accelerate: the OpenMP kernel reads Z once per step and may be as
    // fast; Auto takes Accelerate only once a Mac benchmark shows it ahead.
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
        case IcaBackend::Accelerate:
#ifdef USE_ACCELERATE
            k = makePow3KernelAccelerate();
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
#ifndef USE_ACCELERATE
std::unique_ptr<Pow3Kernel> makePow3KernelAccelerate() { return {}; }
#endif
