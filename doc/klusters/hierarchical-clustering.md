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
toggled). **View → Show Child Cluster Palette** hides and shows the child
palette within the session; hidden, the selection, the displays and the
matrices work on the fibers, and the Hierarchy menu's edits are disabled. A plain `.clu.N`, or one with a `.clc` but no `.clp`, opens as an
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

### Keys (while a palette holds focus)

| Key | Operation |
|---|---|
| `G` | **Adaptive merge.** Merges 2+ selected atoms (same fiber); or, with atoms unselected, collapses one selected fiber's atoms; or folds 2+ selected fibers into one. |
| `Ctrl+Up` | **New fiber** from the selected atoms (or from all atoms of 2+ selected fibers). |
| `Ctrl+Down` | **Group** the selected fibers into one. |
| `Ctrl+Shift+Down` | **Dissolve** the selected fiber into its atoms. |

These keys can be rebound in **Preferences → Input**.

### Hierarchy menu

The **Hierarchy** menu holds the same edits and a few more. Apart from
*Promote to Hierarchical Session*, its entries are enabled only while the
child palette is shown.

| Menu entry | Shortcut | What it does |
|---|---|---|
| Merge Selected Parents | — | Merges the fibers selected in the main palette; their atoms all move under the kept fiber. |
| Promote Child to New Parent | — | Makes each selected atom a fiber of its own (an atom that is already its fiber's only atom is left as is). |
| Group Selected Children into New Parent | — | Takes the selected atoms out of their fiber(s) and makes one new fiber of them. |
| Dissolve Parent into Children | — | Each atom of the selected fiber becomes its own fiber (one keeps the fiber's id). |
| Drop Child to Noise | — | Moves the selected atoms' spikes to the noise cluster (1). |
| Compact Cluster IDs (parents and children) | — | Renumbers both layers into the low integers without gaps (0 and 1 stay). Clears the atom undo history. |
| Merge Orphan Children (by median waveform)… | `Shift+B` | Gathers the selected fiber's tiny leftover atoms and merges them into two atoms: those that match the median waveform of the fiber's established atoms, and the rest. Nothing is deleted. |
| Merge Orphan Parents (by template)… | `Ctrl+Shift+B` | Gathers the tiny fibers: each is matched to the template (mean waveform) of the fiber it resembles most among those not being merged; those matching the same fiber are merged into one new fiber beside it, those matching none stay as they are. See [Merge Orphan Parents](#merge-orphan-parents) below. |
| Repair Nesting (re-cut atoms onto parents) | `Shift+N` | See [Repair Nesting](#repair-nesting) below. |
| Merge Selected Children (atom) | — | Merges the selected atoms of one fiber into one atom. |
| Flatten Hierarchy to Flat Clustering | — | Merges every fiber's atoms into one atom with the fiber's id, so the atom layer becomes a copy of the fiber layer. |
| Undo Child-Layer Edit / Redo Child-Layer Edit | `Ctrl+Shift+Z` / `Ctrl+Shift+Y` | Undo or redo in the atom layer's own history. |

The corresponding document methods are `mergeParents`, `promoteChildren`,
`dissolveParent`, `dropChildToNoise`, `mergeChildren`,
`mergeOrphanChildren`, `planOrphanParents` / `mergeOrphanParents`,
`flattenHierarchyToClu` and `repairNesting`;
`childrenOf(fiber)` / `parentOfChild(atom)` query the maps.

### Merge Orphan Parents

The fiber-layer counterpart of *Merge Orphan Children*, for the tiny fibers
that splitting, reclustering and promotions leave in the main palette.

1. It asks for a spike count and a correlation (defaults 25 and 0.90). A
   fiber below the spike count is an **orphan**, one of the fibers being
   merged. Every other fiber is a **template**, whatever its size and whether
   it is selected: its mean waveform (the template the merge recommendations
   use).
2. Each orphan is matched to the template its own mean waveform correlates
   with best (Pearson, so a smaller copy of a unit still matches it). Orphans
   are compared with the fibers that are not being merged, never with each
   other. Orphans whose best correlation reaches the threshold are grouped by
   that template; an orphan below it matches nothing and **stays as it is**.
3. A box shows the plan; **Show Details…** lists every group with its
   correlations and every orphan left alone with its best match. Nothing
   changes until it is accepted.
4. Each group of two or more orphans is merged into one new fiber, beside the
   fiber whose template they matched: that fiber is not changed, and folding
   the new fiber into it is left to you. A group of one is left as it is. The
   orphans' atoms stay apart under the new fiber, and the selection lands on
   the new fibers.

It works on the fibers selected in the main palette, or on all of them when
fewer than two are selected. With a selection, the selected fibers below the
spike count are the orphans and every other fiber, selected or not, is a
template; with none, every fiber below the spike count is an orphan and every
fiber at or above it a template. A high threshold (0.99) therefore merges
little: only orphans nearly identical in shape to the same fiber. The noise and
artefact clusters and the reserved chunk clusters take no part. Each new fiber is one step in **Edit → Undo**, so only as many as the
undo depth (**Preferences → General → Session**) can be undone; the box says
so when the plan needs more.

A fiber that is a reserved chunk cluster (a chunk a fiber-kit chunk run left
unsorted) is left out of *Merge Selected Parents*, *Group*, *Dissolve*, and
the promotions and drops of its atoms; its atoms can still be merged or re-cut
among themselves. Compact Cluster IDs keeps its id. See
[Reserved chunk clusters](README.md#reserved-chunk-clusters).

> **Note.** Earlier builds carried a second child palette and a
> `Ctrl+Left`/`Ctrl+Right` "custody transfer" that moved atoms between two
> side-by-side fibers. The single-palette view retires that gesture; move
> an atom to another fiber via the merge/new-fiber operations instead.

## Atom-level undo/redo

The atom layer has its **own** undo/redo history, separate from the fiber
layer (**Hierarchy → Undo / Redo Child-Layer Edit**, `Ctrl+Shift+Z` /
`Ctrl+Shift+Y`). The main **Edit → Undo/Redo** follows whichever layer is
currently shown, so undo affects atom edits while the child view has focus
and fiber edits otherwise.

## Repair Nesting

**Hierarchy → Repair Nesting** (`Shift+N`, `repairNesting()`) resyncs the
atom layer to the fiber layer: it re-cuts every atom whose spikes now lie in
more than one fiber so that each atom belongs to one fiber again, re-derives
the atom→fiber maps from the current data, and clears the atom undo/redo
history (the atom structure has just been re-cut). The next save writes the
regenerated `.clp`. Fiber edits do not do this on their own: run it after
reassigning or consolidating fibers in a way that leaves atoms straddling
fibers.

## Saving

Saving a hierarchical session regenerates the **whole triple together** so
the layers never drift apart:

- `.clu.N` — the edited fiber layer (as for a flat session).
- `.clc.N` — the per-spike atom layer.
- `.clp.N` — the atom→fiber map the two per-spike layers imply.

All three are taken from one snapshot on the GUI thread when the save
starts (editing is locked until it finishes) and are replaced together with
every other file the save changes, all or nothing (see
[Saving: all or nothing](README.md#saving-all-or-nothing)); a file that
already holds what would be written is left alone. The `.clc.N` and `.clp.N`
keep their previous version as `.bak`. If a sibling cannot be written, the
save fails, names the file, and nothing is replaced.

Autosave protects both layers: the rescue copies of the unit ids and of the
atom ids are written together and restored together.

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
