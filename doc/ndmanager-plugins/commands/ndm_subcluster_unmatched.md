# `ndm_subcluster_unmatched` — re-cluster the unmatched bin

## Synopsis

```bash
ndm_subcluster_unmatched [session.yaml] [group]
```

When `session.yaml` is omitted, the current directory name is used as
the session basename. When `group` is omitted, all groups are
processed sequentially.

**Input:** `session.clu.N`, `session.fet.N` (or `.fetD.N`), `session.spk.N` (or `.spkD.N`), `session.res.N`
**Output:** updated `session.clu.N` (with new IDs above the prior maximum), backup of the original

## Description

After a `ndm_reextractspikes*` pass, each group's `.clu.N` contains a
single large "unmatched" cluster holding every new spike that exceeded
the χ² threshold from every eligible parent. In typical sessions this
bin is heterogeneous — mixing genuinely novel units with
tail-of-distribution outliers from existing units.

`ndm_subcluster_unmatched` extracts the unmatched bin to a sandbox
`.fet` / `.res` / `.spk` triple, runs a full KlustaKwik invocation on
it (27 parameters, parity with `ndm_klustakwik` via the shared
`read_kk_param` function), maps the resulting cluster IDs back into
the host session's `.clu.N` above the existing max ID, and preserves
backups. Designed to be run once per group after every reextract
pass.

## Parameter resolution

The headline parameters (`MinClusters`, `MaxClusters`, `MergeThresh`,
`PenaltyMix`) follow this priority order, highest first:

1. Per-shank override in `extraInfos.ndm_subcluster_unmatched` of the
   session YAML (per-recording fine-tuning)
2. Per-shank prior file value resolved via `kk_resolve_prior.py`
   (probe-typical default)
3. Script-builtin default (used if no prior matches)

When a prior is found, the script prints:

```
  group 7: prior /home/<you>/.ndm/priors/4-octrodes-32552Hz.7.prior.yaml
    minC=42 maxC=78 merge=55.0 penaltyMix=0.030
```

If `kk_resolve_prior.py` is missing, returns no match, or returns a
hash-mismatch error, the script falls through to its built-in defaults
exactly as before. **Behaviour with no priors is unchanged.**

The remaining 23 KlustaKwik parameters share resolution with
[`ndm_klustakwik`](ndm_klustakwik.md) — see that page for the full
list and tier-1 calibrated defaults.

## Per-session override example

```yaml
# session.yaml — under spikeDetection.channelGroups.group[N]
extraInfos:
  ndm_subcluster_unmatched:
    maxClusters: 120        # bumps the cap when the recluster hits it
```

## See also

- [`ndm_reextractspikes`](ndm_reextractspikes.md) — produces the
  unmatched bin that this command sub-clusters
- [`kk_resolve_prior`](kk_resolve_prior.md) — looks up the matching
  prior, called silently
- [`kk_build_prior`](kk_build_prior.md) — trains the priors
- [Empirical priors workflow](../../workflows/empirical-priors.md) —
  full operational walkthrough
- [Re-extract with a lower threshold](../../workflows/re-extract-lower-threshold.md) —
  the typical recipe that ends here
- [`../../design/kk-prior.md`](../../design/kk-prior.md) — design
  notes for the prior pipeline

---

*Part of the [ndmanager-plugins](../README.md) reference.*
