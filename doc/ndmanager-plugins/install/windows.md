# ndmanager-plugins — Windows Installation

## Platform support overview

ndmanager-plugins has two components with different Windows compatibility:

**C++ processing binaries** (`process_medianfilter`, `process_pca`, `process_extractspikes`, etc.) — build natively with MSVC (checked by the `windows` CI job) and with MinGW-w64. Large recordings are read through a Windows file mapping and 64-bit file offsets, and binary files are opened in binary mode, so the outputs match Linux byte for byte (checked with MinGW-w64 builds under Wine on a synthetic session).

**Bash pipeline scripts** (`ndm_start`, `ndm_hipass`, `ndm_pca`, etc.) — require a Bash environment. On Windows, choose one of:

- **WSL2 (recommended)** — run the full pipeline inside WSL Ubuntu exactly as on bare-metal Linux. See the [Linux CPU guide](linux-cpu.md) inside WSL.
- **MSYS2** — provides Bash and GNU tools natively on Windows; the scripts run but some GNU utilities (`bc`, `awk`, `xpathReader`) must be present.
- **Git Bash** — limited; missing several utilities required by the scripts. Not recommended for the full pipeline.

If you only need the processing binaries (to call them directly without the ndm_* wrapper scripts), native Windows builds work without any of the above.

Not built natively yet: the GPU backends of `process_extractemg` with SYCL (a Windows SYCL build takes its CPU path) and KiloKlustaKwik. `process_aomconvert` needs HDF5 and `process_extractleds` needs FFmpeg; without them they are skipped at configure time.

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

Builds every processing binary. The simplest route is the whole monorepo from its root (see [BUILD.md](../../BUILD.md)): the plugins are built with the GUI programs, and `cmake --install` copies the vcpkg DLLs (GSL, libxml2, yaml-cpp) next to them. The steps below build the plugins on their own.

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

4. **GSL**, **libxml2** and **yaml-cpp** (required), **libsamplerate** (optional; a bundled copy is compiled otherwise), **HDF5** (optional; `process_aomconvert`):
   ```powershell
   vcpkg install gsl:x64-windows libxml2:x64-windows yaml-cpp:x64-windows
   vcpkg install libsamplerate:x64-windows hdf5:x64-windows
   ```

5. **python3** with PyYAML, NumPy and SciPy (the Python pipeline steps):
   ```powershell
   winget install Python.Python.3
   pip install pyyaml numpy scipy
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

Built on their own, the plugins' install does not copy the vcpkg DLLs: put `C:\vcpkg\installed\x64-windows\bin` on `PATH`, or build from the monorepo root, whose install copies them.

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

MSYS2 provides a minimal Linux-like environment on Windows with Bash, GNU coreutils, and a package manager (`pacman`). The ndm_* scripts run natively.

### Install MSYS2

Download and run the MSYS2 installer from https://www.msys2.org/. After installation, open the **MSYS2 UCRT64** shell and update:

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libxml2 \
  mingw-w64-ucrt-x86_64-gsl \
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
