# KlustaKwik — Windows Installation, Intel Arc / SYCL

## Prerequisites

Install in this order:

1. **Visual Studio 2022** with the **Desktop development with C++** workload (or at minimum MSVC v143 build tools and Windows SDK). CMake and Ninja are included — tick them in the installer. Download: https://visualstudio.microsoft.com/downloads/

2. **Intel oneAPI Base Toolkit** (≥ 2023.1). The quickest install:
   ```powershell
   winget install Intel.OneAPI.BaseToolkit
   ```
   Or download from https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html. The C++ Essentials bundle is a smaller download sufficient for building KlustaKwik.

3. **Intel GPU driver** — install via the Intel Driver & Support Assistant or from https://www.intel.com/content/www/us/en/download/785597/intel-arc-iris-xe-graphics-windows.html. Minimum required version: **31.0.101.4887**.

## Open the oneAPI command prompt

The oneAPI installer adds a Start Menu shortcut: **Intel oneAPI command prompt for Intel oneAPI Toolkit 2025**. Opening it runs `setvars.bat` automatically. All subsequent steps must run inside this prompt.

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

Inside the oneAPI command prompt, from the KlustaKwik source directory:

```bat
cd path\to\neurosuite-3\src\klustakwik
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_HIP=OFF -DCMAKE_BUILD_TYPE=Release
ninja
```

CMake auto-detects `icpx` from PATH when inside the oneAPI prompt. No `-DCMAKE_CXX_COMPILER=icpx` flag needed.

To add AOT targets (avoids JIT compilation delay at first run):

```bat
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_HIP=OFF ^
         -DKK_SYCL_DEVICES="mtl-m,acm-g10,bmg-g21"
```

The default build includes AOT targets for `mtl-m` (Meteor Lake), `acm-g10/g11` (Arc Alchemist), `dg2`, and `pvc`. Other devices fall back to JIT at first run.

## Verify

```bat
set ONEAPI_DEVICE_SELECTOR=level_zero:gpu
KlustaKwik.exe --help
KlustaKwik_cpu.exe --help
```
