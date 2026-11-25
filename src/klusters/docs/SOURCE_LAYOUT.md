# Klusters — where the source files are

`src/klusters/src/` holds one subdirectory per layer. The build file, the resources (`.qrc`,
`icons/`, `cursors/`, `keymaps/`) and the application icons stay at its top.

| Directory | What is in it | Typical files |
|---|---|---|
| `app/` | The application window and its shell: the menus and the command table, the action state machine, the slots a command runs, the radial menu, the plugin registry, the self-test harness, `main.cpp`. | `klusters.cpp`, `klusters_*.cpp`, `klusters_commands.cpp`, `actionid.*`, `radialmenu.*` |
| `document/` | The document: `KlustersDoc` and everything it owns that is not the spike store — the edits, the undo history, the hierarchy, saving and its commit, feature spaces, the realign and the nudge, the curation log, the parameter-file readers. | `klustersdoc*.cpp`, `undohistory.*`, `hierarchyfiles.*`, `filecommit.*`, `curationlogger.*` |
| `data/` | The spike store and what reads the files: `Data`, the providers, the `.spk` readers and overlay, the template stores, small value types. | `data.*`, `spkreader.*`, `spkmemory.*`, `*provider.*`, `template*store.*`, `realignio.*` |
| `views/` | Everything drawn: the frame chain, the cluster / waveform / correlation / trace / template views, the four curation matrices and their shared base and helpers, the palettes, small widgets. | `*view.*`, `baseframe.*`, `viewwidget.*`, `matrix*.h`, `clusterpalette.*` |
| `jobs/` | Work off the GUI thread and how it is run: the job pool, tokens and tickets, the serial queue, the realign job and worker, the save thread, each view's compute thread, the process runner. | `klustersjobpool.*`, `serialjobqueue.h`, `realignjob.h`, `*thread.*`, `processwidget.*` |
| `compute/` | Algorithms without a window: the grouping assistant's error matrix, the PCA refinement and core-cut kernels with their GPU back ends, auto-merge, burst followers, drift geometry, masks. | `groupingassistant*`, `pca_refine_*`, `corecut*`, `automerge.*` |
| `dialogs/` | Dialogs that are not preference pages. | `clusterinformationdialog.*`, `plugindialog.*` |
| `prefs/` | The configuration, the preference table and binder, the Preferences dialog and its pages (`.ui` included). | `configuration*.cpp`, `prefspec.h`, `prefbinder.*`, `pref*.cpp`, `pref*layout.ui` |

## Rules

- **Includes are by bare file name** — `#include "data.h"`, never `"../data/data.h"`. Every
  subdirectory is on the include path of the application
  (`klusters_SOURCE_DIRS` in `src/klusters/src/CMakeLists.txt`) and of the tests
  (`_klu_inc` in `src/klusters/test/CMakeLists.txt`). A file can therefore move to another
  directory by changing one line of the build file; **file names stay unique across the
  directories**.
- **File names are lowercase**, words run together or joined by `_` for the parts of one class
  (`klustersdoc_realign.cpp`).
- **A new file** goes where its layer is and is added to `klusters_SRCS` with its directory
  (`views/newview.cpp`). A `.ui` file is added to `klusters_UI` the same way. A test that
  compiles it names it as `${_klu_src}/views/newview.cpp`.

## Which layer includes which

The directories name layers; the build does not enforce a direction between them, and the
code does not yet have one. Includes from the files of one directory (rows) to the files of
another (columns), counted on 2026-11-25:

| from \ to | app | document | data | views | jobs | compute | dialogs | prefs |
|---|---|---|---|---|---|---|---|---|
| **app** | · | 15 | 6 | 63 | 42 | 8 | 7 | 21 |
| **document** | 11 | · | 15 | 32 | 14 | 8 | 0 | 12 |
| **data** | 1 | 6 | · | 1 | 2 | 1 | 0 | 0 |
| **views** | 1 | 12 | 32 | · | 17 | 6 | 1 | 8 |
| **jobs** | 1 | 2 | 12 | 9 | · | 6 | 0 | 2 |
| **compute** | 0 | 1 | 4 | 0 | 1 | · | 0 | 2 |
| **dialogs** | 2 | 1 | 0 | 0 | 0 | 0 | · | 1 |
| **prefs** | 7 | 0 | 0 | 2 | 0 | 0 | 1 | · |

The intended direction is `app` → `document` → `data`, with `views` and `jobs` reading the
document and the data, and `compute` free of windows. What stands against it today:

- the **document includes the application and the views** (11 and 32 includes): `KlustersDoc`
  calls into `KlustersApp` and the views directly (`klustersdoc_edit.cpp`,
  `klustersdoc_dipsplit.cpp`, ...) — the audit's §2;
- **`data.cpp` includes `klusters.h` and `waveformview.h`**, and the data layer includes
  `reservedclusters.h` from `document/` (six of its includes: that header belongs in `data/`);
- `automerge.cpp` in `compute/` includes `klustersdoc.h`.

A new include should not add to the lower-left of the table.

## Finding a file

The other internal references name files without their directory. `git ls-files '*name*'`
finds one; the table above says where a kind of file lives.
