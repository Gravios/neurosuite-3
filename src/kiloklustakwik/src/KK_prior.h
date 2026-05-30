/***************************************************************************
 * KK_prior.h  —  Empirical prior for KlustaKwik CEM initialisation
 *
 * Built offline by scripts/kk_build_prior.py from accumulated Klusters
 * curation logs (.curation_log.N.jl).  Loaded at runtime via -PriorFile.
 *
 * The prior encodes what well-curated clusters look like for a given
 * probe geometry:
 *
 *   • Expected cluster count range  → Min/MaxClusters
 *   • Cluster-type–specific MergeThresh via the d_eff model  (see below)
 *   • Feature dimension importance order  → UseFeatures trimming
 *   • SNR → expected Frobenius variance model  → post-sort QC
 *   • Preseed centres  (chronic recording: previous session's cluster map)
 *
 * d_eff (participation ratio) model
 * ─────────────────────────────────
 * For a cluster with per-feature variances {σ²_0 … σ²_{D-1}} the effective
 * dimensionality is:
 *
 *     d_eff = (Σ σ²_i)² / Σ (σ²_i)²        [participation ratio]
 *
 * This equals D when all dims are equal (distributed / Type B) and 1 when
 * all variance is in one dim (localized / Type A).  The adaptive merge
 * threshold is χ²(d_eff, 0.9999) rather than χ²(nSpatialDims, 0.9999),
 * so Type A pairs get a tight threshold (~18) and Type B pairs get a loose
 * one (~38) automatically.  This is computed in WithinChunkMerge and
 * MergeChunkModels from the already-computed cluster covariances — no prior
 * file is required for this part of the improvement.
 *
 * The prior file additionally provides an empirical calibration point so
 * the user can verify that the adaptive formula agrees with observed
 * inter-cluster distances in curated data.
 ***************************************************************************/

#pragma once

#include <string>
#include <vector>

struct KKPrior {

    bool loaded     = false; ///< false = prior file absent / failed to parse
    /** True when this prior was built from a stderiv (.fetD/.spkD) session.
     *  Preseed centres have n_pca_dims = (nChannels-1)*pcaPerChan when true,
     *  vs nChannels*pcaPerChan for a standard session.  CEMTwoPhase's size
     *  guard will catch a session/prior mismatch; this flag surfaces it. */
    bool is_stderiv      = false;
    /** Electrode group this prior was built from (e.g. 1, 2, …).
     *  0 = unset / built from pooled multi-shank data.
     *  preseed_centres are only applied when ElecNo matches. */
    int  electrode_group  = 0;

    // ── Probe context ──────────────────────────────────────────────────
    int    n_channels    = 0;
    int    n_pca_dims    = 0;
    double sample_rate   = 0.0;
    int    n_sessions    = 0;   ///< sessions that contributed to this prior

    // ── Cluster count empirical distribution ────────────────────────────
    int    n_clusters_p05    = 2;
    int    n_clusters_p25    = 3;
    int    n_clusters_median = 5;
    int    n_clusters_p75    = 7;
    int    n_clusters_p95    = 9;

    // ── d_eff (participation ratio) distribution ─────────────────────────
    // Empirical validation that the adaptive threshold model is on target.
    float  d_eff_p05    = 3.0f;
    float  d_eff_median = 8.0f;
    float  d_eff_p95    = 20.0f;
    // Suggested global MergeThresh = chi²(d_eff_median, 0.9999)
    float  merge_thresh_from_median_d_eff = 30.0f;

    // ── Empirical inter-cluster distance (MNN symmetric Mahalanobis) ──────
    // 5th-percentile of sym-Mahal distances between distinct good clusters.
    // Use as a sanity floor: if adaptive threshold exceeds this value for
    // a pair, the merge would destroy a probably-real boundary.
    float  inter_cluster_sym_mahal_p05 = 20.0f;

    // ── Feature dimension importance ─────────────────────────────────────
    // Fisher discriminant ratio per PCA dimension.
    // kk_build_prior writes this descending-sorted so dim_importance_order[0]
    // is the most discriminating dimension.
    std::vector<float> dim_fisher_ratios;
    std::vector<int>   dim_importance_order;

    // ── SNR → expected Frobenius variance model ───────────────────────────
    // Fit: feat_var_frobenius = snr_A / waveform_snr² + snr_B
    // A cluster with frobenius >> predicted is a merge / drift candidate.
    float  snr_A = 0.0f;
    float  snr_B = 0.0f;
    float  snr_r2 = 0.0f;

    // ── PenaltyMix suggestion ──────────────────────────────────────────────
    float  penalty_mix_suggested = -1.0f;   // -1 = not set (use default)

    // ── Cluster type profiles ─────────────────────────────────────────────
    struct ClusterType {
        std::string label;
        float fraction        = 0.0f;
        float d_eff_median    = 0.0f;
        float chan_spread_med = 0.0f;
        float snr_median      = 0.0f;
        float frobenius_med   = 0.0f;
        float merge_thresh    = 0.0f;  // chi²(d_eff_median, 0.9999)
    };
    std::vector<ClusterType> cluster_types;

    // ── Preseed centres ────────────────────────────────────────────────────
    // Flat layout: [c0_d0, c0_d1, ..., c0_dD, c1_d0, ...]
    // nCentres = preseed_centres.size() / n_pca_dims
    // Empty = no preseed (use farthest-point or ChunkPreseed instead).
    std::vector<float> preseed_centres;
};

/** Load a prior YAML file written by kk_build_prior.py.
 *  Returns a struct with loaded=false on error (non-fatal — caller falls
 *  back to default parameters).  Prints warnings to stderr. */
KKPrior loadKKPrior(const char* path);

/** Apply a loaded prior to the KlustaKwik global parameters.
 *  Only overwrites parameters that were NOT already set explicitly on the
 *  command line (detected by comparing against their compiled defaults).
 *  Always prints a summary of what was applied to Output(). */
void applyKKPrior(const KKPrior& prior);
