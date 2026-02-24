# SpikeRealign — macOS Installation

## GPU acceleration on macOS

CUDA, HIP, and SYCL GPU acceleration are not available on macOS. macOS builds are CPU / OpenMP only. For GPU-accelerated SpikeRealign, use a Linux machine or WSL2 on Windows.

## Package manager

Install [Homebrew](https://brew.sh):

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## System packages

```bash
brew install cmake ninja libomp
```

## Build

```bash
cd /path/to/neurosuite-3/src/spikerealign

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DOpenMP_CXX_FLAGS="-Xpreprocessor -fopenmp -I$(brew --prefix libomp)/include" \
  -DOpenMP_CXX_LIB_NAMES="omp" \
  -DOpenMP_omp_LIBRARY="$(brew --prefix libomp)/lib/libomp.dylib"
cmake --build build -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build
```

Alternatively with GCC (OpenMP included out of the box):

```bash
brew install gcc
export CXX=$(brew --prefix gcc)/bin/g++-14

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build
```

## Verify

```bash
SpikeRealign --help
```
