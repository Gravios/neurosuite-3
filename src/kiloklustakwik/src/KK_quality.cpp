/***************************************************************************
                   KK_quality.cpp
                   --------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Cluster-quality metrics, logging and tightness scoring, split out of KK.cpp.
 Member definitions of the KK class declared in KK.h; partitioning them into
 their own translation unit changes no interface and no behaviour.
 ***************************************************************************/
#include "KK.h"
#include "KlustaKwik.h"
#include "klusters_realign.h"  // BuildClusterMedianWaveform

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <omp.h>


// ---------------------------------------------------------------------------
// ReportClusterQuality — print per-phase diagnostics to stderr
//
// Computes a compact summary of cluster-quality metrics and prints them as
// a table.  Designed to be called at phase boundaries (e.g. end of Phase 1,
// end of Phase 7) so a degenerate clustering shows up in the log instead of
// only in the final visual review.
//
// Metrics:
//
//   - **Gini** of cluster sizes.  Range [0, 1].  Gini=0 means perfectly
//     equal cluster sizes; Gini=1 means one cluster has all the spikes.
//     Real recordings have a long tail of low-rate units, so Gini ~ 0.5
//     is normal.  Gini ≥ 0.7 with a known mostly-balanced session is a
//     red flag for an absorbed-bimodal cluster (cluster 3 in jg05 group 6
//     gives Gini ~ 0.55 with 50%-of-spikes in one cluster).
//
//   - **maxFrac** — the fraction of spikes in the single largest cluster.
//     Complements Gini for very simple sanity checks.  > 0.4 with > 10
//     alive clusters is very suspicious.
//
//   - **CondMax** — the largest cluster condition number across alive
//     clusters.  Computed as λ_max(Σ_c) / λ_min(Σ_c) on the diagonal of
//     the Cholesky factor (cheap proxy: ratio of max² to min² of diag(L)).
//     A well-conditioned Gaussian has CondMax < ~1e3; > 1e6 means at
//     least one cluster is borderline-singular.
//
// Cost: O(nClusters) for size stats, O(nClusters · nDims) for condition
// numbers.  Negligible relative to MStep/EStep.
// ---------------------------------------------------------------------------
void KK::ReportClusterQuality(const char* phaseLabel) const {
    if (nClustersAlive < 2) return;   // nothing to report

    // ── Cluster sizes ────────────────────────────────────────────────────
    std::vector<int> sizes;
    sizes.reserve(nClustersAlive);
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const int c = AliveIndex[cc];
        if (c == 0) continue;          // skip noise
        int n = 0;
        for (int p = 0; p < nPoints; ++p)
            if (Class[p] == c) ++n;
        if (n > 0) sizes.push_back(n);
    }
    if (sizes.empty()) return;
    std::sort(sizes.begin(), sizes.end());

    // Gini coefficient via the standard sorted formula:
    //   G = (Σ (2i − n − 1) · x_i) / (n · Σ x_i)
    // where x_i is the i-th sorted value (1-indexed).
    double sum_xi = 0.0, weighted = 0.0;
    for (size_t i = 0; i < sizes.size(); ++i) {
        sum_xi   += static_cast<double>(sizes[i]);
        weighted += static_cast<double>(2 * (int)(i + 1) - (int)sizes.size() - 1)
                  * static_cast<double>(sizes[i]);
    }
    const double gini    = (sum_xi > 0.0)
                         ? weighted / (static_cast<double>(sizes.size()) * sum_xi)
                         : 0.0;
    const int    sumAll  = std::accumulate(sizes.begin(), sizes.end(), 0);
    const double maxFrac = static_cast<double>(sizes.back())
                         / static_cast<double>(std::max(1, sumAll));

    // ── Condition numbers via the Cholesky diagonal ──────────────────────
    // For a symmetric positive-definite Σ = L Lᵀ, the eigenvalues of Σ are
    // bounded by [d_min², d_max²] where d_min = min(diag(L)).  This is a
    // cheap upper bound on the condition number; for diagonal-dominant Σ
    // it is exact.  Sufficient for the "is anything borderline-singular"
    // diagnostic.
    double maxCond = 0.0;
    int    worstCluster = -1;
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const int c = AliveIndex[cc];
        if (c == 0) continue;
        const float* chol = cholFlat.data() + static_cast<size_t>(c) * nDims2;
        float dmin =  std::numeric_limits<float>::infinity();
        float dmax = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < nDims; ++i) {
            const float dii = chol[i * nDims + i];
            if (dii < dmin) dmin = dii;
            if (dii > dmax) dmax = dii;
        }
        if (!(dmin > 0.0f) || !std::isfinite(dmax)) continue;
        const double cond = static_cast<double>(dmax / dmin)
                          * static_cast<double>(dmax / dmin);  // λ-ratio ≈ (d_max/d_min)²
        if (cond > maxCond) { maxCond = cond; worstCluster = c; }
    }

    fprintf(stderr,
            "[%s quality]  alive=%d  spikes=%d  gini=%.3f  maxFrac=%.3f  "
            "condMax=%.2g (cluster %d)\n",
            phaseLabel ? phaseLabel : "phase",
            nClustersAlive - 1,           // exclude noise from "alive" count
            sumAll, gini, maxFrac, maxCond,
            worstCluster);

    // Heuristic warnings — calibrated for typical extracellular spike-sorting
    // output, which has a long-tailed cluster-size distribution (a few high-
    // rate units, many low-rate units).  A long tail naturally produces high
    // Gini even when no single cluster dominates, so high-Gini alone is NOT
    // a failure signature.
    //
    // We previously warned on `gini > 0.7 && maxFrac > 0.4`, intending to
    // catch the absorbed-bimodal failure (e.g. jg05-group-6 cluster 3
    // pre-fix).  But the mass distribution alone can't distinguish that
    // failure from a fast-spiking interneuron legitimately holding most
    // of the mass — both produce identical (gini, maxFrac) signatures.
    // The actual failure-mode signature lives in the WAVEFORM (bimodal
    // valley in a PC projection, elongated covariance), which is exactly
    // what DipSplit's bloat + elongation gates measure.  If those gates
    // pass on a high-mass cluster, the cluster is a real unit, not a
    // failure — and the gini/maxFrac warning was firing on biology.
    //
    // The metrics themselves (gini, maxFrac, condMax) remain in the
    // header line above as informational diagnostics.  Only the false-
    // alarm warning is removed.
    //
    // condMax warns independently — borderline-singular covariance is
    // always a structural concern regardless of cluster-size distribution
    // or any biological interpretation.
    if (maxCond > 1e6)
        fprintf(stderr, "  WARNING: cluster %d condition number ≈ %.2g — "
                        "borderline-singular covariance\n",
                        worstCluster, maxCond);
}



