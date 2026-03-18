# neurosuite-3 — Changelog

## 2026-03-18

---

### ndmanager-plugins — new plugin: `ndm_aom2dat` / `process_aomconvert`

#### AlphaOmega .mat (HDF5 v7.3) → .dat + session YAML (`process_aomconvert.cpp`)

New C++ tool that converts AlphaOmega multi-channel recordings to the neurosuite `.dat`
binary and generates a complete session YAML compatible with the full ndmanager-plugins
pipeline. Streams HDF5 data via the C API hyperslab interface — peak RAM is
`chunkSamples × nChannels × 2 bytes` regardless of recording length (default 10M samples
≈ 610 MB for 32 channels).

**AlphaOmega HDF5 v7.3 structure assumed:**
- `CRAW_NNN` datasets — int16 sample arrays, one per channel
- `CRAW_NNN_KHz`, `_BitResolution`, `_Gain`, `_TimeBegin`, `_TimeEnd` — scalar metadata
- Channels discovered by iterating root-level datasets matching `CRAW_\d+$`

**Mixed probe topology:** `-t "1-16:16,17-32:4"` creates 1 linear group (16ch) + 4 tetrode
groups (4ch each). Size-to-type inference: `SIZE==4` → tetrode, `SIZE==1` → single,
other → linear.

**YAML output follows `template.yaml` schema** so all pipeline scripts read it without
modification: `acquisitionSystem.samplingRate`, `spikeDetection.channelGroups[g].channels`,
`programs[ndm_klustakwik]`, etc.

**Per-group KlustaKwik calibration written into YAML:**
- `MergeThresh = χ²(nCh×3, 0.9999)` per group
- `MaxClusters` scaled by probe type (linear: 40, tetrode: 20, single: 5)
- `MaxPossibleClusters = ceil(duration/10min) × MaxClusters + 50`
- `GlobalMergeIter` / `TimeMergeIter` scaled by √(`nSpatialDims`/24) from 8ch baseline

**New files:**
- `src/process_aomconvert/process_aomconvert.cpp` — 616-line self-contained C++ tool
- `src/process_aomconvert/CMakeLists.txt` — `find_package(HDF5 REQUIRED)`, `cxx_std_20`, `-O3 -march=native`
- `scripts/ndm_aom2dat` — bash driver (reads `matFile`, `topology`, `groups`, `chunkSamples` from param file)
- `descriptions/ndm_aom2dat.xml` — ndmanager UI parameter description

**Modified files:**
- `src/CMakeLists.txt` — `add_subdirectory(process_aomconvert)` after `process_smrconvert`
- `scripts/CMakeLists.txt` — `ndm_aom2dat` added to `install(PROGRAMS ...)`
- `templates/template.yaml` — `ndm_aom2dat` program block added before `ndm_ncs2dat`

**New dependency:** `libhdf5-dev` (Ubuntu) / `hdf5` (Homebrew) / `vcpkg install hdf5` (Windows).

---

### ndmanager-plugins — `ndm_klustakwik` three-tier parameter resolver

#### Three-tier parameter resolution (`scripts/ndm_klustakwik`)

Added `read_kk_param <group> <paramName> <default>` shell function that resolves each
KlustaKwik parameter through three priority levels using `yaml_read` and
`read_script_parameter` from `ndm_functions`:

1. `spikeDetection.channelGroups[group].klustakwik.<name>` via
   `yaml_read "//spikeDetection/channelGroups/group[$group]/klustakwik/$name"` — per-group
   probe-calibrated values written by `ndm_aom2dat` (tier 1, highest priority)
2. `programs[ndm_klustakwik].parameters.<name>` via `read_script_parameter` — global
   session-level override (tier 2)
3. Caller-supplied default constant — built-in fallback (tier 3)

