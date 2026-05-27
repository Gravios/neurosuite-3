// =============================================================================
// wave_knn_split.cpp  —  implementation of klusters-faithful KNN split
// See wave_knn_split.h for algorithm rationale.
// =============================================================================
#include "wave_knn_split.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <map>
#include <random>
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
           const std::vector<float>&          clusterAnisotropy,
           const std::vector<int>&            clusterAnisotropyIds,
           const Config&                      cfg) {
    Result out;
    if (nPoints <= 0 || nDims < 2 || static_cast<int>(labels.size()) != nPoints) {
        return out;
    }
    const int K = std::max(1, cfg.K);
    const int nDimsUse = nDims - 1;  // exclude last (time) dim
    const int minVotes = static_cast<int>(
        std::ceil(static_cast<double>(K) * static_cast<double>(cfg.majorityThreshold)));

    // Anisotropy map (optional gate on source candidates).  When both
    // a per-cluster anisotropy value AND a positive minSourceAnisotropy
    // are supplied, any cluster whose anisotropy is BELOW the threshold
    // is removed from the source-candidate set later.
    std::unordered_map<int, float> anisoMap;
    const bool useAniso = (cfg.minSourceAnisotropy > 0.0f) &&
                          !clusterAnisotropy.empty() &&
                          clusterAnisotropy.size() == clusterAnisotropyIds.size();
    if (useAniso) {
        anisoMap.reserve(clusterAnisotropyIds.size());
        for (size_t k = 0; k < clusterAnisotropyIds.size(); ++k)
            anisoMap[clusterAnisotropyIds[k]] = clusterAnisotropy[k];
    }

    // 1. Count cluster sizes.  Skip cluster 0 (noise) entirely from the
    //    auto-pick tally; skip cluster 1 only if cfg.skipMuaCluster1.
    //    BUT track maxLabel across ALL clusters (incl. filtered ones) so
    //    new cluster IDs we allocate don't collide with an existing
    //    cluster 1 that we chose not to consider.
    std::unordered_map<int, int> clusterSize;
    int maxLabel = 0;
    for (int i = 0; i < nPoints; ++i) {
        const int c = labels[i];
        if (c > maxLabel) maxLabel = c;
        if (c <= 0) continue;                          // noise: never in tally
        if (cfg.skipMuaCluster1 && c == 1) continue;   // klusters-compat MUA skip
        clusterSize[c]++;
    }
    if (clusterSize.empty()) return out;

    // 2. Compute trace filter cutoff (chunk-median).  Only used in auto-pick
    //    + useTraceFilter mode when no explicit referenceIds were supplied.
    float medianTrace = std::numeric_limits<float>::infinity();
    std::unordered_map<int, float> traceMap;
    const bool autoPickRefs = cfg.referenceIds.empty();
    if (autoPickRefs && cfg.useTraceFilter
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

    // 3. Reference cluster set + source cluster set + pool-exclusion mode.
    //
    //    Three configurations:
    //      (A) Explicit referenceIds given           → fixed ref set
    //      (B) Auto-pick + useTraceFilter=true        → KKE mode: ref =
    //          low-trace clusters, source = the rest.  Refs and sources
    //          are disjoint; no own-cluster exclusion needed in K-NN.
    //      (C) Auto-pick + useTraceFilter=false       → klusters mode:
    //          every sized-OK cluster is BOTH a reference (in the pool)
    //          AND a source candidate.  The K-NN loop excludes own-cluster
    //          pool entries per source spike so it doesn't trivially vote
    //          for itself.  Matches klusters' algorithm where the source
    //          is excluded from the reference pool.
    std::set<int> refClusters;   // pool contributors (klusters semantics: incl. all eligible)
    std::set<int> sourceClusters;
    bool klustersMode = false;
    if (!autoPickRefs) {
        // Mode A
        for (int cid : cfg.referenceIds) {
            const auto it = clusterSize.find(cid);
            if (it != clusterSize.end() && it->second >= cfg.minRefClusterSize) {
                refClusters.insert(cid);
            }
        }
    } else if (cfg.useTraceFilter) {
        // Mode B: trace filter selects refs; sources = the complement
        for (const auto& kv : clusterSize) {
            if (kv.second < cfg.minRefClusterSize) continue;
            if (!traceMap.empty()) {
                auto it = traceMap.find(kv.first);
                if (it == traceMap.end() || it->second >= medianTrace) continue;
            }
            refClusters.insert(kv.first);
        }
    } else {
        // Mode C: klusters semantics — every sized-OK cluster is in the pool
        klustersMode = true;
        for (const auto& kv : clusterSize) {
            if (kv.second < cfg.minRefClusterSize) continue;
            refClusters.insert(kv.first);
        }
    }
    if (refClusters.size() < 2) {
        if (cfg.Verbose) {
            std::fprintf(stderr,
                "[wave_knn_split] only %zu eligible references; skipping\n",
                refClusters.size());
        }
        return out;
    }

    // Source set
    if (!cfg.sourceIds.empty()) {
        for (int cid : cfg.sourceIds) {
            const auto it = clusterSize.find(cid);
            if (it != clusterSize.end() && it->second >= cfg.minSourceClusterSize
                && (klustersMode || !refClusters.count(cid))) {
                sourceClusters.insert(cid);
            }
        }
    } else if (cfg.useTraceFilter) {
        // Mode B: sources = clusters NOT picked as refs
        for (const auto& kv : clusterSize) {
            if (refClusters.count(kv.first)) continue;
            if (kv.second < cfg.minSourceClusterSize) continue;
            sourceClusters.insert(kv.first);
        }
    } else {
        // Mode C: sources = same set as refs (every eligible cluster)
        for (int cid : refClusters) {
            if (clusterSize[cid] >= cfg.minSourceClusterSize) {
                sourceClusters.insert(cid);
            }
        }
    }

    // 3b. Anisotropy gate (mixture detector).  Remove from the source
    //     set any cluster whose λ_max(Σ)/tr(Σ) is below threshold.
    //     A unimodal cluster has roughly isotropic residuals so
    //     anisotropy ≈ 1/nDim; a mixture is stretched along the
    //     separation axis so anisotropy is much higher.  Sparing
    //     unimodals avoids the runaway over-splitting that occurs
    //     when WaveKnnSplit attacks well-isolated clusters and
    //     generates "splits" from random K-NN voting noise.  Refs are
    //     NOT filtered — well-isolated unimodals are exactly the
    //     references we want to vote against.
    //
    // Missing-entry policy: REJECT (defensive).  When the caller has
    // computed anisotropy for some clusters but not others — typically
    // because a freshly-created cluster from an earlier Phase 4b
    // iteration has zero covariance and was skipped by the caller —
    // we treat the absence as "cannot judge unimodality, refuse to
    // split" rather than admit.  Admitting was the regression that
    // produced 10k+ local clusters: every freshly-split cluster came
    // back with no anisotropy entry, got a free pass, and was re-
    // split again on the next iter.  The user can revert to the
    // permissive policy by computing valid anisotropy for every
    // cluster (e.g. by ensuring cov is rebuilt before each call) and
    // would get identical behaviour.
    int nFilteredAniso = 0;
    if (useAniso && !sourceClusters.empty()) {
        std::set<int> kept;
        for (int cid : sourceClusters) {
            auto it = anisoMap.find(cid);
            if (it == anisoMap.end()) {
                ++nFilteredAniso;          // missing-entry → reject
                continue;
            }
            if (it->second >= cfg.minSourceAnisotropy) kept.insert(cid);
            else ++nFilteredAniso;
        }
        sourceClusters.swap(kept);
    }
    out.nSourcesAnisotropyFiltered = nFilteredAniso;

    // 4. Noise-cluster (cid=0) as a source candidate, drawn at this
    //    probability per Run() invocation.  Noise is NEVER part of the
    //    reference pool (it's noise by definition).
    if (cfg.noiseSourceProbability > 0.0f) {
        // Count noise-cluster spikes
        int noiseSize = 0;
        for (int i = 0; i < nPoints; ++i) {
            if (labels[i] == 0) ++noiseSize;
        }
        if (noiseSize >= cfg.minSourceClusterSize) {
            const unsigned seed = cfg.rngSeed ? cfg.rngSeed
                : static_cast<unsigned>(std::time(nullptr));
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            if (u(rng) < cfg.noiseSourceProbability) {
                sourceClusters.insert(0);
                out.noiseClusterTried = true;
                if (cfg.Verbose) {
                    std::fprintf(stderr,
                        "[wave_knn_split] noise cluster (cid=0, %d spikes) "
                        "drawn as source (p=%.2f)\n",
                        noiseSize, cfg.noiseSourceProbability);
                }
            }
        }
    }

    // 5. Build the reference POOL: indices of all spikes whose label is in
    //    refClusters.  Noise spikes are NEVER pool members regardless of
    //    whether cluster 0 was drawn as a source.
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

    // ── 5. Per-source-cluster sequential processing with neighborhood mask.
    //
    // The original algorithm processed ALL source spikes in one flat
    // parallel sweep and bucketed by (sourceCluster, winnerRef) at the
    // end.  That doubles up overlap regions between clusters: cluster A
    // sees its half of an overlap with B and produces a sub-cluster;
    // cluster B sees its half (same physical region) and produces a
    // near-mirror sub-cluster — two new IDs for one real region.
    //
    // This loop processes source clusters one at a time in randomised
    // order (so no cluster is systematically privileged across runs),
    // commits each cluster's split immediately, and MASKS the k-NN
    // pool spikes of every reassigned spike from being eligible
    // sources later in the same call.  When the second cluster's
    // overlap-region spikes look up their k-NN, they discover the
    // just-allocated sub-cluster and either fold into it (via the
    // majority vote) or end up in their own residual.  Either way:
    // no mirror.
    //
    // Parallelism: outer loop is sequential (it MUST be — later
    // iterations depend on earlier iterations' label assignments and
    // masks), inner k-NN voting within one source cluster is parallel.
    // Pool/source within one cluster is identical to the original
    // algorithm so the per-spike vote is unchanged.
    //
    // Sequential ordering can be disabled via cfg.maskNeighbors = false,
    // which restores the flat-parallel algorithm.

    struct DistIdx { double d2; int poolPos; };

    // Randomised source-cluster ordering.  XOR salt 0xa1b2c3d4u to
    // decorrelate from the noise-cluster probability draw above
    // (which uses the unsalted seed).
    std::vector<int> sourceOrder(sourceClusters.begin(), sourceClusters.end());
    {
        const unsigned seed = cfg.rngSeed ? cfg.rngSeed
            : static_cast<unsigned>(std::time(nullptr));
        std::mt19937 ordRng(seed ^ 0xa1b2c3d4u);
        std::shuffle(sourceOrder.begin(), sourceOrder.end(), ordRng);
    }

    // Spike-level mask: once a spike is in here, it is excluded from
    // being a source in any subsequent cluster's loop iteration.
    std::vector<char> maskedAsSrc(static_cast<size_t>(nPoints), 0);

    int nextId = maxLabel + 1;
    int totalResidualSpikesKeptInSource = 0;
    int nMaskedTotal = 0;

    for (int sourceCid : sourceOrder) {
        // Collect this cluster's eligible source spike indices, filtered
        // against the running mask.
        std::vector<int> srcIdx;
        srcIdx.reserve(static_cast<size_t>(nPoints));
        int nThisClusterMasked = 0;
        for (int i = 0; i < nPoints; ++i) {
            if (labels[i] != sourceCid) continue;
            if (cfg.maskNeighbors && maskedAsSrc[static_cast<size_t>(i)]) {
                ++nThisClusterMasked;
                continue;
            }
            srcIdx.push_back(i);
        }
        nMaskedTotal += nThisClusterMasked;
        const int nSrcThisCluster = static_cast<int>(srcIdx.size());
        if (nSrcThisCluster < cfg.minSourceClusterSize) continue;

        // Per-spike state for this cluster.  proposal[si] = winner label
        // (-1 = ambiguous / didn't reach majority); srcKnnPool[si] =
        // the spike indices of the top-K pool neighbours, retained so
        // we can mask them after commit if this spike gets reassigned.
        std::vector<int>                 proposal(
            static_cast<size_t>(nSrcThisCluster), -1);
        std::vector<std::vector<int>>    srcKnnPool(
            static_cast<size_t>(nSrcThisCluster));

        #pragma omp parallel
        {
            std::vector<DistIdx> heap;
            heap.reserve(static_cast<size_t>(K + 1));
            auto cmpMaxByD2 = [](const DistIdx& a, const DistIdx& b) {
                return a.d2 < b.d2;
            };

            #pragma omp for schedule(dynamic, 64)
            for (int si = 0; si < nSrcThisCluster; ++si) {
                const int i        = srcIdx[static_cast<size_t>(si)];
                const int srcLabel = labels[i];
                const float* fi    = features + static_cast<size_t>(i) * nDims;

                heap.clear();
                int nUsablePool = 0;
                for (int p = 0; p < static_cast<int>(poolIdx.size()); ++p) {
                    const int j = poolIdx[static_cast<size_t>(p)];
                    if (labels[j] == srcLabel) continue;
                    ++nUsablePool;
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
                if (static_cast<int>(heap.size()) < std::min(K, nUsablePool)) {
                    continue;   // leave proposal[si] = -1
                }

                std::map<int, int> tally;
                for (const auto& di : heap) {
                    tally[labels[poolIdx[static_cast<size_t>(di.poolPos)]]]++;
                }
                int bestLabel = -1, bestCount = 0;
                for (const auto& kv : tally) {
                    if (kv.second > bestCount) {
                        bestCount = kv.second;
                        bestLabel = kv.first;
                    }
                }
                if (bestCount >= minVotes) {
                    proposal[static_cast<size_t>(si)] = bestLabel;
                    // Retain pool spike indices for masking.
                    auto& nn = srcKnnPool[static_cast<size_t>(si)];
                    nn.reserve(heap.size());
                    for (const auto& di : heap) {
                        nn.push_back(poolIdx[static_cast<size_t>(di.poolPos)]);
                    }
                }
            }
        }   // omp parallel

        // ── Bucket THIS cluster's source spikes by winner.  Positions in
        //    `parts` are indices into srcIdx (NOT global spike indices).
        std::map<int, std::vector<int>> parts;   // winner -> srcIdx positions
        for (int si = 0; si < nSrcThisCluster; ++si) {
            parts[proposal[static_cast<size_t>(si)]].push_back(si);
        }
        // Pull ambiguous bucket (key -1) into residual.
        std::vector<int> residual;
        {
            auto it = parts.find(-1);
            if (it != parts.end()) {
                residual = std::move(it->second);
                parts.erase(it);
            }
        }
        // Fold small winners into residual.
        for (auto it = parts.begin(); it != parts.end(); ) {
            if (static_cast<int>(it->second.size()) < cfg.minNewClusterSize) {
                residual.insert(residual.end(),
                                it->second.begin(), it->second.end());
                it = parts.erase(it);
            } else {
                ++it;
            }
        }

        bool srcSplit = false;
        std::vector<int> reassignedSrcPositions;
        reassignedSrcPositions.reserve(static_cast<size_t>(nSrcThisCluster));

        // Winner buckets first (ascending refId).
        for (auto& winKv : parts) {
            const int newId = nextId++;
            for (int pos : winKv.second) {
                labels[srcIdx[static_cast<size_t>(pos)]] = newId;
                reassignedSrcPositions.push_back(pos);
            }
            out.nNewClusters++;
            out.nSpikesReassigned += static_cast<int>(winKv.second.size());
            srcSplit = true;
        }
        // Residual bucket disposition.
        if (static_cast<int>(residual.size()) >= cfg.minNewClusterSize
            && cfg.residualBecomesNewCluster) {
            const int newId = nextId++;
            for (int pos : residual) {
                labels[srcIdx[static_cast<size_t>(pos)]] = newId;
                reassignedSrcPositions.push_back(pos);
            }
            out.nNewClusters++;
            out.nResidualClusters++;
            out.nSpikesReassigned += static_cast<int>(residual.size());
            srcSplit = true;
        } else {
            totalResidualSpikesKeptInSource +=
                static_cast<int>(residual.size());
        }

        if (srcSplit) {
            out.nSourcesSplit++;

            // Mask the pool neighbours of every reassigned source spike.
            // Only the confident-winner spikes had their k-NN retained
            // (proposal != -1); residual/folded spikes have empty
            // srcKnnPool entries so their iteration is a no-op.
            if (cfg.maskNeighbors) {
                for (int pos : reassignedSrcPositions) {
                    const auto& nn = srcKnnPool[static_cast<size_t>(pos)];
                    for (int j : nn) {
                        maskedAsSrc[static_cast<size_t>(j)] = 1;
                    }
                }
            }
        }
    }
    out.nSpikesResidual        = totalResidualSpikesKeptInSource;
    out.nSpikesMaskedFromSplit = nMaskedTotal;

    if (cfg.Verbose) {
        std::fprintf(stderr,
            "[wave_knn_split] K=%d majThr=%.2f sources=%d split=%d "
            "newClusters=%d (residualClusters=%d) reassigned=%d "
            "residual=%d maskedFromSplit=%d "
            "(maskNeighbors=%s)\n",
            K, cfg.majorityThreshold, out.nSourcesConsidered,
            out.nSourcesSplit, out.nNewClusters, out.nResidualClusters,
            out.nSpikesReassigned, out.nSpikesResidual,
            out.nSpikesMaskedFromSplit,
            cfg.maskNeighbors ? "on" : "off");
    }
    return out;
}

}  // namespace wave_knn_split
