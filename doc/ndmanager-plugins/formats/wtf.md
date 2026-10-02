# `.wtf.<method>.N[.stage]` — template waveform stack (binary int16)

The median template waveforms indexed by [`.wti`](wti.md): a **headerless**
`.spk`-layout `int16` stack, one record per `.wti` row and in the same order.
**Method-tagged** (per waveform variant) and per stage —
`<base>.wtf.<method>.<group>[.<stage>]` — because the waveform *shape* depends on the
extraction method, while the companion `.wti` index does not. Written by fiber-kit's
`fiber-template`.

```
Record: nSamples × nChannels int16, sample-major   (identical to .spk; see spk.md)
Stack:  one record per .wti row, in .wti row order  (record i ↔ .wti row i)
File size = nRows × nSamples × nChannels × 2 bytes
```

The geometry (`nSamples`, `nChannels`, `peakSample`) comes from the companion
[`.wti`](wti.md) header; the `.wtf` itself carries no header and is always read
alongside its `.wti` (for example by Klusters' template-library view).

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
