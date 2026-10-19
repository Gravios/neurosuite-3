# `ndm_subcluster_unmatched` — re-cluster the unmatched bin (`.clu.<method>.N` → `.clu.<method>.N`)

## Synopsis

```bash
ndm_subcluster_unmatched [session.yaml] [group] [--method M]
```

When `session.yaml` is omitted, the current directory name is used as
the session basename. When `group` is omitted, every group is visited
and those without a shadow-merge sidecar are skipped. `--method`
overrides the `method` parameter.

**Input:** `session.clu.<method>.N`, `session.fet.<method>.N`,
`session.shadowmark.<method>.N` (sidecar written by the re-extract
step), session YAML

**Output:** `session.clu.<method>.N` rewritten in place; backup
`session.clu.<method>.N.bak-sub`; sidecar renamed to
`session.shadowmark.<method>.N.done`

## Description

After [`ndm_reextractspikes`](ndm_reextractspikes.md) merges new spikes
into an existing sort (via `process_shadowcluster`), each group's `.clu`
holds the original clusters, one shadow cluster per parent, and a
single **unmatched** bin with every new spike that was too far
(χ² threshold) from all eligible parents. That bin is usually large and
mixes novel low-amplitude units, collisions and noise.
`ndm_subcluster_unmatched` runs KiloKlustaKwik on just those spikes and
writes the resulting clusters back under new IDs.

Per group:

1. **Gate.** Read `session.shadowmark.<method>.N` (`key value` lines:
   `unmatched_id`, `shadow_offset`, `n_unmatched`, `n_new`). No sidecar
   → the group was not shadow-merged and is skipped. `n_unmatched 0` →
   the sidecar is renamed to `.done` and the group is skipped. A sidecar
   without `unmatched_id` is an error for that group.
   If `session.clu.<method>.N.bak-sub` exists, the group was already
   sub-clustered and is skipped (exit status 10); delete the `.bak-sub`
   to force a re-run.
2. **Sandbox.** The rows of `.fet.<method>.N` whose cluster is
   `unmatched_id` are copied to `session-unmatched-N.fet.<method>.N`. If no row
   carries that id (the `.clu` was edited since the merge), the group is
   skipped.
3. **Sort.** The session YAML is symlinked as `session-unmatched-N.yaml`
   (or `.yml`) so KiloKlustaKwik reads the session's sampling rate and
   geometry, then `KiloKlustaKwik session-unmatched-N N -Method <method>`
   runs, writing `session-unmatched-N.clu.<method>.N`, with the
   resolved parameters, `-UseFeatures all`, `-Log 1`, `-Screen 0`,
   `-fSaveModel 0` and `-SaveIntermediates 0`. The symlink is removed
   whatever the exit status. On failure or an empty result the live
   `.clu` is left untouched.
4. **Merge back.** The live `.clu` is copied to `.bak-sub`, then
   sandbox cluster `k ≥ 2` becomes `unmatched_id + (k − 1)`; spikes
   KiloKlustaKwik put in cluster 0 or 1 stay in the unmatched bin. The
   header is rewritten with the new cluster count.
5. **Clean up.** Sandbox files are deleted and the sidecar is renamed to
   `.shadowmark.<method>.N.done`.

Only the `.clu` changes; `.fet`, `.spk` and `.res` are not touched.
Requires `KiloKlustaKwik` and `python3` (with PyYAML when a prior is used)
on `$PATH`.

The re-extract step can call this command itself: set
`reextractAutoSubcluster: 1` in the `ndm_reextractspikes` block. That
call passes no `--method`, so the `method` in this block must match the
one used for re-extraction.

## Parameters

