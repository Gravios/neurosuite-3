# `ndm_stripdat` — subtract sorted spikes from the raw recording (`.dat` → `-spkclean.dat`)

## Synopsis

```bash
ndm_stripdat [session.yaml]
```

**Input:** `session.dat`; per group `session.res.N` and `session.spk.N`
(required), `session.clu.N` (optional); optional `session.strip.N` sidecars,
`session.dat.drift.P` drift signals and the YAML `units:` block

**Output:** `session-spkclean.dat`; `session.autostrip.N` lists generated
from the YAML curation

## Description

Runs `process_subtractspikes`, which subtracts each selected spike's
waveform from `session.dat` and writes the result to `session-spkclean.dat`.
The original `.dat` is never modified. The cleaned file is used by:

- [`ndm_lfp`](signal-processing.md), which takes `session-spkclean.dat`
  instead of `session.dat` when it exists (unless `inputExtension` names
  another source);
- [`ndm_redetectspikes`](ndm_redetectspikes.md), which looks for spikes
  hidden under larger neighbours in the first pass.

The per-group files are read under their **untagged** names
(`session.res.N`, `session.spk.N`, `session.clu.N`); the method-tagged
`.<type>.<method>.N` files are not looked up. Every group in
`spikeDetection.channelGroups` must have its `.res` and `.spk`, otherwise the
script exits 11 without writing anything. A group without a `.clu` (or whose
`.clu` length does not match the `.res`) is stripped with its raw waveforms,
all spikes included.

If `session-spkclean.dat` already exists and `overwrite` is not `true`, the
step is skipped (exit 10). Delete the file or set `overwrite: true` after
each curation round.

When `session.dat.drift.P` (written by
[`ndm_estimatedrift`](ndm_estimatedrift.md)) exists for the group's probe, it
is passed for drift compensation; `P` is the 1-based position of the group's
`probeId` in the YAML `probes:` list (1 when there is no such list).

### Subtraction modes

| `subtractionMode` | Method |
|---|---|
| `raw` (default) | Subtract each spike's own `.spk` snippet. Near-exact cancellation at spike windows, leaving a "hole" in the noise; no modelling, fast. Best before re-detection. |
| `model` | Per-cluster templates: Layer 1 (mean template scaled by L2 projection, ISI-based burst templates) or Layer 2 (asymmetric-exponential PC1 shape model) when the cluster has enough spikes. Templates are built from spikes without overlapping neighbours. Preserves the noise baseline; suited to LFP extraction. |
| `botm` | Bayes-optimal template matching (Pröpper 2015, §3.4): noise covariance *C* from between-spike periods, matched filter *f* = *C*⁻¹ξ per cluster, amplitude *a* = (xᵀf)/(ξᵀf). Preserves the baseline; slower than `model` (one covariance inversion per group). |

`model` and `botm` need a `.clu`; without one the group falls back to raw
subtraction.

### Which clusters are stripped

For each group the first match wins:

1. `session.strip.N` — hand-written list, one cluster ID per line; blank
   lines and `#` comments allowed.
2. `session.autostrip.N` — when `autoStrip` is `true` and the YAML `units:`
   block has at least one unit for the group whose `quality` matches
   `qualityTags` (case-insensitive, trimmed). The file is regenerated on
   every run with a provenance header; edit `session.strip.N` instead.
3. `stripClusters`.

If the group is curated in `units:` but no unit matches `qualityTags`, no
`autostrip` file is written and `stripClusters` applies. To strip nothing for
such a group, create an empty `session.strip.N`.

`stripClusters` accepts:

| Value | Clusters stripped |
|---|---|
| `default`, `-` or empty | every cluster except 0 (artefact) and 1 (MUA) |
| `all` | every cluster, including 0 and 1 |
| `2,3,5` | the listed IDs |
| `@/path/to/file` | IDs read from the file, one per line |

## Parameters

Set in the `ndm_stripdat` block (see [parameters](../parameters.md)):

| Name | Default | Meaning |
|---|---|---|
| `redetectSpikes` | `no` | `yes`: run [`ndm_redetectspikes`](ndm_redetectspikes.md) after writing the cleaned file. |
| `stripClusters` | `default` | Fallback cluster list (see above). |
| `overwrite` | `false` | `true`: regenerate `session-spkclean.dat` even if it exists. |
| `subtractionMode` | `raw` | `raw`, `model` or `botm`; anything else is an error. |
| `botmRidge` | `1e-3` | `botm` only: fraction of trace(*C*)/dim added to the covariance diagonal before inversion. Raise it if inversion fails on short recordings. |
| `autoStrip` | `true` | Generate `session.autostrip.N` from the YAML `units:` block. Not done for XML parameter files. |
| `qualityTags` | `good,great,excellent` | Comma-separated `quality` labels that count as strip-worthy for `autoStrip`. |

`nChannels`, `samplingRate` and `nBits` (default 16) come from
`acquisitionSystem`; `channels`, `nSamples` and `peakSampleIndex` of each
group from `spikeDetection.channelGroups`.

## Typical iterative workflow

```
ndm_extractspikes → ndm_pca → ndm_klustakwik → (curate in Klusters)
ndm_stripdat                  # strip the trusted units
ndm_redetectspikes            # find missed spikes
ndm_pca → ndm_klustakwik → (curate)
ndm_stripdat                  # overwrite: true, or delete session-spkclean.dat first
ndm_lfp                       # LFP from the final cleaned dat
```

## See also

- [`ndm_redetectspikes`](ndm_redetectspikes.md) — usual next step
- [Iterative refinement](../../workflows/iterative-refinement.md) — strip → re-detect → re-sort recipe
- [BOTM subtraction](../../design/subtractspikes-botm.md), [model vs BOTM](../../design/modeling-l1-vs-botm.md) — design notes
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
