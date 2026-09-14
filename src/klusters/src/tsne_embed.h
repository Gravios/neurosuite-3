#ifndef TSNE_EMBED_H
#define TSNE_EMBED_H

// Barnes-Hut t-SNE, 2-D output — the engine behind the feature view's
// alternate presentation.  Dependency-free (STL + OpenMP), deterministic for
// a fixed seed, cancellable, with O(N·k) memory so the spike cap in the view
// (default 32000) is a UI/latency budget rather than an OOM guard.
//
// Pipeline: per-dimension z-score → exact kNN (VP-tree, k = 3·perplexity) →
// per-point Gaussian bandwidth by bisection on the perplexity → symmetrised
// sparse P → gradient descent with early exaggeration, adaptive gains and
// momentum, Barnes-Hut quadtree for the repulsive term (theta).
//
// Verified standalone in-sandbox (see the commit message): well-separated
// Gaussian blobs embed with >99% kNN label purity, a shuffled-label negative
// control collapses to chance, identical seeds give bit-identical output,
// and cancellation aborts promptly.

#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct TsneParams {
    int      nIter      = 500;
    double   perplexity = 30.0;   // clamped internally to (N-1)/3
    double   theta      = 0.5;    // Barnes-Hut accuracy/speed trade-off
    unsigned seed       = 42;
    int      exagIter   = 250;    // early-exaggeration span
    double   exag       = 12.0;
    double   eta        = 200.0;  // learning rate
};

/** Embed N points of dimension D (row-major data, size N*D) into outXY
 *  (size N*2).
 *
 *  progress(phase, done, total) is called from the worker thread through every
 *  phase, not just the gradient loop.  That matters more than it sounds: at the
 *  spike cap the setup -- neighbour search and bandwidth fitting -- is well over
 *  half the wall time, and while it ran the engine said nothing at all, so a
 *  perfectly healthy run looked like a key that did nothing.  @p phase is a
 *  short stable tag ("neighbours", "bandwidths", "embedding") the caller can
 *  show verbatim.
 *
 *  *cancel is polled inside those phases too, not only between iterations, so
 *  aborting no longer waits for the neighbour search to finish; it returns
 *  false with *err = "cancelled".  Returns false on invalid input. */
bool tsneEmbed2D(const std::vector<double>& data, int N, int D,
                 std::vector<double>& outXY,
                 const TsneParams& params = TsneParams(),
                 const std::function<void(const char*, int, int)>& progress = {},
                 const std::atomic<bool>* cancel = nullptr,
                 std::string* err = nullptr);

#endif