**New parameters exposed** (were missing from the old single-tier implementation):
`MaxPossibleClusters`, `nStarts`, `PenaltyMix`, `ChunkOverlapMinutes`,
`ChunkPreseedFraction`, `MaxIter`, `SplitEvery`, `DistThresh`, `FullStepEvery`,
`ChangedThresh`, `PostRealign`, `PostRealignIter`, `NbSamplesPerSpike`, `Log`, `Screen`,
`fSaveModel`.

**Old parameters with updated defaults:**
- `maxClusters`: 100 → resolved from tier 1 (probe-calibrated) or tier 3 default 25
- `mergeThresh`: 35 → resolved from tier 1 (χ²-calibrated) or tier 3 default 42.0
- `chunkMinutes`: 5 → 10.0
- `globalMergeIter`: 30 → 50
- `timeMergeIter`: 35 → 50
- `initMethod`: random → farthest

**Changed file:** `src/ndmanager-plugins/scripts/ndm_klustakwik`

#### `templates/template.yaml` — `ndm_klustakwik` program block updated

All 20 parameters documented with correct defaults, inline comments distinguishing
probe-calibrated (tier-1) from session-level (tier-2) params, and full help text describing
the three-tier system.

**Changed file:** `templates/template.yaml`

---

## 2026-03-02 → 2026-03-09

---

### KlustaKwik — bug fixes & correctness

#### Chunked CEM boundary normalisation (`KK.cpp`)
**Symptom:** Early spikes were systematically misassigned to the wrong time chunk.  
**Root cause:** `normBounds[i]` was computed as `(chunkBoundsSec[i] * SR) / sessionSamples`, which places boundaries in raw-sample space rather than the `[0,1]` range that `Data[p*nDims+timeDim]` actually occupies (which starts at 0, not at `timeRawMin/sessionSamples`).  
**Fix:** Subtract `timeRawMin` before dividing: `(chunkBoundsSec[i] * SR - timeRawMin) / sessionSamples`. All chunk boundaries now align with the normalised time axis, correcting assignment of spikes at the start of the recording.  
**Changed file:** `src/klustakwik/KK.cpp`

#### Chunked CEM sentinel initialisation (`KK.cpp`)
**Symptom:** Spikes that fell through chunked assignment without being written were silently mapped to cluster 0 (first cluster), polluting the global merge step.  
**Fix:** `pointPacked` is now initialised to `-1` (sentinel) instead of `0`. The subsequent `packedToGlobal` lookup is guarded: `pp >= 0 ? find(pp) : end()`, so unwritten spikes cleanly fall through to noise class (0).  
**Changed file:** `src/klustakwik/KK.cpp`

#### `MergeThresh` default too large for active feature dimensionality (`KK.cpp`, `KlustaKwik.cpp`)
**Symptom:** All clusters collapsed into a single unit via MNN union-find.  
**Root cause:** The hardcoded `MergeThresh=200` (from an earlier default) greatly exceeded `χ²(nActiveDims, 0.99)`, causing every pair of cluster centroids to be considered mergeable. The correct value for 7 dims is ~18.5; for 24–25 dims, 42–44.  
**Fix:** Runtime warning when `MergeThresh` exceeds `1.5 × χ²(nSpatialDims, 0.9999)`. Recommended value is now `χ²(nActiveDims, 0.99)`. `TimeMergeIter` and `GlobalMergeIter` should also scale with `sqrt(nDims)` (~50 at 25 dims).

#### `UseFeatures` default string too short — zero-duration session (`KK.cpp`, `KlustaKwik.cpp`)
**Symptom:** Chunked CEM was bypassed entirely; session appeared to have 0-minute duration.  
**Root cause:** The default `UseFeatures` string `"11111111111100001"` (17 chars) never reached the timestamp feature at position 24 in a 25-feature `.fet` file, so the timestamp dimension remained inactive and `sessionSamples` computed as 0.  
**Fix:** Default changed to `"all"`. A length-mismatch warning is now emitted in both binary and text `LoadData` paths when the string is shorter than the feature count.

