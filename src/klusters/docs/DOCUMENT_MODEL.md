# The Klusters document model (`KlustersDoc`)

*Part of the [Developer Guide](../../../doc/dev/DEVELOPER_GUIDE.md), the index of internals references.*

A developer reference for the cluster/spike document layer of Klusters. Every
structural claim below cites the file in `src/klusters/src/` and the symbol
(class, member, function) it is about; grep for the symbol to find the line.
Line numbers are not given because they drift. Anything not directly read out
of the code is marked **(unverified)**.

<details>
<summary>Sources this reference was written against</summary>

**Source note.** This reference was written against `klustersdoc.h`, the
`klustersdoc_*.cpp` family (`klustersdoc.cpp`, `_edit`, `_io`, `_undo`,
`_renumber`, `_hierarchy`, `_recluster`, `_watershed`, `_dipsplit`,
`_realign`, `_strip`, `_featurespace` — feature-space switching and sync —
and `_timesort` — Sort Spikes by Time), `data.h`/`data.cpp`, and
`../../libklustersshared/src/klustersshared/types.h`.
There is **no** `klustersdoc_sort.cpp`; the sort UI lives in `klusters_sort.cpp`
(the `klusters_*` app family, not the document). The rename/renumber primitive
lives in `klustersdoc_renumber.cpp` and the undo stack in `klustersdoc_undo.cpp`.
Cross-references to `STANDARDIZATION.md` use its section numbers.

</details>

