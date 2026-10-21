# Installing neurosuite-3

neurosuite-3 is built and installed as **one project**: a single CMake build
produces Klusters, NeuroScope, NDManager, the ndmanager-plugins programs and
`ndm_*` pipeline steps, KiloKlustaKwik, and the libraries they share. There is
no separate build per program. This page is the installation guide for every
platform; [BUILD.md](BUILD.md) has the build options, GPU backends and
developer details.

| Platform | Status |
|---|---|
| **Linux** (Ubuntu 24.04 / Debian 12 and newer) | Reference platform. CI builds and tests Ubuntu 24.04, 25.10, 26.04 and 26.10. |
| **Windows 10 / 11** | Klusters, NeuroScope, NDManager and the processing programs build natively with Visual Studio 2022 (or Intel `icx` for the Intel GPU). The `ndm_*` steps run through Git Bash. KiloKlustaKwik is not yet part of the Windows build; use WSL2 for it (below). |
| **macOS** (Homebrew) | Built in CI on macOS 15 (Apple silicon); its tests are not yet required to pass, and the `ndm_*` steps need bash 4 (below). |

GPU acceleration (CUDA, ROCm, Intel SYCL) is optional; a CPU build always
works. See [gpu/README.md](gpu/README.md).

---

## Linux

```bash
git clone https://github.com/Gravios/neurosuite-3.git
cd neurosuite-3
cmake -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DNS_INSTALL_DEPS=ON
cmake --build build --parallel
cmake --install build
```

