# Drop-in patch notes — neurosuite-3 probe library, Probe tab, and pipeline plugins

Extract from the neurosuite-3 root:

```bash
cd /path/to/neurosuite-3
tar -xzf neurosuite-3-probe-drift-dropin.tar.gz
cmake --build build
```

---

### Bug fixes (this session)

**klusters: undo/redo state corruption (segfault in `~ItemColors`)**

Three bugs working together could corrupt the undo/redo bookkeeping and
eventually double-free an `ItemColors` object, manifesting as a segfault
inside `qDeleteAll(itemList)` immediately after `undo()` returned.

1. **`undoRedoInProcess` not reset in normal undo/redo path** (`data.cpp`):
   `Data::undo()` and `Data::redo()` set `undoRedoInProcess = true` at entry
   but only reset it to `false` inside the `if(dimChanged)` branch.
   In the common case (no dimension change, e.g. every `groupClusters` undo),
   the flag stayed `true` for the remainder of the session, permanently blocking
   `minMaxDimensionCalculation` from re-running after any undo. Fixed by resetting
   `undoRedoInProcess = false` in the `else` branch of both `Data::undo()` and
   `Data::redo()`.

2. **`KlustersView::nbUndoChangedCleaning` cleared the wrong list** (`klustersview.cpp`):
   When preferences reduced `nbUndo`, the code trimmed excess entries from
   `removedClustersUndoList` in the while-loop, then the comment "Clear the
   redoLists" was followed by `qDeleteAll(removedClustersUndoList)` — clearing
   the **undo** list (including entries that were just kept) instead of the **redo**
   list. This left `removedClustersRedoList` un-cleared and double-freed the
   surviving undo entries. Fixed by correcting the list name to
   `removedClustersRedoList`.

**Changed files**: `src/klusters/src/data.cpp`,
`src/klusters/src/klustersview.cpp`


## Contents

This drop-in delivers four related feature areas and three bug fixes:

| Area | Summary |
|---|---|
| **Probe library** | 50 NeuroNexus `.probe` files installed to the system data directory |
| **Probe tab** | New ndmanager GUI tab for editing the `probes:` session YAML section |
| **New plugins** | `ndm_setupgroups`, `ndm_estimatedrift`, `ndm_decomposecollisions` |
| **Bug fixes** | yaml-cpp 0.8 const crash, null-value emission, klusters `.clu` seeding |

---

## Bug fixes

### `parameteryamlreader.cpp` — yaml-cpp 0.8 const `operator[]` crash

**Symptom:** `ndmanager session.yaml` crashes with
`YAML::InvalidNode: invalid node; first invalid key: "channels"` (or another intermediate key name).

**Root cause:** yaml-cpp 0.8 changed the const overload of `operator[]` to create an *invalid*
node when a key is absent. Because `m_root` is a non-const member, all const methods in
`ParameterYamlReader` implicitly access it via the const overload. Chained subscripts such as
`m_root["neuroscope"]["channels"]["colors"]` therefore throw `YAML::InvalidNode` when any
intermediate key is missing — before the `if (!colors)` guard can execute.

**Fix:** Two static helpers `safeGet2` / `safeGet3` descend the key chain one level at a time
through non-const temporaries, returning an empty `YAML::Node{}` if any key is absent. All 13
affected chained subscripts are patched across: `spikeGroup`, `getLfpSamplingRate`,
`getAnatomicalDescription` (×2), `getSpikeDescription` (×2),
`getVersion`/`getDate`/`getExperimenters`/`getDescription`/`getNotes`,
`getResolution`/`getNbChannels`/`getSamplingRate`/`getVoltageRange`/`getAmplification`/`getOffset`,
`getScreenGain`, `getNbSamplesSpikes`, `getPeakSampleIndexSpikes`, `getTraceBackgroundImage`,
`getChannelDisplayInfo`, `getChannelColors`, `getChannelDefaultOffset`, `getNeuroscopeVideoInfo`.

### `parameteryamlwriter.cpp` — null-value emission

