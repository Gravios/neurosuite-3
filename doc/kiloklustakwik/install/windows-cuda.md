# KiloKlustaKwik — Windows Installation, NVIDIA CUDA

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload — `nvcc` requires MSVC as its host compiler. Download: https://visualstudio.microsoft.com/downloads/

2. **CUDA Toolkit** (≥ 12 recommended). Download the Windows installer from https://developer.nvidia.com/cuda-downloads — select Windows → x86_64 → Windows 11 → exe. The CUDA installer includes a bundled GPU driver; accept it unless you have a newer driver already installed.

3. **CMake** ≥ 3.21 and **Ninja** — included in the Visual Studio C++ workload, or install separately:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

## Verify prerequisites

Open any `cmd.exe` or PowerShell (the CUDA installer adds `nvcc` to PATH):

```bat
nvcc --version
nvidia-smi
```

If `nvcc` is not found, add to system PATH: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.x\bin`

## Build

```bat
cd path\to\neurosuite-3\src\kiloklustakwik
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" -DUSE_HIP=OFF -DUSE_SYCL=OFF -DCMAKE_BUILD_TYPE=Release
ninja
```

`KiloKlustaKwik_cpu.exe` is always built alongside the GPU binary.

To target a specific GPU architecture:

```bat
cmake .. -G "Ninja" -DUSE_HIP=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_CUDA_ARCHITECTURES="89"   # Ada Lovelace (RTX 40xx)
```

Alternatively, using the Visual Studio generator:

```bat
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_HIP=OFF -DUSE_SYCL=OFF
cmake --build . --config Release
```

## Verify

```bat
KiloKlustaKwik.exe --help
KiloKlustaKwik_cpu.exe --help
```
