# The Klusters document model (`KlustersDoc`)

A developer reference for the cluster/spike document layer of Klusters. Every
structural claim below cites `file:line` in `src/klusters/src/`. Anything not
directly read out of the code is marked **(unverified)**.

> **Source note.** This reference was written against `klustersdoc.h`, the
> `klustersdoc_*.cpp` family (`klustersdoc.cpp`, `_edit`, `_io`, `_undo`,
> `_renumber`, `_hierarchy`, `_recluster`, `_watershed`, `_dipsplit`,
> `_realign`, `_strip`), `data.h`/`data.cpp`, and
> `../../libklustersshared/src/klustersshared/types.h`.
> There is **no** `klustersdoc_sort.cpp`; the sort UI lives in `klusters_sort.cpp`
> (the `klusters_*` app family, not the document). The rename/renumber primitive
> lives in `klustersdoc_renumber.cpp` and the undo stack in `klustersdoc_undo.cpp`.
> Cross-references to `STANDARDIZATION.md` use its section numbers.

---

## 1. Roles and ownership

`KlustersDoc : public QObject` (`klustersdoc.h:72-74`) is the document layer in a
classic document/view split: it owns the clustering model and the file-backed
session, drives every curation operation, and notifies the views. It is always
constructed with a `KlustersApp` parent (`klustersdoc.h:107` casts `parent`
straight to `KlustersApp*`).

`KlustersDoc` **owns**, as direct members:

| What | Member | Decl |
|---|---|---|
| The spike/feature/cluster data store | `Data* clusteringData` | `klustersdoc.h:2138` |
| Optional child (`.clc`) clustering + its colours | `Data* childData`, `ItemColors* childColorList` | `klustersdoc.h:2141-2142` |
| View-facing active store / colours | `Data* activeData`, `ItemColors* activeColorList` | `klustersdoc.h:2143-2144` |
| Cluster colours **and** per-cluster status | `ItemColors* clusterColorList` | `klustersdoc.h:2077` |
| Colour undo/redo snapshots | `QList<ItemColors*> clusterColorListUndoList / …RedoList` | `klustersdoc.h:2082,2087` |
| Per-action edit descriptors (undo/redo) | `QList<ClusterEditUndo> editUndoList / editRedoList` | `klustersdoc.h:2293,2297` |
| Connected views | `QList<KlustersView*>* viewList` | `klustersdoc.h:2135` |
| Cluster palette (back-reference) | `ClusterPalette& clusterPalette` | `klustersdoc.h:2284` |
| Curation log | `std::unique_ptr<CurationLogger> curationLogger` | `klustersdoc.h:2349` |
| Pending-file paths + deferred realign writes | `pendingSpkPath…`, `std::vector<PendingRealign> pendingRealign` | `klustersdoc.h:1953-1956,2346` |
| Hierarchy maps + atom-layer undo stacks | `parentToChildren`, `childToParent`, `childUndoStack`, `childRedoStack` | `klustersdoc.h:2178-2179,2161-2162` |

**The cluster↔spike mapping itself is not a `KlustersDoc` member** — it lives
inside `Data` (§2). `KlustersDoc` holds the *presentation* state (colours,
status, selection, masking, undo bookkeeping, hierarchy) and delegates the
membership model to `Data`.

Accessors the rest of the app uses:

- `Data& data() const` → `activeData` if set, else `clusteringData` (`klustersdoc.h:226`).
- `Data& parentData() const` → always `clusteringData` (`klustersdoc.h:228`).
- `Data& childClusterData() const` → `childData` if loaded (`klustersdoc.h:236`).
- `ItemColors& clusterColors() const` → `activeColorList ? … : clusterColorList` (`klustersdoc.h:203`).
- `void addView/removeView(KlustersView*)` (`klustersdoc.h:96-98`); `viewList` is the notify set.

`setActiveClustering(bool child)` (`klustersdoc.h:571`) swaps `activeData` /
`activeColorList` between the parent and child layers; the two layers keep
**independent** undo timelines (`klustersdoc.h:2155-2166`).

### `types.h`

`types.h` defines the two numeric aliases used throughout the model
(`libklustersshared/src/klustersshared/types.h:20-21`):

