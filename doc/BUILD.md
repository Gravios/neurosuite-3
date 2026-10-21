# Building neurosuite-3

The developer build reference: every CMake option, the GPU backends, the test
suite, the sanitizer build, and how to reproduce CI locally. For a first build
from a clean machine, start with the [Quick start](../README.md#quick-start) in the README (it can
install the dependencies for you) — this page assumes the dependencies are
installed and goes into the knobs you reach for while working
on the code.

The authoritative source for all of this is the top-level
[`CMakeLists.txt`](../CMakeLists.txt); when this page and it disagree, the
`CMakeLists.txt` wins and this page should be fixed.

**Contents:** [1. Toolchain](#1-toolchain) · [2. Configure, build, install](#2-configure-build-install) · [3. Build options](#3-build-options) · [4. GPU backends](#4-gpu-backends) · [5. The build graph and skip flags](#5-the-build-graph-and-skip-flags) · [6. Tests](#6-tests) · [7. AddressSanitizer](#7-addresssanitizer) · [8. Continuous integration parity](#8-continuous-integration-parity) · [9. Troubleshooting](#9-troubleshooting)

---

## 1. Toolchain

| Requirement | Version | Notes |
|---|---|---|
| CMake | ≥ 3.22 (policy range to 3.31) | `cmake_minimum_required(VERSION 3.22...3.31)` |
| C++ compiler | C++20 | `CMAKE_CXX_STANDARD 20`, extensions off; GCC 12+/Clang 15+ |
| C compiler | C11 | for the C sources in the plugins and kiloklustakwik (nphys-data is `LANGUAGES NONE`) |
| Qt | 6, ≥ 6.4 | each GUI subproject does `find_package(Qt6 6.4 …)` |
| Build tool | Ninja or Make | Ninja is what CI uses (`-G Ninja`) |

The reference CI toolchain is **Ubuntu 24.04 with the distribution Qt6**; CI
also builds and tests on Ubuntu 25.10, 26.04 LTS and 26.10, which cover Qt 6.9,
6.10 and 6.11, GCC 15, CMake 4 and Python 3.13/3.14 (see §8). Windows builds natively with MSVC (vcpkg + Qt for MSVC; see below),
everything except KiloKlustaKwik; macOS (Homebrew `qt@6`) configures and builds
but is not checked in CI. The step-by-step installation for every platform is
[INSTALL.md](INSTALL.md).

Let CMake install the system dependencies for you with `-DNS_INSTALL_DEPS=ON`
(apt on Linux, Homebrew on macOS, vcpkg and aqtinstall on Windows); the package
lists are in `CMakeLists.txt`.

**Windows, from nothing.** CMake needs a compiler before it can run, so on a
fresh machine start with the script instead, from cmd or PowerShell:

```bat
scripts\install-deps-windows.bat
```

It installs, skipping whatever is already there: Git, the Visual Studio 2022
Build Tools with the C++ workload (MSVC, Windows SDK, CMake, Ninja), Python
(only to download Qt), vcpkg with yaml-cpp, GSL, libxml2 and libsamplerate, and
Qt 6.8.3 for MSVC through aqtinstall (no Qt account needed). It then writes a
`CMakeUserPresets.json` (git-ignored) with a `windows` preset (MSVC) and a
`windows-sycl` preset (Intel `icx`), so the build is, in the **x64 Native Tools
Command Prompt for VS 2022**:

```bat
cmake --preset windows
cmake --build --preset windows
cmake --install build
```

The install goes to `C:\ns3`, with the Qt and vcpkg DLLs beside the programs,
and adds `C:\ns3\bin` to your user PATH, so `klusters`, `neuroscope`,
`ndmanager` and the `process_*` programs run from any prompt opened afterwards
(no administrator rights; `-DNS_ADD_TO_PATH=OFF` to skip; undo with
`powershell -ExecutionPolicy Bypass -File scripts\windows-path.ps1 -Remove`).
Installing again to another prefix moves the entry rather than adding a second.

The `ndm_*` pipeline steps are bash scripts; on Windows each gets a `.bat`
launcher beside it (`ndm_hipass.bat`, ...) that runs it with Git for Windows'
bash, so `ndm_hipass` works from cmd and PowerShell. Git for Windows must be
installed (the install-deps script installs it); see
[the plugins' Windows guide](ndmanager-plugins/install/windows.md).

Options: `-WithHdf5` (`process_aomconvert`), `-WithFfmpeg`
(`process_extractleds`; a long vcpkg build), `-WithPythonPackages` (PyYAML,
NumPy, SciPy), `-QtVersion`, `-QtRoot`, `-VcpkgRoot`, `-Force` (overwrite the
presets) and `-DryRun` (print what would be done). The Intel oneAPI toolkit
for the SYCL build is not installed by it. With a compiler already present,
`-DNS_INSTALL_DEPS=ON` runs the same script for the libraries and Qt (Qt only
when CMake does not already find one); `-DNS_DEPS_HDF5=ON` and
`-DNS_DEPS_FFMPEG=ON` add the optional libraries.

---

## 2. Configure, build, install

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build --parallel
cmake --install build            # or: sudo cmake --install build
```

The whole monorepo configures and builds in a **single** CMake invocation — no
ExternalProject stamp-file dance, no per-package install step between configure
and build. Every target is visible to an IDE, and incremental rebuilds are
incremental across package boundaries.

The default build type is **Release**; pass `-DCMAKE_BUILD_TYPE=Debug` (or
`RelWithDebInfo` / `MinSizeRel`) to change it. If Qt6 is not on the default
search path, point CMake at it with `-DQt6_DIR=/path/to/lib/cmake/Qt6`.

---

## 3. Build options

All are passed with `-D` at configure time.

| Option | Default | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel` |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | install root |
| `Qt6_DIR` | — | path to `Qt6Config.cmake` when Qt6 is not on PATH |
| `USE_CUDA` | `ON` (auto) | NVIDIA CUDA backends; auto-disabled if no `nvcc` is found |
| `USE_HIP` | `ON` (auto) | AMD ROCm/HIP backends |
| `USE_SYCL` | `ON` (auto) | Intel oneAPI SYCL backend (see §4 for the compiler caveat) |
| `NS_USE_ACCELERATE` | `ON` | macOS only: Apple's Accelerate framework (part of macOS) for NeuroScope's spectrogram FFT (vDSP) and `process_extractemg --backend accelerate` |
| `USE_METAL` | `ON` | macOS only: the Apple GPU (Metal) backend (Apple silicon); fetches Apple's metal-cpp headers from GitHub at configure time unless `NS_METAL_CPP_DIR` points to a copy. Programs with a Metal path fall back to the CPU when no Metal device is present. See `doc/design/metal-backend.md` |
| `CMAKE_CUDA_ARCHITECTURES` | native, else fallback list | e.g. `"86;89;120"` |
| `NS_BUILD_TESTS` | `OFF` | build the ctest regression suite (§6) |
| `NS_ASAN` | `OFF` | build host code with AddressSanitizer (§7) |
| `NS_SKIP_<COMPONENT>` | `OFF` | skip one component (§5) |
| `NS_INSTALL_DEPS` | `OFF` | install system dependencies first (apt/Homebrew/vcpkg) |
| `NS_VCPKG_ROOT` | `$VCPKG_ROOT` or `C:/vcpkg` | vcpkg root for Windows `NS_INSTALL_DEPS` |
| `NS_QT_ROOT` / `NS_QT_VERSION` | `C:/Qt` / `6.8.3` | Windows `NS_INSTALL_DEPS`: where and which Qt to install when CMake finds none |
| `NS_DEPS_HDF5` / `NS_DEPS_FFMPEG` | `OFF` | Windows `NS_INSTALL_DEPS`: also install HDF5 / FFmpeg with vcpkg |
| `NS_DEPLOY_RUNTIME` | `ON` | Windows: `cmake --install` also copies the Qt DLLs and plugins (windeployqt), vcpkg's DLLs and the oneAPI runtime next to the programs, so the install folder runs as it is |
| `NS_ADD_TO_PATH` | `ON` | Windows: `cmake --install` puts the installed `bin` on the current user's PATH (`scripts/windows-path.ps1`; skipped with `DESTDIR`) |
| `NS_ZEN_OPT` | `ON` | host tuning from `cmake/ZenOptimizations.cmake` for Release/RelWithDebInfo: `-funroll-loops` and LTO (where the toolchain supports it) globally, and `-ffast-math` through the `ns_fast_math` interface target, which only the targets that link it get (KiloKlustaKwik); `OFF` disables all of it |
| `NS_NATIVE_ARCH` | `ON` | with `NS_ZEN_OPT`, compile Release/RelWithDebInfo for the build host's ISA (`-march=native`); pass `OFF` for a portable or cross build |
| `NS_FIBER_KIT_ROOT` | — | path to a fiber-kit checkout; `core_shared_contract` then also compares the shared vector tables and `contract.yaml` with it (§6) |

---

## 4. GPU backends

The three GPU backends are **optional and auto-detected** — a build with none
of them present falls back to the CPU/OpenMP path, which is the path CI
exercises (and which every GPU kernel has a scalar equivalent of). OpenMP is
itself optional and degrades to single-threaded if absent (see
[`src/klusters/docs/CONCURRENCY.md`](../src/klusters/docs/CONCURRENCY.md) §6).

- **CUDA** (`USE_CUDA`, the primary backend) — the top-level `CMakeLists.txt`
  probes for `nvcc` *before* `project()` so the CUDA language is only enabled
  when the toolkit is present. If it is not found the build continues without
  CUDA and prints how to enable it (`nvcc` on PATH, or
  `-DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc`). Select architectures with
  `-DCMAKE_CUDA_ARCHITECTURES`.
- **HIP** (`USE_HIP`) — handled inside the packages that use it; behaves like
  CUDA for configure purposes.
- **SYCL** (`USE_SYCL`) — **has one monorepo constraint**: the top-level
  `project()` locks the C++ compiler for all subdirectories, so kiloklustakwik
  cannot probe for `icpx` on its own. To build the SYCL backend, select the
  oneAPI compiler at the top level:

  ```sh
  cmake -B build -DCMAKE_CXX_COMPILER=icpx -DCMAKE_C_COMPILER=icx
  ```

  With a standard compiler, kiloklustakwik still builds cleanly with OpenMP and,
  if found, CUDA/HIP — only the SYCL backend is affected.

---

## 5. The build graph and skip flags

Components build in dependency order (top-level `CMakeLists.txt`, via the
`ns_add_subdir` macro):

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

Because `libklustersshared` and `libneurosuite-core` are `add_subdirectory`'d
before their consumers and exported into the build tree, a standalone
`find_package` is unnecessary in the monorepo build — each consumer links the
in-tree target, which also gives CMake the correct parallel build-order edges.

Skip any component with `-DNS_SKIP_<NAME>=ON` (the suffix is the component name
upper-cased, `-`→`_`):

```sh
cmake -B build -DNS_SKIP_KILOKLUSTAKWIK=ON -DNS_SKIP_NDMANAGER=ON
```

Valid names: `NPHYS_DATA`, `LIBNEUROSUITE_CORE`, `LIBKLUSTERSSHARED`,
`KLUSTERS`, `NEUROSCOPE`, `NDMANAGER`, `NDMANAGER_PLUGINS`, `KILOKLUSTAKWIK`.

The plugins (no Qt) also configure on their own, which adds
`libneurosuite-core` for them:

```sh
cmake -S src/ndmanager-plugins -B build-plugins -DNS_BUILD_TESTS=ON
cmake --build build-plugins -j && ctest --test-dir build-plugins
```

---

## 6. Tests

The regression suite is **opt-in** so normal builds are unaffected:

```sh
cmake -B build -DNS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`NS_BUILD_TESTS=ON` calls `enable_testing()` and builds both the C++ unit tests
under each subproject's `test/` directory and the build-system invariant checks
under the top-level [`tests/`](../tests/). Run them with `ctest`.

[tests/README.md](../tests/README.md) covers the rest: how to run one
subproject's tests (`ctest -R`), a Qt-free configure for a fast loop, the
ndmanager-plugins script tests, the fiber-kit contract check
(`-DNS_FIBER_KIT_ROOT`), and how to add a test.

---

## 7. AddressSanitizer

<details>
<summary>Sanitizer build (for debugging memory errors)</summary>

`NS_ASAN=ON` instruments **host** code (C, C++, and the host side of `.cu`
translation units) to catch heap/stack overflows and use-after-free *at the bad
access*, with a symbolized stack trace — the tool to reach for when a crash
surfaces far from the write that caused it (`munmap_chunk(): invalid pointer`,
`free(): invalid pointer`, `malloc(): corrupted top size`).

```sh
cmake -B build-asan -DNS_ASAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-asan --parallel
```

Then reproduce the crash under the sanitizer. Because klusters links a shared
`libneurosuite-qt.so` and the CUDA runtime reserves mappings that overlap ASan's
shadow gap, run with:

```sh
ASAN_OPTIONS=protect_shadow_gap=0:detect_leaks=0:abort_on_error=1:detect_odr_violation=1 \
    ./build-asan/klusters/src/klusters <session> <group>
```

`detect_odr_violation=1` (not the default `2`) is required: the executable and
the shared object each register the standard library's inline globals, and the
default ODR checker aborts in `_dl_init` on that benign same-size duplicate
before `main()`. CUDA **device** code is not instrumented; if corruption
reproduces only with the GPU active, configure a second tree with
`-DUSE_CUDA=OFF -DNS_ASAN=ON` to rule the CUDA host stubs in or out. The full
rationale is in the `NS_ASAN` block of `CMakeLists.txt`.

</details>

---

## 8. Continuous integration parity

<details>
<summary>Reproducing CI locally</summary>

Four workflows ([`.github/workflows/`](../.github/workflows/)), one per
operating system plus the docs, so each has its own status badge in the
README. `ci-linux.yml`, `ci-windows.yml` and `ci-macos.yml` run on pushes to
`main`, pull requests into it and manual dispatch; `docs.yml` runs on pushes to
`main` and on manual dispatch only.

- **`ci-linux.yml`** has two jobs, which run side by side.
  - **`lint`** runs [`scripts/lint.sh`](../scripts/lint.sh): `bash -n`, ruff's
    defect rules and the build invariants, with no build. Run it locally the
    same way.
  - **`build`** is the CPU build-and-test gate, run once per Ubuntu release
    (24.04, 25.10, 26.04, 26.10), each in that release's `ubuntu:<release>`
    container with the distribution's own packages. 25.10 is past end of life
    and does not fail the run. It configures with the GPU
    backends **off**, warnings **on but non-fatal** and the regression suite
    on, exercising the CPU/OpenMP path every GPU backend falls back to. It
    then runs ctest, with a fiber-kit checkout for `core_shared_contract`:

  ```sh
  cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
    -DNS_BUILD_TESTS=ON -DNS_FIBER_KIT_ROOT=/path/to/fiber-kit \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wshadow"
  cmake --build build --parallel
  QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
  ```

  Reproduce a CI failure locally by configuring exactly this way. `-Wshadow` is
  surfaced but not enforced: klusters and libklustersshared are shadow-clean
  (see [`STANDARDIZATION.md`](dev/STANDARDIZATION.md)), while kiloklustakwik,
  ndmanager and neuroscope have not been shadow-audited, so promoting it to
  `-Werror=shadow` must wait for a green baseline there.
- **`ci-windows.yml`** builds the GUI programs (Klusters, NeuroScope,
  NDManager), their libraries and the ndmanager-plugins programs with MSVC on
  `windows-2022`, against Qt 6.8 and vcpkg's yaml-cpp, GSL and libxml2. HDF5
  and FFmpeg are not installed, so `process_aomconvert` and
  `process_extractleds` are skipped; libsamplerate is not installed, so
  `process_resample` compiles its bundled copy. KiloKlustaKwik is skipped
  (`-DNS_SKIP_KILOKLUSTAKWIK=ON`); on Windows it still needs WSL2. The `ndm_*`
  steps run on Windows through their `.bat` launchers and Git Bash. It builds
  without tests.
- **`ci-macos.yml`** builds everything on `macos-15` (Apple silicon) with
  Apple clang and Homebrew's Qt 6, yaml-cpp, GSL, libomp, libsamplerate,
  FFmpeg, HDF5 and bash, as [INSTALL.md](INSTALL.md#macos) installs them,
  with `-DNS_NATIVE_ARCH=OFF` and `-k 0`. Its tests run but do not fail it
  yet: the `ndm_*` scripts start with `#!/bin/bash`, macOS's bash 3.2.

- **`docs.yml`** — builds the documentation site and deploys it to GitHub Pages.
  It runs `doc/site/gen.sh` (the same pandoc pipeline that produces NeuroScope's
  in-app F1 handbook) and then `mkdocs build --strict`. It triggers on
  `doc/site/**`, `src/neuroscope/doc/en/**`, `src/*/docs/**` and
  `DEVELOPER_GUIDE.md` — so the developer-guide references under
  `src/<component>/docs/` are published automatically, and `--strict` means a
  broken internal link fails the deploy. Keep cross-document links relative and
  correct.

The CI dependency list (in `ci-linux.yml`) is the canonical "what to `apt-get
install`" for a Linux build.

</details>

---

## 9. Troubleshooting

| Symptom | Fix |
|---|---|
| `Could NOT find Qt6` | install `qt6-base-dev` (+ `qt6-tools-dev`, `libqt6svg6-dev`), or pass `-DQt6_DIR=…/lib/cmake/Qt6` |
| CUDA toolkit not found but wanted | put `nvcc` on PATH or pass `-DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc` |
| SYCL backend not built | configure with `-DCMAKE_CXX_COMPILER=icpx -DCMAKE_C_COMPILER=icx` at the top level (§4) |
| A component won't build and blocks the rest | isolate it with `-DNS_SKIP_<NAME>=ON` while you work on another |
| Memory corruption with a far-away backtrace | rebuild with `-DNS_ASAN=ON` and the `ASAN_OPTIONS` above (§7) |
| `no space left on device` mid-build | the build tree is large with all backends; delete the `build/` dir and reconfigure, or build fewer components with skip flags |

---

See also: [`DEVELOPER_GUIDE.md`](dev/DEVELOPER_GUIDE.md) (the internals map),
[`CONTRIBUTING.md`](../.github/CONTRIBUTING.md) (patch and verification discipline), and
[`STANDARDIZATION.md`](dev/STANDARDIZATION.md) (coding and layout standards).
