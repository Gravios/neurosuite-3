# KiloKlustaKwik — Linux Installation, CPU / OpenMP

## System packages

```bash
sudo apt install \
  cmake build-essential \
  libgomp1
```

## Build

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## Verify

```bash
KiloKlustaKwik --help
ldd $(which KiloKlustaKwik) | grep -i omp    # should show libgomp.so.1
```

## Note on parallelism

OpenMP is used for parallel chunk processing when `-ChunkMinutes` is set. All available cores are used by default. To limit thread count:

```bash
export OMP_NUM_THREADS=8
KiloKlustaKwik session 1 -ChunkMinutes 5 -SamplingRate 32552 ...
```
