# Building neurosuite-3

The developer build reference: every CMake option, the GPU backends, the test
suite, the sanitizer build, and how to reproduce CI locally. For a first build
from a clean machine, start with the [Quick start](README.md#quick-start) and
[Dependencies](README.md#dependencies) in the README — this page assumes the
dependencies are installed and goes into the knobs you reach for while working
on the code.

The authoritative source for all of this is the top-level
[`CMakeLists.txt`](CMakeLists.txt); when this page and it disagree, the
`CMakeLists.txt` wins and this page should be fixed.

---

## 1. Toolchain

| Requirement | Version | Notes |
|---|---|---|
| CMake | ≥ 3.22 (policy range to 3.31) | `cmake_minimum_required(VERSION 3.22...3.31)` |
| C++ compiler | C++20 | `CMAKE_CXX_STANDARD 20`, extensions off; GCC 12+/Clang 15+ |
| C compiler | C11 | for the C sources in nphys-data / plugins |
| Qt | 6, ≥ 6.4 | each GUI subproject does `find_package(Qt6 6.4 …)` |
| Build tool | Ninja or Make | Ninja is what CI uses (`-G Ninja`) |

The reference CI toolchain is **Ubuntu 24.04 with the distribution Qt6** (see
§8). macOS (Homebrew `qt@6`) and Windows (vcpkg + the Qt Online Installer) are
supported for configuration; the install details are in the README.

Let CMake install the system dependencies for you with `-DNS_INSTALL_DEPS=ON`
(apt on Linux, Homebrew on macOS, vcpkg on Windows); the package lists are in
`CMakeLists.txt`.

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
| `CMAKE_CUDA_ARCHITECTURES` | native, else fallback list | e.g. `"86;89;120"` |
| `NS_BUILD_TESTS` | `OFF` | build the ctest regression suite (§6) |
| `NS_ASAN` | `OFF` | build host code with AddressSanitizer (§7) |
| `NS_SKIP_<COMPONENT>` | `OFF` | skip one component (§5) |
| `NS_INSTALL_DEPS` | `OFF` | install system dependencies first (apt/Homebrew/vcpkg) |
| `NS_VCPKG_ROOT` | `$VCPKG_ROOT` or `C:/vcpkg` | vcpkg root for Windows `NS_INSTALL_DEPS` |
| `NS_ZEN_OPT` | (optional module) | host tuning (`-march=native`, fast-math, LTO) via `cmake/ZenOptimizations.cmake`; pass `OFF` when cross-compiling |

---

## 4. GPU backends

The three GPU backends are **optional and auto-detected** — a build with none
of them present falls back to the CPU/OpenMP path, which is the path CI
exercises (and which every GPU kernel has a scalar equivalent of). OpenMP is
itself optional and degrades to single-threaded if absent (see
[`src/klusters/docs/CONCURRENCY.md`](src/klusters/docs/CONCURRENCY.md) §6).

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
under the top-level [`tests/`](tests/). Run them with `ctest`.

---

## 7. AddressSanitizer

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

---

## 8. Continuous integration parity

Two workflows run on `main` and on pull requests
([`.github/workflows/`](.github/workflows/)):

- **`ci.yml`** — the CPU build gate on `ubuntu-24.04`. It configures with the
  GPU backends **off** and warnings **on but non-fatal**, exercising the
  CPU/OpenMP path every GPU backend falls back to:

  ```sh
  cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wshadow"
  cmake --build build --parallel
  ```

  Reproduce a CI failure locally by configuring exactly this way. `-Wshadow` is
  surfaced but not enforced: klusters and libklustersshared are shadow-clean
  (see [`STANDARDIZATION.md`](STANDARDIZATION.md)), while kiloklustakwik,
  ndmanager and neuroscope have not been shadow-audited, so promoting it to
  `-Werror=shadow` must wait for a green baseline there.

- **`docs.yml`** — builds the documentation site and deploys it to GitHub Pages.
  It runs `doc/site/gen.sh` (the same pandoc pipeline that produces NeuroScope's
  in-app F1 handbook) and then `mkdocs build --strict`. It triggers on
  `doc/site/**`, `src/neuroscope/doc/en/**`, `src/*/docs/**` and
  `DEVELOPER_GUIDE.md` — so the developer-guide references under
  `src/<component>/docs/` are published automatically, and `--strict` means a
  broken internal link fails the deploy. Keep cross-document links relative and
  correct.

The CI dependency list (in `ci.yml`) is the canonical "what to `apt-get
install`" for a Linux build.

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

See also: [`DEVELOPER_GUIDE.md`](DEVELOPER_GUIDE.md) (the internals map),
[`CONTRIBUTING.md`](CONTRIBUTING.md) (patch and verification discipline), and
[`STANDARDIZATION.md`](STANDARDIZATION.md) (coding and layout standards).
