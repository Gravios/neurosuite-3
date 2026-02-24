# SpikeRealign — WSL2 Installation, Intel Arc / SYCL

The GPU backend setup is identical to KlustaKwik. See [KlustaKwik WSL2 SYCL](../../klustakwik/install/wsl2-sycl.md) for full WSL2 setup instructions, then build SpikeRealign:

```bash
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
