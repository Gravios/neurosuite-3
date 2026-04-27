# `ndm_stripdat` — subtract spikes to produce a cleaned `.dat`

## Synopsis

```bash
ndm_stripdat [session.yaml]
```

**Input:** `session.dat`, `session.res.N`, `session.spk.N`, `session.clu.N` (optional, for `model`/`botm` modes)
**Output:** `session-spkclean.dat`

The original `.dat` is **never modified**. `session-spkclean.dat` is
always overwritten on re-run so it can be regenerated after each
curation pass. `ndm_lfp` automatically uses `session-spkclean.dat`
when it exists.

## Description

Subtracts spike waveforms from the raw `.dat` to produce
`session-spkclean.dat`. Useful for:

- **LFP analysis** — removes spike contamination from the LFP band.
- **Iterative refinement** — feed the cleaned `.dat` to
  `ndm_redetectspikes` to find spikes that were masked by larger
  neighbours in the first pass.

## Subtraction modes

| Mode | Description |
|---|---|
| `model` (default) | Per-cluster template model with projection-based amplitude scaling and ISI-driven burst-shape compensation. Uses `.clu.N` when present. |
| `raw` | Subtracts raw `.spk.N` waveforms directly. Use when no curation has happened yet. |
| `botm` | Bayes-Optimal Template Matching (Proepper 2015). ~100× better residuals than `model` mode on the synthetic test fixture while preserving the noise baseline. Use when LFP analysis is the goal. |

When `autoStrip=true`, the per-group strip sidecars are generated
from YAML `Quality` labels automatically — no manual file maintenance
between curation rounds.

## Parameters

```yaml
- name: ndm_stripdat
  parameters:
  - {name: redetectSpikes,   value: 'no',      status: Optional}
  - {name: stripClusters,    value: 'default', status: Optional}
  - {name: subtractionMode,  value: 'model',   status: Optional}
  - {name: autoStrip,        value: 'true',    status: Optional}
  - {name: autoStripQuality, value: '1,2,3',   status: Optional}
```

| Parameter | Effect |
|---|---|
| `redetectSpikes` | Run `ndm_redetectspikes` automatically after stripping |
| `stripClusters` | Comma-separated cluster IDs to strip (`default` = use Quality labels) |
| `subtractionMode` | `model`, `raw`, or `botm` |
| `autoStrip` | If true, derive strip list from `Quality` annotations |
| `autoStripQuality` | Quality tiers to include (e.g. `'1,2,3'` = strip Uncertain through Good, skip Bad) |

## See also

- [`ndm_redetectspikes`](ndm_redetectspikes.md) — typical next step
- [Iterative refinement](../../workflows/iterative-refinement.md) —
  full strip → redetect → re-sort recipe
- [`../../design/subtractspikes-botm.md`](../../design/subtractspikes-botm.md) —
  BOTM algorithm derivation
- [`../../design/modeling-l1-vs-botm.md`](../../design/modeling-l1-vs-botm.md) —
  side-by-side model comparison
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
