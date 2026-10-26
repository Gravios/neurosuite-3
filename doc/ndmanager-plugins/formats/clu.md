# `.clu.<method>.N` — cluster assignments

The cluster id of every spike of group `N`, one entry per [`.res`](res.md) row,
in the same order. In a hierarchical session it is the parent (fiber) layer of
the [`.clc`](clc.md) / [`.clp`](clp.md) triple.

## Layout

Binary, little-endian:

```
int32            header (number of distinct ids)
nSpikes × int32  cluster id per spike, .res order
fileSize = 4 + nSpikes × 4
```

- **Length check:** readers take `nSpikes` from the `.res` / `.spk` and refuse a
  `.clu` that holds a different number of ids (`neurofileio::readCluBinary`
  reports how many it found). A `.clu` belonging to another run, group or method
  is rejected, not truncated.
- **Header:** informational: the number of distinct ids in the file. Klusters
  writes that on Save, and fiber-kit from 0.57.3. KiloKlustaKwik writes its
  highest id, which is the same number, since it numbers without gaps from 1.
  The header is not the highest id + 1: once [reserved ids](#reserved-ids) are
  in a file, that is about 16.8 million. Klusters refuses a header outside
  0–65536 ("Invalid or missing .clu header"); other readers ignore it.
  - fiber-kit 0.56.0–0.57.2 wrote the highest id + 1, so Klusters refused the
    files of its chunk runs. fiber-kit's `fiber-clu-header FILE…` rewrites such
    a header in place and leaves the ids as they are.
- **Ids by convention:** `0` = noise / artefact, `1` = unsorted multi-unit
  activity, `≥ 2` = candidate units. KiloKlustaKwik renumbers its output without
  gaps from 1 (its noise class becomes 1). Nothing enforces the convention.

A plain-text form (first line the cluster count, then one id per line) is read
by `neurofileio::readClusterRes` when the paired `.res` is also text
(NeuroScope, the template generator). Klusters, `process_spikefilter.py` and the
other plugins read binary only.

### Reserved ids

Ids 16 777 116 – 16 777 215 (2^24 − 100 … 2^24 − 1) are reserved for fiber-kit's
chunk runs (`FK_CHUNKS`). Such a run sorts only some chunks of the recording and
puts every spike of each other chunk in that chunk's reserved cluster. Klusters
numbers new clusters below the block (`isReservedClusterId()` in
`src/libklustersshared/src/klustersshared/types.h`). It also protects reserved
clusters. Nothing is added to one, and only reclustering and selections in the
feature space take spikes out of one. See
[Reserved chunk clusters](../../klusters/README.md#reserved-chunk-clusters).

## Naming

`.clu` is **MethodSpecific** in the [naming convention](naming.md):
`SESSION.clu.<method>.N[.<suffix>]`, resolved strictly. The method is that of
the `.fet` it was clustered from (`.clu.standard.N`, `.clu.stderiv_C5.N`,
`.clu.stderiv_C5_D34.N`, …). A trailing suffix marks a stage or experiment
(`.clu.stderiv.5.stack`).

Opening a `.clu` in Klusters fixes the session's method: its `.spk`, `.fet`,
`.pca` and `.res` siblings are resolved under the method in its name (an
untagged `.clu.N` counts as `standard`).

## Writers

| Writer | Output |
|---|---|
| [KiloKlustaKwik](../commands/ndm_klustakwik.md) | `.clu.<method>.N` |
| Klusters | the opened `.clu` on Save (a working `.pending` copy is used during the session); Save As to any name, parsed for base, method, group and suffix |
| [`ndm_reextractspikes`](../commands/ndm_reextractspikes.md) | merges second-pass spikes with shadow cluster ids |
| `process_spikefilter.py` | drops filtered rows, header unchanged |
| `process_reassignspikes`, `process_drifttracker`, `process_autoklusta.py` | additional `.clu` files under names of their own (e.g. `process_autoklusta.py` writes `SESSION.clu.<group>.<label>`) |
| fiber-kit | the fiber layer of a hierarchical session |

## Readers

Klusters, NeuroScope (overlay of `X.clu.<…>` with the `.res` of the same name),
`ndm_pca` (`excludeClusters`), the curation and template tools, fiber-kit.

## Hierarchical sessions

A `.clu` with a `.clc` sibling of the same name (`.clu.` → `.clc.`; same base,
method, group and suffix) opens in Klusters as a two-level session:

| File | Role |
|---|---|
| `.clu` | per spike: the **fiber** (parent unit) |
| [`.clc`](clc.md) | per spike: the **atom** (child micro-cluster), nested in exactly one fiber |
| [`.clp`](clp.md) | per atom: its fiber — a cache of the map the two per-spike files imply |

Hierarchical mode needs the YAML parameter file and both per-spike files in the
binary form; the `.clp` is optional. A `.clu` without a `.clc` sibling is an
ordinary flat clustering. See
[Hierarchical clustering](../../klusters/hierarchical-clustering.md).

## See also

- [`.res`](res.md), [`.fet`](fet.md), [`.spk`](spk.md) — the row-aligned
  siblings.
- [Cluster sorting](../../klusters/cluster-sorting.md).

---

*Part of the [ndmanager-plugins](../README.md) reference.*
