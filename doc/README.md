# Neurosuite-3 Documentation

A modernised, Qt6-compatible fork of the Neurosuite electrophysiology
toolchain. All components compile under **C++20** with Qt 6 on Ubuntu
24.04, Debian 12, and WSL2. GPU acceleration is available for
compute-intensive steps via CUDA (NVIDIA), HIP (AMD ROCm), and SYCL
(Intel Arc/oneAPI).

---

## Programs

### [libklustersshared](libklustersshared/README.md)

Shared library providing the canonical YAML parameter-file implementation.
Contains `ParameterYamlReader`, `ParameterYamlWriter`, and
`ParameterYamlModifier` — the only classes that parse or write YAML in the
entire toolchain — plus shared data types and GUI widget infrastructure.
ndmanager and klusters both depend on it; neuroscope does not.

**Depends on:** Qt6, yaml-cpp

---

### [ndmanager](ndmanager/README.md)

GUI session manager. Opens a `.yaml` (or legacy `.xml`) parameter file
and provides a tabbed interface for editing all acquisition and processing
parameters. Launches the `ndmanager-plugins` preprocessing pipeline
step-by-step or as a full batch. The central starting point for any
recording session.

**Depends on:** Qt6, libklustersshared, libxml2, yaml-cpp

---

### [ndmanager-plugins](ndmanager-plugins/README.md)

Command-line preprocessing pipeline. Converts raw acquisition data
(AlphaOmega, Neuralynx, CED/Spike2, Amplipex) into spike-sorting-ready
files through format conversion, median-subtraction high-pass filtering,
LFP downsampling, spike detection, and PCA feature extraction.

Beyond initial preprocessing, ndmanager-plugins also provides the
post-sorting analysis pipeline:

- **Second-pass re-extraction** — `ndm_reextractspikes{,_stderiv}` runs
  a masked second detection pass at a lower threshold, shadow-clusters
  new spikes into existing cluster shadows, and optionally subdivides
  the unmatched bin via `ndm_subcluster_unmatched`.
- **Waveform subtraction** — `ndm_stripdat` produces a cleaned `.dat`
  via per-cluster template subtraction (raw / model / BOTM modes) to
  enable LFP analysis and iterative sort refinement.
- **Drift estimation & application** — `ndm_estimatedrift` infers probe
  drift from curated spike sorting; `ndm_applydrift` propagates drift
  to sibling shanks via adaptive chunk boundaries.
- **Collision decomposition & source localisation** — `ndm_decomposecollisions`
  and `ndm_localise` for post-hoc analysis of multi-unit events and
  spike source positions.

The filter and threshold steps optionally use GPU acceleration.

**Depends on:** CMake, OpenMP, libhdf5 (AlphaOmega conversion), yaml-cpp,
numpy. CUDA optional for `process_medianfilter`, `process_medianthreshold`.

---

### [neuroscope](neuroscope/README.md)

Multi-channel signal visualiser. Displays wideband, LFP, and high-pass
filtered traces as scrollable panels with simultaneous overlays of
spike waveforms, cluster assignments, event markers, and animal position.
Driven from the session `.yaml` parameter file.

**Depends on:** Qt6, yaml-cpp, FFmpeg (optional)

---

### [klusters](klusters/README.md)

Interactive manual spike-sorting GUI. Loads feature, cluster, waveform,
and parameter files for one electrode group at a time and provides
scatter-plot, waveform, autocorrelogram, template-similarity-matrix,
and grouping-assistant views for cluster inspection, splitting, merging,
and reassignment.

Integrates with KlustaKwik for in-app automatic reclustering, with
automatic feature selection (variance-ranked, noise-floor trimmed,
multi-cluster aware). Interactive spike realignment via normalised
cross-correlation and per-sample timestamp nudging are also available
— both use a transactional pending-file model so edits can be
accepted or rolled back in one atomic step. Pipeline-variant aware:
correctly handles raw `.spk` / stderiv `.fetD` mixed configurations.

**Depends on:** Qt6, libklustersshared. CUDA/ROCm/oneAPI optional for
Grouping Assistant matrix computation and realignment cross-correlation.

---

### [klustakwik](klustakwik/README.md)

Automatic spike sorter using Classification EM (CEM). Reads a `.fet.N`
or `.fetD.N` feature file and writes a `.clu.N` cluster assignment file.
The default chunked-CEM pipeline runs in five phases (Phase 0–2 plus
waveform realignment as Phase 1.5 and subspace reclustering as Phase
2.5) — designed for multi-hour recordings with electrode drift. A
short-recording two-phase mode is available by setting `ChunkMinutes 0`.