// ---------------------------------------------------------------------------
// KK::LogPerChunkClusterState
//
// Diagnostic: one summary line per phase showing how the chunked label
// state evolves.  Reports:
//   • TOTAL across chunks: sum of distinct non-noise local IDs (upper bound
//     on final unique units; cross-chunk identity comes later in Phase 5/6)
//   • PER-CHUNK K: min/median/max of distinct non-noise local IDs
//   • NOISE: total spikes labelled 0 (cluster 0 = noise convention)
//   • SPIKES: total non-noise vs total
//
// Output format (one line, stderr):
//   [<phase>] CLUSTER STATE: total=N (K/chunk min=a median=b max=c),
//             noise=X/Y spikes (Z.Z%)
//
// Catches the "everything collapsed to noise" failure mode the user
// reported: when total drops to ~0 or noise% jumps to ~100%, the
// preceding phase is the culprit.
// ---------------------------------------------------------------------------
void KK::LogPerChunkClusterState(
    const std::vector<std::vector<int>>& chunkPoints,
    const std::vector<std::vector<int>>& perChunkClass,
    const char* phaseLabel) const
{
    const int nCh = static_cast<int>(perChunkClass.size());
    if (nCh == 0) {
        LockedStderr("[%s] CLUSTER STATE: (no chunks)\n", phaseLabel);
        return;
    }

    int total      = 0;
    int totalSpikes = 0;
    int noiseSpikes = 0;
    std::vector<int> kPerChunk;
    kPerChunk.reserve(static_cast<size_t>(nCh));

    for (int ck = 0; ck < nCh; ck++) {
        const auto& cls = perChunkClass[static_cast<size_t>(ck)];
        totalSpikes += static_cast<int>(cls.size());

        // Bound check chunkPoints sizes (defensive — they should match).
        if (ck < static_cast<int>(chunkPoints.size()) &&
            cls.size() != chunkPoints[static_cast<size_t>(ck)].size()) {
            // size mismatch — skip K count for this chunk but still count spikes
            kPerChunk.push_back(0);
            continue;
        }

        std::unordered_set<int> uniq;
        for (int c : cls) {
            if (c == 0) ++noiseSpikes;
            else        uniq.insert(c);
        }
        kPerChunk.push_back(static_cast<int>(uniq.size()));
        total += static_cast<int>(uniq.size());
    }

    // Per-chunk K stats: min, median, max
    int kMin = INT_MAX, kMax = 0;
    for (int k : kPerChunk) {
        if (k < kMin) kMin = k;
        if (k > kMax) kMax = k;
    }
    if (kPerChunk.empty()) { kMin = 0; }
    std::vector<int> kSorted = kPerChunk;
    std::sort(kSorted.begin(), kSorted.end());
    const int kMedian = kSorted.empty() ? 0 : kSorted[kSorted.size() / 2];

    const double noisePct = (totalSpikes > 0)
        ? 100.0 * static_cast<double>(noiseSpikes) / totalSpikes
        : 0.0;

    LockedStderr(
        "[%s] CLUSTER STATE: total=%d (K/chunk min=%d median=%d max=%d), "
        "noise=%d/%d spikes (%.1f%%)\n",
        phaseLabel, total, kMin, kMedian, kMax,
        noiseSpikes, totalSpikes, noisePct);
}



