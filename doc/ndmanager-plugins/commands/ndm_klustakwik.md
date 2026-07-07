# `ndm_klustakwik` — run KiloKlustaKwik for each spike group

## Synopsis

```bash
ndm_klustakwik [session.yaml] [group]
```

When `session.yaml` is omitted, the current directory name is used as
the session basename. When `group` is omitted, all groups run
sequentially.

**Input:** `session.fet.<method>.N` per spike group (variant selected by `--method`)
**Output:** `session.clu.N` per spike group

## Description

Runs KiloKlustaKwik sequentially for each `spikeDetection` group.
KiloKlustaKwik is internally multi-threaded (OpenMP across chunks and
runs); groups run sequentially to avoid CPU contention. Accepts
the `.fet.<method>.N` for the resolved method per group — the choice is made inside
KiloKlustaKwik at `LoadData` via `pickInputPath`, and the picked variant
is propagated through every subsequent session-file open (refeaturization
checkpoint, template matching, realignment), so stderiv-sorted and
raw-sorted groups can coexist in a single session without
cross-contamination.

## Parameter resolution

Every KiloKlustaKwik parameter is resolved in priority order:

1. `spikeDetection.channelGroups[g].klustakwik.<param>` — per-group
   calibrated values (highest priority). `ndm_aom2dat` writes
   probe-type-calibrated defaults here:
   `MergeThresh = χ²(nCh × 3, 0.9999)`, `MaxClusters` by probe type,
   merge iterations scaled by √(`nSpatialDims`/24).

2. `programs[ndm_klustakwik].parameters.<param>` — global session
   override. Edit to change a parameter for all groups at once.

3. Built-in bash defaults in the `ndm_klustakwik` script.

The same resolution function (`read_kk_param`, hoisted into
`ndm_functions`) is used by `ndm_subcluster_unmatched` so sandbox
re-clustering sees identical parameters to the main sort.

### Calibrated defaults

Written by `ndm_aom2dat` based on probe topology:

| Probe type | `nSpatialDims` | `MergeThresh` | `MaxClusters` | `GlobalMergeIter` |
|---|---|---|---|---|
| linear 16ch | 48 | 93.22 | 40 | 70 |
| tetrode 4ch | 12 | 39.13 | 20 | 50 |
| single 1ch | 3 | 21.11 | 5 | 40 |

### Global session-level parameters

Tier 2 — same for all groups:

```yaml
- name: ndm_klustakwik
  parameters:
  - {name: minClusters,              value: 2,        status: Optional}
  - {name: useFeatures,              value: all,      status: Optional}
  - {name: nStarts,                  value: 1,        status: Optional}
  - {name: nRuns,                    value: 3,        status: Optional}
  - {name: initMethod,               value: farthest, status: Optional}
  - {name: chunkMinutes,             value: 10.0,     status: Optional}
  - {name: chunkOverlapMinutes,      value: 2.0,      status: Optional}
  - {name: chunkPreseedFraction,     value: 0.05,     status: Optional}
  - {name: splitRecurseDepth,        value: 1,        status: Optional}
  - {name: subspaceRecluster,        value: 1,        status: Optional}
  - {name: subspaceReclusterDepth,   value: 2,        status: Optional}
  - {name: subspaceDims,             value: 3,        status: Optional}
  - {name: templateMatchIters,       value: 10,       status: Optional}
  - {name: templateMatchScore,       value: 0.90,     status: Optional}
  - {name: crossChunkTemplateScore,  value: 0.90,     status: Optional}
  - {name: maxIter,                  value: 500,      status: Optional}
```

For full parameter semantics and the chunked-CEM phase diagram, see
[`../../kiloklustakwik/README.md`](../../kiloklustakwik/README.md).

## Examples

```bash
ndm_klustakwik session.yaml      # all groups sequentially
ndm_klustakwik session.yaml 3    # single group
```

## See also

- [`../../kiloklustakwik/README.md`](../../kiloklustakwik/README.md) — full
  KiloKlustaKwik program reference (CEM phases, GPU dispatch, parameters)
- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) — uses
  the same parameter resolution for re-clustering the unmatched bin
- [`kk_build_prior`](kk_build_prior.md) — empirical priors that
  feed `ndm_subcluster_unmatched` (not yet `ndm_klustakwik`)
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
