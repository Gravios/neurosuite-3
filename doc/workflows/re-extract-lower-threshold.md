# Re-extract with a lower threshold

Recovers weak units missed by the first detection pass. The original
high-threshold detection ensures clean templates; this workflow drops
the threshold to find spikes that were just below the cut.

The recipe assumes you have a curated `.clu.<method>.N` from a
[first-time sort](first-time-sort.md) and want to add weak units
without disturbing the existing clusters. The examples use the
`stderiv` method; use the method of your existing sort throughout.

## Steps

### 1. Re-extract with shadow clustering

```sh
ndm_reextractspikes session.yaml --method stderiv
```

This is the key step. It scans the `.fil` again at a lower threshold —
`reextractThresholdFactor` in the `ndm_reextractspikes` node, default
0.75 × the `ndm_detectspikes` `thresholdFactor` — and ignores any peak
within `reextractMaskHalfWidth` samples of an existing spike, so only
spikes the first pass missed survive. For each new spike it then:

- Computes its robust Mahalanobis distance to every existing curated
  cluster in the reference feature space.
- If the nearest cluster *k* is within the χ² threshold
  (`reextractChi2`, default 0.999), assigns the spike to that cluster's
  **shadow** ID, `maxId + 1 + k` (`maxId` = the highest existing ID).
- Otherwise puts it in the single **unmatched** bin, ID
  `2 × maxId + 2`.

Existing spikes and curated cluster IDs are unchanged; the new spikes
are merged into the `.res`, `.spk.<method>.N`, `.fet.<method>.N` and
`.clu.<method>.N` files, and the originals are kept as `.bak` copies.
See
[`ndm_reextractspikes`](../ndmanager-plugins/commands/ndm_reextractspikes.md)
for the full algorithm including the inflation guard.

### 2. Sub-cluster the unmatched bin

```sh
ndm_subcluster_unmatched session.yaml --method stderiv
```

Runs KiloKlustaKwik on the unmatched bin only and renumbers the
resulting clusters `unmatchedId + 1` … `unmatchedId + K` in the host
`.clu.<method>.N`. Groups without a `.shadowmark` sidecar from step 1
are skipped. The script calls the sorter as `KlustaKwik`; this
repository installs it as `KiloKlustaKwik`, so put a `KlustaKwik` link
to it on your `PATH` (for example
`ln -s "$(command -v KiloKlustaKwik)" ~/.local/bin/KlustaKwik`). **This step automatically picks up empirical priors** when
present — see [Empirical priors](empirical-priors.md). (Setting
`reextractAutoSubcluster: 1` in the `ndm_reextractspikes` node runs
this step at the end of step 1, but it reads the method from the
`ndm_subcluster_unmatched` node of the YAML, not from step 1's
`--method` flag.)

If the recluster routinely hits the cluster-count cap, raise it for
that shank in the session YAML:

```yaml
# session.yaml — under spikeDetection.channelGroups.group[N]
klustakwik:
  maxClusters: 120
```

Each KiloKlustaKwik parameter is taken from, in order: the shank's
`klustakwik` block, `programs[ndm_subcluster_unmatched]`,
`programs[ndm_klustakwik]`, the prior, and the script default.

### 3. Curate the new clusters

```sh
klusters session.fet.stderiv.N
```

The original curated cluster IDs are unchanged. New IDs above the old
maximum are the shadow clusters (new spikes matched to a curated
cluster — usually merged into their parent with `G` after a look) and
the clusters from sub-clustering the unmatched bin.
Treat them as you would in a fresh sort — split where bimodal, merge
near-duplicates, send weak/noisy clusters to noise/MUA.

The new clusters are typically dimmer and more diffuse than the
originals (that's why they were missed first time). Lower SNR means
DipSplit will rarely fire on them; manual lasso splitting is usually
the right tool. Note borderline units as uncertain in the quality field
of the cluster information dialog.

### 4. Refresh the prior

After accepting the lower-threshold sort, regenerate the empirical
prior so future runs use the recalibrated values:

```sh
kk_build_prior.py \
    /data/session_*/session_*.curation_log.N.stderiv \
    --session-yaml /data/session_001/session_001.yaml \
    --electrode-group N \
    --out-dir ~/.ndm/priors
```

The new YAML overwrites the old one in place. See the
[empirical priors workflow](empirical-priors.md) for the full lifecycle.

## What changes about the prior's calibration

If you had built a prior from the original (higher-threshold) sort,
re-extracting at a lower threshold and re-running
`ndm_subcluster_unmatched` produces calibration drift in two of the
four headline parameters:

| Parameter | Drift direction | Impact |
|---|---|---|
| `MergeThresh` | Slightly up | Compact clusters got captured originally; residuals are more diffuse → higher d_eff → higher threshold. Self-correcting if you rebuild the prior after curation. |
| `MaxClusters` | Up | More residuals ⇒ more clusters. May hit the cap; raise it in the shank's `klustakwik` block. |
| `MinClusters` | Up slightly | Same direction; rarely binding in practice. |
| `PenaltyMix` | Stable | AIC/BIC tradeoff is shape-dependent, not data-volume-dependent. |

The existing prior is *better than the static defaults* even with this
drift — `MergeThresh` and `PenaltyMix` transfer cleanly. Just rebuild
the prior after curating the new sort.

## See also

- [`ndm_reextractspikes`](../ndmanager-plugins/commands/ndm_reextractspikes.md)
- [`ndm_subcluster_unmatched`](../ndmanager-plugins/commands/ndm_subcluster_unmatched.md)
- [Empirical priors](empirical-priors.md)
- [`../design/reextract-v2.md`](../design/reextract-v2.md) — design notes
