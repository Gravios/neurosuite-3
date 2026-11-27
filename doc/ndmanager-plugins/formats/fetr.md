# `.fetr.<method>.N[.stage]` — sparse residual features (binary)

The features of the **other** spike of a collision row. The [`.spkr`](spkr.md)
holds, for each collision row, the partner's spike as a waveform record; the
`.fetr` holds that record projected into a feature space. It is to the `.spkr`
what the [`.fet`](fet.md) is to the [`.spk`](spk.md).

A row of it is laid out like a row of the space's `.fet` — the same columns, the
time last — so a residual spike can be put beside the partner's cluster in that
space, or clustered with the other residuals.

It is not a variant of the `.fet`: a reader must never serve a `.fetr` row as
the features of the row it is indexed by. That row's features are in the `.fet`.

**Naming.** One file per feature space and per stage:
`<base>.fetr.<method>.<group>[.<stage>]`, `<method>` the feature space's token
(the `.fet`'s), which for a lag space differs from the `.spkr`'s.

**Writers / readers.** `neurofileio::readFetr` / `writeFetr` / `fetrFind` in
`libneurosuite-core`. Klusters writes one at Save for every feature space of
the group that has a basis (`.pca.<method>`), beside the `.spkr`. fiber-kit
does not read or write it; its dedup, which drops rows, renames the group's to
`<file>.stalebkp` (fiber-kit 0.71.24), and the next Save that writes the `.spkr`
writes it again.

## Layout

Little-endian:

```
Header (32 B)
  magic      4 B    'F' 'T' 'R' 0x01
  nRows      u32    rows held here
  nFeatures  u32    columns of a row, the time included (the .fet's header)
  nSpikes    u32    rows of the .fet this belongs to
  group      u32    1-based spike group
  flags      u32    reserved (0)
  pad        8 B
Index
  nRows × int64     0-based .fet rows, strictly ascending
Body
  nRows × nFeatures int64, the .fet row layout, in index order
```

`readFetr` rejects a bad magic or version, a file shorter than its header says,
fewer than two columns, and an index that is not strictly ascending or names a
row at or beyond `nSpikes`. `writeFetr` refuses the same and a table that does
not hold `nFeatures` values a row.

## What a row holds

- **The projected columns** (the first `nChannels × nComponents` of the space):
  the residual record of the space's waveform method — the `.spkr` record, or
  for a space over another `.spk` the same window cut in that method — through
  the space's `.pca`, as `.fet` rows are made.
- **The columns the projection does not give** (any the space adds between the
  components and the time): the indexed row's own values in that `.fet`. They
  describe the same stretch of recording, not the residual spike.
- **The time, last:** the residual spike's — the row's `.res` time plus the
  `.spkr`'s offset for that row.

## Relation to the `.spkr`

The index is the `.spkr`'s of the same stage: the same rows, in the same order.
Offset, class, kind and twin of a row are read there; in particular only the
rows whose kind is *recovered* are spikes no row of the `.fet` holds.

Derived data: rebuilt from the recording, the `.eap`, the clustering and the
bases. A space with no basis (a detached feature table) gets no `.fetr`.
