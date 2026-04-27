# `.clu.N` — cluster assignments

Binary (canonical) or plain-text (legacy). Binary format: `int32_t`
header (number of clusters) followed by `int32_t` cluster IDs, one per
spike. Text format: one line with the cluster count, then one cluster
ID per spike, newline-separated.

Cluster ID conventions: `0` = noise/artefact, `1` = unsorted MUA,
`≥ 2` = candidate single units. Same spike order as `.res.N`.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