```cpp
typedef long      dataType;          // cluster ids, feature values, spike counts/indices
typedef long long recordingUnitType; // sample timestamps
```

All cluster ids, feature-row indices and feature values flow as `dataType`.

---

## 2. The `Data` store — spikes, features, waveforms

`class Data` (`data.h:151`) is the single, non-copyable document-model instance
(`data.h:165-173` deletes copy/move). It holds three parallel structures plus a
feature matrix.

### 2.1 Storage

| Store | Declaration | Shape |
|---|---|---|
| Feature matrix | `Array<dataType> features;` (`data.h:1410`) | row-major, **1-based** rows; one row per spike, one column per feature dimension |
| Row table | `std::shared_ptr<SortableTable> spikesByCluster;` (`data.h:1421`) | 2 rows × `nbSpikes` columns: row 1 = the spike's feature-row index, row 2 = its cluster id; **sorted by (cluster id, then time)** (`data.h:1411-1421`) |
| Per-cluster index | `std::shared_ptr<ClusterInfoMap> clusterInfoMap;` where `typedef QMap<dataType,ClusterInfo> ClusterInfoMap;` (`data.h:1487,1477`) | key = cluster id → `ClusterInfo` |
| Spike count | `long nbSpikes;` (`data.h:1369`) | total spikes in the group |

`ClusterInfo` (`data.h:1434-1475`) stores the cluster's **contiguous span**
inside `spikesByCluster` plus optional user metadata:

- `firstSpikePosition()` / `position` — 1-based start column in `spikesByCluster` (`data.h:1442,1466`).
- `nbSpikes()` / `spikeNb` — number of columns for this cluster (`data.h:1443,1467`).
- user fields: `structure`, `type`, `ID` (isolation distance), `quality`, `notes` (`data.h:1469-1474`).
- `position`/`spikeNb` are **zero-initialised** so a `QMap::value()` miss yields a
  benign empty span rather than an out-of-bounds read (`data.h:1460-1467`).

