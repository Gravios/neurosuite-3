// =============================================================================
// proxy_isi.cpp  —  implementation of cluster-free proxy-ISI computation
//
// See proxy_isi.h for algorithm rationale and interface contract.
//
// Implementation notes
// --------------------
// The search is naturally bounded by the causal time window: we never
// look back further than `causalWindowSec` seconds in time-sorted order.
// At typical multi-unit firing rates (200–500 Hz aggregate) this gives a
// candidate set of order 10³ spikes per query, well within reach of a
// brute-force O(cand · d) inner loop.
//
// For each query spike i:
//   - candStart = the smallest j with t_j ≥ t_i − causalWindowSec
//   - candidates = j in [candStart, i)
//   - compute squared distances d²_ij in feature space
//   - partial-sort the K smallest into a small heap
//   - epsilon = K-th smallest distance (the local feature scale)
//   - filter to candidates with d²_ij ≤ (epsilonFactor · sqrt(epsilon))²
//   - select most-recent (strict) or weighted-average (weighted) over the
//     filtered set
//
// candStart is found by binary search on times[].  Since times are
// monotone and we iterate i upward, we could also maintain a sliding
// candStart pointer; we chose binary search for clarity (cost is O(log
// nPoints) per query, negligible relative to the distance computations).
// =============================================================================
#include "proxy_isi.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace proxy_isi {

namespace {

// Squared Euclidean distance between two feature vectors of length nDims.
// Returns +∞ if either vector contains a NaN (so the candidate is filtered
// out downstream by any finite ε-gate).
static inline double sqDistance(const float* a, const float* b, int nDims) {
    double acc = 0.0;
    for (int d = 0; d < nDims; ++d) {
        const double diff = static_cast<double>(a[d]) - static_cast<double>(b[d]);
        if (!std::isfinite(diff)) {
            return std::numeric_limits<double>::infinity();
        }
        acc += diff * diff;
    }
    return acc;
}

// Binary search: index of first element ≥ target in sorted times[lo, hi).
// Returns hi if no such element exists.
static inline int lowerBound(const double* times, int lo, int hi, double target) {
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (times[mid] < target) lo = mid + 1;
        else                     hi = mid;
    }
    return lo;
}

// Verify monotone non-decreasing times[].  Cheap O(N) check; assert in
// debug builds.  Release builds rely on caller contract.
static inline bool isMonotone(const double* times, int n) {
    for (int i = 1; i < n; ++i) {
        if (times[i] < times[i - 1]) return false;
    }
    return true;
}

}  // anonymous namespace

