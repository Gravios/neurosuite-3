// =============================================================================
// per_channel_split.cpp
//
// Implementation of per-channel amplitude+phase bimodality split.
//
// Algorithm (refactored)
// ----------------------
// Original attempt used z-score + PCA on the 4-feature per-channel matrix.
// That fails because z-scoring equalises per-column variance, so PCA's
// "direction of max variance" loses its grip on the bimodal signal — PC1
// ends up in a random direction.
//
// Replacement: per-feature 1D dip + 2D angle-sweep on the (amp, time) pairs.
//
// For each channel ch and each pair (amplitude_feature, time_feature) ∈
//   { (peak_amp, peak_time), (trough_amp, trough_time) }:
//
//   1. Range-normalise both columns to [0, 1] (preserves bimodality, makes
//      angle sweep meaningful — z-score would NOT preserve bimodality, but
//      range-norm does because it's a scalar rescale, not a re-centring).
//   2. Sweep θ ∈ [0, π) at K angles (default 18 = 10° increments).
//   3. At each θ compute proj_i = cos(θ)·amp_i + sin(θ)·time_i.
//   4. Run dipsplit::valley_test on the 1D projection.
//   5. Track (θ, depth, valley_loc) at max depth.
//
// Also run direct 1D valley_test on each of the 4 individual features as
// fallback — catches clusters where one feature dominates and the angle
// sweep's discretisation misses the exact 0° or 90° optimum.
//
// Pick the (channel, projection) with max depth across all tests.  If
// max depth >= valleyThreshold:
//   • Assign each spike to "above" or "below" sub-cluster based on its
//     projection score vs the valley location.
//   • BIC-gate using the standardised (peak_amp, peak_time, trough_amp,
//     trough_time) features on that channel.  Confirms that the proposed
//     2-cluster split is supported by the joint distribution, not just
//     the 1D projection.
//   • Min-size gate on both halves.
// =============================================================================
#include "per_channel_split.h"
#include "dipsplit.h"
#include <cstdio>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace per_channel_split {

// ---------------------------------------------------------------------------
// extract_per_channel_features
// ---------------------------------------------------------------------------
void extract_per_channel_features(
    const int16_t* wave, int nChan, int nSamples,
    float* out_4xNchan)
{
    if (!wave || !out_4xNchan || nChan <= 0 || nSamples <= 0) return;
    for (int ch = 0; ch < nChan; ++ch) {
        int peakVal    = INT16_MIN;
        int troughVal  = INT16_MAX;
        int peakTime   = 0;
        int troughTime = 0;
        for (int s = 0; s < nSamples; ++s) {
            const int v = wave[s * nChan + ch];
            if (v > peakVal)   { peakVal   = v; peakTime   = s; }
            if (v < troughVal) { troughVal = v; troughTime = s; }
        }
        out_4xNchan[ch * 4 + 0] = static_cast<float>(peakVal);
        out_4xNchan[ch * 4 + 1] = static_cast<float>(peakTime);
        out_4xNchan[ch * 4 + 2] = static_cast<float>(troughVal);
        out_4xNchan[ch * 4 + 3] = static_cast<float>(troughTime);
    }
}

// ---------------------------------------------------------------------------
// range_normalise — rescale a column to [0, 1] preserving bimodality.
// Constant columns become 0.5 (degenerate but harmless).
// ---------------------------------------------------------------------------
static void range_normalise(double* x, int n)
{
    if (n <= 0) return;
    double lo = x[0], hi = x[0];
    for (int i = 1; i < n; ++i) {
        if (x[i] < lo) lo = x[i];
        if (x[i] > hi) hi = x[i];
    }
    const double range = hi - lo;
    if (range < 1e-12) {
        for (int i = 0; i < n; ++i) x[i] = 0.5;
        return;
    }
    const double inv = 1.0 / range;
    for (int i = 0; i < n; ++i) x[i] = (x[i] - lo) * inv;
}

