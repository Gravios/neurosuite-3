// =============================================================================
// per_channel_split.h  —  per-channel amplitude+phase bimodality split
//
// Catches the cluster-splitting failure mode that all three existing splitters
// (DipSplit, HullSplit, KnnSplit) miss: two units that differ by a small
// combination of amplitude and phase on a single channel.
//
// Why the existing splitters miss this case
// ------------------------------------------
//   • DipSplit tests bimodality on each PCA dimension of the cluster's
//     feature space.  PCA basis vectors span all channels, so a single-channel
//     waveform-shape difference projects faintly across 3-5 PCA dims with no
//     single dim crossing the dip threshold.
//   • HullSplit catches multi-D topology that 1D dip tests miss — but two
//     spatially-overlapping-but-slightly-shifted clusters are *connected*
//     in PCA space (the k-NN graph has no disconnected component), just
//     elongated.  HullSplit also fails.
//   • KnnSplit needs reference clusters with clearly different waveforms.
//     Two units differing only in fine waveform shape on one channel don't
//     produce sufficiently distinguishable templates for the K-NN majority
//     vote to break a tie.
//
// Algorithm
// ---------
//   1. For each alive cluster c with ≥ minClusterSize spikes:
//      a. Read each spike's full waveform (sample-major, nChan*nSamples).
//      b. For each channel ch ∈ [0, nChan):
//           Extract 4 per-spike features:
//             • peak amplitude (max value)
//             • peak time (argmax sample index)
//             • trough amplitude (min value)
//             • trough time (argmin sample index)
//      c. Standardise each feature dimension (z-score across the cluster)
//         so that amplitude (range ~1000) doesn't dominate time (range ~32)
//         when computing the principal component.
//      d. Run top_pcs_with_eigenvalues on the standardised 4D matrix to get
//         channel-PC1 — the direction of greatest amplitude+phase covariation
//         on this channel.
//      e. Project each spike to 1D along PC1.
//      f. Run dipsplit::valley_test on the 1D projections.
//      g. Record (channel, valley_depth, valley_loc, pc1_direction).
//
//   2. Pick the channel with deepest valley.
//   3. If valley_depth ≥ valleyThreshold:
//      a. Split each spike by sign of (pc1_score - valley_loc).
//      b. BIC-gate: confirm BIC(k=2) < BIC(k=1) on the 4D feature matrix.
//      c. Require both halves ≥ minSubClusterSize.
//      d. If all gates pass, assign one half a fresh cluster ID.
//
// Cost
// ----
// Per cluster (N spikes, C channels, S samples):
//   • waveform read:   O(N · C · S)        [memcpy / mmap]
//   • feature extract: O(N · C · S)        [scan for peak/trough]
//   • z-score:         O(N · C)            [4 features each]
//   • channel-PC1:     O(C · 16 + iters · 16)  [d=4, k=1]
//   • valley_test:     O(C · N log N)      [Silverman-rule KDE]
//   • BIC + materialise: O(N) on the winning channel only
//
// For N=1000, C=8, S=32: ~256k ops feature extract, ~24k ops PC1,
// ~80k ops valley test → ~1ms per cluster.  Full pass on 200 clusters = ~200ms.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace per_channel_split {

// -----------------------------------------------------------------------------
// Configuration.
// -----------------------------------------------------------------------------
struct Config {
    // Optional stop predicate: return true to stop splitting further
    // clusters (enforces a wall-clock time limit).  Empty = no limit.
    std::function<bool()> shouldStop;

    // Minimum cluster size to consider.  Below this, per-channel feature
    // distributions are too noisy for KDE-based valley detection.
    int   minClusterSize     = 50;

    // Valley depth threshold (0..1).  Matches dipsplit::valley_test
    // convention: 0.5 = valley is half the height of the shorter peak,
    // 0.7 = deep valley between well-separated modes.  0.4 is permissive
    // (catches faint single-channel differences), 0.6 is conservative.
    float valleyThreshold    = 0.5f;

    // Minimum spikes in each side after a split.  Both halves must clear
    // this floor or the split is rejected.
    int   minSubClusterSize  = 25;

