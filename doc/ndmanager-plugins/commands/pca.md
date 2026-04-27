# PCA feature extraction

Short utilities for pca feature extraction.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_pca` — raw-waveform PCA (`.spk` → `.fet`, `.pca`)

Computes principal component features from raw spike waveforms.
`process_pca` is OpenMP-parallelised across electrode groups.
Writes `.fet.N` (features, with timestamp column) and `.pca.N` (the
eigenvector basis used for projection — needed by klusters realignment
and by reextract workflows).

```bash
ndm_pca session.yaml       # all electrode groups (parallel)
ndm_pca session.yaml 3     # single electrode group
```


---

## `ndm_pca_stderiv` — stderiv-waveform PCA (`.spkD` → `.fetD`, `.pcaD`)

PCA on stderiv-transformed waveforms. Reads `.spkD.N`, passes the
waveforms through `process_pca_stderiv` (channel reduction for
rank-deficient orders 1 and 3), then through `process_pca` on the
reduced channel set. Writes `.fetD.N` and `.pcaD.N`. Klusters and
KlustaKwik auto-detect the D variant at open time.

For SDIFF_FIRST and SDIFF_ALLPAIRS (orders 1 and 3), one channel is
linearly dependent on the others; `process_pca_stderiv` drops it before
PCA so `pca.nChannels = nElectrodes − 1` for those orders. SDIFF_NONE
and SDIFF_LAPLACIAN (orders 0 and 2) preserve full rank, so
`pca.nChannels = nElectrodes`. Downstream consumers (shadowcluster,
klusters, KlustaKwik) accept `pca.nChannels ≤ nChanGroup` and skip
the dropped channel automatically.



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
