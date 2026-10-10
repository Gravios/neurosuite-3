# `.col.<method>.N` — collision decomposition results (binary)

Binary, little-endian. Written by `ndm_decomposecollisions`
(`process_decomposecollisions`); read back by `neurofileio::readColAccepted`
and consumed by klusters' decollide path. One record per candidate collision
spike. A **MethodSpecific** artifact under the
[variant naming convention](naming.md), resolved strictly as
`<base>.col.<method>.N`. The source `.clu` / `.res` / `.spk` files are not
modified.

> This file is **binary**, not YAML. (Earlier design sketches showed a
> `collisions:`/`spikes:` YAML mapping; no in-repo reader consumes that — the
> on-disk format of record is the binary layout below.)

## Layout

```
Header (32 B)
  magic[4]       {'C','O','L',0x01}
  nSpikes        u32
  nRecords       u32
  nTemplates     u32
  group          u32
  flags          u32
  pad[8]                         -> 32 B total

ColParams (32 B)                 fixed parameter block (skipped by the reader)
ColTemplate[nTemplates]          24 B each            (skipped by the reader)

Record[nRecords]  (60 B each)
  ts                 i64         spike timestamp
  spikeIdx           i32         index into the group's .res / .spk
  best_single_unit   i32         best single-unit fit
  best_single_corr   f32         its correlation
  flags              u32         bit 0 = REC_FLAG_ACCEPTED
  resid_norm         f32         residual norm after decomposition
  component[0]  { u i32, sh i32, sf f32, a i32->f32 }   16 B
  component[1]  { u i32, sh i32, sf f32, a i32->f32 }   16 B
```

Each accepted record decomposes one collision into **two** components, each a
`(unit, shift, scale, amplitude)` tuple.

## What the reader returns

`readColAccepted` reads the header (validating the `COL\x01` magic), skips the
parameter and template tables, and returns **only** the records whose `flags`
has `REC_FLAG_ACCEPTED` (bit 0) set — as `(spikeIndex, u1/sh1/a1, u2/sh2/a2)`,
the input klusters' decollide engine applies. A bad magic or a short file
yields an empty result.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
