# Klusters Qt6 / C++17 Migration Notes

## Summary

Migrated klusters-2.0.0 from Qt4/Qt5 to Qt6 with modern C++17 practices.

## Breaking Changes Fixed

### CMakeLists.txt (top-level + src/)
- `cmake_minimum_required` raised to 3.16
- `find_package(Qt6 ...)` replaces Qt4/Qt5 detection logic
- `qt_standard_project_setup()` added
- `qt4_add_resources` → `qt_add_resources`
- `qt4_wrap_ui` → `qt_wrap_ui`
- Removed 150+ lines of GCC version detection boilerplate
- Removed `ENFORCE_QT4_BUILD` option and Qt4/Qt5 conditional blocks
- `target_link_libraries` uses `Qt6::Core/Gui/Widgets/Xml`

### main.cpp
- `QApplication::setGraphicsSystem("raster")` removed (Qt6 drops raster system)
- Bespoke argument parser replaced with `QCommandLineParser`
- Added `#include <QCommandLineParser>` and `#include <QFileInfo>`

### Qt API replacements
| Old | New | Files |
|-----|-----|-------|
| `qSort(list)` | `std::sort(list.begin(), list.end())` | clustersprovider, correlationview, waveformview, data, klustersdoc, klusters, eventsprovider |
| `QString::SkipEmptyParts` | `Qt::SkipEmptyParts` | correlationview, klusters, data, klustersdoc |
| `QRegExp(...)` | `QRegularExpression(...)` | eventsprovider |
| `layout->setMargin(n)` | `layout->setContentsMargins(n,n,n,n)` | clusterinformationdialog, clusterPalette, tracewidget |
| `#include <qregexp.h>` | `#include <QRegularExpression>` | eventsprovider |

### Old-style Qt includes
All lowercase Qt4-era headers (e.g. `<qfile.h>`, `<qobject.h>`) updated to
modern Qt6 style (`<QFile>`, `<QObject>`) across all source files.
Note: libklustersshared headers (`qhelpviewer.h`, `qpagedialog.h`, etc.) remain
lowercase as they are project-internal headers, not Qt headers.

### `#include <algorithm>` additions
Added to all files using `std::sort` after the `qSort` replacement.

## Remaining Work (non-breaking in Qt6, but deprecated)

1. **Old-style SIGNAL/SLOT macros (~263 occurrences)** — still compile in Qt6
   with deprecation warnings. Should be migrated to typed function pointers
   or lambdas for compile-time safety.

2. **`Q_PRIVATE_SLOT` usage** — should be replaced with lambdas or private slots.

3. **Raw pointer member variables** — consider `std::unique_ptr` for owned objects.

4. **`long` used as array index** — consider `qsizetype` or `std::ptrdiff_t`.

5. **Missing `override`/`final`** — add to all virtual method overrides.

## Build Instructions

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires libklustersshared 2.0.0 (Qt6 build) to be installed or its
cmake config file to be on `CMAKE_PREFIX_PATH`.

---

## Post-Migration Features

### Preferences tab refactor (patch 0067, 2026-05-29)

The single General preferences tab grew into a kitchen-sink of 8 group
boxes covering display settings, crash recovery, undo, KlustaKwik
reclustering, realignment, DipSplit, KNN-split, and template-matrix
display.  Split into five focused tabs:

| Tab | Content |
|---|---|
| Display | background color, marker size, line width, autoscale margin, white-on-printing, auto-show-matrices, template-matrix display thresholds |
| Session | crash recovery, undo |
| Reclustering | KlustaKwik executable + args, auto-select features, auto-select N features, mean-subtracted subdim |
| Refinement | realign (threshold/iters/maxshift), DipSplit (minsize/bloat/valley), KNN-split (K/threshold/minNew/minRef) |
| Auto-Merge | (populated in patch 0068) |

All 27 controls from the original General tab kept their exact widget
names so QSettings save/restore is unchanged across the refactor —
existing user preferences carry over without migration.

Each new tab follows the existing `PrefClusterView` / `PrefWaveformView`
pattern (Qt Designer `.ui` + trivial `PrefXxxLayout` wrapper +
`PrefXxx` class with getters/setters/glue).

Five new 32×32 RGB tab icons in `src/klusters/src/icons/` matching the
existing `clusterview.png` / `waveformview.png` style.  Generated with
PIL; script preserved in the patch commit message.

### Auto-Merge action (patches 0068 + 0069, 2026-05-29)

New `Action → Auto-Merge Similar Clusters...` menu entry (toolbar icon,
`Shift+G` shortcut) that applies the **same template cross-correlation
mechanism KKE uses** in `WithinChunkTemplateMatch` /
`WithinChunkTemplateMatchMedianKnn`.

Pipeline:

1. Filter candidates (skip 0 = artefact, 1 = noise, below `minClusterSize`).
2. Build per-cluster template — mean (all spikes) or median (up to K
   sampled spikes, fixed RNG seed for reproducible previews).
3. Optional Hann taper to suppress edge-discontinuity xcorr contributions.
4. Pairwise normalised xcorr with bounded shift; pairs scoring at or
   above the user threshold are merge-edge candidates.
5. Union-find on the score graph → connected components of size ≥ 2 are
   merge groups.
6. If preview is enabled, modal dialog with a checkbox per group.
7. Apply each accepted group via `doc->groupClusters` — the existing
   merge path, so the operation integrates with klusters' undo/redo.

Settings tab (Preferences → Auto-Merge) covers algorithm (mean/median),
median K, score threshold, max shift (0 = auto = nSamp/4 matching KKE),
Hann taper samples, min cluster size, target scope (selected /
all-active), preview-before-apply.  All defaults match KKE flag
defaults so interactive klusters merges and offline KKE merges produce
**consistent decisions on the same data**:

| Setting | Default | KKE equivalent |
|---|---|---|
| Algorithm | Median | matches `MedianKnnTemplateMatchEnable=1` |
| Median K | 50 | matches `MedianKnnTemplateMatchK` |
| Score threshold | 0.98 | matches `TemplateMatchScore` |
| Max shift | 0 = auto (`nSamp/4`) | matches `WithinChunkTemplateMatch` |
| Hann taper | 0 = off | matches `TemplateMatchTaperHannSamples` |
| Min cluster size | 25 | matches KKE clusters threshold |

8 new fields added to `Configuration` (in the `General` QSettings
group) with the standard `setXxx` / `getXxx` / `getXxxDefault` pattern.

Files added:

- `src/klusters/src/autoMerge.{h,cpp}` — algorithm + preview dialog
- `src/klusters/src/icons/auto_merge_tool.png` — 22×22 RGBA toolbar icon

Files modified:

- `src/klusters/src/klusters.{h,cpp}` — `mAutoMerge` action, `slotAutoMerge`
- `src/klusters/src/configuration.{h,cpp}` — 8 new fields + QSettings round-trip
- `src/klusters/src/prefautomerge*.{h,cpp,ui}` — settings UI

#### Known follow-ups

The current `AutoMerge::computeProposals` runs synchronously on the GUI
thread with `QApplication::processEvents()` for cancel/responsiveness.
For selected-scope mode this is fine; for all-active-scope on large
sessions a `QThread` async variant mirroring `TemplateMatrixThread`
would keep the UI more responsive (queued in the roadmap; see
`SESSION_SUMMARY_phase8_klusters_automerge.md` §7.4).

`normXcorr` in `autoMerge.cpp` is a verbatim copy of
`templatematrixthread.cpp`'s `tmNormXcorr`; extraction to a shared
header is a future cleanup (§7.3 of the same handoff document).

