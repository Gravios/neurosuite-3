# SpikeRealign — Linux Installation, NVIDIA CUDA

The GPU backend setup is identical to KlustaKwik. See [KlustaKwik Linux CUDA](../../klustakwik/install/linux-cuda.md) for CUDA Toolkit installation instructions, then build SpikeRealign:

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
  -DCMAKE_CUDA_ARCHITECTURES="86"   # Ampere (RTX 30xx)
```

## Verify

```bash
SpikeRealign --help
SpikeRealign_cpu --help
```
