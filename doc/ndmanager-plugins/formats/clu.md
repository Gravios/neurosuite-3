# `.clu.N` — cluster assignments

Binary (canonical) or plain-text (legacy). Binary format: `int32_t`
header (number of clusters) followed by `int32_t` cluster IDs, one per
spike. Text format: one line with the cluster count, then one cluster
ID per spike, newline-separated.

Cluster ID conventions: `0` = noise/artefact, `1` = unsorted MUA,
`≥ 2` = candidate single units. Same spike order as `.res.N`.

## Hierarchical sessions

A `.clu.N` can be the **fiber (parent) layer** of a two-level
hierarchical clustering. In that case it has two siblings sharing the
same anchor:

| File | Role |
|---|---|
| [`.clc.N`](clc.md) | The **atom (child) layer** — per-spike assignment to the finest over-split micro-clusters |
| [`.clp.N`](clp.md) | The **atom→fiber map** — which fiber each atom belongs to |

Here the `.clu.N` assigns each spike to a *fiber* (an assembled unit) and
the `.clc.N` assigns it to an *atom* (nested inside exactly one fiber).
Opening the `.clu.N` when both siblings are present starts a hierarchical
session in klusters; see
[Hierarchical clustering](../../klusters/hierarchical-clustering.md).

A plain `.clu.N` with no `.clc`/`.clp` siblings is an ordinary flat
clustering.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
