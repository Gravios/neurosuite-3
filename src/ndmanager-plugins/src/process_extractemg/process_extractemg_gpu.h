/***************************************************************************
    process_extractemg_gpu.h
    ------------------------
    Backend-agnostic interface to the GPU-accelerated FastICA pow3 inner
    kernel.  Mirrors the KlustaKwik backend pattern (CUDA primary, HIP
    and SYCL stubs maintained, priority CUDA > HIP > SYCL > CPU).

    Pow3 kernel role
    ----------------
    The fpica.m deflation/pow3 fixed-point loop has only one heavy inner
    operation per iteration:

        y[t]    = ( Z @ w )[t]   = Σ_c Z[t,c] * w[c]      (length-N gemv)
        y[t]    = y[t]^3                                  (elementwise)
        newW[c] = ( Z^T @ y )[c] / N   -   3 * w[c]       (length-nch gemv + axpby)

    Everything else (random init, ortho-against-B, normalisation,
    convergence test) is O(nch²) and stays on the host.  The GPU is
    therefore responsible for exactly the three steps above; the FastICA
    outer loop is identical in CPU and GPU code paths.

    Lifetime
    --------
    A Pow3Kernel is created with a backend selection, has Z uploaded
    once via prepare(), then step() is called per fixed-point iteration.
    destroy() releases device memory.  One kernel object is reused for
    all K independent components within a single FastICA call.

    copyright  (C) 2026 neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Backend identifier for runtime / CLI selection.
enum class IcaBackend {
    Auto = 0,   // CUDA > HIP > SYCL > CPU
    Cpu,
    Cuda,
    Hip,
    Sycl,
};

// Parse / format a backend name, mirroring CLI strings.
//   "auto" / "cpu" / "cuda" / "hip" / "sycl"
// Returns IcaBackend::Cpu on unknown input (with err set).
IcaBackend     icaBackendFromString(const std::string &s, std::string *err = nullptr);
const char    *icaBackendName(IcaBackend b);

// Compile-time + runtime availability.  An "available" backend was
// compiled in AND its device runtime is reachable (CUDA driver loaded,
// HIP runtime loaded, SYCL device discovered).
bool icaBackendAvailable(IcaBackend b);

// Resolve Auto → first available among CUDA, HIP, SYCL, falling back to
// Cpu.  Pass-through for explicit backends.
IcaBackend icaPickBackend(IcaBackend requested);

// ---------------------------------------------------------------------------
// Pow3Kernel — abstract base for one fixed-point pow3 update step.
// ---------------------------------------------------------------------------
//
// All implementations honour the same numerical contract:
//     newW[c] = ( Σ_t Z[t,c] * (Σ_c' Z[t,c']*w[c'])^3 ) / N  -  3 * w[c]
//
// where Z is N × nch row-major (sample-major) and w, newW are nch-vectors.
//
// The CPU implementation is the OpenMP-parallelised fused sweep used by
// the reference fastIcaDeflationPow3.  GPU implementations upload Z once,
// run the kernel + cuBLAS / rocBLAS / oneMKL gemvs per call, and cycle w
// and newW through pinned host buffers.
//
// step() is host-blocking on completion (the FastICA outer loop is
// strictly sequential through it) so callers do not need to manage
// streams, events, or device synchronisation.
//
// All implementations return false (and leave outputs untouched) on
// internal device errors; the caller may then fall back to a different
// backend.  Errors are reported to stderr.
class Pow3Kernel {
public:
    virtual ~Pow3Kernel() = default;
    virtual bool prepare(const std::vector<double> &Z, int nch, int N) = 0;
    virtual bool step(const double *w, double *newW) = 0;
    virtual void destroy() = 0;
    virtual IcaBackend backend() const = 0;
};

// Factory.  Returns a CPU kernel if the requested backend is
// unavailable; never returns nullptr.
std::unique_ptr<Pow3Kernel> makePow3Kernel(IcaBackend backend);