// ---------------------------------------------------------------------------
// channel_has_signal — heuristic SNR gate.  Compute the cluster's mean
// waveform amplitude on this channel; reject channels where the mean
// peak-to-trough is too small for per-spike feature bimodality to be
// meaningful (the per-spike features would be dominated by noise patterns).
//
// "snr ratio" here = (mean_peak_to_trough_on_this_channel) /
//                    (max mean_peak_to_trough across all channels).
// Channels below `minSnrRatio` are skipped.
// ---------------------------------------------------------------------------
static bool channel_has_signal(
    const float* fullFeatures, int n, int nChan, int channel,
    float minSnrRatio)
{
    // Mean peak-amp and trough-amp on each channel; use the cluster-mean
    // peak-to-trough as the per-channel signal strength.
    double thisAmp = 0.0;
    double maxAmp  = 0.0;
    for (int ch = 0; ch < nChan; ++ch) {
        double sumPeak = 0.0, sumTrough = 0.0;
        for (int i = 0; i < n; ++i) {
            const float* row = fullFeatures
                             + static_cast<ptrdiff_t>(i) * (4 * nChan)
                             + ch * 4;
            sumPeak   += row[0];
            sumTrough += row[2];
        }
        const double meanPk = sumPeak   / n;
        const double meanTr = sumTrough / n;
        const double amp    = meanPk - meanTr;
        if (amp > maxAmp) maxAmp = amp;
        if (ch == channel) thisAmp = amp;
    }
    if (maxAmp < 1e-6) return false;
    return thisAmp / maxAmp >= static_cast<double>(minSnrRatio);
}
// Mass-filters: if the valley would produce halves smaller than minHalf,
// returns depth=0 (effectively rejecting this candidate).  This is the
// fix for "outlier-driven bimodality" where valley_test's depth formula
// (1 − valley/min(peak_L, peak_R)) gives high values when one mode is a
// tiny outlier cluster.
// ---------------------------------------------------------------------------
struct Scan1D {
    double depth      = 0.0;
    double valley_loc = 0.0;
};

static Scan1D scan_1d(const double* col, int n, int minHalf)
{
    const auto vr = dipsplit::valley_test(col, n, /*threshold=*/0.0);
    Scan1D out;
    if (vr.depth <= 0.0) return out;
    // Mass-filter: count points on each side of valley_loc.
    int nLeft = 0, nRight = 0;
    for (int i = 0; i < n; ++i) {
        if (col[i] < vr.valley_loc) ++nLeft; else ++nRight;
    }
    if (nLeft < minHalf || nRight < minHalf) return out;  // depth stays 0
    out.depth      = vr.depth;
    out.valley_loc = vr.valley_loc;
    return out;
}

// ---------------------------------------------------------------------------
// scan_2d_angle_sweep — at K equally-spaced angles in [0, π), project the
// 2D pair onto a 1D axis and run valley_test.  Returns max depth, the
// winning angle, and the corresponding valley location in projection space.
// The caller-supplied scratch buffer `proj` must hold `n` doubles.
// ---------------------------------------------------------------------------
struct Scan2D {
    double depth      = 0.0;
    double theta      = 0.0;    // winning angle in radians
    double valley_loc = 0.0;
    double cos_theta  = 1.0;
    double sin_theta  = 0.0;
};

static Scan2D scan_2d_angle_sweep(
    const double* xcol, const double* ycol, int n,
    int nAngles, int minHalf, double* proj)
{
    Scan2D best;
    const double dTheta = M_PI / static_cast<double>(std::max(1, nAngles));
    for (int k = 0; k < nAngles; ++k) {
        const double th = dTheta * k;
        const double c  = std::cos(th);
        const double s  = std::sin(th);
        for (int i = 0; i < n; ++i) proj[i] = c * xcol[i] + s * ycol[i];
        const auto vr = dipsplit::valley_test(proj, n, /*threshold=*/0.0);
        if (vr.depth <= best.depth) continue;
        // Mass-filter same as 1D.
        int nLeft = 0, nRight = 0;
        for (int i = 0; i < n; ++i) {
            if (proj[i] < vr.valley_loc) ++nLeft; else ++nRight;
        }
        if (nLeft < minHalf || nRight < minHalf) continue;
        best.depth      = vr.depth;
        best.theta      = th;
        best.cos_theta  = c;
        best.sin_theta  = s;
        best.valley_loc = vr.valley_loc;
    }
    return best;
}

