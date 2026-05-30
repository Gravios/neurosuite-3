// =============================================================================
// clust_quality.h  —  variance-based cluster quality score
//
// Computes per-cluster q = mean over (sample, channel) of
//     var_i(spike_i[ch, t] - meanWav[ch, t])  /  E[meanWav[ch, t]²]
// = sample-wise coefficient of variation, averaged over channels and time.
//
// Lower q means a tighter, more confident cluster.  Scale-invariant
// (doesn't penalise quiet units), captures what L-ratio captures but in
// waveform space rather than feature space.  Crucially does NOT depend
// on the PCA basis — it sees structure that PCA-feature scoring throws
// away.
//
// Use cases in the rewrite:
//   • Priority queue: high-q clusters get split-tested first (probably
//     contaminated mixtures); low-q clusters get used as KNN-split
//     reference pool.
//   • Stop criterion: refine until quality stops improving.
//   • Sanity check: report worst-q-cluster after each phase to spot
//     pipeline regressions.
// =============================================================================
#pragma once

#include <unordered_map>
#include <vector>

namespace clust_quality {

struct ClusterQuality {
    int    clusterId   = 0;
    int    nSpikes     = 0;
    float  meanEnergy  = 0.0f;  // E[meanWav[ch,t]²] over all (ch,t)
    float  residVar    = 0.0f;  // mean of var_i(spike_i - meanWav) over (ch,t)
    float  qScore      = 0.0f;  // residVar / meanEnergy  (lower = better)
};

// -----------------------------------------------------------------------------
// Compute() — produces per-cluster quality scores.
//
// Parameters:
//   meanWaveforms     map clusterId → channel-major mean waveform pointer
//                     (caller-owned).  Each [nCh × nSamp] floats.
//   getSpike          callback: given spikeGlobalIndex, write the spike
//                     into the supplied buffer (nCh × nSamp).  Returns
//                     false if the spike is unavailable (filtered, etc.).
//   spikeIndicesByCluster   map clusterId → vector of global spike indices
//   nCh, nSamp        waveform dimensions
//
// Returns:
//   vector of ClusterQuality, one entry per cluster present.  Sorted by
//   qScore ascending (best clusters first).
// -----------------------------------------------------------------------------
using SpikeFetcher = bool(*)(int spikeGlobalIdx, float* dst, void* userData);

std::vector<ClusterQuality> compute(
    const std::unordered_map<int, const float*>&     meanWaveforms,
    const std::unordered_map<int, std::vector<int>>& spikeIndicesByCluster,
    int                                              nCh,
    int                                              nSamp,
    SpikeFetcher                                     getSpike,
    void*                                            fetcherUserData);

}  // namespace clust_quality
