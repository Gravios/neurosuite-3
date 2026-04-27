# `ndm_reextractspikes` / `ndm_reextractspikes_stderiv`

Masked second-pass detection + shadow-clustered merge.

## Synopsis

```bash
ndm_reextractspikes         [session.yaml] [group]
ndm_reextractspikes_stderiv [session.yaml] [group]
```

Both variants take the same arguments and produce the same file
layout — they differ only in detection signal and waveform value
space (see Description).

**Input:** `session.fil`, `session.res.N`, `session.clu.N`, `session.fet.N` (or `.fetD.N`), `session.pca.N` (or `.pcaD.N`)
**Output:** updated `session.res.N`, `session.spk.N` (or `.spkD.N`), `session.clu.N`, `session.fet.N` (or `.fetD.N`); per-group backups (`*.bak`)

## Description

Second-pass spike detection that runs at a reduced threshold while
masking out timestamps already present in `.res.N`. New spikes are
then shadow-clustered into the existing `.clu.N`: each new spike is
assigned by robust Mahalanobis distance (per-dim median + MAD × 1.4826)
in PCA feature space to the nearest eligible parent cluster; spikes
exceeding the χ² threshold from every parent land in a single
"unmatched" bin.

The two variants share all logic except detection signal and waveform
value space:

- `ndm_reextractspikes` — detection on the raw high-pass signal;
  waveforms written raw to `.spk.N`.
- `ndm_reextractspikes_stderiv` — detection on the stderiv signal
  (same spatial + temporal first-difference transform as
  `ndm_extractspikes_stderiv`); waveforms **also written in stderiv
  space** to `.spkD.N` so they stay in the same value space as the
  reference rows and the `.pcaD.N` basis they will be projected
  through.

## Transactional safety

Both scripts are transactional: Pass 2 writes to a sandbox stem
(`$mergeStem.*`) and only commits (atomic `mv`) to the live session
files after every output file is verified present and non-empty. On
failure, live files are byte-identical to their pre-run state.
Per-group backups (`*.res.N.bak`, `*.spk.N.bak` / `*.spkD.N.bak`,
`*.clu.N.bak`, `*.fet.N.bak` / `*.fetD.N.bak`) are written on first
merge so a full rollback is always a `mv` away.

## Parameters

```yaml
- name: ndm_reextractspikes_stderiv
  parameters:
  - {name: reextractThresholdFactor, value: 0.75,   status: Optional}
  - {name: reextractMaskHalfWidth,   value: 16,     status: Optional}
  - {name: reextractMinClusterSize,  value: 50,     status: Optional}
  - {name: reextractChi2,            value: 0.9999, status: Optional}
  - {name: reextractExtraFeat,       value: 0,      status: Optional}
  - {name: sdiffOrder,               value: 3,      status: Optional}
```

| Parameter | Effect |
|---|---|
| `reextractThresholdFactor` | Threshold multiplier vs first-pass detection (`0.75` = 25% lower) |
| `reextractMaskHalfWidth` | Samples around each existing `.res.N` entry to mask off |
| `reextractMinClusterSize` | Parent clusters smaller than this are skipped as candidates |
| `reextractChi2` | χ² confidence for shadow assignment (`0.9999` = very strict) |
| `reextractExtraFeat` | If 1, append per-spike amplitude to the feature vector |
| `sdiffOrder` | Stderiv variant only: spatial-derivative order (1 = nearest, 2 = next-nearest, 3 = mixed) |

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
