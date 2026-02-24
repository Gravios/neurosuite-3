# SpikeRealign — Windows Installation, CPU / OpenMP

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload. Download: https://visualstudio.microsoft.com/downloads/

2. **CMake** ≥ 3.21 and **Ninja**:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

## Build

Open a **Developer Command Prompt for VS 2022**:

```bat
cd path\to\neurosuite-3\src\spikerealign
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" ^
         -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_BUILD_TYPE=Release
ninja
```

`SpikeRealign_cpu.exe` and `SpikeRealign.exe` (same binary when no GPU backend is selected) are built in the `build\` directory. Install:

```bat
cmake --install . --prefix C:\neurosuite
```

## Verify

```bat
C:\neurosuite\bin\SpikeRealign.exe --help
```
