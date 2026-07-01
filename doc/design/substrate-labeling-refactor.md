# Substrate / labeling split for the flat + hierarchical clustering model — design

**Status:** design only. This is a multi-phase, in-memory refactor of the klusters
`Data` model. It changes no on-disk format — Save still regenerates the
`.clu`/`.clc`/`.clp` triple. It is grounded in the current interfaces
(`KlustersDoc`, `Data`, `ErrorMatrixThread`) as of the `clusterFeaturesReprojected`
patch (HEAD `ee90be9`).

---

## 1. The problem

A klusters session is either **Flat** (a parent `.clu` clustering only) or
**Hierarchical** (parent fibers plus a `.clc` child "microfiber"/atom layer). The
mode is committed at open from the file set and has no runtime toggle
(`KlustersDoc::clusteringMode()`; Hierarchical iff both `.clc` and `.clp` are
present).

Both clusterings are represented as full `Data` objects:

- `clusteringData` — the parent (`.clu`) clustering, always present.
- `childData` — the child (`.clc`) clustering, lazily built by
  `loadChildClustering()`.
- `activeData` — points at whichever the views render; `data()` returns it.

The two `Data`s are clusterings **over the same spikes**. `Data` owns
`Array<dataType> features` by value (`data.h:1006`), and `loadChildClustering()`
builds the child by re-reading the `.fet`/`.spk` into a second `Data`. The code
says so directly:

> *v1 re-reads the feature/spike arrays; they could later be shared with the
> parent to halve memory.*

So loading the hierarchy **duplicates the entire feature substrate in RAM** — for
the reference session that is the `.fet` (and the on-demand `.spk` reader state),
copied. The parent and child differ only in their cluster assignment
(`spikesByCluster` + `clusterInfoMap`, built from `.clu` vs `.clc`).

### 1.1 Why this is more than a memory issue

`features` is **not read-only**. Realign rewrites it in place via
`Data::updateFeatureRow`. `loadChildClustering()` already carries a comment
worrying that if a realign repointed the parent to the pending (realigned) `.spk`,
the child must follow it or it opens on the original waveforms. With two
independent copies of the substrate this is a special case that has to be
maintained by hand; with one shared substrate it cannot happen. **Sharing the
substrate is a correctness fix as well as a memory fix.**

### 1.2 What is *not* the problem

The view layer is already mode-agnostic: `data()` (the active clustering) is used
~175×, `parentData()` once, and the flat/hierarchical fork is concentrated in ~31
sites (14 `childData` guards + ~17 mode checks), almost all of them child-layer
cache invalidation. The dual-mode branching is contained and well-commented. The
wart is the duplicated substrate, not the mode switch.

## 2. Target model

Separate the **substrate** (spike-indexed, shared) from the **labeling**
(per-clustering):

- **`SpikeStore`** — one copy of `features`, the per-dimension extents
  (`dimensionMaxima`/`dimensionMinima`), sample geometry (nSamp/peak/sr), the
  `.spk` filename + on-demand waveform reader, and the par/electrode metadata.
- **`ClusterLayer`** — a labeling (`spikesByCluster` + `clusterInfoMap` + colours)
  bound to a `SpikeStore`. One layer per clustering.

Under this model a clustering is `SpikeStore + ClusterLayer`, and:

- **Flat = one layer; Hierarchical = two** (fiber + atom) sharing one store.
- `data()`/active clustering means "the layer the views render" — unchanged
  semantics.
- The parent↔child map is **derived** from co-membership rather than stored: the
  nesting invariant (a child's spikes are a strict subset of its parent's) makes
  `parent(atom)` the fiber label of any of the atom's spikes. `childToParent` /
  `buildHierarchyMaps()` / the `.clp` become a persistence detail, not the
  in-memory source of truth.
- `clusteringMode()` degenerates to `layerCount()`; the 14 `childData` guards
  become "for each non-base layer, invalidate."

This answers the original question — *should we drop Flat and go hierarchical-only?*
— with **no**: the N-layer model subsumes both. A flat session simply has one
layer and carries no atom overhead; no synthetic identity atoms are ever
manufactured. The distinction dissolves into a data property instead of being
forced one way.

The code already has the seam for this: `Data::labelByFeatureRow()` and its inverse
(rebuild `spikesByCluster` + `clusterInfoMap` from a per-row label vector,
`data.h:369`) round-trip between the two representations, so a `ClusterLayer` is a
label vector over a `SpikeStore`.

