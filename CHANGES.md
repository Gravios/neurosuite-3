# Drop-in patch: probe library + Probe tab + ndm_estimatedrift

Unzip from the neurosuite-3 root:

    cd /path/to/neurosuite-3
    tar -xzf neurosuite-3-probe-drift-dropin.tar.gz

Then rebuild normally:

    cmake --build build

---

## Files changed (4 CMakeLists, 1 QRC)

### `src/libklustersshared/src/CMakeLists.txt`
Added `parameteryamlreader_probes.cpp` to `_srcs` and
`parameteryamlreader_probes.h` to `_hdrs` so the probe YAML extensions are
compiled into and exported from `libklustersshared`.

### `src/ndmanager/src/CMakeLists.txt`
Added `probepage.cpp` to the regular sources list and
`probelayout.cpp` to the AUTOUIC layout wrappers list.
AUTOUIC will generate `ui_probelayout.h` from `probelayout.ui` automatically.

### `src/ndmanager/src/ndmanager-icons.qrc`
Added `icons/probe.png` so the Probe tree-item has its own icon.

### `src/ndmanager-plugins/scripts/CMakeLists.txt`
Added `ndm_estimatedrift` to the `install(PROGRAMS …)` block.
Added a second `install(PROGRAMS process_estimatedrift.py …)` so the Python
helper lands in `${CMAKE_INSTALL_BINDIR}` alongside the bash driver.
Added `ndm_estimatedrift` to the `add_manpage` loop.

### `src/nphys-data/src/CMakeLists.txt`
Added a `file(GLOB …)` + `install(FILES …)` block that installs all
`probes/neuronexus/*.probe` files to
`${CMAKE_INSTALL_DATADIR}/neurosuite/probes/neuronexus/`.

---

## New files

### `src/libklustersshared/src/klustersshared/parameteryamlreader_probes.h/cpp`
Free-function YAML reader/writer extensions for the new `probes` top-level
section and the `probeId`/`shankIndex` fields on `channelGroups` entries.
These are separate from `ParameterYamlReader` to avoid altering its API.
Call from `ndmanagerdoc.cpp` after the main `loadFromReader()` call.

### `src/ndmanager/src/probelayout.ui`
Qt Designer UI for the Probe tab page.  AUTOUIC generates `ui_probelayout.h`
at build time.

### `src/ndmanager/src/probelayout.h` / `probelayout.cpp`
Thin AUTOUIC wrapper class (`ProbeLayout : QWidget, Ui_ProbeLayout`), exactly
matching the pattern of `AnatomyLayout` / `SpikeLayout`.

### `src/ndmanager/src/probepage.h` / `probepage.cpp`
`ProbePage` — the Probe tab page class.  Inherits `ProbeLayout`.
Implements `setProbes()` / `getProbes()` / `setLibraryPath()` /
`getLibraryPath()` called by `ParameterView`.
Add to `parameterview.cpp` (see integration note below).

### `src/ndmanager/src/icons/probe.png`
32×32 RGBA icon: silicon shank silhouette with gold octrode sites.

### `src/ndmanager-plugins/scripts/ndm_estimatedrift`
Bash driver for the drift estimation plugin.  Reads parameters from the
session file, checks for curated `.clu.N` files, delegates to
`process_estimatedrift.py`.

### `src/ndmanager-plugins/scripts/process_estimatedrift.py`
Core algorithm (Python 3, numpy, pyyaml, optional scipy):
- Method 1: per-unit spatial-profile cross-correlation — for each tracked
  unit, cross-correlates its amplitude-vs-depth fingerprint between the
  reference window and each subsequent window.  Weighted median of per-unit
  shifts (weights = geometric mean of spike counts).  Geometric consistency
  check flags units whose pairwise depth relationships changed.
- Method 2: population amplitude-profile cross-correlation — pooled
  multi-unit fingerprint, used as fallback and independent cross-check.
Writes `SESSION.drift` (YAML).

### `src/ndmanager-plugins/scripts/ndm_estimatedrift.docbook`
DocBook 5.2 man page.  Built by the existing `add_manpage()` mechanism in
`NdmManPage.cmake` when xsltproc + DocBook XSL are available.

### `src/ndmanager-plugins/descriptions/ndm_estimatedrift.xml`
Plugin description XML for the ndmanager Plugins tab (parameter defaults,
help text).  Picked up automatically by the existing `file(GLOB _xml …)`
rule in `descriptions/CMakeLists.txt`.

### `src/nphys-data/src/probes/neuronexus/*.probe` (43 files)
NeuroNexus probe configuration library (YAML).  Installed to
`${CMAKE_INSTALL_DATADIR}/neurosuite/probes/neuronexus/`.

---

## Integration note — `parameterview.cpp`

Two small additions are required in `src/ndmanager/src/parameterview.cpp`.
These are intentionally left as a manual step to avoid a fragile source patch:

**1. Include and member (in the class definition in `parameterview.h`):**
```cpp
#include "probepage.h"
// ...
ProbePage* probe;   // add alongside AnatomyPage* anatomy
```

**2. Construction (in `ParameterView::ParameterView(…)`, after the spike block):**
```cpp
if (expertMode) {
    probe = new ProbePage;
    mStackWidget->addWidget(probe);
    mParameterTree->addPage(":/icons/probe", tr("Probes"), probe);
} else {
    probe = new ProbePage();
}
connect(this, SIGNAL(resetModificationStatus()),
        probe, SLOT(resetModificationStatus()));
```

**3. Initialize (in `ParameterView::initialize(…)`, after `anatomy->setGroups(…)`):**
```cpp
probe->setProbes(probeList);
probe->setLibraryPath(probeLibraryPath);
```

**4. Retrieve (in `ParameterView::getInformation(…)`, after `anatomy->getGroups(…)`):**
```cpp
probe->getProbes(probeList);
probeLibraryPath = probe->getLibraryPath();
```
