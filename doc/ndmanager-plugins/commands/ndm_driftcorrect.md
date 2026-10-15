# `ndm_driftcorrect` — re-interpolate spike waveforms to the reference probe position (`.spk` + `.dat.drift.P` → `.spkC.N` / `.spkCD.N`)

## Synopsis

```bash
ndm_driftcorrect [session]
```

**Input:** per spike group the waveform file `.spk` (resolved at method
`stderiv`: `SESSION.spk.stderiv.N`, else `SESSION.spk.standard.N`, else
`SESSION.spk.N`) and `SESSION.res.N`; per probe the drift signal
`SESSION.dat.drift.P` from [`ndm_estimatedrift`](ndm_estimatedrift.md); site
geometry from the session YAML and its probe file.

**Output:** `SESSION.spkCD.N` when the resolved `.spk` is a stderiv file,
otherwise `SESSION.spkC.N` (same layout as the input `.spk`).

> **File names.** The outputs use glued suffixes, not the dotted
> `SESSION.spk.<method>.N` scheme of [naming](../formats/naming.md), and
> `.res` is read only under its untagged name `SESSION.res.N`.  No other
> pipeline step reads `.spkC.N` / `.spkCD.N`: to sort the corrected
> waveforms, the file has to be put in place of the `.spk` that
> [`ndm_pca`](pca.md) reads (`SESSION.spk.<method>.N`).  A stderiv token with
> a suffix (e.g. `stderiv_C5`) is not matched by the `stderiv` lookup and
> falls through to the standard or untagged copy.

## Description

For every spike, the probe offset `d` at the spike's timestamp is read from
`SESSION.dat.drift.P` (timestamps beyond the signal are clamped to its ends).
Each site's corrected waveform is the recorded waveform interpolated linearly
across the group's sites at depth `z − d`, where `z` is the site's depth —
i.e. the waveform the site would have recorded with the probe at the
reference position (`d = 0`).  Queries beyond the outermost sites take the
outermost site's waveform.  Sites without a depth (`null` in
`sitePositions_um`, or a channel the probe file does not place) are copied
unchanged and left out of the interpolation.  A warning is printed when
spikes have `|d|` larger than half the group's depth span.

No `.clu` is read: every spike is corrected, including clusters 0 and 1, so
a later re-sort can recover units from them.  The output is always
recomputed from the original `.spk`, and the drift signal is the total offset
from the reference, so repeated rounds do not compound interpolation.

Per group:

- no `.spk` → skipped;
- output exists and `overwrite` is false → skipped;
- the probe's `SESSION.dat.drift.P` is missing or empty → skipped;
- largest `|d|` in the signal below `passthroughThreshUm` → the `.spk` is
  copied unchanged;
- no usable geometry (fewer than two site depths) → skipped;
- `SESSION.res.N` missing → error, the run stops.

If the `.spk` and `.res` spike counts differ, the smaller is used.  The
command exits 11 when no group has a `.spk` or no `SESSION.dat.drift.*` file
exists (`ndm_estimatedrift` writes these only when `SESSION.dat` is present).

### Site geometry

Same lookup as [`ndm_estimatedrift`](ndm_estimatedrift.md#site-geometry):
`sitePositions_um` on the spike group if present, otherwise the group's
channels looked up **by channel number** in the probe file selected by the
group's `probeId` / `shankIndex` (`scripts/probe_geometry.py`, see
[`.probe` geometry and channels](../formats/probe.md#geometry-and-channels)).
Unlike `ndm_estimatedrift`, there is no 50 µm default: a group without
geometry is skipped.  The probe index `P` is the probe's 1-based position in
the YAML `probes:` list (1 when the list is absent).

## Parameters

Set under `programs:` → `- name: ndm_driftcorrect` → `parameters:` (see
[parameters](../parameters.md)).

| Name | Default | Meaning |
|---|---|---|
| `overwrite` | `false` | Rewrite existing `.spkC.N` / `.spkCD.N`. |
| `passthroughThreshUm` | `0.25` | If the largest absolute drift (µm) in the probe's signal is below this, copy the waveforms unchanged. |
| `batchSize` | `100000` | Spikes per batch; bounds memory. |
| `nWorkers` | `1` | Worker processes, one spike group each; `0` = all CPUs. |
| `probeLibrary` | — | Extra directory searched for probe files (search order as for `ndm_estimatedrift`). |

```yaml
- name: ndm_driftcorrect
  parameters:
    - {name: overwrite,           value: 'false', status: Optional}
    - {name: passthroughThreshUm, value: 0.25,    status: Optional}
    - {name: batchSize,           value: 100000,  status: Optional}
    - {name: nWorkers,            value: 1,       status: Optional}
    - {name: probeLibrary,                        status: Optional}
```

Also read from the YAML: `acquisitionSystem` `samplingRate`, `nChannels`,
`nBits` (to check the drift signal's length against `SESSION.dat`) and each
spike group's `nSamples` (default 32).

## Iterating

The intended loop is: sort → curate → `ndm_estimatedrift` →
`ndm_driftcorrect` → re-featurise and re-sort the corrected waveforms →
`ndm_estimatedrift` again (after removing `SESSION.drift`, which it will not
overwrite) → repeat until the drift estimate stops changing.  Set
`overwrite: true` for the second and later rounds.

## See also

- [`ndm_estimatedrift`](ndm_estimatedrift.md) — writes `SESSION.dat.drift.P`
- [`.drift` format](../formats/drift.md) — includes the binary drift signal
- [`.spk` format](../formats/spk.md), [`.probe` format](../formats/probe.md)
- [Drift correction workflow](../../workflows/drift-correction.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
