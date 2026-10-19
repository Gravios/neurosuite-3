# KiloKlustaKwik — Linux Installation, NVIDIA CUDA

KiloKlustaKwik is part of the top-level neurosuite-3 build: install the CUDA
toolkit (Step 1), then follow **[doc/INSTALL.md → Linux](../../INSTALL.md#linux)**;
CUDA is auto-detected. To build KiloKlustaKwik alone, use the `NS_SKIP_*`
flags shown in [linux-cpu.md](linux-cpu.md) (without `-DUSE_CUDA=OFF`). The
build steps below are for `src/kiloklustakwik` on its own.

## Step 1 — Install CUDA Toolkit

Follow **[doc/gpu/README.md — NVIDIA CUDA](../../gpu/README.md#nvidia-cuda)** for complete installation instructions, including CUDA 12.8 for RTX 5000 series (Blackwell / sm_120) and Secure Boot MOK key signing.

## Step 2 — System packages

```bash
sudo apt install cmake build-essential libyaml-cpp-dev libgomp1
```

yaml-cpp is required; configure fails without it.

## Step 3 — Build

CMake auto-detects `nvcc`. `KiloKlustaKwik_cpu` is always built alongside the GPU binary.

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

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
KiloKlustaKwik --help       # GPU binary
KiloKlustaKwik_cpu --help   # CPU fallback
```
