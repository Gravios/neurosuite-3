/***************************************************************************
 * KK_prior.cpp  —  KlustaKwik empirical prior loader
 ***************************************************************************/

#include "KK_prior.h"
#include "KlustaKwik.h"   // global params (MinClusters, MergeThresh, ...)

#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// loadKKPrior
// ---------------------------------------------------------------------------
KKPrior loadKKPrior(const char* path)
{
    KKPrior p;
    if (!path || path[0] == '\0') return p;

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        fprintf(stderr, "KlustaKwik: cannot load prior '%s': %s\n", path, e.what());
        return p;
    }

    auto safeInt   = [](const YAML::Node& n, int   def){ return n ? n.as<int>(def)   : def; };
    auto safeFloat = [](const YAML::Node& n, float def){ return n ? n.as<float>(def) : def; };

    try {
        // ── Probe context ──────────────────────────────────────────────
        if (auto ps = root["probe_signature"]) {
            p.n_channels  = safeInt(ps["n_channels"],  0);
            p.n_pca_dims  = safeInt(ps["n_pca_dims"],  0);
            p.sample_rate = ps["sample_rate"] ? ps["sample_rate"].as<double>(0.0) : 0.0;
        }
        if (root["source"]) {
            p.n_sessions  = safeInt(root["source"]["n_sessions"], 0);
            if (root["source"]["is_stderiv"])
                p.is_stderiv = root["source"]["is_stderiv"].as<bool>(false);
            if (root["source"]["electrode_group"])
                p.electrode_group = safeInt(root["source"]["electrode_group"], 0);
        }

        // ── Cluster count ──────────────────────────────────────────────
        if (auto nc = root["n_clusters"]) {
            p.n_clusters_p05    = safeInt(nc["p05"],    p.n_clusters_p05);
            p.n_clusters_p25    = safeInt(nc["p25"],    p.n_clusters_p25);
            p.n_clusters_median = safeInt(nc["median"], p.n_clusters_median);
            p.n_clusters_p75    = safeInt(nc["p75"],    p.n_clusters_p75);
            p.n_clusters_p95    = safeInt(nc["p95"],    p.n_clusters_p95);
        }

        // ── d_eff distribution ─────────────────────────────────────────
        if (auto de = root["effective_dimensionality"]) {
            p.d_eff_p05    = safeFloat(de["d_eff_p05"],    p.d_eff_p05);
            p.d_eff_median = safeFloat(de["d_eff_median"], p.d_eff_median);
            p.d_eff_p95    = safeFloat(de["d_eff_p95"],    p.d_eff_p95);
            p.merge_thresh_from_median_d_eff =
                safeFloat(de["merge_thresh_from_median_d_eff"],
                          p.merge_thresh_from_median_d_eff);
        }

        // ── Inter-cluster distance ─────────────────────────────────────
        if (auto ic = root["inter_cluster_distance"])
            p.inter_cluster_sym_mahal_p05 =
                safeFloat(ic["sym_mahal_p05"], p.inter_cluster_sym_mahal_p05);

        // ── Feature dimension importance ───────────────────────────────
        if (auto fr = root["dim_fisher_ratios"]) {
            p.dim_fisher_ratios.clear();
            for (const auto& v : fr)
                p.dim_fisher_ratios.push_back(v.as<float>(0.0f));
        }
        if (auto io = root["dim_importance_order"]) {
            p.dim_importance_order.clear();
            for (const auto& v : io)
                p.dim_importance_order.push_back(v.as<int>(0));
        }

        // ── SNR → variance model ───────────────────────────────────────
        if (auto sv = root["snr_variance_model"]) {
            p.snr_A  = safeFloat(sv["A"],  0.0f);
            p.snr_B  = safeFloat(sv["B"],  0.0f);
            p.snr_r2 = safeFloat(sv["r2"], 0.0f);
        }

        // ── PenaltyMix ─────────────────────────────────────────────────
        if (root["penalty_mix_suggested"])
            p.penalty_mix_suggested = root["penalty_mix_suggested"].as<float>(-1.0f);

        // ── Cluster types ──────────────────────────────────────────────
        if (auto ct = root["cluster_types"]) {
            for (const auto& t : ct) {
                KKPrior::ClusterType typ;
                if (t["label"])         typ.label         = t["label"].as<std::string>();
                if (t["fraction"])      typ.fraction      = t["fraction"].as<float>(0.0f);
                if (t["d_eff_median"])  typ.d_eff_median  = t["d_eff_median"].as<float>(0.0f);
                if (t["waveform_chan_spread_median"])
                    typ.chan_spread_med = t["waveform_chan_spread_median"].as<float>(0.0f);
                if (t["waveform_snr_median"])
                    typ.snr_median = t["waveform_snr_median"].as<float>(0.0f);
                if (t["feat_var_frobenius_median"])
                    typ.frobenius_med = t["feat_var_frobenius_median"].as<float>(0.0f);
                if (t["merge_thresh_for_type"])
                    typ.merge_thresh  = t["merge_thresh_for_type"].as<float>(0.0f);
                p.cluster_types.push_back(std::move(typ));
            }
        }

        // ── Preseed centres ────────────────────────────────────────────
        if (auto pc = root["preseed_centres"]) {
            p.preseed_centres.clear();
            for (const auto& v : pc)
                p.preseed_centres.push_back(v.as<float>(0.0f));
        }

        p.loaded = true;
        fprintf(stderr, "KlustaKwik: loaded prior from '%s' (%d sessions, "
                        "%d cluster types)\n",
                path, p.n_sessions, (int)p.cluster_types.size());
    } catch (const YAML::Exception& e) {
        fprintf(stderr, "KlustaKwik: error parsing prior '%s': %s\n", path, e.what());
        p.loaded = false;
    }
    return p;
}

