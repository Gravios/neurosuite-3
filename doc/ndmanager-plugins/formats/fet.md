# `.fet.<method>.N` — PCA feature vectors

Binary. `int32_t` nDimensions header (= `nChannels × nPCs + 2`,
including extra features and timestamp). Then
`nSpikes × nDimensions × int64_t`, row-major. Last column is the spike
timestamp (sample index). Written by `process_mergefeatures` / the PCA
plugins; read by KiloKlustaKwik and klusters.

`.fet` is a **MethodSpecific** artifact under the
[variant naming convention](naming.md): it is resolved **strictly** as
`<base>.fet.<method>.N`, with no fallback to another variant. The two
standard variants are:

- **`fet.standard.N`** — features in the raw waveform domain; pairs with
  `pca.standard.N`.
- **`fet.stderiv.N`** — features in the stderiv (spatial-derivative +
  temporal-difference) domain, produced by piping the shared raw `.spk`
  through `process_pca_stderiv` into the PCA step; pairs with
  `pca.stderiv.N`.

The old `.fetD.N` is retired in favour of `.fet.stderiv.N`.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
