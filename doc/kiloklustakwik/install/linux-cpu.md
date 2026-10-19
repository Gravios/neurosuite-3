# KiloKlustaKwik — Linux Installation, CPU / OpenMP

KiloKlustaKwik is part of the top-level neurosuite-3 build: follow
**[doc/INSTALL.md → Linux](../../INSTALL.md#linux)**. To build KiloKlustaKwik
alone, skip the other components with `NS_SKIP_*`
([INSTALL.md → Installing one program only](../../INSTALL.md#installing-one-program-only)):

```bash
cmake -B build -DNS_SKIP_NPHYS_DATA=ON -DNS_SKIP_LIBKLUSTERSSHARED=ON \
      -DNS_SKIP_KLUSTERS=ON -DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_NDMANAGER=ON \
      -DNS_SKIP_NDMANAGER_PLUGINS=ON \
      -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF
```

This needs no Qt.

The rest of this page builds `src/kiloklustakwik` on its own.

## System packages

```bash
sudo apt install cmake build-essential libyaml-cpp-dev libgomp1
```

yaml-cpp is required (`find_package(yaml-cpp REQUIRED)`); configure fails
without it.

## Build

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

The build produces `KiloKlustaKwik_cpu`; a CPU-only install also copies it
as `KiloKlustaKwik`.

## Verify

```bash
KiloKlustaKwik --help                        # prints usage and every default
ldd $(which KiloKlustaKwik) | grep -i omp    # should show libgomp.so.1
```

## Note on parallelism

OpenMP parallelises the chunked pipeline (across chunks and per-chunk runs),
which is the default; `-ChunkMinutes 0` selects the non-chunked two-phase CEM.
All available cores are used by default. To limit the thread count:

```bash
export OMP_NUM_THREADS=8
KiloKlustaKwik session 1 ...
```

`SamplingRate` and the spike geometry are read from `session.yaml`.
