/***************************************************************************
 * shiftprobe_disabled.cpp
 *
 * No-op stand-ins for the GPU shift-probe entry points
 * (gpu_timeshift_init / gpu_timeshift_free / gpu_timeshift_project_batch).
 *
 * Background
 * ----------
 * The time-shift probe was an experimental Phase-1.5 refinement step that
 * fanned a (2N+1)-candidate PCA basis over each spike and re-projected to
 * find the alignment that minimised within-cluster Mahalanobis distance.
 * In practice it did not improve sort quality reliably and ndm_alignspikes
 * (run as a pre-pass over .spk before clustering) produced better results
 * with a fraction of the runtime cost.
 *
 * The probe has been disabled at the call site (KlustaKwik.cpp forces
 * MaxTimeShift=0 and TimeShiftAlignIter=0).  The implementation in KK.cpp
 * is now dead code — guarded by the m_timeShiftReady flag, which is set
 * only by InitTimeShift(), which is in turn gated by MaxTimeShift>0.
 *
 * The original GPU back-end files
 *
 *     shiftprobe_cuda.cu
 *     shiftprobe_hip.cpp
 *     shiftprobe_sycl.cpp
 *
 * are retained on disk for reference but are no longer compiled (see
 * src/klustakwikExp/CMakeLists.txt).  The KK.cpp call sites still emit
 * extern declarations for the three GPU entry points, so the linker
 * needs them to resolve to *something* — these stubs do the job, and
 * are unconditionally safe because the call sites are also runtime-
 * gated by `m_timeShiftGpuCtx != nullptr`, which can never be set
 * non-null while MaxTimeShift==0.
 *
 * Re-enabling the probe (for future experimentation) requires:
 *   1. Remove the MaxTimeShift / TimeShiftAlignIter overrides in
 *      KlustaKwik.cpp.
 *   2. Re-add shiftprobe_<backend>.{cu,cpp} to the appropriate target
 *      in CMakeLists.txt (and drop this file from the build).
 *
 * Copyright (C) 2026  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "KK.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The whole TU is conditional on a GPU backend being selected.  In the
// CPU-only build, KK.h does not declare the m_timeShiftGpuCtx member nor
// the TimeShiftGpuCtx forward declaration, and KK.cpp does not reference
// gpu_timeshift_{init,free,project_batch} — so these stubs are neither
// available to define nor needed.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)

// The KK::TimeShiftGpuCtx struct is forward-declared in KK.h and defined
// per-backend in shiftprobe_{cuda,hip,sycl}.{cu,cpp}.  None of those files
// participate in the build any more, so we provide a minimal definition
// here.  It is never instantiated at runtime (gpu_timeshift_init below
// returns nullptr), but a complete type is needed for the pointer member
// KK::m_timeShiftGpuCtx that lives on KK.
struct KK::TimeShiftGpuCtx {
    int dummy = 0;
};

// ---------------------------------------------------------------------------
// gpu_timeshift_init — disabled stub
// Returns nullptr so KK::InitTimeShift() falls back to the CPU path.  In
// the current configuration InitTimeShift is itself never called (gated
// by MaxTimeShift>0 in KlustaKwik.cpp), so this stub is unreachable in
// practice; it exists only to satisfy the linker.
// ---------------------------------------------------------------------------
struct KK_GPU; // forward decl (full definition in KK_<backend>.h)
KK::TimeShiftGpuCtx* gpu_timeshift_init(
    KK_GPU* /*base*/, const KK::TimeShiftBasis& /*basis*/,
    int /*nChan*/, int /*nSamplesPerSpike*/, int /*nPoints*/,
    const char* /*spkPath*/)
{
    return nullptr;
}

// ---------------------------------------------------------------------------
// gpu_timeshift_free — disabled stub
// Accepts a nullptr (which is what gpu_timeshift_init returns), in which
// case there is nothing to free.  Defensive: also accepts a non-null
// pointer (e.g. if a future caller resurrected the probe and passed a
// host-allocated stand-in) and frees it.
// ---------------------------------------------------------------------------
void gpu_timeshift_free(KK::TimeShiftGpuCtx* ctx)
{
    delete ctx;  // safe when ctx == nullptr
}

// ---------------------------------------------------------------------------
// gpu_timeshift_project_batch — disabled stub
// Returns false so the call site in KK.cpp (KK::TimeShiftSplit etc.)
// falls through to the CPU projection path.  Like gpu_timeshift_init
// above, this is unreachable in the current configuration because the
// call site is guarded by m_timeShiftGpuCtx != nullptr.
// ---------------------------------------------------------------------------
bool gpu_timeshift_project_batch(
    KK::TimeShiftGpuCtx* /*ctx*/,
    const std::vector<int>& /*globalSpikeIndices*/,
    const std::vector<int>& /*cumShift*/,
    int /*maxShiftAbs*/,
    const std::vector<float>& /*dimMin*/,
    const std::vector<float>& /*dimRange*/,
    float* /*trialFeatsOut*/, float* /*trialTimeOut*/,
    const float* /*timeCol*/, int /*nDims*/, float /*sessionSamples*/)
{
    return false;
}

#endif // USE_CUDA || USE_SYCL || USE_HIP