// ---------------------------------------------------------------------------
// KK::LogGlobalClusterState
//
// Global counterpart to LogPerChunkClusterState.  Called from the
// post-chunked phases (Phase 4, 5, 6, 6a, 6b, 7) where the perChunkClass
// labels have been promoted to global Class[]/ClassAlive[] and chunked
// diagnostics no longer apply.
//
// Output:
//   [<phase>] GLOBAL STATE: alive=N (incl noise), spikes per cluster
//             min=a median=b max=c, noise=X/Y spikes (Z.Z%)
//
// Same collapse-detection role: if alive drops to 1 or noise% goes to
// 100%, the phase between this line and the previous log entry is the
// offender.
// ---------------------------------------------------------------------------
void KK::LogGlobalClusterState(const char* phaseLabel) const
{
    // Defensive: if Class[] hasn't been populated yet (early in the
    // pipeline) the report is meaningless — emit a one-line note so the
    // log shows the call happened but no global state existed.
    if (nPoints <= 0 || !Class.m_Data) {
        LockedStderr("[%s] GLOBAL STATE: (no global Class state yet)\n",
                     phaseLabel);
        return;
    }

    // Count spikes per cluster from Class[].  Don't trust ClassAlive[]
    // alone — a cluster can be marked alive with zero members between
    // certain phase transitions (race between deletion and reassign).
    std::vector<int> sizeByCluster(static_cast<size_t>(MaxPossibleClusters), 0);
    int noiseSpikes = 0;
    int totalSpikes = nPoints;
    for (int p = 0; p < nPoints; ++p) {
        const int c = Class[p];
        if (c >= 0 && c < MaxPossibleClusters) {
            ++sizeByCluster[static_cast<size_t>(c)];
            if (c == 0) ++noiseSpikes;
        }
    }

    // Stats over non-noise non-empty clusters.
    std::vector<int> sizes;
    sizes.reserve(static_cast<size_t>(MaxPossibleClusters));
    int aliveNonNoise = 0;
    for (int c = 1; c < MaxPossibleClusters; ++c) {
        if (sizeByCluster[static_cast<size_t>(c)] > 0) {
            sizes.push_back(sizeByCluster[static_cast<size_t>(c)]);
            ++aliveNonNoise;
        }
    }

    int sMin = 0, sMax = 0, sMedian = 0;
    if (!sizes.empty()) {
        sMin = *std::min_element(sizes.begin(), sizes.end());
        sMax = *std::max_element(sizes.begin(), sizes.end());
        std::vector<int> sorted = sizes;
        std::sort(sorted.begin(), sorted.end());
        sMedian = sorted[sorted.size() / 2];
    }

    const double noisePct = (totalSpikes > 0)
        ? 100.0 * static_cast<double>(noiseSpikes) / totalSpikes
        : 0.0;

    // Single locked write — atomic against Output() under -Screen 1,
    // so 'Deleting Class ... Lose ... but Gain ...' lines no longer
    // splice GLOBAL STATE into their middle.
    LockedStderr(
        "[%s] GLOBAL STATE: alive=%d non-noise clusters, "
        "spikes/cluster min=%d median=%d max=%d, "
        "noise=%d/%d spikes (%.1f%%)\n",
        phaseLabel, aliveNonNoise,
        sMin, sMedian, sMax,
        noiseSpikes, totalSpikes, noisePct);
}



