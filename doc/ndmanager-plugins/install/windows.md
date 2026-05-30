# ndmanager-plugins — Windows Installation

## Platform support overview

ndmanager-plugins has two components with different Windows compatibility:

**C++ processing binaries** (`process_medianfilter`, `process_pca`, `process_extractspikes`, etc.) — fully portable, build natively with MSVC or MinGW.

**Bash pipeline scripts** (`ndm_start`, `ndm_hipass`, `ndm_pca`, etc.) — require a Bash environment. On Windows, choose one of:

- **WSL2 (recommended)** — run the full pipeline inside WSL Ubuntu exactly as on bare-metal Linux. See the [Linux CPU guide](linux-cpu.md) inside WSL.
- **MSYS2** — provides Bash and GNU tools natively on Windows; the scripts run but some GNU utilities (`bc`, `awk`, `xpathReader`) must be present.
- **Git Bash** — limited; missing several utilities required by the scripts. Not recommended for the full pipeline.

If you only need the processing binaries (to call them directly without the ndm_* wrapper scripts), native Windows builds work without any of the above.

> **Note:** `process_extractspikes` uses `sys/sysinfo.h`, a Linux-only header for querying available RAM. This prevents a native Windows build of that specific binary. All other binaries build without issue. If you need `ndm_extractspikes`, use WSL2.

---

## Option A — WSL2 (full pipeline, recommended)

Install WSL2 with Ubuntu 24.04, then follow the [Linux CPU guide](linux-cpu.md) or [Linux CUDA guide](linux-cuda.md) inside the WSL environment. The ndm_* scripts run without modification and can process files on the Windows filesystem via `/mnt/c/...` paths.

To install WSL2:

```powershell
wsl --install -d Ubuntu-24.04
```

Then restart and follow the [Linux CPU guide](linux-cpu.md) inside the WSL terminal.

---

## Option B — Native Windows build (processing binaries only)

Builds all binaries that do not require `sys/sysinfo.h`. This covers `process_medianfilter`, `process_pca`, `process_resample`, `process_mergefeatures`, and the video tools, but **not** `process_extractspikes`.

### Prerequisites

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

4. **libxml2** and **yaml-cpp**:
   ```powershell
   vcpkg install libxml2:x64-windows yaml-cpp:x64-windows
   ```

5. **python3** with pyyaml (for YAML reading in scripts if using MSYS2 later):
   ```powershell
   winget install Python.Python.3
   pip install pyyaml
   ```

6. **FFmpeg** (optional — video tools):
   ```powershell
   vcpkg install ffmpeg:x64-windows
   ```

### Build (CPU / OpenMP)

Open a **Developer Command Prompt for VS 2022**:

```bat
cd path\to\neurosuite-3\src

cmake -B build\ndmanager-plugins ndmanager-plugins ^
  -G "Ninja" ^
  -DUSE_CUDA=OFF ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build\ndmanager-plugins
cmake --install build\ndmanager-plugins --prefix C:\neurosuite
```

### Build with CUDA

```bat
cmake -B build\ndmanager-plugins ndmanager-plugins ^
  -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build\ndmanager-plugins
```

See [KiloKlustaKwik Windows CUDA](../../kiloklustakwik/install/windows-cuda.md) for CUDA Toolkit installation instructions — the setup is identical.

---

## Option C — MSYS2 (native Bash, full pipeline)

MSYS2 provides a minimal Linux-like environment on Windows with Bash, GNU coreutils, and a package manager (`pacman`). The ndm_* scripts run natively, but `process_extractspikes` still cannot build due to `sys/sysinfo.h`.

### Install MSYS2

Download and run the MSYS2 installer from https://www.msys2.org/. After installation, open the **MSYS2 UCRT64** shell and update:

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libxml2 \
  mingw-w64-ucrt-x86_64-yaml-cpp \
  mingw-w64-ucrt-x86_64-python-yaml \
  python bc
```

### Build

```bash
cd /path/to/neurosuite-3/src

cmake -B build/ndmanager-plugins ndmanager-plugins \
  -G "Ninja" \
  -DUSE_CUDA=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager-plugins -j$(nproc)
cmake --install build/ndmanager-plugins --prefix /ucrt64
```

The binaries and scripts are installed into the MSYS2 prefix and are accessible from the MSYS2 shell.
