# neuroscope — Windows Installation

## Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload (or Build Tools). Download: https://visualstudio.microsoft.com/downloads/

2. **CMake** ≥ 3.21 and **Ninja**:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

3. **vcpkg** for dependency management:
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```

4. **Qt6** — Qt online installer is recommended (prebuilt binaries, no wait):
   Download from https://www.qt.io/download-qt-installer and install **Qt 6.x MSVC 2022 64-bit**.

   Alternatively via vcpkg (slow source build):
   ```powershell
   vcpkg install qt6-base:x64-windows
   ```

5. **libxml2** and **yaml-cpp**:
   ```powershell
   vcpkg install libxml2:x64-windows yaml-cpp:x64-windows
   ```

6. **FFmpeg** (optional — video display support):
   ```powershell
   vcpkg install ffmpeg:x64-windows
   ```

## Build

NeuroScope does **not** depend on `libklustersshared`. Open a **Developer Command Prompt for VS 2022**, then:

```bat
cd path\to\neurosuite-3\src

cmake -B build\neuroscope neuroscope ^
  -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build\neuroscope
cmake --install build\neuroscope --prefix C:\neurosuite
```

## Deploy Qt DLLs

```bat
cd C:\neurosuite\bin
windeployqt6.exe neuroscope.exe
```

## Verify

```bat
C:\neurosuite\bin\neuroscope.exe
```
