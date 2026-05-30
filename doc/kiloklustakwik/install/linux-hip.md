# KiloKlustaKwik — Linux Installation, AMD ROCm / HIP

## Step 1 — Install ROCm

Follow **[doc/gpu/README.md — AMD ROCm / HIP](../../gpu/README.md#amd-rocm--hip)** for complete ROCm 6.x installation instructions.

## Step 2 — System packages

```bash
sudo apt install cmake build-essential
```

## Step 3 — Build

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

To target specific GPU architectures (find yours with `rocminfo | grep "Name: gfx"`):

```bash
# RDNA3 (RX 7000 series)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_HIP_ARCHS="gfx1100;gfx1101;gfx1102"

# RDNA2 (RX 6000 series)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_HIP_ARCHS="gfx1030;gfx1031;gfx1032"

# CDNA2 (MI200 series)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_HIP_ARCHS="gfx90a" -DKK_HIP_BLOCK=512
```

## Verify

```bash
KiloKlustaKwik --help
KiloKlustaKwik_cpu --help
```
