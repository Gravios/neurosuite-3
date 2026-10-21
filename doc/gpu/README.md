# GPU Compute — Installation Guide

Several neurosuite-3 components optionally use GPU acceleration:

| Component | GPU path | What is accelerated |
|---|---|---|
| `kiloklustakwik` | CUDA / HIP / SYCL | CEM E-step distance computations + waveform cross-correlation (xcorr) |
| `klusters` | CUDA / HIP / SYCL | Grouping Assistant posterior probability matrix + interactive spike realignment (xcorr) |
| `process_butterworth` | CUDA | Butterworth filtering |
| `process_medianfilter` | CUDA · Metal (macOS) | Median-subtraction high-pass filter |
| `process_medianthreshold` | CUDA · Metal (macOS) | Per-channel noise threshold estimation |
| `process_spikegrouper` | CUDA / OpenMP | Coincidence matrix construction |
| `process_decomposecollisions` | CUDA | Collision decomposition fit (opt-in with `--gpu true`) |
| `process_extractemg` | CUDA / HIP / SYCL | EMG extraction (SYCL not on Windows) |
| `libneurosuite-core` spectral (NeuroScope spectrogram) | CUDA (cuFFT) | FFTs |

The waveform realignment cross-correlation kernel (`realign_xcorr`) is compiled into both
`kiloklustakwik` and `klusters` — it is not a separate binary.

GPU backends are auto-detected by CMake at build time. `kiloklustakwik` and `process_extractemg` compile one GPU backend per build, chosen in the order CUDA > HIP > SYCL. If none is found the build falls back to an OpenMP CPU path automatically — you do not need to set any flag to get a working build. Pass `-DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF` to force a CPU-only build explicitly.

---

## NVIDIA CUDA

Supported by kiloklustakwik, klusters, process\_butterworth, process\_medianfilter, process\_medianthreshold, process\_spikegrouper, process\_decomposecollisions, process\_extractemg and the libneurosuite-core spectral library (cuFFT).

### Driver and toolkit versions

| GPU generation | Architecture | `sm_` target | Minimum CUDA | Minimum driver |
|---|---|---|---|---|
| RTX 30xx | Ampere | sm\_86 | 11.1 | 455 |
| RTX 40xx | Ada Lovelace | sm\_89 | 11.8 | 520 |
| RTX 50xx (Blackwell) | Blackwell | sm\_120 | **12.8** | **570** |
| A100 / H100 | Ampere / Hopper | sm\_80 / sm\_90 | 11.0 | 450 |

Ubuntu 24.04's packaged `cuda-toolkit` is version 12.0 and **does not support sm\_120**. Install CUDA 12.8+ directly from NVIDIA's repository.

### Ubuntu 24.04 — CUDA 12.8 (NVIDIA repository)

```bash
# 1. Add NVIDIA's keyring and repository
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt-get update

# 2. Install toolkit and open-kernel driver
#    nvidia-open is required for RTX 5000 series (Blackwell)
sudo apt-get install -y cuda-toolkit-12-8 nvidia-open

# 3. Add nvcc to PATH (add to ~/.bashrc for persistence)
export PATH="/usr/local/cuda-12.8/bin:$PATH"
export LD_LIBRARY_PATH="/usr/local/cuda-12.8/lib64:${LD_LIBRARY_PATH:-}"

# 4. Verify
nvcc --version
nvidia-smi
```

### Secure Boot and kernel module signing (Ubuntu 24.04)

If Secure Boot is enabled, the NVIDIA open kernel module must be signed with a Machine Owner Key (MOK):

