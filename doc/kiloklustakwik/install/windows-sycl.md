# KiloKlustaKwik — Windows Installation, Intel Arc / SYCL

> **Status (2026-10):** KiloKlustaKwik is not part of the tested Windows build:
> the Windows CI job and the `windows` presets skip it
> (`-DNS_SKIP_KILOKLUSTAKWIK=ON`), and it has not been ported the way the
> plugins were. The steps below are untested. To cluster on a Windows machine
> today, run it under WSL2 —
> see [Installing neurosuite-3 → WSL2](../../INSTALL.md#kiloklustakwik-and-the-full-pipeline-wsl2).

## Prerequisites

Install in this order:

1. **Visual Studio 2022** with the **Desktop development with C++** workload (or at minimum MSVC v143 build tools and Windows SDK). CMake (≥ 3.22) and Ninja are included — tick them in the installer. Download: https://visualstudio.microsoft.com/downloads/

2. **Intel oneAPI Base Toolkit** (≥ 2023.1). The quickest install:
   ```powershell
   winget install Intel.OneAPI.BaseToolkit
   ```
   Or download from https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html. The C++ Essentials bundle is a smaller download sufficient for building KiloKlustaKwik.

3. **Intel GPU driver** — install via the Intel Driver & Support Assistant or from https://www.intel.com/content/www/us/en/download/785597/intel-arc-iris-xe-graphics-windows.html. Minimum required version: **31.0.101.4887**.

4. **yaml-cpp** — required (`find_package(yaml-cpp REQUIRED)`); install it with vcpkg:
   ```bat
   C:\vcpkg\vcpkg install yaml-cpp:x64-windows
   ```
   and pass `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake` to `cmake`. `scripts\install-deps-windows.bat` installs vcpkg and yaml-cpp ([BUILD.md §1](../../BUILD.md#1-toolchain)).

## Open the oneAPI command prompt

The oneAPI installer adds a Start Menu shortcut: **Intel oneAPI command prompt for Intel 64 for Visual Studio 2022**. Opening it runs `setvars.bat` automatically. All subsequent steps must run inside this prompt.

To activate manually:
```bat
"C:\Program Files (x86)\Intel\oneAPI\setvars.bat"
```

Or in PowerShell:
```powershell
cmd.exe /K '"C:\Program Files (x86)\Intel\oneAPI\setvars.bat" && powershell'
```

## Verify

Inside the oneAPI prompt:

```bat
icpx --version
sycl-ls
```

`sycl-ls` should list your Intel GPU under `[level_zero:gpu]` or `[opencl:gpu]`. If only CPU appears, update the Intel GPU driver.

## Build

Inside the oneAPI command prompt, from the KiloKlustaKwik source directory:

```bat
cd path\to\neurosuite-3\src\kiloklustakwik
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_HIP=OFF -DCMAKE_BUILD_TYPE=Release ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
ninja
```

In this standalone build CMake does not take `icpx` from `PATH`: it searches the
oneAPI locations in `NS_GPU_SYCL_HINTS` (`%CMPLR_ROOT%\bin`,
`%ONEAPI_ROOT%\compiler\latest\bin`, …), which the oneAPI prompt sets, and
selects it as the compiler unless `-DCMAKE_CXX_COMPILER` or `CXX` is set. The
top-level `windows-sycl` preset builds Klusters with SYCL but skips
KiloKlustaKwik ([INSTALL.md → Intel GPU (SYCL)](../../INSTALL.md#intel-gpu-sycl)).

To add AOT targets (avoids JIT compilation delay at first run):

```bat
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_HIP=OFF ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
         -DKK_EXP_SYCL_DEVICES="mtl-m,acm-g10,bmg-g21"
```

When `ocloc` is found, the default build includes AOT targets for `mtl-m` (Meteor Lake), `acm-g10/g11` (Arc Alchemist), `dg2`, and `pvc` (`KK_EXP_SYCL_DEVICES`). Other devices fall back to JIT at first run.

## Verify

```bat
set ONEAPI_DEVICE_SELECTOR=level_zero:gpu
KiloKlustaKwik_sycl.exe --help
KiloKlustaKwik_cpu.exe --help
```

The build names the SYCL binary `KiloKlustaKwik_sycl.exe`; `cmake --install` also copies it as `KiloKlustaKwik.exe`.
