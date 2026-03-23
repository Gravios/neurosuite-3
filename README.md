# neurosuite-3

A modernised, Qt6-compatible fork of the Neurosuite electrophysiology toolchain. All components compile under C++20 with Qt 6 on Ubuntu 24.04, Debian 12, and WSL2. GPU acceleration is available for compute-intensive steps via CUDA, ROCm/HIP, and SYCL.

---

## Components

| Component | Purpose |
|---|---|
| [libklustersshared](doc/libklustersshared/README.md) | Shared library: canonical YAML I/O, shared data types, GUI widgets |
| [ndmanager](doc/ndmanager/README.md) | GUI session manager — edit parameters, run the preprocessing pipeline |
| [ndmanager-plugins](doc/ndmanager-plugins/README.md) | Command-line preprocessing pipeline (format conversion, filtering, PCA, spike detection) |
| [neuroscope](doc/neuroscope/README.md) | Multi-channel signal visualiser |
| [klusters](doc/klusters/README.md) | Interactive manual spike-sorting GUI |
| [klustakwik](doc/klustakwik/README.md) | Automatic spike sorter (Classification EM) |
| [spikerealign](doc/spikerealign/README.md) | Spike waveform realignment engine (used inside klusters and KlustaKwik Phase 1.5) |

---

## Quick start

The top-level `CMakeLists.txt` is a CMake superbuild that builds and installs all components in dependency order. The invocation is identical on Linux, macOS, and Windows:

```bash
git clone https://github.com/Gravios/neurosuite-3.git
cd neurosuite-3

# Install system dependencies and build in one step:
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local -DNS_INSTALL_DEPS=ON
cmake --build build

# Or, if you have already installed dependencies manually:
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
```

