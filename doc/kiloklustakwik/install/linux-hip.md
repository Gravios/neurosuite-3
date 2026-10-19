# KiloKlustaKwik — Linux Installation, AMD ROCm / HIP

KiloKlustaKwik is part of the top-level neurosuite-3 build: install ROCm
(Step 1), then follow **[doc/INSTALL.md → Linux](../../INSTALL.md#linux)**;
HIP is auto-detected when no CUDA toolkit is found. To build KiloKlustaKwik
alone, use the `NS_SKIP_*` flags shown in [linux-cpu.md](linux-cpu.md)
(without `-DUSE_HIP=OFF`). The build steps below are for
`src/kiloklustakwik` on its own.

## Step 1 — Install ROCm

Follow **[doc/gpu/README.md — AMD ROCm / HIP](../../gpu/README.md#amd-rocm--hip)** for complete ROCm 6.x installation instructions.

## Step 2 — System packages

```bash
sudo apt install cmake build-essential libyaml-cpp-dev
```

yaml-cpp is required; configure fails without it.

## Step 3 — Build

CMake finds `hipcc` in `$ROCM_PATH/bin`, `/opt/rocm/bin` or `/usr/bin`.

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

The default targets are `gfx1030;gfx1031;gfx1100;gfx1101;gfx90a;gfx908`
(`KK_EXP_HIP_ARCHS`). To target specific GPU architectures (find yours with
`rocminfo | grep "Name: gfx"`):

```bash
# RDNA3 (RX 7000 series)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_EXP_HIP_ARCHS="gfx1100;gfx1101;gfx1102"

# RDNA2 (RX 6000 series)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_EXP_HIP_ARCHS="gfx1030;gfx1031;gfx1032"

# CDNA2 (MI200 series)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_EXP_HIP_ARCHS="gfx90a"
```

## Verify

```bash
KiloKlustaKwik --help
KiloKlustaKwik_cpu --help
```