`-DNS_INSTALL_DEPS=ON` installs the required packages with `apt` (it asks for
your password through `sudo`); leave it out if you install them yourself. The
package list is in `CMakeLists.txt` and in [BUILD.md §1](BUILD.md#1-toolchain).

The programs land in `~/.local/bin`, which Ubuntu puts on `PATH` for new
logins. Check:

```bash
klusters &          # the GUIs: klusters, neuroscope, ndmanager
command -v ndm_hipass   # the pipeline steps are on PATH too
```

For a system-wide install use `-DCMAKE_INSTALL_PREFIX=/usr/local` and
`sudo cmake --install build`.

---

## Windows

Everything below runs in **cmd** (Command Prompt). PowerShell works too for
the commands that do not start with `cmake --preset`; see the notes.

### 1. Get the source

Install Git if you do not have it (`winget install --id Git.Git -e`), then:

```bat
cd /d C:\src
git clone https://github.com/Gravios/neurosuite-3.git
cd neurosuite-3
```

### 2. Install the dependencies

```bat
scripts\install-deps-windows.bat
```

The script installs whatever is missing and skips what is already there, so it
can be run again at any time:

- Git and the **Visual Studio 2022 Build Tools** with the C++ workload
  (compiler, Windows SDK, CMake, Ninja), through `winget`; installing Visual
  Studio shows a UAC prompt;
- **Python 3**, only if Qt has to be downloaded;
- **vcpkg** in `C:\vcpkg` with yaml-cpp, GSL, libxml2 and libsamplerate (the
  vcpkg bundled with Visual Studio cannot install these and is passed over);
- **Qt 6.8.3** for MSVC in `C:\Qt`, through aqtinstall (no Qt account);
- a `CMakeUserPresets.json` with the presets `windows` and `windows-sycl`.

Options: `-DryRun` (show the steps, change nothing), `-WithHdf5`
(`process_aomconvert`), `-WithFfmpeg` (`process_extractleds`; a long build),
`-WithPythonPackages` (PyYAML, NumPy, SciPy for the Python steps),
`-QtVersion`, `-QtRoot`, `-VcpkgRoot`, `-Force` (rewrite the presets).

### 3. Build and install

Open a **new** "x64 Native Tools Command Prompt for VS 2022" (Start menu), so a
freshly installed Visual Studio is found, and in the repository:

```bat
cmake --preset windows
cmake --build --preset windows
cmake --install build
```

The install goes to `C:\ns3`. It copies the Qt and vcpkg DLLs next to the
programs and adds `C:\ns3\bin` to **your user PATH**, so every prompt opened
afterwards finds them. The prompt the install ran in keeps its old PATH.

### 4. Run

In a new cmd or PowerShell window:

```bat
klusters
neuroscope
ndmanager
ndm_hipass yoursession
```

The `ndm_*` steps are bash scripts. Each has a `.bat` launcher beside it that
runs it with **Git Bash** (from Git for Windows) and gives it a `python3`, so
they start from cmd and PowerShell like any program. The processing programs
(`process_butterworth`, `process_pca`, ...) are native Windows programs.

### Intel GPU (SYCL)

With the Intel oneAPI Base Toolkit installed, use the **Intel oneAPI command
prompt for Intel 64 for Visual Studio 2022** instead, and the `windows-sycl`
preset:

```bat
cmake --preset windows-sycl
cmake --build --preset windows-sycl
cmake --install build-sycl
```

It installs to `C:\ns3-sycl`, with the oneAPI runtime and SYCL adapters, and
moves the PATH entry from `C:\ns3\bin` to `C:\ns3-sycl\bin`. In
Klusters the grouping assistant then runs on the GPU; realignment's
cross-correlation does with `set KLUSTERS_USE_SYCL=1`.

### KiloKlustaKwik and the full pipeline: WSL2

KiloKlustaKwik (`ndm_klustakwik`) is not built on Windows yet. To run the
whole pipeline, including clustering, install WSL2 and follow the
[Linux](#linux) steps inside it; your Windows files are under `/mnt/c/...`:

```bat
wsl --install -d Ubuntu-24.04
```

The Windows Klusters and NeuroScope open the sessions WSL2 produces.

### Updating, uninstalling

- **Update:** `git pull`, then `cmake --build --preset windows` and
  `cmake --install build`.
- **Uninstall:** run
  `powershell -ExecutionPolicy Bypass -File scripts\windows-path.ps1 -Remove`
  to take `C:\ns3\bin` off your PATH, then delete `C:\ns3`.

### When something goes wrong

| Symptom | Cause and fix |
|---|---|
| `'klusters' is not recognized` | The prompt is older than the install: open a new one. `scripts\windows-path.ps1 -Show` (run with `powershell -ExecutionPolicy Bypass -File`) says what was added. |
| Windows asks which app should open `ndm_hipass` | The `.bat` launchers are missing: rebuild and install with a current checkout. |
| `ndm: Git Bash not found` | Install Git for Windows, or `setx NS_BASH "C:\path\to\Git\bin\bash.exe"` and open a new prompt. |
| `Access is denied.` right after the command | A stray `>` in front of the command (cmd redirects into the `cmake` folder): remove it. |
| A program starts from the build folder but reports missing Qt DLLs | Run it from `C:\ns3\bin` (the install), not from `build`. |
| `dumpbin not on PATH` warning during the install | Run `cmake --install` from the Visual Studio (or oneAPI) prompt. |
| `Could not locate a manifest (vcpkg.json)` | An old install script met Visual Studio's own vcpkg: update the checkout, or pass `-VcpkgRoot C:\vcpkg`. |
| `icx` / `sycl-ls` not recognized | Use the oneAPI command prompt. |

Further detail: [BUILD.md](BUILD.md) (options, presets, `-DNS_INSTALL_DEPS=ON`
on Windows) and the [ndmanager-plugins Windows notes](ndmanager-plugins/install/windows.md).

---

## macOS

With [Homebrew](https://brew.sh) installed:

```bash
git clone https://github.com/Gravios/neurosuite-3.git
cd neurosuite-3
cmake -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DNS_INSTALL_DEPS=ON
cmake --build build --parallel
cmake --install build
```

`-DNS_INSTALL_DEPS=ON` runs `brew install` for CMake, Ninja, Qt 6, yaml-cpp,
GSL, libomp, FFmpeg and libsamplerate, and points CMake at Homebrew's Qt.
Apple's compiler has no OpenMP of its own; if the configure output says
OpenMP was not found, add
`-DCMAKE_PREFIX_PATH="$(brew --prefix libomp)"` for the multithreaded build.

There is no CUDA, HIP or SYCL on a Mac. A Metal (Apple GPU) backend
(`doc/design/metal-backend.md`, Apple silicon, macOS 14 or newer) covers the
Butterworth and median high-pass filters, the noise thresholds and the
channel grouping (`process_butterworth`, `process_medianfilter`,
`process_medianthreshold`, `process_spikegrouper`), Klusters' realignment
and Grouping Assistant (FP32 option), and KiloKlustaKwik's CEM steps (the
`KiloKlustaKwik` binary; `KiloKlustaKwik_cpu` beside it stays on the CPU).
The kernels are compiled when a program first uses the GPU, so no Xcode is
needed; without a Metal device, or with `NS_DISABLE_METAL=1` in the
environment, every program takes its CPU path. `ctest -R metal` runs the
Metal tests, including `metal_kernels_compile`, which compiles every kernel
on the device. To compare the GPU with the CPU on your own recording, run
`scripts/metal_validate.sh SESSION NCHANNELS SAMPLERATE [--kk GROUP]` (see
its header); please report what it prints. The configure step downloads
Apple's metal-cpp headers from GitHub; for an offline build, point
`-DNS_METAL_CPP_DIR=` at a copy, or turn it off with `-DUSE_METAL=OFF`. Apple's Accelerate
framework, part of macOS, is used instead where it applies: NeuroScope's
spectrogram FFT runs on vDSP (unless FFTW is installed, `brew install fftw`;
then `NS_SPECTRAL_FFT=vdsp` selects vDSP), and `process_extractemg` has a
`--backend accelerate` kernel. `-DNS_USE_ACCELERATE=OFF` leaves both out.

The `ndm_*` steps need bash 4 or newer and macOS ships bash 3.2 as
`/bin/bash`: `brew install bash`. The scripts start with `#!/usr/bin/env bash`,
so they use the first bash on `PATH`; Homebrew's shell setup
(`eval "$(brew shellenv)"`) puts its `bin` before `/bin`. Started with bash
3.2, a step stops at once and says so. CI builds this configuration on macOS 15 (Apple silicon) (the
macOS badge in the README); its tests are not yet required to pass, so report
what fails.

---

## Installing one program only

Turn the others off at configure time, for example Klusters alone:

```bash
cmake -B build -DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_NDMANAGER=ON \
      -DNS_SKIP_NDMANAGER_PLUGINS=ON -DNS_SKIP_KILOKLUSTAKWIK=ON
```

The shared libraries a program needs are always built with it. The plugins
also configure on their own (`cmake -S src/ndmanager-plugins`); see
[BUILD.md §5](BUILD.md#5-the-build-graph-and-skip-flags).