#### GPU shared memory overflow with large `MaxPossibleClusters` (`KK_cuda.cu`, `KK_hip.cpp`, `KK_sycl.cpp`)
**Symptom:** `cudaErrorInvalidValue` crash in the MStep mean kernel when `UseFeatures=all` (25 dims) × `MaxPossibleClusters=500` on pre-Blackwell hardware (48 KB shared memory limit).  
**Fix:** Shared memory requirement (`MaxPossibleClusters × nDims × sizeof(float)`) is now computed and compared against `sharedMemPerBlock` at runtime. Falls back to direct global atomics when the shared buffer would overflow. RTX 5070 Ti (Blackwell, 128 KB) is unaffected.

---

### Klusters GUI — new features

#### Toolbar N-features spinbox
New spinbox in the main toolbar for setting the number of PCA features per channel used for auto-feature selection. Reads/writes `autoSelectNFeatures` in preferences. Initialised correctly from `initializePreferences()` on startup; visibility tied to the `autoSelectFeatures` checkbox.  
**New/changed files:** `configuration.h/cpp`, `prefgeneral.h/cpp`, `prefgenerallayout.ui`, `klusters.cpp`

#### Cluster palette "S" key multi-selection
"S" key in the cluster palette toggles persistent multi-cluster selection independently of Qt's visual selection state. Behaviour: first press selects and pins the row; second press on the same row isolates it (clears all others); pressing S on a different already-selected row deselects it.  
Implementation: explicit `QSet<int> m_sRows` tracks S-pinned clusters. `ShortcutOverride` + `KeyPress` events intercepted in `KlustersApp::eventFilter` to beat the conflicting "Split Clusters" `QAction`. Arrow navigation preserves the S-pinned set via `clearSelection() + setCurrentRow() + visual restore`. `selectedClusters()` returns the union of visual selection and `m_sRows`.  
**Changed files:** `klusterspalette.h/cpp`, `klusters.cpp`

---

### Klusters GUI — bug fixes

#### Keyboard shortcut conflicts
- `Key_Plus` caused ambiguous shortcut warnings (conflicts with numpad). Replaced with `Key_Equal` (unshifted `=`) for marker-size increase and `Shift+Key_Equal` for the explicit `+` label.  
- Added `Alt+S` and `Alt+P` menu bar shortcuts.  
**Changed file:** `src/klusters/src/klusters.cpp`

#### Abort / recluster slot wiring
- Phantom `slotAbortReclustering` connect removed; duplicate `slotStopRecluster` connect deduplicated.  
**Changed file:** `src/klusters/src/klusters.cpp`

#### Preferences dialog tab navigation
- `tabKeyNavigation` disabled on `QPageListView` to prevent Tab from cycling into the page list instead of the form fields.  
**Changed files:** `src/klusters/src/qpageview.cpp`, `qpageview_p.cpp`

#### Probe drift slot API fix
`slotGenerateProbeDrift()` and `slotApplyDriftSiblings()` called the non-existent `doc->documentUrl()`; corrected to `doc->url()`.  
**Changed file:** `src/klusters/src/klusters.cpp`

#### Debug output cleanup
Removed extensive `qDebug()` spam from `qpageview.cpp` and `qpageview_p.cpp`. KlustaKwik `fprintf(stderr)` calls routed through `Output()` (respects `Screen`/`Log` config flags).

---

### ndmanager-plugins — new plugins

#### `ndm_denoiseuniform` — uniform noise event removal
Removes electrically uniform noise events from `.spk.N` / `.res.N` after `ndm_extractspikes`. Detects events where waveform energy is distributed uniformly across all channels (common-mode artefacts, motion, electrical interference).  
Two previously conflated conditions separated: *flat* waveforms (near-zero AC energy → removed by default, controlled by `removeFlat` parameter) vs. *amplitude-guarded* spikes (raw peak below `minAmplitude` → always kept). Output reports `[flat=N threshold=N amplitude-guarded=N]` per group.  
**New files:** `scripts/ndm_denoiseuniform`, `scripts/process_denoiseuniform.cpp`, `descriptions/ndm_denoiseuniform.xml`

