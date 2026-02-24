# ndmanager — Windows Installation

## Package manager

Install [vcpkg](https://vcpkg.io) for dependency management. From an elevated PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "Machine")
[System.Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";C:\vcpkg", "Machine")
```

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload, or the standalone **Build Tools for Visual Studio 2022**. Download: https://visualstudio.microsoft.com/downloads/

2. **CMake** ≥ 3.21 and **Ninja** — included in the Visual Studio C++ workload, or:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

3. **Qt6** via the Qt online installer (recommended) or vcpkg:
   ```powershell
   # Option A — Qt online installer (includes prebuilt binaries, easiest)
   # Download from https://www.qt.io/download-qt-installer and install Qt 6.x MSVC 2022 64-bit

   # Option B — vcpkg (source build, slower)
   vcpkg install qt6-base:x64-windows
   ```

4. **libxml2** via vcpkg:
   ```powershell
   vcpkg install libxml2:x64-windows
   ```

5. **yaml-cpp** via vcpkg:
   ```powershell
   vcpkg install yaml-cpp:x64-windows
   ```

## Build libklustersshared first

ndmanager depends on `libklustersshared`, which must be built from source.

Open a **Developer Command Prompt for VS 2022** (so MSVC is on PATH), then:

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

Adjust `C:\Qt\6.x.x\msvc2022_64` to the actual Qt installation path.

## Build ndmanager

```bat
cmake -B build\ndmanager ndmanager ^
  -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64;C:\neurosuite"
cmake --build build\ndmanager
cmake --install build\ndmanager --prefix C:\neurosuite
```

## Deploy Qt DLLs

Qt applications on Windows require the Qt DLLs to be deployed alongside the executable:

```bat
cd C:\neurosuite\bin
windeployqt6.exe ndmanager.exe
```

`windeployqt6.exe` is in the Qt installation's `bin\` directory (e.g. `C:\Qt\6.x.x\msvc2022_64\bin\`). Add it to PATH or call it with the full path.

## Verify

```bat
C:\neurosuite\bin\ndmanager.exe
```
