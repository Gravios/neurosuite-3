# Hierarchical clustering (fibers and atoms)

Klusters can open a **two-level** clustering in which the units you curate
(*fibers*) are assembled from a finer, over-split layer (*atoms*). This
mirrors the upstream fiber-kit workflow: the pipeline deliberately
over-splits each putative unit into many small, drift-stable atoms, then
links atoms into fibers. The hierarchical session lets you inspect and
re-cut that assembly by hand without losing the atom layer.

| Term | Layer | File | Meaning |
|---|---|---|---|
| **Fiber** | parent | [`.clu.N`](../ndmanager-plugins/formats/clu.md) | The assembled unit you curate. One fiber ID per spike. |
| **Atom** | child | [`.clc.N`](../ndmanager-plugins/formats/clc.md) | A finest-grain micro-cluster. One atom ID per spike. |
| **Map** | linkage | [`.clp.N`](../ndmanager-plugins/formats/clp.md) | Which fiber each atom belongs to (indexed by atom ID). |

All three share one anchor (`<base>.<type>[.<method>].<group>[.<suffix>]`)
and are read on open and rewritten together on save.

## The nesting invariant

Every atom belongs to **exactly one** fiber. Equivalently, all spikes
that carry a given `.clc` atom ID carry the same `.clu` fiber ID. The
`.clp.N` records that atom→fiber function directly and is authoritative;
if it is missing, Klusters reconstructs it by scanning the aligned
`.clu`/`.clc` arrays (first-seen owner wins, with a warning on any
violation). Every hierarchy edit below preserves the invariant.

## Opening a hierarchical session

Open the fiber file (`<base>.clu.N`) as usual. If the `.clc.N` and
`.clp.N` siblings are present, **Displays → Hierarchical Session
(.clc + .clp)** becomes available and, on a valid triple, enables
automatically. A plain `.clu.N` with no siblings opens as an ordinary
flat clustering and the toggle stays disabled.

When the hierarchical view is on:

- The **main (left) cluster palette** lists the **fibers**.
- A second **Child clusters (.clc)** dock lists the **atoms of the
  currently selected fiber**, scope-bound to that fiber's children.
- Selecting a fiber repopulates the child dock with its atoms; selecting
  atoms drives the scatter/waveform/correlogram views to the union of the
  selected atoms (with no atom selected the views fall back to the parent
  fiber).

Only **one** child palette is shown; selecting a different fiber replaces
its contents. Keyboard focus cycles **parent → child → toolbar fields**
with `Tab` (`Shift+Tab` reverses) while the hierarchical view is open.

## Hierarchy operations

These act on the current fiber/atom selection while a palette holds focus.
They reuse the same document primitives as the **Hierarchy** menu.

| Key | Operation |
|---|---|
| `G` | **Adaptive merge.** Merges 2+ selected atoms (same fiber); or, with atoms unselected, collapses one selected fiber's atoms; or folds 2+ selected fibers into one. |
| `Ctrl+Up` | **New fiber** from the selected atoms (or from all atoms of 2+ selected fibers). |
| `Ctrl+Down` | **Group** the selected fibers into one. |
| `Ctrl+Shift+Down` | **Dissolve** the selected fiber into its atoms. |

The corresponding document methods are `mergeChildren`,
`groupChildrenIntoFiber`, `mergeParentFibers`, and `dissolveFiber`;
`childrenOf(fiber)` / `parentOfChild(atom)` query the maps.

> **Note.** Earlier builds carried a second child palette and a
> `Ctrl+Left`/`Ctrl+Right` "custody transfer" that moved atoms between two
> side-by-side fibers. The single-palette view retires that gesture; move
> an atom to another fiber via the merge/new-fiber operations instead.

## Atom-level undo/redo

The atom layer has its **own** undo/redo history, separate from the fiber
layer. The main **Edit → Undo/Redo** follows whichever layer is currently
shown, so undo affects atom edits while the child view has focus and fiber
edits otherwise. Dissolving/merging keeps each layer's timeline coherent.

## `refiberize` — resync the atom layer

`refiberize` performs a deliberate full resync of the atom layer to the
fiber layer: it collapses any loose spikes to self-atoms, re-derives the
`childToParent` / `parentToChildren` maps from the current data, and
resets the atom undo/redo history (the atom structure has just been
re-cut). The next save writes the regenerated `.clp` from those maps. It
is used after operations that can leave atoms straddling fibers.

## Saving

Saving a hierarchical session regenerates the **whole triple together** so
the layers never drift apart:

- `.clu.N` — the edited fiber layer (as for a flat session).
- `.clc.N` — the per-spike atom layer.
- `.clp.N` — the edited atom→fiber map.

The `.clc.N` and `.clp.N` are overwritten in place, each after a `.bak`
copy of the previous version. If any sibling fails to write, Klusters
warns and reports the save as failed.

## Internals

Under the hood the child layer is a second `Data` (`childData`) built over
the **same** `.fet`/`.spk`/parameters as the parent but with the `.clc` as
its cluster file (following the parent's active `.spk`, including a
realigned pending file). It has its own colour list (same HSV scheme) and
its own scope set. `activeData` switches between the parent
(`clusteringData`) and the child (`childData`) as focus moves, so the
existing single-layer editing/undo code works against "the active layer"
unchanged.

---

*Part of the [klusters](README.md) documentation.*