`-DNS_INSTALL_DEPS=ON` runs `apt-get` / `brew` / `vcpkg` at configure time to install all required packages automatically (see [Dependencies](#dependencies) for what gets installed). It is `OFF` by default so repeated cmake invocations don't re-run the package manager unnecessarily.

On Linux, run `sudo cmake --build build` if writing to a system prefix, or pass `-DCMAKE_INSTALL_PREFIX=$HOME/.local` to install without elevated privileges.

On **macOS**, Qt6 is not on the system path by default — pass `-DQt6_DIR` explicitly:

```bash
cmake -B build \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DQt6_DIR="$(brew --prefix qt@6)/lib/cmake/Qt6"
cmake --build build
```

On **Windows**, run from a **Developer Command Prompt for VS 2022** and point at your Qt installation:

```bat
cmake -B build ^
  -DCMAKE_INSTALL_PREFIX=C:\NeuroSuite ^
  -DQt6_DIR=C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
cmake --build build
```

**Useful options** (pass with `-D`):

| Option | Default | Description |
|---|---|---|
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | Install root |
| `CMAKE_BUILD_TYPE` | `Release` | `Release` or `Debug` |
| `JOBS` | CMake default | Parallel job count |
| `Qt6_DIR` | auto-detected | Path to `Qt6Config.cmake` |
| `NS_INSTALL_DEPS` | `OFF` | Auto-install system deps via apt / brew / vcpkg |
| `NS_VCPKG_ROOT` | `%VCPKG_ROOT%` or `C:/vcpkg` | vcpkg root (Windows, `NS_INSTALL_DEPS=ON` only) |
| `USE_CUDA` / `USE_HIP` / `USE_SYCL` | `ON` | GPU backend enable/disable |
| `CMAKE_CUDA_ARCHITECTURES` | auto | e.g. `"86;89;120"` for Blackwell |
| `NS_SKIP_<n>` | `OFF` | Skip a component, e.g. `-DNS_SKIP_KLUSTAKWIK=ON` |

Valid `NS_SKIP_*` names: `NPHYS_DATA`, `LIBKLUSTERSSHARED`, `KLUSTERS`, `NEUROSCOPE`, `NDMANAGER`, `NDMANAGER_PLUGINS`, `KLUSTAKWIK`, `SPIKEREALIGN`.

After installing to a non-standard prefix on Linux, register the shared library with the dynamic linker:

```bash
echo '/usr/local/lib' | sudo tee /etc/ld.so.conf.d/neurosuite.conf
sudo ldconfig
```

Platform-specific build scripts (`scripts/build-neurosuite.sh`, `scripts/build-neurosuite-macos.sh`, `scripts/build-neurosuite.bat`) are also available for advanced use cases such as incremental rebuilds of individual components.

---

## Dependencies

GPU backends (CUDA, ROCm/HIP, SYCL) are optional and auto-detected at build time. Instructions for those are in the per-component `doc/` directories. The packages below are everything needed for a full CPU build.

### Linux (Ubuntu 24.04 / Debian 12)

```bash
sudo apt install -y \
  build-essential cmake ninja-build pkg-config git \
  qt6-base-dev qt6-tools-dev libqt6svg6-dev \
  libgl-dev libxkbcommon-dev libxcb-cursor0 \
  libyaml-cpp-dev libxml2-dev libgsl-dev \
  libhdf5-dev \
  libsamplerate0-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  ffmpeg python3 python3-yaml python3-numpy
```

**What each group covers:**

| Package(s) | Required by |
|---|---|
| `build-essential cmake ninja-build pkg-config git` | All components — build toolchain |
| `qt6-base-dev qt6-tools-dev libqt6svg6-dev` | libklustersshared, klusters, neuroscope, ndmanager |
| `libgl-dev libxkbcommon-dev libxcb-cursor0` | Qt6 OpenGL and XCB platform plugin |
| `libyaml-cpp-dev` | libklustersshared — YAML parameter I/O |
| `libxml2-dev` | ndmanager — legacy XML parameter files and xpathReader |
| `libgsl-dev` | process\_pca — PCA feature extraction |
| `libhdf5-dev` | process\_aomconvert — AlphaOmega .mat (HDF5 v7.3) conversion |
| `libsamplerate0-dev` | process\_resample — avoids building the vendored fallback |
| `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev` | process\_extractleds — video LED tracking |
| `ffmpeg` | ndm\_transcodevideo and other pipeline video scripts |
| `python3 python3-yaml python3-numpy` | ndm\_setupgroups, ndm\_estimatedrift, ndm\_decomposecollisions, ndm\_checkconsistency |

OpenMP is provided by GCC (part of `build-essential`) and requires no additional package.

---

### macOS

Requires [Homebrew](https://brew.sh) and Xcode Command Line Tools (`xcode-select --install`).

```bash
brew install cmake ninja pkg-config qt@6 yaml-cpp gsl hdf5 libomp
# Optional but recommended:
brew install ffmpeg libsamplerate
```

Then build using the macOS script (GPU backends are disabled automatically):

```bash
./build-neurosuite-macos.sh --prefix "$HOME/.local" --with-ffmpeg --with-libsamplerate
```

`libomp` is needed because Apple Clang does not ship OpenMP; the build script passes the required CMake hint flags automatically when Homebrew libomp is present.

---

### Windows

Requires:
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- [CMake 3.21+](https://cmake.org/download/) (or use the one bundled with Visual Studio)
- [Git for Windows](https://git-scm.com/download/win)
- [Qt 6.6+](https://www.qt.io/download-qt-installer) — install the `MSVC 2022 64-bit` component; set `Qt6_DIR` to the cmake subdirectory, e.g. `C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6`
- [vcpkg](https://vcpkg.io/en/getting-started) — used for the remaining C++ libraries

Bootstrap vcpkg once:

```bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT C:\vcpkg
```

Then build from a **Developer Command Prompt for VS 2022**:

```bat
build-neurosuite.bat --qt-dir C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6 ^
                     --vcpkg-root C:\vcpkg ^
                     --with-ffmpeg --with-libsamplerate
```

The build script installs `yaml-cpp`, `libxml2`, `gsl`, `hdf5`, and (if requested) `ffmpeg` and `libsamplerate` via vcpkg automatically.

---

## Build order

`libklustersshared` must be installed before ndmanager or klusters. The build script handles this automatically.

```
libklustersshared → ndmanager
                 → klusters (includes spikerealign)
ndmanager-plugins  (independent)
neuroscope         (independent)
klustakwik         (independent)
```

---

## Typical workflow

### AlphaOmega recordings

```
AlphaOmega .mat file
        │
        ▼
ndm_aom2dat session.yaml     ← converts .mat → .dat + generates session YAML
        │                       with calibrated per-group KlustaKwik parameters
        │
        ├─ ndm_hipass          → SESSION.fil
        ├─ ndm_lfp             → SESSION.lfp
        ├─ ndm_extractspikes   → SESSION.res.N, SESSION.spk.N
        ├─ ndm_pca             → SESSION.fet.N
        ▼
ndm_klustakwik session.yaml   ← automatic sorting (reads per-group KK params from YAML)
        │
        ▼
klusters session.yaml         ← manual curation
        │
        ├─ ndm_stripdat        → SESSION-spkclean.dat  (spike-subtracted)
        │       └─ ndm_redetectspikes → merges newly detected spikes into .res/.spk/.clu
        │              └─ ndm_pca + ndm_klustakwik → re-sort merged spike set
        │
        ├─ ndm_decomposecollisions  → SESSION.col.N
        ├─ ndm_estimatedrift        → SESSION.drift
        ▼
neuroscope session.yaml       ← validation against LFP / events / position
```

### Other acquisition systems (Neuralynx, CED, Amplipex)

```
Raw acquisition files
        │
        ▼
ndmanager session.yaml        ← edit parameters; fill in Probes tab
        │
        ├─ ndm_setupgroups    ← writes anatomical + spike groups from probe library
        │
        ├─ ndm_ncs2dat / ndm_smr2dat / ndm_tsp2sts   ← format conversion
        ├─ ndm_resample / ndm_mergedat / ndm_concatenate
        ├─ ndm_reorderchannels
        │
        ├─ ndm_hipass / ndm_lfp / ndm_extractspikes / ndm_pca
        │  (full pipeline via ndm_start)
        ▼
ndm_klustakwik session.yaml
        │
        ▼
klusters session.yaml         ← manual curation
        │
        ├─ ndm_decomposecollisions
        ├─ ndm_estimatedrift
        ▼
neuroscope session.yaml
```

---

## Parameter file format

All tools share a single session `.yaml` (or legacy `.xml`) parameter file. Both formats are supported by every GUI application. Use `ndm_xml2yaml` to convert existing XML sessions to YAML.

See [libklustersshared — YAML schema reference](doc/libklustersshared/README.md#yaml-schema-reference) for the full schema. A worked 96-channel silicon probe example is in `templates/jg05-20120316.yaml`. A blank template is in `templates/template.yaml`.

---

## What's new in neurosuite-3

### Data acquisition

- **`ndm_aom2dat`** — new pipeline plugin for AlphaOmega recordings. Converts `.mat` (HDF5 v7.3) files directly to the neurosuite `.dat` binary and generates a complete session YAML with calibrated per-group KlustaKwik parameters. `process_aomconvert` streams in configurable chunks; peak RAM is independent of recording length. Supports mixed probe topologies via `topology: "1-16:16,17-32:4"`.

### Spike sorting

- **Three-tier KlustaKwik parameter system** — `ndm_klustakwik` now resolves every parameter through three priority levels: (1) per-group `spikeDetection.channelGroups[g].klustakwik` block (highest — probe-type-calibrated, written by `ndm_aom2dat`), (2) global `programs[ndm_klustakwik].parameters` block (session-level override), (3) built-in bash defaults. `MergeThresh` is calibrated to χ²(`nCh×3`, 0.9999) per group, `MaxClusters` is scaled by probe type (linear: 40, tetrode: 20, single: 5), and `GlobalMergeIter` / `TimeMergeIter` scale with √(`nSpatialDims`/24).
- **KlustaKwik chunked CEM** — three-phase temporal chunking for long recordings: Phase 0 global pre-seed (`ChunkPreseedFraction`), Phase 1 per-chunk CEM, Phase 2 overlap-vote cross-chunk merge (`ChunkOverlapMinutes`), Phase 3 global warm-start refinement. All parameters exposed in `ndm_klustakwik` and the session YAML.
- **KlustaKwik bug fixes** — chunk boundary normalisation (spikes at recording start no longer misassigned), sentinel initialisation (unwritten spikes no longer mapped to cluster 0), `MergeThresh` range warning, `UseFeatures` length-mismatch warning (now defaults to `"all"`), GPU shared-memory overflow fallback for pre-Blackwell hardware.

### LFP and post-sorting pipeline

- **`ndm_stripdat`** — subtracts spike waveforms from the raw `.dat` to produce `SESSION-spkclean.dat`. Uses per-cluster template modelling when `.clu.N` files are present (projection-based amplitude scaling, ISI-driven burst shape compensation). The original `.dat` is never modified.
- **`ndm_redetectspikes`** — second-round spike detection on the spike-cleaned `.dat`. Detects spikes missed in the first pass, then merges them into the existing `.res`/`.spk`/`.clu` files via `process_mergespikes`. Re-run `ndm_pca` and `ndm_klustakwik` afterwards.

### Qt6 / C++20

- All five Qt packages compile cleanly under Qt 6 on Ubuntu 24.04.

### YAML parameter format

- New YAML schema mirrors the XML schema exactly; `ParameterYamlReader`, `ParameterYamlWriter`, and `ParameterYamlModifier` in `libklustersshared` are the single implementation shared by all applications.

### GPU acceleration

- KlustaKwik and klusters support CUDA, ROCm/HIP, and SYCL (Intel Arc) for both the CEM E-step and the waveform realignment xcorr kernel. `process_medianfilter`, `process_medianthreshold`, and `process_spikegrouper` support CUDA. See [doc/gpu/README.md](doc/gpu/README.md).

### Probe library and setup

- **`ndm_setupgroups`** — automatically populates `anatomicalDescription` and `spikeDetection` channel groups from the probe library. Run before `ndm_extractspikes`.
- **Probe library** — 50 NeuroNexus `.probe` configuration files installed to `share/neurosuite/probes/`. Covers the full current silicon probe catalog including all Buzsaki series, A-series, and tetrode probes.
- **Probe tab** — new ndmanager GUI tab for editing the `probes:` session YAML section.

### Analysis plugins

- **`ndm_estimatedrift`** — estimates in-vivo probe drift from curated spike-sorting results. Output: `SESSION.drift` (YAML).
- **`ndm_decomposecollisions`** — template-matching decomposition of overlapping spike waveforms. Output: `SESSION.col.N` YAML sidecars (original data unchanged).
- **`ndm_spikegrouper`** — automatic discovery of optimal spike detection channel groups from the high-pass filtered data.
- **`ndm_denoiseuniform`** — removes electrically uniform noise events (common-mode artefacts) from `.spk.N`/`.res.N` files after spike extraction.
- **`ndm_xml2yaml`** — converts legacy XML parameter files to YAML.
- **`process_extractspikes_sdiff`** — spatial-derivative spike detection for common-mode noise suppression on high-density probes.

### Bug fixes

- **yaml-cpp 0.8 const `operator[]` crash** in `ParameterYamlReader` (13 chained subscripts patched).
- **Null-value emission** fix in `ParameterYamlWriter` (empty strings now emit `~`).
- **klusters undo/redo segfault** — three interacting bugs fixed: `undoRedoInProcess` not reset in normal path, `nbUndoChangedCleaning` clearing wrong list, `deletedClusters` uninitialised in `openDocument`.
- **klusters empty `.clu` placeholder seeding** — opening a raw `.fet.N` file before any sorting no longer fails.
- **ndmanager `channelcolorspage` segfault on save** — null table cells and half-row-removal loop fixed.

See [CHANGES.md](CHANGES.md) for the full list of fixes and improvements.

---

## Documentation

Per-component documentation is in `doc/`:

- [doc/libklustersshared/](doc/libklustersshared/README.md)
- [doc/ndmanager/](doc/ndmanager/README.md)
- [doc/ndmanager-plugins/](doc/ndmanager-plugins/README.md)
- [doc/neuroscope/](doc/neuroscope/README.md)
- [doc/klusters/](doc/klusters/README.md)
- [doc/klustakwik/](doc/klustakwik/README.md)
- [doc/spikerealign/](doc/spikerealign/README.md)

Each component doc directory contains an `install/` subdirectory with platform-specific build instructions.

---

## GPU Acceleration

Several components use GPU acceleration when the relevant toolkit is present at build time. A CPU/OpenMP fallback is always compiled — no flag is needed to get a working build without a GPU.

| Component | GPU backends | What is accelerated |
|---|---|---|
| `klustakwik` | CUDA / HIP / SYCL | CEM E-step + Phase 1.5 waveform realignment (xcorr) |
| `klusters` | CUDA / HIP / SYCL | Grouping Assistant, interactive spike realignment (xcorr) |
| `process_medianfilter` | CUDA | High-pass filter (`ndm_hipass`) |
| `process_medianthreshold` | CUDA | Threshold estimation (`ndm_extractspikes`) |
| `process_spikegrouper` | CUDA / OpenMP | Coincidence matrix (`ndm_spikegrouper`) |

See **[doc/gpu/README.md](doc/gpu/README.md)** for toolkit installation instructions for:
- **NVIDIA CUDA** — including CUDA 12.8 for RTX 5000 series (Blackwell / sm_120) and Secure Boot MOK key signing on Ubuntu 24.04
- **AMD ROCm / HIP**
- **Intel oneAPI / SYCL** — bare metal and WSL2
