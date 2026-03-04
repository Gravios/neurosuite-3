# SpikeRealign — Linux Installation, Intel Arc / SYCL (bare metal)

## Step 1 — Install oneAPI and Intel GPU runtime

Follow **[doc/gpu/README.md — Intel SYCL / oneAPI](../../gpu/README.md#intel-sycl--oneapi-intel-arc)** for complete installation instructions.

After sourcing the oneAPI environment, verify:

```bash
source /opt/intel/oneapi/setvars.sh
sycl-ls   # should show level_zero:gpu entries
```

## Step 2 — Build

```bash
cd /path/to/neurosuite-3/src/spikerealign

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## Verify

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu SpikeRealign --help
SpikeRealign_cpu --help
```
