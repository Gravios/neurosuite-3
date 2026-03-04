# KlustaKwik — Linux Installation, NVIDIA CUDA

## Step 1 — Install CUDA Toolkit

Follow **[doc/gpu/README.md — NVIDIA CUDA](../../gpu/README.md#nvidia-cuda)** for complete installation instructions, including CUDA 12.8 for RTX 5000 series (Blackwell / sm_120) and Secure Boot MOK key signing.

## Step 2 — System packages

```bash
sudo apt install cmake build-essential libgomp1
```

## Step 3 — Build

CMake auto-detects `nvcc`. `KlustaKwik_cpu` is always built alongside the GPU binary.

```bash
cd /path/to/neurosuite-3/src/klustakwik

cmake -B build \
  -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

To target a specific GPU architecture:

```bash
cmake -B build \
  -DUSE_HIP=OFF -DUSE_SYCL=OFF \
  -DCMAKE_CUDA_ARCHITECTURES="86;89;120"
```

Common targets: `86` = Ampere (RTX 30xx), `89` = Ada Lovelace (RTX 40xx), `120` = Blackwell (RTX 50xx, requires CUDA ≥ 12.8).

## Verify

```bash
KlustaKwik --help       # GPU binary
KlustaKwik_cpu --help   # CPU fallback
```
