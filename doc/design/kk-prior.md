# CHANGES — per-probe empirical KlustaKwik priors

Adds the `kk_build_prior.py` / `kk_resolve_prior.py` pair and wires
them into `ndm_subcluster_unmatched`. Lets you train empirical
KlustaKwik defaults once per probe-type-and-shank from accumulated
Klusters curation logs, then reuse them silently across sessions,
animals, and re-extraction passes.

For the operational workflow (bootstrap, build, reuse, refresh,
troubleshooting), see
[`../workflows/empirical-priors.md`](../workflows/empirical-priors.md).

---

## Components

### `kk_build_prior.py` — train a per-probe prior

Reads one or more `.curation_log.<group>.jl` files (the JSON-line
audit trail Klusters writes for every editing operation), filters to
curator-accepted clusters by quality / L-ratio / isolation distance,
and emits `<probe_id>.<group>.prior.yaml` containing:

- Cluster-count distribution (`p05`, `median`, `p95`) → `MinClusters`,
  `MaxClusters` defaults
- Per-cluster effective dimensionality `d_eff` (participation ratio of
  the within-cluster variance vector); median feeds an empirically
  calibrated `MergeThresh` via Wilson-Hilferty χ²(d_eff, 0.9999)
- Per-PCA-dim Fisher discriminant ratios (between-class /
  within-class variance) → `dim_importance_order`
- A SNR-to-variance fit (`frobenius = A/snr² + B`)
- Cluster taxonomy via 4-feature k-means on (`d_eff`,
  top-3 concentration ratio, waveform channel spread, waveform SNR)

### `kk_resolve_prior.py` — locate the right prior

Given a session YAML and electrode group, recomputes the probe
identity and returns the path to the matching prior. Search order:

1. `$NDM_PRIOR_DIR` (colon-separated, like `$PATH`)
2. `~/.ndm/priors`
3. `/etc/ndm/priors`
4. The session yaml's directory

Validates the candidate file's `probe_signature_hash` against the
session signature before returning a path; never silently uses a
mismatched prior.

### `ndm_subcluster_unmatched` — silent integration

Calls `kk_resolve_prior.py` early in the per-group loop. When a prior
is found, four headline values (`MinClusters`, `MaxClusters`,
`MergeThresh`, `PenaltyMix`) become the defaults for `read_kk_param`,
slotting in below per-shank `extraInfos` overrides and above the
script-builtin defaults. Missing prior, missing resolver, or
hash-mismatch all fall through cleanly to script defaults.

---

## Probe identity

The probe identity is computed from the session YAML's channel-group
structure:

```python
signature = {
    "n_channels":     acquisitionSystem.nChannels,
    "channel_groups": [
        {
            "channels":          sorted(group.channels),
            "n_samples":         group.nSamples,
            "peak_sample_index": group.peakSampleIndex,
            "n_features":        group.nFeatures,
        }
        for group in spikeDetection.channelGroups
    ],
}
```

`samplingRate` is **not** part of the signature — it lives in
`acquisitionSystem`, not in the probe definition. Two sessions on the
same physical probe at different rates resolve to the same probe
identity *only* when their `nSamples` happens to match (which
typically it doesn't — a 2 ms window is 64 samples at 32 kHz vs 40 at
20 kHz).

The signature is then SHA-256-hashed (first 16 hex chars) to produce
`probe_signature_hash`. When the signature exactly matches one of the
installed `Template-*.yaml` files
(`/usr/share/ndmanager/templates/` or `$NDM_TEMPLATE_DIR`), the
template's basename (sans `Template-` prefix) is used as a
human-readable `probe_id` like `4-octrodes-32552Hz`. Otherwise the
hex hash is the `probe_id`. Both are stored in every prior file; the
resolver validates against the hash, the friendly name is human
convenience.

---

## File layout

```
~/.ndm/priors/
  4-octrodes-32552Hz.1.prior.yaml     ← shank 1 of 4-octrode probe
  4-octrodes-32552Hz.2.prior.yaml
  4-octrodes-32552Hz.3.prior.yaml
  4-octrodes-32552Hz.4.prior.yaml
  46b75788e3de5d6a.7.prior.yaml       ← shank 7 of a non-template probe
```

A `.prior.yaml` schema head:

```yaml
probe_id: 4-octrodes-32552Hz
probe_signature_hash: 79899eea3d42439f
electrode_group: 1
probe_signature:
  n_channels: 32
  n_pca_dims: 24
  channel_groups: [...]
source:
  n_sessions: 3
  n_clusters: 158
  log_files: [...]
  session_yaml: /data/jg05-20120316/jg05-20120316.yaml
  is_stderiv: true
n_clusters:        {p05: 42, median: 60, p95: 78}
effective_dimensionality:
  d_eff_median: 21.5
  merge_thresh_from_median_d_eff: 55.0
penalty_mix_suggested: 0.030
dim_fisher_ratios: [...]
dim_importance_order: [...]
snr_variance_model:
  A: 4.0
  B: 11.0
cluster_types: [...]
```

---

## Cross-use-case calibration

The four headline parameters port differently between use cases:

| Parameter | Within-probe portability | Notes |
|---|---|---|
| `MergeThresh` | Excellent | Determined by cluster geometry (`d_eff`); independent of extraction threshold |
| `PenaltyMix` | Excellent | AIC/BIC tradeoff is shape-dependent, not data-volume-dependent |
| `MaxClusters` | Use-case-dependent | Lower extraction threshold ⇒ more residuals ⇒ higher cluster count; bump per-session via `extraInfos` if the recluster hits the cap |
| `MinClusters` | Use-case-dependent | Same direction as MaxClusters but less impactful in practice |

For the typical "lower-threshold re-extract" workflow, the existing
prior (built from a higher-threshold sort) is *better than the static
defaults*: `MergeThresh` and `PenaltyMix` transfer cleanly,
`MaxClusters` may need a one-off override. After curating the
lower-threshold sort, regenerate the prior with `kk_build_prior.py`
to recalibrate.

---

## Build / install

The two Python scripts install to `${CMAKE_INSTALL_BINDIR}` alongside
existing ndmanager-plugins helpers, via the existing
`../../src/ndmanager-plugins/scripts/CMakeLists.txt` (just two added entries
to the existing `install(PROGRAMS …)` block).

No new build dependencies. Both scripts use `numpy`, `yaml`, and the
standard library; both are already required for other ndmanager-plugins
scripts.

---

## Files touched

```
src/ndmanager-plugins/scripts/kk_build_prior.py     (new — 750 lines)
src/ndmanager-plugins/scripts/kk_resolve_prior.py   (new — 200 lines)
src/ndmanager-plugins/scripts/ndm_subcluster_unmatched   (modified —
        +50 lines around the read_kk_param block)
src/ndmanager-plugins/scripts/CMakeLists.txt        (modified —
        +2 install entries)
doc/workflows/empirical-priors.md          (new — workflow guide)
doc/ndmanager-plugins/README.md                     (updated)
doc/klusters/README.md                              (updated — DipSplit,
                                                     curation logging)
doc/klustakwik/README.md                            (updated)
doc/README.md                                       (updated)
```
