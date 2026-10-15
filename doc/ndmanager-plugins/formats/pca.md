# `.pca.<method>.N` — PCA basis

The per-channel principal components [`ndm_pca`](../commands/pca.md) fitted for
group `N`: what projects a waveform into the matching [`.fet`](fet.md). Klusters
uses it to re-project waveforms after a realignment; KiloKlustaKwik, fiber-kit
and the re-featurisation tools read it too. The format is called **PCAE**.

## Layout

Binary, little-endian. The single reader/writer pair is
`neurosuite::core::loadPca` / `writePca`
(`src/libneurosuite-core/src/neurosuite/core/pca_projection.hpp`); `neurofileio`
does not handle `.pca`.

Header, `int32` fields:

| # | Field | Meaning |
|---|---|---|
| 0 | magic | `0x50434145` (`"PCAE"`) |
| 1 | version | `2` (written); `1` still read |
| 2 | `nChannels` | channels in the basis (`C`) |
| 3 | `data2use` | samples per channel the basis spans (`D`) |
| 4 | `nComponents` | principal components per channel (`K`) |
| 5 | `recShift` | first waveform sample (0-based) the basis applies to |
| 6 | `isCentered` | 1: subtract the means before projecting |
| 7 | `method` | *(v2)* transform the basis was fitted on, see below |
| 8 | `nInputChannels` | *(v2)* raw channels the transform consumes (`C`, or `C + 1` when a channel was dropped) |

Body, block-wise:

```
C × (D × float64)        per-channel means (always present)
C × (D × K × float64)    per-channel eigenvectors, column-major: ev[k × D + u]
```

The score of component `k` on channel `c` is
`Σ_u ev_c[k × D + u] · (x_c[recShift + u] − mean_c[u])` (means subtracted only
when `isCentered` is 1), with `x` the waveform in the basis's transform
domain. With `varimax` (the `ndm_pca` default) the stored eigenvectors are the
rotated ones.

A v1 file has no fields 7–8 and loads as `method` 0 with
`nInputChannels = nChannels`.

`method` values (`neurosuite::core::Method`):

| Value | Transform | Method token family |
|---|---|---|
| 0 | none (raw) | `standard` |
| 1 / 2 / 3 | spatial derivative, order first / Laplacian / all-pairs | `sdiff` |
| 4 / 5 / 6 | spatial derivative order 1 / 2 / 3 + temporal first difference | `stderiv` (`_S1`–`_S3`, or bare with `methodOrder`) |
| 7 | custom single-partner pattern + temporal difference | `stderiv_C4` |
| 8 | custom reference-set pattern + temporal difference | `stderiv_C5` |

A **lagged** basis (`.pca.<token>_D<lag><dims>.N`) uses the same format: `K` is
3 or 4, the window is widened by `lag` on each side (`D` = the ordinary window
+ 2 × lag, `recShift` reduced by `lag`), `isCentered` is 0 and the means are
zero, so the lag `.fet` is exactly the projection of the `.spk` through it.

## Naming

`.pca` is **MethodSpecific** in the [naming convention](naming.md):
`SESSION.pca.<method>.N`, resolved strictly. `process_pca` derives the name from
its `.fet` output (`.fet.` → `.pca.`), so every `.fet` has a `.pca` of the same
name: `.pca.standard.N`, `.pca.stderiv_C5.N`, `.pca.stderiv_C5_D34.N`, … The
retired `.pcaD.N` corresponds to `.pca.stderiv.N`; `process_pca` run directly
with a `.fetD.N` output still writes `.pcaD.N`.

## Writers and readers

- **Written** by [`ndm_pca`](../commands/pca.md) (`process_pca`), and by
  fiber-kit for its own feature spaces.
- **Read** by Klusters (realign and re-featurise: the basis whose method matches
  the opened `.clu`), KiloKlustaKwik (time-shift and re-featurisation, strict
  `.pca.<method>.N`), `process_refeaturize` / `process_refeaturize_stderiv`,
  `process_alignspikes_pca`, `process_shadowcluster`, and fiber-kit.

Realigning waveforms makes the `.pca` and `.fet` stale
([`staleAfterRealign`](naming.md#derivation-and-staleness)).

## See also

- [`.fet`](fet.md) — the scores computed with this basis.
- [`.spk`](spk.md) — the waveforms it projects.
- [PCA feature extraction](../commands/pca.md).

---

*Part of the [ndmanager-plugins](../README.md) reference.*
