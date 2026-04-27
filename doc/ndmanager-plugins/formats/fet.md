# `.fet.N` / `.fetD.N` — PCA feature vectors

Binary. `int32_t` nDimensions header (= `nChannels × nPCs + 2`,
including extra features and timestamp). Then
`nSpikes × nDimensions × int64_t`, row-major. Last column is the spike
timestamp (sample index). Written by `process_mergefeatures`; read by
KlustaKwik and klusters. Canonical `.fet.N` pairs with `.pca.N`;
`.fetD.N` pairs with `.pcaD.N`.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
