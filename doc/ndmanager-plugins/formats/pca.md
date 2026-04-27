# `.pca.N` / `.pcaD.N` — PCA eigenvector basis

Binary. 5-int32 header
(`nCh`, `data2use`, `nComp`, `isCentered`, `recShift`), followed by
per-channel double-precision means and eigenvectors. Written by
`process_pca`; read by klusters (realignment, nudge) and
`process_shadowcluster` to reproject waveforms when new spikes arrive
after the initial sort.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