File-extension fallback (`pickInputPath`) lets stderiv-sorted and
raw-sorted groups coexist within a single session without manual
symlink shims.

GPU acceleration available via CUDA, HIP (AMD ROCm), or SYCL (Intel
Arc/oneAPI).

**Depends on:** C++20 compiler, CMake, OpenMP (parallel chunking /
runs / items), yaml-cpp. CUDA / ROCm / oneAPI optional.

---

### [spikerealign](spikerealign/README.md)

Standalone batch waveform realignment tool. Reads binary
`.spk/.res/.clu/.fet` files, aligns each spike to the cluster mean
template via normalised cross-correlation, and writes corrected data
back in-place. Shares the GPU backend selection with KlustaKwik. The
same algorithm runs as Phase 1.5 of chunked KlustaKwik sorting and as
the interactive realignment inside klusters.

**Depends on:** C++20 compiler, CMake. OpenMP, CUDA/ROCm/oneAPI optional.

---

## Pipeline variants — raw vs stderiv

Two detection transforms are supported across the toolchain. The choice
is encoded in the filename extension of every downstream artefact:

| Canonical (raw) | stderiv D variant |
|---|---|
| `.spk.N` — raw waveform | `.spkD.N` — spatial + temporal derivative |
| `.fet.N` — PCA on raw | `.fetD.N` — PCA on stderiv |
| `.pca.N` — raw eigenvectors | `.pcaD.N` — stderiv eigenvectors |

`.res.N` (timestamps) and `.clu.N` (cluster IDs) have no variant — they
are pipeline-neutral.

Within a single session, groups can mix pipelines. Every downstream
tool (klusters, KlustaKwik, shadowcluster, nudge, realign) auto-detects
the variant per group and applies the correct transforms. See the
ndmanager-plugins and KlustaKwik READMEs for details.

---

## GPU Acceleration

GPU acceleration is available for klustakwik, spikerealign, klusters
(Grouping Assistant and realignment), and several ndmanager-plugins
(`process_medianfilter`, `process_medianthreshold`). All GPU backends
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

`libklustersshared` must be installed before `ndmanager` or `klusters`.
Everything else is independent.

```
libklustersshared → ndmanager
                 → klusters
ndmanager-plugins  (independent)
neuroscope         (independent — does not use libklustersshared)
klustakwik         (independent)
spikerealign       (independent)
```

The top-level `CMakeLists.txt` is a superbuild that builds and installs
all components in dependency order. See each program's README for
dependency details and its `install/` subdirectory for platform-specific
build instructions.

---

## Typical workflow

```
Raw acquisition files (.mat / .ncs / .smr / .dat)
        │
        ▼
ndmanager session.yaml        ← edit parameters, then run pipeline
        │  (calls ndm_start internally, which drives ndmanager-plugins)
        ▼
session.dat → .fil → .res.N + .spk.N (or .spkD.N)
                       │
                       ▼
                    .fet.N (or .fetD.N)
        │
        ▼
KlustaKwik session N         ← automatic cluster assignment
        │
        ▼
session.clu.N
        │
        ▼
klusters session.yaml         ← manual curation (split / merge / realign / nudge)
        │
        ▼
ndm_reextractspikes{,_stderiv} + ndm_subcluster_unmatched
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

---

## Parameter file format

All tools share a single session `.yaml` parameter file. See
[ndmanager/README.md](ndmanager/README.md) for the full schema and a
worked example. Legacy XML parameter files must be converted with
`ndm_xml2yaml` before use — the preprocessing pipeline has no
XML-reading path. klusters and neuroscope read XML only at open time
for backward compatibility; they always save YAML.

---

## Changelog and migration notes

- `../CHANGES.md` — top-level consolidated changelog, dated entries
  from most recent at top, with an index of per-topic detail files
  (`CHANGES-*.md` at the repo root)
- `../src/klustakwik/CHANGES.md` — KlustaKwik internals: every change
  from v1.7 → neurosuite-3, including the chunked-CEM pipeline, GPU
  dispatch, Phase 1.5 realignment, Phase 2.5 subspace reclustering,
  and the `pickInputPath` fallback
- `../OPTIMIZE.md` — hardware and OS tuning recipe
- `../modeling-recommendations.md` — spike-subtraction modelling
  comparison (Layer 1/2 vs BOTM)
