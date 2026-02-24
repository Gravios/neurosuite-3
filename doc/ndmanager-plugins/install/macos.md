# ndmanager-plugins — macOS Installation

## Platform support overview

The bash pipeline scripts (`ndm_start`, `ndm_hipass`, etc.) run natively on macOS — Bash and the required GNU utilities are available via Homebrew.

> **Note:** `process_extractspikes` uses `sys/sysinfo.h`, a Linux-only header. This binary does not build on macOS. Spike extraction (`ndm_extractspikes`) is therefore not available natively on macOS. All other pipeline tools build and run without issue. If you need spike extraction on macOS, consider running the pipeline under a Linux VM or Docker container.

## Package manager

Install [Homebrew](https://brew.sh):

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## System packages

```bash
brew install cmake ninja libxml2 yaml-cpp python pkg-config
pip3 install pyyaml
```

### Optional: FFmpeg (video tools)

```bash
brew install ffmpeg
```

## Build (CPU / OpenMP)

macOS ships with the Apple Clang compiler which supports OpenMP via `libomp`:

```bash
brew install libomp
```

```bash
cd /path/to/neurosuite-3/src

cmake -B build/ndmanager-plugins ndmanager-plugins \
  -DUSE_CUDA=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix libxml2);$(brew --prefix yaml-cpp)" \
  -DOpenMP_CXX_FLAGS="-Xpreprocessor -fopenmp -I$(brew --prefix libomp)/include" \
  -DOpenMP_CXX_LIB_NAMES="omp" \
  -DOpenMP_omp_LIBRARY="$(brew --prefix libomp)/lib/libomp.dylib"
cmake --build build/ndmanager-plugins -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build/ndmanager-plugins
```

The `-DOpenMP_*` flags are required because Apple Clang does not include the OpenMP runtime by default — it must be pulled from Homebrew's `libomp`.

If you prefer GCC (which includes OpenMP out of the box):

```bash
brew install gcc
export CC=$(brew --prefix gcc)/bin/gcc-14
export CXX=$(brew --prefix gcc)/bin/g++-14

cmake -B build/ndmanager-plugins ndmanager-plugins \
  -DUSE_CUDA=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix libxml2);$(brew --prefix yaml-cpp)"
cmake --build build/ndmanager-plugins -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build/ndmanager-plugins
```

Replace `gcc-14` / `g++-14` with the version installed by `brew info gcc`.

## Note on CUDA

NVIDIA dropped macOS support for CUDA after CUDA 10.2 (macOS 10.14 Mojave). CUDA acceleration is not available on macOS. All processing binaries fall back to OpenMP.

## Verify

```bash
which ndm_start ndm_hipass ndm_pca
process_medianfilter --version    # should show (OpenMP, N threads)
```

## Note on python3 and YAML

The ndm_* scripts use `python3 -c` inline for YAML file reading. Verify the path is correct:

```bash
which python3          # should return /opt/homebrew/bin/python3 or similar
python3 -c "import yaml; print('ok')"
```

If `python3` is not found in the script PATH (scripts run in a non-interactive shell), add a symlink:

```bash
sudo ln -sf $(which python3) /usr/local/bin/python3
```
