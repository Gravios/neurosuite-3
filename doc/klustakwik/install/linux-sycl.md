# KlustaKwik — Linux Installation, Intel Arc / SYCL (bare metal)

Requires Intel oneAPI Base Toolkit ≥ 2023.1. Supports Intel Arc (Alchemist, Battlemage), Iris Xe, and Meteor Lake integrated graphics.

## Step 1 — Install the oneAPI compiler

```bash
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
    | gpg --dearmor \
    | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null

echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] \
    https://apt.repos.intel.com/oneapi all main" \
    | sudo tee /etc/apt/sources.list.d/oneAPI.list

sudo apt update
sudo apt install -y intel-oneapi-compiler-dpcpp-cpp
```

This installs only `icpx` and the SYCL runtime (~2 GB). Install `intel-basekit` if you also want VTune and MKL.

## Step 2 — Install Intel GPU runtime packages

```bash
. /etc/os-release   # sets $VERSION_CODENAME

wget -qO - https://repositories.intel.com/gpu/intel-graphics.key \
    | sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg

echo "deb [arch=amd64 signed-by=/usr/share/keyrings/intel-graphics.gpg] \
    https://repositories.intel.com/gpu/ubuntu ${VERSION_CODENAME} client" \
    | sudo tee /etc/apt/sources.list.d/intel-gpu.list

sudo apt update
sudo apt install -y \
    libze1 intel-level-zero-gpu intel-opencl-icd intel-ocloc \
    clinfo libze-dev level-zero-dev
```

On Ubuntu 24.04 (noble), if `intel-level-zero-gpu` fails with missing `libigc1` / `libigdfcl1`:

```bash
sudo apt install -y libigc1 libigdfcl1
sudo apt install -y intel-level-zero-gpu intel-opencl-icd intel-ocloc
```

## Step 3 — Add user to render group

```bash
sudo usermod -aG render $USER
sudo usermod -aG video $USER
# Log out and back in, or: newgrp render
```

## Step 4 — Activate oneAPI environment

```bash
echo 'source /opt/intel/oneapi/setvars.sh' >> ~/.bashrc
source ~/.bashrc
```

## Step 5 — Verify

```bash
clinfo -l        # should show Intel(R) OpenCL Graphics GPU
sycl-ls          # should show level_zero:gpu and opencl:gpu entries
```

## Step 6 — Build

```bash
cd /path/to/neurosuite-3/src/klustakwik

# icpx is auto-detected from PATH after sourcing setvars.sh
cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## Verify binary

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu KlustaKwik --help
KlustaKwik_cpu --help   # CPU fallback
```

## Note on Meteor Lake iGPUs

Integrated Arc GPUs on Meteor Lake (Core Ultra) share memory with the CPU — SYCL USM allocations are zero-copy on this device. KlustaKwik's feature matrices are typically small enough that transfer overhead is negligible, making the iGPU effective for this workload.
