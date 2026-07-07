# `.pca.<method>.N` — PCA eigenvector basis

Binary. The principal-component eigenvectors used to project spike
waveforms into the `.fet.<method>.N` feature space, and to re-project
re-extracted waveforms after a spike realignment.

`.pca` is a **MethodSpecific** artifact under the
[variant naming convention](naming.md): resolved **strictly** as
`<base>.pca.<method>.N`, no fallback. Each variant pairs with the matching
`.fet`:

- **`pca.standard.N`** — basis for the raw-domain `fet.standard.N`.
- **`pca.stderiv.N`** — basis for the stderiv-domain `fet.stderiv.N`.

The old `.pcaD.N` is retired in favour of `.pca.stderiv.N`. Klusters and
`spikerealign` pick the basis whose method matches the loaded feature
variant, so realigned features are re-projected through the correct one.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
