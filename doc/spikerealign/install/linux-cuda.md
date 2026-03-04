# SpikeRealign — Linux Installation, NVIDIA CUDA

## Step 1 — Install CUDA Toolkit

Follow **[doc/gpu/README.md — NVIDIA CUDA](../../gpu/README.md#nvidia-cuda)** for complete installation instructions, including CUDA 12.8 for RTX 5000 series (Blackwell / sm_120) and Secure Boot MOK key signing.

## Step 2 — System packages

```bash
sudo apt install cmake build-essential libgomp1
```

## Step 3 — Build

```bash
cd /path/to/neurosuite-3/src/spikerealign

cmake -B build \
  -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

`SpikeRealign_cpu` is always built alongside the GPU binary.

To target a specific GPU architecture:

```bash
cmake -B build -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_CUDA_ARCHITECTURES="86;89;120"
```

## Verify

```bash
SpikeRealign --help
SpikeRealign_cpu --help
```
