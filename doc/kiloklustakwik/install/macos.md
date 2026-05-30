# KiloKlustaKwik — macOS Installation

## GPU acceleration on macOS

NVIDIA dropped macOS support for CUDA after CUDA 10.2 (2019). AMD ROCm/HIP has no macOS support. Intel oneAPI SYCL is available on macOS in limited form but does not support GPU dispatch on Apple hardware. **macOS builds are CPU / OpenMP only.**

For GPU-accelerated KiloKlustaKwik, use a Linux machine or WSL2 on Windows.

## Package manager

Install [Homebrew](https://brew.sh):

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## System packages

```bash
brew install cmake ninja libomp
```

`libomp` is required because Apple Clang does not bundle the OpenMP runtime.

## Build

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DOpenMP_CXX_FLAGS="-Xpreprocessor -fopenmp -I$(brew --prefix libomp)/include" \
  -DOpenMP_CXX_LIB_NAMES="omp" \
  -DOpenMP_omp_LIBRARY="$(brew --prefix libomp)/lib/libomp.dylib"
cmake --build build -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build
```

If you prefer GCC (includes OpenMP without extra flags):

```bash
brew install gcc
export CXX=$(brew --prefix gcc)/bin/g++-14    # adjust version as needed

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build
```

## Verify

```bash
KiloKlustaKwik --help
ldd KiloKlustaKwik 2>/dev/null || otool -L $(which KiloKlustaKwik) | grep -i omp
```

The `otool` output should show a reference to `libomp.dylib`.

## Note on parallelism

`nproc` is not available on macOS. Use `sysctl -n hw.logicalcpu` to get the logical core count:

```bash
echo "Cores: $(sysctl -n hw.logicalcpu)"
export OMP_NUM_THREADS=$(sysctl -n hw.logicalcpu)
```

To run on a subset of cores:

```bash
OMP_NUM_THREADS=8 KiloKlustaKwik session 1 -ChunkMinutes 5 -SamplingRate 32552 ...
```
