# SpikeRealign — Windows Installation, AMD HIP

The GPU backend setup is identical to KlustaKwik. See [KlustaKwik Windows HIP](../../klustakwik/install/windows-hip.md) for HIP SDK and Visual Studio setup, then build SpikeRealign:

```bat
cd path\to\neurosuite-3\src\spikerealign
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" ^
         -DUSE_CUDA=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_PREFIX_PATH="C:\Program Files\AMD\ROCm\6.3" ^
         -DCMAKE_BUILD_TYPE=Release
ninja
```

## Verify

```bat
SpikeRealign.exe --help
SpikeRealign_cpu.exe --help
```