// ---------------------------------------------------------------------------
// applyKKPrior
// ---------------------------------------------------------------------------
// Compiled-default sentinel values.  A parameter equals its default when
// it was not set on the command line, so it's safe to override from the prior.
static constexpr int   kDefaultMinClusters = 2;
static constexpr int   kDefaultMaxClusters = 10;
static constexpr float kDefaultMergeThresh = 30.0f;
static constexpr float kDefaultPenaltyMix  = 0.0f;

void applyKKPrior(const KKPrior& prior)
{
    if (!prior.loaded) return;

    fprintf(stderr, "\nKlustaKwik prior application:\n");

    // ── Cluster count ──────────────────────────────────────────────────
    if (MinClusters == kDefaultMinClusters) {
        MinClusters = std::max(2, prior.n_clusters_p05);
        fprintf(stderr, "  MinClusters  <- %d  (prior p05)\n", MinClusters);
    } else {
        fprintf(stderr, "  MinClusters     = %d  (command-line, not overridden)\n",
                MinClusters);
    }
    if (MaxClusters == kDefaultMaxClusters) {
        // p95 + 1 gives one extra cluster worth of headroom
        MaxClusters = prior.n_clusters_p95 + 1;
        if (MaxClusters > MaxPossibleClusters)
            MaxClusters = MaxPossibleClusters;
        fprintf(stderr, "  MaxClusters  <- %d  (prior p95+1)\n", MaxClusters);
    } else {
        fprintf(stderr, "  MaxClusters     = %d  (command-line, not overridden)\n",
                MaxClusters);
    }

    // ── MergeThresh ────────────────────────────────────────────────────
    // The adaptive d_eff mechanism (enabled by AdaptiveMerge=1) handles
    // per-pair calibration at runtime.  Here we set the global MergeThresh
    // as the calibrated median d_eff value, which acts as the floor for the
    // adaptive path and as the fallback when AdaptiveMerge=0.
    if (MergeThresh == kDefaultMergeThresh &&
        prior.merge_thresh_from_median_d_eff > 0.0f) {
        MergeThresh = prior.merge_thresh_from_median_d_eff;
        fprintf(stderr, "  MergeThresh  <- %.1f  (chi2(d_eff_median=%.1f, 0.9999))\n",
                MergeThresh, prior.d_eff_median);
    } else {
        fprintf(stderr, "  MergeThresh     = %.1f  (command-line or default, not overridden)\n",
                MergeThresh);
    }

    // ── PenaltyMix ─────────────────────────────────────────────────────
    if (PenaltyMix == kDefaultPenaltyMix && prior.penalty_mix_suggested >= 0.0f) {
        PenaltyMix = prior.penalty_mix_suggested;
        fprintf(stderr, "  PenaltyMix   <- %.3f  (prior suggestion)\n", PenaltyMix);
    }

    // ── Preseed centres ────────────────────────────────────────────────
    // Dimension guard: stderiv priors have n_pca_dims=(nChan-1)*pcaK while
    // standard priors have nChan*pcaK.  CEMTwoPhase already has a size guard
    // (falls back to farthest-point when size < nCentres*nSpatialDims), but
    // we warn explicitly so mismatches are diagnosable.
    if (!prior.preseed_centres.empty()) {
        if (prior.n_pca_dims <= 0) {
            fprintf(stderr, "  preseedCentres: SKIPPED"
                            " — prior n_pca_dims=%d (malformed; rebuild prior)\n",
                            prior.n_pca_dims);
        } else if (prior.electrode_group != 0 && prior.electrode_group != ElecNo) {
            // preseed_centres are PCA-space coordinates specific to the electrode
            // group the prior was built from.  Each group has its own independent
            // PCA basis, so centres from group %d are meaningless in group %d.
            // The transferable fields (MergeThresh, PenaltyMix, d_eff) are
            // still applied above; only preseed is skipped.
            fprintf(stderr, "  preseedCentres: SKIPPED"
                            " — prior electrode_group=%d, running ElecNo=%d\n",
                            prior.electrode_group, ElecNo);
        } else {
            const int nCentres = static_cast<int>(prior.preseed_centres.size())
                               / prior.n_pca_dims;
            fprintf(stderr, "  preseedCentres <- %d centres x %d dims"
                            " (is_stderiv=%s, electrode_group=%d)\n",
                            nCentres, prior.n_pca_dims,
                            prior.is_stderiv ? "yes" : "no",
                            prior.electrode_group);
            ExternalPreseedCentres = prior.preseed_centres;
        }
    }

    fprintf(stderr, "  d_eff range: p05=%.1f  median=%.1f  p95=%.1f\n",
            prior.d_eff_p05, prior.d_eff_median, prior.d_eff_p95);
    if (prior.snr_r2 > 0.0f)
        fprintf(stderr, "  SNR→variance model: frobenius = %.0f/snr² + %.0f  (R²=%.2f)\n",
                prior.snr_A, prior.snr_B, prior.snr_r2);
    if (!prior.cluster_types.empty()) {
        fprintf(stderr, "  Cluster types:\n");
        for (const auto& t : prior.cluster_types)
            fprintf(stderr,
                    "    %-22s  %.0f%%  d_eff=%.1f  snr=%.1f  merge_thresh=%.1f\n",
                    t.label.c_str(),
                    t.fraction * 100.0f,
                    t.d_eff_median,
                    t.snr_median,
                    t.merge_thresh);
    }
    fprintf(stderr, "\n");
}
