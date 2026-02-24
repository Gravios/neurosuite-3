# SpikeRealign — Linux Installation, CPU / OpenMP

## System packages

```bash
sudo apt install cmake build-essential libgomp1
```

## Build

```bash
cd /path/to/neurosuite-3/src/spikerealign

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## Verify

```bash
SpikeRealign --help
ldd $(which SpikeRealign) | grep -i omp    # should show libgomp.so.1
```
