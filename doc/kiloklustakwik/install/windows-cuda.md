# KiloKlustaKwik — Windows Installation, NVIDIA CUDA

> **Status (2026-10):** KiloKlustaKwik is not part of the tested Windows build:
> the Windows CI job and the `windows` presets skip it
> (`-DNS_SKIP_KILOKLUSTAKWIK=ON`), and it has not been ported the way the
> plugins were. The steps below are untested. To cluster on a Windows machine
> today, run it under WSL2 —
> see [Installing neurosuite-3 → WSL2](../../INSTALL.md#kiloklustakwik-and-the-full-pipeline-wsl2).

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload — `nvcc` requires MSVC as its host compiler. Download: https://visualstudio.microsoft.com/downloads/

2. **CUDA Toolkit** (≥ 12 recommended). Download the Windows installer from https://developer.nvidia.com/cuda-downloads — select Windows → x86_64 → Windows 11 → exe. The CUDA installer includes a bundled GPU driver; accept it unless you have a newer driver already installed.

3. **CMake** ≥ 3.22 and **Ninja** — included in the Visual Studio C++ workload, or install separately:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

4. **yaml-cpp** — required (`find_package(yaml-cpp REQUIRED)`); install it with vcpkg and pass the vcpkg toolchain to CMake:
   ```bat
   C:\vcpkg\vcpkg install yaml-cpp:x64-windows
   ```
   then add `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake` to the `cmake` commands below. `scripts\install-deps-windows.bat` installs vcpkg and yaml-cpp ([BUILD.md §1](../../BUILD.md#1-toolchain)).

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
cmake .. -G "Ninja" -DUSE_HIP=OFF -DUSE_SYCL=OFF -DCMAKE_BUILD_TYPE=Release ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
ninja
```

`KiloKlustaKwik_cpu.exe` is always built alongside the GPU binary.

To target a specific GPU architecture:

```bat
cmake .. -G "Ninja" -DUSE_HIP=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
         -DCMAKE_CUDA_ARCHITECTURES="89"
```

`89` = Ada Lovelace (RTX 40xx).

Alternatively, using the Visual Studio generator:

```bat
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_HIP=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

## Verify

```bat
KiloKlustaKwik.exe --help
KiloKlustaKwik_cpu.exe --help
```
