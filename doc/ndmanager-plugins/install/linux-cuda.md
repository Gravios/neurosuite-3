# ndmanager-plugins — Linux Installation, NVIDIA CUDA

GPU acceleration speeds up `process_medianfilter` (used by `ndm_hipass`), `process_medianthreshold` (used by `ndm_extractspikes`), and `process_spikegrouper` (used by `ndm_spikegrouper`). All three fall back to OpenMP automatically when CUDA is absent, so this guide is only needed if you want GPU acceleration.

Requires CUDA ≥ 12.8 for sm_120 (Blackwell). CUDA 12.x is sufficient for sm_86 / sm_89 / sm_100.

## Step 1 — Install CUDA Toolkit

Follow **[doc/gpu/README.md — NVIDIA CUDA](../../gpu/README.md#nvidia-cuda)** for complete installation instructions, including CUDA 12.8 for RTX 5000 series and Secure Boot MOK key signing.

## Step 2 — System packages

```bash
sudo apt install \
  cmake build-essential pkg-config \
  libxml2-dev libyaml-cpp-dev python3-yaml libgomp1
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
  -DCMAKE_CUDA_ARCHITECTURES="86;89;120"
  # 86 = Ampere (RTX 30xx), 89 = Ada (RTX 40xx), 120 = Blackwell (RTX 50xx)
```

To force CPU-only despite having CUDA installed:

```bash
cmake -B build/ndmanager-plugins ndmanager-plugins \
  -DUSE_CUDA=OFF -DCMAKE_BUILD_TYPE=Release
```

## Verify

```bash
process_medianfilter --version
```

Should print `process_medianfilter (CUDA, sm_XX)`. If it prints `(OpenMP, ...)` instead, CMake did not detect CUDA — check that `nvcc --version` works and re-run from a clean build directory.

## Performance notes

`chunkSize` in the `ndm_hipass` parameter block controls how much data is loaded per GPU pass. Reduce from the default if you see out-of-memory errors:

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
