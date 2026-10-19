# Neurosuite-3 Documentation

A modernized, Qt6-compatible fork of the Neurosuite electrophysiology
toolchain. All components compile under **C++20** with Qt 6; supported
platforms (Linux, WSL2, native Windows, macOS) and their limits are in
[INSTALL.md](INSTALL.md). GPU acceleration is available for
compute-intensive steps via CUDA (NVIDIA), HIP (AMD ROCm), and SYCL
(Intel Arc/oneAPI).

**Contents:** [Programs](#programs) · [Pipeline variants — raw vs stderiv](#pipeline-variants--raw-vs-stderiv) · [GPU Acceleration](#gpu-acceleration) · [Build order](#build-order) · [Typical workflow](#typical-workflow) · [Parameter file format](#parameter-file-format) · [Documentation map](#documentation-map) · [Changelog and migration notes](#changelog-and-migration-notes)

---

## Programs

### [libklustersshared](libklustersshared/README.md)

Shared library providing the session-file YAML implementation used by the
GUIs: `ParameterYamlReader`, `ParameterYamlWriter`, and
`ParameterYamlModifier`, plus shared data types, the `input::` key/mouse
engine and GUI widget infrastructure. ndmanager, klusters and neuroscope
all depend on it. YAML is also parsed elsewhere — by KiloKlustaKwik
(`KlustaKwikYaml.cpp`, `KK_prior.cpp`), by ndmanager's own readers and
pages, by NeuroScope's `sessionyamlreader.cpp`, and by the `ndm_*`
scripts.

**Depends on:** Qt6, yaml-cpp

---

### [ndmanager](ndmanager/README.md)

GUI session manager. Opens a `.yaml` parameter file (a legacy `.xml` is
refused with a message to convert it with `ndm_xml2yaml`) and provides a tabbed interface for editing all acquisition and processing
parameters. Launches the `ndmanager-plugins` preprocessing pipeline
step-by-step or as a full batch. The central starting point for any
recording session.

**Depends on:** Qt6, libklustersshared, yaml-cpp

---

### [ndmanager-plugins](ndmanager-plugins/README.md)

Command-line preprocessing pipeline. Converts raw acquisition data
(AlphaOmega, Plexon, Neuralynx, CED/Spike2, Amplipex) into spike-sorting-ready
files through format conversion, high-pass filtering (zero-phase
Butterworth by default; the median filter with `filter: median`), LFP
downsampling, spike detection, waveform extraction, and PCA feature
extraction.

Beyond initial preprocessing, ndmanager-plugins also provides the
post-sorting analysis pipeline:

- **Second-pass re-extraction** — `ndm_reextractspikes [--method M]` runs
  a masked second detection pass at a lower threshold, shadow-clusters
  new spikes into existing cluster shadows, and optionally subdivides
  the unmatched bin via `ndm_subcluster_unmatched`.
- **Waveform subtraction** — `ndm_stripdat` produces a cleaned `.dat`
  via per-cluster template subtraction (raw / model / BOTM modes) to
  enable LFP analysis and iterative sort refinement.
- **Drift estimation & application** — `ndm_estimatedrift` infers probe
  drift from curated spike sorting; `ndm_applydrift` propagates drift
  to sibling shanks via adaptive chunk boundaries.
- **Collision decomposition & source localization** — `ndm_decomposecollisions`
  and `ndm_localise` for post-hoc analysis of multi-unit events and
  spike source positions.

Several steps optionally use GPU acceleration.

**Depends on:** CMake, OpenMP, libxml2 (`xpathReader`), libhdf5
(AlphaOmega conversion), yaml-cpp, numpy. CUDA optional for
`process_medianfilter`, `process_medianthreshold`, `process_butterworth`,
`process_lowpass`, `process_spikegrouper`, `process_decomposecollisions`
and `process_extractemg`.

---

### [neuroscope](neuroscope/README.md)

Multi-channel signal visualiser. Displays wideband, LFP, and high-pass
filtered traces as scrollable panels with simultaneous overlays of
spike waveforms, cluster assignments, event markers, and animal position.
Driven from the session `.yaml` parameter file.

**Depends on:** Qt6, libklustersshared, yaml-cpp, FFmpeg (optional)

---

### [klusters](klusters/README.md)

Interactive manual spike-sorting GUI. Loads feature, cluster, waveform,
and parameter files for one electrode group at a time and provides
scatter-plot, waveform, autocorrelogram, template-similarity-matrix,
and grouping-assistant views for cluster inspection, splitting, merging,
and reassignment.

Integrates with KiloKlustaKwik for in-app automatic reclustering, with
automatic feature selection (variance-ranked, noise-floor trimmed,
multi-cluster aware). Interactive spike realignment via normalized
cross-correlation and per-sample timestamp nudging are also available
— both use a transactional pending-file model so edits can be
accepted or rolled back in one atomic step. Variant-aware: resolves every file through the shared method-tagged
naming convention (`standard` / `sdiff` / `stderiv`).

Supports **hierarchical (two-level) sessions** — fibers assembled from an
over-split atom layer (`.clu` + `.clc` + `.clp`) — a **residual
separability matrix** view, and a range of **cluster sorting/reordering**
methods (similarity seriation, feature-space, and waveform
nearest-neighbor / spectral). See
[Hierarchical clustering](klusters/hierarchical-clustering.md) and
[Cluster sorting and reordering](klusters/cluster-sorting.md).

**Depends on:** Qt6, libklustersshared. CUDA/ROCm/oneAPI optional for
Grouping Assistant matrix computation and realignment cross-correlation.

---

### [klustakwik](kiloklustakwik/README.md)

Automatic spike sorter using Classification EM (CEM). Reads a
`.fet.<method>.N` feature file and writes a `.clu.<method>.N` cluster
assignment file.
The default chunked-CEM pipeline runs in five phases (Phase 0–2 plus
waveform realignment as Phase 1.5 and subspace reclustering as Phase
2.5) — designed for multi-hour recordings with electrode drift. A
short-recording two-phase mode is available by setting `ChunkMinutes 0`.

Input files are resolved by the `Method` parameter
(`pickInputPath` → `custody::resolve`), so groups sorted with different
methods coexist in one session; there is no fallback to the retired
D-suffix names.

GPU acceleration available via CUDA, HIP (AMD ROCm), or SYCL (Intel
Arc/oneAPI).

**Depends on:** C++20 compiler, CMake, OpenMP (parallel chunking /
runs / items), yaml-cpp. CUDA / ROCm / oneAPI optional.

---

### [spikerealign](spikerealign/README.md)

Waveform realignment engine, not a separate program or build target: it
is compiled into klusters (interactive realignment) and KiloKlustaKwik
(Phase 1.5 of chunked sorting). It aligns each spike to the cluster mean
template via normalized cross-correlation and updates the
`.res`/`.spk`/`.fet` files. GPU dispatch (CUDA / HIP / SYCL, OpenMP
fallback) follows the host program's backends.

---

## Pipeline variants — raw vs stderiv

Three detection transforms (**methods**) are supported across the toolchain:
`standard` (raw waveforms), `sdiff` (spatial difference) and `stderiv`
(spatial + temporal derivative); `stderiv` also takes an order suffix
(`stderiv_S<order>`, `stderiv_C<order>`).
The method is an explicit tag in every per-group filename —
`<base>.<type>.<method>.<group>` — under the shared
[variant naming convention](ndmanager-plugins/formats/naming.md):

| `standard` | `stderiv` |
|---|---|
| `.fet.standard.N` — PCA on raw waveforms | `.fet.stderiv.N` — PCA on the stderiv transform |
| `.pca.standard.N` — raw eigenvectors | `.pca.stderiv.N` — stderiv eigenvectors |

`.res` is **shared** across methods (one physical copy per group, whatever
token wrote it). `.spk` is shared in class but domain-carrying: the stderiv
transform is applied at extraction, so a stderiv session has its own
`.spk.stderiv.N` distinct from `.spk.standard.N`. `.clu` / `.clc` cluster files are
method-specific (`.clu.standard.N`, `.clu.stderiv.N`). The old
`.spkD` / `.fetD` / `.pcaD` "D-suffix" scheme is retired.

Within a single session, groups can mix pipelines. Every downstream
tool (klusters, KiloKlustaKwik, shadowcluster, nudge, realign) auto-detects
the variant per group and applies the correct transforms. See the
ndmanager-plugins and KiloKlustaKwik READMEs for details.

---

## GPU Acceleration

GPU acceleration is available for kiloklustakwik, klusters (Grouping
Assistant and realignment, including the built-in spikerealign engine),
and seven ndmanager-plugins programs (`process_medianfilter`,
`process_medianthreshold`, `process_butterworth`, `process_lowpass`,
`process_spikegrouper`, `process_decomposecollisions`,
`process_extractemg`). All GPU backends
are auto-detected at build time; a CPU/OpenMP fallback is always
compiled.

See the **[GPU installation guide](gpu/README.md)** for:

- NVIDIA CUDA — including CUDA 12.8 for RTX 5000 series (Blackwell /
  sm_120) and Secure Boot MOK key signing
- AMD ROCm / HIP
- Intel oneAPI / SYCL (bare metal and WSL2)
- Runtime backend selection and verification

---

## Build order

One CMake build at the top of the repository builds every component in this
order; the components are not installed one by one. Installation, per
platform: [INSTALL.md](INSTALL.md).

```
nphys-data            (MIME types + desktop icons; no compiler)
libneurosuite-core    (Qt-free core: neurofileio, …)
libklustersshared     (shared Qt6 library; links Neurosuite::core)
  ├── klusters
  ├── neuroscope
  └── ndmanager
ndmanager-plugins     (preprocessing pipeline; no Qt)
kiloklustakwik        (standalone spike sorter)
```

This is the graph in [BUILD.md §5](BUILD.md#5-the-build-graph-and-skip-flags),
which also lists the skip flags. See each program's README for its own
dependencies and options.

---

## Typical workflow

```
Raw acquisition files (.mat / .ncs / .smr / .dat)
        │
        ▼
ndmanager session.yaml        ← edit parameters, then run pipeline
        │  (calls ndm_start internally, which drives ndmanager-plugins)
        ▼
session.dat → .fil → .res.<method>.N + .spk.<method>.N
                       │
                       ▼
                    .fet.<method>.N
        │
        ▼
ndm_klustakwik session.yaml    ← automatic cluster assignment (KiloKlustaKwik)
        │
        ▼
session.clu.<method>.N
        │
        ▼
klusters session.yaml         ← manual curation (split / merge / realign / nudge / dipsplit)
        │  writes session.curation_log.<group>.<method> per-group audit trail
        ▼
ndm_reextractspikes + ndm_subcluster_unmatched
        │     ↑ kk_resolve_prior.py picks up <probe_id>.<group>.prior.yaml
        │       (built once via kk_build_prior.py from curation logs)
ndm_stripdat (raw / model / botm)
ndm_decomposecollisions
ndm_estimatedrift + ndm_applydrift
ndm_localise
        │
        ▼
neuroscope session.yaml       ← validation against LFP / events / position
```

The post-sorting steps can be run in any order, and the results fed
back into another klusters curation pass. `ndm_stripdat` +
`ndm_redetectspikes` + `ndm_pca` + `ndm_klustakwik` is a common
iterative-refinement loop.

For task-oriented walkthroughs of the recipes above, see
[`workflows/`](workflows/README.md):

- [`workflows/first-time-sort.md`](workflows/first-time-sort.md) — full first pass
- [`workflows/cluster-curation.md`](workflows/cluster-curation.md) — Klusters operations
- [`workflows/re-extract-lower-threshold.md`](workflows/re-extract-lower-threshold.md) — recover weak units
- [`workflows/iterative-refinement.md`](workflows/iterative-refinement.md) — strip → redetect → re-sort
- [`workflows/empirical-priors.md`](workflows/empirical-priors.md) — per-probe KK priors
- [`workflows/drift-correction.md`](workflows/drift-correction.md) — chronic-recording drift
- [`workflows/collision-decomposition.md`](workflows/collision-decomposition.md) — overlapping spikes

---

## Parameter file format

All tools share a single session `.yaml` parameter file. See
[ndmanager/README.md](ndmanager/README.md) for the full schema and a
worked example. The GUIs do not read legacy XML parameter files
(ndmanager refuses them; Klusters opens `.yaml` or the old `.par`
files); only the `ndm_*` scripts still fall back to reading a legacy
`.xml` through `xpathReader`. Convert legacy files with `ndm_xml2yaml`.

---

## Documentation map

<details>
<summary>Full directory tree</summary>

```
doc/
├── README.md                       ← this file
├── INSTALL.md                      ← installation, every platform
├── BUILD.md                        ← build options, platforms, GPU, CI parity
├── GLOSSARY.md                     ← terms used across the docs
├── dev/                            ← for contributors
│   ├── DEVELOPER_GUIDE.md          ← index of internals references
│   ├── ARCHITECTURE.md
│   ├── RECIPES.md                  ← which files a common change touches
│   ├── STANDARDIZATION.md          ← conventions + verification gates
│   ├── ROADMAP.md
│   └── PLUGINS-ROADMAP.md
├── history/                        ← past fix-bundle and session notes (archival)
├── workflows/                      ← task-oriented recipes
│   ├── first-time-sort.md
│   ├── cluster-curation.md
│   ├── re-extract-lower-threshold.md
│   ├── iterative-refinement.md
│   ├── empirical-priors.md
│   ├── drift-correction.md
│   └── collision-decomposition.md
├── design/                         ← deep technical references
│   ├── reextract-v2.md
│   ├── decomposecollisions.md
│   ├── subtractspikes-botm.md
│   ├── modeling-l1-vs-botm.md
│   ├── kk-prior.md
│   ├── optimization.md
│   └── ...
├── ndmanager-plugins/
│   ├── README.md                   ← thin index
│   ├── pipeline.md                 ← per-acquisition-system pipeline diagrams
│   ├── commands/<command>.md       ← per-command reference
│   └── formats/<format>.md         ← binary/YAML format reference
├── klusters/README.md
├── kiloklustakwik/README.md
├── ndmanager/README.md
├── neuroscope/README.md
├── spikerealign/README.md
├── libklustersshared/README.md
├── gpu/README.md
└── <component>/install/<platform>.md
```

</details>

---

## Changelog and migration notes

- [`../CHANGELOG.md`](../CHANGELOG.md) — top-level consolidated
  changelog. Dated entries from most recent at top, each linking to
  a `doc/design/<topic>.md` for the design discussion.
- [`history/`](history/README.md) — archival notes from past fix bundles and
  working sessions (may not match the current tree).
- [`design/`](design/README.md) — durable design references.
  Indexed in the changelog by date; indexed by topic in
  [`design/README.md`](design/README.md).
- [`../src/kiloklustakwik/CHANGES-inherited-from-canonical.md`](../src/kiloklustakwik/CHANGES-inherited-from-canonical.md) —
  inherited canonical-engine history (v1.7 → neurosuite-3 diff). Lives in
  the source tree because it tracks code-internal changes.
- [`../src/kiloklustakwik/CHANGES.md`](../src/kiloklustakwik/CHANGES.md) —
  KiloKlustaKwik-internal changes (DipSplit, time-shift merging).
