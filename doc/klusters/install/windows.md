# klusters — Windows Installation

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload. Download: https://visualstudio.microsoft.com/downloads/

2. **CMake** ≥ 3.21 and **Ninja**:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

3. **vcpkg**:
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```

4. **Qt6** — Qt online installer recommended (https://www.qt.io/download-qt-installer), install **Qt 6.x MSVC 2022 64-bit** and tick **Qt SVG**.

   Or via vcpkg:
   ```powershell
   vcpkg install qt6-base:x64-windows qt6-svg:x64-windows
   ```

5. **yaml-cpp**:
   ```powershell
   vcpkg install yaml-cpp:x64-windows
   ```

## Build libklustersshared first

klusters depends on `libklustersshared`. Open a **Developer Command Prompt for VS 2022**:

```bat
cd path\to\neurosuite-3\src

cmake -B build\libklustersshared libklustersshared ^
  -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build\libklustersshared
cmake --install build\libklustersshared --prefix C:\neurosuite
```

## Build klusters

```bat
cmake -B build\klusters klusters ^
  -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64;C:\neurosuite"
cmake --build build\klusters
cmake --install build\klusters --prefix C:\neurosuite
```

## Deploy Qt DLLs

```bat
cd C:\neurosuite\bin
windeployqt6.exe klusters.exe
```

## Verify

```bat
C:\neurosuite\bin\klusters.exe
```
