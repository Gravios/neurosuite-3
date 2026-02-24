# SpikeRealign — Linux Installation, AMD ROCm / HIP

The GPU backend setup is identical to KlustaKwik. See [KlustaKwik Linux HIP](../../klustakwik/install/linux-hip.md) for ROCm installation instructions, then build SpikeRealign:

```bash
cd /path/to/neurosuite-3/src/spikerealign

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

To target specific architectures:

```bash
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
  -DKK_HIP_ARCHS="gfx1100;gfx1101;gfx1102"   # RDNA3
```

## Verify

```bash
SpikeRealign --help
SpikeRealign_cpu --help
```
