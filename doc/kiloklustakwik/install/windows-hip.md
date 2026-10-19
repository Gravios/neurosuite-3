# KiloKlustaKwik — Windows Installation, AMD HIP

> **Status (2026-10):** KiloKlustaKwik is not part of the tested Windows build:
> the Windows CI job and the `windows` presets skip it
> (`-DNS_SKIP_KILOKLUSTAKWIK=ON`), and it has not been ported the way the
> plugins were. The steps below are untested. To cluster on a Windows machine
> today, run it under WSL2 —
> see [Installing neurosuite-3 → WSL2](../../INSTALL.md#kiloklustakwik-and-the-full-pipeline-wsl2).

AMD ships a standalone **HIP SDK** for Windows separate from the full Linux ROCm stack. It provides `hipcc`, the HIP headers, and runtime DLLs without requiring any Linux tooling.

Supports: Radeon RX 6000 (RDNA2), RX 7000 (RDNA3), RX 9000 (RDNA4). Older GCN cards (RX 500/Vega) are not supported on Windows. Check system requirements at https://rocm.docs.amd.com/projects/install-on-windows/en/latest/reference/system-requirements.html

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload — `hipcc` on Windows uses MSVC as its host compiler.

2. **AMD HIP SDK** (≥ 5.5). Download from https://www.amd.com/en/developer/resources/rocm-hub/hip-sdk.html. The installer places the SDK at `C:\Program Files\AMD\ROCm\<version>\` and adds it to PATH.

3. **CMake** ≥ 3.22 and **Ninja**:
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

```bat
hipcc --version
hipconfig --full
```

`hipconfig --full` should show `HIP_PLATFORM=amd`.

## Build

KiloKlustaKwik does not use CMake's HIP language: it looks for `hipcc` with
`find_program` in `%ROCM_PATH%\bin` (and `/opt/rocm/bin`, `/usr/bin`), and
compiles the kernels with it. Make sure `ROCM_PATH` points at the SDK (the
installer sets `HIP_PATH`; set `ROCM_PATH` to the same directory if it is
missing):

```bat
set ROCM_PATH=C:\Program Files\AMD\ROCm\6.3
cd path\to\neurosuite-3\src\kiloklustakwik
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" ^
         -DUSE_CUDA=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
         -DCMAKE_BUILD_TYPE=Release
ninja
```

Replace `6.3` with your installed version (shown in `hipconfig --full`). The
configure output reports `KiloKlustaKwik: HIP available — <path to hipcc>`.

The default targets are `gfx1030;gfx1031;gfx1100;gfx1101;gfx90a;gfx908`. To
target a specific GPU architecture:

```bat
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
         -DKK_EXP_HIP_ARCHS="gfx1100"
```

## Verify

```bat
KiloKlustaKwik.exe --help
KiloKlustaKwik_cpu.exe --help
```
