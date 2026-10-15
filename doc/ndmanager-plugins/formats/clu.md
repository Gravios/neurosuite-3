# `.clu.<method>.N` — cluster assignments

The cluster id of every spike of group `N`, one entry per [`.res`](res.md) row,
in the same order. In a hierarchical session it is the parent (fiber) layer of
the [`.clc`](clc.md) / [`.clp`](clp.md) triple.

## Layout

Binary, little-endian:

```
int32            header (cluster count)
nSpikes × int32  cluster id per spike, .res order
fileSize = 4 + nSpikes × 4
```

- **Length check:** readers take `nSpikes` from the `.res` / `.spk` and refuse a
  `.clu` that holds a different number of ids (`neurofileio::readCluBinary`
  reports how many it found). A `.clu` belonging to another run, group or method
  is rejected, not truncated.
- **Header:** informational. KiloKlustaKwik writes its highest id, Klusters the
  number of distinct clusters. Klusters refuses a header outside 0–65536; other
  readers ignore it.
- **Ids by convention:** `0` = noise / artefact, `1` = unsorted multi-unit
  activity, `≥ 2` = candidate units. KiloKlustaKwik renumbers its output without
  gaps from 1 (its noise class becomes 1). Nothing enforces the convention.

A plain-text form (first line the cluster count, then one id per line) is read
by `neurofileio::readClusterRes` when the paired `.res` is also text
(NeuroScope, the template generator). Klusters, `process_spikefilter.py` and the
other plugins read binary only.

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