// ---------------------------------------------------------------------------
// CandidateAxis — describes a candidate split axis on one channel.
// "kind" encodes which projection was used so we can apply it consistently
// at split-commit time without re-running the search.
// ---------------------------------------------------------------------------
enum class Kind { Feat1D, Pair2D };

struct CandidateAxis {
    int    channel       = -1;
    Kind   kind          = Kind::Feat1D;
    int    feature1D     = -1;  // valid if kind == Feat1D (0..3)
    int    featA         = -1;  // valid if kind == Pair2D (one of 0/1/2/3)
    int    featB         = -1;
    double cos_theta     = 1.0;
    double sin_theta     = 0.0;
    double valley_loc    = 0.0;
    double valley_depth  = 0.0;

    // The range-normalisation parameters for the channel's 4 features
    // (lo[4], range[4]) so split-commit can recompute the same projection
    // value for each spike without re-doing the sweep.
    double lo[4]    = {0,0,0,0};
    double range[4] = {1,1,1,1};
};

// Compute a single spike's projection value for a CandidateAxis.
static double project_spike(const CandidateAxis& cand,
                            const float* spikeFeatures4)
{
    auto rn = [&](int f) -> double {
        double v = static_cast<double>(spikeFeatures4[f]) - cand.lo[f];
        return (cand.range[f] > 1e-12) ? (v / cand.range[f]) : 0.5;
    };

    if (cand.kind == Kind::Feat1D) {
        return rn(cand.feature1D);
    }
    return cand.cos_theta * rn(cand.featA) + cand.sin_theta * rn(cand.featB);
}

// ---------------------------------------------------------------------------
// scan_channel — evaluate all 1D + 2D candidate projections on one channel.
// Returns the best CandidateAxis (with cand.channel set by caller).
// ---------------------------------------------------------------------------
static CandidateAxis scan_channel(
    const float* fullFeatures, int n, int nChan, int channel,
    const Config& cfg, int nAngles,
    std::vector<double>& scratchA,
    std::vector<double>& scratchB,
    std::vector<double>& scratchProj)
{
    CandidateAxis best;
    best.channel = channel;

    // Range-normalise each of the 4 features for this channel.
    double lo[4]  = {0,0,0,0};
    double rng[4] = {1,1,1,1};
    std::vector<std::vector<double>> norm(4, std::vector<double>(n, 0.0));
    for (int f = 0; f < 4; ++f) {
        for (int i = 0; i < n; ++i)
            norm[f][i] = static_cast<double>(
                fullFeatures[static_cast<ptrdiff_t>(i) * (4 * nChan) +
                             channel * 4 + f]);
        double a = norm[f][0], b = norm[f][0];
        for (double v : norm[f]) { if (v < a) a = v; if (v > b) b = v; }
        lo[f]  = a;
        rng[f] = b - a;
        if (rng[f] < 1e-12) {
            for (auto& v : norm[f]) v = 0.5;
        } else {
            const double inv = 1.0 / rng[f];
            for (auto& v : norm[f]) v = (v - a) * inv;
        }
    }
    for (int f = 0; f < 4; ++f) {
        best.lo[f]    = lo[f];
        best.range[f] = rng[f];
    }

    auto featOn = [&](int f) -> bool {
        switch (f) {
            case 0: return cfg.usePeakAmplitude;
            case 1: return cfg.usePeakTime;
            case 2: return cfg.useTroughAmplitude;
            case 3: return cfg.useTroughTime;
        }
        return false;
    };

    // 1D scans on each enabled feature.
    for (int f = 0; f < 4; ++f) {
        if (!featOn(f)) continue;
        const auto sc = scan_1d(norm[f].data(), n, cfg.minSubClusterSize);
        if (sc.depth > best.valley_depth) {
            best.valley_depth = sc.depth;
            best.kind         = Kind::Feat1D;
            best.feature1D    = f;
            best.featA = best.featB = -1;
            best.valley_loc   = sc.valley_loc;
            best.cos_theta    = 1.0;
            best.sin_theta    = 0.0;
        }
    }

    // 2D angle-sweep on the user's "amplitude + phase" pairs (peak, then
    // trough) plus the (peak_amp, trough_amp) and (peak_time, trough_time)
    // cross-pairs for completeness.  Each call is cheap (18 × valley_test).
    struct Pair { int a; int b; };
    const Pair pairs[] = {
        {0, 1},  // peak amp  + peak time   ← user's primary case
        {2, 3},  // trough amp + trough time
        {0, 2},  // peak amp  + trough amp
        {1, 3},  // peak time + trough time
    };
    for (const auto& pr : pairs) {
        if (!featOn(pr.a) || !featOn(pr.b)) continue;
        scratchProj.assign(static_cast<size_t>(n), 0.0);
        const auto sc = scan_2d_angle_sweep(
            norm[pr.a].data(), norm[pr.b].data(), n, nAngles,
            cfg.minSubClusterSize, scratchProj.data());
        if (sc.depth > best.valley_depth) {
            best.valley_depth = sc.depth;
            best.kind         = Kind::Pair2D;
            best.feature1D    = -1;
            best.featA        = pr.a;
            best.featB        = pr.b;
            best.cos_theta    = sc.cos_theta;
            best.sin_theta    = sc.sin_theta;
            best.valley_loc   = sc.valley_loc;
        }
    }

    (void)scratchA; (void)scratchB;  // reserved for future use
    return best;
}

