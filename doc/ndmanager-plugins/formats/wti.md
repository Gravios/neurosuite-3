# `.wti.N[.stage]` — template index (version-tagged text)

The index of a spike group's template-waveform library: one row per record in the
companion [`.wtf`](wtf.md). **Method-less**, per group, per stage
(`<base>.wti.<group>[.<stage>]`) — the index is shared across waveform methods even
though each method keeps its own `.wtf`. Written by fiber-kit's `fiber-template`.

```
wti 1
nSamples 42
nChannels 8
peakSample 21
sr 20000
nRows 24
# row unit link bin a b nSpikes
row 0 3 drift 0 0 720 118
row 1 3 drift 1 720 1440 95
row 2 3 adapt 0 0.0 0.5 140
```

- **row** — 0-based; equals the matching `.wtf` record index (row *i* ↔ `.wtf`
  record *i*).
- **unit** — the series this template belongs to. Under `fiber-template --eap` this
  is the **template-class id** (the [`.eap`](eap.md) column); in the plain clu-keyed
  mode it is the clu id.
- **link** — `drift` (one median per fixed time **chunk**; `a`,`b` = chunk
  start/end **seconds**) or `adapt` (one median per spike-**energy** bin; `a`,`b` =
  energy lo/hi).
- **bin** — 0-based ordinal within that `(unit, link)` series.
- **nSpikes** — spikes behind the median (`0` = an empty placeholder row).

The header also records the waveform geometry (`nSamples`, `nChannels`,
`peakSample`, `sr`) that the headerless `.wtf` relies on. A class's **temporal
scope** (used by Klusters' temporally-restricted projection) is the union of
`[a,b]` over its `drift` rows.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
