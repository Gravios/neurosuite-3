# KlustaKwik

Automatic spike sorting via Classification EM (CEM). Reads a `.fet` feature file
produced by ndm_pca and writes a `.clu` cluster assignment file.

This is a modernised fork of [KlustaKwik v1.7](http://sourceforge.net/projects/klustakwik)
(Hazan, Zugaro & Buzsáki 2006). The core algorithm is unchanged; the additions are
described in [CHANGES.md](CHANGES.md).

---

## Building

Requires a C++17 compiler and CMake ≥ 3.21. OpenMP is detected automatically
and enables parallel chunk processing when available.

```bash
# CPU-only
cmake -B build -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF
cmake --build build -j$(nproc)

# With CUDA — NVIDIA GPUs (requires CUDA Toolkit >= 11)
cmake -B build -DUSE_HIP=OFF -DUSE_SYCL=OFF
cmake --build build -j$(nproc)

# With HIP — AMD GPUs (requires ROCm >= 5.0 on Linux, HIP SDK >= 5.5 on Windows)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF
cmake --build build -j$(nproc)

# With SYCL — Intel Arc/Xe GPUs (requires oneAPI Base Toolkit >= 2023.1)
cmake -B build -DUSE_CUDA=OFF -DUSE_HIP=OFF
cmake --build build -j$(nproc)
```

When none of the three backend flags are explicitly set, CMake auto-detects in
priority order CUDA > HIP > SYCL and selects the first toolkit it finds.
Every GPU build also produces `KlustaKwik_cpu` as a CPU-only fallback binary.

On Windows, use `-G "Ninja"` and run inside a Visual Studio Developer Command
Prompt (or any shell where MSVC is on PATH). The SYCL backend additionally
requires the oneAPI command prompt — see the platform-specific sections below.

---

### CUDA installation (NVIDIA)

Install the [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads)
(>= 11, recommended >= 12). `check_language(CUDA)` locates `nvcc`
automatically — no extra CMake flags required on any platform.

- [Ubuntu / Debian (bare metal or WSL2)](#ubuntu--debian-1)
- [Windows (native)](#windows-native-1)

---

#### Ubuntu / Debian (bare metal or WSL2)

```bash
# Add the CUDA keyring and repository
# Choose your exact distro at https://developer.nvidia.com/cuda-downloads
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update
sudo apt install cuda-toolkit-12-x
```

If `nvcc` is not on `PATH` after install, add to `~/.bashrc`:

```bash
export PATH=/usr/local/cuda/bin:$PATH
```

Verify:

```bash
nvcc --version
nvidia-smi
```

Build KlustaKwik:

```bash
rm -rf build && mkdir build && cd build
cmake .. -DUSE_HIP=OFF -DUSE_SYCL=OFF
make -j$(nproc)
```

To target a specific GPU generation:

```bash
cmake .. -DUSE_HIP=OFF -DUSE_SYCL=OFF -DCMAKE_CUDA_ARCHITECTURES="86"  # Ampere (RTX 30xx)
cmake .. -DUSE_HIP=OFF -DUSE_SYCL=OFF -DCMAKE_CUDA_ARCHITECTURES="89"  # Ada Lovelace (RTX 40xx)
cmake .. -DUSE_HIP=OFF -DUSE_SYCL=OFF -DCMAKE_CUDA_ARCHITECTURES="90"  # Hopper (H100)
```

---

#### Windows (native) {#windows-native-1}

**Step 1 — Install prerequisites**

1. **Visual Studio 2022** with the **Desktop development with C++** workload.
   `nvcc` uses MSVC as its host compiler and requires it to be present.
   Download: https://visualstudio.microsoft.com/downloads/

2. **CUDA Toolkit** (>= 12 recommended).
   Download the Windows installer from https://developer.nvidia.com/cuda-downloads.
   Select: Windows → x86_64 → Windows 11 → exe (local or network).
   The CUDA installer includes a bundled GPU driver; accept it unless you have
   a newer driver already installed.

**Step 2 — Verify**

Open any `cmd.exe` or PowerShell (the CUDA installer adds `nvcc` to PATH):

```bat
nvcc --version
nvidia-smi
```

If `nvcc` is not found, add to your system PATH:
`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.x\bin`

**Step 3 — Build**

```bat
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" -DUSE_HIP=OFF -DUSE_SYCL=OFF
ninja
```

CMake finds `nvcc` automatically. For the Visual Studio generator:

```bat
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_HIP=OFF -DUSE_SYCL=OFF
cmake --build . --config Release
```

To target a specific GPU architecture:

```bat
cmake .. -G "Ninja" -DUSE_HIP=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_CUDA_ARCHITECTURES="89"
```

---

### HIP installation (AMD ROCm)

Install the [AMD HIP SDK](https://rocm.docs.amd.com/) (>= 5.0). On Linux this
is the full ROCm stack; on Windows it is the lighter HIP SDK package. CMake
locates `hipcc` automatically via `ROCM_PATH` or the default install path.

- [Ubuntu / Debian (bare metal)](#ubuntu--debian-2)
- [Windows (native)](#windows-native-2)

---

#### Ubuntu / Debian (bare metal)

```bash
# Add the ROCm package repository (Ubuntu 22.04 / 24.04)
sudo apt install wget gnupg
wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo apt-key add -
echo "deb [arch=amd64] https://repo.radeon.com/rocm/apt/debian/ jammy main" \
    | sudo tee /etc/apt/sources.list.d/rocm.list
sudo apt update
sudo apt install rocm-dev hipcc
```

Add to `~/.bashrc` to make it permanent:

```bash
export PATH=/opt/rocm/bin:$PATH
export ROCM_PATH=/opt/rocm
```

Verify:

```bash
hipcc --version
rocminfo | grep "Marketing Name"
```

Build KlustaKwik:

```bash
rm -rf build && mkdir build && cd build
cmake .. -DUSE_CUDA=OFF -DUSE_SYCL=OFF
make -j$(nproc)
```

The default GPU architecture list covers RDNA2 (RX 6000), RDNA3 (RX 7000),
and CDNA2 (Instinct MI210/MI250). To target specific architectures:

```bash
# RDNA3 only (RX 7900 XTX etc.)
cmake .. -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
         -DKK_HIP_ARCHS="gfx1100;gfx1101;gfx1102"

# RDNA2 only (RX 6800 XT etc.)
cmake .. -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
         -DKK_HIP_ARCHS="gfx1030;gfx1031;gfx1032"
```

Run `rocminfo` and look for `Name: gfxNNNN` under each agent for the full list
of architecture identifiers on your system.

**Note on wavefront size:** RDNA wavefronts are 64 threads wide (vs CUDA
warps at 32). The default block size of 256 threads (4 wavefronts) is safe
on all supported architectures. For CDNA2 datacenter cards:

```bash
cmake .. -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
         -DKK_HIP_ARCHS="gfx90a" -DKK_HIP_BLOCK=512
```

---

#### Windows (native) {#windows-native-2}

AMD ships a standalone **HIP SDK** for Windows that is separate from the full
Linux ROCm stack. It provides `hipcc`, the HIP headers, and the runtime DLLs
without requiring any Linux tooling.

> **GPU support on Windows:** The HIP SDK supports Radeon RX 6000 (RDNA2),
> RX 7000 (RDNA3), and RX 9000 (RDNA4) series. Older GCN cards (RX 500/Vega)
> are not supported on Windows. Check the AMD HIP SDK system requirements
> before installing: https://rocm.docs.amd.com/projects/install-on-windows/en/latest/reference/system-requirements.html

**Step 1 — Install prerequisites**

1. **Visual Studio 2022** with the **Desktop development with C++** workload.
   `hipcc` on Windows uses MSVC as its host compiler.
   Download: https://visualstudio.microsoft.com/downloads/

2. **AMD HIP SDK** (>= 5.5 recommended, >= 6.0 for best CMake support).
   Download from: https://www.amd.com/en/developer/resources/rocm-hub/hip-sdk.html
   The installer places the SDK at `C:\Program Files\AMD\ROCm\<version>\`
   by default and adds it to PATH.

3. **CMake** >= 3.21 and **Ninja** — install via winget or the Visual Studio
   CMake component:
   ```powershell
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   ```

**Step 2 — Verify**

Open a new `cmd.exe` or PowerShell (so the updated PATH takes effect):

```bat
hipcc --version
hipconfig --full
```

`hipconfig --full` should show `HIP_PLATFORM=amd` and the path to the Clang
compiler bundled with the SDK.

**Step 3 — Build**

CMake needs to find the HIP SDK's `hip-lang-config.cmake`. The HIP SDK
installer sets `%HIP_PATH%` in the system environment, but CMake also needs
it on `CMAKE_PREFIX_PATH`:

```bat
rmdir /s /q build
mkdir build && cd build
cmake .. -G "Ninja" ^
         -DUSE_CUDA=OFF -DUSE_SYCL=OFF ^
         -DCMAKE_PREFIX_PATH="C:\Program Files\AMD\ROCm\6.3"
ninja
```

Replace `6.3` with the version you installed — the exact path is shown in
`hipconfig --full` output. If CMake still cannot find HIP, set the root
explicitly:

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

> **Visual Studio generator note:** The Visual Studio generator does not
> support the CMake HIP language on Windows. Always use `-G "Ninja"` (or
> `-G "Unix Makefiles"` via MSYS2/Git Bash) when building HIP code on Windows.

---

### SYCL installation (Intel Arc / Xe)

SYCL support requires the [Intel oneAPI Base Toolkit](https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit.html)
(>= 2023.1), which provides the `icpx` DPC++ compiler and SYCL runtime.
CMake will auto-detect `icpx` and select the SYCL backend automatically when
the toolkit is installed — no manual `-DCMAKE_CXX_COMPILER=icpx` flag needed.

Choose the section for your platform:

- [Ubuntu / Debian (bare metal)](#ubuntu--debian-bare-metal)
- [WSL2 (Windows Subsystem for Linux)](#wsl2-windows-subsystem-for-linux)
- [Windows (native)](#windows-native)

---

#### Ubuntu / Debian (bare metal)

**Step 1 — Install the oneAPI compiler**

```bash
# Add the Intel oneAPI APT repository
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
    | gpg --dearmor \
    | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null

echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] \
    https://apt.repos.intel.com/oneapi all main" \
    | sudo tee /etc/apt/sources.list.d/oneAPI.list

sudo apt update
sudo apt install -y intel-oneapi-compiler-dpcpp-cpp
```

`intel-oneapi-compiler-dpcpp-cpp` installs only `icpx`/`icx` and the SYCL
runtime — a ~2 GB download rather than the full ~10 GB Base Toolkit. Install
`intel-basekit` instead if you also want VTune, MKL, and the other tools.

**Step 2 — Install the Intel GPU runtime packages**

These provide the Level-Zero and OpenCL GPU runtimes that SYCL dispatches
through. Skip this step only if you intend to run on CPU only.

```bash
# Add the Intel GPU package repository (Ubuntu 22.04 jammy / 24.04 noble)
. /etc/os-release   # sets $VERSION_CODENAME
wget -qO - https://repositories.intel.com/gpu/intel-graphics.key \
    | sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg

echo "deb [arch=amd64 signed-by=/usr/share/keyrings/intel-graphics.gpg] \
    https://repositories.intel.com/gpu/ubuntu ${VERSION_CODENAME} client" \
    | sudo tee /etc/apt/sources.list.d/intel-gpu.list

sudo apt update
sudo apt install -y \
    libze1 \
    intel-level-zero-gpu \
    intel-opencl-icd \
    intel-ocloc \
    clinfo \
    libze-dev \
    level-zero-dev
```

> **Ubuntu 24.04 (noble) dependency note:** If `intel-level-zero-gpu` fails
> with unmet dependencies on `libigc1` or `libigdfcl1`, install them first:
> ```bash
> sudo apt-get install -y libigc1 libigdfcl1
> sudo apt-get install -y intel-level-zero-gpu intel-opencl-icd intel-ocloc
> ```
> This happens because the `unified` channel omits these packages on noble.
> The `client` channel used above includes them, but in case of repo sync
> lag the explicit install is a reliable fallback.

**Step 3 — Add your user to the render group**

```bash
sudo usermod -aG render $USER
sudo usermod -aG video $USER
# Log out and back in, or: newgrp render
```

**Step 4 — Activate the oneAPI environment**

`icpx` is not on `PATH` until the environment is sourced. Add to `~/.bashrc`
to make it permanent:

```bash
source /opt/intel/oneapi/setvars.sh
```

**Step 5 — Verify**

```bash
clinfo -l      # should show an Intel(R) OpenCL Graphics GPU entry
sycl-ls        # should show level_zero:gpu and opencl:gpu entries
```

**Step 6 — Build**

```bash
rm -rf build && mkdir build && cd build
cmake ..
make -j$(nproc)
```

CMake auto-detects `icpx` and selects the SYCL backend. No extra flags needed.

---

#### WSL2 (Windows Subsystem for Linux)

Under WSL2 the GPU driver lives on the Windows host — the Linux side only needs
the Level-Zero and OpenCL *runtime* packages (not the Windows GPU driver).
The oneAPI compiler (`icpx`) is installed inside WSL exactly as on bare-metal
Ubuntu.

**Step 1 — Install the oneAPI compiler (inside WSL)**

Same as the Ubuntu bare-metal Step 1 above — add the oneAPI APT repository and
install `intel-oneapi-compiler-dpcpp-cpp`.

**Step 2 — Add the Intel GPU package repository for Ubuntu 24.04 (noble)**

Use the `client` channel. The `unified` channel is missing the IGC dependency
packages (`libigc1`, `libigdfcl1`) on noble and will produce unmet-dependency
errors.

```bash
wget -qO - https://repositories.intel.com/gpu/intel-graphics.key \
    | sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg

echo 'deb [arch=amd64 signed-by=/usr/share/keyrings/intel-graphics.gpg] \
    https://repositories.intel.com/gpu/ubuntu noble client' \
    | sudo tee /etc/apt/sources.list.d/intel-gpu.list

sudo apt update
```

**Step 3 — Install the GPU runtime packages**

```bash
sudo apt-get install -y \
    libze1 \
    intel-level-zero-gpu \
    intel-opencl-icd \
    intel-ocloc \
    clinfo \
    libze-dev \
    level-zero-dev
```

If `intel-level-zero-gpu` fails with unmet dependencies on `libigc1` or
`libigdfcl1`, install those explicitly first:

```bash
sudo apt-get install -y libigc1 libigdfcl1
sudo apt-get install -y intel-level-zero-gpu intel-opencl-icd intel-ocloc
```

**Step 4 — Add your user to the render and video groups**

```bash
sudo usermod -aG render $USER
sudo usermod -aG video $USER
```

Then restart WSL from PowerShell:

```powershell
wsl.exe --shutdown
```

**Step 5 — Activate the oneAPI environment**

Add to `~/.bashrc` to make it permanent:

```bash
source /opt/intel/oneapi/setvars.sh
```

**Step 6 — Verify**

```bash
clinfo -l      # should list an Intel(R) OpenCL Graphics GPU entry
sycl-ls        # should list level_zero:gpu and opencl:gpu entries
```

A working setup on a Meteor Lake Core Ultra system looks like:

```
$ clinfo -l
Platform #0: Intel(R) OpenCL
 `-- Device #0: Intel(R) Core(TM) Ultra 7 155H
Platform #1: Intel(R) OpenCL Graphics
 `-- Device #0: Intel(R) Graphics [0x7d55]

$ sycl-ls
[level_zero:gpu][level_zero:0] Intel(R) oneAPI Unified Runtime over Level-Zero, Intel(R) Graphics [0x7d55] 12.71.4 [1.3.29735+27]
[opencl:cpu][opencl:0] Intel(R) OpenCL, Intel(R) Core(TM) Ultra 7 155H OpenCL 3.0 (Build 0) [2026.20.1.0.12_160000]
[opencl:gpu][opencl:1] Intel(R) OpenCL Graphics, Intel(R) Graphics [0x7d55] OpenCL 3.0 NEO [24.39.31294]
```

The `level_zero:gpu` entry is what SYCL uses by default — Level Zero has lower
dispatch overhead than OpenCL and is the preferred backend for compute
workloads. If only `opencl:gpu` appears (no `level_zero:gpu`), install
`intel-level-zero-gpu` and restart WSL.

If only `opencl:cpu` and FPGA emulation appear (no GPU at all), the Windows
Intel GPU driver needs updating. WSL2 GPU passthrough requires driver version
**31.0.101.4887 or newer** — check in Windows Device Manager → Display
Adapters → right-click the Intel GPU → Properties → Driver tab.

**Step 7 — Build**

```bash
rm -rf build && mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Step 8 — Verify the binary sees your GPU**

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu ./KlustaKwik_sycl
```

The binary should print its usage/help output rather than failing or silently
falling back to CPU.

**Note on Meteor Lake iGPUs (Core Ultra series)**

The integrated Arc GPU in Meteor Lake (e.g. `[0x7d55]` on Core Ultra 155H)
shares memory with the CPU — there is no discrete VRAM. SYCL USM allocations
are zero-copy on this device: data does not physically move and the GPU
accesses system RAM directly. This is ideal for KlustaKwik's feature matrices,
which are typically small enough that transfer overhead would dominate on a
discrete GPU. The bottleneck is EU throughput on the E-step distance
computations, which is exactly what the SYCL kernel parallelises.

---

#### Windows (native)

Building natively on Windows requires the oneAPI toolkit, a C++ build
environment (Visual Studio or Build Tools), CMake, and Ninja or NMake.
The Intel GPU driver is provided by Windows Update or the Intel DSA tool —
no separate Linux-side runtime packages are needed.

**Step 1 — Install prerequisites**

Install in this order:

1. **Visual Studio 2022** (Community edition is free) with the
   **Desktop development with C++** workload, or at minimum the
   **MSVC v143 build tools** and **Windows SDK**. CMake and Ninja are
   included in this workload — tick them in the installer if not already
   present, or install them separately.
   Download: https://visualstudio.microsoft.com/downloads/

2. **Intel oneAPI Base Toolkit** (>= 2023.1).
   The quickest install via winget:
   ```powershell
   winget install Intel.OneAPI.BaseToolkit
   ```
   Or download the GUI/offline installer from:
   https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html

   If you only need `icpx` and the SYCL runtime without the full toolkit,
   install the C++ Essentials bundle instead — it is a smaller download and
   sufficient for building KlustaKwik.

3. **Intel GPU driver** — install via the Intel Driver & Support Assistant
   (DSA) or manually from:
   https://www.intel.com/content/www/us/en/download/785597/intel-arc-iris-xe-graphics-windows.html
   Minimum required version for SYCL: **31.0.101.4887**

**Step 2 — Open the oneAPI command prompt**

The oneAPI installer adds a Start Menu shortcut:
**Intel oneAPI command prompt for Intel oneAPI Toolkit 2025**.

Opening it runs `setvars.bat` automatically, placing `icpx`, `icx`, and all
related tools on `PATH`. All subsequent steps must be run inside this prompt
(or any `cmd.exe`/PowerShell session where you have run `setvars.bat` manually).

To activate manually in an existing `cmd.exe`:
```bat
"C:\Program Files (x86)\Intel\oneAPI\setvars.bat"
```

Or in PowerShell:
```powershell
cmd.exe /K '"C:\Program Files (x86)\Intel\oneAPI\setvars.bat" && powershell'
```

Verify the compiler is on PATH:
```bat
icpx --version
sycl-ls
```

`sycl-ls` should list your Intel GPU under `[level_zero:gpu]` or
`[opencl:gpu]`. If only CPU appears, check that the Intel GPU driver is
up to date.

**Step 3 — Configure and build with CMake**

Inside the oneAPI command prompt, from the KlustaKwik source directory:

```bat
rmdir /s /q build
mkdir build
cd build
cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_HIP=OFF
ninja
```

CMake auto-detects `icpx` from `PATH` and selects the SYCL backend. No
`-DCMAKE_CXX_COMPILER=icpx` flag is needed when running inside the oneAPI
command prompt.

If you prefer the Visual Studio generator over Ninja, use CMake >= 3.29 and
ensure the Intel oneAPI Visual Studio extensions are installed:

```bat
cmake .. -G "Visual Studio 17 2022" -T "Intel C++ Compiler 2025" ^
         -DUSE_CUDA=OFF -DUSE_HIP=OFF
cmake --build . --config Release
```

**Step 4 — Verify**

```bat
set ONEAPI_DEVICE_SELECTOR=level_zero:gpu
KlustaKwik_sycl.exe
```

The binary should print its usage/help output.

> **AOT compilation note:** The Windows build compiles ahead-of-time for
> `mtl-m` (Meteor Lake), `acm-g10/g11` (Arc Alchemist), `dg2`, and `pvc`
> (Ponte Vecchio). Devices not in this list fall back to JIT compilation
> by the GPU driver at first run — this adds a ~1–2 s startup delay on the
> first invocation but works correctly on any SYCL-capable Intel GPU.
> To add or change AOT targets at configure time:
> ```bat
> cmake .. -G "Ninja" -DUSE_CUDA=OFF -DUSE_HIP=OFF ^
>          -DKK_SYCL_DEVICES="mtl-m,acm-g10,bmg-g21"
> ```

---

To compile manually without CMake:

```bash
g++ -std=c++17 -O2 -fopenmp -DNDEBUG -o KlustaKwik KlustaKwik.cpp KK.cpp param.c
```

---

## Usage

```
KlustaKwik FileBase ElecNo [options]
```

`FileBase` and `ElecNo` together locate the input file `FileBase.fet.ElecNo` and
determine the names of all output files.

### Minimal examples

```bash
# Two-phase farthest-point mode (default)
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12

# Three-phase temporal chunking for long recordings with electrode drift
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 \
    -ChunkMinutes 5 -SamplingRate 32572

# With screen output
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 -Screen 1
```

### Output files

| File | Contents |
|---|---|
| `FileBase.clu.ElecNo` | Cluster assignment per spike (1-based; label 1 = noise) |
| `FileBase.klg.ElecNo` | Run log (created only when `-Log 1`) |
| `FileBase.model.ElecNo` | Gaussian model parameters (omitted with `-fSaveModel 0`) |

### Startup banner

Regardless of `-Screen` and `-Log`, the binary always prints a brief summary to
**stderr** before the first CEM call. This confirms that data loaded successfully
and shows whether parallelism is active:

```
KlustaKwik  jg05-20120316.fet.7
  344094 spikes, 25 dims, clusters 2-50
  mode: chunked  chunk=5.0 min  SR=32572  ~16 chunks
  parallel: 16 OpenMP threads
  K=2/50 start=1/1
```

The `K=N/M` line updates in-place as the outer loop advances. If parallelism is
not working correctly, the banner will say so — see
[Troubleshooting](#troubleshooting) below.

---

## Parameters

All parameters are optional. Defaults reflect typical tetrode recordings at 20 kHz.
Parameter names are case-sensitive. Unrecognised names are **silently ignored** by
the parser — double-check spelling if a setting appears to have no effect.

### Cluster range

| Parameter | Default | Description |
|---|---|---|
| `MinClusters` | `2` | Minimum number of starting clusters |
| `MaxClusters` | `10` | Maximum number of starting clusters |
| `MaxPossibleClusters` | `100` | Hard ceiling on cluster count; also sizes internal arrays |
| `nStarts` | `1` | Random restarts per starting cluster count |

### Algorithm selection

| Parameter | Default | Description |
|---|---|---|
| `InitMethod` | `farthest` | Seed strategy: `farthest` (periphery-inward) or `random` |
| `TimeMergeIter` | `30` | Iterations for the Phase 2 time-merge pass. Set `0` to skip |
| `ChunkMinutes` | `0` | Enables three-phase temporal chunking when > 0. Set to the expected stationarity window, typically 3-10 min for silicon probes |
| `SamplingRate` | `20000` | Samples per second. **Must match the actual recording rate** when `ChunkMinutes > 0`; controls conversion of raw sample timestamps to chunk boundaries |

### Temporal chunking (three-phase mode)

Only relevant when `ChunkMinutes > 0`.

| Parameter | Default | Description |
|---|---|---|
| `MergeThresh` | `30` | Symmetric Mahalanobis^2 threshold for matching clusters across adjacent chunk boundaries. Approximates chi^2(nSpatialDims, p=0.99) by default. Increase if units fail to link across chunks; decrease if unrelated units are being merged |
| `GlobalMergeIter` | `20` | Maximum EM iterations for the Phase 3 global warm-start pass |

### EM convergence

| Parameter | Default | Description |
|---|---|---|
| `MaxIter` | `500` | Maximum EM iterations per CEM run |
| `FullStepEvery` | `10` | Full E-step every N iterations; intermediate steps use a faster approximate update |
| `ChangedThresh` | `0.05` | Convergence criterion: fraction of points that must change assignment to continue |
| `SplitEvery` | `50` | Attempt cluster splits every N iterations |
| `PenaltyMix` | `0` | Blends BIC (`0`) and AIC (`1`) as the model-complexity penalty |

### Input / output

| Parameter | Default | Description |
|---|---|---|
| `UseFeatures` | `11111111111100001` | Binary mask selecting which feature columns to load. Length must match the feature count in the `.fet` header; the trailing `1` selects the timestamp column. Use `all` to select every column |
| `StartCluFile` | _(none)_ | Path to an existing `.clu` file to use as the initial assignment |
| `RandomSeed` | `1` | RNG seed (applies to `InitMethod random`) |
| `fSaveModel` | `1` | Write Gaussian model file. Set `0` to skip |
| `SaveIntermediates` | `1` | Write `.clu` on every new best score during the cluster-count sweep. Set `0` to suppress mid-run writes and perform only the single final write — recommended on network storage |

### Logging

All output is silent by default. The stderr startup banner is always printed
and is not controlled by these flags.

| Parameter | Default | Description |
|---|---|---|
| `Screen` | `0` | Print per-iteration progress to stdout |
| `Log` | `0` | Write per-iteration progress to `FileBase.klg.ElecNo` |
| `Verbose` | `0` | Also print the full parameter table at startup. Requires `Screen 1` or `Log 1` |

### Diagnostic / rarely changed

| Parameter | Default | Description |
|---|---|---|
| `DistDump` | `0` | Dump the log-probability matrix to `DISTDUMP` after each E-step. Large; for debugging only |
| `DistThresh` | `6.908` | Log-odds threshold for noise assignment (ln 1000) |
| `Debug` | `0` | Print per-cluster mean and covariance at each M-step |

---

## Algorithm overview

### Default: two-phase farthest-point CEM

**Phase 1 — spatial EM.** Features are clustered using only the PCA dimensions,
with the timestamp excluded. Initial centres are chosen by farthest-point seeding:
the first centre is the global centroid; each subsequent centre is the data point
with the largest minimum Mahalanobis distance from the current set. This
periphery-inward strategy places seeds at the natural boundaries of the data
rather than having multiple seeds compete for the same dense core, reducing EM
iterations by 60-75% compared to random seeding.

**Phase 2 — time-merge pass.** The timestamp is reintroduced and `TimeMergeIter`
additional EM iterations run at full dimensionality. Units active in different
time epochs can be distinguished; units that drift slightly over the session will
now fit a tilted ellipse in PCA+time space rather than an inflated sphere.

### Optional: three-phase temporal chunking

Enabled with `ChunkMinutes > 0`. Designed for multi-hour recordings where
electrode drift causes a unit's PCA trajectory to shift substantially over the
session, making a single global Gaussian a poor fit.

**Phase 0 — partition.** The session is divided into windows of `ChunkMinutes`
minutes using the raw sample timestamps captured at load time. Any window below
the minimum spike count (`nStartingClusters x nSpatialDims x 3`) is merged into
its predecessor.

**Phase 1 — per-chunk CEM (parallel).** Each chunk runs an independent two-phase
CEM. Chunks are processed in parallel across CPU cores using OpenMP
(`schedule(dynamic)` accounts for variable chunk sizes). Each chunk sub-object
owns its own Cholesky storage and does not touch any shared state, making the
parallel execution data-race-free.

**Phase 2 — cross-chunk model matching.** For each pair of immediately adjacent
chunks `(k, k+1)`, every cluster from chunk `k` is compared against every cluster
from chunk `k+1` using the symmetric Mahalanobis distance in the spatial
dimensions. Pairs within `MergeThresh` are linked in a Union-Find structure.
Transitivity handles continuously drifting units: A-B and B-C edges imply A and C
belong to the same global unit without requiring a direct A-C comparison.
Non-adjacent chunks are never compared. Noise clusters from all chunks are
unconditionally merged to a single global noise class before any distance
comparisons run.

**Phase 3 — global warm-start EM.** The full dataset is reassembled with globally
matched labels as the starting assignment. A short EM pass at full dimensionality
(at most `GlobalMergeIter` iterations) refines the models. This typically
converges in 1-5 iterations on real recordings.

If the merge produces more global clusters than `MaxPossibleClusters` (indicating
`MergeThresh` is too small), the run falls back to two-phase CEM on the full
session with a diagnostic message to stderr.

---

## Typical invocations

```bash
# Standard tetrode recording, two-phase mode
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12

# Suppress intermediate .clu writes (recommended on NFS / network storage)
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 -SaveIntermediates 0

# 80-minute silicon probe recording at 32572 Hz, chunked mode
KlustaKwik jg05-20120316 7 \
    -MinClusters 2 -MaxClusters 20 \
    -UseFeatures 1111111111111111111111111 \
    -ChunkMinutes 5 -SamplingRate 32572 \
    -MergeThresh 30 -GlobalMergeIter 20 -TimeMergeIter 30 \
    -SaveIntermediates 0

# Re-run from an existing sort
KlustaKwik session 1 -StartCluFile session.clu.1

# Original v1.7 behaviour (random init, no time-merge, full logging)
KlustaKwik session 1 -InitMethod random -TimeMergeIter 0 \
    -Screen 1 -Log 1 -Verbose 1
```

---

## Integration with ndm_pca / NeuroSuite

Pass `-SamplingRate` from the XML session descriptor so chunk boundaries are
calculated correctly:

```xml
<process cmd="KlustaKwik %filebase% %electrodeNo%
    -MinClusters 2 -MaxClusters 12
    -ChunkMinutes 5 -SamplingRate %samplingRate%
    -SaveIntermediates 0"/>
```

---

## Troubleshooting

### The program runs on a single core despite -ChunkMinutes

The startup banner identifies the exact cause before the first CEM call, so you
do not have to wait for the run to finish to diagnose the problem.

**Case 1 — `OMP_NUM_THREADS` is set to 1:**

```
parallel: 1 of 16 cores  (OMP_NUM_THREADS=1 limits parallelism —
                           unset it or set to 16 to use all cores)
```

This is the most common cause. Fix it in the current shell:

```bash
unset OMP_NUM_THREADS
```

To make it permanent, add this to `~/.bashrc`:

```bash
export OMP_NUM_THREADS=$(nproc)
```

You can also override it inline for a single run without touching the environment:

```bash
OMP_NUM_THREADS=16 KlustaKwik session 1 -ChunkMinutes 5 ...
```

**Case 2 — running under WSL2 with CPU allocation restricted:**

```
parallel: 1 of 1 cores
```

If `nproc` returns 1 (or far fewer cores than the Windows host has), WSL2 is only
being given 1 CPU by its configuration. On the Windows host, create or edit
`C:\Users\<YourUsername>\.wslconfig`:

```ini
[wsl2]
processors=16    ; set to the number of physical cores on the host
```

Then restart WSL from an elevated PowerShell or Command Prompt:

```powershell
wsl --shutdown
```

Reopen your WSL terminal. `nproc` should now return the configured value and
KlustaKwik will use all of them without any environment variable needed. You can
confirm the host core count in Task Manager > Performance > CPU.

**Case 3 — binary not linked with OpenMP:**

```
WARNING: built without OpenMP — chunks run serially.
         Recompile with -fopenmp to enable parallelism.
```

Rebuild with the flag explicitly:

```bash
g++ -std=c++17 -O2 -fopenmp -DNDEBUG -o KlustaKwik KlustaKwik.cpp KK.cpp param.c
```

When using CMake, check that `find_package(OpenMP)` succeeded during
configuration — the output should include a line like
`-- Found OpenMP_CXX: -fopenmp (found version "4.5")`.
If it is missing, install the OpenMP runtime (`sudo apt install libomp-dev` on
Debian/Ubuntu) and re-run CMake.

You can verify a built binary at any time:

```bash
ldd KlustaKwik | grep -i omp    # should print something like libgomp.so.1
```

### A parameter has no effect

Parameter names are case-sensitive and unrecognised names are silently ignored.
Run with `-Screen 1 -Verbose 1` to print the full parameter table at startup and
confirm the value was accepted.

Common mistakes:

- `-SaveIntermediates` is frequently misspelled (e.g. `-SaveIntermeiates`)
- `-SamplingRate` is often omitted in chunked mode; the default of 20000 will
  produce wrong chunk boundaries if your recording rate differs

### Chunk merge produces too many global clusters

```
WARNING: MergeChunkModels produced N global clusters >= MaxPossibleClusters (100).
  This means MergeThresh=30.0 is too small ...
  Falling back to CEMTwoPhase on the full session.
```

Either increase `MergeThresh` (try 50-100) or raise `MaxPossibleClusters` to
accommodate the actual unit count. If the fallback produces a good result, the
recording may not have enough drift to need chunking.

### Runtime is much longer than expected

With `MaxClusters=50` and 25 features, each per-chunk CEM at K=50 is heavy.
The outer loop runs every K from `MinClusters` to `MaxClusters` sequentially;
a K=2..50 sweep on 16 chunks is 49 parallel batch jobs, each potentially taking
several minutes at high K. Consider a first pass with `MaxClusters=20` to find
the score plateau before committing to a wider search.

---

## References

Hazan L, Zugaro M, Buzsáki G (2006). Klusters, NeuroScope, NDManager: a free
software suite for neurophysiological data processing and visualization.
*Journal of Neuroscience Methods* 155:207-216.

Svihra M, Bhatt D, et al. (2020). KlustaKwik — spike sorting for large scale
multi-channel recordings. GitHub: klusta-team/klustakwik.
