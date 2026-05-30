// =============================================================================
// cluster_hull_split.cpp  —  see header for algorithm + rationale
// =============================================================================

#include "cluster_hull_split.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cluster_hull_split {

namespace {

// ─── Disjoint Set Union (union-find) ─────────────────────────────────────────
// Path-compression + union-by-rank.  α(n) amortised per op.  All operations
// are serial — calling code wraps Run() in an OMP-parallel-for over clusters,
// not within a single cluster's union-find.
struct DSU {
    std::vector<int> parent;
    std::vector<int> rank;

    explicit DSU(int n) : parent(static_cast<size_t>(n)),
                          rank  (static_cast<size_t>(n), 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    int find(int x) {
        while (parent[static_cast<size_t>(x)] != x) {
            // Path-halving: cheaper than full path-compression, same α.
            parent[static_cast<size_t>(x)] =
                parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    }

    void unite(int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (rank[static_cast<size_t>(a)] < rank[static_cast<size_t>(b)])
            std::swap(a, b);
        parent[static_cast<size_t>(b)] = a;
        if (rank[static_cast<size_t>(a)] == rank[static_cast<size_t>(b)])
            ++rank[static_cast<size_t>(a)];
    }
};

// ─── Squared Euclidean distance ──────────────────────────────────────────────
// Scalar fallback; the compiler auto-vectorises this with -O3 for typical
// nDims (16-32).  We deliberately leave SIMD intrinsics out of the prototype
// — cluster sizes are bounded and the dominant cost is the n² outer loop,
// not per-pair arithmetic.  If profiling shows this loop matters, swap in
// AVX-512 batched distance like EStep's SIMD path.
inline float sqDist(const float* a, const float* b, int d) {
    float s = 0.0f;
    for (int i = 0; i < d; ++i) {
        const float diff = a[i] - b[i];
        s += diff * diff;
    }
    return s;
}

}  // anonymous namespace

// =============================================================================
// Run
// =============================================================================
Result Run(const float* data, int nPoints, int nDims, const Config& cfg) {
    Result r;

    // ── Guards ──────────────────────────────────────────────────────────────
    if (!data || nDims <= 0 || nPoints <= 0) {
        return r;  // empty result, didSplit=false
    }

    if (nPoints < 2 * cfg.minComponentSize) {
        // Too few points for any meaningful sub-component test.  Return
        // a single-component result so the caller can still consume
        // componentLabels uniformly.
        r.componentLabels.assign(static_cast<size_t>(nPoints), 1);
        r.nComponents          = 1;
        r.largestComponentSize = nPoints;
        r.didSplit             = false;
        return r;
    }

    // Effective distance dimensions (optionally drop time).
    const int dEff = cfg.excludeTimeDim ? (nDims - 1) : nDims;
    if (dEff <= 0) {
        // Pathological — only the time dim was present.  Treat as no
        // structure to find.
        r.componentLabels.assign(static_cast<size_t>(nPoints), 1);
        r.nComponents          = 1;
        r.largestComponentSize = nPoints;
        r.didSplit             = false;
        return r;
    }

    const int k = std::min(std::max(cfg.k, 1), nPoints - 1);

    // ── Step 1: k-NN per point (brute force) ────────────────────────────────
    // For each point p, we maintain a max-heap of size k holding its
    // (sq_dist, idx) pairs.  After scanning all q ≠ p, the heap contains
    // p's k nearest neighbours; we extract in reverse to get them sorted
    // ascending by distance.
    //
    // OMP-parallel-for over p: each iteration writes only to its own row
    // of nnDist / nnIdx, so the writes are race-free.  The outer caller
    // (KK Phase 2a.6 dispatch) typically already has an OMP-parallel-for
    // over clusters; if nested-parallel is disabled there, this inner
    // pragma is a no-op and Run() executes single-threaded.  That is the
    // intended behaviour.
    std::vector<float> nnDistFlat(static_cast<size_t>(nPoints) * k);
    std::vector<int>   nnIdxFlat (static_cast<size_t>(nPoints) * k);

    #pragma omp parallel for schedule(static)
    for (int p = 0; p < nPoints; ++p) {
        std::priority_queue<std::pair<float, int>> heap;  // max-heap
        const float* pa = data + static_cast<size_t>(p) * nDims;

        for (int q = 0; q < nPoints; ++q) {
            if (q == p) continue;
            const float* pb = data + static_cast<size_t>(q) * nDims;
            const float dsq = sqDist(pa, pb, dEff);

            if (static_cast<int>(heap.size()) < k) {
                heap.emplace(dsq, q);
            } else if (dsq < heap.top().first) {
                heap.pop();
                heap.emplace(dsq, q);
            }
        }

        // Extract in reverse to get ascending order.  After this loop,
        // nnDist[p][0] is p's nearest neighbour, nnDist[p][k-1] is its
        // k-th NN.
        for (int j = k - 1; j >= 0; --j) {
            const auto& top = heap.top();
            nnDistFlat[static_cast<size_t>(p) * k + j] = std::sqrt(top.first);
            nnIdxFlat [static_cast<size_t>(p) * k + j] = top.second;
            heap.pop();
        }
    }

    // ── Step 2: median d_k (k-th NN distance) → edge cutoff ─────────────────
    std::vector<float> dk(static_cast<size_t>(nPoints));
    for (int p = 0; p < nPoints; ++p)
        dk[static_cast<size_t>(p)] = nnDistFlat[static_cast<size_t>(p) * k + (k - 1)];

    std::vector<float> dkSorted = dk;
    std::nth_element(dkSorted.begin(),
                     dkSorted.begin() + nPoints / 2,
                     dkSorted.end());
    const float medianDk  = dkSorted[static_cast<size_t>(nPoints / 2)];
    const float threshold = cfg.mutualReachabilityScale * medianDk;

    // ── Step 3-5: build edges, union-find ───────────────────────────────────
    DSU dsu(nPoints);
    for (int p = 0; p < nPoints; ++p) {
        const float dkP = dk[static_cast<size_t>(p)];
        for (int j = 0; j < k; ++j) {
            const int   q     = nnIdxFlat [static_cast<size_t>(p) * k + j];
            const float dPQ   = nnDistFlat[static_cast<size_t>(p) * k + j];
            const float dkQ   = dk[static_cast<size_t>(q)];

            float edgeWeight;
            if (cfg.useMutualReachability) {
                edgeWeight = std::max({dkP, dkQ, dPQ});
            } else {
                edgeWeight = dPQ;
            }

            if (edgeWeight < threshold) {
                dsu.unite(p, q);
            }
        }
    }

    // ── Step 6: compact component labels, absorb small ──────────────────────
    // First pass: assign each unique root a 1-indexed temporary label and
    // count component sizes.
    std::vector<int> rootToTmp(static_cast<size_t>(nPoints), -1);
    std::vector<int> tmpSize;
    tmpSize.reserve(64);
    std::vector<int> spikeRoot(static_cast<size_t>(nPoints));
    for (int p = 0; p < nPoints; ++p) {
        spikeRoot[static_cast<size_t>(p)] = dsu.find(p);
    }
    int nextTmp = 1;
    for (int p = 0; p < nPoints; ++p) {
        const int rt = spikeRoot[static_cast<size_t>(p)];
        if (rootToTmp[static_cast<size_t>(rt)] == -1) {
            rootToTmp[static_cast<size_t>(rt)] = nextTmp++;
            tmpSize.push_back(0);
        }
        ++tmpSize[static_cast<size_t>(rootToTmp[static_cast<size_t>(rt)] - 1)];
    }

    // Second pass: identify large components (size ≥ minComponentSize) and
    // assign final 1-indexed contiguous labels.  Small components get
    // label 0.
    std::vector<int> tmpToFinal(tmpSize.size(), 0);
    int nLarge = 0;
    int largest = 0;
    for (size_t i = 0; i < tmpSize.size(); ++i) {
        if (tmpSize[i] >= cfg.minComponentSize) {
            tmpToFinal[i] = ++nLarge;
            if (tmpSize[i] > largest) largest = tmpSize[i];
        }
    }

    // Third pass: write per-spike final labels.
    r.componentLabels.assign(static_cast<size_t>(nPoints), 0);
    for (int p = 0; p < nPoints; ++p) {
        const int rt  = spikeRoot[static_cast<size_t>(p)];
        const int tmp = rootToTmp[static_cast<size_t>(rt)];
        r.componentLabels[static_cast<size_t>(p)] =
            tmpToFinal[static_cast<size_t>(tmp - 1)];
    }

    r.nComponents          = nLarge;
    r.largestComponentSize = largest;
    r.didSplit             = (nLarge >= 2);

    return r;
}

}  // namespace cluster_hull_split
