# libneurosuite-core — developer reference (`neurosuite/core/*`)

*Part of the [Developer Guide](../../../doc/dev/DEVELOPER_GUIDE.md). Sibling of [FILE_FORMATS.md](FILE_FORMATS.md), which documents the on-disk formats (`neurofileio`) and the file-naming policy (`custody.hpp`) in depth; this page documents everything else in the library and how it is built, consumed and tested.*

The code is the authority. Every statement below was read out of the headers in `src/libneurosuite-core/src/neurosuite/core/`, the CMake files, and the consumers that include them. Where a comment, a test description or another document disagrees with the code, the disagreement is listed in [§5 Known issues](#5-known-issues).

**Contents:** [1. What the library is](#1-what-the-library-is) · [2. Conventions shared by all headers](#2-conventions-shared-by-all-headers) · [3. Header reference](#3-header-reference) · [4. Tests](#4-tests) · [5. Known issues](#5-known-issues)

---

## 1. What the library is

### 1.1 Purpose

`libneurosuite-core` holds the program-agnostic NeuroSuite primitives that need neither Qt nor yaml-cpp, so that the Qt apps (Klusters, NeuroScope), the standalone `ndmanager-plugins` binaries and KiloKlustaKwik can share one implementation instead of hand-maintained copies. It started as the new home of `neurofileio` (moved out of `libklustersshared`) and has since collected:

| Area | Headers |
|---|---|
| On-disk formats and naming | `neurofileio.h/.cpp`, `custody.hpp` |
| Waveform transforms and PCA | `stderiv_transform.hpp`, `pca_projection.hpp`, `spike_extract.hpp` |
| Collision decomposition | `decollide.hpp`, `decollide_eap.hpp` |
| Template classes and the template library | `templateclass.hpp`, `template_generate.hpp`, `running_stats.hpp`, `drift_partition.hpp`, `projectionscope.hpp` |

### 1.2 Place in the layering

[ARCHITECTURE.md §2](../../../doc/dev/ARCHITECTURE.md#2-target-architecture) describes a target split into `neuro-io` / `neuro-core` / `neuro-compute` (all Qt-free) under `neuro-ui` (Qt) and the apps, with the [dependency rules in §2.1](../../../doc/dev/ARCHITECTURE.md#21-dependency-rules-must-not-be-violated): the three lower layers must never depend on Qt or on `neuro-ui`, and `neuro-io`/`neuro-core` must not depend on a GPU runtime.

`libneurosuite-core` is today the only realised Qt-free library and spans all three lower layers in one target:

- `neuro-io`: `neurofileio`, `custody.hpp`;
- `neuro-core`: `templateclass.hpp`, `drift_partition.hpp`, `projectionscope.hpp`, `running_stats.hpp`;
- `neuro-compute` (CPU only): `decollide*.hpp`, `template_generate.hpp`, `pca_projection.hpp`, `stderiv_transform.hpp`, `spike_extract.hpp`.

It satisfies the rules that matter for it: it depends on the C++ standard library only (no Qt, no yaml-cpp, no GPU runtime, no other in-repo library). The rule to keep: **nothing under `neurosuite/core/` may include a Qt, yaml-cpp, CUDA/HIP/SYCL header or a header from another subproject.** Qt callers convert at the boundary (`QString::toStdString()`); GPU mirrors (for example `src/klusters/src/pca_refine_cuda.cu` for the PCA energy) live in the consuming app.

Build order (root `CMakeLists.txt`, see [BUILD.md §5](../../../doc/BUILD.md#5-the-build-graph-and-skip-flags)): `nphys-data` → **`libneurosuite-core`** → `libklustersshared` (which PUBLIC-links `Neurosuite::core`) → klusters / neuroscope / ndmanager → ndmanager-plugins → kiloklustakwik. Skip it with `-DNS_SKIP_LIBNEUROSUITE_CORE=ON` (which breaks every consumer that links it).

### 1.3 The CMake target and package

| Item | Value | Where |
|---|---|---|
| Project | `libneurosuite-core` VERSION 2.1.0, `LANGUAGES CXX` | `src/libneurosuite-core/CMakeLists.txt` |
| Library target | `neurosuite-core`, `SHARED`, alias **`Neurosuite::core`** | `src/libneurosuite-core/src/CMakeLists.txt` |
| Compiled sources | `neurosuite/core/neurofileio.cpp` only | same |
| SOVERSION | major version (`2`) | same |
| Package | `find_package(LibNeurosuiteCore)` → imported `Neurosuite::core` (`EXPORT_NAME core`); version file `SameMajorVersion` | `LibNeurosuiteCoreConfig.cmake.in` |
| Export set | `LibNeurosuiteCoreTargets`, namespace `Neurosuite::`; the monorepo root also `export()`s it into the build tree and sets `LibNeurosuiteCore_DIR` | root `CMakeLists.txt` |
| Include dir | `src/libneurosuite-core/src` (build), `${CMAKE_INSTALL_INCLUDEDIR}` (install), PUBLIC | `src/CMakeLists.txt` |
| Installed headers | every `*.h`/`*.hpp` in `neurosuite/core/` → `include/neurosuite/core/` | `install(DIRECTORY …)` in `src/CMakeLists.txt` |
| C++ standard | `CMAKE_CXX_STANDARD 20`, required, no extensions — set for the library directory only; **not** propagated to consumers as a compile feature | `src/libneurosuite-core/CMakeLists.txt` |
| Visibility | `CMAKE_CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN ON` | same |
| Link hardening (GCC/Clang, non-Apple) | `-Wl,--fatal-warnings -Wl,--no-undefined` | same |
| Dependencies | none beyond the standard library (`<filesystem>` is used by `custody.hpp`) | — |

The headers themselves are written to compile as C++17: the plugins build with `cxx_std_17` and include them, and `pca_projection.hpp` states that constraint explicitly. Do not use C++20-only features in any header.

**Export macro.** `neurosuite_core_export.h` defines `NEUROSUITE_CORE_EXPORT` without any Qt dependency:

| Condition | Expansion |
|---|---|
| `NEUROSUITE_CORE_STATIC` defined | empty (for consumers that compile `neurofileio.cpp` into themselves) |
| Windows/Cygwin, `NEUROSUITE_CORE_BUILD_LIB` defined | `__declspec(dllexport)` |
| Windows/Cygwin, not defined | `__declspec(dllimport)` |
| elsewhere | `__attribute__((visibility("default")))` |

`NEUROSUITE_CORE_BUILD_LIB` is a PRIVATE definition of the library target only. Because of the hidden-visibility preset, every **out-of-line** function in `neurofileio.h` must carry `NEUROSUITE_CORE_EXPORT`, or consumers fail to link. Header-only code (`inline` functions and templates) needs no macro.

### 1.4 How a component consumes it

All consumers include with the full prefix, either form of quotes:

```cpp
#include <neurosuite/core/custody.hpp>
#include "neurosuite/core/neurofileio.h"
```

Three consumption modes exist in the tree:

| Mode | Who | How |
|---|---|---|
| Link the shared library | `libklustersshared` (PUBLIC), klusters, `process_initeap`, `process_decomposecollisions` (via its `_pdc_flags` INTERFACE target), klusters' store tests | `target_link_libraries(<t> PRIVATE Neurosuite::core)` — gives the include path and `libneurosuite-core.so` |
| Include only (header-only parts) | every other `ndmanager-plugins` binary | `src/ndmanager-plugins/src/CMakeLists.txt` adds `include_directories(${CMAKE_SOURCE_DIR}/src/libneurosuite-core/src)`; nothing is linked |
| Source-include | kiloklustakwik | compiles `../libneurosuite-core/src/neurosuite/core/neurofileio.cpp` into each executable and defines `NEUROSUITE_CORE_STATIC` (see `src/kiloklustakwik/CMakeLists.txt`) |

**Which headers need the library linked.** Only `neurofileio.cpp` is compiled, so a translation unit needs `Neurosuite::core` (or the source-include) exactly when it ODR-uses an out-of-line `neurofileio` function:

| Header | Include-only safe? |
|---|---|
| `custody.hpp`, `pca_projection.hpp`, `stderiv_transform.hpp`, `spike_extract.hpp`, `decollide.hpp`, `running_stats.hpp` | yes — std only |
| `templateclass.hpp`, `projectionscope.hpp`, `drift_partition.hpp` | yes — they use only `neurofileio`'s structs, `EAP_ABSENT` and the inline `eapPresent` |
| `template_generate.hpp` | `generate`, `renderLineage` and the numeric helpers: yes. `generateToFiles`, `renderLineageToFiles`, `readResAny`: **link** |
| `decollide_eap.hpp` | **link** (`applyDecompsToEap` calls `neurofileio::growEap`) |
| `neurofileio.h` | **link**, except for the structs and inline helpers |

### 1.5 Header dependency graph

```
neurofileio.h ─── neurosuite_core_export.h          neurofileio.cpp ─── custody.hpp
templateclass.hpp ─── neurofileio.h
decollide_eap.hpp ─── decollide.hpp, templateclass.hpp, neurofileio.h
projectionscope.hpp ─── neurofileio.h
drift_partition.hpp ─── neurofileio.h, running_stats.hpp
template_generate.hpp ─── neurofileio.h, custody.hpp, running_stats.hpp
spike_extract.hpp ─── stderiv_transform.hpp ─── pca_projection.hpp (for SdiffOrder)
custody.hpp, decollide.hpp, running_stats.hpp, pca_projection.hpp ─── std only
```

---

## 2. Conventions shared by all headers

**Namespaces.** `neurofileio` is a top-level namespace (historical). Everything else is under `neurosuite::`: `custody`, `decollide`, `templateclass`, `templategen`, `stats`, `drift`, `projectionscope`, and `core` (for `pca_projection`, `stderiv_transform`, `spike_extract`). Consumers commonly alias (`namespace cst = neurosuite::custody;`, `dc`, `tg`, `dr`, `ps`, `tc`).

**Waveform layouts.** Two layouts are in use; mixing them is the most likely bug.

| Layout | Index of (sample `t`, channel `c`) | Used by |
|---|---|---|
| **Sample-major, channel-fastest** (the `.spk` layout) | `t*nChannels + c` within a record; record `s` starts at `s*nSamples*nChannels` | `neurofileio::SpkFile`, `.wtf`/`.mtf`, `decollide`, `template_generate`, `running_stats`, `WtlNode::mean/std`, the rows `extractSpikeWindows` emits |
| **Channel-major** | `c*nSamp + t` | `pcaProjectScores`/`pcaProjectionEnergy` input, `applyStderivTransform` in/out, `extractSpikeWindows`' internal buffer |

**Units.**

| Quantity | Unit |
|---|---|
| `.res` times, `res` vectors, `timestampAt`, `tau`, EAP offsets, `recShift`, `sampleShift` | samples (integer) |
| `.wti`/`.wtl` drift windows `a`,`b`; `Partition` bounds and `tEnd`; `projectionscope::Interval` | seconds |
| `.wti`/`.wtl` adapt windows | spike energy (L2 norm of the int16 record) |
| `Params::sr` | Hz (default 32552) |
| `Params::chunkMin` | minutes |
| `.evt` times | milliseconds (see FILE_FORMATS) |

**Indices.** Spike indices are 0-based positions in the group's `.res`/`.clu`/`.spk` order. Klusters' `featureValue` is 1-based; callers convert (`klustersdoc_realign.cpp` passes `featureValue(i + 1, timeDim)` and `peakSampleIndex() - 1`). Template-class ids are `.eap` column indices. `.wti` `row` equals the `.wtf` record index.

**Rounding to int16 differs by header, deliberately (each matches a reference implementation):**

| Where | Rule | Saturates? |
|---|---|---|
| `stderiv_transform::clampToInt16` | `std::lround` (half away from zero) | yes, to [-32768, 32767] |
| `decollide::applyMethod` | `std::nearbyint` (current FP mode; default half-to-even, matching numpy `rint`) | **no** |
| `decollide::applyFeatures` (int64 features) | `std::llround` (half away from zero) | n/a |
| `template_generate` medians and lineage means | `std::rint` (half-to-even, numpy parity) | no (values stay inside int16 range by construction) |

**Error handling.** No header throws on purpose. Failure is reported through `ok`/`found` flags (`Grown::ok`, `Result::ok` + `Result::err`, `Resolved::found`, `Anchor::ok`, `EapApply::ok`), a `bool` return, `-1`/empty return, or an `outOfRange` counter. Library code may still throw from the standard library (`std::bad_alloc`, and the one `std::stoi` in `custody::parseAnchor` — see §5).

**Ownership and threading.** All functions are free functions over value types; there is no global or static mutable state anywhere in the library. Inputs are taken by `const&` and outputs returned by value, except the explicitly mutating APIs (`templateclass::*`, `decollide::applyDecompsToEap`, `drift::splitRegion/mergeRegion/tileClassDrift`, `templategen::median_np` which reorders its argument). Concurrent calls are safe on distinct objects; concurrent mutation of one `EapFile`/`TclRegistry`/`WtlForest` needs the caller's lock. Functions that probe the filesystem (`custody::resolve*`, `fileExists`, the `*ToFiles` wrappers) are subject to the usual check-then-use races.

---

## 3. Header reference

### 3.1 `custody.hpp` — file-naming policy

Namespace `neurosuite::custody`. Header-only. **Documented in depth in [FILE_FORMATS.md §1.1](FILE_FORMATS.md#11-per-group-naming-and-chain-of-custody) and in [`naming.md`](../../../doc/ndmanager-plugins/formats/naming.md); not repeated here.** In one paragraph: per-group files are `<base>.<type>.<method>.<group>[.<stage>]`; `classify(type)` puts each type in `MethodSpecific` (strict path, no fallback), `Shared` (`res`, `spk`: method → waveform token → `standard` → untagged) or `SessionWide` (`<base>.<type>`). `parseMethodToken` parses `<family>[_<S|C><order>][_D<lag><dims>]`; unknown suffixes make the token opaque. `resolveAny` finds the one physical copy of a Shared artifact under any method token by scanning the directory. `parseAnchor`/`methodOf` parse names from the right, so a base may contain dots.

API surface (signatures only):

```cpp
enum class Klass { MethodSpecific, Shared, SessionWide };
struct MethodSpec { std::string family; char kind; int order; int lag; int dims; };
struct Anchor     { std::string base, type, method; int group = -1; std::string suffix; bool ok = false; };
struct Resolved   { std::string path, method; bool found = false; };

const char* kDefaultMethod();                         // "standard"
MethodSpec  parseMethodToken(const std::string& m);
bool        isStderivMethod(const std::string& m);
std::string waveformToken(const std::string& m);      // stderiv_C5_D34 -> stderiv_C5
bool isPerGroupType(const std::string&); bool isSessionWideType(const std::string&);
bool isKnownType(const std::string&);    bool isClusterIdType(const std::string&);   // clu | clc
Klass classify(const std::string& type);
std::string methodPath(base, type, method, group);
std::string stagePath(base, type, method, group, stage);
std::string untaggedPath(base, type, group);
std::string sessionPath(base, type);
std::string methodOf(const std::string& path);
Anchor      parseAnchor(const std::string& path);
Resolved    resolve(base, type, group, method);
Resolved    resolveAny(base, type, group, preferred = "");
bool        resolvedIsStderiv(const Resolved&);
std::vector<std::string> staleAfterRealign();         // {"fet","pca"}
```

**Consumers.** kiloklustakwik (`KlustaKwik.cpp`); klusters (`klusters*.cpp`, `klustersdoc*.cpp/.h`, `clustersprovider.cpp`, `clusterview.cpp`, `templateview.cpp`, `templateclassstore.cpp`, `templatelineagestore.cpp`); plugins `process_alignspikes`, `process_alignspikes_pca`, `process_decomposecollisions`, `process_initeap`; `neurofileio.cpp` (delegates `methodPath`/`stagePath`/`methodFromPath`); `template_generate.hpp`.

**Twins — must stay in step:**

| Twin | Language | Coverage |
|---|---|---|
| `src/ndmanager-plugins/scripts/ndm_resolve_io.py` | Python | whole policy; imported by `process_driftcorrect.py`, `collision_viewer.py` |
| `src/ndmanager-plugins/scripts/ndm_custody` | bash | whole policy (`ndm_classify`, `ndm_resolve`, `ndm_resolve_any`, `ndm_parse_method_token`, …); sourced by `ndm_functions` |
| fiber-kit `neuro_io.py` (`resolve_any`, token grammar) | Python, other repo | `method_token`, `is_stderiv`, `resolve_any` vector kinds |

All four run [`custody_vectors.tsv`](#43-cross-language-conformance-vectors).

### 3.2 `decollide.hpp` — apply and fit two-component collision decompositions

Namespace `neurosuite::decollide`. Header-only, std only. A C++ port of fiber-kit's `fiber_decollide.decollide_spikes`. It **applies** a decomposition (and, for the interactive mode, fits one against a fixed pair of units); the batch decomposition itself is `process_decomposecollisions`.

```cpp
struct Component { int unit = 0; int tau = 0; double amp = 0.0; };          // tau in samples
struct Decomp    { int64_t spikeIndex = -1; Component c1; Component c2; };
struct Grown     { std::vector<int64_t> res; std::vector<int> clu; std::vector<int16_t> spk; bool ok = false; };
struct OutRow    { int64_t time; int clu; int64_t src; int decompIdx; int comp; };  // comp 0 pass, 1 = c1, 2 = c2
struct Fit       { Component c1, c2; double residFrac = 1.0; bool ok = false; };
struct AmpGate   { double lo = 0.0, hi = 0.0; };                               // hi <= lo disables

void roll0(const std::vector<float>& w, int nSamples, int nChannels, int s, std::vector<float>& out);
std::map<int, std::vector<float>> meanTemplates(const std::vector<int16_t>& spk, int nSamples,
                                                int nChannels, const std::vector<int>& clu);
std::vector<OutRow> planGrow(const std::vector<int64_t>& res, const std::vector<int>& clu,
                             const std::vector<Decomp>& decomps);
Grown applyMethod(const std::vector<int64_t>& res, const std::vector<int>& clu,
                  const std::vector<int16_t>& spk, int nSamples, int nChannels,
                  const std::vector<Decomp>& decomps);
std::vector<std::vector<int64_t>> applyFeatures(const std::vector<int64_t>& res, const std::vector<int>& clu,
                  const std::vector<std::vector<int64_t>>& fet, const std::vector<Decomp>& decomps);
Fit  fitOne(const std::vector<float>& c, int unitA, const std::vector<float>& TA,
            int unitB, const std::vector<float>& TB, int nSamples, int nChannels, int maxShift);
double peakToPeak(const std::vector<float>& w, double amp);
std::vector<Decomp> fitPair(const std::vector<int16_t>& spk, int nSamples, int nChannels,
            const std::vector<int64_t>& idx, int unitA, const std::vector<float>& TA,
            int unitB, const std::vector<float>& TB, int maxShift, double residualThreshold,
            AmpGate gateA = {}, AmpGate gateB = {});
```

**Model.** For collision waveform `c` at spike `i` with components `(k1,a1,τ1)`, `(k2,a2,τ2)` and per-unit mean templates `T`:

```
clean1 = c − a2 · roll0(T2, τ2)                    → unit k1 at res[i] + τ1
clean2 = roll0(c − a1 · roll0(T1, τ1), −τ2)        → unit k2 at res[i] + τ2
```

`roll0` is a zero-filled, non-circular shift: `out[t] = w[t−s]` when `0 ≤ t−s < nSamples`, else 0.

**Contracts.**

- `planGrow` decides the output order once: every non-selected spike passes through; each valid `Decomp` (index in range) is replaced by its two constituents; the result is **stable-sorted by time**. Because the order depends only on times, `res`/`clu` produced for different methods are identical and row-aligned; only `spk` differs. Out-of-range `spikeIndex` entries are ignored. The output grows by one row per resolved collision.
- `applyMethod` requires `clu.size() == res.size()` and `spk.size() == res.size()*nSamples*nChannels`, else returns `ok=false`. Templates are the means of **this method's** `spk`, so run it once per method with the same `decomps`. A constituent whose unit has no template (no spikes) is written as the unmodified collision waveform, keeping rows aligned.
- `meanTemplates` does no size check; callers must pass `spk` with at least `clu.size()` records. Units with no spikes are absent from the map.
- `applyFeatures` grows the in-memory feature table on the same plan, using PCA linearity: `f(clean1) = f(c) − a2·meanFeat(k2)` and symmetrically; exact at `τ = 0`, approximate otherwise. Feature dimension is taken from `fet[0]`. Returns empty on a size mismatch.
- `fitOne`/`fitPair` (greedy two-pass matching pursuit): for each shift in `[−maxShift, maxShift]` the least-squares amplitude `a = ⟨r, T@τ⟩ / ⟨T@τ, T@τ⟩` and analytic residual are computed; the unit explaining more is `c1`, the other is fitted to the residual. `residFrac = ‖residual‖² / ‖c‖²`. `fitPair` returns only accepted fits: `residFrac < residualThreshold`, both amplitudes `> 0`, and, when a gate is enabled, peak-to-peak of `amp·T` inside `[lo, hi]`. Rejected spikes are simply omitted (so `applyMethod` leaves them as they are). Templates passed to the fit must be in the same channel-fastest layout.

**Consumers.** klusters (`klusters.cpp`: interactive targeted decollide — `meanTemplates`, `fitPair`, `applyMethod`, `applyFeatures`), `process_decomposecollisions` (`Decomp`/`Component` as the input of `decollide_eap`).
**Twin.** fiber-kit `fiber_decollide.py` (`decollide_spikes`, `_roll0`). The batch fit in `process_decomposecollisions` is a separate implementation of the same gates.

### 3.3 `decollide_eap.hpp` — record decompositions in the `.eap` matrix

Namespace `neurosuite::decollide`. Header-only, **requires linking** (`growEap`). Records the presence and integer offset of each collision constituent in the `.eap` membership matrix (format: [FILE_FORMATS §4.1](FILE_FORMATS.md#41-eap--membership--offset-matrix-binary-int8)) without adding rows; amplitudes and fractional shifts stay in `.col`.

```cpp
struct EapApply { bool ok = false; int cellsWritten = 0; int classesCreated = 0; int grows = 0; };

int    eapClassForUnit(const neurofileio::TclRegistry& reg, int unit);   // lowest Active col with provenanceClu == unit, else -1
int8_t eapClampOffset(int tau);                                          // clamp to [-127, 127]; never EAP_ABSENT (-128)
EapApply applyDecompsToEap(neurofileio::EapFile& e, neurofileio::TclRegistry& reg,
                           const std::vector<Decomp>& decomps,
                           const std::string& stage, const std::string& created, int growBy = 128);
```

**Contract.** No-op (`ok=false`) unless `e.ok && reg.ok && reg.nClasses == e.nClasses`. For each decomposition, for each constituent: map the unit to a class column (reuse the unit's existing Active class — "same unit → same column" — else `templateclass::createClass` with `provenanceClu = unit`, `provenanceStage = stage`, `created = created`, empty membership); if no Free column is left, widen both `e` (via `growEap`) and `reg` by `growBy` (≤ 0 → 128); then set `cell[spike][col] = eapClampOffset(tau)`. Other cells are untouched. Out-of-range spike indices are skipped silently. Mutates `e` and `reg` in memory; the caller persists with `writeEap`/`writeTcl`.

**Consumers.** `process_decomposecollisions.cpp`, klusters (`klusters.cpp`, in-GUI decollide commit).
**Twin.** fiber-kit's decollide engine applies the same `.eap` logic (per the header comment); `projectionscope::classForUnit` is an identical copy of `eapClassForUnit` in this library.

### 3.4 `drift_partition.hpp` — session-wide drift partition and lineage re-grain

Namespace `neurosuite::drift`. Header-only, no link.

The drift regions of the manual template lineage (`.wtl`) form a single session-wide tiling of `[0, tEnd)` seconds shared by all template classes. No separate file stores the partition; it is recovered from the tiling drift-root windows.

```cpp
struct Partition {
    double tEnd = 0.0;              // seconds
    std::vector<double> bounds;     // interior boundaries, strictly increasing, inside (0, tEnd)
    int nRegions() const;           // bounds.size() + 1
    double edge(int i) const;       // 0, bounds[i-1], tEnd
    std::pair<double,double> region(int i) const;
    int  regionOf(double t) const;  // half-open regions; last region closed at tEnd; clamped
    bool valid() const;
};
Partition uniformPartition(double tEnd, int k);
Partition partitionFromWindows(const std::vector<std::pair<double,double>>& windows, double tEndHint = 0.0);
bool splitRegion(Partition& p, int i, double t);   // false unless t strictly inside region i
bool mergeRegion(Partition& p, int i);             // removes boundary i (merges i and i+1)
neurofileio::WtlForest regrainForest(const neurofileio::WtlForest& forest, const Partition& p);
void tileClassDrift(neurofileio::WtlForest& forest, int classId, const Partition& p);
```

**Algorithm (`regrainForest`).** Works on the nodes' running summaries only (`mean`, `std`, `count`; see `running_stats.hpp`). A drift root is a node whose `kind` starts with `"drift"` and whose `parent < 0`. For each class (first-seen order) and each new region, the new drift root is the exact weighted combine of every old drift root whose window overlaps the region: exact match copies, merged regions combine, a split duplicates the parent summary into both halves, a moved boundary combines partial overlaps (approximate), and a region nothing overlaps becomes an empty placeholder (`count 0`). Every other node of the class is copied with its own summary and re-parented to the new region its old parent root overlaps most (region 0 if the parent is not an old drift root). Node ids are reassigned densely; the output is `version 2` with the input geometry.

`tileClassDrift` appends one empty `"drift-root"` per region for one class, with ids continuing after the current maximum.

**Consumers.** klusters `templatelineagestore.cpp/.h` (`Partition`, `uniformPartition`, `partitionFromWindows`, `splitRegion`, `mergeRegion`, `regrainForest`, `tileClassDrift`, `detail::isDriftKind`).
**Twin.** None in code. fiber-kit still writes `.wtl` v1 (see [FILE_FORMATS §5](FILE_FORMATS.md#5-fiber-kit-mirroring)).

### 3.5 `neurofileio.h` / `neurofileio.cpp` — on-disk formats

Namespace `neurofileio`. The only compiled part of the library; every out-of-line function is `NEUROSUITE_CORE_EXPORT`. **Fully documented in [FILE_FORMATS.md](FILE_FORMATS.md)** ([formats at a glance](FILE_FORMATS.md#1-formats-at-a-glance), [template-library family](FILE_FORMATS.md#4-the-template-library-family--eap-tcl-wti-wtf-wtl)). In short: readers/writers for `.clu`/`.res` (text and binary, with auto-detection in `readClusterRes`), `.fet` (text, binary int64), `.spk` (also used for `.wtf`/`.mtf`), `.col` (accepted records only), `.wti`, `.wtl`, `.eap`, `.tcl`, `.evt`, interleaved int16 signal windows, and the older `resolveInput`/`preferDerived`/`preferCanonical` resolution plus thin wrappers (`methodPath`, `stagePath`, `methodFromPath`) that delegate to `custody.hpp`. Functions are pure (path in, value out) and signal failure with `ok` fields or `bool`/`-1` returns.

**Consumers.** All of klusters' I/O (`data.cpp`, `klustersdoc*.cpp`, `klusters.cpp`, `clusterview.cpp`, `templateview.h`, `template*store.*`, `spikerealigndialog.cpp`), NeuroScope's providers (`clustersprovider.cpp`, `eventsprovider.cpp`, `tracesprovider.cpp`), kiloklustakwik (`KlustaKwik.cpp`, source-included), `process_initeap`, `process_decomposecollisions`, and the other core headers.
**Twin.** fiber-kit `neuro_io.py` (format contract; `.wtl` is still v1 there).

### 3.6 `pca_projection.hpp` — PCAE basis I/O and projection

Namespace `neurosuite::core`. Header-only, std only, kept C++17-clean. The `.pca` basis file is **not** a `neurofileio` format; this header owns it.

```cpp
enum class SdiffOrder : int32_t { None = 0, First = 1, Laplacian = 2, AllPairs = 3, Custom = 4, CustomCar = 5 };
enum class Method : int32_t {
    Standard = 0, SdiffFirst = 1, SdiffLaplacian = 2, SdiffAllPairs = 3,
    StderivFirst = 4, StderivLaplacian = 5, StderivAllPairs = 6,   // 6 = canonical
    StderivCustom = 7, StderivCustomCar = 8 };                     // tokens stderiv_C4 / stderiv_C5
bool        methodValid(int32_t v);          // 0..8
SdiffOrder  spatialOrder(Method m);
bool        hasTemporalDiff(Method m);       // all Stderiv*
const char* methodTag(Method m);             // "standard" | "sdiff" | "stderiv"

struct PcaBasis {
    int nCh, data2use, nComp, recShift; bool centered; Method method; int nInputChannels;
    std::vector<std::vector<double>> means;  // [ch][data2use]
    std::vector<std::vector<double>> evec;   // [ch][data2use*nComp], ev[k*data2use + u]
    bool valid() const;                      // nCh, data2use, nComp > 0
    int  inputChannels() const;              // nInputChannels, or nCh when 0
};
inline constexpr int32_t kPcaeMagic = 0x50434145, kPcaeVersion = 2;
bool   loadPca (const std::string& path, PcaBasis& pca);
bool   writePca(const std::string& path, const PcaBasis& pca);
int    pcaProjectScores(const int16_t* wfChannelMajor, int nChan, int nSamp, const PcaBasis& pca,
                        std::vector<double>& out, int sampleShift = 0);
double pcaProjectionEnergy(const int16_t* wfChannelMajor, int nChan, int nSamp, const PcaBasis& pca,
                           int sampleShift = 0);
```

**File format (PCAE, native byte order, little-endian in practice).** `int32` header: magic `0x50434145`, version (1 or 2), `nChannels`, `data2use`, `nComponents`, `recShift`, `isCentered`, and for v2 `method`, `nInputChannels`. Body, block-wise: all channels' means (`nCh × data2use` doubles, always present), then all channels' eigenvectors (`nCh × data2use × nComp` doubles, component-major per channel: `ev[k*data2use + u]`). `isCentered` controls only whether means are subtracted at projection time. `loadPca` accepts v1 (as `Standard`, `nInputChannels = nCh`) and v2, and fails on open error, bad magic, unknown version, invalid method, non-positive geometry or a short read. `writePca` always writes v2 and writes `inputChannels()` (never 0).

**Projection.** `score[ch][k] = Σ_u ev[k*data2use+u] · (x[recShift + sampleShift + u] − (centered ? μ[u] : 0))` over a **channel-major** int16 waveform. `out` is resized to `min(pca.nCh, nChan) × nComp` (channel-major, `out[ch*nComp + k]`); the return value is the number of channels projected (0 for an unusable basis or bad geometry). Samples that a shift moves outside `[0, nSamp)` are skipped, so scores at different shifts are comparable only if the caller keeps the usable window the same. `sampleShift = +s` gives the scores of a waveform whose peak is `s` samples earlier. `pcaProjectionEnergy = Σ score²` and is defined through `pcaProjectScores`. **The projection does not apply the basis' transform**: a caller with a stderiv basis must transform the waveform first (`stderiv_transform.hpp`, order from `spatialOrder(pca.method)`).

**Consumers.** `process_pca` (`writePca`, `Method`, `spatialOrder`, `methodValid`), `process_refeaturize`, `process_refeaturize_stderiv`, `process_shadowcluster`, `process_alignspikes_pca` (`loadPca`, `pcaProjectionEnergy`), kiloklustakwik (`KK_refeaturize.cpp`, `KK_timeshift.cpp`), klusters (`klustersdoc.h`, `klustersdoc_realign.cpp`, `klustersdoc_featurespace.cpp`, `pca_row_projection.h`).
**Twins.** fiber-kit `fiber_pca.py` (`Method` IntEnum and `method_tag`, pinned by [`pca_method_vectors.tsv`](#43-cross-language-conformance-vectors)); `src/klusters/src/pca_refine_cuda.cu` (device mirror of the projection energy).

### 3.7 `projectionscope.hpp` — temporally restricted projection scope

Namespace `neurosuite::projectionscope`. Header-only, no link. Backs Klusters' ClusterView gate: in temporally restricted mode, a spike is shown only if its time falls inside the drift windows of the pinned basis clusters' templates.

```cpp
struct Interval { double a = 0.0; double b = 0.0; };   // closed [a, b], seconds
int  classForUnit(const neurofileio::TclRegistry& reg, int unit);
std::vector<Interval> mergeIntervals(std::vector<Interval> v);
std::vector<Interval> scopeIntervals(const neurofileio::WtiIndex& wti, const neurofileio::TclRegistry& reg,
                                     const std::vector<int>& basisClusters,
                                     const std::string& driftLink = "drift");
bool inScope(const std::vector<Interval>& iv, double t);
bool gateActive(bool temporallyRestricted, const std::vector<Interval>& iv);
```

**Contract.** For each basis cluster the candidate `.wti` unit ids are its Active class (via `.tcl` provenance, if any) **and** the cluster id itself, because a `.wti` written with `--eap` is keyed by class id and one written without it by cluster id. Rows with `link == driftLink` and `nSpikes > 0` contribute `[a, b]`; placeholders do not. `mergeIntervals` drops `b < a`, sorts, and coalesces overlapping or touching intervals. `inScope` is a binary search and requires sorted, non-overlapping input (the output of the two functions above). `gateActive` is false for an empty scope, so the gate never hides every spike. The caller converts spike times to seconds.

**Consumers.** klusters `clusterview.h/.cpp`.

### 3.8 `running_stats.hpp` — mean/std summaries

Namespace `neurosuite::stats`. Header-only, std only.

```cpp
struct MeanStd { std::vector<float> mean; std::vector<float> std; int64_t count = 0; };
MeanStd meanStdOfRecords(const std::vector<int16_t>& recs, std::size_t recLen);
MeanStd combineMeanStd(const std::vector<float>& meanA, const std::vector<float>& stdA, int64_t countA,
                       const std::vector<float>& meanB, const std::vector<float>& stdB, int64_t countB);
```

A lineage node stores mean, **population** std and count, never spikes; a new selection is folded in with `combineMeanStd`, which is exact (Chan's parallel-variance formula, per element, double arithmetic, stored as float): `combine(A, B) == summary(A ∪ B)` up to float rounding. `meanStdOfRecords` returns `count 0` with empty vectors when `recLen == 0`, when `recs.size()` is not a multiple of `recLen`, or for an empty stack; it uses `E[x²] − E[x]²` with a clamp at 0. `combineMeanStd` treats a `count ≤ 0` operand as empty and returns the other; on a length mismatch between the means it returns A unchanged (no error flag). A median cannot be combined this way, which is why lineage nodes store means.

**Consumers.** `drift_partition.hpp`, `template_generate.hpp` (include only), klusters `templatelineagestore.cpp` (`meanStdOfRecords`, `combineMeanStd`).

### 3.9 `spike_extract.hpp` — cut spike windows out of a `.fil`

Namespace `neurosuite::core`. Header-only, std + `<cstdio>`.

```cpp
struct SpikeWindowSpec {
    int nSamp = 0;                 // samples per waveform
    int peakSample = 0;            // 0-based index in the window that the timestamp refers to
    int totalNbChannels = 0;       // channels interleaved in the .fil
    const int* groupChannels = nullptr;   // absolute channel per group channel (nChan entries)
    int nChan = 0;
    bool applyTransform = false;   // stderiv transform per window
    SdiffOrder order = SdiffOrder::AllPairs;
    const int* partner = nullptr;  // Custom (order 4)
    const int* setOff  = nullptr;  // CustomCar (order 5): (nChan+1) prefix table
    const int* setMem  = nullptr;  //   flattened reference sets
    std::size_t bufferBytes = 8u << 20;   // read buffer budget; affects speed only
};
template <class TimestampAt, class EmitRow>
std::int64_t extractSpikeWindows(std::FILE* fil, std::int64_t nSpikes, const SpikeWindowSpec& spec,
                                 TimestampAt timestampAt, EmitRow emitRow,
                                 std::int64_t* outOfRange = nullptr);
```

**Contract.** `timestampAt(i)` returns spike `i`'s time in samples and **must be non-decreasing**; the window starts at `timestampAt(i) − peakSample`. `emitRow(i, const int16_t* row)` is called exactly once per spike, in order, with `nSamp × nChan` int16 values in **sample-major** (`.spk`) order; the pointer is only valid during the call. A window outside the file (or that the buffer cannot supply) is emitted as zeros and counted in `*outOfRange`. Returns the number of rows emitted, or −1 if `fil` is null, the geometry is non-positive, or the file cannot be sized. The function seeks `fil` (ownership stays with the caller) and keeps a forward-moving sliding buffer, so the whole pass is sequential I/O. With `applyTransform`, each window is transformed with `applyStderivTransform`, seeded (`seedFromPrevious`, default true) with `stderivSeed()` of the sample before the window — byte-identical to `process_extractspikes_stderiv -R`, which `ndm_extractspikes` runs — or with 0 when `seedFromPrevious` is false, the convention of `.spk` files from the older single-pass extractor. `detectStderivSeeding()` tells which convention an existing file follows by recomputing sample 0 of a few stored spikes both ways; a tool that rewrites spikes of an existing file must follow it. Offsets use 64-bit `fseeko` / `_fseeki64`.

**Consumers.** klusters `klustersdoc_realign.cpp` (re-extract on save; `peakSample = peakSampleIndex() − 1`), `klustersdoc_featurespace.cpp`, `klustersdoc.h` (`SpikeWindowSpec` member).
**Twins.** `process_extractspikes_stderiv` and `process_reextractspikes_stderiv` cut windows with their own code; they are meant to produce the same bytes.

### 3.10 `stderiv_transform.hpp` — spatial derivative + temporal first difference

Namespace `neurosuite::core`. Header-only, std only (includes `pca_projection.hpp` for `SdiffOrder`).

```cpp
int16_t clampToInt16(double v);      // lround, then saturate
double  spatialDeriv(SdiffOrder order, const double* chanRow, int idx, int nChan,
                     const int* partner = nullptr, const int* setOff = nullptr, const int* setMem = nullptr);
void    applyStderivTransform(SdiffOrder order, const int16_t* in, int nChan, int nSamp, int16_t* out,
                              const int* partner = nullptr, const int16_t* prevSeed = nullptr,
                              const int* setOff = nullptr, const int* setMem = nullptr);
```

**Spatial derivative of channel `i` in one time sample** (`n = nChan`):

| Order | Value |
|---|---|
| `None` | `x[i]` |
| `First` | `x[i] − x[i+1]`; last channel `x[i] − x[i−1]` |
| `Laplacian` | `x[i] − ½(x[i−1] + x[i+1])`; edges use the single neighbour; `n == 1` → `x[i]` |
| `Custom` | `x[i] − x[partner[i]]`; null `partner` or `n == 1` → `x[i]` |
| `CustomCar` | `x[i] − mean(x[setMem[setOff[i] .. setOff[i+1])])`; null/empty set or `n == 1` falls through to `AllPairs` |
| `AllPairs` (default) | `n·x[i] − Σ_j x[j]`; `n == 1` → 0 |

**Transform.** Per time sample: spatial derivative → `clampToInt16`; then per channel `out[t] = clampToInt16(sd[t] − sd[t−1])` with `sd[−1] = prevSeed[c]` (0 when `prevSeed` is null). Layout is channel-major in and out; `out` may alias `in`. `partner` and the set tables are within-group channel indices and are not bounds-checked. The header names `process_extractspikes_stderiv::computeSDiff` + `fill_sdiff_buffer` as the byte-exact ground truth.

**Consumers.** `spike_extract.hpp`; klusters `klustersdoc_internal.h` (`spatialDeriv`), `klustersdoc_realign.cpp` (`applyStderivTransform`), `klustersdoc.h`.
**Twins (hand copies, must match byte for byte):** `computeSDiff` in `process_extractspikes_stderiv.cpp`, `process_pca_stderiv.cpp` and `process_reextractspikes_stderiv.cpp`; fiber-kit `fiber_stderiv.apply_stderiv_transform`.

### 3.11 `template_generate.hpp` — template library generation and lineage rendering

Namespace `neurosuite::templategen`. Header-only; the `*ToFiles` wrappers and `readResAny` require linking. A port of fiber-kit's `fiber_template.py` so Klusters can build the template library without the Python plugin.

```cpp
struct Params {
    int nSamples = 0, nChannels = 0; double sr = 32552.0;
    bool linkDrift = true, linkAdapt = false;
    int nChunks = 0; double chunkMin = 0.0;      // drift: fixed count, or chunk minutes
    int nEnergy = 5;                              // adapt: bins
    int maxPer = 800;                             // median over <= maxPer strided spikes per bin
    bool dropEmpty = true;                        // else emit a 0-filled placeholder row
    bool eap = false;                             // membership from .eap columns instead of .clu
};
struct Result { std::vector<neurofileio::WtiRow> rows;
                std::map<std::string, std::vector<int16_t>> wtf;   // variant -> .spk-layout stack
                bool ok = false; std::string err; };

Result generate(const std::vector<int64_t>& times, const std::vector<std::string>& variants,
                const std::map<std::string, std::vector<int16_t>>& spk, const std::string& energyVariant,
                const std::vector<int>* clu, const neurofileio::EapFile* eapPtr,
                std::vector<int> units, const Params& p);

struct FileParams { std::string base; int group = 0; std::vector<std::string> variants;
                    std::string resVariant = "standard", cluVariant, cluTag, eapTag, outTag, energyVariant;
                    std::vector<int> units; };
Result generateToFiles(const FileParams& fp, const Params& gen, std::string* wtiPathOut = nullptr,
                       std::map<std::string, std::string>* wtfPaths = nullptr);

std::string lineageKindToLink(const std::string& kind);   // drift*, adapt*, collision* -> link; else verbatim
Result renderLineage(const neurofileio::WtlForest& forest, const std::string& variant, int nSamples, int nChannels);
struct LineageFileParams { std::string base; int group = 0; std::vector<std::string> variants;
                           std::string stage, spkTag; int nSamples = 0, nChannels = 0; double sr = 0.0; };
Result renderLineageToFiles(const LineageFileParams& fp, const neurofileio::WtlForest& forest,
                            std::string* wtlPathOut = nullptr, std::string* mtiPathOut = nullptr,
                            std::map<std::string, std::string>* mtfPaths = nullptr);

// path + numeric helpers (numpy parity)
std::string sessionPath(base, type, group, variant, tag);   // <base>.<type>[.<variant>].<group>[.<tag>], '.' in tag -> '_'
std::vector<int64_t> readResAny(const std::string& resPath, bool* ok);
double median_np(std::vector<float>& v);                     // reorders v; NaN if empty
double quantile_linear(const std::vector<double>& sorted, double q);
int    assign_bin(const std::vector<double>& edges, double v);
std::pair<std::vector<double>, int> chunk_edges(const std::vector<int64_t>& times, double sr, double chunkMin, int nChunks);
std::vector<double> energy_edges(const std::vector<double>& Esorted, int nEnergy);
double spike_energy(const int16_t* rec, std::size_t recLen);
std::vector<float> align_to_res(const int16_t* rec, int offset, int nsamp, int nchan);
std::vector<std::size_t> linspace_indices(std::size_t S, int maxPer);
double round_nd(double x, int d);
```

**`generate` (pure).** Validates geometry, a non-empty `variants` list, and that every variant's stack holds `times.size()` records (else `ok=false` with `err`). Membership: clu mode (`clu`, length N; default units are the ids ≥ 2) or eap mode (`eapPtr`, `nSpikes == N`; a unit is a column; default units are columns with any member; each member is shifted into the `.res` frame by its stored offset with `align_to_res`, zero-filled). If neither `nChunks` nor `chunkMin` is set, 6 chunks are used. For each unit and each enabled link:

- **drift**: chunk edges in samples from `chunk_edges` (`nC` equal chunks over `[0, max(times)+1)`, or `ceil(duration_min / chunkMin)` chunks); window `[a, b]` in seconds rounded to 3 decimals;
- **adapt**: energy = L2 norm of the record in `energyVariant` (default `"standard"` if requested, else the first variant); `nEnergy` equal-occupancy bins from linear-interpolated quantiles (last edge +1e-6); window rounded to 1 decimal.

Each bin with members (or every bin when `dropEmpty` is false) produces one `WtiRow` (`nSpikes` = full member count) and, per variant, one record: the per-element numpy median (`np.median` even-count convention) over at most `maxPer` members chosen with `np.linspace(...).astype(int)` stride, rounded with `rint`. Records are appended in row order, so `Result.wtf[v]` is a `.wtf` stack aligned to `rows`. Explicit `units` are not validated (§5).

**`generateToFiles` (link).** Resolves the shared `.res` with `custody::resolveAny(base, "res", group, resVariant)`; in clu mode reads `<base>.clu.<cluVariant>.<g>[.<cluTag>]` with the `.res` via `readClusterRes`; in eap mode reads `readResAny` + `<base>.eap.<g>[.<eapTag>]`; reads each `<base>.spk.<variant>.<g>` (method-pinned, no fallback). Applies the wrapper-level chunk default (eap: 12-minute chunks; clu: 6 chunks), calls `generate`, then writes `<base>.wtf.<variant>.<g>[.<outTag>]` per variant and `<base>.wti.<g>[.<outTag>]` (`peakSample = −1`, `sr = gen.sr`). On a read failure nothing is written; on a write failure earlier files may already exist.

**`renderLineage` (pure).** One `.wti` row per `.wtl` node in forest order: `link = lineageKindToLink(kind)`, `bin` = ordinal within `(class, link)`, `parent` = row index of the parent node if it was already emitted (else −1), `nSpikes = count`; the record is the node's **stored mean** rounded with `rint` (0-filled when the mean is empty or the wrong length). A single stack is returned under `variant`. Parents must precede children in `forest.nodes` for `parent` to be set.

**`renderLineageToFiles` (link).** Refuses a forest in which every node has `count 0`. Writes `<base>.wtl.<g>[.<stage>]` (version 2, geometry stamped), the model stack `<base>.mtf.<variants[0]>.<g>[.<stage>]` and the index `<base>.mti.<g>[.<stage>]` (v2 when any parent is set). Only `variants[0]` is used; `spkTag` is not read.

**Consumers.** klusters `klusters.cpp` (`generateToFiles`, standalone template generation), `templatelineagestore.cpp/.h` (`renderLineageToFiles`, `readResAny`, `sessionPath`), `templateview.cpp` (`sessionPath`, `lineageKindToLink`), `clusterview.cpp` (via the lineage store's `Result`). No plugin uses it.
**Twins.** fiber-kit `fiber_template.py` (`generate`, `chunk_edges`, `energy_edges`, `_align_to_res`, CLI chunk default in `main()`) and `neuro_io.session_path`. The numeric helpers exist to reproduce numpy exactly; change both sides together.

### 3.12 `templateclass.hpp` — template-class CRUD over `.eap` + `.tcl`

Namespace `neurosuite::templateclass`. Header-only, no link (persistence is the caller's, via `writeEap`/`writeTcl`). A class id is its `.eap` column; a class is live iff its `.tcl` entry is `Active`; its members are the present cells of its column. Formats: [FILE_FORMATS §4.1–4.2](FILE_FORMATS.md#4-the-template-library-family--eap-tcl-wti-wtf-wtl).

```cpp
std::vector<int>     activeCols(const TclRegistry& reg);                 // ascending
int                  firstFree(const TclRegistry& reg);                  // -1 when the pool is exhausted
bool                 setMembership(EapFile& e, int col, const std::vector<int64_t>& spikes, int8_t offset = 0);
std::vector<int64_t> members(const EapFile& e, int col);                 // ascending
int  createClass(EapFile& e, TclRegistry& reg, const std::vector<int64_t>& spikes, int8_t offset,
                 const std::string& label, int provClu, const std::string& provStage, const std::string& created);
bool deleteClass(EapFile& e, TclRegistry& reg, int col);                 // -> Tomb, column cleared, id never reused
bool mergeClasses(EapFile& e, TclRegistry& reg, int survivor, int victim);
```

**Contracts.** `reg.entries` is indexed by column (`entries[i].col == i`), as `readTcl`/`initTcl` produce. `setMembership` replaces the whole column; out-of-range spike indices are skipped. `createClass` takes the first Free column, fills its provenance, sets `mergedInto = −1`, and returns the column, or −1 when the pool is full (the caller grows with `neurofileio::growEap` and extends the registry, as `decollide_eap` and klusters' `TemplateClassStore` do) or `reg.nClasses != e.nClasses`. `mergeClasses` copies each victim cell into the survivor only where the survivor is absent (the survivor's offset wins a conflict), clears the victim column, and marks the victim `Merged` with `mergedInto = survivor`. All return false/−1 on invalid ids or non-Active classes.

**Consumers.** klusters `templateclassstore.cpp` (wraps every function), `decollide_eap.hpp` (`firstFree`, `createClass`).
**Twin.** None in code; the formats are mirrored by fiber-kit `neuro_io`.

---

## 4. Tests

### 4.1 Building and running

The suite is opt-in ([BUILD.md §6](../../../doc/BUILD.md#6-tests)):

```sh
cmake -B build -DNS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure            # everything
ctest --test-dir build -R '^core_test_|custody_conformance'   # this library only
```

`src/libneurosuite-core/CMakeLists.txt` adds `test/` only when `NS_BUILD_TESTS` is on; the root `CMakeLists.txt` calls `enable_testing()`. Each test is a self-contained executable with its own `main()` and a local `check()` helper; it prints `FAIL: …` lines and returns non-zero on failure. Several write scratch files into the working directory.

### 4.2 Test inventory (`src/libneurosuite-core/test/`)

| ctest name | Source | Links lib? | Covers |
|---|---|---|---|
| `core_test_custody_conformance` | `custody_conformance_test.cpp` | no | runs `custody_vectors.tsv` (argument) against `custody.hpp`; creates files in a `mkdtemp` directory for the `resolve`/`resolve_any` rows |
| `custody_conformance_python` | `../../ndmanager-plugins/test/test_custody_conformance.py` | — | same vectors against `ndm_resolve_io.py` (registered only if Python 3 is found) |
| `custody_conformance_bash` | `../../ndmanager-plugins/test/test_custody_conformance.sh` | — | same vectors against `ndm_custody` (registered only if bash is found) |
| `core_test_pca_method_conformance` | `pca_method_conformance_test.cpp` | no | runs `pca_method_vectors.tsv` against `Method`, `spatialOrder`, `hasTemporalDiff`, `methodTag`, `kPcaeMagic`/`kPcaeVersion` |
| `core_test_neurofileio_spk` | `neurofileio_spk_test.cpp` | yes | `.spk` round trip, binary `.fet`, `.col` accepted-record parse, `.wti` + `.wtf` alignment, `.eap` (sentinel, grow, queries), `.tcl` statuses, `stagePath` composition |
| `core_test_wti` | `wti_test.cpp` | yes | `.wti` v1/v2 round trip, `parent` column, `collision` link, hand-written files, version-3 rejection |
| `core_test_wtl` | `wtl_test.cpp` | yes | `.wtl` v2 round trip, v1 back-compat, header/version rejection, `nNodes` check, corrupt line, `wtl*` helpers |
| `core_test_decollide` | `decollide_test.cpp` | no | `roll0`; exact `applyMethod` at τ = 0; cross-method invariance of `res`/`clu`; shifted case; missing template; `applyFeatures`; `fitPair` |
| `core_test_decollide_eap` | `decollide_eap_test.cpp` | yes | class allocation per unit, reuse by provenance, pool growth, offset clamp, `.eap`/`.tcl` persistence |
| `core_test_templateclass` | `templateclass_test.cpp` | yes | create, merge (survivor keeps offset), delete (tombstone), pool exhaustion, persistence |
| `core_test_running_stats` | `running_stats_test.cpp` | no | `meanStdOfRecords`; `combine(A,B) == summary(A∪B)`; order independence; empty operands; three-way fold |
| `core_test_drift_partition` | `drift_partition_test.cpp` | no | tiling, `regionOf`, split/merge, `regrainForest` combine / duplicate / placeholder, leaf re-parenting |
| `core_test_projectionscope` | `projectionscope_test.cpp` | no | class lookup, drift-window union, abutting merge, placeholder → gate off, clu-id fallback, `inScope` |
| `core_test_template_generate` | `template_generate_test.cpp` | no | clu-mode drift bins, even-count median, `rint` half-to-even, eap offset alignment, adapt energy bins, `dropEmpty` |
| `core_test_template_lineage` | `template_lineage_test.cpp` | no | `renderLineage`: kind→link, bin ordinals, parent rows, `nSpikes`, stored mean copied, empty placeholder |
| `core_test_template_lineage_io` | `template_lineage_io_test.cpp` | yes | `renderLineageToFiles`: `.wtl` v2 + `.mti` + `.mtf` written and read back |

Not covered by any test in this directory: `stderiv_transform.hpp`, `spike_extract.hpp`, and `pca_projection.hpp`'s `loadPca`/`writePca`/`pcaProjectScores` (only the `Method` enum is pinned). Tests in other subprojects that exercise core code: `klusters_test_pcarowprojection` (`PcaBasis` via `pca_row_projection.h`), `klusters_test_templatelineagestore` and `klusters_test_templateclassstore` (both link `Neurosuite::core`), in `src/klusters/tests/`.

### 4.3 Cross-language conformance vectors

Two tab-separated tables are the shared contract between implementations in different languages and repositories. Lines starting with `#` and empty lines are comments; column 1 is the case kind; empty fields are real (trailing tabs matter, so runners split on every tab rather than on whitespace).

**`custody_vectors.tsv`** — naming policy.

| Kind | Columns after the kind |
|---|---|
| `classify` | `type`, `MethodSpecific`/`Shared`/`SessionWide` |
| `method_of` | `path`, expected method (empty for untagged) |
| `parse_anchor` | `path`, `base`, `type`, `method`, `group`, `suffix`, `ok` 0/1 |
| `resolve` | comma-separated suffixes to create under `<tmp>/sess.`, `type`, `group`, `method`, expected basename, `found` 0/1 |
| `resolve_any` | same as `resolve`, with the preferred method in the method column |
| `method_token` | token, `family`, `kind`, `order` (lag/dims are not columns; they are covered only by C++) |
| `is_stderiv` | method, 0/1 |

Readers: `custody_conformance_test.cpp` (C++, all kinds), `ndmanager-plugins/test/test_custody_conformance.py` (Python `ndm_resolve_io`, all kinds), `ndmanager-plugins/test/test_custody_conformance.sh` (bash `ndm_custody`, all kinds), and fiber-kit `test/test_custody_token_conformance.py` (only `method_token`, `is_stderiv`, `resolve_any`; it reports the skipped kinds).

**`pca_method_vectors.tsv`** — PCAE `Method` enum.

| Kind | Columns after the kind |
|---|---|
| `format` | magic (hex), version |
| `method` | integer value, name, spatial order (0–5), has-temporal 0/1, tag |

Readers: `pca_method_conformance_test.cpp` and fiber-kit `test/test_pca_method_conformance.py` (`fiber_pca.Method`).

**`test_shared_contract.py`.** Both tables are shipped as byte-identical copies in neurosuite-3 (`src/libneurosuite-core/test/`) and fiber-kit (`test/`). This script prints their MD5 hashes; with `FK_ROOT` pointing at a fiber-kit checkout it compares the two copies and fails on a difference (`FK_ROOT=/path/to/fiber-kit python3 src/libneurosuite-core/test/test_shared_contract.py`). It is not registered with ctest. fiber-kit carries the mirror-image script (`NS3_ROOT`).

**Rule for changing shared behaviour.** A change to anything a vector table pins (type classes, path shapes, the resolution order, the method-token grammar, `Method` values/helpers, the PCAE magic/version) is one change across all implementations:

1. Edit the vector table first, adding rows for the new behaviour.
2. Change `custody.hpp` (or `pca_projection.hpp`), `ndm_resolve_io.py` and `ndm_custody` in the same patch; run `ctest -R custody_conformance` (all three runners) or `core_test_pca_method_conformance`.
3. Copy the identical table into fiber-kit and change its mirror (`neuro_io.py` / `fiber_pca.py`) in a separate fiber-kit patch; run `test_shared_contract.py` with `FK_ROOT` to confirm the copies are identical.
4. Update [naming.md](../../../doc/ndmanager-plugins/formats/naming.md) / [FILE_FORMATS.md](FILE_FORMATS.md) if the documented behaviour changed.

Adding a new column to a table changes every reader in both repositories, which is why lag/dims are not columns of `method_token`. Behaviour without a vector table but with a twin (decollide, template generation, the stderiv transform; §3) must be changed in both places by hand and checked against the twin's tests.

### 4.4 Recipes

**Adding a header.**

1. Create `src/libneurosuite-core/src/neurosuite/core/<name>.hpp` with an include guard (`NEUROSUITE_CORE_<NAME>_HPP`) or `#pragma once`, code in `namespace neurosuite { namespace <area> { … } }`, and only standard-library, `neurosuite/core/…` includes. Keep it C++17-clean.
2. Header-only is the default (`inline` free functions, no globals). If it needs compiled code, add the `.cpp` to `_core_srcs` and mark every out-of-line declaration `NEUROSUITE_CORE_EXPORT` (hidden visibility is the default).
3. Put it in `src/neurosuite/core/`; the directory install picks up every `.h`/`.hpp` there.
4. Write the file comment: what it is, who uses it, its twins.
5. In each consumer: `#include <neurosuite/core/<name>.hpp>`; link `Neurosuite::core` if any out-of-line `neurofileio` function is used; plugins that do not link get the include path from `ndmanager-plugins/src/CMakeLists.txt`.
6. Add a test (below) and a section to this page.

**Adding a test.**

1. Create `src/libneurosuite-core/test/<name>_test.cpp` with its own `main()`, a `check(bool, what)` helper that prints `FAIL:` and counts, and a non-zero exit on failure.
2. Register it in `test/CMakeLists.txt` following the existing blocks:

   ```cmake
   add_executable(core_test_<name> <name>_test.cpp)
   target_compile_features(core_test_<name> PRIVATE cxx_std_17)
   target_include_directories(core_test_<name> PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
   target_link_libraries(core_test_<name> PRIVATE Neurosuite::core)   # only if neurofileio.cpp is needed
   add_test(NAME core_test_<name> COMMAND core_test_<name>)
   ```

3. For a vector-driven test, pass the table path as an argument (`COMMAND core_test_<name> "${CMAKE_CURRENT_SOURCE_DIR}/<table>.tsv"`), and register the other-language runners in the same file with a `find_package`/`find_program` guard, as the custody runners are.
4. Build with `-DNS_BUILD_TESTS=ON` and run `ctest -R core_test_<name> --output-on-failure`.

---

## 5. Known issues

Inconsistencies found while writing this page. None has been fixed here.

**Code**

| # | Where | Issue |
|---|---|---|
| C1 | `spike_extract.hpp`, Klusters realign / nudge | **Fixed (0489).** Windows re-cut from the `.fil` were transformed with seed 0, while `process_extractspikes_stderiv -R` (what `ndm_extractspikes` runs) seeds with the spatial derivative of the preceding sample. The extractor's detection pass and the older single-pass extractor do use seed 0, so both conventions exist in `.spk` files. Now `stderivSeed()` gives the seed, `SpikeWindowSpec::seedFromPrevious` selects the convention, `detectStderivSeeding()` reads it off an existing file, and Klusters follows the file; checked byte-for-byte against the extractor binary. |
| C2 | `src/libneurosuite-core/src/CMakeLists.txt` | **Fixed (0492).** All headers are installed (directory install), and the exported target is named `Neurosuite::core` like the in-tree alias (it was `Neurosuite::neurosuite-core`, so an installed consumer linking `Neurosuite::core` failed to configure). Verified with an out-of-tree consumer that includes every header. Was: `_core_hdrs` installs only 5 of 13 headers. `decollide*.hpp`, `drift_partition.hpp`, `projectionscope.hpp`, `running_stats.hpp`, `spike_extract.hpp`, `template_generate.hpp` and `templateclass.hpp` are missing from an install, so an installed `Neurosuite::core` cannot be used for them. |
| C3 | `custody.hpp` `resolveAny` | The directory scan uses `std::filesystem::directory_iterator`, whose order is unspecified; `ndm_resolve_io.resolve_any` sorts names and bash globs in collation order. With two or more copies under tokens outside `standard`/`stderiv`/`sdiff` (for example `stderiv_C4` and `stderiv_C5`), the C++ result can differ from the mirrors. The filters also differ: C++ and Python reject a token that contains a dot; bash accepts any name `ndm_method_of` parses. No vector covers either case. |
| C4 | `custody.hpp` `parseAnchor` | `std::stoi` on the all-digit group field throws `std::out_of_range` for a group token larger than `INT_MAX`. It is the only throwing path in the library. |
| C5 | `template_generate.hpp` `generate` | In eap mode, explicit `units` are not checked against `eapPtr->nClasses`; an out-of-range id reads past the row (`cells[i*T + u]`). |
| C6 | `decollide.hpp` `applyMethod` | The int16 conversion is `static_cast<int16_t>(std::nearbyint(x))` with no saturation; a reconstructed sample outside the int16 range (possible with a large amplitude subtracting an opposite-sign template) is an out-of-range float-to-integer conversion. |
| C7 | `spike_extract.hpp` | **Fixed (0489).** Offsets went through `long` (`std::fseek` / `std::ftell`), so on Windows windows past 2 GiB were emitted as zeros; now 64-bit `fseeko` / `_fseeki64`. |
| C8 | `projectionscope.hpp` / `decollide_eap.hpp` | `projectionscope::classForUnit` and `decollide::eapClassForUnit` are two copies of the same lookup in one library. |
| C9 | `drift_partition.hpp` | `detail::timeOf` and `detail::medianTime` are unused anywhere in the repository. `regrainForest` flattens any deeper tree (a leaf's child is re-parented to the region root) and turns a non-drift root (`parent < 0`, kind not `drift*`) into a child of region 0. |
| C10 | `custody.hpp` `staleAfterRealign` | Unused by any C++ code (only the Python mirror's `stale_after_realign` exists). |
| C11 | `test/CMakeLists.txt` | The core tests ask for `cxx_std_17`, but `CMAKE_CXX_STANDARD 20` is set for the whole `libneurosuite-core` directory, so they compile as C++20 and do not prove the C++17 compatibility that `pca_projection.hpp` and the plugins rely on. |

**Comments and documentation**

| # | Where | Issue |
|---|---|---|
| D1 | `custody.hpp` file comment (Shared class) | Says "the raw .spk is shared, the stderiv transform being applied downstream at PCA time rather than stored separately". Stale: `process_extractspikes_stderiv` writes transformed waveforms (`.spk.stderiv*.N`), and [naming.md](../../../doc/ndmanager-plugins/formats/naming.md#artifact-classes) correctly says `.spk` "carries a domain". The same "raw .spk is one physical copy" wording is repeated above `resolveAny`. |
| D2 | `custody.hpp` | The comment "Convenience: true if the resolved file is in the stderiv (transformed) domain…" sits above `resolveAny` instead of `resolvedIsStderiv`. The file comment says "std + `<fstream>` only", but `<filesystem>` is also used. |
| D3 | `template_generate.hpp` | The `renderLineage` block comment says each node is re-medianed from its own spikes, subsampled to `maxPer`, into per-variant `.wtf` stacks. The code copies the stored running mean into a single stack destined for `.mtf`. `LineageFileParams` comments ("one .wtf each", "stage tag for .wtl/.wti/.wtf") are also stale; `spkTag` is never read and only `variants[0]` is used. |
| D4 | `test/CMakeLists.txt` | The comments for `core_test_template_lineage` ("one median per node over its own spike set") and `core_test_template_lineage_io` ("render it to .wti (v2) + per-variant .wtf") describe the old behaviour; the test sources themselves are correct (mean copy, `.mti`/`.mtf`). The top-level `CMakeLists.txt` comment calls the test set "custody policy conformance" only. |
| D5 | `custody_vectors.tsv` header, `custody_conformance_test.cpp` | Both describe the bash runner as "(future)"; it exists and is registered as `custody_conformance_bash`. The table header's kind list omits `resolve_any` (documented further down). |
| D6 | `test_shared_contract.py` | The docstring says it compares against "the hashes recorded below"; no hashes are recorded, so without `FK_ROOT` it always passes. Its usage text says `FK_ROOT=/path/to/neurosuite-3`, but `FK_ROOT` must point at fiber-kit (a rename of fiber-kit's `NS3_ROOT` copy). It is not run by ctest. |
| D7 | `pca_projection.hpp` | Refers to `docs/pca-align-refine-behavioral-diff.md`, which does not exist in the repository. States that the `sampleShift` equality "is asserted in the unit test"; no test calls `pcaProjectScores` or uses `sampleShift`. |
| D8 | `stderiv_transform.hpp`, `spike_extract.hpp` | Both present themselves as the single implementation, but `process_extractspikes_stderiv`, `process_pca_stderiv` and `process_reextractspikes_stderiv` still use their own `computeSDiff` and window-cutting code; only Klusters calls the headers. |
| D9 | `neurofileio.h` `.wtl` comment | Says the node's spike set is "the source of truth" that "the native generator re-medians", and attributes the parallel-variance combine to `template_generate.hpp`. In v2 there are no spike sets, rendering copies the mean, and the combine is in `running_stats.hpp`. [FILE_FORMATS §4.5](FILE_FORMATS.md#45-wtl--manual-template-lineage-forest-version-tagged-text) repeats "re-medians". |
| D10 | `neurofileio.h` "Mandatory-method resolution" comment | Says there is no untagged form for `res`/`spk`/`clu`/`fet`/`pca`; `custody::resolve`/`resolveAny` still fall back to the untagged name for the Shared types `res` and `spk`. |
| D11 | FILE_FORMATS.md | Line citations into `custody.hpp` are stale (`custody.hpp:140-152` and `:149-152` for the type tables; they are now around lines 155–169). §4.7 says `.mti`/`.mtf` are written by "klusters' `renderLineageToFiles`" and marks it unverified; the writer is `templategen::renderLineageToFiles` in this library (`template_generate.hpp`), called from klusters' `templatelineagestore.cpp`. |
| D12 | `src/libneurosuite-core/src/CMakeLists.txt` | The comment says the library "currently" holds `neurofileio`; it now holds twelve headers. [ARCHITECTURE.md](../../../doc/dev/ARCHITECTURE.md) §0/§2.2 still places `neurofileio` in `libklustersshared` (`klustersshared/neurofileio.{h,cpp}`) and does not mention `libneurosuite-core`. |
| D13 | `src/klusters/tests/CMakeLists.txt` | Says "the templateclass engine" and "the renderLineage* engine" live out of line in `Neurosuite::core`; both are header-only. The link is needed for the `neurofileio` readers/writers. |
| D14 | `templategen::sessionPath` vs `custody::stagePath` | Two path composers for the same name shape: `templategen::sessionPath` folds `.` in the tag to `_`, `custody::stagePath` does not. A stage containing a dot produces different file names depending on which one a caller uses. `templategen::sessionPath` also shares its name with the unrelated `custody::sessionPath(base, type)`. |