## 3. Phases

Each phase is one or a few single-concern patches, `git am`-verified on a fresh
clone at upstream HEAD, syntax-checked with the Qt6 `g++ -fsyntax-only` proxy, then
built and smoke-tested against a hierarchical session (groups 4/5) on the RTX
hardware. Ship in order; every phase stands alone.

### Phase 0 — Draw the substrate/labeling line (analysis, no code)

Audit every `Data` member and classify it: substrate, labeling, or derived cache.
Working split:

| Class | Members |
|---|---|
| Substrate | `features`, `dimensionMaxima`/`dimensionMinima`, `spkFileName` + `.spk` reader, `nbSpikes`, sample geometry, par/electrode metadata |
| Labeling | `spikesByCluster`, `clusterInfoMap`, colour list |
| Derived / cache | waveform sample/mean/stdev tables, correlograms |

The output is a written contract of which members move. It settles the boundary
before any edit and de-risks Phase 1.

### Phase 1 — Extract a `SpikeStore` value type (mechanical, no sharing)

Move the substrate members into a `SpikeStore` that `Data` still owns **by value**.
Route `features(...)`, the dim extents, and the spk accessor through it. Parent and
child still hold independent stores — nothing semantic changes. This isolates the
large-but-mechanical member move ("identical behaviour") from the small-but-semantic
ownership change in Phase 2. Verify with the syntax proxy plus a feature-accessor
equivalence run.

### Phase 2 — Share the `SpikeStore` (memory + correctness fix)

Move ownership to a `shared_ptr<SpikeStore>` held by the document. Add a
`Data::initialize` overload that takes an existing store and reads only the `.clc`
cluster file to build the child's labeling; `loadChildClustering()` stops
re-reading `.fet`/`.spk`. Effects:

- The `.fet` duplication disappears when the hierarchy is loaded.
- The child tracks parent realigns automatically (single source of truth); the
  "child must follow the realigned `.spk`" special case can be simplified out.
- **Guard:** only the parent (fiber) layer mutates the store — this is the existing
  "edit via `parentData()`" rule. Assert that child-scope never writes the store.
  The nesting invariant guarantees a parent feature-row rewrite propagates to the
  atoms correctly.

Highest value-to-risk phase; ship on its own. Verify: hierarchy-loaded memory drops
by the `.fet` size; parent-vs-child feature values assert-identical; a parent
realign is visible to the child with no reload.

### Phase 3 — Child as a label layer, not a full `Data` (the real lift)

With storage shared, the child is mostly just its labeling over the common store.
Introduce `ClusterLayer` and derive `childToParent` from co-membership instead of
`buildHierarchyMaps()` + `.clp`. This is the largest change and the one that needs a
design review before commit — the judgment is in how `ClusterLayer` interacts with
realign, undo, and Save. Do it only once Phase 2 is stable.

### Phase 4 — Generalize mode to layer-count; retire the fork (optional)

`clusteringMode()` → `layerCount()`; Flat = 1, Hierarchical = 2. `data()`/active
stays "the rendered layer." The `childData` guards collapse to a loop over non-base
layers. Decide whether to keep an explicit flat fast-path or treat everything
uniformly — with the substrate shared, a flat session carries no extra storage
either way.

## 4. Invariants to hold across every phase

1. **`.res`/`.clu` 1:1 alignment must never shift.** `spikesByCluster` is the
   canonical sorted order; any rebuild must be bit-exact. Add a
   `labelByFeatureRow` ↔ rebuild round-trip self-check under a verify flag (same
   style as the error-matrix self-check) so a regression logs an assertion instead
   of silently corrupting `.res`.
2. **Edits and Save stay pinned to the fiber (parent) layer** — never mutate through
   child scope.
3. **Realign is a substrate-level mutation.** Undo of a realign restores the shared
   store, not a per-layer copy; there is one substrate mutation stack.
4. **On-disk format unchanged.** Save still regenerates the `.clu`/`.clc`/`.clp`
   triple byte-compatibly throughout.

## 5. Risk read

Phases 1–2 are mostly mechanical and high-confidence — the member move and the
ownership swap. Phase 3 is where the design judgment lives (layer/realign/undo/save
interaction) and should be treated as a review point, not a straight-through patch.
Phase 4 is optional cleanup that only becomes cheap and safe once 1–3 land.
