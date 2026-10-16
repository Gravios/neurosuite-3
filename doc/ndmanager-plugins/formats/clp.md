# `.clp.<method>.N` — atom → fiber map

The linkage file of a hierarchical clustering: for each atom of the
[`.clc`](clc.md) layer, the fiber of the [`.clu`](clu.md) layer it belongs to.
It is indexed by **atom id**, not by spike.

The per-spike `.clu` and `.clc` are the source of truth; the `.clp` is a cache
of the map they imply.

## Layout

Binary, little-endian, the `.clu` framing:

```
int32            header (highest atom id)
nAtoms × int32   parent fiber id, entry i = atom i + 1
nAtoms = (fileSize − 4) / 4
```

- Atom ids are global and 1-based: `parent[c − 1]` is the fiber of atom `c`.
- `0` means the atom is noise / unmapped.
- Readers derive `nAtoms` from the file size and ignore the header.

## How Klusters uses it

On open, Klusters builds the atom → fiber map by scanning the per-spike `.clu`
and `.clc`:

- With both readable, the scan wins. The `.clp`, if present, is compared and a
  stale one is reported (`.clp is STALE`); it is regenerated on the next Save.
- Only when the per-spike files cannot be read is the map built from the `.clp`
  alone, with a warning that it may not reflect the current curation.

On **Save** Klusters rewrites an existing `.clp` from its map (keeping a `.bak`
copy) whenever the session is hierarchical, even if the hierarchical view was
never opened. It does not write the `.clp` when the scan found an atom under two
fibers and the atom layer was never loaded — that map would record an arbitrary
owner; open the hierarchical view (or use Repair Nesting) and save again. A Save
does not create a `.clp` that did not exist; **Promote to Hierarchical Session**
does (it is the step that makes a flat `.clu` hierarchical), and Save As of a
hierarchical session writes one under the new name.

Klusters writes the `.clc`/`.clp` to `<file>.saving` and renames them into place
after the `.clu` is committed, so an interrupted save never leaves a truncated
file.

## Naming

`.clp` is not in the custody type tables ([naming](naming.md#files-outside-the-type-tables)).
Klusters finds it by replacing `.clu.` with `.clp.` in the name of the opened
`.clu`, so the triple shares base, method, group and suffix:
`rat01.clu.stderiv_C5.3` / `rat01.clc.stderiv_C5.3` / `rat01.clp.stderiv_C5.3`.

## Writers and readers

- **Written** by fiber-kit together with the `.clc`, by Klusters' Promote to
  Hierarchical Session, and rewritten by Klusters on Save.
- **Read** by Klusters.

## See also

- [`.clu`](clu.md), [`.clc`](clc.md).
- [Hierarchical clustering](../../klusters/hierarchical-clustering.md).

---

*Part of the [ndmanager-plugins](../README.md) reference.*