    // BIC margin gate.  Splits are accepted only when
    //   bic_k1 - bic_k2  >  bicMarginConstant + bicMarginPerLogN · log(N)
    //
    // HONEST CAVEAT: diagonal-Gaussian BIC is a WEAK gate for this kind
    // of cluster split.  On continuous-noise unimodal data, valley_test
    // finds spurious depth ~0.5 by chance (multiple-testing across
    // channels × features × angles), and a 2-Gaussian fit on the
    // resulting partition produces BIC deltas in the THOUSANDS — the
    // 2-Gaussian model fits any roughly-median split far better than a
    // single Gaussian.  So BIC almost always accepts.
    //
    // This is mostly OK in production because:
    //   1. valleyThreshold (0.5 default) already rejects most noise-only
    //      candidates before BIC sees them.
    //   2. Downstream Phase 4 (within-chunk template merge), Phase 5
    //      (cross-chunk merge) and Phase 6b (mean-subtraction merge)
    //      re-merge over-splits whose mean templates xcorr highly.
    //   3. The user can disable per_channel_split entirely with
    //      -PerChannelSplitEnable 0 if it hurts.
    //
    // The N-scaled term is mostly a placeholder for a future replacement
    // with a proper gate (e.g. silhouette or permutation test).
    float bicMarginConstant = 12.0f;
    float bicMarginPerLogN  = 6.0f;

    // Per-channel SNR gate.  Only channels whose cluster-mean peak-to-trough
    // amplitude is at least `minChannelSnrRatio` × the strongest channel's
    // are tested.  Filters out low-signal channels whose per-spike feature
    // bimodality would be noise-driven (most often: trough timing on a
    // weak-signal channel follows the argmin of noise, which has spurious
    // KDE peaks).  Default 0.5 = a channel needs ≥ 50% of the strongest
    // channel's amplitude to qualify.  0.0 disables the gate.
    float minChannelSnrRatio = 0.5f;

    // Per-feature toggles (allow user to disable parts of the 4-vector).
    // Default: all four — amplitude AND phase, on both peak and trough.
    bool  usePeakAmplitude   = true;
    bool  usePeakTime        = true;
    bool  useTroughAmplitude = true;
    bool  useTroughTime      = true;

    // Number of new cluster IDs to allocate from (max existing ID + 1).
    // Caller passes this in if they need to reserve a range; the module
    // adds new IDs starting from `firstNewClusterId`.
    int   firstNewClusterId  = 0;

    bool  Verbose            = false;
};

// -----------------------------------------------------------------------------
// Result diagnostics.
// -----------------------------------------------------------------------------
struct Result {
    int nClustersConsidered = 0;
    int nClustersSplit      = 0;
    int nNewClusters        = 0;   // = nClustersSplit (each split → 1 new ID)
    int nSpikesReassigned   = 0;
    int nSpikesReadFailed   = 0;

    // Per-cluster diagnostic record (one entry per split that was attempted).
    struct ClusterReport {
        int   clusterId      = -1;
        int   bestChannel    = -1;
        float bestValleyDepth = 0.0f;
        bool  split          = false;
        const char* skipReason = nullptr;
    };
    std::vector<ClusterReport> reports;
};

// -----------------------------------------------------------------------------
// extract_per_channel_features — for one spike's full-channel waveform,
// fill a 4*nChan float buffer with [peak_amp_ch0, peak_t_ch0, trough_amp_ch0,
// trough_t_ch0, peak_amp_ch1, ...].
//
// waveform is sample-major (a[s*nChan + ch], matches .spk on-disk format).
// Static so callers (and tests) can extract without the wrapper.
// -----------------------------------------------------------------------------
void extract_per_channel_features(
    const int16_t* waveform, int nChan, int nSamples,
    float* out_4xNchan);

// -----------------------------------------------------------------------------
// Run — main entry point.
//
// Parameters:
//   clusterSpikes  vector indexed by cluster ID; clusterSpikes[c] is the
//                  list of global spike indices belonging to cluster c.
//                  Cluster 0 (noise) is skipped.  Empty entries skipped.
//   readWaveform   callable that fills the nChan*nSamples buffer with the
//                  waveform for the given spike index (returns false on
//                  I/O error).  This is a thin wrapper around KK::Time-
//                  ShiftReadSpikeWave to keep the module decoupled from
//                  KK state.
//   nChan          channels per spike
//   nSamples       samples per channel per spike
//   labels         per-spike cluster labels [nPoints]; updated in place.
//                  New cluster IDs are allocated starting from
//                  cfg.firstNewClusterId.
//   cfg            configuration (see Config above).
//
// Returns:
//   Result with diagnostics.  Labels updated in place.
// -----------------------------------------------------------------------------
Result Run(
    const std::vector<std::vector<int>>& clusterSpikes,
    const std::function<bool(int spikeIdx, int16_t* dst)>& readWaveform,
    int nChan, int nSamples,
    std::vector<int>& labels,
    const Config& cfg);

}  // namespace per_channel_split