#### `process_extractspikes_sdiff` — spatial-derivative spike extraction
Applies all-pairs spatial derivative preprocessing before threshold-based spike detection, suppressing common-mode noise while preserving spatially localised single-unit signals. Formula: `s[i] = n·x[i] − Σⱼx[j]`.  
Segfault fixed: cross-buffer boundary path computed `sdiff_cur[maxId[grp] - totalChannelCount + spkChanId[grp]]`, producing a negative index (`0 − 128 + 116 = −12`) at buffer boundaries. Non-original block removed; detection logic now matches `process_extractspikes` exactly.  
**New files:** `scripts/process_extractspikes_sdiff.cpp`, `descriptions/ndm_extractspikes_sdiff.xml`

#### `ndm_klustakwik` — batch KlustaKwik runner
Zsh/bash script that detects spike groups from `.fet` files, parses sampling rate from YAML, determines CPU thread count, and runs KlustaKwik with sensible defaults. Supports command-line override of all parameters. Parses binary `.fet` headers via `od -An -t d4 -N 4` (avoids null-byte warnings from text-mode read).  
**New file:** `scripts/ndm_klustakwik`

---

### `neurosuite_compare` — new Python package

Standalone Python package for comparing neurosuite-3 and Kilosort output. Install with `pip install -e .` from the package root.

**Readers:**
- `NeurosuiteSorting`: loads `.res.N` (`int64` LE, no header), `.spk.N` (`int16`, sample-major layout), `.fet.N` / `.clu.N` (binary with `int32` nDimensions header, auto-detects vs. legacy text)
- `KilosortSorting`: loads standard Kilosort output directory; `filter_by_channels(group_channels, method)` filters units to a spike group via `"peak"` (peak channel index matching, recommended for tetrodes), `"weight"` (energy-weighted CoM for units straddling boundaries), or `spatial_radius_um` (proximity-based, for high-density probes)

**Metrics:** SNR, peak-to-valley, waveform duration, repolarisation slope, ISI violations, Isolation Distance, L-ratio, silhouette score, presence ratio, firing rate.

**Cross-sorter comparison:** F1 agreement matrix, Hungarian optimal unit matching.

**CLI:** `python -m neurosuite_compare compare session_dir/ kilosort_dir/ --group N`

---

### libklustersshared — build fix (Ubuntu 25 / Qt 6.8+)

`QRecentFileAction` MOC error on Ubuntu 25: generated `moc_qrecentfileaction.cpp` called `_t->d->initializeRecentMenu()` / `_t->d->fileSelected(...)` but the class had no `d` member visible to MOC.  
Root cause: `Q_PRIVATE_SLOT(d, ...)` declarations in the header require a PIMPL `d` pointer; Qt 6.8+ MOC is stricter about this.  
**Changed file:** `src/libklustersshared/src/klustersshared/qrecentfileaction.h/cpp`

---

### Parameter sweep recommendations (jg05-20120316, electrode group 7)

For the primary recording (80 min, ~338 k spikes, 8-channel V-config, 32 552 Hz, 25 features):

| Tier | Parameters | Range |
|---|---|---|
| 1 (priority) | `MergeThresh` × `PenaltyK` | {35, 42, 50, 58} × {0.5, 1.0, 1.5, 2.0} — 4×4 grid, `nStarts 1` |
| 2 | `ChunkMinutes`, `SplitEvery` | {5, 10, 15, 20} × {15, 25, 40, 50} |
| 3 | `PenaltyMix`, `DistThresh` | after Tier 1–2 settled |

Recommended baseline: `-MergeThresh 42 -ChunkMinutes 10 -MaxPossibleClusters 500 -MinClusters 5 -MaxClusters 30 -nStarts 3 -SplitEvery 25 -PenaltyMix 0 -SamplingRate 32552`

---

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
