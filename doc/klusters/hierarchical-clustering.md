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
per-spike `.clu` and `.clc` are the source of truth: Klusters builds the
atom→fiber map by scanning them (first-seen owner wins, with a warning on
any violation). The `.clp.N` records the same function and is a cache of
it: it is compared with the scan on open, reported if stale, and rewritten
on save; only when the per-spike files cannot be read is the map taken from
the `.clp` alone. Every hierarchy edit below preserves the invariant.

## Opening a hierarchical session

Open the fiber file (`<base>.clu.N`) as usual. If the `.clc.N` and
`.clp.N` siblings are both present, the session opens hierarchical and the
child palette is shown; **Displays → Hierarchical Session (.clc + .clp)**
is checked as an indicator (the mode is fixed when the file is opened, not
toggled). A plain `.clu.N`, or one with a `.clc` but no `.clp`, opens as an
ordinary flat clustering. To make a flat clustering hierarchical, use
[Promote to Hierarchical Session](#promoting-a-flat-clustering).

## Promoting a flat clustering

**Hierarchy → Promote to Hierarchical Session (.clc + .clp)…** turns the
open flat clustering into a hierarchical one, for a `.clu` that did not come
from fiber-kit:

- With no `.clc` yet, Klusters writes one in which **every unit is one atom
  with the same id** (the artefact bin 0 gets atom 0), and the `.clp` that
  maps each atom to its unit. Nothing about the clustering changes; the
  child palette then shows each unit as a single atom, ready to be split
  (`Ctrl+Up` makes a new fiber from selected atoms, as usual). This is the
  same state **Flatten Hierarchy** leaves.
- A `.clc` that is already there (one without its `.clp` is ignored on
  open) is **kept** when it nests in the `.clu`, and only its `.clp` is
  written. A `.clc` that does not nest, or has the wrong number of entries,
  is refused with the reason; move it aside to promote from the `.clu`
  alone. An existing `.clp` is replaced, its previous content kept with a
  `.bak` suffix.
- Promotion reads the `.clu` on disk, so unsaved edits are **saved first**
  (Klusters asks). The session is then **reopened** as a hierarchical
  session, which clears the undo history.
- It needs a cluster file named `<base>.clu.<group>[…]` — the new files are
  named after it by replacing `.clu.` — and the session's YAML parameter
  file.

The command id is `hierarchy.promoteToHierarchical`.

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
- `.clp.N` — the atom→fiber map the two per-spike layers imply.

All three are taken from one snapshot on the GUI thread when the save
starts, so editing can continue while it runs; an edit made during the save
leaves the document modified. The `.clc.N` and `.clp.N` are written to
`<file>.saving` first and renamed into place, each after a `.bak` copy of
the previous version, only once the `.clu` has been committed — a save that
fails earlier leaves all three files as they were. If a sibling cannot be
written, the save is reported as failed with the file named.

When the atom layer is not loaded (its load failed) and an edit has left an
atom under two fibers, nothing has re-cut it: the `.clp` is left as it was
and Klusters says so after the save ("Saved, with a note"). Load the
hierarchical view, use **Repair Nesting**, and save again.

**Save As** writes the new `.clu` together with a `.clc` and `.clp` named
after it (the `.clu.` in the new name becomes `.clc.` / `.clp.`), and the
session continues on the new triple. The new name must therefore contain
`.clu.`; Klusters refuses one that does not before writing anything.

## Internals

Under the hood the child layer is a second `Data` (`childData`) built over
the **same** `.fet`/`.spk`/parameters as the parent but with the `.clc` as
its cluster file (following the parent's active `.spk`, including a
realigned pending file). It has its own color list (same HSV scheme) and
its own scope set. `activeData` switches between the parent
(`clusteringData`) and the child (`childData`) as focus moves, so the
existing single-layer editing/undo code works against "the active layer"
unchanged.

---

*Part of the [klusters](README.md) documentation.*
