# Re-extract with a lower threshold

Recovers weak units missed by the first detection pass. The original
high-threshold detection ensures clean templates; this workflow drops
the threshold to find spikes that were just below the cut.

The recipe assumes you have a curated `.clu.N` from a
[first-time sort](first-time-sort.md) and want to add weak units
without disturbing the existing clusters.

## Steps

### 1. Re-extract spikes at the lower threshold

```sh
process_extractspikes_stderiv session.yaml --threshold-factor 4.0
```

(Default is typically 5.0 or 6.0; lowering to 4.0 typically doubles
the spike count. Tune for your noise floor.) The new `.res` and the method's `.spk` will include all originally-detected
spikes plus the new weak ones (the `.spk` already holds the stderiv-domain
waveform; `.fet.stderiv.N` is its projection).

### 2. Run shadow-clustered re-extraction

```sh
ndm_reextractspikes session.yaml --method stderiv
```

This is the key step. For each new spike:

- Computes its Mahalanobis distance to every existing curated
  cluster.
- If it falls within the χ² shadow of a single cluster, assigns it
  there.
- Otherwise marks it **unmatched** — a single oversize cluster ID
  containing every spike that didn't fit any existing template.

Existing curated cluster IDs are preserved unchanged. See
[`ndm_reextractspikes`](../ndmanager-plugins/commands/ndm_reextractspikes.md)
for the full algorithm including the inflation guard and
cross-pipeline shim.

### 3. Sub-cluster the unmatched bin

```sh
ndm_subcluster_unmatched session.yaml
```

Runs KiloKlustaKwik on the unmatched bin only, mapping the resulting
cluster IDs back into the host `.clu.N` above the existing max ID.
**This step automatically picks up empirical priors** when present —
see [Empirical priors](empirical-priors.md).

If the recluster routinely hits the cluster-count cap, bump it for
this run via per-session override:

```yaml
# session.yaml — under spikeDetection.channelGroups.group[N]
extraInfos:
  ndm_subcluster_unmatched:
    maxClusters: 120
```

Per-session override > prior default > script default.

### 4. Curate the new clusters

```sh
klusters session.fet.N
```

The original curated cluster IDs are unchanged. New IDs (above the
old maximum) are the result of sub-clustering the unmatched bin.
Treat them as you would in a fresh sort — split where bimodal, merge
near-duplicates, send weak/noisy clusters to noise/MUA.

The new clusters are typically dimmer and more diffuse than the
originals (that's why they were missed first time). Lower SNR means
DipSplit will rarely fire on them; manual lasso splitting is usually
the right tool. Annotate quality (Uncertain rather than Good is fine
for borderline units).

### 5. Refresh the prior

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
| `MaxClusters` | Up | More residuals ⇒ more clusters. May hit the cap; use per-session override. |
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
