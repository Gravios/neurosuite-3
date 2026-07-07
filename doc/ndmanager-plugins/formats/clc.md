# `.clc.N` — child (atom) cluster assignments

The **child layer** of a hierarchical clustering session. Same binary
(or legacy text) layout as [`.clu.N`](clu.md): an `int32_t` header giving
the cluster count, followed by one `int32_t` cluster ID per spike. Same
spike order as `.res.N`.

Where `.clu.N` assigns each spike to a **fiber** (an assembled unit), the
sibling `.clc.N` assigns the same spike to an **atom** — the finest,
over-split micro-cluster produced upstream (e.g. by fiber-kit's
over-split stage). A fiber is a set of atoms; the map from atoms to
fibers lives in the [`.clp.N`](clp.md) sibling.

Cluster ID conventions match `.clu.N`: `0` = noise/artefact, `1` =
unsorted, `≥ 2` = atoms. Atom IDs are **global** across the group
(`1..nAtoms`), not per-fiber.

## Nesting invariant

Every atom belongs to **exactly one** fiber — the child→parent map must
be a function. Equivalently, for the aligned per-spike `.clu.N` (parent)
and `.clc.N` (child) arrays, all spikes carrying a given `.clc` atom ID
must carry the same `.clu` fiber ID. Klusters enforces this on every
edit and re-derives it with [`refiberize`](../../klusters/hierarchical-clustering.md);
a violation found while loading (when the `.clp` is missing) keeps the
first-seen owner and warns.

## Relationship to the other files

A hierarchical session is the triple `.clu.N` + `.clc.N` + `.clp.N`
sharing one anchor (base name, method tag, group, optional suffix). All
three are read on open and **regenerated together on save**. See
[Hierarchical clustering](../../klusters/hierarchical-clustering.md) for
the full model and the Klusters operations that edit it.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
