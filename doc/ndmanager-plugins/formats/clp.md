# `.clp.N` — child→parent (atom→fiber) map

The **linkage** file of a hierarchical clustering session: which fiber
each atom belongs to. Unlike `.clu.N`/`.clc.N`, this is indexed by
**cluster ID**, not by spike, so it is `O(nAtoms)` rather than
`O(nSpikes)`.

## Layout

Binary, little-endian, same clu-family framing as [`.clu.N`](clu.md):

```
int32_t   header                     (4 bytes)
nAtoms ×  int32_t  parentFiberId      (one per atom, indexed by atom ID)
```

- The number of atoms is derived from the file size:
  `nAtoms = (fileSizeBytes − 4) / 4`. The 4-byte header is written (it
  carries the highest atom ID) but readers ignore it and use the size.
- Entry `i` (0-based) is the parent **fiber** ID of atom `i + 1` — atom
  IDs are global and 1-based, so `parent[c − 1]` owns atom `c`.
- A parent ID of `0` means the atom is noise / unmapped and is skipped
  when building the hierarchy maps.

## Role

The `.clp.N` is the **authoritative** source for the atom→fiber map:
it is exact even if an atom ID were reused across the per-spike arrays,
and it needs no per-spike scan. When it is present and readable, Klusters
builds `childToParent` / `parentToChildren` directly from it. When it is
absent or unreadable, Klusters falls back to scanning the aligned
[`.clu.N`](clu.md) (parent) and [`.clc.N`](clc.md) (child) per-spike
arrays, keeping the first-seen owner for each atom and warning if the
nesting invariant is violated.

## Lifecycle

`.clp.N` is one third of the hierarchical triple `.clu.N` + `.clc.N` +
`.clp.N`. On **save**, Klusters rewrites the `.clc.N` and `.clp.N`
siblings together from the edited hierarchy (each with a `.bak` backup),
so the fiber layer (`.clu.N`), the atom layer (`.clc.N`), and this map
stay consistent. See
[Hierarchical clustering](../../klusters/hierarchical-clustering.md).

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