// ---------------------------------------------------------------------------
// KK::FullCemSplitPerChunk  (Phase 4b alternative splitter)
//
// Per-cluster full CEM splitter intended to run inside the Phase 4b
// alternation loop alongside (or instead of) WaveKnnSplitPerChunk.
//
// Differences from Phase 2a's PerClusterCEMPerChunk:
//   * Random shuffle of source clusters per call (rngSeed XOR'd with
//     a salt so different alternation iters surface different
//     clusters).
//   * Optional cap on # sources processed per call
//     (FullCemSplitMaxSourcesPerCall).  Default 0 = unlimited.
//   * No large-cluster subdivision.  The cap controls per-call
//     volume; each picked source is processed whole, mirroring
//     klusters' interactive Recluster-on-one-cluster workflow.
//   * Honours FullCemSplitMinClusterSize as the size floor (or
//     max(nFullDims+5, 25) when that flag is 0).
//
// Same scratch-KK pattern + sub-cluster ID assignment convention as
// PerClusterCEMPerChunk.  See that function for the design rationale
// of the inner per-cluster CEM body — this function deliberately
// duplicates it rather than refactoring, to keep the two phases'
// behaviour independent (Phase 2a can evolve its load-balancing
// strategy without affecting Phase 4b, and vice-versa).
// ---------------------------------------------------------------------------
// KK::ClusterMembershipHash — order-independent signature of a cluster's
// membership + per-spike alignment.  Commutative fold (add of a per-spike
// avalanche mix) of each member's (globalId, m_cumShift[globalId]).  Two
// clusters with the same spike set AND the same shifts hash equal regardless
// of spike order; any membership change or realign changes the hash.
// ---------------------------------------------------------------------------
uint64_t KK::ClusterMembershipHash(const std::vector<int>& globalSpikeIds) const
{
    uint64_t h = 0;
    for (int gid : globalSpikeIds) {
        uint64_t x = static_cast<uint64_t>(static_cast<unsigned>(gid));
        const int sh = (!m_cumShift.empty() && gid >= 0
                        && gid < static_cast<int>(m_cumShift.size()))
                       ? m_cumShift[static_cast<size_t>(gid)] : 0;
        x = x * 1099511628211ull
            + static_cast<uint64_t>(static_cast<unsigned>(sh + 1024));
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
        h += x;   // commutative -> order-independent
    }
    h ^= (static_cast<uint64_t>(globalSpikeIds.size()) << 1);
    return h;
}



