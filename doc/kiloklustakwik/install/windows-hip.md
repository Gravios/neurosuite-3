# KiloKlustaKwik — Windows Installation, AMD HIP

AMD ships a standalone **HIP SDK** for Windows separate from the full Linux ROCm stack. It provides `hipcc`, the HIP headers, and runtime DLLs without requiring any Linux tooling.

Supports: Radeon RX 6000 (RDNA2), RX 7000 (RDNA3), RX 9000 (RDNA4). Older GCN cards (RX 500/Vega) are not supported on Windows. Check system requirements at https://rocm.docs.amd.com/projects/install-on-windows/en/latest/reference/system-requirements.html

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload — `hipcc` on Windows uses MSVC as its host compiler.

2. **AMD HIP SDK** (≥ 5.5, ≥ 6.0 recommended for best CMake support). Download from https://www.amd.com/en/developer/resources/rocm-hub/hip-sdk.html. The installer places the SDK at `C:\Program Files\AMD\ROCm\<version>\` and adds it to PATH.

3. **CMake** ≥ 3.21 and **Ninja**:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

## Verify prerequisites

```bat
hipcc --version
hipconfig --full
```

`hipconfig --full` should show `HIP_PLATFORM=amd`.

## Build

CMake needs the HIP SDK's `hip-lang-config.cmake` via `CMAKE_PREFIX_PATH`:

```bat
cd path\to\neurosuite-3\src\kiloklustakwik
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" ^
         -DUSE_CUDA=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_PREFIX_PATH="C:\Program Files\AMD\ROCm\6.3" ^
         -DCMAKE_BUILD_TYPE=Release
ninja
```

Replace `6.3` with your installed version (shown in `hipconfig --full`).

If CMake still cannot find HIP:

```bat
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_HIP_COMPILER_ROCM_ROOT="C:\Program Files\AMD\ROCm\6.3"
```

To target a specific GPU architecture:

```bat
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_PREFIX_PATH="C:\Program Files\AMD\ROCm\6.3" ^
         -DKK_HIP_ARCHS="gfx1100"
```

> The Visual Studio generator does not support the CMake HIP language on Windows. Always use `-G "Ninja"`.

## Verify

```bat
KiloKlustaKwik.exe --help
KiloKlustaKwik_cpu.exe --help
```
