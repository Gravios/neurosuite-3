# ndmanager-plugins — Linux Installation, NVIDIA CUDA

GPU acceleration speeds up `process_medianfilter` (used by `ndm_hipass`) and `process_medianthreshold` (used by `ndm_extractspikes`). Both fall back to OpenMP automatically when CUDA is absent, so this guide is only needed if you want GPU acceleration.

Requires CUDA ≥ 12.8 for sm_120 (Blackwell) support. CUDA 12.x is sufficient for sm_86 / sm_89 / sm_100.

## Step 1 — Install CUDA Toolkit

```bash
# Add the CUDA keyring (choose your exact distro at https://developer.nvidia.com/cuda-downloads)
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update
sudo apt install cuda-toolkit-12-x
```

Add `nvcc` to PATH if not already present:

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
sudo apt install \
  cmake build-essential pkg-config \
  libxml2-dev \
  libyaml-cpp-dev \
  python3-yaml \
  libgomp1
```

## Step 3 — Build

CUDA is detected automatically when `nvcc` is on PATH. No extra flags required.

```bash
cd /path/to/neurosuite-3/src

cmake -B build/ndmanager-plugins ndmanager-plugins \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager-plugins -j$(nproc)
sudo cmake --install build/ndmanager-plugins
```

To target a specific GPU architecture:

```bash
cmake -B build/ndmanager-plugins ndmanager-plugins \
  -DCMAKE_CUDA_ARCHITECTURES="86"   # Ampere (RTX 30xx)
  # 89 = Ada Lovelace (RTX 40xx), 100 = Hopper, 120 = Blackwell
```

To force CPU-only despite having CUDA installed:

```bash
cmake -B build/ndmanager-plugins ndmanager-plugins \
  -DUSE_CUDA=OFF \
  -DCMAKE_BUILD_TYPE=Release
```

## Step 4 — Verify

```bash
process_medianfilter --version
```

Should print:

```
process_medianfilter (CUDA, sm_XX)
```

If it prints `(OpenMP, ...)` instead, CMake did not detect CUDA — check that `nvcc --version` works and re-run CMake from a clean build directory.

## Performance notes

`chunkSize` in the `ndm_hipass` parameter block controls how much data is loaded into GPU memory per pass. The default is 64 GiB which is fine for systems with ample RAM; reduce it if you see out-of-memory errors:

```yaml
- name: ndm_hipass
  parameters:
    - name: chunkSize
      value: 8000000000    # 8 GB
```

## Uninstall

```bash
sudo xargs rm -f < build/ndmanager-plugins/install_manifest.txt
```
