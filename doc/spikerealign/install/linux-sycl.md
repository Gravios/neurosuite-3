# SpikeRealign — Linux Installation, Intel Arc / SYCL (bare metal)

The GPU backend setup is identical to KlustaKwik. See [KlustaKwik Linux SYCL](../../klustakwik/install/linux-sycl.md) for oneAPI and GPU runtime installation instructions, then build SpikeRealign:

```bash
# Ensure oneAPI environment is active
source /opt/intel/oneapi/setvars.sh

cd /path/to/neurosuite-3/src/spikerealign

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## Verify

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu SpikeRealign --help
SpikeRealign_cpu --help
```
