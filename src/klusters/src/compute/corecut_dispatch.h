/***************************************************************************
 * corecut_dispatch.h
 *
 * C++ public face of the core-cut GPU dispatcher.  KlustersDoc::detectCoreCut
 * hands corecut::analyse CoreCutGpu::order as its ordering hook.
 ***************************************************************************/

#pragma once

namespace CoreCutGpu {

/** True iff a GPU backend reported a usable device on its first probe.  Cached. */
bool hasGpu();

/** Below this spike count the dispatcher returns failure: the CPU loop is
 *  done before the device has its copy of the spikes. */
long gpuThreshold();

/** The ordering loop on the active GPU backend (see corecut_gpu.h).  Returns
 *  true on success; false means the caller runs the CPU loop. */
bool order(long n, int d, const float* x, long seed, long* order, double* step);

} // namespace CoreCutGpu