So a cluster's spikes are the columns `firstSpikePosition() …
firstSpikePosition()+nbSpikes()-1` of `spikesByCluster`; row 1 of each column
indexes into `features`.

> **`clusterInfoMap` belongs to `Data`, not `KlustersDoc`.** `STANDARDIZATION.md`
> §2.5/§6.2 speak of "`clusterInfoMap`" as if it were a doc field; in the code
> it is `Data`'s member (`data.h:1487`) and the §2.5 "never pre-mutate" rule is a
> rule about the **`Data` mutators** (§4.1). The doc never touches it directly.

### 2.2 Access — the `Iterator` and `features()`

Spikes of one cluster are walked via `Data::Iterator` (`data.h:691-745`),
created by `Data::iterator(clusterId)` (`data.h:682-684`). Its constructor reads
the cluster's span from `clusterInfoMap->value(clusterId)` (`data.h:734-736`,
using `value()` not `operator[]` to avoid inserting a phantom entry).

Two call operators:

- `QPoint operator()(dataType dimX, dataType dimY) const` (`data.h:707-711`) —
  returns a scatter-space point **with the ordinate already negated**:
  ```cpp
  return QPoint(data.features(featuresRowIndex, dimX),
              - data.features(featuresRowIndex, dimY));   // data.h:709-710
  ```
- `dataType operator()(dataType dim) const` (`data.h:721-723`) — raw feature value, no flip.
- `featureRow()` (`data.h:715`) — the current spike's 1-based feature row (maps a
  drawn point back to its `.spk`/`.eap` row).

> **§2.3 ordinate-negation, corrected line reference.** The convention is real and
> load-bearing: *never* re-flip Y downstream of the iterator. But the negation is
> at **`data.h:707-711`**, not `data.h:405-409` as `STANDARDIZATION.md` §2.3 states
> — line 405 is now the doc-comment of a `createNewCluster` overload
> (`data.h:398-409`). The doc-comment at `data.h:698-706` restates the convention.

Feature values are read through `data.features(row, dim)` — 1-based row,
1-based dimension.

### 2.3 Id allocation — `nextFreeClusterId`

```cpp
dataType highestClusterId() const {            // data.h:760-763
    if (nbSpikes == 0) return 0;
    return (*spikesByCluster)(2, nbSpikes);    // last column's cluster id (table is id-sorted)
}
dataType nextFreeClusterId() const {           // data.h:770-772
    return highestClusterId() + 1;
}
```

This is the canonical, gap-ignoring "new clusters go at the tail" policy
(`data.h:765-772`), matching §2.3. Because `spikesByCluster` is kept id-sorted,
the last column holds the max id; renumber operations that repack the table
exist precisely to keep this honest (`data.cpp:4745-4794`, §4.1).

Related read-only helpers: `clusterIds()` = `clusterInfoMap->keys()`
(`data.h:748-750`, ascending because `QMap` iterates ascending — §2.3);
`nbOfClusters()` (`data.h:1203`); `clusterHasMembers(id)` (`data.h:813-818`).

### 2.4 Waveforms

Waveforms are **not** held in `Data` as an array. The `.spk` file is opened but
read **on the fly** when waveforms need drawing (`klustersdoc_io.cpp:310-311`).
`Data` holds a shared `SpkReader` (`std::shared_ptr<SpkReader> spkReaderInstance`,
`data.h:1380`); `installSpkReader()` / `reopenSpkReader()` (`data.h:1016,1516`)
install or rebind it. Each published epoch snapshot carries its own reader
(`data.h:2485`) so an in-flight `WaveformThread` keeps reading the file version
pinned at enqueue time. Waveform fetching is done by `WaveformThread`, a friend of
`Data` (`data.h:156`).

---

## 3. Epochs and snapshots (the `Data`-level undo substrate)

`Data` versions its membership as immutable **epochs**. `struct
ClusteringSnapshot` (`data.h:189`) bundles the `(spikesByCluster, clusterInfoMap)`
pair plus that epoch's waveform/correlogram stores, reader and overlay
generation. `currentSnapshot()` (`data.h:194`) is the live epoch; pool jobs
capture it at enqueue time so a later edit can't free tables a running job still
reads (`data.h:175-188`, `1416-1420`, `1484-1486`). See
[CONCURRENCY.md](CONCURRENCY.md).

Undo/redo at the data layer are two snapshot stacks:

```cpp
QList<std::shared_ptr<const ClusteringSnapshot>> undoSnapshots;   // data.h:1499
QList<std::shared_ptr<const ClusteringSnapshot>> redoSnapshots;   // data.h:1500
```

`changedClustersBetween(from, to, renamedFromTo)` (`data.h:212-214`) diffs two
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
                     bool dimensionChanged);        // decl data.h:1980, def data.cpp:4213
```

The contract (from the body, `data.cpp:4213-4292`):

1. Validate the candidate table; **refuse** an internally inconsistent one and keep
   current state (`data.cpp:4249-4261`).
2. **Push the pre-edit epoch**: `undoSnapshots.prepend(currentSnapshot());`
   (`data.cpp:4267`).
3. Swap the temp tables in under the mutex (`data.cpp:4273-4277`).
4. `publishSnapshot();` — publish the new epoch (`data.cpp:4280`).
5. Trim `undoSnapshots` to `nbUndo`; clear the redo history (`data.cpp:4284-4291`).

**Why you must not pre-mutate `clusterInfoMap`/`spikesByCluster`:** step 2 snapshots
whatever the *live* members currently are. If you mutate them before calling
`prepareUndo`, the "pre-edit" snapshot captures the already-mutated state and undo
cannot return to the real prior state. The correct shape is: build fresh temp
tables from the current state, then hand them to `prepareUndo`, which
snapshots-then-swaps atomically.

`renumberPartial` (`data.cpp:4666-4802`) is the canonical example: it allocates
fresh temps (`data.cpp:4676-4678`), builds them from the current tables without
touching the live ones, and only at the end calls `prepareUndo(...)`
(`data.cpp:4801`). It also refuses a non-injective map — a merge in disguise —
before building anything (`data.cpp:4726-4737`).

### 4.2 `applyClusterRename` — the rename/renumber primitive (§2.5)