**Symptom:** Optional string fields saved as bare `key:` lines (no value token) caused
`YAML::ParserException: illegal map value` on re-read with some yaml-cpp builds.

**Fix:** Empty optional strings are emitted as the explicit YAML null scalar `~`
(`std::string("~")`) instead of `YAML::NodeType::Null`, which is unambiguous across all
yaml-cpp versions.

### `klusters/src/klustersdoc.cpp` — empty `.clu` placeholder seeding

**Symptom:** Opening a session on a raw `.fet.N` file (before any sorting) could fail because
`initPendingFiles` assumed the `.clu.N` source file already existed.

**Fix:** A `seedCluFile` lambda now creates an empty placeholder `.clu` pending file when the
source `.clu.N` does not yet exist, and copies it normally when it does.

### `klusters/src/klustersdoc.cpp` — segfault during undo (three interacting bugs)

**Symptom:** Klusters crashes with a segfault inside `~ItemColors()::qDeleteAll(itemList)`
immediately after the last debug print of `KlustersDoc::undo()`.  The sequence printed is:

```
nbUndo in KlustersDoc::undo:  1
addedClusters->size() > 0 && modifiedClusters->size() == 0
in KlustersDoc::undo 2
~ItemColors()
[N] segmentation fault
```

**Root causes (three separate bugs):**

1. **`clusteringData->undo()` called outside the guard.**  
   The call `clusteringData->undo(*addedClusters, *modifiedClusters)` appeared
   *before* the `if(clusterColorListUndoList.count() > 0)` guard.  When the
   guard body then calls `addedClustersUndoList.takeAt(0)` on an unexpectedly
   empty list (possible after a partial error rollback or list skew), it returns
   `nullptr`.  `addedClusters` is set to `nullptr`, and the *next* call into any
   code that dereferences it — including the subsequent `clusteringData->undo()`
   invocation — crashes through the waveform / ItemColors cleanup path.

   **Fix:** `clusteringData->undo()` is now called *inside* the guard, and all
   three `takeAt(0)` swaps (`addedClusters`, `modifiedClusters`, `deletedClusters`)
   now guard against an unexpectedly empty undo list by allocating a fresh empty
   `QList<int>` instead of returning `nullptr`.

2. **`closeDocument()` double-`qDeleteAll` of `modifiedClustersUndoList`.**  
   A copy-paste error caused `qDeleteAll(modifiedClustersUndoList)` to appear
   twice in a row.  After the first call the list is `clear()`-ed, so the second
   `qDeleteAll` operates on an empty list and is harmless — but `modifiedClustersRedoList`
   was never freed, leaking every redo entry on document close.  
   **Fix:** Second call changed to `qDeleteAll(modifiedClustersRedoList)`.

3. **`deletedClusters` not initialised in `openDocument()`.**  
   Unlike `addedClusters` and `modifiedClusters` (both seeded to `new QList<int>()`),
   `deletedClusters` started life as `0L` from the constructor and was never
   re-seeded.  The first `prepareUndo()` call therefore pushed `nullptr` into
   `deletedClustersUndoList`; after the first undo `deletedClusters` would revert
   to `nullptr`, causing a latent dereference risk.  
   **Fix:** `deletedClusters = new QList<int>()` added to `openDocument()` beside
   the other two initialisations.

### `ndmanager/src/channelcolorspage.cpp` — segfault on save with no colour data**Symptom:** `ndmanager` crashes with a segfault when saving any session YAML that was opened
without a `neuroscope.channels.colors` section — which includes every file produced by
`ndm_xml2yaml`, every template-derived session, and every freshly created session.

**Root cause:** `ChannelColorsPage::setNbChannels` resizes the table but leaves all cells as
null `QTableWidgetItem*`. `setColors` only populates cells for channels present in the list,
so when the list is empty (no colour data in the YAML) all cells remain null.
`getColors` then calls `->item(i,col)->text()` without a null check → segfault.

`setNbChannels` also had a separate pre-existing bug: `for(i=0; i<rowCount(); ++i)
removeRow(i)` only removes half the rows because `rowCount()` shrinks as rows are deleted.