All KlustaKwik parameters are read with
`read_kk_param -b ndm_subcluster_unmatched`, so for each key the
lookup is: the group's `spikeDetection.channelGroups[g].klustakwik`
block, then this program block, then the `ndm_klustakwik` block, then
the default below. See [parameters](../parameters.md). Each maps to the
KiloKlustaKwik flag of the same name described on
[`ndm_klustakwik`](ndm_klustakwik.md#parameters).

**Default** is the template value of this block; the script's built-in
fallback is given in brackets where it differs.

| Parameter | Default | Meaning |
|---|---|---|
| `method` | `standard` | Selects `.clu/.fet/.shadowmark.<method>.N`; must match the re-extract run. Read from this block only. |
| `minClusters` | `2` | Minimum clusters (prior: `n_clusters.p05`). |
| `maxClusters` | `100` | Maximum clusters (prior: `n_clusters.p95`). |
| `maxPossibleClusters` | `1000` [`500`] | Cluster-table cap; raised because the bin can hold many low-SNR units. |
| `nStarts` | `5` [`3`] | Random restarts (two-phase mode). |
| `nRuns` | `0` | CEM restarts per chunk. |
| `initMethod` | `farthest` | Centre initialisation. |
| `penaltyMix` | `0.25` [`0`] | BIC/AIC blend; a small AIC share admits more clusters in the sparse bin (prior: `penalty_mix_suggested`). |
| `chunkMinutes` | `10` | Chunk length; `0` disables chunking. |
| `chunkOverlapMinutes` | `2` | Chunk overlap. |
| `chunkPreseedFraction` | `0.05` | Phase 0 preseed fraction. |
| `subspaceDims` | `3` | Subspace dimensions for Phase 2.5. |
| `subspaceRecluster` | `1` | Enable per-cluster subspace reclustering. |
| `splitRecurseDepth` | `1` [`0`] | `TrySplits` recursion depth. |
| `templateMatchScore` | `0.85` [`0.0`] | Template-match xcorr threshold; `0` disables. |
| `templateMatchIters` | `10` | Template-match iterations. |
| `crossChunkTemplateScore` | `0.0` | Cross-chunk template-match threshold; `0` disables. |
| `mergeThresh` | `30.0` [`42.0`] | Merge d² threshold; tighter than the main sort so distinct low-amplitude units are not merged (prior: `effective_dimensionality.merge_thresh_from_median_d_eff`). |
| `globalMergeIter` | `50` | EM iterations on the merged model. |
| `timeMergeIter` | `50` | Two-phase merge iterations. |
| `splitEvery` | `25` | Iterations between split attempts. |
| `maxIter` | `500` | Maximum CEM iterations. |
| `distThresh` | `6.9` | Assignment distance threshold. |
| `fullStepEvery` | `10` | Full E-step period. |
| `changedThresh` | `0.05` | Convergence threshold. |
| `nThreads` | `0` | `OMP_NUM_THREADS` for the run; `0` = all cores. |

`log`, `screen`, `fSaveModel`, `saveIntermediates` and `overwrite` are
not read; the sandbox run uses the fixed values listed in step 3.

### Empirical prior

Before reading parameters, the script calls
[`kk_resolve_prior.py`](kk_resolve_prior.md) for the group. When a
matching `<probe_id>.<N>.prior.yaml` is found it logs

```
  group 7: prior /home/<you>/.ndm/priors/4-octrodes-32552Hz.7.prior.yaml
    minC=42 maxC=78 merge=55.0 penaltyMix=0.030
```

and the four prior values (marked "prior:" in the table) replace the
script's built-in fallbacks. They are the **lowest** tier: any value
set in the group block, this block or the `ndm_klustakwik` block wins.
The template sets all four keys in both program blocks, so a prior only
takes effect for keys you remove from the session YAML. If the resolver
is not installed or finds no match, the built-in fallbacks are used.

## See also

- [`ndm_reextractspikes`](ndm_reextractspikes.md) — produces the
  unmatched bin and the sidecar
- [`ndm_klustakwik`](ndm_klustakwik.md) — main sort and flag reference
- [`kk_resolve_prior`](kk_resolve_prior.md) — prior lookup
- [`kk_build_prior`](kk_build_prior.md) — prior training
- [Empirical priors workflow](../../workflows/empirical-priors.md)
- [Re-extract with a lower threshold](../../workflows/re-extract-lower-threshold.md) —
  the typical recipe that ends here
- [`kk-prior` design notes](../../design/kk-prior.md)
- [`.clu` format](../formats/clu.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
