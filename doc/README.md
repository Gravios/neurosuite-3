# Neurosuite-3 Documentation

A modernised, Qt6-compatible fork of the Neurosuite electrophysiology toolchain. All components compile under C++17 with Qt 6 on Ubuntu 24.04, Debian 12, and WSL2. GPU acceleration is available for compute-intensive steps.

---

## Programs

### [ndmanager](ndmanager/README.md)

GUI session manager. Opens a `.xml` (or `.yaml`) parameter file and provides a tabbed interface for editing all acquisition and processing parameters. Launches the `ndmanager-plugins` preprocessing pipeline step-by-step or as a full batch. The central starting point for any recording session.

**Depends on:** Qt6, libklustersshared, libxml2, yaml-cpp

---

### [ndmanager-plugins](ndmanager-plugins/README.md)

Command-line preprocessing pipeline. Converts raw acquisition data (Neuralynx, CED/Spike2, Amplipex) into spike-sorting-ready files through format conversion, median-subtraction high-pass filtering, LFP downsampling, spike detection, and PCA feature extraction. The filter and threshold steps optionally use GPU acceleration.

**Depends on:** CMake, OpenMP, FFmpeg (optional, for video). CUDA/ROCm/oneAPI optional.

---

### [neuroscope](neuroscope/README.md)

Multi-channel signal visualiser. Displays wideband, LFP, and high-pass filtered traces as scrollable panels with simultaneous overlays of spike waveforms, cluster assignments, event markers, and animal position. Driven entirely from the session `.xml` parameter file.

**Depends on:** Qt6, libxml2, FFmpeg (optional)

---

### [klusters](klusters/README.md)

Interactive manual spike-sorting GUI. Loads feature, cluster, waveform, and parameter files for one electrode group at a time and provides scatter-plot, waveform, and autocorrelogram views for cluster inspection, splitting, merging, and reassignment. Integrates with KlustaKwik for in-app automatic reclustering.

**Depends on:** Qt6, libklustersshared

---

### [klustakwik](klustakwik/README.md)

Automatic spike sorter using Classification EM (CEM). Reads a `.fet` feature file and writes a `.clu` cluster assignment file. Supports two-phase farthest-point CEM and optional three-phase temporal chunking for long recordings with electrode drift. GPU acceleration available via CUDA, HIP (AMD ROCm), or SYCL (Intel Arc/oneAPI).

**Depends on:** C++17 compiler, CMake. OpenMP (parallel chunking), CUDA/ROCm/oneAPI (optional GPU).

---

### [spikerealign](spikerealign/README.md)

Standalone batch waveform realignment tool. Reads binary `.spk/.res/.clu/.fet` files, aligns each spike to the cluster mean template via normalised cross-correlation, and writes corrected data back in-place. Shares GPU backend selection with KlustaKwik. Intended for batch processing outside the klusters GUI; the klusters interactive realignment uses the same algorithm internally.

**Depends on:** C++17 compiler, CMake. OpenMP, CUDA/ROCm/oneAPI (optional GPU).

---

## Build order

`libklustersshared` must be installed before `ndmanager` or `klusters`. Everything else is independent.

```
libklustersshared → ndmanager
                 → klusters
ndmanager-plugins  (independent)
neuroscope         (independent)
klustakwik         (independent)
spikerealign       (independent)
```

See each program's README for dependency details and its `install/` subdirectory for platform-specific build instructions.

---

## Typical workflow

```
Raw acquisition files
        │
        ▼
ndmanager session.xml        ← edit parameters, then run pipeline
        │  (calls ndm_start internally)
        ▼
session.fet.1 … session.fet.N
        │
        ▼
KlustaKwik session N         ← automatic cluster assignment
        │
        ▼
session.clu.1 … session.clu.N
        │
        ▼
klusters session.xml         ← manual curation
        │
        ▼
neuroscope session.xml       ← validation against LFP / events / position
```

---

## Parameter file format

All tools share a single session `.xml` (or `.yaml`) parameter file. See [ndmanager/README.md](ndmanager/README.md) for the full schema and a worked example.

---

## Migration and release notes

- [MIGRATION_NOTES.md](MIGRATION_NOTES.md) — Qt6 / CMake modernisation changes
- [MIGRATION_NOTES_klusters.md](MIGRATION_NOTES_klusters.md) — klusters-specific migration notes
