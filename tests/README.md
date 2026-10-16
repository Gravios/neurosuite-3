# neurosuite-3 regression test suite

This page is the one reference for running the tests. The test suite is
opt-in (`NS_BUILD_TESTS`, off by default), so normal builds are unaffected.
The C++ tests and the build-system checks run under `ctest`; the
ndmanager-plugins script tests and the fiber-kit contract check run on their
own (see below).

## Running everything

```sh
cmake -B build -S . -DNS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`ctest` options worth knowing:

| Option | Does |
|---|---|
| `-N` | list the registered tests without running them |
| `-R <regex>` | run only tests whose name matches |
| `-E <regex>` | skip tests whose name matches |
| `-j <n>` | run tests in parallel |
| `--rerun-failed` | run only what failed last time |
| `-V` | show each test's output even when it passes |

## Running one part

Test names start with their subproject, so `-R` selects a part:

| Prefix | Subproject | Needs |
|---|---|---|
| `core_test_`, `custody_conformance_` | libneurosuite-core | C++ compiler; Python 3 and bash for the custody runners |
| `kk_test_` | kiloklustakwik | C++ compiler, OpenMP (GPU backends optional) |
| `klustersshared_` | libklustersshared | Qt 6, yaml-cpp |
| `klusters_test_` | klusters | Qt 6 |
| `neuroscope_spectral_` | neuroscope | Qt 6 (to configure neuroscope) |
| `build_invariants` | the build system | CMake only |

```sh
ctest --test-dir build -R '^core_test_|custody_conformance' --output-on-failure
ctest --test-dir build -R klustersshared_sessiontemplate --output-on-failure
```

### Without Qt (a fast loop)

The core library, kiloklustakwik and the build-system checks configure without
Qt or a GPU toolkit. That covers about 27 tests and builds in a minute or two:

```sh
cmake -B build-tests -S . -DNS_BUILD_TESTS=ON \
      -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
      -DNS_SKIP_KLUSTERS=ON -DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_NDMANAGER=ON \
      -DNS_SKIP_LIBKLUSTERSSHARED=ON -DNS_SKIP_NDMANAGER_PLUGINS=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

The same `-DNS_BUILD_TESTS=ON` works for a standalone subproject configure
(for example `cmake -B build -S src/kiloklustakwik -DNS_BUILD_TESTS=ON`).

To check the build system alone, with no configure at all:

```sh
cmake -DREPO=. -P tests/cmake_invariants.cmake
```

## Tests ctest does not run

**ndmanager-plugins scripts** (`src/ndmanager-plugins/test/`). These are
`test_*.py` and `test_*.sh`. Each one exits non-zero on failure. Run them from
the repository root:

```sh
for t in src/ndmanager-plugins/test/test_*.py; do python3 "$t" || echo "FAILED: $t"; done
for t in src/ndmanager-plugins/test/test_*.sh; do bash "$t"    || echo "FAILED: $t"; done
```

Some of them call built plugin binaries under `build/` (for example,
`test_subset_pca.py` needs `build/src/ndmanager-plugins/src/process_pca/process_pca`),
so build into `build/` first.

**Shared contract with fiber-kit.** The vector tables in
`src/libneurosuite-core/test/*.tsv` must be byte-identical to fiber-kit's
`test/` copies. To compare them:

```sh
FK_ROOT=/path/to/fiber-kit python3 src/libneurosuite-core/test/test_shared_contract.py
```

## Build-system invariants

`build_invariants` is a static `cmake -P` check that needs no compiler. It
asserts the following:

1. `-march=native` is a global opt-out through `NS_NATIVE_ARCH`.
2. The `ns_native_arch` shim is gone.
3. The shared `cmake/GpuBackends.cmake` is present and complete.
4. The optional converters skip gracefully when their dependency is missing.
5. No target hardcodes `-march=native`.
6. `cmake_minimum_required` is `3.22...3.31` at every root.
7. nphys-data is `LANGUAGES NONE`.
8. The tree is on one C++ standard: `CMAKE_CXX_STANDARD 20` at every root, and
   no other `cxx_std_NN` is requested. The exemptions are `process_smrconvert`
   (an external package) and the vendored libsamplerate.
9. Every subproject keeps its tests in `test/`, and no C/C++ source sits at a
   subproject root.

When a convention in [STANDARDIZATION.md](../doc/dev/STANDARDIZATION.md) can be
checked by reading files, add the check here, so the convention is enforced
rather than only documented.

## Adding a test

| Where | How |
|---|---|
| libneurosuite-core | [CORE_API.md](../src/libneurosuite-core/docs/CORE_API.md), "Adding a test" |
| libklustersshared | [LIBRARY.md](../src/libklustersshared/docs/LIBRARY.md) §8 |
| any other subproject | add the executable and an `add_test` in `<sub>/test/CMakeLists.txt`, inside `if(NS_BUILD_TESTS)`; name it `<sub>_test_<what>` |

Each test is a program with its own `main()`. It prints the checks that fail
and returns non-zero on failure.

## Known quarantine

`kk_test_per_channel_split` is registered but `DISABLED`. It fails 1 of 30
assertions ("disabling phase keeps amp-only split working"). This is a
pre-existing issue that appeared when the suite was wired up, not a
regression. Once the phase-disable path or the test's expectation is fixed,
re-enable the test by dropping the `DISABLED` property in
`src/kiloklustakwik/test/CMakeLists.txt`.
