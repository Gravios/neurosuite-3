# libklustersshared — developer reference

**Contents:**
[1. Overview](#1-overview) ·
[2. The YAML parameter layer (summary)](#2-the-yaml-parameter-layer-summary) ·
[3. The input engine (summary)](#3-the-input-engine-summary) ·
[4. `src/xcorr` — the realign engine](#4-srcxcorr--the-realign-engine) ·
[5. `src/gui/page` — the paged preferences dialog](#5-srcguipage--the-paged-preferences-dialog) ·
[6. Other GUI widgets](#6-other-gui-widgets) ·
[7. `src/klustersshared` utilities](#7-srcklustersshared-utilities) ·
[8. Tests](#8-tests) ·
[9. Known issues](#9-known-issues)

This page is written from the sources under `src/libklustersshared/`. A
`file:line` citation was right when written; if the code has moved, search
for the function it names. It does not repeat
three existing references; it summarises and links them:

| Topic | Reference |
|---|---|
| YAML parameter files, probe reader, shared data types, schema | [doc/libklustersshared/README.md](../../../doc/libklustersshared/README.md) |
| The `input::` binding engine in `src/input/` | [Klusters INPUT_SYSTEM.md](../../klusters/docs/INPUT_SYSTEM.md), [NeuroScope INPUT_SYSTEM.md](../../neuroscope/docs/INPUT_SYSTEM.md) |
| `DockArea` (`src/gui/dockarea.*`) | [VIEW_SYSTEM.md §3](../../klusters/docs/VIEW_SYSTEM.md#3-klustersview--the-per-document-container--and-dockarea) |

---

## 1. Overview

`libklustersshared` is the shared Qt6 library of the three NeuroSuite GUI
programs. It holds the YAML parameter-file layer, the input-binding engine,
the KDE-derived dialog and widget set, the light/dark theme, colour
containers and a few small data types. It also hosts, as plain source files
that are **not** compiled into the library, the spike-realignment
cross-correlation kernel (`src/xcorr/`) and two header/source pairs that
Qt-free tools compile directly (`sdiff_pairs.h`, `progressbar.*`).

### 1.1 Who links it

| Consumer | How | Evidence |
|---|---|---|
| klusters | links `KlustersShared::klustersshared` | `src/klusters/src/CMakeLists.txt` (`target_link_libraries(klusters PRIVATE KlustersShared::klustersshared …)`) |
| neuroscope | links `KlustersShared::klustersshared` | `src/neuroscope/src/CMakeLists.txt` |
| ndmanager | links `KlustersShared::klustersshared` | `src/ndmanager/src/CMakeLists.txt` |
| kiloklustakwik | compiles `src/xcorr/*` into its own executables (`_xcorr_dir`) | `src/kiloklustakwik/CMakeLists.txt` |
| ndmanager-plugins | include path only: `sdiff_pairs.h`, `customtypes.h`; compiles `progressbar.cpp` | e.g. `src/ndmanager-plugins/src/process_nlxconvert/CMakeLists.txt` |

Each GUI app guards its lookup with `if(NOT TARGET KlustersShared::klustersshared)`
then `find_package(LibKlustersShared 2.1.0 REQUIRED …)`, so inside the monorepo
the in-tree target is used and outside it an installed package is used. The
top-level `CMakeLists.txt` also adds `add_dependencies(<app> neurosuite-qt)`
for the three apps as a redundant build-order edge.

### 1.2 Source areas

| Directory | Built into the `.so`? | Contents |
|---|---|---|
| `src/klustersshared/` | yes (except `sdiff_pairs.h`, which is header-only and not installed) | YAML layer, colours, theme, small types, `ZoomWindow`, `ProgressBar` |
| `src/input/` | yes | `input::` binding engine and the shared Preferences ▸ Input page |
| `src/gui/`, `src/gui/page/` | yes | `DockArea`, `QExtendTabWidget`, `QHelpViewer`, `QRecentFileAction`, `QColorButton`, `KlusterRubberBand`; paged dialog framework; icons and cursors (`.qrc`) |
| `src/xcorr/` | **no** | realign kernel: OpenMP, CUDA, HIP and SYCL back ends plus a runtime dispatcher |
| `src/HTML/en/common/`, `src/images/` | **no** | KDE handbook chrome (CSS, logos, licence pages) and `note.gif`. Nothing in any `CMakeLists.txt` or `.qrc` references them (see §9) |
| `test/` | no | ten ctest programs (each compiles or links what it tests) plus an unbuilt legacy demo (§8) |

The embedded resources are `src/gui/libklustersshared-icons.qrc` (prefix
`/shared-icons`: document/dialog/edit icons, `folder-open`, `configure`,
`help-contents`) and `src/gui/libklustersshared-cursors.qrc` (prefix
`/shared-cursors`: `zoom_cursor`, `measure_cursor`, `select_*_cursor`, …).
Apps refer to them as `QIcon(":/shared-icons/folder-open")`.

### 1.3 CMake target and package

| Item | Value | Where |
|---|---|---|
| Real target | `neurosuite-qt` (`SHARED`) | `src/libklustersshared/src/CMakeLists.txt` |
| Aliases | `Neurosuite::qt`, `KlustersShared::klustersshared`, `klustersshared` | same |
| Output file | `libneurosuite-qt.so.2.1.0`, `SOVERSION 2` | `set_target_properties(… OUTPUT_NAME "neurosuite-qt")` |
| Export | set `LibKlustersSharedTargets`, namespace `KlustersShared::`, `EXPORT_NAME klustersshared` | `src/libklustersshared/CMakeLists.txt` |
| Package | `LibKlustersSharedConfig.cmake` + `…ConfigVersion.cmake` (`SameMajorVersion`), installed to `${libdir}/cmake/LibKlustersShared` | `LibKlustersSharedConfig.cmake.in` |
| Build-tree export | written by the top-level `CMakeLists.txt` after `ns_add_subdir(libklustersshared …)`, which also sets `LibKlustersShared_DIR` | top-level `CMakeLists.txt` |
| Skip flag | `-DNS_SKIP_LIBKLUSTERSSHARED=ON` | top-level `CMakeLists.txt` |

The config file calls `find_dependency(yaml-cpp)`,
`find_dependency(Qt6 COMPONENTS Core Gui Widgets)` and
`find_dependency(LibNeurosuiteCore)` before including the targets file.

Compile settings set in `src/libklustersshared/CMakeLists.txt`: C++20,
`AUTOMOC/AUTOUIC/AUTORCC`, `CMAKE_CXX_VISIBILITY_PRESET hidden` with hidden
inline visibility, and on GCC/Clang (non-Apple) the link options
`-Wl,--fatal-warnings -Wl,--no-undefined`. Hidden visibility means a class or
function is reachable from an app only if it carries `KLUSTERSSHARED_EXPORT`.

`src/libklustersshared/cmake/modules/` (three KDE-era version macros) is not
referenced by any CMake file.

### 1.4 Dependencies

| Dependency | Linkage | Notes |
|---|---|---|
| Qt6 ≥ 6.4: Core, Gui, Widgets | `PUBLIC` | `find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)`. No Xml. |
| yaml-cpp | `PUBLIC` | YAML layer |
| `Neurosuite::core` (libneurosuite-core) | `PUBLIC` | Qt-free core (`neurofileio`, …); must be configured first, which the top-level build guarantees |
| OpenMP, CUDA, HIP, SYCL | none | only the `src/xcorr/` sources use them, and only inside the consumer's target |

### 1.5 Build options

The library itself has no GPU code and no options of its own beyond the
monorepo's `NS_BUILD_TESTS` (§8) and `NS_SKIP_LIBKLUSTERSSHARED`. The GPU
options that matter for `src/xcorr/` are declared at the top level and again in
each consumer:

| Option | Default | Effect on xcorr |
|---|---|---|
| `USE_CUDA` | `ON` (auto-off without `nvcc`) | consumer compiles `realign_xcorr_cuda.cu` and defines `USE_CUDA` |
| `USE_HIP` | `ON` (auto-off without ROCm `hip` package / `hipcc`) | consumer compiles `realign_xcorr_hip.hip` (`LANGUAGE HIP`) and defines `USE_HIP` |
| `USE_SYCL` | `ON` (auto-off without `icpx`) | consumer compiles `realign_xcorr_sycl.cpp` with `-fsycl` and defines `USE_SYCL` |

The probing helpers live in [`cmake/GpuBackends.cmake`](../../../cmake/GpuBackends.cmake)
(`ns_gpu_find_cuda_compiler`, `ns_gpu_select_cuda_arch`,
`ns_gpu_select_sycl_compiler`, `NS_GPU_CUDA_ARCH_FALLBACK`). See
[BUILD.md §4](../../../doc/BUILD.md#4-gpu-backends) for platform details.

### 1.6 Export macro

`src/libklustersshared_export.h`:

```c++
#ifdef KLUSTERSSHARED_STATICLIB          // compile sources directly, no .so
#  define KLUSTERSSHARED_EXPORT
#elif defined(KLUSTERSSHARED_BUILD_KLUSTERSSHARED_LIB)   // set PRIVATE on neurosuite-qt
#  define KLUSTERSSHARED_EXPORT Q_DECL_EXPORT
#else                                    // consumers
#  define KLUSTERSSHARED_EXPORT Q_DECL_IMPORT
#endif
```

`KLUSTERSSHARED_STATICLIB` is what the Klusters input unit tests define when
they compile `src/input/*.cpp` straight into a test binary
(`src/klusters/test/CMakeLists.txt`). The header is committed, not generated,
so consumer AUTOMOC never waits for the library to build.

### 1.7 How a consumer includes headers

The target publishes these include directories:

| Build tree | Install tree |
|---|---|
| `src/`, `src/klustersshared/`, `src/gui/`, `src/gui/page/` | `${includedir}`, `${includedir}/klustersshared` |

All headers in `_hdrs` are installed flat into `${includedir}/klustersshared/`;
`src/input/*.h` keep their directory, `${includedir}/klustersshared/input/`.
That makes the following spellings work in both trees, and all of them are in
use:

```c++
#include <klustersshared/theme.h>      // prefixed (preferred for new code)
#include "itemcolors.h"                // bare, src/klustersshared/
#include <qpagedialog.h>               // bare, src/gui/page/
#include <dockarea.h>                  // bare, src/gui/
#include "input/bindingregistry.h"     // via src/ or ${includedir}/klustersshared
```

### 1.8 Place in the layering

[ARCHITECTURE.md §2](../../../doc/dev/ARCHITECTURE.md#2-target-architecture)
plans four layers: `neuro-io` and `neuro-core` (Qt-free), `neuro-compute`
(xcorr/realign, CPU + GPU, Qt-free) and `neuro-ui` (Qt). Today
libklustersshared is the `neuro-ui` precursor but still also carries
`neuro-core` material (`Array`, `types.h`, `customtypes.h`) and the
`neuro-compute` sources in `src/xcorr/` (compiled per app through `_xcorr_dir`,
which is Stage 2 of that plan). The file-format readers (`neurofileio`) have
already moved out to `src/libneurosuite-core`, which this library links.

---

## 2. The YAML parameter layer (summary)

`ParameterYamlReader`, `ParameterYamlWriter` and `ParameterYamlModifier`
(`src/klustersshared/parameteryaml*.{h,cpp}`) are the single implementation of
the session parameter YAML; `parameteryamlreader_probes.{h,cpp}` adds the
`probes:` section and the `.probe` file. The value types `GeneralInformation`,
`FileInformation`, `ProgramInformation`, `NeuroscopeVideoInfo` and
`ChannelColorEntry` are the reader/writer's data carriers. The per-app wrappers
(`NdManagerYamlReader`, `NeuroscopeYamlReader`, `KlustersYamlReader`) live in
the apps.

All of this — API, schema, probe format — is documented in
[doc/libklustersshared/README.md](../../../doc/libklustersshared/README.md).
The regression test for the modifier is described in §8.

---

## 3. The input engine (summary)

`src/input/` is the `input::` namespace shared by Klusters and NeuroScope:

| File | Role |
|---|---|
| `chord.h/.cpp` | `Chord`: key / button / wheel trigger plus modifiers; `QKeySequence` bridge |
| `command.h`, `ctx.h`, `inputscope.h` | `Command` (unit of behaviour), `Ctx` (view + event), `InputScope` (context layer) |
| `bindingregistry.h/.cpp` | the registry: defaults, override layer, resolver |
| `inputdispatcher.h/.cpp` | Qt glue: `registry()`, `chordFromEvent()`, `dispatch()` |
| `keymapprofile.h/.cpp` | named, saveable binding layouts |
| `prefinput.h/.cpp`, `buttonchordedit.*`, `wheelchordedit.*` | generated Preferences ▸ Input page and its row editors |
| `inputprefsstore.h` | `InputPrefsStore`, the interface each app's `Configuration` implements |

The design, dispatch path and recipes are in
[Klusters INPUT_SYSTEM.md](../../klusters/docs/INPUT_SYSTEM.md) and
[NeuroScope INPUT_SYSTEM.md](../../neuroscope/docs/INPUT_SYSTEM.md).

---

## 4. `src/xcorr` — the realign engine

### 4.1 What it computes

Given a batch of spike waveforms and a template (normally the cluster mean),
the kernel finds, for each spike, the circular lag `τ ∈ [−maxShift, +maxShift]`
that maximises the normalised cross-correlation over **all channels and all
samples**:

```
num(τ)   = Σ_ch Σ_s tmpl[ch,s] · spk[ch, (s+τ+N) mod N]
score(τ) = num(τ) / ( ‖tmpl‖ · ‖spk‖ )        ‖·‖ over the full window
```

Both norms are the full-window energies, so the denominator does not change
with `τ`. The shift is circular (wrap-around), so every lag uses the same number
of samples and there is no bias towards large lags. On ties the lowest index
(the most negative lag) wins in every back end.

`realign_center` is the companion step: a single circular-centroid shift that
recentres a whole cluster after per-spike alignment, and a template-free
per-spike centroid de-jitter.

### 4.2 Files

| File | Compiled when | Content |
|---|---|---|
| `realign_xcorr.h` | always | C API of every back end + `XcorrDispatch` C++ API |
| `realign_xcorr_dispatch.cpp` | always | runtime back-end selection; stubs for back ends not compiled in |
| `realign_xcorr_omp.cpp` | always | CPU back end; parallel over spikes when OpenMP is linked |
| `realign_xcorr_cuda.cu` | `USE_CUDA` | CUDA back end (`#ifdef USE_CUDA` wraps the file) |
| `realign_xcorr_hip.hip` | `USE_HIP` | HIP port of the CUDA back end |
| `realign_xcorr_sycl.cpp` | `USE_SYCL` | SYCL back end, two kernels (scores, then per-spike reduction) |
| `realign_center.h/.cpp` | always | `circularRecenterShift`, `perSpikeCentroidShifts` |

None of these are in the `neurosuite-qt` target; see the comment at the top of
`src/libklustersshared/src/CMakeLists.txt`. Each consumer adds the sources and
`${_xcorr_dir}` to its own target so it controls the `USE_*` macros:

| Consumer target | Sources |
|---|---|
| `klusters` | omp, dispatch, center; + cuda/hip/sycl per `USE_*` (`src/klusters/src/CMakeLists.txt`) |
| `KiloKlustaKwik_cpu`, `KiloKlustaKwik` | omp, dispatch, center via `_cpu_srcs`; `KiloKlustaKwik` adds `realign_xcorr_cuda.cu` or the HIP object library |
| `KiloKlustaKwik_sycl` | omp, dispatch, sycl, center |
| `kk_test_klusters_realign` | omp, dispatch, center (`src/kiloklustakwik/test/CMakeLists.txt`) |

### 4.3 API

```c++
// realign_xcorr.h — extern "C", one pair per back end (cuda, hip, sycl, omp)
int xcorr_cuda_available();                 // also _hip_, _sycl_
int xcorr_cuda_compute(const int16_t* waveforms, const int16_t* tmpl,
                       int nSpikes, int nChannels, int nSamples,
                       int maxShift, float minScore,
                       int* shifts_out, float* scores_out);   // 0 = success

namespace XcorrDispatch {                   // C++ only
int compute(/* same parameters */);         // best back end, OMP fallback
const char* backendName();                  // "CUDA", "HIP (AMD ROCm)", "SYCL (Intel oneAPI)", "OpenMP CPU"
}
```

```c++
// realign_center.h
namespace realign_center {
struct RecenterResult { int shift; double centroid; double R; bool applied; };
RecenterResult circularRecenterShift(const double* energy, int nSamp,
                                     int peakPos, double rMin);
void perSpikeCentroidShifts(const double* energy, int nSpikes, int nSamp,
                            std::vector<int>& shiftOut);
}
```

### 4.4 Data layout and output contract

| Buffer | Shape | Index |
|---|---|---|
| `waveforms` | `nSpikes × nChannels × nSamples`, `int16` | `[(s·nChannels + ch)·nSamples + t]` — **channel-major per spike** |
| `tmpl` | `nChannels × nSamples`, `int16` | `[ch·nSamples + t]` |
| `shifts_out` | `nSpikes`, `int` | best lag if `score ≥ minScore`, else 0 |
| `scores_out` | `nSpikes`, `float` | score at the best lag (always written), range [−1, 1] |
| `energy` (center) | `nSamp` or `nSpikes × nSamp`, `double`, ≥ 0 | built by the caller in its own layout |

A `.spk` record on disk is sample-major (`[t][ch]`), so callers transpose
before calling (for example `KK_chunked.cpp` repacks into `xcWfm` just before
`XcorrDispatch::compute`). All buffers are host memory; GPU back ends allocate,
copy, run and copy back inside each call.

Shift convention, shared by xcorr and `realign_center`: the realigned waveform
is `new[t] = old[(t + shift) mod N]` and the corrected timestamp is
`ts + shift`. `circularRecenterShift` returns `applied = false, shift = 0` when
the mean resultant length `R < rMin` (energy split into two lobes); `R` and
`centroid` are filled either way for logging.

### 4.5 Dispatch, GPU selection and threading

`XcorrDispatch::compute` detects the back end on its first call and holds it
in a function-local `static std::atomic` (`backendState()` in
`realign_xcorr_dispatch.cpp`), so detection runs exactly once even when the
first calls come from several threads. Probe order:

1. CUDA, if compiled in and `cudaGetDeviceCount() > 0`;
2. HIP, if compiled in and `hipGetDeviceCount() > 0`;
3. SYCL, if compiled in **and** the environment variable `KLUSTERS_USE_SYCL` is
   set (any value) and a `gpu_selector_v` device exists. The opt-in exists
   because some Level Zero drivers call `std::terminate()` during JIT; the SYCL
   back end also wraps its work in a terminate guard;
4. OpenMP CPU, always.

The chosen back end is printed once to stdout as `[realign] xcorr backend: …`.

If a GPU call returns non-zero, the dispatcher runs the same call on the
OpenMP path and **permanently** switches the process to OpenMP. The switch is
an atomic compare-exchange, and only the thread that makes it prints the
message. Failures include any allocation failure and a launch error. A call
whose `2·maxShift + 1 > 1024` (one CUDA/HIP thread block per spike holds every
lag, so `maxShift > 511`) is not a failure: on CUDA/HIP that call runs on
OpenMP and the GPU stays in use for later calls.

Device choice: CUDA and HIP use the runtime's default device (no
`cudaSetDevice`); SYCL uses `gpu_selector_v`. Steer them with
`CUDA_VISIBLE_DEVICES`, `HIP_VISIBLE_DEVICES` or `ONEAPI_DEVICE_SELECTOR`.

Threading:

- `xcorr_omp_compute` runs `#pragma omp parallel for schedule(static)` over
  spikes; each iteration writes only its own output slot. Without OpenMP it is
  a serial loop.
- In Klusters the realign runs off the GUI thread in `RealignWorker`, queued on
  the serial realign lane so only one realignment touches the pending
  `.spk/.res/.fet` at a time — see
  [CONCURRENCY.md §7.2](../../klusters/docs/CONCURRENCY.md#72-realignworker--realignjob--the-serialized-realign-pipeline).
- KiloKlustaKwik calls `XcorrDispatch::compute` from inside an OpenMP
  parallel loop (`KK_templatematch.cpp`); the atomic back-end state above
  makes that safe (§9 item 10, `klustersshared_xcorr_dispatch_test`).

### 4.6 Callers

| Caller | Use |
|---|---|
| `src/klusters/src/klustersdoc_realign.cpp` | interactive per-cluster realign (`compute` at `:1049`), score-only pass with `maxShift = 0` (`:1663`), `circularRecenterShift` (`:295`), `perSpikeCentroidShifts` (`:922`); `backendName()` in the log |
| `src/klusters/src/spikerealigndialog.cpp` | shows `backendName()` in the dialog |
| `src/kiloklustakwik/src/klusters_realign.cpp` | Klusters-style realign inside the sorter |
| `src/kiloklustakwik/src/KK_chunked.cpp`, `KK_templatematch.cpp` | cross-chunk and pairwise template similarity (`nSpikes = 1` or batched partners, `minScore = 0`) |
| `KK.cpp`, `KK_split.cpp`, `KK_refeaturize.cpp`, `KK_timeshift.cpp` | realign and recentre steps of the sorter |

Background on GPU batching of the separate PCA-centre realign path (not this
kernel) is in [realign-gpu-batch.md](../../../doc/design/realign-gpu-batch.md).

### 4.7 Adding a back end

1. Write `realign_xcorr_<x>.<ext>` implementing `xcorr_<x>_available()` and
   `xcorr_<x>_compute()` with the exact signature, wrapped in
   `#ifdef USE_<X>`. Keep the contract of §4.4: host pointers, channel-major,
   full-window norms, circular lag, lowest-lag tie-break, `shifts_out = 0` below
   `minScore`, scores always written, non-zero return on any failure.
2. Declare both functions in the `extern "C"` block of `realign_xcorr.h`.
3. In `realign_xcorr_dispatch.cpp`: add an `#ifndef USE_<X>` stub pair, an enum
   value, a probe in `detectBackend()` at the right priority, a `case` in
   `compute()` that demotes to OMP on failure, and a name in `backendName()`.
4. Wire the source and `USE_<X>` definition into every consumer target
   (`src/klusters/src/CMakeLists.txt`, `src/kiloklustakwik/CMakeLists.txt`),
   with detection helpers in `cmake/GpuBackends.cmake` if needed.
5. Compare its `shifts_out`/`scores_out` against `xcorr_omp_compute` on the same
   input. `kk_test_klusters_realign` exercises only the OMP path.

---

## 5. `src/gui/page` — the paged preferences dialog

A Qt6 port (via KDAB's Qt4 port) of KDE's `KPageDialog` family. Klusters and
NeuroScope build their Preferences dialogs on it; ndmanager does not use it
(`queryoutputdialog` is a plain `QDialog`).

### 5.1 Classes

| Class | Base | Role |
|---|---|---|
| `QExtendDialog` | `QDialog` | dialog with a standard button row (`ButtonCode` flags `Help, Default, Ok, Apply, Try, Cancel, Close, No, Yes, Reset, Details, User1-3`), one `mainWidget`, an optional details widget, per-button signals (`okClicked`, `applyClicked`, `defaultClicked`, `helpClicked`, …), `buttonClicked(ButtonCode)`, and `restoreDialogSize()`/`saveDialogSize()` |
| `QPageDialog` | `QExtendDialog` | owns a `QPageWidget` as its main widget and forwards the page API (`addPage`, `addSubPage`, `insertPage`, `removePage`, `setCurrentPage`, `setFaceType`) and the `currentPageChanged`/`pageRemoved` signals |
| `QPageWidget` | `QPageView` | a `QPageView` that owns a `QPageWidgetModel`; adds `pageToggled` |
| `QPageView` | `QWidget` | renders any page model with a `FaceType`: `Auto`, `Plain`, `List`, `Tree`, `Tabbed`; left/top navigator plus a stacked page area |
| `QPageModel` | `QAbstractItemModel` | abstract model; roles `HeaderRole`, `WidgetRole` |
| `QPageWidgetModel` | `QPageModel` | tree of `QPageWidgetItem`s |
| `QPageWidgetItem` | `QObject` | one page: `widget`, `name`, `header`, `icon`, `checkable`/`checked`, `enabled` |
| `KLusterSeparator` | `QFrame` | horizontal/vertical line between the page area and the buttons (internal, not exported or installed) |

Private (`*_p.h`, not installed) classes: `QExtendDialogPrivate`,
`QPageDialogPrivate` (`init()` connects the page widget's signals and calls
`setMainWidget`), `QPageViewPrivate` (+ `QPageStackedWidget`),
`QPageWidgetPrivate` (in `src/gui/qpagewidget_p.h`, one level up from the rest),
`QPageModelPrivate`, `QPageWidgetModelPrivate` with `PageItem`, and in namespace
`KDEPrivate` the navigator views `QPagePlainView`, `QPageListView`,
`QPageTreeView`, `QPageTabbedView` with `QPageListViewDelegate`,
`QPageListViewProxy` and `SelectionModel`. The d-pointer pattern is Qt's
(`Q_DECLARE_PRIVATE`); `Q_PRIVATE_SLOT`s were replaced by forwarding slots for
Qt6.

```
QPageDialog ──owns──> QPageWidget ──is-a──> QPageView ──shows──> QPageWidgetModel
     │                                          │                     └─ QPageWidgetItem (widget,name,icon…)
     └─is-a─> QExtendDialog (buttons, mainWidget, KLusterSeparator)
                                                └─ createView(): KDEPrivate::QPage{Plain,List,Tree,Tabbed}View
```

`FaceType::Auto` picks `Tree` if any page has sub-pages, `List` if there is more
than one page, else `Plain` (`QPageViewPrivate::detectAutoFace`).

Behaviour to know:

- `Ok` emits `okClicked()` then `accept()`; `Apply`, `Default`, `Help`,
  `Reset`, `Try` and `User1-3` only emit their signal; `Cancel` → `reject()`;
  `Close` → `done(Close)`; `Yes`/`No` → `done(Yes/No)`.
- `setHelp(anchor, app)` stores the values but the built-in help launch is
  commented out; apps connect `helpClicked` to their own `QHelpViewer`.
- The page header text (`QPageWidgetItem::setHeader`) is not displayed:
  `QPageView::showPageHeader()` returns `false` and the KDE title widget is
  commented out.
- `restoreDialogSize()`/`saveDialogSize()` use the default `QSettings` with keys
  `"Width <screenW>"`/`"Height <screenH>"`, not scoped per dialog.

### 5.2 How the apps build their Preferences dialog

Klusters (`src/klusters/src/prefdialog.{h,cpp}`) and NeuroScope
(`src/neuroscope/src/prefdialog.{h,cpp}`) follow the same pattern:

```c++
PrefDialog::PrefDialog(QWidget* parent, …) : QPageDialog(parent) {
    setButtons(Help | Default | Ok | Apply | Cancel);
    setDefaultButton(Ok);
    setFaceType(List);
    setHelp("settings", "klusters");                 // stored, not used
    prefDisplay = new PrefDisplay(this);              // a .ui-backed QWidget
    auto* item = new QPageWidgetItem(prefDisplay, tr("Display"));
    item->setHeader(tr("Display Configuration"));
    item->setIcon(QIcon(":/icons/display"));         // app .qrc or :/shared-icons/…
    addPage(item);
    // … more pages, incl. the shared PrefInput(configuration(), this)
    connect(prefDisplay->backgroundColorButton, &QColorButton::colorChanged,
            this, &PrefDialog::enableApply);          // every editable widget
    connect(this, &QExtendDialog::applyClicked,   this, &PrefDialog::slotApply);
    connect(this, &QExtendDialog::defaultClicked, this, &PrefDialog::slotDefault);
    connect(this, &QExtendDialog::helpClicked,    this, &PrefDialog::slotHelp);
}
```

- `updateDialog()` copies the app `Configuration` into the widgets.
- `updateConfiguration()` copies widgets back into `Configuration` and calls
  `enableButtonApply(false)`.
- `enableApply()` → `enableButtonApply(true)` and sets `applyEnable`.
- `slotApply()` → `updateConfiguration()`, `emit settingsChanged()`.
- `slotDefault()` asks for confirmation, then resets every page.
- The app creates the dialog on demand, connects `settingsChanged` to its
  apply function, calls `updateDialog()`, then `exec()`; on `Accepted` with
  `isApplyEnable()` it calls `updateConfiguration()` and applies
  (`KlustersApp` around `klusters.cpp:1814-1838`).

### 5.3 Recipe: add a Preferences page

1. Create the page widget (usually a Designer `.ui` plus a thin `QWidget`
   subclass, as the `Pref*` classes do). Add it to the app's `CMakeLists.txt`
   (`klusters_UI` / sources).
2. In the `PrefDialog` constructor: construct it with the dialog as parent,
   wrap it in a `QPageWidgetItem`, set name and icon, `addPage(item)`
   (`addSubPage(parent, …)` for a child page; with `setFaceType(List)` sub-pages
   are not shown as a tree — use `Tree` or `Auto`).
3. Connect every editable widget's change signal to `enableApply`.
4. Extend `updateDialog()`, `updateConfiguration()` and `slotDefault()` for the
   new fields, and add the fields to the app's `Configuration` (and its
   persistence).
5. If the page needs live effects, act on them in the app's apply function
   connected to `settingsChanged`.

---

## 6. Other GUI widgets

| Class (file) | What it is | Used by |
|---|---|---|
| `DockArea` (`gui/dockarea.*`) | `QScrollArea` hosting `QDockWidget`s by name; see [VIEW_SYSTEM.md §3](../../klusters/docs/VIEW_SYSTEM.md#3-klustersview--the-per-document-container--and-dockarea) | klusters, neuroscope |
| `QExtendTabWidget` (`gui/qextendtabwidget.*`) | `QTabWidget` whose tabs are `DockArea`s: `addDockArea(label)`, `addDockArea(area, label)`, `currentDockArea()` (a `static_cast`, so every tab must be a `DockArea`). No `Q_OBJECT`. | klusters, neuroscope |
| `QColorButton` (`gui/qcolorbutton.*`) | push button showing a colour; click opens `QColorDialog`; `color` property, `colorChanged(QColor)` | promoted widget in `prefdisplaylayout.ui` (klusters) and `prefgenerallayout.ui` (neuroscope) |
| `QHelpViewer` (`gui/qhelpviewer.*`) | `QDialog` + `QTextBrowser` handbook viewer. `setHtml(file, anchor)` loads the file and sets the search path to its directory; relative and `file:` links navigate inside the viewer, other schemes go to `QDesktopServices`. Palette pinned to white/black because the handbook HTML is authored light. | all three apps |
| `QRecentFileAction` (`gui/qrecentfileaction.*`) | `QAction` with a submenu of recent files; `addRecentFile`, `removeRecentFile`, `setRecentFiles`, `recentFiles()`, `maximumFileCount` (default 10), signals `recentFileSelected(QString)`, `recentFileCleared`, `recentFileListChanged`. `save()` only refreshes and emits; persisting `recentFiles()` is the app's job. | all three apps |
| `KlusterRubberBand` (`gui/klusterrubberband.*`) | `QRubberBand` painting a 1-px white rectangle at 0.9 opacity | klusters, neuroscope |
| `QPageWidgetPrivate` (`gui/qpagewidget_p.h`) | private of `QPageWidget` (§5.1); misplaced outside `page/` | library only |

---

## 7. `src/klustersshared` utilities

### 7.1 `sdiff_pairs.h` — custom spatial-difference patterns

Header-only, Qt-free, not installed and not part of the `.so`. Consumers put
`src/libklustersshared/src/klustersshared` on their include path:
`process_extractspikes_stderiv`, `process_reextractspikes_stderiv`,
`process_alignspikes`, `process_alignspikes_pca` (ndmanager-plugins) and
Klusters (`klustersdoc_internal.h`, `klustersdoc_realign.cpp`).

It parses a spike group's `sdiffPairs` value (session YAML
`spikeDetection/channelGroups/group/sdiffPairs`, engine flag `-P`). Positions
are 0-based within the group.

| Function | Grammar | Result |
|---|---|---|
| `parseSdiffPairs(spec, ok=nullptr)` | `a-b,c-d,…` (order 4, `SDIFF_CUSTOM`) | `partner[a] = b`: output `a` = `x[a] − x[b]` |
| `parseSdiffSets(spec, ok=nullptr)` | `a-b+c+d,e-f,…` (order 5, `SDIFF_CUSTOM_CAR`) | `set[a] = {b,c,d}`: output `a` = `x[a] − mean(x[set])` |
| `sdiffSpecUsesSets(spec)` | — | true if `+` appears anywhere in `spec` |
| `parseSdiffPairsPerGroup(spec)`, `parseSdiffSetsPerGroup(spec)` | `g1:g2:…` | one map per colon-separated group (aligned with `-c`); an empty segment yields an empty map (group keeps its base order) |

Validation rules enforced by the code:

| Order 4 (`parseSdiffPairs`) | Order 5 (`parseSdiffSets`) |
|---|---|
| every token has a `-`; `a ≥ 0`, `b ≥ 0`, `a ≠ b` | every token has a `-`; every target non-empty, `≥ 0`, `≠ a` |
| no source listed twice | no source listed twice |
| exactly one position with no target (the root), and it is the highest position | every position `0…max` has a non-empty set; no root rule |
| the root has at least one predecessor; the root is then wired to that predecessor so its (redundant) output is the last channel, which `SDIFF_PASS` drops | — |

Error mode: with `ok == nullptr` (the CLI engines) a bad pattern prints to
stderr and calls `exit(1)`; with a non-null `ok` (Klusters) it sets
`*ok = false` and returns an empty map. The `PerGroup` helpers always use the
exiting mode.

Cross-check with
[spike-detection.md](../../../doc/ndmanager-plugins/commands/spike-detection.md#custom-difference-patterns-and-alignment)
and the engines:

- The two grammars and their formulas match the doc. The doc does not state the
  order-4 constraints (single root, root = last position).
- Order is decided for the whole spec, not per group: the bash front end tests
  the colon-joined list for `+` (`scripts/ndm_functions:1697`, `:1718`), and
  `process_extractspikes_stderiv` (`-P`) and both aligners call
  `sdiffSpecUsesSets` on the whole argument. A session that mixes an order-4
  group with an order-5 group is therefore parsed entirely as sets and rejected.
- `process_extractspikes_stderiv` and the aligners accept both orders;
  `process_reextractspikes_stderiv` only calls `parseSdiffPairsPerGroup`
  (`process_reextractspikes_stderiv.cpp:290`). See §9 for what that means.

### 7.2 Theme — `theme.h`

Namespace `neurosuite`. `enum class Theme { System, Light, Dark }`.

| Function | Behaviour |
|---|---|
| `initThemeFromSettings()` | call once after `QApplication`: samples the OS appearance, applies the saved preference, and on Qt ≥ 6.5 re-applies when the OS colour scheme changes while the preference is `System` |
| `applyTheme(Theme)` | sets the Fusion style and a built-in light or dark `QPalette` (incl. disabled colours); records `currentTheme()` |
| `loadThemePreference()` / `saveThemePreference(Theme)` | `QSettings("neurosuite", "appearance")`, key `theme`, tokens `system`/`light`/`dark` — one value for the whole suite |
| `systemIsDark()`, `isDark(Theme)` | Qt ≥ 6.5: `QStyleHints::colorScheme()`; older: luminance of the startup window colour |
| `themeToString`, `themeFromString`, `themeDisplayName` | token conversion (unknown → `System`), translated labels |

Callers: each app's `main.cpp` calls `initThemeFromSettings()`; Klusters'
and NeuroScope's Preferences dialogs (`prefdialog.cpp`) and ndmanager's
**Settings ▸ Theme** submenu (`ndmanager.cpp`, `setupActions()`) call
`saveThemePreference` and `applyTheme`.

### 7.3 Colour containers

| Class | Purpose | Used by |
|---|---|---|
| `ItemColors` (`itemcolors.*`) | ordered list of `{itemId, color, label, isChanged}` for clusters/events; lookup `BY_INDEX` or `BY_ITEM_NUMBER`; change tracking (`colorChangedItemList`, `resetAllColorStatus`); `sortByItemId` | klusters, neuroscope |
| `ChannelColors` (`channelcolors.*`) | ordered list of `{channelId, color, groupColor, spikeGroupColor}` | klusters, neuroscope, YAML layer |
| `ChannelColorEntry` (`channelcolorentry.h`) | value type with the same three colours plus id, used by the YAML reader/writer; ndmanager aliases its own `ChannelColors` to it (`src/ndmanager/src/channelcolors.h:22`) | ndmanager, neuroscope |

Both list classes own their entries through raw pointers (`QList<T*>`) and
define a deep-copy constructor and a deep-copy `operator=` (§9 item 25,
`klustersshared_colors_test`).

### 7.4 Small types and helpers

| Header | Content |
|---|---|
| `array.h` | header-only `Array<T>` (row × column, `unique_ptr<T[]>`, **1-based** `operator()(i,j)`, 0-based `operator[]`, `copySubset`, `copyAndPrependColumn`, `fillWithZeros`, `data()`) for trivially copyable `T`; `pArray<T>` adds element-wise copy for non-trivial `T`. Bounds are checked only by `Q_ASSERT_X`. |
| `types.h` | `dataType = long`, `recordingUnitType = long long` |
| `customtypes.h` | Neuralynx types (`Data`, `Time`, `Frequency`, `Coordinates`) and NCS/NVT/NEV/SMI constants; used by `process_nlxconvert` only |
| `utilities.h` | `Utilities::getNbLines(path)` (used by NeuroScope's `positionsprovider.cpp`), `createBackup(path)` (writes `path~`), `compareVersion(old, new)` (no callers) |
| `zoomwindow.h` | `ZoomWindow`: float window over a `QRect` with `zoom(factor, center)`, `zoom(p1, p2)`, `setVisible`, `reset`; scale clamped to `[MIN_SCALE = 1, MAX_SCALE = 22.73]` (klusters, neuroscope) |
| `progressbar.h` | terminal `ProgressBar` and defrag-style `BlockProgress` for the CLI plugins (draw to `/dev/tty`, else stderr, else 5 % milestone lines on stdout). POSIX-only. Not exported: plugins compile `progressbar.cpp` themselves |
| `generalinformation.h`, `fileinformation.h`, `programinformation.h`, `neuroscopevideoinfo.h` | header-only value types for the YAML layer — see [the YAML README](../../../doc/libklustersshared/README.md#shared-data-types) |

---

## 8. Tests

### 8.1 What exists

Registered in `src/libklustersshared/CMakeLists.txt` under `if(NS_BUILD_TESTS)`:

| ctest name | Source | Covers | Environment |
|---|---|---|---|
| `klustersshared_theme_test` | `test/theme_test.cpp` | token round-trip, light/dark palettes (luminance and contrast of window, text, disabled, highlight), `currentTheme`, preference save/load | `QT_QPA_PLATFORM=offscreen`, `XDG_CONFIG_HOME=<build>/themecfg` so the user's settings are not touched |
| `klustersshared_yamlmodifier_test` | `test/yamlmodifier_test.cpp` | `ParameterYamlModifier` round trip as NeuroScope's save does it: unknown and tool-owned keys (`sdiffPairs`, `probeId`, `shankIndex`, `channelMapping`, …) survive, keys follow channels not positions, trash group not written, per-channel keys and `files:` entries kept | none |
| `klustersshared_yamlwriter_test` | `test/yamlwriter_test.cpp` | `ParameterYamlWriter` over a base document as ndmanager saves: per-group keys, extra acquisition keys and unmodelled blocks (`sourceRecording`) survive | none |
| `klustersshared_yamlformat_test` | `test/yamlformat_test.cpp` + `yamlformat.cpp` | `emitWithLayout` keeps comments, inline lists, quoting and spacing; round trips over the repository's templates | the repository root as argument |
| `klustersshared_sessiontemplate_test` | `test/sessiontemplate_test.cpp` + `sessiontemplate.cpp` | session-template sections, export and apply | none |
| `klustersshared_colors_test` | `test/colors_test.cpp` | `ItemColors` / `ChannelColors` copies (construction and assignment) are deep | none |
| `klustersshared_sdiff_pairs_test` | `test/sdiff_pairs_test.cpp` | the `sdiffPairs` grammar (`sdiff_pairs.h`): trees, sets, strict positions; malformed patterns rejected | the shared grammar vectors in `libneurosuite-core/test/` |
| `klustersshared_unitsidecar_test` | `test/unitsidecar_test.cpp` | the per-clustering cluster-information file (`unitsidecar.h`) | none |
| `klustersshared_xcorr_dispatch_test` | `test/xcorr_dispatch_test.cpp` + dispatcher and OpenMP kernel | `XcorrDispatch` under concurrent calls with a fake CUDA back end: one probe, atomic demotion, wide shifts on OpenMP | none (no GPU needed) |
| `klustersshared_realign_kernels_test` | `test/realign_kernels_test.cpp` + OpenMP kernel and `realign_center.cpp` | zero-energy spikes and templates, lag ties, `perSpikeCentroidShifts` | none |

Each is a standalone program with its own `main()` and a
small check helper that reports failures and returns non-zero if any check
failed.

`test/main.cpp`, `listview.*`, `mainview.*`, `scrollarea.*`, `itemgroupview.*`
and `test/CMakeLists.txt` are a legacy `test_scrollview` demo. No CMake file
adds that directory, so it is never built.

Library code tested from elsewhere:

| ctest name | Library code exercised |
|---|---|
| `klusters_test_bindingregistry`, `klusters_test_inputdispatcher`, `klusters_test_keymapprofile` (`src/klusters/test/`) | `src/input/` (compiled in with `KLUSTERSSHARED_STATICLIB`) |
| `kk_test_klusters_realign` (`src/kiloklustakwik/test/`) | `realign_xcorr_omp.cpp`, `realign_xcorr_dispatch.cpp`, `realign_center.cpp` through `klusters_realign.cpp` |
| `build_invariants` (`tests/`) | `cmake_minimum_required` of `src/libklustersshared/CMakeLists.txt` |

Not covered by any test: the real GPU xcorr back ends, the page framework and
widgets, `Array`, `Utilities`, `ZoomWindow`, `ProgressBar`.

### 8.2 Running

```sh
cmake -B build -DNS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build -R klustersshared --output-on-failure   # this library
ctest --test-dir build --output-on-failure                     # everything
```

See [BUILD.md §6](../../../doc/BUILD.md#6-tests).

### 8.3 Recipe: add a test

1. Create `test/<topic>_test.cpp` with its own `main()` and a check helper
   copied from an existing test; return `fails ? 1 : 0`.
2. In `src/libklustersshared/CMakeLists.txt`, inside `if(NS_BUILD_TESTS)`:

   ```cmake
   add_executable(klustersshared_<topic>_test test/<topic>_test.cpp)
   target_link_libraries(klustersshared_<topic>_test PRIVATE Neurosuite::qt)
   add_test(NAME klustersshared_<topic>_test COMMAND klustersshared_<topic>_test)
   ```

3. If the test creates a `QApplication`, set
   `QT_QPA_PLATFORM=offscreen`; if it touches `QSettings`, point
   `XDG_CONFIG_HOME` into the build tree (copy the `set_tests_properties`
   block of the theme test).
4. Linking `Neurosuite::qt` reaches only `KLUSTERSSHARED_EXPORT` symbols. For
   non-exported code compile the source into the test instead (with
   `KLUSTERSSHARED_STATICLIB`, as the Klusters input tests do). Header-only
   Qt-free code (`sdiff_pairs.h`) needs only
   `target_include_directories(... PRIVATE src/klustersshared)`; for a
   non-exiting test call the parser with a non-null `ok`.

---

## 9. Known issues

Found while writing this page. Items marked **Fixed** have since been
corrected; the text describes the problem as it was.

**Build and packaging**

1. **Fixed (0492):** `src/input/*.h` install to `include/klustersshared/input/`,
   so `#include "input/…"` resolves on the existing install interface.
   Was: `src/input/*.h` are not in `_hdrs`, so they are not installed, and the
   install interface does not expose an `input/` path. Install-tree consumers
   cannot compile the `#include "input/…"` lines that Klusters and NeuroScope
   use.
2. **Fixed (0492):** `find_package(LibNeurosuiteCore)` when the target does
   not already exist.
   Was: `src/libklustersshared/CMakeLists.txt` links `Neurosuite::core` (via
   `src/CMakeLists.txt`) but never calls `find_package(LibNeurosuiteCore)`, so
   the library configures only inside the monorepo, not standalone.
3. `src/HTML/en/common/` (KDE handbook chrome) and `src/images/note.gif` are
   referenced by no CMake file or `.qrc`.
4. `cmake/modules/*.cmake` are referenced by no CMake file.
5. `depends` starts with `#/bin/bash` (no `!`) and looks for
   `build/src/<dir>.so`, but the library is `libneurosuite-qt.so`.
6. `ProgressBar`/`BlockProgress` are compiled into the `.so` but not exported
   (unreachable there), and `progressbar.cpp` includes `<unistd.h>` and
   `<sys/ioctl.h>`, which ties the Qt library to POSIX.
7. `KLusterSeparator` (spelled with a capital L) is neither exported nor
   installed; `gui/qpagewidget_p.h` sits outside `gui/page/`.
8. `customtypes.h` uses `int16_t`/`int64_t` but includes only `<stdlib.h>`;
   its `NVT_SHIFT_Y` comment says `(2^12-1)*2^16` for the value 65536.
9. Klusters hard-codes `CUDA_ARCHITECTURES "75;86;89;120"`
   (`src/klusters/src/CMakeLists.txt:177`), bypassing
   `ns_gpu_select_cuda_arch()` ("native") and `NS_GPU_CUDA_ARCH_FALLBACK`
   (which also has 100).

**xcorr**

10. **Fixed (0491):** the back end is probed once through a function-local
    static and held in a `std::atomic`; demotion is a compare-exchange.
    Test: `klustersshared_xcorr_dispatch_test`.
    Was: `XcorrDispatch` caches its back end in unsynchronised statics
    (`realign_xcorr_dispatch.cpp:67-68`, writes at `:101`, `:139`, `:147`,
    `:155`) while KiloKlustaKwik calls `compute` from inside
    `#pragma omp parallel for` (`KK_templatematch.cpp:314`). That is a data race
    on first detection and on GPU demotion.
11. **Partly fixed (0491):** a `maxShift` beyond the CUDA/HIP block limit now
    runs that call on OpenMP without demoting; a real GPU failure still demotes
    for the rest of the process.  The OpenMP kernel also indexed out of bounds
    when `maxShift` exceeded `nSamples` (negative modulo); fixed.
    Was: A single GPU failure demotes the whole process to OpenMP for good, and
    `maxShift > 511` is such a failure on CUDA/HIP, so one large-shift call
    silently turns off the GPU for every later call.
12. `realign_xcorr.h` calls the layout "interleaved samples"; it is
    channel-major per spike (`[spike][channel][sample]`), the transpose of the
    interleaved `.spk` layout.
13. **Partly fixed (0491):** the two `realign_xcorr_omp.cpp` comments.
    Was: Stale comments in `realign_xcorr_omp.cpp`: "matches the zero-padded
    numerator sum range" (the sum is circular) and "Negate so the caller can
    simply add…" (nothing is negated). `realign_xcorr_cuda.cu` still says
    "One block, one thread per spike (up to 1024 spikes per call chunk)".
14. **Partly fixed (0491):** the dispatcher header lists its real consumers.
    Was: The consumer list is stale in three places: `realign_xcorr_dispatch.cpp`
    names "KK.cpp (KlustaKwik)" and "klustersdoc.cpp"; `src/CMakeLists.txt` names
    "klustakwik, kiloklustakwik, klusters" (there is no klustakwik);
    `process_alignspikes_pca.cpp:84` suggests calling `XcorrDispatch::compute`
    and "rebuild against libklustersshared", but the dispatcher is not in the
    library.
15. Back ends differ in precision: SYCL accumulates in `float`, OMP/CUDA/HIP in
    `double` (CUDA/HIP store the spike norm as `float`). Scores can differ in
    the last digits between back ends.
16. HIP ignores the return of `hipDeviceSynchronize()`
    (`realign_xcorr_hip.hip:192`); CUDA and HIP ignore every
    `cudaMemcpy`/`hipMemcpy` return.
17. `xcorr_omp_available()` is defined but not declared in the header and has
    no caller. Klusters forward-declares `XcorrDispatch` in
    `klustersdoc_realign.cpp` and `spikerealigndialog.cpp` although it already
    includes the header that declares it.
18. `realign_center.cpp:51` uses `M_PI` (not standard C++; MSVC needs
    `_USE_MATH_DEFINES`) while the other function in the same file uses a
    literal.

**sdiff patterns**

19. **Fixed (0490):** the parser follows every channel to the root and
    rejects a cycle.
    Was: `parseSdiffPairs` checks for a single root but not for cycles or
    connectivity: `0-1,1-0,2-3` is accepted (`ok = true`, partner
    `[1,0,3,2]`) although it is not a spanning tree.
20. **Fixed (0490):** positions must be plain non-negative integers.
    Was: Positions are read with `atoi`, so junk is accepted: `x-1` is read as
    `0-1`, and an order-5 token `0-1+2` given to `parseSdiffPairs` is read as
    `0-1`.
21. **Fixed (0490):** the engine dispatches on `+` like the extractor and
    handles order 5; `ndm_reextractspikes` passes `-P`; `-d 4`/`-d 5` without
    `-P` is an error.
    Was: Because of 20, `process_reextractspikes_stderiv`, which always calls
    `parseSdiffPairsPerGroup` (`:290`), would silently misread an order-5
    pattern. In practice `ndm_reextractspikes` never passes `-P`; for a
    `stderiv_C4`/`_C5` method it passes `-d 4`/`-d 5`
    (`scripts/ndm_reextractspikes:313`), so the engine has no partner map and
    falls back to the all-pairs derivative (order 4 falls through, order 5 hits
    `default`). Re-extraction then runs in a different spatial space from the
    session's `.spk`.
22. **Fixed (0490):** repeated members are rejected.
    Was: `parseSdiffSets` accepts a repeated target (`0-1+1` gives `{1,1}`, which
    double-weights it).
23. **Fixed (0490):** header comment and `spike-detection.md` updated.
    Was: The `sdiff_pairs.h` header comment says only the two extractors use it and
    that malformed input always exits; four tools and Klusters use it and the
    `ok` mode does not exit. `spike-detection.md` omits the order-4 root rules
    and does not say that one `+` anywhere makes every group order 5.
24. **Fixed (0490):** fixture is now `0-1,1-2,2-3`.
    Was: The test fixture in `yamlmodifier_test.cpp` uses `sdiffPairs:
    "0-1,1-2,2-3,3-0"`, which the parser would reject (no root). The test only
    checks that the string survives, so it passes, but it is not a valid
    example.

**Found by the 2026-10-15 audit (all fixed)**

34. **Fixed (0517):** an ndmanager save rebuilt the YAML from its model and
    dropped every key it does not model (per-group `probeId`, `shankIndex`,
    `sitePositions_um`, `klustakwik:`, `nCells`, extra acquisition fields,
    top-level blocks). `ParameterYamlWriter::setBaseDocument(path)` now starts
    from the file the document was loaded from and writes ndmanager's values
    over it; groups are matched with the now-public `matchExistingGroups`.
    Test: `klustersshared_yamlwriter_test`.
35. **Fixed (0518):** per-group integers (`nSamples`, `peakSampleIndex`,
    `nFeatures`, `probeId`, `shankIndex`) written as `42.0` read as 0, and
    ndmanager then saved the 0. They share `acquisitionInt`'s tolerant read.
36. **Fixed (0519):** a group whose `channels` is null or missing was skipped
    and the groups after it renumbered on save; it is kept as an empty group.
37. **Fixed (0520):** probe entries: `ndm_setupgroups` writes `probeId`, the
    C++ reader read only `id`. All readers now take `probeId`, then `id`,
    then the list position; the writer writes `probeId`.
38. **Fixed (0523):** every xcorr back end returned shift `-maxShift` for a
    zero-energy spike or template when `minScore` was 0 (KiloKlustaKwik
    always passes 0); an undefined correlation now gives shift 0, score 0.
39. **Fixed (0524):** the CUDA/HIP reduction did not keep the lowest lag on
    ties as its comment claimed; it does now, matching OMP/SYCL.
40. **Fixed (0525):** `perSpikeCentroidShifts` rolled a zero-energy spike by
    about N/2 and let it bias the target. Test:
    `klustersshared_realign_kernels_test`.
41. **Fixed (0526):** `chordFromKeySequence` took the first combination of a
    multi-key sequence although its header says that is invalid; the
    Preferences key editor now records one combination (Qt ≥ 6.5 via
    `setMaximumSequenceLength`, older Qt by truncating).

42. **Fixed (0528):** every save (ndmanager's `ParameterYamlWriter`,
    NeuroScope/Klusters' `ParameterYamlModifier`) re-emitted the file through
    a block-style yaml-cpp emitter: comments were dropped, inline lists
    expanded and quotes removed (`'false'` became the boolean `false` for
    PyYAML). Both now write through `yamlformat::emitWithLayout`
    (`klustersshared/yamlformat.{h,cpp}`, Qt-free), which keeps comments,
    flow style, quoting and spacing from the file that was loaded. Test:
    `klustersshared_yamlformat_test` (round trips over the repository's
    templates; the 3596-line `templates/template.yaml` comes back identical
    apart from one multi-line quoted string written as a literal block).

**Widgets and utilities**

25. **Fixed (0493):** both have a deep-copy `operator=` (copy-and-swap), and
    `ChannelColors`' copy constructor copies every channel — it copied the
    first one N times. Test: `klustersshared_colors_test`.
    Was: `ItemColors` and `ChannelColors` own raw pointers and define a copy
    constructor but no copy assignment; an assignment copies the pointers and
    both objects delete them.
26. `Utilities::compareVersion` compares components as strings (`"10" < "9"`)
    and returns `true` for a lower version whose later component is higher
    (`old 2.0`, `new 1.5`). It has no callers. `getNbLines` carries a stale
    comment about quoting the path, and `createBackup` ignores open failures.
27. `QPageWidgetItem::setHeader` has no visible effect (`showPageHeader()`
    returns `false`); both apps set headers on every page.
28. `QExtendDialog::setHelp` stores the anchor but the help launch is commented
    out; `saveDialogSize()`/`restoreDialogSize()` use global `"Width N"`/
    `"Height N"` keys shared by every dialog.
29. `QRecentFileAction` is declared between `QT_BEGIN_NAMESPACE` and
    `QT_END_NAMESPACE`, which would put it in Qt's namespace in a namespaced Qt
    build.
30. `progressbar.h` has two "Visual style" paragraphs; the second (block
    characters, `#`/`.` fallback) describes `BlockProgress`, not `ProgressBar`,
    which draws `━`/`─` and falls back to `=`/`-`.
31. Stale header text: `zoomwindow.h` is titled `floatrect.h` with guard
    `FLOATRECT_H`; `klusterseparator.h` ends with `// KSEPARATOR_H`;
    `qextenddialog.h` keeps `TODO KDE5` notes; `array.h` says "modernized for
    C++17" while the library builds as C++20 and guards `ARRAY_H` only after
    its first `#include`.

**Other documentation**

32. **Fixed:** the README's dependencies and build order now match the build.
    Was: [doc/libklustersshared/README.md](../../../doc/libklustersshared/README.md)
    lists Qt6 Xml as a dependency (not required), omits libneurosuite-core and
    neuroscope from "Build order", and says the library "must be installed
    before" the apps, which the monorepo build does not require.
33. [ARCHITECTURE.md §2.2](../../../doc/dev/ARCHITECTURE.md#22-per-layer-detail)
    places `neurofileio` at `klustersshared/neurofileio.{h,cpp}`; it is in
    `src/libneurosuite-core/src/neurosuite/core/`.
