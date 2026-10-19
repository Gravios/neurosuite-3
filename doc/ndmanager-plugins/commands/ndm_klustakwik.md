# `ndm_klustakwik` — run KiloKlustaKwik for each spike group (`.fet.<method>.N` → `.clu.<method>.N`)

## Synopsis

```bash
ndm_klustakwik [session.yaml] [group] [--method M]
```

When `session.yaml` is omitted, the current directory name is used as
the session basename. When `group` is omitted, all `spikeDetection`
groups run sequentially. `--method` overrides the `method` parameter.

**Input:** `session.fet.<method>.N` per spike group, plus the session YAML

**Output:** `session.clu.<method>.N` per spike group (KiloKlustaKwik also
writes its own side files, e.g. the log and, with `fSaveModel: 1`, the
fitted model)

## Description

Runs `KiloKlustaKwik` once per spike group. Groups run one after another;
KiloKlustaKwik itself is multi-threaded (OpenMP across chunks and runs),
so each group gets all cores (see `nThreads`). `OMP_WAIT_POLICY=passive`
is exported for the run so idle OpenMP workers do not spin.

The `method` token (`standard`, `sdiff`, `stderiv`, …) selects the
artifact names: the wrapper checks `session.fet.<method>.N` and
`session.clu.<method>.N`, and passes `-Method <method>` so KiloKlustaKwik
resolves the same names for every session file it opens (features,
waveforms, refeaturization checkpoint, output). Groups sorted under
different methods can therefore coexist in one session. See
[file naming](../formats/naming.md).

`useFeatures` (default `all`) is passed as `-UseFeatures`. `all` uses
every column of the `.fet`, whatever its width. The `.fet` written by
`ndm_pca` (through `process_mergefeatures`) has `nChannels × nFeatures`
PCA columns, `nChannels` peak columns with `extra: true`, and the
timestamp column last, which must be used: the chunked CEM takes the last
dimension as time. A 0/1 mask needs one character per column; a shorter
one drops the columns after it, the timestamp first. (Before 2026-10-19
the wrapper passed `nChannels × nFeatures + 2` ones, one too many by
default, and with `extra: true` too few, which dropped the timestamp.)

A group is skipped (exit status 11) when its `.fet.<method>.N` is
missing, and skipped (exit status 10) when its `.clu.<method>.N` already
exists, unless `overwrite: 1`, which deletes the old `.clu` first. When
all groups are run, these two skip codes do not make the overall run
fail.

## Parameters

Every KiloKlustaKwik parameter is read with `read_kk_param`, so a value
can be set per group in `spikeDetection.channelGroups[g].klustakwik.<name>`
(highest priority) or for all groups in this program block; the
resolution rules are described in [parameters](../parameters.md).
Only `method` is read from the program block alone.

The **Default** column is the value in `templates/template.yaml`, which a
new session YAML inherits. Where the script's built-in fallback (used
only when the key is absent from the YAML altogether) differs, it is
given in brackets.

| Parameter | Default | Meaning (KiloKlustaKwik flag) |
|---|---|---|
| `method` | `standard` | Chain-of-custody method; selects `.fet.<method>.N` / `.clu.<method>.N` and is passed as `-Method`. |
| `minClusters` | `2` | `-MinClusters`: minimum number of clusters. |
| `maxClusters` | `100` | `-MaxClusters`: maximum number of clusters to fit. |
| `maxPossibleClusters` | `500` | `-MaxPossibleClusters`: hard cap on the cluster table (also sizes the packed per-chunk ID range); must be ≥ `maxClusters`. |
| `nStarts` | `3` | `-nStarts`: random restarts in non-chunked (two-phase) mode. |
| `nRuns` | `0` | `-nRuns`: independent CEM restarts per chunk in chunked mode; the best-scoring run per chunk is kept. `0` = one run per chunk. |
| `useFeatures` | `all` | `-UseFeatures`: the `.fet` columns to cluster on (see above). Left out of the template. |
| `initMethod` | `farthest` | `-InitMethod`: `farthest` or `random` centre initialisation. |
| `penaltyMix` | `0` | `-PenaltyMix`: `0` = BIC penalty, `1` = AIC/CEM penalty, values in between blend. |
| `chunkMinutes` | `10` | `-ChunkMinutes`: chunk length; `0` disables chunking (two-phase CEM on the whole recording). |
| `chunkOverlapMinutes` | `2` | `-ChunkOverlapMinutes`: overlap between adjacent chunks. |
| `chunkPreseedFraction` | `0.05` | `-ChunkPreseedFraction`: fraction of spikes used for the Phase 0 global preseed; `0` disables. |
| `subspaceDims` | `0` [`3`] | `-SubspaceDims`: number of top eigenvectors for subspace reclustering; `0` = all spatial dimensions. |
| `subspaceRecluster` | `0` [`1`] | `-SubspaceRecluster`: `1` enables per-cluster subspace reclustering (Phase 2.5). |
| `templateMatchScore` | `0.0` | `-TemplateMatchScore`: waveform-xcorr threshold for template matching; `0` disables it. |
| `templateMatchIters` | `10` | `-TemplateMatchIters`: maximum template-matching iterations. |
| `crossChunkTemplateScore` | `0.0` | `-CrossChunkTemplateScore`: xcorr threshold for cross-chunk template matching; `0` disables it. |
| `splitRecurseDepth` | `1` [`0`] | `-SplitRecurseDepth`: recursion depth of `TrySplits`. |
| `mergeThresh` | `42.0` | `-MergeThresh`: maximum Mahalanobis d² for merging cluster models across chunks; calibrate to χ²(nDims, p). |
| `globalMergeIter` | `50` | `-GlobalMergeIter`: EM iterations on the globally merged model. |
| `timeMergeIter` | `50` | `-TimeMergeIter`: Phase 2 merge iterations in two-phase mode; `0` disables. |
| `splitEvery` | `25` | `-SplitEvery`: CEM iterations between split attempts. |
| `maxIter` | `500` | `-MaxIter`: maximum CEM iterations. |
| `distThresh` | `6.9` | `-DistThresh`: Mahalanobis distance threshold for spike-to-cluster assignment. |
| `fullStepEvery` | `10` | `-FullStepEvery`: full E-step every N iterations. |
| `changedThresh` | `0.05` | `-ChangedThresh`: convergence when fewer than this fraction of spikes change cluster. |
| `log` | `1` | `-Log`: write the KiloKlustaKwik log file. |
| `screen` | `0` | `-Screen`: print progress to stdout. |
| `fSaveModel` | `1` | `-fSaveModel`: save the fitted mixture model. |
| `saveIntermediates` | `0` | `-SaveIntermediates`: write a `.clu` each time a new best score is found. |
| `overwrite` | `0` | `1` deletes an existing `.clu.<method>.N` and re-sorts the group; `0` skips it. |
| `nThreads` | `0` | Exported as `OMP_NUM_THREADS` for the run; `0` = all cores (`nproc`). |

