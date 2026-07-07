# `kk_build_prior.py` — train a per-probe empirical KiloKlustaKwik prior

Reads one or more `.curation_log.<group>.<method>` files (produced by
Klusters), filters to curator-accepted clusters by quality / L-ratio /
isolation distance, and emits `<probe_id>.<group>.prior.yaml`
containing per-shank cluster-count distribution, effective
dimensionality (`d_eff`), empirically-calibrated `MergeThresh`
(Wilson-Hilferty χ² at median d_eff), per-PCA-dim Fisher discriminant
ratios, and a SNR-to-variance fit.

The probe identity is computed from the session YAML's channel-group
structure (sorted channel ids per group, plus `nSamples`,
`peakSampleIndex`, `nFeatures`, and total `nChannels`) and matched
against installed `Template-*.yaml` files for a human-readable
friendly name when available.

## Usage

```sh
kk_build_prior.py <log_files...> \
    --session-yaml <session.yaml> \
    --electrode-group <N> \
    --out-dir <prior_dir>
```

| Argument | Purpose |
|---|---|
| `log_files` | One or more `.curation_log.<group>.<method>` files for the same shank |
| `--session-yaml` | Any session YAML on the target probe — used only to compute probe identity |
| `--electrode-group` | 1-based shank number |
| `--out-dir` | Output directory; default `.` |
| `--templates-dir` | Override search path for `Template-*.yaml` files (repeatable) |
| `--min-quality` | Minimum quality annotation: 0=bad, 1=uncertain, 2=good (default 1) |
| `--max-l-ratio` | Maximum L-ratio for accepted clusters (default 0.1) |
| `--min-isolation` | Minimum isolation distance (default 15.0) |
| `--min-spikes` | Minimum spike count per accepted cluster (default 30) |

## Output

`<probe_id>.<group>.prior.yaml`. The friendly `probe_id` is used when
the session signature matches an installed template (e.g.
`4-octrodes-32552Hz`); otherwise a 16-hex-char SHA-256 hash. Both the
friendly id and the verification hash are stored inside.

## See also

- [`kk_resolve_prior.py`](kk_resolve_prior.md) — the lookup
  counterpart, invoked silently by `ndm_subcluster_unmatched`.
- [Empirical priors workflow](../../workflows/empirical-priors.md) —
  full operational walkthrough.
- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) — the
  primary consumer of priors.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
