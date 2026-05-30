# KiloKlustaKwik — Linux Installation, Intel Arc / SYCL (bare metal)

## Step 1 — Install oneAPI and Intel GPU runtime

Follow **[doc/gpu/README.md — Intel SYCL / oneAPI](../../gpu/README.md#intel-sycl--oneapi-intel-arc)** for complete installation instructions, including the Intel GPU runtime packages required for bare-metal Arc GPUs on Ubuntu 24.04.

After sourcing the oneAPI environment, verify:

```bash
source /opt/intel/oneapi/setvars.sh
clinfo -l        # should show Intel(R) OpenCL Graphics GPU
sycl-ls          # should show level_zero:gpu and opencl:gpu entries
```

## Step 2 — System packages

```bash
sudo apt install cmake build-essential
```

## Step 3 — Build

CMake detects `icpx` automatically when the oneAPI environment is sourced.

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## Verify

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu KiloKlustaKwik --help
KiloKlustaKwik_cpu --help
```

## Note on Meteor Lake iGPUs

Integrated Arc GPUs on Meteor Lake (Core Ultra) share memory with the CPU — SYCL USM allocations are zero-copy on this device. KiloKlustaKwik's feature matrices are typically small enough that transfer overhead is negligible, making the iGPU effective for this workload.