// ---------------------------------------------------------------------------
// try_split_one_cluster — orchestrate the channel sweep, BIC gate, commit.
// ---------------------------------------------------------------------------
static bool try_split_one_cluster(
    const std::vector<int>& spikeIndices,
    const std::function<bool(int, int16_t*)>& readWaveform,
    int nChan, int nSamples,
    int newClusterId,
    const Config& cfg,
    std::vector<int>& outLabels,
    int& outBestChannel,
    float& outBestDepth,
    const char*& outSkipReason,
    int& outReadFailed)
{
    const int n = static_cast<int>(spikeIndices.size());
    outBestChannel = -1;
    outBestDepth   = 0.0f;
    outSkipReason  = nullptr;

    if (n < cfg.minClusterSize)        { outSkipReason = "minClusterSize"; return false; }
    if (n < 2 * cfg.minSubClusterSize) { outSkipReason = "minSubClusterSize*2"; return false; }
    if (nChan <= 0 || nSamples <= 0)   { outSkipReason = "nChan/nSamples"; return false; }

    // ── 1. Read waveforms and extract per-channel features ──────────
    const int waveSamples = nChan * nSamples;
    const int fullDim     = 4 * nChan;
    std::vector<int16_t> waveBuf(static_cast<size_t>(waveSamples));
    std::vector<float>   fullFeatures(static_cast<size_t>(n) * fullDim, 0.0f);
    int nReadOk = 0;
    for (int i = 0; i < n; ++i) {
        const int p = spikeIndices[static_cast<size_t>(i)];
        if (!readWaveform(p, waveBuf.data())) {
            ++outReadFailed;
            continue;
        }
        extract_per_channel_features(
            waveBuf.data(), nChan, nSamples,
            fullFeatures.data() + static_cast<ptrdiff_t>(i) * fullDim);
        ++nReadOk;
    }
    if (nReadOk < 2 * cfg.minSubClusterSize) {
        outSkipReason = "too many read failures";
        return false;
    }

    // ── 2. Scan each channel for best bimodal projection ─────────────
    const int nAngles = 18;  // 10° increments over [0, π)
    std::vector<double> sA(n), sB(n), sP(n);
    CandidateAxis best;
    best.valley_depth = -1.0;
    for (int ch = 0; ch < nChan; ++ch) {
        // Per-channel SNR gate — skip channels with little signal so we
        // don't pick up noise-driven bimodality on amplitude or timing
        // features.
        if (cfg.minChannelSnrRatio > 0.0f &&
            !channel_has_signal(fullFeatures.data(), n, nChan, ch,
                                cfg.minChannelSnrRatio))
            continue;
        auto cand = scan_channel(fullFeatures.data(), n, nChan, ch,
                                  cfg, nAngles, sA, sB, sP);
        if (cand.valley_depth > best.valley_depth) best = cand;
    }
    outBestChannel = best.channel;
    outBestDepth   = static_cast<float>(best.valley_depth);

    if (best.valley_depth < cfg.valleyThreshold) {
        outSkipReason = "no bimodal channel";
        return false;
    }

    // ── 3. Form tentative labels via sign-vs-valley on the winning axis ──
    std::vector<int> tentative(static_cast<size_t>(n), 0);
    int nLeft = 0, nRight = 0;
    for (int i = 0; i < n; ++i) {
        const float* spikeFeat =
            fullFeatures.data() +
            static_cast<ptrdiff_t>(i) * (4 * nChan) +
            best.channel * 4;
        const double proj = project_spike(best, spikeFeat);
        tentative[static_cast<size_t>(i)] = (proj >= best.valley_loc) ? 1 : 0;
        if (tentative[i] == 0) ++nLeft; else ++nRight;
    }
    if (nLeft < cfg.minSubClusterSize || nRight < cfg.minSubClusterSize) {
        outSkipReason = "imbalanced halves";
        return false;
    }

    // ── 4. BIC gate on the channel's 4D feature vector ──────────────
    // Build standardised X for BIC: (raw - mean) / std per column.  This
    // is the natural feature space for a diagonal-Gaussian BIC and
    // matches how dipsplit::bic_two_vs_one expects its input.
    std::vector<float> X(static_cast<size_t>(n) * 4, 0.0f);
    for (int f = 0; f < 4; ++f) {
        double sum = 0.0, sumSq = 0.0;
        for (int i = 0; i < n; ++i) {
            const double v = fullFeatures[static_cast<ptrdiff_t>(i) * (4*nChan) +
                                          best.channel * 4 + f];
            sum   += v;
            sumSq += v * v;
        }
        const double mean = sum / n;
        const double var  = std::max(1e-12, sumSq / n - mean*mean);
        const double sd   = std::sqrt(var);
        for (int i = 0; i < n; ++i) {
            const double v = fullFeatures[static_cast<ptrdiff_t>(i) * (4*nChan) +
                                          best.channel * 4 + f];
            X[static_cast<size_t>(i) * 4 + f] =
                static_cast<float>((v - mean) / sd);
        }
    }
    const auto bic = dipsplit::bic_two_vs_one(X.data(), n, 4, tentative.data());
    const double bicDelta = bic.bic_k1 - bic.bic_k2;
    const double margin   = static_cast<double>(cfg.bicMarginConstant) +
                            static_cast<double>(cfg.bicMarginPerLogN) *
                            std::log(static_cast<double>(std::max(2, n)));
    if (bicDelta <= margin) {
        outSkipReason = "BIC fails";
        return false;
    }

    // ── 5. Commit: relabel the "right" half ─────────────────────────
    for (int i = 0; i < n; ++i) {
        if (tentative[i] == 1) {
            const int p = spikeIndices[static_cast<size_t>(i)];
            outLabels[static_cast<size_t>(p)] = newClusterId;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------
Result Run(
    const std::vector<std::vector<int>>& clusterSpikes,
    const std::function<bool(int, int16_t*)>& readWaveform,
    int nChan, int nSamples,
    std::vector<int>& labels,
    const Config& cfg)
{
    Result result;
    int nextNewId = cfg.firstNewClusterId;
    if (nextNewId <= 0) {
        int maxLabel = 0;
        for (int lab : labels) maxLabel = std::max(maxLabel, lab);
        nextNewId = maxLabel + 1;
    }

    const int nClusters = static_cast<int>(clusterSpikes.size());
    for (int c = 0; c < nClusters; ++c) {
        if (c == 0) continue;
        const auto& spikes = clusterSpikes[static_cast<size_t>(c)];
        if (spikes.empty()) continue;

        ++result.nClustersConsidered;
        Result::ClusterReport rep;
        rep.clusterId = c;

        int readFailed = 0;
        const bool didSplit = try_split_one_cluster(
            spikes, readWaveform, nChan, nSamples,
            nextNewId, cfg, labels,
            rep.bestChannel, rep.bestValleyDepth, rep.skipReason,
            readFailed);

        result.nSpikesReadFailed += readFailed;
        rep.split = didSplit;
        result.reports.push_back(rep);

        if (didSplit) {
            ++result.nClustersSplit;
            ++result.nNewClusters;
            int moved = 0;
            for (int p : spikes)
                if (labels[static_cast<size_t>(p)] == nextNewId) ++moved;
            result.nSpikesReassigned += moved;
            ++nextNewId;
        }
    }
    return result;
}

}  // namespace per_channel_split
