/***************************************************************************
 * corecut_gpu.h
 *
 * Common C interface a GPU backend implements for the ordering loop of the
 * core cut (corecut.h, step 4): the spikes of one cluster put in the order in
 * which each next one changes the cumulative mean least.
 *
 * The loop is sequential in its steps and wide inside each: step k takes the
 * spike j found by step k-1, folds it into every spike's running product
 * x_i . m (one dot product of d values per spike), and finds the nearest
 * spike left.  A backend keeps the spikes, the products and the taken flags
 * on the device and returns one index per step.
 *
 * Array layouts (host pointers; a backend does its own copies)
 *   x      [n x d]  row-major, x[i * d + u]: the centred, windowed spikes
 *   order  [n]      output: order[0] == seed, then the spikes as taken
 *   step   [n]      output: |x - m| at which each was taken, step[0] == 0
 *
 * A backend must take the same spikes the CPU loop (corecut::detail::orderCpu)
 * takes: float products, double running sums, ties to the lower index.
 * Rounding in the d-term float product can still differ between the two, so a
 * near-tie may be taken in the other order; the cut that follows is a
 * statistic over hundreds of ranks and does not turn on it.
 ***************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the backend has a usable device, 0 otherwise. */
int cuda_corecut_available();

/* Returns 0 on success, non-zero on failure (the caller runs the CPU loop). */
int cuda_corecut_order(long n, int d, const float* x, long seed, long* order, double* step);

#ifdef __cplusplus
}
#endif