// ---------------------------------------------------------------------------
// KK::ClusterISIContamination — fraction of inter-spike intervals below the
// refractory window.  A clean single unit has ~0 (true refractoriness); a
// temporal mixture of >= 2 units has elevated violations because the units
// fire independently and their pooled spike train has short cross-unit gaps.
//
// globalSpikeIds: indices into Data[] (time = last feature dim, raw samples).
// refractorySamples: refractory window in raw samples.
// Returns violations / (nSpikes - 1) in [0, 1]; 0 for < 2 spikes.
// ---------------------------------------------------------------------------
double KK::ClusterISIContamination(const std::vector<int>& globalSpikeIds,
                                   float refractorySamples) const
{
    const int n = static_cast<int>(globalSpikeIds.size());
    if (n < 2 || refractorySamples <= 0.0f) return 0.0;
    const int timeDim = nDims - 1;
    std::vector<double> t;
    t.reserve(static_cast<size_t>(n));
    for (int p : globalSpikeIds)
        t.push_back(static_cast<double>(
            Data[static_cast<size_t>(p) * nDims + timeDim]));
    std::sort(t.begin(), t.end());
    int violations = 0;
    for (int i = 1; i < n; i++)
        if ((t[static_cast<size_t>(i)] - t[static_cast<size_t>(i - 1)])
                < static_cast<double>(refractorySamples))
            ++violations;
    return static_cast<double>(violations) / static_cast<double>(n - 1);
}



// ---------------------------------------------------------------------------
// KK::ClusterWaveformVariance — mean squared deviation of member spikes from
// the cluster's MEDIAN template, normalised per sample.  A clean unit has
// low spread around its template; a waveform mixture (two shapes sharing a
// cluster) has high spread.  Median template (not mean) so a minority shape
// doesn't drag the reference toward itself and mask the spread.
//
// Reads spike waveforms via TimeShiftReadSpikeWave (requires m_timeShiftReady).
// Returns 0 if backing store unavailable or < 2 spikes.
// ---------------------------------------------------------------------------
double KK::ClusterWaveformVariance(const std::vector<int>& globalSpikeIds,
                                   int nChan, int nSamples)
{
    const int n = static_cast<int>(globalSpikeIds.size());
    const int wElems = nChan * nSamples;
    if (n < 2 || wElems <= 0 || !m_timeShiftReady) return 0.0;

    std::vector<int16_t> waves(static_cast<size_t>(n) * wElems);
    int got = 0;
    for (int i = 0; i < n; i++) {
        if (TimeShiftReadSpikeWave(globalSpikeIds[static_cast<size_t>(i)],
                                   wElems,
                                   waves.data() + static_cast<size_t>(got) * wElems))
            ++got;
    }
    if (got < 2) return 0.0;

    std::vector<int16_t> med;
    KlustersRealign::BuildClusterMedianWaveform(
        waves.data(), got, nChan, nSamples, med);
    if (static_cast<int>(med.size()) != wElems) return 0.0;

    double acc = 0.0;
    for (int i = 0; i < got; i++) {
        const int16_t* w = waves.data() + static_cast<size_t>(i) * wElems;
        for (int e = 0; e < wElems; e++) {
            const double d = static_cast<double>(w[e])
                           - static_cast<double>(med[static_cast<size_t>(e)]);
            acc += d * d;
        }
    }
    return acc / (static_cast<double>(got) * wElems);
}