```cpp
// public slot — klustersdoc.h:1796-1797
void applyClusterRename(const QMap<int,int>& partialOldToNew,
                      const QMap<int,int>* fullOldToNewOpt = nullptr);
// definition — klustersdoc_renumber.cpp:258
```

It is the single apply-path for "rename a set of clusters". `partialOldToNew`
lists **only** the renamed clusters; `fullOldToNewOpt` is an optional covering map
(every live id → post-rename id, identity for unchanged), built automatically
when null (`klustersdoc_renumber.cpp:266-276`). Order of operations
(`klustersdoc_renumber.cpp:281-345`):

1. **Data layer** — `clusteringData->renumberPartial(partialOldToNew)` rewrites the
   spike table + `clusterInfoMap` and **pushes its own data-side undo entry**
   (`klustersdoc_renumber.cpp:282`; `data.cpp:4801`).
2. **Colour list** — each renamed id relabelled via `clusterColorList->changeItemId`,
   then `sortByItemId()` (`klustersdoc_renumber.cpp:285-305`).
3. **S-pins + matrix scope** ride the rename (`klustersdoc_renumber.cpp:312-317`).
4. **Each view** — `v->renumberClusters(full, isActive)` + `updateTraceView`
   (`klustersdoc_renumber.cpp:320-326`).
5. **Similarity matrices** — `emit renumber(full)` (`klustersdoc_renumber.cpp:329`).

**`applyClusterRename` does *not* snapshot undo itself.** The caller is responsible
for the doc-level undo (`prepareUndo` / `prepareReclusteringUndo`) and any
`logBefore`/`logAfter` pair (`klustersdoc.h:1793-1795`). Callers include
`renumberClustersToEnd` (T key) and `reorderClustersByPermutation` (Shift+S).

### 4.3 The multi-cluster-creation siblings (§2.5, §2.6)

Operations that *create* clusters use a different trio, which §2.5 calls
`applyClusterRename`'s "siblings":

- `void prepareReclusteringUndo(QList<int>& newClusters, QList<int>& deletedClusters)`
  (decl `klustersdoc.h:2072`; def `klustersdoc_undo.cpp:258-263`) — one doc-side
  undo entry recording the created + emptied clusters.
- `KlustersView::addNewClustersToView(…)` — the per-view update (two overloads,
  `klustersview.cpp:1520,1557`), called in a `for v : *viewList` loop.
- `emit newClustersAdded(QList<int>&)` — the recluster-shaped multi-cluster signal (§6).

Single-new-cluster paths use `commitClusterCreation(...)` (`klustersdoc.h:1890-1893`),
which registers a colour, calls `prepareUndo()`, does per-view `addNewClusterToView`,
and ends with `emit newClusterAdded(...)` (emit at `klustersdoc_edit.cpp:972`).
Two-new-cluster paths (`dipSplitApply`) use `commitTwoClusterCreation(...)`
(`klustersdoc.h:1905-1909`), which pushes **one** `prepareReclusteringUndo` entry
so one Ctrl+Z reverts both.

### 4.4 `nbUndo`

The stack cap is a single global `int nbUndo;` (defined `main.cpp:35`,
`extern`-declared in every doc TU), set from `configuration().getNbUndo()`
(`klusters.cpp:1882`; re-applied on preference change at `klusters.cpp:1755`).
Both the `Data` snapshot stacks and the three `KlustersDoc` stacks trim to it in
lockstep.

---

## 5. Undo/redo stack mechanics (`KlustersDoc`)

On top of the `Data` snapshot stacks the doc keeps **two** parallel stacks, all
capped at `nbUndo`:

- `clusterColorListUndoList` / `…RedoList` — deep copies of `ItemColors` (`klustersdoc.h:2082,2087`).
- `editUndoList` / `editRedoList` of `ClusterEditUndo` (`klustersdoc.h:2293,2297`).

`struct ClusterEditUndo` (`klustersdoc.h:1998-2010`) records one action's *intent*:
`added`, `modified`, `deleted` id lists, a `byDeletion` flag, and for a renumber
`renumbering` plus the `renumberOldNew`/`renumberNewOld` maps. These are *recorded
provenance* the undo/redo view-notifications replay, not something a snapshot diff
could re-derive (`klustersdoc.h:1989-1997`).

