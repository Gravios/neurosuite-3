# ndmanager-plugins — Windows Installation

## Platform support overview

The plugins are built and installed with the rest of neurosuite-3: follow
**[Installing neurosuite-3 → Windows](../../INSTALL.md#windows)**. After that:

**Processing programs** (`process_butterworth`, `process_pca`,
`process_extractspikes`, ...) are native Windows programs in `C:\ns3\bin`, on
your PATH. Large recordings are read through a Windows file mapping with
64-bit offsets and binary files in binary mode, so the outputs match Linux
byte for byte (checked with MinGW-w64 builds under Wine on a synthetic
session; the MSVC build is checked by the `windows` CI job).

**Pipeline steps** (`ndm_start`, `ndm_hipass`, `ndm_pca`, ...) are bash
scripts. Each is installed with a `.bat` launcher beside it (`ndm_hipass.bat`,
...) that runs it with Git for Windows' bash, so `ndm_hipass` works from cmd
and PowerShell; the extension-less Python scripts (`ndm_xml2yaml`, ...) run
with Python. The launchers look for Git Bash in the usual places or beside
`git.exe` (`NS_BASH` overrides, e.g. `setx NS_BASH "C:\...\Git\bin\bash.exe"`)
and for Python as `py -3` or `python` (`NS_PYTHON` overrides), and give the
scripts a `python3`. The scripts are installed with LF line endings whatever
the checkout used. This route is new: report what fails.

Not on Windows yet: KiloKlustaKwik, so `ndm_klustakwik` and pipelines that
cluster run under WSL2 (Option A); the SYCL backend of `process_extractemg` (a
SYCL build takes its CPU path). `process_aomconvert` needs HDF5 and
`process_extractleds` FFmpeg (`install-deps-windows.bat -WithHdf5 -WithFfmpeg`);
without them they are skipped at configure time.

---

## Option A — WSL2 (full pipeline, including KiloKlustaKwik)

Install WSL2 with Ubuntu 24.04, then follow the [Linux CPU guide](linux-cpu.md) or [Linux CUDA guide](linux-cuda.md) inside the WSL environment. The ndm_* scripts run without modification and can process files on the Windows filesystem via `/mnt/c/...` paths.

To install WSL2:

```powershell
wsl --install -d Ubuntu-24.04
```

Then restart and follow the [Linux CPU guide](linux-cpu.md) inside the WSL terminal.

---

## Option B — The plugins on their own

The normal route is the whole project ([Installing → Windows](../../INSTALL.md#windows)), whose install also copies the vcpkg DLLs (GSL, libxml2, yaml-cpp), and puts `bin` on PATH. The steps below build only the plugins; their install does none of the DLL or PATH steps.

### Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload. Download: https://visualstudio.microsoft.com/downloads/

2. **CMake** ≥ 3.22 and **Ninja**:
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

`-DUSE_CUDA=OFF` only affects `process_extractemg` here. `process_butterworth`,
`process_medianfilter`, `process_medianthreshold` and `process_spikegrouper`
build with CUDA whenever CMake finds `nvcc`, and `process_decomposecollisions`
has its own `-DPDC_USE_CUDA=OFF`; for a CPU-only build configure where `nvcc`
is not on `PATH` (see the [Linux CUDA notes](linux-cuda.md#cpu-only-build-with-cuda-installed)).

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

## Option C — MSYS2 (native Bash, no KiloKlustaKwik)

MSYS2 provides a minimal Linux-like environment on Windows with Bash, GNU coreutils, and a package manager (`pacman`). The ndm_* scripts run natively. This builds the plugins only, not KiloKlustaKwik, so `ndm_klustakwik` and pipelines that cluster still need WSL2 (Option A).

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
  mingw-w64-ucrt-x86_64-python-numpy \
  mingw-w64-ucrt-x86_64-python-scipy \
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