Result compute(const float*  features,
               int           nPoints,
               int           nDimsFeat,
               const double* times,
               const Config& cfg) {
    Result out;
    out.isi.assign(static_cast<size_t>(nPoints), kUndefined);
    out.predecessor.assign(static_cast<size_t>(nPoints), -1);
    if (nPoints <= 0 || nDimsFeat <= 0) return out;

    assert(features != nullptr);
    assert(times    != nullptr);
    assert(isMonotone(times, nPoints) && "proxy_isi::compute: times[] must be sorted ascending");

    const int   K          = std::max(1, cfg.K);
    const float winSec     = std::max(0.0f, cfg.causalWindowSec);
    const float epsFactor  = cfg.epsilonFactor;  // can be +inf
    const bool  weighted   = cfg.WeightedVariant;

    // Pre-allocate per-thread scratch outside the parallel region to
    // amortise allocation cost across spikes.  We size each thread's
    // scratch to the worst-case candidate count (entire window
    // population), but in practice each query touches O(cand) entries.
#ifdef _OPENMP
    const int nThreads = omp_get_max_threads();
#else
    const int nThreads = 1;
#endif
    // [thread][slot] pair (sqDist, candidateIdx) — top-K heap per spike
    struct DistIdx { double d2; int idx; };
    std::vector<std::vector<DistIdx>> heapBuf(nThreads);

    int64_t nDefined_local   = 0;
    int64_t nUndefined_local = 0;

    #pragma omp parallel reduction(+:nDefined_local,nUndefined_local)
    {
#ifdef _OPENMP
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        auto& heap = heapBuf[tid];
        heap.reserve(static_cast<size_t>(K + 1));

        #pragma omp for schedule(dynamic, 256)
        for (int i = 0; i < nPoints; ++i) {
            const double  ti = times[i];
            const float*  fi = features + static_cast<size_t>(i) * nDimsFeat;

            // Find causal window start: first j with t_j ≥ t_i − winSec.
            const double targetT = ti - static_cast<double>(winSec);
            const int cStart = lowerBound(times, 0, i, targetT);
            const int cEnd   = i;  // exclusive
            if (cStart >= cEnd) {
                // No causal predecessors in window.
                ++nUndefined_local;
                continue;
            }

            // Build top-K heap of (squared distance, index) over candidates.
            // Max-heap by d2 so we can pop the current largest when full.
            heap.clear();
            auto cmpMaxByD2 = [](const DistIdx& a, const DistIdx& b) {
                return a.d2 < b.d2;
            };

            for (int j = cStart; j < cEnd; ++j) {
                const float* fj = features + static_cast<size_t>(j) * nDimsFeat;
                const double d2 = sqDistance(fi, fj, nDimsFeat);
                if (!std::isfinite(d2)) continue;
                if (static_cast<int>(heap.size()) < K) {
                    heap.push_back({d2, j});
                    std::push_heap(heap.begin(), heap.end(), cmpMaxByD2);
                } else if (d2 <= heap.front().d2) {
                    // `≤` (not `<`) breaks d²-ties in favour of more-recent
                    // candidates.  Matters for the degenerate case where
                    // many candidates share an identical distance (e.g.,
                    // exact-duplicate feature vectors); without it the
                    // heap freezes on the first-K candidates regardless
                    // of how many newer ones arrive.
                    std::pop_heap(heap.begin(), heap.end(), cmpMaxByD2);
                    heap.back() = {d2, j};
                    std::push_heap(heap.begin(), heap.end(), cmpMaxByD2);
                }
            }

            // Minimum heap occupancy for the ε-gate to be meaningful.
            // With a single candidate, min-of-heap is just that single
            // distance and the gate trivially passes whatever it was —
            // there's no notion of "near vs far" with only one sample.
            // Require ≥ 2 candidates so the min-of-heap is a meaningful
            // local scale.  Exception: epsilonFactor = +∞ disables the
            // gate entirely, in which case any candidate count is fine.
            if (heap.empty() ||
                (static_cast<int>(heap.size()) < 2 && std::isfinite(epsFactor))) {
                ++nUndefined_local;
                continue;
            }

            // ε-gate threshold: epsilonFactor² × (min-of-heap d²).
            //
            // Why min, not K-th (max-of-heap)?  K-th-nearest as scale is
            // standard in K-NN density estimation, but it assumes the
            // K-NN are mostly same-distribution.  For spike data this
            // fails whenever the K-NN contains cross-cluster entries —
            // and when it does, K-th-nearest equals the cross-cluster
            // distance and ε auto-calibrates loose enough to let those
            // cross-cluster entries pass the gate.  Min-of-heap is robust
            // to such contamination: as long as the nearest candidate is
            // a same-unit spike (which is the case whenever two units
            // are even moderately well-separated in feature space), the
            // scale is correctly set to the within-unit nearest-neighbour
            // distance, and cross-unit candidates at distance ≫ that get
            // filtered out by any moderate epsilonFactor.
            //
            // Trade-off: the user must set epsilonFactor to bracket the
            // expected within-unit feature jitter, which is now in units
            // of the nearest-neighbour distance rather than the K-th.
            // Defaults to 3.0 in Config (see header).
            double minD2 = std::numeric_limits<double>::infinity();
            for (const auto& di : heap) {
                if (di.d2 < minD2) minD2 = di.d2;
            }
            const double epsD2 = (std::isfinite(epsFactor))
                ? minD2 * static_cast<double>(epsFactor) *
                          static_cast<double>(epsFactor)
                : std::numeric_limits<double>::infinity();

            if (weighted) {
                // Weighted average ISI over qualifying candidates.
                // Bandwidth σ² = epsD2 / 2 → exp(−d² / (2σ²)) = exp(−d²/epsD2).
                // Guard against σ² = 0 (all-coincident features) by skipping.
                if (!(epsD2 > 0.0) || !std::isfinite(epsD2)) {
                    ++nUndefined_local;
                    continue;
                }
                double wSum   = 0.0;
                double isiSum = 0.0;
                int    argmax_t_idx = -1;
                double max_t = -std::numeric_limits<double>::infinity();
                for (const auto& di : heap) {
                    if (di.d2 > epsD2) continue;
                    const double w   = std::exp(-di.d2 / epsD2);
                    const double isi = ti - times[di.idx];
                    wSum   += w;
                    isiSum += w * isi;
                    if (times[di.idx] > max_t) {
                        max_t = times[di.idx];
                        argmax_t_idx = di.idx;
                    }
                }
                if (wSum > 0.0 && argmax_t_idx >= 0) {
                    out.isi[i]         = static_cast<float>(isiSum / wSum);
                    out.predecessor[i] = argmax_t_idx;
                    ++nDefined_local;
                } else {
                    ++nUndefined_local;
                }
            } else {
                // Strict: most-recent candidate that passes ε-gate.
                int    bestIdx = -1;
                double bestT   = -std::numeric_limits<double>::infinity();
                for (const auto& di : heap) {
                    if (di.d2 > epsD2) continue;
                    const double tj = times[di.idx];
                    if (tj > bestT) {
                        bestT   = tj;
                        bestIdx = di.idx;
                    }
                }
                if (bestIdx >= 0) {
                    out.isi[i]         = static_cast<float>(ti - bestT);
                    out.predecessor[i] = bestIdx;
                    ++nDefined_local;
                } else {
                    ++nUndefined_local;
                }
            }
        }
    }  // end parallel

    out.nDefined   = nDefined_local;
    out.nUndefined = nUndefined_local;

    // Median of defined ISIs (for diagnostics).  Collect, partial-sort.
    if (out.nDefined > 0) {
        std::vector<float> defined;
        defined.reserve(static_cast<size_t>(out.nDefined));
        for (int i = 0; i < nPoints; ++i) {
            if (std::isfinite(out.isi[i])) defined.push_back(out.isi[i]);
        }
        const size_t mid = defined.size() / 2;
        std::nth_element(defined.begin(), defined.begin() + mid, defined.end());
        out.medianIsi = defined[mid];
    }

    if (cfg.Verbose) {
        std::fprintf(stderr,
            "[proxy_isi] N=%d, defined=%lld, undefined=%lld, median ISI=%.4g s "
            "(K=%d, window=%.2g s, eps=%.2g, %s)\n",
            nPoints,
            static_cast<long long>(out.nDefined),
            static_cast<long long>(out.nUndefined),
            static_cast<double>(out.medianIsi),
            K, static_cast<double>(winSec),
            static_cast<double>(epsFactor),
            weighted ? "weighted" : "strict");
    }

    return out;
}

}  // namespace proxy_isi
