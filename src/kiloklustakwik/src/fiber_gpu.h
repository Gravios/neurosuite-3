/***************************************************************************
 * fiber_gpu.h
 *
 * GPU backend interface for the standalone fiber clusterer.  Mirrors the
 * realign_xcorr / KK_cuda pattern: a C-linkage API with a CUDA backend
 * (fiber_gpu_cuda.cu); HIP/SYCL backends drop in behind the same symbols.
 * The caller probes fiber_gpu_available() and falls back to the CPU path
 * (fiberstage::meanshift_inband) on any non-zero return.
 *
 * All pointers are HOST arrays; the backend copies to device internally.
 * GPU calls are made only under #ifdef USE_CUDA, so when no GPU toolkit is
 * built these symbols are never referenced and need no definition.
 ***************************************************************************/
#ifndef FIBER_GPU_H
#define FIBER_GPU_H

// Largest feature dimension the kernel supports (per-thread local acc[]);
// above this the wrapper returns non-zero and the caller uses the CPU path.
#define FIBER_GPU_MAXP 256

#ifdef __cplusplus
extern "C" {
#endif

// 1 if a usable CUDA device is present, else 0.
int fiber_gpu_available(void);

// In-band directional mean-shift; numerically matches fiberstage::meanshift_inband.
//   dsup[nsup*p], rsup[nsup] : fixed support (directions, radii)
//   ds[S*p], rs[S]           : seed directions/radii, IN (init) and OUT (converged)
// Returns 0 on success, non-zero on failure (caller falls back to CPU).
int fiber_gpu_meanshift(const double* dsup, const double* rsup, int nsup, int p,
                        double* ds, double* rs, int S,
                        double kappa, double dr, int iters);

#ifdef __cplusplus
}
#endif
#endif // FIBER_GPU_H
