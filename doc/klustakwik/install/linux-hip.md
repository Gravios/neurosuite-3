# KlustaKwik — Linux Installation, AMD ROCm / HIP

Requires ROCm ≥ 5.0. Supports RDNA2 (RX 6000), RDNA3 (RX 7000), and CDNA2 (Instinct MI210/MI250).

## Step 1 — Install ROCm

```bash
sudo apt install wget gnupg
wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo apt-key add -
echo "deb [arch=amd64] https://repo.radeon.com/rocm/apt/debian/ jammy main" \
    | sudo tee /etc/apt/sources.list.d/rocm.list
sudo apt update
sudo apt install rocm-dev hipcc
```

Add to `~/.bashrc`:

```bash
echo 'export PATH=/opt/rocm/bin:$PATH' >> ~/.bashrc
echo 'export ROCM_PATH=/opt/rocm' >> ~/.bashrc
source ~/.bashrc
```

Verify:

```bash
hipcc --version
rocminfo | grep "Marketing Name"
```

## Step 2 — System packages

```bash
sudo apt install cmake build-essential libgomp1
```

## Step 3 — Build

```bash
cd /path/to/neurosuite-3/src/klustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

The default architecture list covers RDNA2, RDNA3, and CDNA2. To target specific architectures:

```bash
# RDNA3 only (RX 7900 XTX etc.)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_HIP_ARCHS="gfx1100;gfx1101;gfx1102"

# RDNA2 only (RX 6800 XT etc.)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_HIP_ARCHS="gfx1030;gfx1031;gfx1032"

# CDNA2 datacenter (MI210/MI250) — larger block size recommended
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_HIP_ARCHS="gfx90a" -DKK_HIP_BLOCK=512
```

Run `rocminfo` and look for `Name: gfxNNNN` under each agent to find your GPU's identifier.

## Verify

```bash
KlustaKwik --help
KlustaKwik_cpu --help   # CPU fallback always built alongside
```
