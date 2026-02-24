# SpikeRealign — Windows Installation, Intel Arc / SYCL

The GPU backend setup is identical to KlustaKwik. See [KlustaKwik Windows SYCL](../../klustakwik/install/windows-sycl.md) for oneAPI Toolkit and Visual Studio setup, then build SpikeRealign inside the oneAPI command prompt:

```bat
cd path\to\neurosuite-3\src\spikerealign
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_HIP=OFF -DCMAKE_BUILD_TYPE=Release
ninja
```

## Verify

```bat
set ONEAPI_DEVICE_SELECTOR=level_zero:gpu
SpikeRealign.exe --help
SpikeRealign_cpu.exe --help
```
