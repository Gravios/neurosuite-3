# neurosuite-3

A modernised, Qt6-compatible fork of the Neurosuite electrophysiology toolchain. All components compile under C++17 with Qt 6 on Ubuntu 24.04, Debian 12, and WSL2. GPU acceleration is available for compute-intensive steps via CUDA, ROCm/HIP, and SYCL.

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
| [spikerealign](doc/spikerealign/README.md) | Batch spike waveform realignment tool |

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
| `NS_SKIP_<NAME>` | `OFF` | Skip a component, e.g. `-DNS_SKIP_KLUSTAKWIK=ON` |

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
  libsamplerate0-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  ffmpeg python3
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
| `libsamplerate0-dev` | process\_resample — avoids building the vendored fallback |
| `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev` | process\_extractleds — video LED tracking |
| `ffmpeg` | ndm\_transcodevideo and other pipeline video scripts |
| `python3` | ndm\_prepare, ndm\_checkconsistency scripts |

OpenMP is provided by GCC (part of `build-essential`) and requires no additional package.

---

### macOS

Requires [Homebrew](https://brew.sh) and Xcode Command Line Tools (`xcode-select --install`).

```bash
brew install cmake ninja pkg-config qt@6 yaml-cpp gsl libomp
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

The build script installs `yaml-cpp`, `libxml2`, `gsl`, and (if requested) `ffmpeg` and `libsamplerate` via vcpkg automatically. The `--with-ffmpeg` flag is needed only if you intend to use `process_extractleds` for video LED tracking.

---

## Build order

`libklustersshared` must be installed before ndmanager or klusters. The build script handles this automatically.

```
libklustersshared → ndmanager
                 → klusters
ndmanager-plugins  (independent)
neuroscope         (independent)
klustakwik         (independent)
spikerealign       (independent)
```

---

## Typical workflow

```
Raw acquisition files
        │
        ▼
ndmanager session.yaml       ← edit parameters, then run pipeline
        │  (calls ndm_start)
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
klusters session.yaml        ← manual curation
        │
        ▼
neuroscope session.yaml      ← validation against LFP / events / position
```

---

## Parameter file format

All tools share a single session `.yaml` (or legacy `.xml`) parameter file. Both formats are supported by every GUI application. Use `ndm_xml2yaml` to convert existing XML sessions to YAML.

See [libklustersshared — YAML schema reference](doc/libklustersshared/README.md#yaml-schema-reference) for the full schema. A worked 96-channel silicon probe example is in `templates/jg05-20120316.yaml`.

---

## What's new in neurosuite-3

- **Qt6 / C++17** — all five Qt packages compile cleanly under Qt 6 on Ubuntu 24.04.
- **YAML parameter format** — new YAML schema mirrors the XML schema exactly; `ParameterYamlReader`, `ParameterYamlWriter`, and `ParameterYamlModifier` in `libklustersshared` are the single implementation shared by all applications.
- **GPU acceleration** — KlustaKwik and SpikeRealign support CUDA, ROCm/HIP, and SYCL (Intel Arc). `process_medianfilter` (ndmanager-plugins) supports CUDA.
- **Three-phase chunked CEM** — KlustaKwik can sort long recordings in temporal chunks for improved handling of electrode drift.
- **`ndm_spikegrouper`** — automatic discovery of optimal spike detection channel groups from the high-pass filtered data.
- **`ndm_xml2yaml`** — converts legacy XML parameter files to YAML.
- **Bug fixes** — critical data-loss bug in `getUnits()` fixed; video orientation (rotation/flip/trajectory) no longer lost on YAML save/reload; `ndmanager` video page fields now populate correctly from YAML.

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
