# SpikeRealign — Windows Installation, NVIDIA CUDA

The GPU backend setup is identical to KlustaKwik. See [KlustaKwik Windows CUDA](../../klustakwik/install/windows-cuda.md) for CUDA Toolkit and Visual Studio setup, then build SpikeRealign:

```bat
cd path\to\neurosuite-3\src\spikerealign
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" -DUSE_HIP=OFF -DUSE_SYCL=OFF -DCMAKE_BUILD_TYPE=Release
ninja
```

## Verify

```bat
SpikeRealign.exe --help
SpikeRealign_cpu.exe --help
```
