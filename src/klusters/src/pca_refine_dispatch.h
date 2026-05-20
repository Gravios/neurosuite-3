/***************************************************************************
 * pca_refine_dispatch.h
 *
 * C++ public face of the PCA-refine GPU dispatcher.  klustersdoc.cpp talks
 * to the dispatcher through this namespace.
 ***************************************************************************/

#pragma once

#include <stdint.h>

namespace PcaRefineGpu {

/** True iff at least one GPU backend reported a usable device on its
 *  first probe.  Cached. */
bool hasGpu();

/** Below this spike count the dispatcher always returns failure (forces
 *  the existing CPU per-candidate loop).  At small K the launch + H↔D
 *  overhead dominates over the compute saved. */
int gpuThreshold();

/** Run the PCA-projection-energy refine pass on the active GPU backend.
 *  See pca_refine_gpu.h for parameter semantics.  Returns 0 on success;
 *  any non-zero return means the caller should run the CPU fallback. */
int refine(int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
           int kComp, int d2u, int rShift, int maxShift,
           int centered, int useStder,
           const int16_t* rawWindowsCM,
           const float*   pcaEvec,
           const float*   pcaMeans,
           int*           bestShifts);

} // namespace PcaRefineGpu
