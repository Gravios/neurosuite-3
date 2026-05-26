// =============================================================================
// wave_knn_split.cpp  —  implementation of klusters-faithful KNN split
// See wave_knn_split.h for algorithm rationale.
// =============================================================================
#include "wave_knn_split.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace wave_knn_split {

namespace {

// Squared Euclidean distance over [0, nDimsUse).
static inline double sqDist(const float* a, const float* b, int nDimsUse) {
    double acc = 0.0;
    for (int d = 0; d < nDimsUse; ++d) {
        const double diff = static_cast<double>(a[d]) - static_cast<double>(b[d]);
        acc += diff * diff;
    }
    return acc;
}

}  // anonymous

Result Run(const float*                       features,
           int                                nPoints,
           int                                nDims,
           std::vector<int>&                  labels,
           const std::vector<float>&          clusterTraces,
           const std::vector<int>&            clusterTraceIds,
           const Config&                      cfg) {
    Result out;
    if (nPoints <= 0 || nDims < 2 || static_cast<int>(labels.size()) != nPoints) {
        return out;
    }
    const int K = std::max(1, cfg.K);
    const int nDimsUse = nDims - 1;  // exclude last (time) dim
    const int minVotes = static_cast<int>(
        std::ceil(static_cast<double>(K) * static_cast<double>(cfg.majorityThreshold)));

    // 1. Count cluster sizes.
    std::unordered_map<int, int> clusterSize;
    int maxLabel = 0;
    for (int i = 0; i < nPoints; ++i) {
        const int c = labels[i];
        if (c <= 0) continue;  // noise/MUA — ignore
        clusterSize[c]++;
        if (c > maxLabel) maxLabel = c;
    }
    if (clusterSize.empty()) return out;

    // 2. Compute trace filter cutoff (chunk-median over non-noise clusters
    //    of sufficient size to be eligible as references).  Only used in
    //    AUTO-PICK mode when no explicit referenceIds were supplied.
    float medianTrace = std::numeric_limits<float>::infinity();
    std::unordered_map<int, float> traceMap;
    const bool autoPickRefs = cfg.referenceIds.empty();
    if (autoPickRefs && cfg.referencesBelowMedianTrace
        && !clusterTraces.empty()
        && clusterTraces.size() == clusterTraceIds.size()) {
        std::vector<float> sizedOk;
        sizedOk.reserve(clusterTraceIds.size());
        for (size_t k = 0; k < clusterTraceIds.size(); ++k) {
            const int cid = clusterTraceIds[k];
            const float tr = clusterTraces[k];
            traceMap[cid] = tr;
            auto it = clusterSize.find(cid);
            if (it != clusterSize.end() && it->second >= cfg.minRefClusterSize) {
                sizedOk.push_back(tr);
            }
        }
        if (sizedOk.size() >= 2) {
            std::sort(sizedOk.begin(), sizedOk.end());
            medianTrace = sizedOk[sizedOk.size() / 2];
        }
    }

    // 3. Reference-cluster set.
    //    Mode A (explicit refIds supplied): intersect with size threshold.
    //    Mode B (auto-pick): size ≥ minRefClusterSize AND, if traces
    //    supplied with the filter on, trace < median.
    std::set<int> refClusters;
    if (!autoPickRefs) {
        for (int cid : cfg.referenceIds) {
            const auto it = clusterSize.find(cid);
            if (it != clusterSize.end() && it->second >= cfg.minRefClusterSize) {
                refClusters.insert(cid);
            }
        }
    } else {
        for (const auto& kv : clusterSize) {
            if (kv.second < cfg.minRefClusterSize) continue;
            if (cfg.referencesBelowMedianTrace && !traceMap.empty()) {
                auto it = traceMap.find(kv.first);
                if (it == traceMap.end() || it->second >= medianTrace) continue;
            }
            refClusters.insert(kv.first);
        }
    }
    if (refClusters.size() < 2) {
        if (cfg.Verbose) {
            std::fprintf(stderr, "[wave_knn_split] only %zu eligible references; skipping\n",
                         refClusters.size());
        }
        return out;
    }

    // 4. Source-cluster set.
    //    Mode A (explicit sourceIds): use them (size threshold still applies).
    //    Mode B (auto-pick): every non-ref non-noise cluster ≥ minSource.
    std::set<int> sourceClusters;
    if (!cfg.sourceIds.empty()) {
        for (int cid : cfg.sourceIds) {
            const auto it = clusterSize.find(cid);
            if (it != clusterSize.end() && it->second >= cfg.minSourceClusterSize
                && !refClusters.count(cid)) {
                sourceClusters.insert(cid);
            }
        }
    } else {
        for (const auto& kv : clusterSize) {
            if (refClusters.count(kv.first)) continue;
            if (kv.second < cfg.minSourceClusterSize) continue;
            sourceClusters.insert(kv.first);
        }
    }

    // 5. Build the reference POOL: indices of all spikes whose label is in
    //    refClusters.
    std::vector<int> poolIdx;
    poolIdx.reserve(static_cast<size_t>(nPoints));
    for (int i = 0; i < nPoints; ++i) {
        if (refClusters.count(labels[i])) poolIdx.push_back(i);
    }
    if (poolIdx.empty() || sourceClusters.empty()) {
        if (cfg.Verbose) {
            std::fprintf(stderr,
                "[wave_knn_split] pool=%zu sources=%zu; nothing to do\n",
                poolIdx.size(), sourceClusters.size());
        }
        return out;
    }
    out.nSourcesConsidered = static_cast<int>(sourceClusters.size());

    // 5. For each source spike, find K nearest neighbours in pool, vote.
    //    new_label_proposal[i] = winning ref label, or -1 = stay in source.
    std::vector<int> proposal(static_cast<size_t>(nPoints), -1);

    // Collect source-spike indices for OMP-balanced work distribution.
    std::vector<int> sourceIdx;
    sourceIdx.reserve(static_cast<size_t>(nPoints));
    for (int i = 0; i < nPoints; ++i) {
        if (sourceClusters.count(labels[i])) sourceIdx.push_back(i);
    }

    struct DistIdx { double d2; int poolPos; };

    #pragma omp parallel
    {
        std::vector<DistIdx> heap;
        heap.reserve(static_cast<size_t>(K + 1));
        auto cmpMaxByD2 = [](const DistIdx& a, const DistIdx& b) {
            return a.d2 < b.d2;
        };

        #pragma omp for schedule(dynamic, 64)
        for (int si = 0; si < static_cast<int>(sourceIdx.size()); ++si) {
            const int i = sourceIdx[si];
            const float* fi = features + static_cast<size_t>(i) * nDims;

            // Top-K heap of (d², poolPos) — max-heap by d²
            heap.clear();
            for (int p = 0; p < static_cast<int>(poolIdx.size()); ++p) {
                const int j = poolIdx[p];
                const float* fj = features + static_cast<size_t>(j) * nDims;
                const double d2 = sqDist(fi, fj, nDimsUse);
                if (static_cast<int>(heap.size()) < K) {
                    heap.push_back({d2, p});
                    std::push_heap(heap.begin(), heap.end(), cmpMaxByD2);
                } else if (d2 < heap.front().d2) {
                    std::pop_heap(heap.begin(), heap.end(), cmpMaxByD2);
                    heap.back() = {d2, p};
                    std::push_heap(heap.begin(), heap.end(), cmpMaxByD2);
                }
            }
            if (static_cast<int>(heap.size()) < std::min(K, static_cast<int>(poolIdx.size()))) {
                continue;  // not enough neighbours; leave proposal[i] = -1
            }

            // Majority vote — small map, OK to use std::map for this size
            std::map<int, int> tally;
            for (const auto& di : heap) {
                tally[labels[poolIdx[di.poolPos]]]++;
            }
            int bestLabel = -1, bestCount = 0;
            for (const auto& kv : tally) {
                if (kv.second > bestCount) {
                    bestCount = kv.second;
                    bestLabel = kv.first;
                }
            }
            if (bestCount >= minVotes) {
                proposal[i] = bestLabel;
            }
        }
    }  // omp parallel

    // 6. Group source spikes by (source_id, proposal_id).  Materialise each
    //    bucket of size ≥ minNewClusterSize as a NEW cluster ID.  Smaller
    //    buckets and -1-proposal spikes stay in the source.
    //
    //    Note: matches klusters' semantics — we do NOT actually move
    //    spikes into the reference cluster.  We only create new sub-clusters
    //    of the source.  Phase 5 cross-chunk template matching then
    //    consolidates these into global units.
    std::map<int, std::map<int, std::vector<int>>> bucket;  // source -> winner -> idx
    for (int i : sourceIdx) {
        if (proposal[i] < 0) continue;
        bucket[labels[i]][proposal[i]].push_back(i);
    }

    int nextId = maxLabel + 1;
    int nResidual = 0;
    for (auto& srcKv : bucket) {
        bool srcSplit = false;
        for (auto& winKv : srcKv.second) {
            auto& idxs = winKv.second;
            if (static_cast<int>(idxs.size()) < cfg.minNewClusterSize) {
                nResidual += static_cast<int>(idxs.size());  // stays in source
                continue;
            }
            const int newId = nextId++;
            for (int i : idxs) labels[i] = newId;
            out.nNewClusters++;
            out.nSpikesReassigned += static_cast<int>(idxs.size());
            srcSplit = true;
        }
        if (srcSplit) out.nSourcesSplit++;
    }
    out.nSpikesResidual = nResidual;

    if (cfg.Verbose) {
        std::fprintf(stderr,
            "[wave_knn_split] K=%d majThr=%.2f sources=%d split=%d "
            "newClusters=%d reassigned=%d residual=%d\n",
            K, cfg.majorityThreshold, out.nSourcesConsidered,
            out.nSourcesSplit, out.nNewClusters,
            out.nSpikesReassigned, out.nSpikesResidual);
    }
    return out;
}

}  // namespace wave_knn_split