```bash
# Generate a MOK key pair (run once)
sudo mkdir -p /var/lib/shim-signed/mok
sudo openssl req -new -x509 -newkey rsa:2048 -keyout /var/lib/shim-signed/mok/MOK.key \
    -out /var/lib/shim-signed/mok/MOK.crt -days 36500 -subj "/CN=NVIDIA MOK/" -nodes
sudo openssl x509 -in /var/lib/shim-signed/mok/MOK.crt \
    -out /var/lib/shim-signed/mok/MOK.der -outform DER

# Enrol the key (requires reboot and BIOS/UEFI confirmation)
sudo mokutil --import /var/lib/shim-signed/mok/MOK.der

# Reboot, confirm MOK enrolment in the UEFI blue screen, then sign the module
sudo /usr/src/linux-headers-$(uname -r)/scripts/sign-file sha256 \
    /var/lib/shim-signed/mok/MOK.key \
    /var/lib/shim-signed/mok/MOK.crt \
    $(modinfo -n nvidia)
sudo modprobe nvidia
```

### Windows

> **Status (2026-10):** the Windows presets and CI build with `USE_CUDA=OFF`,
> `USE_HIP=OFF` and skip KiloKlustaKwik; the only GPU path in the documented
> Windows build is Klusters with SYCL through the `windows-sycl` preset
> ([INSTALL.md → Intel GPU (SYCL)](../INSTALL.md#intel-gpu-sycl)). CUDA and HIP on
> Windows are untested. For KiloKlustaKwik use WSL2.

Install the [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads) 12.8 or later from NVIDIA's download page. Choose the local installer and add `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8\bin` to `PATH`. Visual Studio 2022 integration is included automatically.

### macOS

NVIDIA dropped macOS support in macOS 10.14 (Mojave). CUDA is not available on macOS. All GPU-accelerated components build as CPU-only.

---

## AMD ROCm / HIP

Supported by kiloklustakwik, klusters and process\_extractemg.

### Ubuntu 22.04 / 24.04

```bash
# 1. Add AMD's ROCm repository
wget https://repo.radeon.com/amdgpu-install/6.2.2/ubuntu/noble/amdgpu-install_6.2.60202-1_all.deb
sudo dpkg -i amdgpu-install_6.2.60202-1_all.deb
sudo apt-get update

# 2. Install ROCm (HIP compiler + runtime)
sudo amdgpu-install --usecase=rocm,hip --no-32

# 3. Add your user to the render and video groups
sudo usermod -aG render,video $USER
newgrp render

# 4. Add ROCm to PATH
export PATH="/opt/rocm/bin:$PATH"
export LD_LIBRARY_PATH="/opt/rocm/lib:${LD_LIBRARY_PATH:-}"

# 5. Verify
hipcc --version
rocm-smi
```

Supported GPU families: RDNA2 (gfx1030), RDNA3 (gfx1100), CDNA2 (gfx90a), CDNA3 (gfx940/gfx942). Default targets per component:

| Component | Default architectures | Override |
|---|---|---|
| `klusters` | `gfx1100;gfx1030;gfx90a;gfx908` | fixed in `src/klusters/src/CMakeLists.txt` |
| `kiloklustakwik` | `gfx1030;gfx1031;gfx1100;gfx1101;gfx90a;gfx908` | `-DKK_EXP_HIP_ARCHS=…` |
| `process_extractemg` | `gfx1030;gfx1100;gfx90a;gfx908` | `-DEXTRACTEMG_HIP_ARCHS=…` |

### Windows

Install [ROCm for Windows](https://rocm.docs.amd.com/en/latest/deploy/windows/index.html) 6.x. At the time of writing, ROCm Windows support is in preview and primarily targets the Radeon RX 7000 series. Refer to AMD's current documentation for the latest supported GPU list.

---

## Intel SYCL / oneAPI (Intel Arc)

Supported by kiloklustakwik, klusters and process\_extractemg (Linux). Requires Intel oneAPI Base Toolkit ≥ 2023.1 and the `icpx` compiler.

The top-level build does **not** select the oneAPI compiler by itself. On
Linux, configure with it explicitly (see [BUILD.md §4](../BUILD.md#4-gpu-backends)):

```bash
source /opt/intel/oneapi/setvars.sh
cmake -B build -DCMAKE_CXX_COMPILER=icpx -DCMAKE_C_COMPILER=icx
```

On Windows use the `windows-sycl` preset from the oneAPI prompt (it builds
Klusters with SYCL; KiloKlustaKwik is skipped). Only a standalone
`src/kiloklustakwik` configure picks `icpx` automatically. A SYCL build of
kiloklustakwik installs `KiloKlustaKwik_sycl` and a copy named
`KiloKlustaKwik`.

### Ubuntu 22.04 / 24.04 (bare metal Intel Arc)

```bash
# 1. Add Intel's apt repository
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
    | gpg --dearmor | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] \
    https://apt.repos.intel.com/oneapi all main" \
    | sudo tee /etc/apt/sources.list.d/oneAPI.list
sudo apt-get update

# 2. Install the Base Toolkit (includes icpx, SYCL runtime, oneMKL)
sudo apt-get install -y intel-basekit

# 3. Source the environment
source /opt/intel/oneapi/setvars.sh

# 4. Verify
icpx --version
sycl-ls   # lists available SYCL devices
```

### WSL2 (Intel Arc via WSL2 GPU passthrough)

Intel Arc GPUs are accessible from WSL2 via the Level Zero backend without any extra driver installation — the Windows Intel graphics driver exposes the device through WSL2's GPU virtualisation layer.

```bash
# Inside WSL2 — install the oneAPI Base Toolkit as above, then:
source /opt/intel/oneapi/setvars.sh
sycl-ls   # should show ext_oneapi_level_zero:gpu:0
```

See [doc/kiloklustakwik/install/wsl2-sycl.md](../kiloklustakwik/install/wsl2-sycl.md) for a complete walkthrough including GPU passthrough verification.

### Windows (native)

Download and install the [Intel oneAPI Base Toolkit](https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html) for Windows. Select the offline installer. After installation, open the **Intel oneAPI command prompt for Intel 64 for Visual Studio 2022** before running `cmake`, and build with the `windows-sycl` preset ([INSTALL.md → Intel GPU (SYCL)](../INSTALL.md#intel-gpu-sycl)).

---

## Verifying GPU detection at build time

After installing the GPU toolkit, run `cmake` on the repository root and check the status messages:

```
-- neurosuite-3: CUDA compiler — /usr/local/cuda-12.8/bin/nvcc
-- neurosuite-3: CUDA architectures = native (GPU detected via nvidia-smi)
-- CUDA backend enabled — compiler: /usr/local/cuda-12.8/bin/nvcc
-- neurosuite-spectral: CUDA (cuFFT) GPU backend enabled
-- process_butterworth: CUDA build (86;89;100;120)
-- process_medianfilter: CUDA build (86;89;100;120)
-- KiloKlustaKwik: CUDA available — /usr/local/cuda-12.8/bin/nvcc
-- KiloKlustaKwik: active backend → CUDA  (NVIDIA)
```

If only the CPU path is selected you will see messages such as:

```
-- neurosuite-3: CUDA toolkit not found — building without CUDA
-- KiloKlustaKwik: CUDA requested but nvcc not found
-- KiloKlustaKwik: HIP requested but hipcc not found — install ROCm: https://rocm.docs.amd.com/
-- KiloKlustaKwik: SYCL requested but icpx not found — install oneAPI: …
-- KiloKlustaKwik: active backend → CPU only (no GPU toolkit found)
```

Pass `-DCMAKE_CUDA_ARCHITECTURES="120"` to target only a specific GPU generation and reduce build time. The `process_*` CUDA programs build for `86;89;100;120` regardless.

---

## Choosing a backend at runtime

Each GPU-enabled binary probes for available devices at startup and selects the first usable one. To force the CPU path at runtime without rebuilding:

| Binary | Override |
|---|---|
| `KiloKlustaKwik` | run `KiloKlustaKwik_cpu` instead (installed with every build; KiloKlustaKwik has no runtime CPU switch) |
| `process_butterworth` | `-c` |
| `process_medianfilter` | `-c` |
| `process_spikegrouper` | `--cpu` |
| `process_extractemg` | `--backend cpu` |
| `process_decomposecollisions` | CPU unless `--gpu true` is given |

For klusters, the xcorr backend is selected at runtime via `KLUSTERS_USE_SYCL=1` (SYCL only; CUDA and HIP are probed automatically).