Older templates also listed `phase15Iters` and `subspaceReclusterDepth`.
KiloKlustaKwik no longer has those options (`-Phase15Iters` became
`-TimeShiftAlignIter`, which is off unless a `-TimeShiftAlignAfterPhase*`
switch is set; `-SubspaceReclusterDepth` was removed), so it ignored them;
they are no longer read and can be deleted from a session file.

For the algorithm behind these flags (chunked CEM phases, template
matching, subspace reclustering, GPU dispatch) see the
[KiloKlustaKwik reference](../../kiloklustakwik/README.md).

### Per-group calibrated values

`process_aomconvert`, run directly without `--no-yaml`, writes a
per-group `klustakwik:` block into each `spikeDetection` group of the YAML
it creates. `ndm_aom2dat` always passes `--no-yaml`, so it does not write
these blocks. The values are calibrated from the group size
(`nSpatialDims = nChannels × 3`):

- `mergeThresh` = χ²(`nSpatialDims`, 0.9999) (Wilson–Hilferty approximation)
- `maxClusters` = 20 for 4-channel groups (`tetrode`), 5 for 1-channel
  groups (`single`), 40 otherwise (`linear`)
- `maxPossibleClusters` = ⌈duration / 10 min⌉ × `maxClusters` + 50
- `globalMergeIter` = `timeMergeIter` = max(40, 50·√(max(`nSpatialDims`, 24)/24), rounded to 10)

| Group | `nSpatialDims` | `mergeThresh` | `maxClusters` | `globalMergeIter` / `timeMergeIter` |
|---|---|---|---|---|
| 16 channels | 48 | 93.39 | 40 | 70 |
| tetrode (4 ch) | 12 | 39.50 | 20 | 50 |
| single (1 ch) | 3 | 21.84 | 5 | 50 |

```yaml
spikeDetection:
  channelGroups:
  - channels: [0, 1, 2, 3]
    nFeatures: 3
    klustakwik:
      maxClusters: 20
      mergeThresh: 39.50
```

### External sorter: `ndm_fiber-kit`

The external fiber-kit sorter is not run by this command. It plugs into
NDManager as its own program, `ndm_fiber-kit`, through the ndm plugin
protocol (v1): NDManager scans `$NDM_PLUGIN_PATH` and then `$PATH` for
`ndm_*` executables, keeps those whose `--ndm-version` prints
`ndm-plugin-protocol <N>` with a supported `N`, and loads the program
description each one prints for `--ndm-describe`. Its parameters live in
the `ndm_fiber-kit` program block of the template, flattened as
`<stage>.<knob>` (e.g. `intrachunk.cos_thr`).

## Examples

```bash
ndm_klustakwik session.yaml                    # all groups, method from YAML
ndm_klustakwik session.yaml 3                  # group 3 only
ndm_klustakwik session.yaml --method stderiv   # sort .fet.stderiv.N
```

## See also

- [KiloKlustaKwik reference](../../kiloklustakwik/README.md) — CEM
  phases, GPU dispatch, full flag list
- [Parameters](../parameters.md) — how `read_kk_param` resolves values
- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) — re-clusters
  the unmatched bin with the same parameter resolution
- [`ndm_autoklusta`](ndm_autoklusta.md) — learned curation of the sort
- [`kk_build_prior`](kk_build_prior.md) — empirical priors (used by
  `ndm_subcluster_unmatched`, not by this command)
- [`.clu` format](../formats/clu.md), [`.fet` format](../formats/fet.md)
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