### 5.1 Preparing an undo

`prepareClusterColorUndo()` (`klustersdoc_undo.cpp:99-122`) is itself the
"read current → write temp → commit" pattern for the colour list.
`prepareUndo(ClusterEditUndo)` (`klustersdoc_undo.cpp:124-145`) calls it then
prepends the descriptor and clears `editRedoList`. All typed overloads funnel
through it (`klustersdoc_undo.cpp:209-263`).

### 5.2 `undo()` / `redo()`

`undo()` (`klustersdoc_undo.cpp:265-428`):

1. Sets `modified`, gets `activeView`; no-op if the colour undo list is empty.
2. **Quiesce** every view's worker threads — `supersedeAllViewThreads()` — before
   the data swap, so a late worker result can't land against the new epoch
   (`klustersdoc_undo.cpp:293-294`; see [CONCURRENCY.md](CONCURRENCY.md)).
3. `clusteringData->undo()` reverts the **Data** epoch inside the same guard
   (`klustersdoc_undo.cpp:299`).
4. Move colour undo→redo and descriptor `editUndoList`→`editRedoList`.
5. **Dispatch on the descriptor**: renumber → `view->undoRenumbering` +
   `emit undoRenumbering`; else by (added, modified) shape → `view->undo*` +
   `emit undoAdditionModification / undoAddition / undoModification`.
6. Repaint, refresh palette/selection, emit undo/redo counts.
7. `curationLogger->notifyUndo()` flips the reverted action's log status.

`redo()` (`klustersdoc_undo.cpp:431-571`) mirrors it. The atom/child layer has its
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
  connect to the document's signals (`klustersview.cpp:2103-2169`,
  `klusters.cpp:1399-1426`).

Key signals (`Q_SIGNALS` block `klustersdoc.h:1655-1741`):

| Signal (decl) | Emitted for | Emit site |
|---|---|---|
| `clustersGrouped(QList<int>&, int)` (`:1675`) | merge/group | `klustersdoc_edit.cpp:138` |
| `clustersDeleted(QList<int>&, int)` (`:1712`) | delete clusters | `klustersdoc_edit.cpp:525` |
| `newClusterAdded(QList<int>& from, int clusterId, QList<int>& emptied)` (`:1714`) | **one** new cluster | `klustersdoc_edit.cpp:972` |
| `newClustersAdded(QMap<int,int>& fromToNew, QList<int>& emptied)` (`:1715`) | create-many, from→new map | `klustersdoc_edit.cpp:1420` |
| `newClustersAdded(QList<int>& clusters)` (`:1740`) | recluster/watershed/dipsplit | `_edit.cpp:1074`, `_dipsplit.cpp:650`, `_recluster.cpp:325,358`, `_watershed.cpp:269` |
| `renumber(QMap<int,int>&)` (`:1716`) | rename/renumber | `klustersdoc_renumber.cpp:181,329` |
| `clusterFeaturesReprojected(int)` (`:1723`) | nudge/realign reprojected features | `klustersdoc_realign.cpp:2254,3584` |
| `hierarchyChanged()` (`:1678`) | parent↔child map edit | `klustersdoc_hierarchy.cpp:1065,…` |

### 6.1 `newClusterAdded(int)` vs `newClustersAdded(QList<int>&)` (§2.6)

§2.6/§6.3 abbreviate the two. The real signatures:

- **Single-cluster** — `newClusterAdded(QList<int>& fromClusters, int clusterId,
  QList<int>& emptiedClusters)` (`klustersdoc.h:1714`). Fired once by
  `commitClusterCreation` (`klustersdoc_edit.cpp:972`).
- **Multi-cluster, recluster-shaped** — `newClustersAdded(QList<int>& clusters)`
  (`klustersdoc.h:1740`). Fired **once** per commit by every operation producing
  >1 cluster.

