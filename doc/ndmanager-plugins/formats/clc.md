# `.clc.<method>.N` — child (atom) cluster assignments

The child layer of a hierarchical clustering: for every spike of group `N`, the
**atom** — a fine, over-split micro-cluster — it belongs to. The sibling
[`.clu`](clu.md) assigns the same spike to a **fiber** (an assembled unit);
a fiber is a set of atoms, and [`.clp`](clp.md) records which.

## Layout

Identical to the binary [`.clu`](clu.md):

```
int32            header (cluster count)
nSpikes × int32  atom id per spike, .res order
fileSize = 4 + nSpikes × 4
```

Readers check the length against the group's spike count and refuse a mismatch.
Klusters reads it with `neurofileio::readCluBinary`, so the binary form is
required.

- Atom ids are **global** within the group (`1 … nAtoms`), not numbered per
  fiber.
- `0` = noise, as in `.clu`.

## Nesting invariant

Every atom belongs to exactly one fiber: all spikes with the same `.clc` id
carry the same `.clu` id. Klusters builds the atom → fiber map by scanning the
two per-spike files. On a violation it keeps the first-seen fiber for each atom
and warns; with the atom layer loaded, its edits re-cut atoms that straddle
fibers (also available as Repair Nesting).

## Naming

`.clc` is **MethodSpecific** in the [naming convention](naming.md) and is
treated as a cluster-id file like `.clu` (`custody::isClusterIdType`). Klusters
finds it by replacing `.clu.` with `.clc.` in the name of the opened `.clu`, so
the two share base, method, group and suffix:
`rat01.clu.stderiv_C5.3.microfiber` ↔ `rat01.clc.stderiv_C5.3.microfiber`.

A `.clc` can also be opened in Klusters on its own, as a flat clustering of the
atoms.

## Writers and readers

- **Written** by fiber-kit's over-split stage, and rewritten by Klusters on Save
  (with a `.bak` copy) when the atom layer was loaded during the session.
  `process_spikefilter.py` and the other row-dropping steps keep it aligned with
  the `.res`.
- **Read** by Klusters (hierarchical view, loaded when the view is first
  opened, and the nesting scan on open and Save).

## See also

- [`.clu`](clu.md), [`.clp`](clp.md) — the other two files of the triple.
- [Hierarchical clustering](../../klusters/hierarchical-clustering.md) — the
  model and the Klusters operations that edit it.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
