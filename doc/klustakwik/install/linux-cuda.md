# KlustaKwik — Linux Installation, NVIDIA CUDA

Requires CUDA Toolkit ≥ 11. CUDA 12 is recommended.

## Step 1 — Install CUDA Toolkit

```bash
# Choose your exact distro at https://developer.nvidia.com/cuda-downloads
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update
sudo apt install cuda-toolkit-12-x
```

Add to `~/.bashrc` if `nvcc` is not on PATH after install:

```bash
echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

Verify:

```bash
nvcc --version
nvidia-smi
```

## Step 2 — System packages

```bash
sudo apt install cmake build-essential libgomp1
```

## Step 3 — Build

CMake detects `nvcc` automatically. `KlustaKwik_cpu` is always built alongside the GPU binary.

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
  -DCMAKE_CUDA_ARCHITECTURES="86"   # Ampere (RTX 30xx)
  # 89 = Ada Lovelace (RTX 40xx), 90 = Hopper (H100)
```

## Verify

```bash
KlustaKwik --help       # GPU binary
KlustaKwik_cpu --help   # CPU fallback
```
