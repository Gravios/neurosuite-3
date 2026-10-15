# `.fet.<method>.N` — feature vectors

One feature row per spike of group `N`, aligned to the group's [`.res`](res.md):
the PCA projections of the [`.spk`](spk.md) waveforms, optional peak values,
and the spike time. KiloKlustaKwik clusters on it; Klusters plots it.

## Layout

Binary, little-endian:

```
int32                         nDimensions
nSpikes × nDimensions × int64 row-major (row = spike, in .res order)
fileSize = 4 + nSpikes × nDimensions × 8
```

`nSpikes` is derived from the file size. The **last column is the spike time**,
equal to the `.res` value.

Columns written by [`ndm_pca`](../commands/pca.md), with `C` the number of
channels PCA ran on and `P` the group's `nFeatures`:

| Columns | Content |
|---|---|
| `C × P` | PCA scores, channel-major: channel 0 PC1…PC`P`, channel 1 PC1…, … (rounded to integers) |
| `C` *(only with `extra: true`)* | the waveform value at the peak sample, one per channel |
| 1 | spike time (sample index) |

`nDimensions = C × P [+ C] + 1`. `C` is the group's channel count, or one less
for a `stderiv` method of order 1, 3, 4 or 5, where `ndm_pca` drops the last
channel before PCA (`dropLastChannel`, default `true`).

A **lagged feature space** (method `<token>_D<lag><dims>`) has, per channel,
PC1 at −lag, 0 and +lag samples, then PC2 at 0 when `dims` is 4, and the time
column: `nDimensions = C × dims + 1`. It has no peak columns.

A plain-text form (first line `nDimensions`, then one whitespace-separated row
per spike) is still read by `neurofileio::readFet` and by KiloKlustaKwik, which
tells the two apart by the first byte. Nothing in the pipeline writes it, and
Klusters reads binary only.

## Naming

`.fet` is **MethodSpecific** in the [naming convention](naming.md):
`SESSION.fet.<method>.N`, resolved strictly — a missing `.fet.stderiv.N` is an
error, never a fallback to `.fet.standard.N`.

- `ndm_pca` names the ordinary features by the **waveform** method (the token
  without any `_D` suffix), i.e. the method of the `.spk` it read:
  `.fet.standard.N`, `.fet.sdiff.N`, `.fet.stderiv_C5.N`, …
- A `_D` run additionally writes `.fet.<token>_D<lag><dims>.N`
  (e.g. `.fet.stderiv_C5_D34.N`).
- Each `.fet` is paired with the [`.pca`](pca.md) basis of the same name.

The retired `.fetD.N` corresponds to `.fet.stderiv.N`.

## Writers

| Writer | Notes |
|---|---|
| [`ndm_pca`](../commands/pca.md) | `process_pca` writes the scores (`int32` header, `int64` body), `process_mergefeatures` appends the `.res` times; skips when the output exists |
| `process_spikefilter.py`, [`ndm_reextractspikes`](../commands/ndm_reextractspikes.md) | keep the rows aligned when spikes are dropped or merged |
| Klusters | re-projects realigned waveforms through the `.pca` basis and commits the `.fet` on Save |
| KiloKlustaKwik, fiber-kit | re-featurisation after time shifts / their own feature spaces |

After a realignment the `.fet` (and `.pca`) of the realigned waveforms are stale
([`staleAfterRealign`](naming.md#derivation-and-staleness)).

## Readers

[KiloKlustaKwik](../commands/ndm_klustakwik.md) (`.fet.<method>.N`, strict),
Klusters (the `.fet` sibling of the opened `.clu`, or the `.fet` opened
directly), `process_autoklusta.py`, fiber-kit.

## See also

- [`.pca`](pca.md) — the basis the scores were computed with.
- [`.spk`](spk.md) — the waveforms projected.
- [PCA feature extraction](../commands/pca.md).

---

*Part of the [ndmanager-plugins](../README.md) reference.*
