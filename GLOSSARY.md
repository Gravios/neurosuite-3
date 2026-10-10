# neurosuite-3 glossary

A quick reference for the component names, domain terms, on-disk formats, and
architecture concepts used across the codebase and its documentation. Each entry
points to the authoritative reference where one exists; the code is always the
final word.

---

## Components

| Term | Meaning |
|---|---|
| **klusters** | The interactive spike-sorting / cluster-curation GUI (the primary app). Internals: [`src/klusters/docs/`](src/klusters/docs/). |
| **NeuroScope** | The signal/trace and event viewer (binary `neuroscope`). Shares the `input::` engine with klusters ([`src/neuroscope/docs/INPUT_SYSTEM.md`](src/neuroscope/docs/INPUT_SYSTEM.md)). |
| **ndmanager** | Session/parameter manager — edits the YAML parameter file and launches the pipeline. |
| **ndmanager-plugins** | The headless preprocessing pipeline: the `ndm_*` / `process_*` command-line tools (filtering, PCA, spike extraction, collision decomposition). No Qt. |
| **kiloklustakwik** | The standalone spike sorter — the shift-probe refeaturization engine, formerly the experimental KlustaKwikExp fork, now the canonical and only sorter. See `src/kiloklustakwik/CHANGES.md`. |
| **libklustersshared** (**neurosuite-qt**) | The shared Qt6 library (the `input::` engine, shared GUI widgets such as `DockArea`, xcorr). Target renamed `neurosuite-qt`; still exported as `KlustersShared::klustersshared`. |
| **libneurosuite-core** (**Neurosuite::core**) | The Qt-free core library; home of `neurofileio`, the authoritative on-disk format reader/writer ([`src/libneurosuite-core/docs/FILE_FORMATS.md`](src/libneurosuite-core/docs/FILE_FORMATS.md)). |
| **nphys-data** | MIME types and desktop icons; builds with no compiler. |
| **fiber-kit** | Sibling repository (`github.com/Gravios/fiber-kit`). Its `neuro_io` mirrors `neurofileio`'s on-disk contract byte-for-byte (FILE_FORMATS §5). |
| **KlustaKwik** | The *upstream* engine (Kenneth Harris'); distinct from this fork's **KiloKlustaKwik**. Standalone "KlustaKwik" usually means the upstream tool. |

---

## Domain terms

| Term | Meaning |
|---|---|
| **spike** | One detected extracellular event; index *i* is the same spike across the `.res`/`.clu`/`.fet`/`.spk` files of a group. |
| **cluster** | A set of spikes assigned the same id. By convention id `0` = noise, `1` = unsorted MUA, `≥2` = candidate single units. |
| **spike group / electrode group** | The channel group a set of spikes was detected on; the trailing `.N` on a per-group file names it. |
| **feature / feature vector** | The per-spike coordinates (usually PCA components, last column = timestamp) that curation clusters in; stored in `.fet`, held in `Data::features`. |
| **PCA** | Principal-component basis used to featurize waveforms; the per-group basis is the `.pca` file. |
| **waveform** | A spike's windowed voltage snippet (`nSamples × nChannels`), stored in `.spk`, read on the fly (DOCUMENT_MODEL §2.4). |
| **MUA** | Multi-unit activity — unsorted spikes (cluster id `1`). |
| **isolation distance** | A per-cluster separation quality metric (`ClusterInfo::ID`). |
| **EAP** | Extracellular action potential — a template class's characteristic waveform; the `.eap` matrix records each spike's offset to each class. |
| **template / template-class** | A stable waveform identity that coexists with `.clu` and survives cluster-id churn; a template-class id **is** its `.eap` column index. |
| **template library** | The generated set of templates and their index: the `.eap`/`.tcl`/`.wti`/`.wtf`/`.wtl` family (FILE_FORMATS §4). |
| **lineage** | The curator's forest of template nodes (`.wtl`), one tree per class, that the generator re-medians into templates. |
| **drift** | Slow change of a unit's waveform over a session (electrode movement); tracked by the drift matrix and drift-root lineage nodes. |
| **collision** | Two spikes overlapping in time so their waveforms sum; **decollide** is the process (`process_decomposecollisions`) that separates them, its accepted results in `.col`. |
| **curation** | The interactive editing of the clustering: merge/group, split, recluster, watershed, dip-split, rename/renumber, delete. |
| **recluster / watershed / dip-split** | Operations that create >1 cluster from existing spikes; each emits `newClustersAdded(QList)` once (DOCUMENT_MODEL §4.3, §6.1). |
| **realign / nudge** | Rewriting spike peak alignment; writes the pending `.spk`/`.res`/`.fet` scratch files and preserves the spike-file invariant (DOCUMENT_MODEL §7.4, CONCURRENCY §7.2). |
| **correlogram** | Auto-/cross-correlogram — the spike-timing histogram shown by `CorrelationView`. |
| **error / confusion matrix** | The pairwise mis-assignment probability matrix (`ErrorMatrixView`); the template / residual / drift matrices are its siblings (VIEW_SYSTEM §5). |

---

## On-disk formats

Authoritative spec: [`src/libneurosuite-core/docs/FILE_FORMATS.md`](src/libneurosuite-core/docs/FILE_FORMATS.md). Binary formats are little-endian.

| Ext | What |
|---|---|
| `.dat` / `.fil` / `.eeg` / `.lfp` | Interleaved `int16` signal — raw, filtered, and LFP (`.eeg` = legacy name for `.lfp`). |
| `.res(.N)` | `int64` sample-index timestamps, one per spike. |
| `.clu(.N)` / `.clc(.N)` | `int32` cluster ids (first value = count). `.clc` is the child/hierarchy clustering. |
| `.fet(.N)` | Feature vectors — binary body is **`int64`**, row-major. |
| `.spk(.N)` | `int16` waveforms, sample-major; geometry supplied by the caller, no header. |
| `.evt` | Text events: `double` ms time + label. |
| `.col(.N)` | **Binary** accepted-collision decompositions (magic `COL\x01`) — *not* YAML. |
| `.eap(.N)` | `int8` membership/offset matrix (32-byte header; `EAP_ABSENT = -128`). |
| `.tcl(.N)` | Template-class registry (TAB-separated text; status `free/active/tomb/merged`). |
| `.wti(.N)` / `.wtf(.N)` | Template index (text, v1/v2) and its waveform stack (`.spk` layout, read by `readSpk`). |
| `.wtl(.N)` | Manual template-lineage forest (text; v2 stores running mean/std/count). |
| `.pca(.N)` | PCA eigenvector basis (read by `core::loadPca`, not `neurofileio`). |
| `.mti` / `.mtf` | The final committed template model — reuses the `.wti` v2 / `.spk` schema; the distinction is role, not byte layout. |
| `.yaml` (`.par`, `.nrs`) | Session parameters (YAML is current; `.par`/`.nrs` are legacy). |

---

## Architecture concepts

| Term | Meaning |
|---|---|
| **document / view** | The split between the model (`KlustersDoc` + `Data`) and its on-screen presenters. DOCUMENT_MODEL §1. |
| **`KlustersDoc`** | The document layer: owns presentation state (colors, status, undo, hierarchy) and drives every curation op; delegates the membership model to `Data`. |
| **`Data`** | The spike/feature/cluster store: `features`, `spikesByCluster`, `clusterInfoMap`. Non-copyable; versioned as epochs. |
| **epoch / snapshot** | An immutable `ClusteringSnapshot` of the membership model; background jobs pin the current epoch so a later edit can't tear tables under them. DOCUMENT_MODEL §3, CONCURRENCY §3. |
| **`prepareUndo`** | The single `Data` commit point: snapshot-then-swap atomically. Never pre-mutate the live tables before calling it (STANDARDIZATION §2.5, §6.2). |
| **`applyClusterRename`** | The one apply-path for renaming/renumbering a set of clusters (DOCUMENT_MODEL §4.2). |
| **`BaseFrame` / `BufferedView` / `ViewWidget`** | The drawing-view base chain: zoom/coordinate frame → off-screen double buffer → document/view refs + cluster-edit slots. VIEW_SYSTEM §1–2. |
| **`KlustersView` / `DockArea`** | The per-document view container; `DockArea` is a `QScrollArea` hosting an inner `QMainWindow` of dockable views. VIEW_SYSTEM §3. |
| **`KlustersJobPool`** | The two process-wide `QThreadPool` lanes (`pool()` + `interactivePool()`) that run background compute. CONCURRENCY §1. |
| **`KlustersJobToken`** | The per-view cancellation/completion token (`generation`, `active`, `viewDead` + `postMutex`) a view shares with its jobs. CONCURRENCY §2. |
| **supersede vs stop** | Retiring in-flight jobs: `supersede` bumps the generation (non-blocking, the edit-path default); `stop` blocks until jobs retire. CONCURRENCY §4. |
| **`RequestTicket`** | The "subscribe, don't block" handle: a job registers a waiter on a computation another job owns instead of sleep-polling. CONCURRENCY §5. |
| **`SerialJobQueue`** | The one-at-a-time lane that serializes realign so its pending-file writes can't race. CONCURRENCY §7.1. |
| **`input::` engine** | The shared keyboard+mouse binding registry: scopes, the tiered scope stack, chords, capture/focus/repeat policies, and the derived cheat-sheet. INPUT_SYSTEM. |
| **scope / chord / cheat-sheet** | A binding's applicability context; a multi-key/mouse combination; the generated key reference. INPUT_SYSTEM. |
| **chain-of-custody / method / group** | Per-group file naming: `<base>.<type>.<method>.<group>` where *method* is the variant tag and *group* the trailing all-digit field. FILE_FORMATS §1.1. |
| **group (curation)** | The G-key "group/merge" operation that combines selected clusters — distinct from a *spike group*. |

---

## Build / CMake

| Term | Meaning |
|---|---|
| **`ns_add_subdir`** | The top-level macro that adds a component with `NS_SKIP_<NAME>` support. |
| **`NS_SKIP_<NAME>`** | Skip one component at configure time (BUILD.md §5). |
| **`NS_BUILD_TESTS`** | Opt-in; builds the ctest regression suite (BUILD.md §6). |
| **`NS_ASAN`** | Opt-in; AddressSanitizer host build (BUILD.md §7). |
| **`USE_CUDA` / `USE_HIP` / `USE_SYCL`** | The three optional, auto-detected GPU backends; all fall back to CPU/OpenMP (BUILD.md §4). |
| **`NS_INSTALL_DEPS`** | Install system dependencies before building (apt/Homebrew/vcpkg). |

---

See [`DEVELOPER_GUIDE.md`](DEVELOPER_GUIDE.md) for the full internals map,
[`BUILD.md`](BUILD.md) for building, and [`STANDARDIZATION.md`](STANDARDIZATION.md)
for the coding and layout standards.
