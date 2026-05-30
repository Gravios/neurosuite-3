// =============================================================================
// proxy_isi.h  —  cluster-free "proxy inter-spike-interval" estimation
//
// Motivation
// ----------
// A unit's spike waveform morphs with recent firing history (Na⁺ channel
// inactivation, K⁺ recovery): bursting spikes are smaller-amplitude and
// broader than rested ones.  To model this within a cluster, the natural
// covariate is the ISI to the previous spike from the SAME unit.
//
// But that's circular: we don't know unit identity at this stage — we're
// trying to use the model to fix the clustering.  Cluster-conditional ISI
// is wrong precisely when contamination is present, which is the case we
// most want to detect.
//
// The trick: replace "previous spike from the same unit" with "most recent
// spike whose features lie within an ε-neighbourhood in feature space".
// Same-unit spikes are close in feature space by construction (modulo the
// adaptation morph we're trying to model, which is the second-order
// correction).  No cluster labels needed.
//
// Algorithm
// ---------
// For each spike i (with time t_i, features f_i ∈ ℝᵈ):
//   1. Scan backward in time-sorted order until t_j < t_i − causalWindowSec.
//   2. Among candidates j (all spikes in that causal window), compute
//      feature-space distance d_ij = ‖f_i − f_j‖.
//   3. Among the K nearest candidates by distance, pick the one with
//      maximum t_j (most recent).  That's the proxy predecessor j*.
//   4. proxy_isi[i] = t_i − t_{j*}.  If no candidates in window or none
//      pass the ε-gate, proxy_isi[i] = +∞ (undefined).
//
// The ε-gate is auto-calibrated per-spike from the local K-th nearest
// distance.  An optional weighted-average variant smooths over noise in
// the strict-nearest choice; controlled by `WeightedVariant`.
//
// Cost: O(N · cand · d) where `cand` is the average number of spikes in
// a causal window.  At 200 Hz × 5 s = 1000 candidates × 25 dims × 1e5
// spikes ≈ 2.5e9 ops; OMP-parallel across the outer loop → seconds on a
// modern CPU.
//
// Failure modes (caller should plan to detect downstream)
// -------------------------------------------------------
//   1. Two similar units overlap in feature space → proxy_isi is too
//      short, adaptation fit comes out with implausibly small τ_c.
//      Caller should disable adaptation correction for such clusters.
//   2. Very low rates (< 1 Hz) → most spikes have no causal predecessor
//      in window; output is +∞ for those.  Caller should fall back to
//      the no-adaptation model.
//   3. Drift period transitions → predecessor may be excluded by ε-gate
//      because the spike has moved in feature space.  Caller can mitigate
//      by computing proxy_isi within drift windows (sub-divide N) rather
//      than across the full session.
// =============================================================================
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace proxy_isi {

// -----------------------------------------------------------------------------
// Sentinel value for undefined proxy_isi (no causal predecessor in window).
// Stored as a float so caller can check with std::isinf().
// -----------------------------------------------------------------------------
constexpr float kUndefined = std::numeric_limits<float>::infinity();

// -----------------------------------------------------------------------------
// Configuration for compute().
// -----------------------------------------------------------------------------
struct Config {
    // Number of feature-space neighbours to consider when picking the proxy
    // predecessor.  Typical: 8–32.  Smaller → fewer candidates to choose
    // among (may miss the true predecessor if it isn't in the top-K);
    // larger → more candidates but more cross-unit pollution of the pool.
    //
    // K only controls candidate-pool size, NOT the ε scale (which uses
    // the nearest d² in the pool, see epsilonFactor below).  So K can be
    // set generously without worrying about scale calibration; the typical
    // value is K = max(8, expected-same-unit-count-per-window).
    int K = 16;

    // Maximum time gap (in seconds) to look back for a predecessor.  Spikes
    // with no candidate inside [t_i − causalWindowSec, t_i) return
    // proxy_isi = kUndefined.  Set to ~5 × the longest adaptation τ you
    // care to model (so default 1.0 s is appropriate for Na⁺-recovery
    // τ < 200 ms).
    float causalWindowSec = 1.0f;

    // ε-gate as a multiplier on the local nearest-neighbour distance.
    // A candidate j qualifies as a possible predecessor iff
    //     ‖f_i − f_j‖ ≤ epsilonFactor × min_{k ∈ heap} ‖f_i − f_k‖
    // i.e., its distance is within epsilonFactor× the distance to the
    // strictly-nearest candidate in the K-NN pool.
    //
    //   epsilonFactor = 1.0  → only the strictly-nearest candidate(s)
    //   epsilonFactor = 3.0  → 3× the nearest distance — typical default,
    //                          covers within-unit feature jitter (factor
    //                          √10 ≈ 3 over the nearest squared distance)
    //   epsilonFactor = +∞   → no ε-gate (use all causal candidates)
    //
    // The nearest-neighbour scale is robust against cross-unit pollution
    // of the K-NN pool: as long as the closest candidate is a same-unit
    // spike (true whenever two units are at all separable in feature
    // space), the scale is correctly set to within-unit distance, and
    // cross-unit candidates at much greater distance are filtered out.
    float epsilonFactor = 3.0f;

    // If true, the output is a feature-distance-weighted average of all
    // qualifying candidates' ISIs:
    //     proxy_isi[i] = Σ_j w_ij · (t_i − t_j) / Σ_j w_ij
    //     w_ij = exp(−d_ij² / (2σ²))   with σ = K-th nearest distance
    // If false (default), output is the strict nearest-predecessor ISI.
    // Weighted variant is more robust on dense recordings; strict variant
    // is faster and matches the conceptual model more directly.
    bool WeightedVariant = false;

    // Verbose logging to stderr (counts of defined / undefined / candidates).
    bool Verbose = false;
};

// -----------------------------------------------------------------------------
// Result.
// -----------------------------------------------------------------------------
struct Result {
    // proxy_isi[i] in seconds, or kUndefined if no causal predecessor in
    // window.  Length: nPoints.
    std::vector<float> isi;

    // proxyPredecessor[i] = global index of the predecessor spike, or -1
    // if undefined.  Length: nPoints.  Useful for downstream diagnostics
    // and for the alternating-LS adaptation fit (which only needs ISIs,
    // not the predecessor identity, but the identity is logged for sanity
    // checks).
    std::vector<int> predecessor;

    // Diagnostics.
    int64_t nDefined   = 0;  // count of spikes with proxy_isi < kUndefined
    int64_t nUndefined = 0;  // count of spikes with proxy_isi = kUndefined
    float   medianIsi  = 0;  // median of defined ISIs (s)
};

// -----------------------------------------------------------------------------
// compute() — main entry point.
//
// Parameters:
//   features     [nPoints × nDimsFeat] point-major.  Caller supplies the
//                feature view EXCLUDING the time dimension (so dims are the
//                spatial PCA features only, length nDimsFeat = nDims − 1).
//   nPoints      number of spikes.
//   nDimsFeat    number of feature dimensions used for KNN.
//   times        [nPoints] raw timestamps in SECONDS, monotonically
//                non-decreasing.  Caller is responsible for converting
//                from KKE's normalised time dim if needed.
//   cfg          configuration; see Config above.
//
// Returns:
//   Result containing isi[], predecessor[], and diagnostics.  Both arrays
//   are sized to nPoints.
//
// Threading:
//   Outer loop over spikes is OMP-parallel.  Per-thread scratch buffers
//   are allocated inside compute(); no external state.
//
// Preconditions:
//   - times[] is sorted ascending.  Function asserts in debug; undefined
//     behaviour in release if violated.
//   - All inputs are non-NaN.  NaN features produce kUndefined output for
//     affected spikes but do not crash.
// -----------------------------------------------------------------------------
Result compute(const float*    features,
               int             nPoints,
               int             nDimsFeat,
               const double*   times,
               const Config&   cfg);

}  // namespace proxy_isi