**Contents:** [1. Roles and ownership](#1-roles-and-ownership) · [2. The `Data` store — spikes, features, waveforms](#2-the-data-store--spikes-features-waveforms) · [3. Epochs and snapshots (the `Data`-level undo substrate)](#3-epochs-and-snapshots-the-data-level-undo-substrate) · [4. Curation primitives and the undo rule](#4-curation-primitives-and-the-undo-rule) · [5. Undo/redo stack mechanics (`KlustersDoc`)](#5-undoredo-stack-mechanics-klustersdoc) · [6. Document → view signals](#6-document--view-signals) · [7. File load and save](#7-file-load-and-save) · [8. Cross-check against `STANDARDIZATION.md`](#8-cross-check-against-standardizationmd)

---

## 1. Roles and ownership

`KlustersDoc : public QObject` (`klustersdoc.h`) is the document layer in a
classic document/view split: it owns the clustering model and the file-backed
session, drives every curation operation, and notifies the views. It is always
constructed with a `KlustersApp` parent (`klustersdoc.h` casts `parent`
straight to `KlustersApp*`).

`KlustersDoc` **owns**, as direct members:

| What | Member | Decl |
|---|---|---|
| The spike/feature/cluster data store | `Data* clusteringData` | `klustersdoc.h` |
| Optional child (`.clc`) clustering + its colours | `Data* childData`, `ItemColors* childColorList` | `klustersdoc.h` |
| View-facing active store / colours | `Data* activeData`, `ItemColors* activeColorList` | `klustersdoc.h` |
| Cluster colours **and** per-cluster status | `ItemColors* clusterColorList` | `klustersdoc.h` |
| Colour undo/redo snapshots | `QList<ItemColors*> clusterColorListUndoList / …RedoList` | `klustersdoc.h` |
| Per-action edit descriptors (undo/redo) | `QList<ClusterEditUndo> editUndoList / editRedoList` | `klustersdoc.h` |
| Connected views | `QList<KlustersView*>* viewList` | `klustersdoc.h` |
| Cluster palette (back-reference) | `ClusterPalette& clusterPalette` | `klustersdoc.h` |
| Curation log | `std::unique_ptr<CurationLogger> curationLogger` | `klustersdoc.h` |
| Pending-file paths + deferred realign writes | `pendingSpkPath…`, `std::vector<PendingRealign> pendingRealign` | `klustersdoc.h` |
| Hierarchy maps + atom-layer undo stacks | `parentToChildren`, `childToParent`, `childUndoStack`, `childRedoStack` | `klustersdoc.h` |

**The cluster↔spike mapping itself is not a `KlustersDoc` member** — it lives
inside `Data` (§2). `KlustersDoc` holds the *presentation* state (colours,
status, selection, masking, undo bookkeeping, hierarchy) and delegates the
membership model to `Data`.

Accessors the rest of the app uses:

- `Data& data() const` → `activeData` if set, else `clusteringData` (`klustersdoc.h`).
- `Data& parentData() const` → always `clusteringData` (`klustersdoc.h`).
- `Data& childClusterData() const` → `childData` if loaded (`klustersdoc.h`).
- `ItemColors& clusterColors() const` → `activeColorList ? … : clusterColorList` (`klustersdoc.h`).
- `void addView/removeView(KlustersView*)` (`klustersdoc.h`); `viewList` is the notify set.

`setActiveClustering(bool child)` (`klustersdoc.h`) swaps `activeData` /
`activeColorList` between the parent and child layers; the two layers keep
**independent** undo timelines (`klustersdoc.h`).

### `types.h`

`types.h` defines the two numeric aliases used throughout the model
(`libklustersshared/src/klustersshared/types.h`):

```cpp
typedef long      dataType;          // cluster ids, feature values, spike counts/indices
typedef long long recordingUnitType; // sample timestamps
```

All cluster ids, feature-row indices and feature values flow as `dataType`.

---

## 2. The `Data` store — spikes, features, waveforms

`class Data` (`data.h`) is the single, non-copyable document-model instance
(`data.h` deletes copy/move). It holds three parallel structures plus a
feature matrix.

### 2.1 Storage

| Store | Declaration | Shape |
|---|---|---|
| Feature matrix | `Array<dataType> features;` (`data.h`) | row-major, **1-based** rows; one row per spike, one column per feature dimension |
| Row table | `std::shared_ptr<SortableTable> spikesByCluster;` (`data.h`) | 2 rows × `nbSpikes` columns: row 1 = the spike's feature-row index, row 2 = its cluster id; **sorted by (cluster id, then time)** (`data.h`) |
| Per-cluster index | `std::shared_ptr<ClusterInfoMap> clusterInfoMap;` where `typedef QMap<dataType,ClusterInfo> ClusterInfoMap;` (`data.h`) | key = cluster id → `ClusterInfo` |
| Spike count | `long nbSpikes;` (`data.h`) | total spikes in the group |

`ClusterInfo` (`data.h`) stores the cluster's **contiguous span**
inside `spikesByCluster` plus optional user metadata:

- `firstSpikePosition()` / `position` — 1-based start column in `spikesByCluster` (`data.h`).
- `nbSpikes()` / `spikeNb` — number of columns for this cluster (`data.h`).
- user fields: `structure`, `type`, `ID` (isolation distance), `quality`, `notes` (`data.h`).
- `position`/`spikeNb` are **zero-initialised** so a `QMap::value()` miss yields a
  benign empty span rather than an out-of-bounds read (`data.h`).

So a cluster's spikes are the columns `firstSpikePosition() …
firstSpikePosition()+nbSpikes()-1` of `spikesByCluster`; row 1 of each column
indexes into `features`.

> **`clusterInfoMap` belongs to `Data`, not `KlustersDoc`.** `STANDARDIZATION.md`
> §2.5/§6.2 speak of "`clusterInfoMap`" as if it were a doc field; in the code
> it is `Data`'s member (`data.h`) and the §2.5 "never pre-mutate" rule is a
> rule about the **`Data` mutators** (§4.1). The doc never touches it directly.

### 2.2 Access — the `Iterator` and `features()`

Spikes of one cluster are walked via `Data::Iterator` (`data.h`),
created by `Data::iterator(clusterId)` (`data.h`). Its constructor reads
the cluster's span from `clusterInfoMap->value(clusterId)` (`data.h`,
using `value()` not `operator[]` to avoid inserting a phantom entry).

Two call operators:

- `QPoint operator()(dataType dimX, dataType dimY) const` (`data.h`) —
  returns a scatter-space point **with the ordinate already negated**:
  ```cpp
  return QPoint(data.features(featuresRowIndex, dimX),
              - data.features(featuresRowIndex, dimY));   // data.h
  ```
- `dataType operator()(dataType dim) const` (`data.h`) — raw feature value, no flip.
- `featureRow()` (`data.h`) — the current spike's 1-based feature row (maps a
  drawn point back to its `.spk`/`.eap` row).

> **§2.3 ordinate-negation, location.** The convention is real and
> load-bearing: *never* re-flip Y downstream of the iterator. The negation is in
> the iterator's `QPoint operator()(dataType, dataType)` in `data.h`, whose
> doc-comment restates the convention. `STANDARDIZATION.md` §2.3 cites it by a
> line range (`data.h:405-409`) that now points at a `createNewCluster`
> overload.

Feature values are read through `data.features(row, dim)` — 1-based row,
1-based dimension.

### 2.3 Id allocation — `nextFreeClusterId`

```cpp
dataType highestClusterId() const {            // data.h
    if (nbSpikes == 0) return 0;
    return (*spikesByCluster)(2, nbSpikes);    // last column's cluster id (table is id-sorted)
}
dataType nextFreeClusterId() const {           // data.h
    return highestClusterId() + 1;
}
```

This is the canonical, gap-ignoring "new clusters go at the tail" policy
(`data.h`), matching §2.3. Because `spikesByCluster` is kept id-sorted,
the last column holds the max id; renumber operations that repack the table
exist precisely to keep this honest (`data.cpp`, §4.1).

Related read-only helpers: `clusterIds()` = `clusterInfoMap->keys()`
(`data.h`, ascending because `QMap` iterates ascending — §2.3);
`nbOfClusters()` (`data.h`); `clusterHasMembers(id)` (`data.h`).

### 2.4 Waveforms

Waveforms are **not** held in `Data` as an array. The `.spk` file is opened but
read **on the fly** when waveforms need drawing (`klustersdoc_io.cpp`).
`Data` holds a shared `SpkReader` (`std::shared_ptr<SpkReader> spkReaderInstance`,
`data.h`); `installSpkReader()` / `reopenSpkReader()` (`data.h`)
install or rebind it. Each published epoch snapshot carries its own reader
(`data.h`) so an in-flight `WaveformThread` keeps reading the file version
pinned at enqueue time. Waveform fetching is done by `WaveformThread`, a friend of
`Data` (`data.h`).

---

## 3. Epochs and snapshots (the `Data`-level undo substrate)

`Data` versions its membership as immutable **epochs**. `struct
ClusteringSnapshot` (`data.h`) bundles the `(spikesByCluster, clusterInfoMap)`
pair plus that epoch's waveform/correlogram stores, reader and overlay
generation. `currentSnapshot()` (`data.h`) is the live epoch; pool jobs
capture it at enqueue time so a later edit can't free tables a running job still
reads (`data.h`, `1416-1420`, `1484-1486`). See
[CONCURRENCY.md](CONCURRENCY.md).

Undo/redo at the data layer are two snapshot stacks:

```cpp
QList<std::shared_ptr<const ClusteringSnapshot>> undoSnapshots;   // data.h
QList<std::shared_ptr<const ClusteringSnapshot>> redoSnapshots;   // data.h
```

`changedClustersBetween(from, to, renamedFromTo)` (`data.h`) diffs two
epochs **by spike-row content** (so a pure renumber is reported as a rename, not
a change), letting id-keyed caches survive relabels.

---

## 4. Curation primitives and the undo rule

### 4.1 The "read current → write temp → commit" rule (§2.5, §6.2)

Every forward edit of the membership model goes through the single `Data` commit
point:

```cpp
void Data::prepareUndo(SortableTable* spikesByClusterTemp,
                     ClusterInfoMap* clusterInfoMapTemp,
                     bool dimensionChanged);        // decl data.h, def data.cpp
```

The contract (from the body, `data.cpp`):

1. Validate the candidate table; **refuse** an internally inconsistent one and keep
   current state (`data.cpp`).
2. **Push the pre-edit epoch**: `undoSnapshots.prepend(currentSnapshot());`
   (`data.cpp`).
3. Swap the temp tables in under the mutex (`data.cpp`).
4. `publishSnapshot();` — publish the new epoch (`data.cpp`).
5. Trim `undoSnapshots` to `nbUndo`; clear the redo history (`data.cpp`).

**Why you must not pre-mutate `clusterInfoMap`/`spikesByCluster`:** step 2 snapshots
whatever the *live* members currently are. If you mutate them before calling
`prepareUndo`, the "pre-edit" snapshot captures the already-mutated state and undo
cannot return to the real prior state. The correct shape is: build fresh temp
tables from the current state, then hand them to `prepareUndo`, which
snapshots-then-swaps atomically.

`renumberPartial` (`data.cpp`) is the canonical example: it allocates
fresh temps (`data.cpp`), builds them from the current tables without
touching the live ones, and only at the end calls `prepareUndo(...)`
(`data.cpp`). It also refuses a non-injective map — a merge in disguise —
before building anything (`data.cpp`).

### 4.2 `applyClusterRename` — the rename/renumber primitive (§2.5)

```cpp
// public slot — klustersdoc.h
void applyClusterRename(const QMap<int,int>& partialOldToNew,
                      const QMap<int,int>* fullOldToNewOpt = nullptr);
// definition — klustersdoc_renumber.cpp
```

It is the single apply-path for "rename a set of clusters". `partialOldToNew`
lists **only** the renamed clusters; `fullOldToNewOpt` is an optional covering map
(every live id → post-rename id, identity for unchanged), built automatically
when null (`klustersdoc_renumber.cpp`). Order of operations
(`klustersdoc_renumber.cpp`):

1. **Data layer** — `clusteringData->renumberPartial(partialOldToNew)` rewrites the
   spike table + `clusterInfoMap` and **pushes its own data-side undo entry**
   (`klustersdoc_renumber.cpp`; `data.cpp`).
2. **Colour list** — each renamed id relabelled via `clusterColorList->changeItemId`,
   then `sortByItemId()` (`klustersdoc_renumber.cpp`).
3. **S-pins + matrix scope** ride the rename (`klustersdoc_renumber.cpp`).
4. **Each view** — `v->renumberClusters(full, isActive)` + `updateTraceView`
   (`klustersdoc_renumber.cpp`).
5. **Similarity matrices** — `emit renumber(full)` (`klustersdoc_renumber.cpp`).

**`applyClusterRename` does *not* snapshot undo itself.** The caller is responsible
for the doc-level undo (`prepareUndo` / `prepareReclusteringUndo`) and any
`logBefore`/`logAfter` pair (`klustersdoc.h`). Callers include
`renumberClustersToEnd` (T key) and `reorderClustersByPermutation` (Shift+S).

### 4.3 The multi-cluster-creation siblings (§2.5, §2.6)

Operations that *create* clusters use a different trio, which §2.5 calls
`applyClusterRename`'s "siblings":

- `void prepareReclusteringUndo(QList<int>& newClusters, QList<int>& deletedClusters)`
  (decl `klustersdoc.h`; def `klustersdoc_undo.cpp`) — one doc-side
  undo entry recording the created + emptied clusters.
- `KlustersView::addNewClustersToView(…)` — the per-view update (two overloads,
  `klustersview.cpp`), called in a `for v : *viewList` loop.
- `emit newClustersAdded(QList<int>&)` — the recluster-shaped multi-cluster signal (§6).

Single-new-cluster paths use `commitClusterCreation(...)` (`klustersdoc.h`),
which registers a colour, calls `prepareUndo()`, does per-view `addNewClusterToView`,
and ends with `emit newClusterAdded(...)` (emit at `klustersdoc_edit.cpp`).
Two-new-cluster paths (`dipSplitApply`) use `commitTwoClusterCreation(...)`
(`klustersdoc.h`), which pushes **one** `prepareReclusteringUndo` entry
so one Ctrl+Z reverts both.

### 4.4 `nbUndo`

The stack cap is a single global `int nbUndo;` (defined `main.cpp`,
`extern`-declared in every doc TU), set from `configuration().getNbUndo()`
(`klusters.cpp`; re-applied on preference change at `klusters.cpp`).
Both the `Data` snapshot stacks and the three `KlustersDoc` stacks trim to it in
lockstep.

---

## 5. Undo/redo stack mechanics (`KlustersDoc`)

On top of the `Data` snapshot stacks the doc keeps **two** parallel stacks, all
capped at `nbUndo`:

- `clusterColorListUndoList` / `…RedoList` — deep copies of `ItemColors` (`klustersdoc.h`).
- `editUndoList` / `editRedoList` of `ClusterEditUndo` (`klustersdoc.h`).

`struct ClusterEditUndo` (`klustersdoc.h`) records one action's *intent*:
`added`, `modified`, `deleted` id lists, a `byDeletion` flag, and for a renumber
`renumbering` plus the `renumberOldNew`/`renumberNewOld` maps. These are *recorded
provenance* the undo/redo view-notifications replay, not something a snapshot diff
could re-derive (`klustersdoc.h`).

### 5.1 Preparing an undo

`prepareClusterColorUndo()` (`klustersdoc_undo.cpp`) is itself the
"read current → write temp → commit" pattern for the colour list.
`prepareUndo(ClusterEditUndo)` (`klustersdoc_undo.cpp`) calls it then
prepends the descriptor and clears `editRedoList`. All typed overloads funnel
through it (`klustersdoc_undo.cpp`).

### 5.2 `undo()` / `redo()`

`undo()` (`klustersdoc_undo.cpp`):

1. Sets `modified`, gets `activeView`; no-op if the colour undo list is empty.
2. **Quiesce** every view's worker threads — `supersedeAllViewThreads()` — before
   the data swap, so a late worker result can't land against the new epoch
   (`klustersdoc_undo.cpp`; see [CONCURRENCY.md](CONCURRENCY.md)).
3. `clusteringData->undo()` reverts the **Data** epoch inside the same guard
   (`klustersdoc_undo.cpp`).
4. Move colour undo→redo and descriptor `editUndoList`→`editRedoList`.
5. **Dispatch on the descriptor**: renumber → `view->undoRenumbering` +
   `emit undoRenumbering`; else by (added, modified) shape → `view->undo*` +
   `emit undoAdditionModification / undoAddition / undoModification`.
6. Repaint, refresh palette/selection, emit undo/redo counts.
7. `curationLogger->notifyUndo()` flips the reverted action's log status.

`redo()` (`klustersdoc_undo.cpp`) mirrors it. The atom/child layer has its
**own** `childUndoStack`/`childRedoStack`; `undo()`/`redo()` drive **only**
`clusteringData`, and scope is switched to undo the other layer.

---

## 6. Document → view signals

Two delivery mechanisms coexist:

- **Direct per-view method calls.** The primary scatter/cluster-list views
  (`KlustersView`/`ClusterView`) are updated by the curation code iterating
  `for v : *viewList` and calling `addNewClusterToView` / `renumberClusters` /
  `undo*` directly.
- **Qt signals.** The similarity-matrix docks (`ErrorMatrixView`,
  `TemplateMatrixView`, `ResidualMatrixView`, `DriftMatrixView`) and `KlustersApp`
  connect to the document's signals (`klustersview.cpp`,
  `klusters.cpp`).

Key signals (`Q_SIGNALS` block `klustersdoc.h`):

| Signal (decl) | Emitted for | Emit site |
|---|---|---|
| `clustersGrouped(QList<int>&, int)` | merge/group | `klustersdoc_edit.cpp` |
| `clustersDeleted(QList<int>&, int)` | delete clusters | `klustersdoc_edit.cpp` |
| `newClusterAdded(QList<int>& from, int clusterId, QList<int>& emptied)` | **one** new cluster | `klustersdoc_edit.cpp` |
| `newClustersAdded(QMap<int,int>& fromToNew, QList<int>& emptied)` | create-many, from→new map | `klustersdoc_edit.cpp` |
| `newClustersAdded(QList<int>& clusters)` | recluster/watershed/dipsplit | `_edit.cpp`, `_dipsplit.cpp`, `_recluster.cpp`, `_watershed.cpp` |
| `renumber(QMap<int,int>&)` | rename/renumber | `klustersdoc_renumber.cpp` |
| `clusterFeaturesReprojected(int)` | nudge/realign reprojected features | `klustersdoc_realign.cpp` |
| `hierarchyChanged()` | parent↔child map edit | `klustersdoc_hierarchy.cpp,…` |

### 6.1 `newClusterAdded(int)` vs `newClustersAdded(QList<int>&)` (§2.6)

§2.6/§6.3 abbreviate the two. The real signatures:

- **Single-cluster** — `newClusterAdded(QList<int>& fromClusters, int clusterId,
  QList<int>& emptiedClusters)` (`klustersdoc.h`). Fired once by
  `commitClusterCreation` (`klustersdoc_edit.cpp`).
- **Multi-cluster, recluster-shaped** — `newClustersAdded(QList<int>& clusters)`
  (`klustersdoc.h`). Fired **once** per commit by every operation producing
  >1 cluster.

There is **also** a third overload `newClustersAdded(QMap<int,int>&, QList<int>&)`
(`klustersdoc.h`) for create-many paths with a from→new map. Because two
`newClustersAdded` overloads exist, every `connect` disambiguates with an explicit
member-function-pointer cast (`klustersview.cpp`).

§2.6's rule holds: for a >1-cluster commit, emit the **`QList` recluster signal
once**; do **not** loop `newClusterAdded(int)` per new cluster.

---

## 7. File load and save

### 7.1 Formats

Binary, little-endian (per §3.3): `.res.N` (int64 timestamps), `.spk.N` (int16
waveforms), `.clu.N` (int32 count + int32 ids), `.fet.N` features. Session
parameters are YAML (`<base>.yaml`), with legacy `<base>.par.N` + `<base>.par` as
fallback. Decoding details live in [FILE_FORMATS.md](../../libneurosuite-core/docs/FILE_FORMATS.md).

### 7.2 Open

`int openDocument(const QString& url, QString& errorInformation, const char* format)`
(`klustersdoc_io.cpp`):

1. `clusteringData = new Data();` + fresh `clusterColorList`; `activeData = clusteringData`.
2. Resolve siblings via the shared chain-of-custody layer
   `neurosuite::custody::parseAnchor` + `resolveFeature(...)` — `.spk`, `.clu`
   (or `.clc`), `.fet`, `.yaml`, pinned to the anchor's method tag
   (`klustersdoc_io.cpp`). Missing `.spk`/`.fet` → `SPK_DOWNLOAD_ERROR`/
   `FET_DOWNLOAD_ERROR`.
3. `.fet` loaded eagerly; `.spk` only measured for length, read on the fly.
4. A commit journal `.<clu>.commit` left by an interrupted save is rolled
   forward first (`filecommit::recoverCommit`).  Crash-recovery: an
   `.autosave` newer than the `.clu` prompts to use it, together with the
   `.clc` rescue copy in a hierarchical session
   (`klustersdoc_io.cpp`).
5. **Parsing delegated to `Data`**: `clusteringData->initialize(...)`
   (`klustersdoc_io.cpp`).

Return code is `OpenSaveCreateReturnMessage` (`klustersdoc.h`).

### 7.3 Save

A save has two halves.  `bool prepareSave(url)` runs on the GUI thread
(`KlustersApp::startSave`, `canCloseView`): it takes the fiber id of every spike
and, for a hierarchical session, the atom id of every spike and the `.clp` map
those two imply (`hierarchyfiles::scanNesting`), after validating the in-memory
hierarchy maps.  `int saveDocument(url, format)` then runs on the `SaveThread`
and writes only that snapshot; the GUI swallows user input until it is done
(`SaveInputBlocker`, `KlustersApp::beginSaveLock`):

1. optional re-extract of the `.spk` from the `.fil`;
2. Save As: the new base name, group, method, parameter file and waveform
   triple paths are worked out (nothing moves yet), and the parameter file is
   checked writable;
3. **phase 1, staging**: each file whose content would change is written in
   full to `<target>.saving` and flushed — the `.clu`, `.clc`, `.clp`
   (`stageIdTarget`, skipped when the ids on disk are the same), the
   `.spk`/`.res`/`.fet` working copies and synced feature spaces
   (`stagePendingFiles`, skipped when `filecommit::atParity` proves the working
   copy equals its file — from stamps alone in the common case), and the
   clustering's `.cli` file with the cluster information taken in the
   snapshot (skipped when unchanged; the session `.yaml` is never written);
4. **phase 2, install**: `filecommit::commitStaged` writes the journal
   `.<clu>.commit`, moves each target aside to `.rollback`, renames the staged
   file in, and on any failure puts the moved ones back — all or nothing; the
   `.clc`/`.clp` keep their previous version as `.bak`;
5. the session follows (Save As), the pending `.clu` is rewritten with what was
   saved, and `renewPendingAfterCommit` drops the realign queue, rebinds the
   readers only when waveforms were committed, and stamps the working copies
   as in sync;
6. `modified` is cleared only if no edit set it after the snapshot
   (`DirtyFlag::clearIfUnchanged`).

A failure anywhere leaves every file as it was.  `finishSave(ok)` (GUI thread) drops the snapshot; a failed save leaves
the document modified.  `lastSaveError()` names the failing step;
`lastSaveWarning()` carries notes for a save that succeeded (a `.clp` kept
because the files disagree about the nesting).

### 7.4 Pending files and the spike-file invariant (§3.1)

The session runs on four persistent `.pending` scratch copies seeded on open
(`pendingSpkPath/ResPath/FetPath/CluPath`, `klustersdoc.h`). Realign/
nudge write into the pending files (`klustersdoc_realign.cpp`), deferred
until `saveDocument()`. These preserve §3.1 — `.spk[i]` peak sample ≡ `.fil` at
`.res[i]`; after a nudge the authoritative timestamp is read from `.res.pending`,
not the original `.res` (`klustersdoc_realign.cpp`).

---

## 8. Cross-check against `STANDARDIZATION.md`

<details>
<summary>Audit trail</summary>

| Claim | Verdict | Evidence |
|---|---|---|
| §2.3 iterator returns `QPoint` with ordinate negated | **Holds** — its `data.h:405-409` line citation is stale | the iterator's `QPoint operator()` in `data.h` |
| §2.3 `QMap` ascending iteration relied on | Holds | `clusterIds()` = `clusterInfoMap->keys()` (`data.h`) |
| §2.3 `nextFreeClusterId() = highestClusterId()+1` | Holds verbatim | `data.h` |
| §2.5 `applyClusterRename(partial, fullOptional)` is the rename primitive | Holds | `klustersdoc.h`, `klustersdoc_renumber.cpp` |
| §2.5 never pre-mutate before `prepareUndo` | Holds — and it is a **`Data`** rule | `Data::prepareUndo` snapshots live members at `data.cpp` |
| §2.6 emit `newClustersAdded(QList)` once for >1 new cluster | Holds | one emit per recluster/watershed/dipsplit |
| §3.1 nudge/realign preserve `.spk[i]`↔`.res[i]`; `.res.pending` authoritative | Holds | `klustersdoc_realign.cpp` |

### Cautions for future editors

1. **`klustersdoc_sort.cpp` does not exist.** Sorting is `klusters_sort.cpp`.
   The renumber primitive is in `klustersdoc_renumber.cpp`; the undo stack in
   `klustersdoc_undo.cpp`.
2. **§2.3's `data.h:405-409` citation is stale** (the negation is in the iterator's `QPoint operator()`); the convention holds.
3. **`clusterInfoMap` is a `Data` member**, not a `KlustersDoc` field.
4. `newClustersAdded` has **two** overloads; always disambiguate the
   member-function pointer in `connect`.

</details>
