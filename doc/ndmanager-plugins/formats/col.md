# `.col.<method>.N` — collision decomposition results (binary)

Written by [`ndm_decomposecollisions`](../commands/ndm_decomposecollisions.md)
(`process_decomposecollisions`), one file per spike group. Read by
`neurofileio::readColAccepted` (Klusters' decollide path) and by
`collision_viewer.py`. One record per collision candidate; the source `.clu`,
`.res` and `.spk` are not modified.

`.col` is **MethodSpecific** under the [naming convention](naming.md):
`<base>.col.<method>.N`, the same method as the `.clu` it was computed from.
When `process_decomposecollisions` reads a curated stage (`--clu-tag T`), the
output carries the same tag: `<base>.col.<method>.N.T`.

## Layout

Little-endian, packed (no padding between fields):

```
Header (32 B)
  magic         4 B    'C' 'O' 'L' 0x01
  nSpikes       u32    spikes in the group
  nRecords      u32    candidate records that follow
  nTemplates    u32    template rows that follow the parameter block
  group         u32    1-based spike group
  flags         u32    bit 0: clusters 0/1 excluded from templates
                       bit 1: the waveforms were stderiv (.spk.stderiv*)
  pad           8 B

Parameters (32 B)
  corrThreshold      f32
  residualThreshold  f32
  maxShiftSamp       i32
  minSnrRms          f32
  minSpikesTemplate  i32
  pad                12 B

Template[nTemplates] (24 B each)
  unit          i32    cluster ID
  nSpikes       i32    spikes behind the mean waveform
  dominantCh    i32    channel (index within the group) of largest amplitude
  ampMean       f32    mean projection coefficient <w, T>/||T||² of the unit's spikes
  ampPct01      f32    1st percentile of those coefficients
  ampPct99      f32    99th percentile

Record[nRecords] (60 B each)
  ts            i64    spike time (samples)
  spikeIdx      i32    row in the group's .res / .spk / .clu
  bestUnit      i32    the spike's own cluster
  bestCorr      f32    correlation with that cluster's template (-2: no template)
  flags         u32    bit 0: accepted   bit 1: component 1 fitted   bit 2: component 2 found
  residNorm     f32    ||residual|| / ||waveform|| after both components
  u1  i32   sh1 i32   sf1 f32   a1 f32     component 1
  u2  i32   sh2 i32   sf2 f32   a2 f32     component 2
```

Records are sorted by `spikeIdx`.

## Components

Each record explains the spike as `a1·T_u1(t − s1) + a2·T_u2(t − s2)`:

- `u` — unit (cluster ID); `-1` when the component was not found. A candidate
  whose cluster has no template is recorded with `u1 = -1` and is never
  accepted.
- `sh`, `sf` — integer and fractional shift; the total shift in samples is
  `sh + sf`, with `sf` in [−0.5, 0.5] from parabolic interpolation.
  Component 1 is the spike's own cluster at `sh1 = 0`.
- `a` — fitted amplitude scale of the template, clamped to the unit's
  [`ampPct01`, `ampPct99`] range (relative to `ampMean`) widened by half its
  width on each side.

A record is accepted (`flags` bit 0) when `residNorm < residualThreshold` and a
second unit was found.

Files written before 2026-04-19 stored `sf2 = sh2 + fraction`; subtract `sh2`
from `sf2` for those, or recompute them.

## Readers

`readColAccepted` checks the magic, skips the parameter block and template
rows, and returns only accepted records as `(spikeIdx, u1, sh1, a1, u2, sh2,
a2)`. A bad magic or a short file gives an empty result.

The accepted decompositions are also recorded, as presence and integer offset
only, in the [`.eap`](eap.md) / [`.tcl`](tcl.md) template-class layer.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