// ---------------------------------------------------------------------------
// ─────────────────────────────────────────────────────────────────────────
// KK::computeClusterTightnessRho — shared waveform-space variance metric
// ─────────────────────────────────────────────────────────────────────────
// ρ = V_res / P_sig on signal-support channels:
//   * mean[e] is the per-element mean of the cluster's read waveforms,
//     channel-major (e = ch*nSamp + s)
//   * Ech[ch]  = Σ_s mean[ch,s]²  (per-channel energy of the template)
//   * signal channels are those with Ech ≥ τ·maxEch (τ = signalChannelFraction)
//   * P_sig    = Σ_{signal ch, s} mean[ch,s]² / nSigElems
//   * V_res    = Σ_{spikes, signal ch, s} (w − mean)² / (ok·nSigElems)
//   * ρ        = V_res / P_sig  (scale-free: amplitude cancels; per-element
//     averaging on the signal support makes concentrated and diffuse
//     footprints comparable)
//
// Returns +inf when ρ cannot be measured (N < 2, ok < 2, maxE = 0, no signal
// support, denom = 0) so the cluster is treated as "untouchable" by callers
// using this metric (Phase 4c masks tight, Phase 8 targets loose — both want
// a +inf to mean "leave alone").
//
// Caller-owned rbuf scratch (size nChan*nSamp) → no shared state → safe to
// call from inside an OMP parallel region with each thread passing its own
// rbuf.
double KK::computeClusterTightnessRho(
    const std::vector<int>&  globalSpikeIds,
    std::vector<int16_t>&    rbuf,
    int nChan, int nSamp, double signalChannelFraction,
    int& nSigOut)
{
    const int wElems = nChan * nSamp;
    const int N      = static_cast<int>(globalSpikeIds.size());
    nSigOut = 1;
    if (N < 2) return std::numeric_limits<double>::infinity();

    std::vector<double> mean(static_cast<size_t>(wElems), 0.0);
    int ok = 0;
    std::vector<std::vector<int16_t>> cache;
    cache.reserve(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) {
        if (!TimeShiftReadSpikeWave(globalSpikeIds[static_cast<size_t>(i)],
                                    wElems, rbuf.data())) continue;
        cache.push_back(rbuf);
        for (int e = 0; e < wElems; ++e)
            mean[static_cast<size_t>(e)] +=
                static_cast<double>(rbuf[static_cast<size_t>(e)]);
        ++ok;
    }
    if (ok < 2) return std::numeric_limits<double>::infinity();
    const double invOk = 1.0 / ok;
    for (int e = 0; e < wElems; ++e) mean[static_cast<size_t>(e)] *= invOk;

    // Per-channel energy → signal support.
    double maxE = 0.0;
    std::vector<double> Ech(static_cast<size_t>(nChan), 0.0);
    for (int ch = 0; ch < nChan; ++ch) {
        double e2 = 0.0;
        for (int s = 0; s < nSamp; ++s) {
            const double m = mean[static_cast<size_t>(ch) * nSamp + s];
            e2 += m * m;
        }
        Ech[static_cast<size_t>(ch)] = e2;
        if (e2 > maxE) maxE = e2;
    }
    if (maxE <= 0.0) return std::numeric_limits<double>::infinity();
    const double tau = std::max(0.0, signalChannelFraction);

    long nSig = 0;
    double Psig = 0.0, Vres = 0.0;
    long long nResElem = 0;
    for (int ch = 0; ch < nChan; ++ch) {
        if (Ech[static_cast<size_t>(ch)] < tau * maxE) continue;
        ++nSig;
        for (int s = 0; s < nSamp; ++s) {
            const int e = ch * nSamp + s;
            const double m = mean[static_cast<size_t>(e)];
            Psig += m * m;
        }
    }
    nSigOut = static_cast<int>(nSig);
    if (nSig == 0) return std::numeric_limits<double>::infinity();
    const long sigElems = nSig * nSamp;
    Psig /= static_cast<double>(sigElems);

    for (const auto& w : cache)
        for (int ch = 0; ch < nChan; ++ch) {
            if (Ech[static_cast<size_t>(ch)] < tau * maxE) continue;
            for (int s = 0; s < nSamp; ++s) {
                const int e = ch * nSamp + s;
                const double d = static_cast<double>(w[static_cast<size_t>(e)])
                               - mean[static_cast<size_t>(e)];
                Vres += d * d;
                ++nResElem;
            }
        }
    if (nResElem == 0 || Psig <= 0.0)
        return std::numeric_limits<double>::infinity();
    Vres /= static_cast<double>(nResElem);
    return Vres / Psig;
}
