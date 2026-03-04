# SpikeRealign — Linux Installation, AMD ROCm / HIP

## Step 1 — Install ROCm

Follow **[doc/gpu/README.md — AMD ROCm / HIP](../../gpu/README.md#amd-rocm--hip)** for complete ROCm 6.x installation instructions.

## Step 2 — System packages

```bash
sudo apt install cmake build-essential
```

## Step 3 — Build

```bash
cd /path/to/neurosuite-3/src/spikerealign

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

To target specific GPU architectures:

```bash
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DSR_HIP_ARCHS="gfx1100;gfx1030;gfx90a"
```

## Verify

```bash
SpikeRealign --help
SpikeRealign_cpu --help
```
