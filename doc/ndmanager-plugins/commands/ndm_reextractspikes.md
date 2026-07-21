# `ndm_reextractspikes`

Masked second-pass detection + shadow-clustered merge (method-aware).

## Synopsis

```bash
ndm_reextractspikes [session.yaml] [--method M] [--methodOrder N]
```

Processes every group that has the full set of inputs. `--method`
(`standard` | `sdiff` | `stderiv`) selects the detection engine and the
dotted artifact names; it overrides the `method` field on the
`ndm_reextractspikes` node, which in turn defaults to `standard`. This is
the same three-tier resolution used by `ndm_extractspikes` and `ndm_pca`.
The former `ndm_reextractspikes_stderiv` is folded in as `--method
stderiv`.

**Input:** `session.fil`, `session.res.<method>.N`, `session.clu.<method>.N`, `session.fet.<method>.N`, `session.pca.<method>.N` (`.res` is resolved across methods)
**Output:** updated `session.res.<method>.N`, `session.spk.<method>.N`, `session.clu.<method>.N`, `session.fet.<method>.N`; a `session.shadowmark.<method>.N` sidecar; per-group backups (`*.bak`)

## Description

Second-pass spike detection that runs at a reduced threshold while
masking out timestamps already present in `.res`. New spikes are then
shadow-clustered into the existing `.clu`: each new spike is assigned by
robust Mahalanobis distance (per-dim median + MAD × 1.4826) in PCA
feature space to the nearest eligible parent cluster; spikes exceeding
the χ² threshold from every parent land in a single "unmatched" bin
(re-cluster it with [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md)).

The `method` selects the detection engine, exactly as in `ndm_pca`:

- `standard` / `sdiff` — `process_reextractspikes`, thresholds computed
  externally with `process_medianthreshold` and scaled by
  `reextractThresholdFactor`. `sdiff` shares the raw engine because its
  waveforms are raw, the same treatment `ndm_pca` gives it — the only
  difference from `standard` is the dotted name the artifacts carry.
- `stderiv` — `process_reextractspikes_stderiv`, detection on the stderiv
  signal (spatial + temporal first-difference, matching
  `ndm_extractspikes` with `method: stderiv`); thresholds are computed
  internally over `[start, start+duration)`. Waveforms are written in
  stderiv space so they stay in the same value space as the reference
  rows and the `pca.stderiv.N` basis they are projected through.

Because `process_reextractspikes` and `process_shadowcluster` take a stem
and hard-code the raw extensions, the dotted files are marshaled through
temporary raw-named links (a dedicated mask base for the `-m` spike-time
mask, a dedicated ref base for shadowcluster's `--ref`) and renamed onto
the dotted names on the atomic commit.

## Transactional safety

The script is transactional: Pass 2 writes to a sandbox stem
(`$mergeStem.*`) and only commits (atomic `mv`) onto the live dotted files
after every output is verified present and non-empty. On failure, live
files are byte-identical to their pre-run state. Per-group backups
(`*.res.<method>.N.bak`, `*.spk.<method>.N.bak`, `*.clu.<method>.N.bak`,
`*.fet.<method>.N.bak`) are written on first merge so a full rollback is
always a `mv` away.

## Parameters

```yaml
- name: ndm_reextractspikes
  parameters:
  - {name: reextractThresholdFactor, value: 0.75,   status: Optional}
  - {name: reextractMaskHalfWidth,   value: 16,     status: Optional}
  - {name: reextractMinClusterSize,  value: 50,     status: Optional}
  - {name: reextractChi2,            value: 0.9999, status: Optional}
  - {name: reextractExtraFeat,       value: 0,      status: Optional}
```

`method` and `methodOrder` are read from the node too (or given on the CLI);
they are not listed among the descriptor parameters, matching the
convention in `ndm_extractspikes` / `ndm_pca`. `methodOrder` is accepted as
the current name for the spatial-derivative order, with `sdiffOrder` still
honoured as a fallback.

| Parameter | Effect |
|---|---|
| `reextractThresholdFactor` | Threshold multiplier vs first-pass detection (`0.75` = 25% lower) |
| `reextractMaskHalfWidth` | Samples around each existing `.res` entry to mask off |
| `reextractMinClusterSize` | Parent clusters smaller than this are skipped as candidates |
| `reextractChi2` | χ² confidence for shadow assignment (`0.9999` = very strict) |
| `reextractExtraFeat` | If 1, append per-spike amplitude to the feature vector |
| `methodOrder` | `sdiff`/`stderiv` only: spatial-derivative order (1 = nearest, 2 = next-nearest, 3 = mixed) |

## See also

- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) — the
  next step: re-cluster the unmatched bin into new clusters
- [Re-extract with a lower threshold](../../workflows/re-extract-lower-threshold.md) —
  full workflow recipe
- [`../../design/reextractspikes-v1.md`](../../design/reextractspikes-v1.md) — first specification
- [`../../design/reextract-v2.md`](../../design/reextract-v2.md) —
  extension handling and cross-pipeline shim
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
