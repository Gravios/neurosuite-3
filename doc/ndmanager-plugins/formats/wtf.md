# `.wtf.<method>.N[.stage]` — template waveform stack (binary int16)

The template waveforms indexed by [`.wti`](wti.md): a headerless stack in
[`.spk`](spk.md) layout, one record per `.wti` row and in the same order.

**Naming.** Method-tagged and per stage:
`<base>.wtf.<method>.<group>[.<stage>]`. The waveform shape depends on the
extraction method, so each method has its own `.wtf`; they share one `.wti`. A
`.` in the stage tag is written as `_`.

**Writers.** fiber-kit's `fiber-template` and Klusters' built-in template
generation (one `.wtf` per requested `.spk` variant, read from
`<base>.spk.<method>.<group>`). A committed manual lineage is rendered to
`<base>.mtf.<method>.<group>[.<stage>]` in the same layout. Read with
`neurofileio::readSpk`.

## Layout

```
Record   nSamples × nChannels int16, sample-major (as in .spk)
Stack    one record per .wti row, in .wti row order (record i ↔ row i)
Size     nRows × nSamples × nChannels × 2 bytes
```

Each record is the element-wise median of up to 800 evenly strided spikes of
its bin, rounded to int16. A placeholder row (`nSpikes 0`) is all zeros.

There is no header: `nSamples` and `nChannels` come from the `.wti`, and the
`.wtf` is always read together with it (for example by Klusters' template
view).

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
