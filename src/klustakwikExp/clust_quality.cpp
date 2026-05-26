// =============================================================================
// clust_quality.cpp  —  implementation
// =============================================================================
#include "clust_quality.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace clust_quality {

std::vector<ClusterQuality> compute(
    const std::unordered_map<int, const float*>&     meanWaveforms,
    const std::unordered_map<int, std::vector<int>>& spikeIndicesByCluster,
    int                                              nCh,
    int                                              nSamp,
    SpikeFetcher                                     getSpike,
    void*                                            fetcherUserData) {
    std::vector<ClusterQuality> out;
    if (nCh <= 0 || nSamp <= 0) return out;
    const int N = nCh * nSamp;

    std::vector<float> tmp(static_cast<size_t>(N));

    for (const auto& kv : meanWaveforms) {
        const int     cid   = kv.first;
        const float*  mu    = kv.second;
        const auto    sIt   = spikeIndicesByCluster.find(cid);
        if (mu == nullptr || sIt == spikeIndicesByCluster.end()) continue;
        const auto&   sIdxs = sIt->second;
        if (sIdxs.empty()) continue;

        ClusterQuality q;
        q.clusterId = cid;
        q.nSpikes   = static_cast<int>(sIdxs.size());

        // 1. Mean-energy: average squared mean-waveform value.
        double meanE = 0.0;
        for (int k = 0; k < N; ++k) {
            const double mk = static_cast<double>(mu[k]);
            meanE += mk * mk;
        }
        meanE /= N;
        q.meanEnergy = static_cast<float>(meanE);

        // 2. Residual variance: average over (ch,t) of var_i(s_i - mu)
        //    Streaming: accumulate Σ(s_i - mu)² over all spikes and (ch,t),
        //    then divide by (N · nSpikes).  Skip spikes that fetcher fails.
        double residSumSq = 0.0;
        int    nGotSpikes = 0;
        for (int gi : sIdxs) {
            if (!getSpike(gi, tmp.data(), fetcherUserData)) continue;
            for (int k = 0; k < N; ++k) {
                const double d = static_cast<double>(tmp[k]) -
                                 static_cast<double>(mu[k]);
                residSumSq += d * d;
            }
            ++nGotSpikes;
        }
        if (nGotSpikes == 0) continue;
        const double residMean = residSumSq /
            (static_cast<double>(N) * nGotSpikes);
        q.residVar = static_cast<float>(residMean);

        // 3. q-score: residual variance / mean energy.  Guard against
        //    zero mean energy (silent cluster) — return +∞ to push such
        //    clusters to the back of the priority queue.
        q.qScore = (meanE > 0.0)
            ? static_cast<float>(residMean / meanE)
            : std::numeric_limits<float>::infinity();
        out.push_back(q);
    }

    std::sort(out.begin(), out.end(),
              [](const ClusterQuality& a, const ClusterQuality& b) {
                  return a.qScore < b.qScore;
              });
    return out;
}

}  // namespace clust_quality
