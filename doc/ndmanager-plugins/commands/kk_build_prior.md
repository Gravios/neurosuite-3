# `kk_build_prior.py` — train a per-probe empirical KiloKlustaKwik prior (`.cur` → `.prior.yaml`)

## Synopsis

```sh
kk_build_prior.py <log_files...> \
    --session-yaml <session.yaml> \
    --electrode-group <N> \
    [--out-dir <prior_dir> | --out <file>] [options]
```

**Input:** one or more Klusters curation logs
`session.cur.<method>.<group>` for the same shank; any session
YAML recorded on the same probe

**Output:** `<probe_id>.<N>.prior.yaml` in `--out-dir` (or the path
given by `--out`)

## Description

Reads the JSON-lines curation logs written by Klusters, keeps the
cluster snapshots the curator accepted, and summarises them into a
prior for one electrode group (shank). A snapshot is accepted when all
of these hold:

- it is a post-action result (`phase: after`, `role: result`) of a
  `GROUP`, `SPLIT`, `SPLIT_N`, `RECLUSTER`, `REALIGN` or `NUDGE` action
  that was not later undone;
- it has at least `--min-spikes` spikes and, when waveforms were
  logged, a positive SNR;
- its L-ratio is ≤ `--max-l-ratio` and its isolation distance is
  ≥ `--min-isolation` (each only when logged);
- its quality annotation, if any, is ≥ `--min-quality`.

The script exits with status 1 if `--electrode-group` is out of range
for the session or no cluster passes the filters.

### Probe identity

The probe identity is computed from the session YAML: total
`acquisitionSystem.nChannels` plus, per `spikeDetection` group, the
sorted channel ids, `nSamples`, `peakSampleIndex` and `nFeatures`. The
sampling rate is not part of it. The SHA-256 of this signature (first
16 hex characters) is the `probe_signature_hash`. If an installed
`Template-*.yaml` has the same signature, its name (without the
`Template-` prefix, e.g. `4-octrodes-32552Hz`) is used as `probe_id`;
otherwise `probe_id` is the hash. Templates are searched in
`--templates-dir`, else `$NDM_TEMPLATE_DIR` (colon-separated),
`/usr/share/ndmanager/templates`, `/usr/local/share/ndmanager/templates`
and the source tree's `src/ndmanager/src`.

## Options

| Option | Default | Meaning |
|---|---|---|
| `log_files` | — | One or more `.cur.<method>.<group>` files for the same shank. |
| `--session-yaml` | required | Session YAML on the target probe; used only for the probe identity. |
| `--electrode-group` | required | 1-based group (shank) the prior is for. |
| `--out-dir` | `.` | Output directory (created if needed); file name `<probe_id>.<N>.prior.yaml`. |
| `--out` | — | Explicit output path; overrides `--out-dir` naming. |
| `--templates-dir` | — | Directory searched for `Template-*.yaml` (repeatable). |
| `--min-quality` | `1` | Minimum quality annotation: 0 = bad, 1 = uncertain, 2 = good. |
| `--max-l-ratio` | `0.1` | Maximum L-ratio of accepted clusters. |
| `--min-isolation` | `15.0` | Minimum isolation distance. |
| `--min-spikes` | `30` | Minimum spikes per accepted cluster. |
| `--n-types` | `3` | Number of cluster types in the taxonomy. |
| `--session-preseed` | — | Curation log from which to extract `preseed_centres` (e.g. the latest session of a chronic series). |

## Output

A YAML file with these top-level keys:

| Key | Content |
|---|---|
| `probe_id`, `probe_signature_hash`, `electrode_group` | Identity checked by `kk_resolve_prior.py`. |
| `probe_signature` | `n_channels`, `n_pca_dims`, `channel_groups`. |
| `source` | Number of sessions and clusters, log files, session YAML. |
| `n_clusters` | `p05`, `p25`, `median`, `p75`, `p95` of the accepted cluster count. |
| `effective_dimensionality` | Percentiles of `d_eff` (participation ratio of the cluster covariance) and `merge_thresh_from_median_d_eff` = χ²(median `d_eff`, 0.9999). |
| `inter_cluster_distance` | `sym_mahal_p05`, `sym_mahal_median`. |
| `dim_fisher_ratios`, `dim_importance_order` | Per-PCA-dimension Fisher discriminant ratios and their ranking. |
| `snr_variance_model` | Fit `frobenius = A / snr² + B` (`A`, `B`, `r2`). |
| `penalty_mix_suggested` | 0.03, or 0.05 when strong localised clusters exceed 40 %. |
| `cluster_types` | Taxonomy of cluster types with fractions and per-type merge thresholds. |
| `preseed_centres` | Flat list of centres from `--session-preseed` (empty otherwise). |

The prior is consumed in two ways:

- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) finds it
  with [`kk_resolve_prior.py`](kk_resolve_prior.md) and uses
  `n_clusters.p05`, `n_clusters.p95`, `merge_thresh_from_median_d_eff`
  and `penalty_mix_suggested` as fallback values for `minClusters`,
  `maxClusters`, `mergeThresh` and `penaltyMix`.
- `KiloKlustaKwik -PriorFile <file>` loads it directly and sets
  `MinClusters = max(2, p05)`, `MaxClusters = p95 + 1`,
  `MergeThresh` and `PenaltyMix` where they are still at the built-in
  defaults, and uses `preseed_centres` when the prior's
  `electrode_group` matches. `ndm_klustakwik` does not pass
  `-PriorFile`.

## See also

- [`kk_resolve_prior.py`](kk_resolve_prior.md) — the lookup counterpart
- [Empirical priors workflow](../../workflows/empirical-priors.md) —
  bootstrap, refresh, troubleshooting
- [`kk-prior` design notes](../../design/kk-prior.md)
- [Curation logging in Klusters](../../klusters/README.md#curation-logging)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