**Fix:**
- `setNbChannels` now uses `setRowCount(0)` to clear the table (fixes the half-removal bug),
  then pre-populates every cell with the default colour `#0080ff` after `setRowCount(nbChannels)`
  so that `getColors` always encounters valid items regardless of what `setColors` does.
- `getColors` now null-checks each `item()` pointer and substitutes the default colour if null,
  providing a defensive second layer of protection.

---

## Probe library — `src/nphys-data/src/probes/neuronexus/` (50 files)

50 NeuroNexus probe configuration files covering the current catalog (penetrating silicon probes,
tetrodes, and Buzsaki series).

Each `.probe` file is a YAML document:

```yaml
probeFile:
  version: '1.0'
  vendor: NeuroNexus
  model: A1x16-5mm-25-177
  totalChannels: 16
  shanks:
    count: 1
    spacing_um: null
    length_mm: 5.0
  sites:
    count_per_shank: 16
    layout: linear
    area_um2: 177
    spacing_um: 25
  channelMap:
    description: null
    map: null           # null → sequential channel assignment
```

Files are installed to `${CMAKE_INSTALL_DATADIR}/neurosuite/probes/neuronexus/`
(typically `/usr/local/share/neurosuite/probes/neuronexus/`).

The runtime search order (lowest → highest priority):

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH  (colon-separated)
SESSION_DIR/probes/     (session-local override)
```

**Changed file:** `src/nphys-data/src/CMakeLists.txt` — added `file(GLOB _probes …) + install(FILES …)` block.

---

## Probe YAML extensions — `src/libklustersshared/`

New free functions for reading and writing the `probes:` top-level section and
`probeId`/`shankIndex` metadata on anatomical group entries.

### `parameteryamlreader_probes.h` / `.cpp`

New data types:

| Type | Fields |
|---|---|
| `ProbeEntry` | `id`, `probeFile`, `label`, `channelOffset`, `anatomicalGroups` |
| `ProbeGroupMeta` | `groupId`, `probeId`, `shankIndex` |

New free functions:

| Function | Purpose |
|---|---|
| `readProbesSection(root, out, libraryPathOut)` | Parse `probes:` sequence from the YAML root |
| `readAnatomyGroupMeta(root, out)` | Parse `probeId`/`shankIndex` from `anatomicalDescription.channelGroups` |
| `writeProbesSection(root, probes, libraryPath)` | Emit `probes:` sequence into a YAML document node |
| `writeAnatomyGroupMeta(root, meta)` | Write `probeId`/`shankIndex` back into existing `channelGroups` entries |

**Changed file:** `src/libklustersshared/src/CMakeLists.txt` — added both files to the build.

---

## ndmanager Probe tab — `src/ndmanager/src/`

A new **Probes** tab appears in the ndmanager parameter tree between Electrode Groups and Programs.
It reads and writes the `probes:` section of the session YAML.

### New files

| File | Purpose |
|---|---|
| `probelayout.ui` | Qt Designer UI — probe list table + library path field |
| `probelayout.h` / `.cpp` | AUTOUIC wrapper (`ProbeLayout : QWidget`) |
| `probepage.h` / `.cpp` | `ProbePage` tab — `setProbes()` / `getProbes()` / `setLibraryPath()` / `getLibraryPath()` |
| `icons/probe.png` | 32×32 RGBA tab icon |

### Changed files

| File | Change |
|---|---|
| `src/ndmanager/src/CMakeLists.txt` | Added `probepage.cpp` and `probelayout.cpp` |
| `src/ndmanager/src/ndmanager-icons.qrc` | Added `icons/probe.png` |

### Integration note — `parameterview.cpp`

Two small additions are required in `src/ndmanager/src/parameterview.cpp` (intentionally left as
a manual step):

**`parameterview.h`** — add alongside `AnatomyPage`:
```cpp
#include "probepage.h"
ProbePage* probe;
```

**Constructor** — after the spike block:
```cpp
if (expertMode) {
    probe = new ProbePage;
    mStackWidget->addWidget(probe);
    mParameterTree->addPage(":/icons/probe", tr("Probes"), probe);
} else {
    probe = new ProbePage();
}
connect(this, SIGNAL(resetModificationStatus()), probe, SLOT(resetModificationStatus()));
```

**`initialize()`** — after `anatomy->setGroups(…)`:
```cpp
probe->setProbes(probeList);
probe->setLibraryPath(probeLibraryPath);
```

**`getInformation()`** — after `anatomy->getGroups(…)`:
```cpp
probe->getProbes(probeList);
probeLibraryPath = probe->getLibraryPath();
```

---

## New plugin: `ndm_setupgroups`

Reads the `probes:` section of the session YAML, loads each `.probe` file, and writes
`anatomicalDescription.channelGroups` and `spikeDetection.channelGroups` in-place.

Position in pipeline: after `ndm_reorderchannels`, before `ndm_spikegrouper`/`ndm_extractspikes`.

**Channel assignment** — when `channelMap.map` is null (all supplied NeuroNexus files):

```
shank i → [offset + i × n_per_shank, …, offset + (i+1) × n_per_shank − 1]
```

**Mirroring** — spikeDetection groups mirror anatomical groups exactly (same channel list, same
order), annotated with `nSamples`, `peakSampleIndex`, `nFeatures`.

**Metadata** — `probeId` and `shankIndex` written into each anatomical group for
`ndm_estimatedrift`. `probes[].anatomicalGroups` updated with assigned group IDs.

**Guard** — refuses to overwrite existing groups unless `overwrite: true`.

### New files

| File | Purpose |
|---|---|
| `scripts/ndm_setupgroups` | Bash driver |
| `scripts/process_setupgroups.py` | Python implementation |
| `descriptions/ndm_setupgroups.xml` | Plugin description |
| `scripts/ndm_setupgroups.docbook` | DocBook man page |

### Parameters

| Parameter | Default | Description |
|---|---|---|
| `nSamples` | 52 | Waveform window width in samples |
| `peakSampleIndex` | 26 | 0-based peak index within window |
| `nFeatures` | 3 | PCA components per channel |
| `probeLibrary` | — | Override probe library search path |
| `overwrite` | false | Replace existing groups if true |

---

## New plugin: `ndm_estimatedrift`

Estimates in-vivo probe drift from curated spike-sorting results. Must be run after
`ndm_klustakwik` and Klusters curation. Requires `.res.N`, `.clu.N`, and optionally `.spk.N`.

**Primary method — per-unit spatial-profile cross-correlation:** for each unit, cross-correlates
its amplitude-vs-depth fingerprint between reference and subsequent windows. Sub-site precision
via parabolic interpolation. Weighted-median drift estimate per shank.

**Fallback method — population amplitude-profile cross-correlation:** pooled multi-unit spatial
fingerprint; used when fewer than `minUnits` units are tracked.

Output: `SESSION.drift` (YAML).

### New files

| File | Purpose |
|---|---|
| `scripts/ndm_estimatedrift` | Bash driver |
| `scripts/process_estimatedrift.py` | Python implementation |
| `descriptions/ndm_estimatedrift.xml` | Plugin description |
| `scripts/ndm_estimatedrift.docbook` | DocBook man page |

### Parameters

| Parameter | Default | Description |
|---|---|---|
| `windowSec` | 60 | Time window length in seconds |
| `minUnits` | 3 | Minimum tracked units per shank |
| `minSpikes` | 20 | Minimum spikes per unit per window |
| `excludeNoise` | true | Exclude clusters 0 and 1 |
| `probeLibrary` | — | Override probe library search path |

---

## New plugin: `ndm_decomposecollisions`

Template-matching collision decomposition for overlapping spike waveforms. Must be run after
curation. Never modifies original `.clu.N`/`.res.N`/`.spk.N` files.

**Algorithm:** (1) Build mean templates from curated clusters. (2) Flag candidates with
normalized cross-correlation < `corrThreshold`. (3) Fit all same-shank pairwise template
combinations with shifts up to `maxShiftSamp`. (4) Accept when residual RMS fraction <
`residualThreshold`. Output: `SESSION.col.N` YAML sidecars.

### New files

| File | Purpose |
|---|---|
| `scripts/ndm_decomposecollisions` | Bash driver |
| `scripts/process_decomposecollisions.py` | Python implementation |
| `descriptions/ndm_decomposecollisions.xml` | Plugin description |
| `scripts/ndm_decomposecollisions.docbook` | DocBook man page |

### Parameters

| Parameter | Default | Description |
|---|---|---|
| `maxShiftSamp` | 10 | Maximum alignment shift in samples |
| `corrThreshold` | 0.85 | Correlation threshold for collision candidates |
| `residualThreshold` | 0.25 | Residual RMS fraction for decomposition acceptance |
| `minSnrRms` | 4.0 | Minimum SNR (RMS) for unit templates |
| `minSpikesTemplate` | 30 | Minimum spikes to build a template |
| `excludeNoise` | true | Exclude clusters 0 and 1 |

---

## Changed files summary

| File | Type | Change |
|---|---|---|
| `src/libklustersshared/src/klustersshared/parameteryamlreader.cpp` | Bug fix | yaml-cpp 0.8 const crash — 13 chained subscripts → `safeGet2`/`safeGet3` |
| `src/libklustersshared/src/klustersshared/parameteryamlwriter.cpp` | Bug fix | Null strings → `~` |
| `src/libklustersshared/src/CMakeLists.txt` | Build | Added probe YAML extension sources |
| `src/klusters/src/klustersdoc.cpp` | Bug fix | Three undo bugs: misplaced clusteringData->undo(), double-qDeleteAll, uninit deletedClusters |
| `src/ndmanager/src/channelcolorspage.cpp` | Bug fix | Segfault on save — null table cells + half-removal loop |
| `src/ndmanager/src/CMakeLists.txt` | Build | Added Probe tab sources |
| `src/ndmanager/src/ndmanager-icons.qrc` | Build | Added `probe.png` |
| `src/ndmanager-plugins/scripts/CMakeLists.txt` | Build | Added 3 plugins + Python helpers + man pages |
| `src/nphys-data/src/CMakeLists.txt` | Build | Added `.probe` library install |
| `templates/template.yaml` | Data | Added `ndm_setupgroups`; 28 plugins total |

## New files summary

| File | Area |
|---|---|
| `src/libklustersshared/src/klustersshared/parameteryamlreader_probes.h/cpp` | Probe YAML extensions |
| `src/ndmanager/src/probelayout.ui/h/cpp` | Probe tab UI |
| `src/ndmanager/src/probepage.h/cpp` | Probe tab logic |
| `src/ndmanager/src/icons/probe.png` | Probe tab icon |
| `src/ndmanager-plugins/scripts/ndm_setupgroups` | Plugin bash driver |
| `src/ndmanager-plugins/scripts/process_setupgroups.py` | Plugin Python |
| `src/ndmanager-plugins/descriptions/ndm_setupgroups.xml` | Plugin description |
| `src/ndmanager-plugins/scripts/ndm_setupgroups.docbook` | Plugin man page |
| `src/ndmanager-plugins/scripts/ndm_estimatedrift` | Plugin bash driver |
| `src/ndmanager-plugins/scripts/process_estimatedrift.py` | Plugin Python |
| `src/ndmanager-plugins/descriptions/ndm_estimatedrift.xml` | Plugin description |
| `src/ndmanager-plugins/scripts/ndm_estimatedrift.docbook` | Plugin man page |
| `src/ndmanager-plugins/scripts/ndm_decomposecollisions` | Plugin bash driver |
| `src/ndmanager-plugins/scripts/process_decomposecollisions.py` | Plugin Python |
| `src/ndmanager-plugins/descriptions/ndm_decomposecollisions.xml` | Plugin description |
| `src/ndmanager-plugins/scripts/ndm_decomposecollisions.docbook` | Plugin man page |
| `src/nphys-data/src/probes/neuronexus/*.probe` (50 files) | Probe library |
| `templates/template.yaml` | Session template (28 plugins) |