There is **also** a third overload `newClustersAdded(QMap<int,int>&, QList<int>&)`
(`klustersdoc.h:1715`) for create-many paths with a from→new map. Because two
`newClustersAdded` overloads exist, every `connect` disambiguates with an explicit
member-function-pointer cast (`klustersview.cpp:2107,2120,2135`).

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
(`klustersdoc_io.cpp:209`):

1. `clusteringData = new Data();` + fresh `clusterColorList`; `activeData = clusteringData`.
2. Resolve siblings via the shared chain-of-custody layer
   `neurosuite::custody::parseAnchor` + `resolveFeature(...)` — `.spk`, `.clu`
   (or `.clc`), `.fet`, `.yaml`, pinned to the anchor's method tag
   (`klustersdoc_io.cpp:249-298`). Missing `.spk`/`.fet` → `SPK_DOWNLOAD_ERROR`/
   `FET_DOWNLOAD_ERROR`.
3. `.fet` loaded eagerly; `.spk` only measured for length, read on the fly.
4. Crash-recovery: an `.autosave` newer than the `.clu` prompts to use it
   (`klustersdoc_io.cpp:378-408`).
5. **Parsing delegated to `Data`**: `clusteringData->initialize(...)`
   (`klustersdoc_io.cpp:428`).

Return code is `OpenSaveCreateReturnMessage` (`klustersdoc.h:81-83`).

### 7.3 Save

`int saveDocument(const QString& saveUrl, const char* format)` (`klustersdoc_io.cpp:660`):
Save-vs-SaveAs decided at `:666`; optional re-extract `.spk` from `.fil`; write
clusters via `clusteringData->saveClusters(cluFile)` (`:726`); regenerate `.clc`/
`.clp` hierarchy siblings; persist per-cluster user info to YAML; then
`commitAndRenewPending()` promotes the pending files to the originals.

### 7.4 Pending files and the spike-file invariant (§3.1)

The session runs on four persistent `.pending` scratch copies seeded on open
(`pendingSpkPath/ResPath/FetPath/CluPath`, `klustersdoc.h:1952-1956`). Realign/
nudge write into the pending files (`klustersdoc_realign.cpp:1852-1869`), deferred
until `saveDocument()`. These preserve §3.1 — `.spk[i]` peak sample ≡ `.fil` at
`.res[i]`; after a nudge the authoritative timestamp is read from `.res.pending`,
not the original `.res` (`klustersdoc_realign.cpp:3219-3222`).

---

## 8. Cross-check against `STANDARDIZATION.md`

| Claim | Verdict | Evidence |
|---|---|---|
| §2.3 iterator returns `QPoint` with ordinate negated | **Holds** — cited `data.h:405-409` is stale | real location `data.h:707-711` |
| §2.3 `QMap` ascending iteration relied on | Holds | `clusterIds()` = `clusterInfoMap->keys()` (`data.h:748-750`) |
| §2.3 `nextFreeClusterId() = highestClusterId()+1` | Holds verbatim | `data.h:770-772` |
| §2.5 `applyClusterRename(partial, fullOptional)` is the rename primitive | Holds | `klustersdoc.h:1796`, `klustersdoc_renumber.cpp:258` |
| §2.5 never pre-mutate before `prepareUndo` | Holds — and it is a **`Data`** rule | `Data::prepareUndo` snapshots live members at `data.cpp:4267` |
| §2.6 emit `newClustersAdded(QList)` once for >1 new cluster | Holds | one emit per recluster/watershed/dipsplit |
| §3.1 nudge/realign preserve `.spk[i]`↔`.res[i]`; `.res.pending` authoritative | Holds | `klustersdoc_realign.cpp:2085,3219-3222,3289` |

### Cautions for future editors

1. **`klustersdoc_sort.cpp` does not exist.** Sorting is `klusters_sort.cpp`.
   The renumber primitive is in `klustersdoc_renumber.cpp`; the undo stack in
   `klustersdoc_undo.cpp`.
2. **§2.3's `data.h:405-409` citation is stale** (→ `707-711`); the convention holds.
3. **`clusterInfoMap` is a `Data` member**, not a `KlustersDoc` field.
4. `newClustersAdded` has **two** overloads; always disambiguate the
   member-function pointer in `connect`.
