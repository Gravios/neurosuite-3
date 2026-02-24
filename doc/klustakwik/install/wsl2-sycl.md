# KlustaKwik — WSL2 Installation, Intel Arc / SYCL

Under WSL2 the GPU driver lives on the Windows host. The Linux side only needs the Level-Zero and OpenCL runtime packages — not the Windows GPU driver. The oneAPI compiler (`icpx`) is installed inside WSL exactly as on bare-metal Ubuntu.

WSL2 GPU passthrough requires Intel GPU driver version **31.0.101.4887 or newer** on the Windows host. Check in Windows Device Manager → Display Adapters → Intel GPU → Properties → Driver tab.

## Step 1 — Install the oneAPI compiler (inside WSL)

Same as [linux-sycl.md Step 1](linux-sycl.md) — add the oneAPI APT repository and install `intel-oneapi-compiler-dpcpp-cpp`.

## Step 2 — Add the Intel GPU repository (Ubuntu 24.04 noble)

Use the `client` channel. The `unified` channel is missing `libigc1` / `libigdfcl1` on noble.

```bash
wget -qO - https://repositories.intel.com/gpu/intel-graphics.key \
    | sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg

echo 'deb [arch=amd64 signed-by=/usr/share/keyrings/intel-graphics.gpg] \
    https://repositories.intel.com/gpu/ubuntu noble client' \
    | sudo tee /etc/apt/sources.list.d/intel-gpu.list

sudo apt update
```

## Step 3 — Install GPU runtime packages

```bash
sudo apt install -y \
    libze1 intel-level-zero-gpu intel-opencl-icd intel-ocloc \
    clinfo libze-dev level-zero-dev
```

If `intel-level-zero-gpu` fails with missing `libigc1` / `libigdfcl1`:

```bash
sudo apt install -y libigc1 libigdfcl1
sudo apt install -y intel-level-zero-gpu intel-opencl-icd intel-ocloc
```

## Step 4 — Add user to render and video groups

```bash
sudo usermod -aG render $USER
sudo usermod -aG video $USER
```

Restart WSL from PowerShell:

```powershell
wsl.exe --shutdown
```

## Step 5 — Activate oneAPI environment

```bash
echo 'source /opt/intel/oneapi/setvars.sh' >> ~/.bashrc
source ~/.bashrc
```

## Step 6 — Verify

```bash
clinfo -l      # should show Intel(R) OpenCL Graphics GPU
sycl-ls        # should show level_zero:gpu and opencl:gpu entries
```

A working setup on a Meteor Lake Core Ultra system looks like:

```
$ sycl-ls
[level_zero:gpu][level_zero:0] Intel(R) oneAPI Unified Runtime over Level-Zero, Intel(R) Graphics [0x7d55] ...
[opencl:gpu][opencl:1] Intel(R) OpenCL Graphics, Intel(R) Graphics [0x7d55] ...
```

If only `opencl:cpu` appears with no GPU, the Windows Intel GPU driver needs updating to 31.0.101.4887 or later.

## Step 7 — Build

```bash
cd /path/to/neurosuite-3/src/klustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

## Step 8 — Verify binary

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu KlustaKwik --help
```

## WSL2 CPU core allocation

If `nproc` returns fewer cores than your Windows host has, edit `C:\Users\<user>\.wslconfig`:

```ini
[wsl2]
processors=16    ; set to the physical core count of the host
```

Then from PowerShell:

```powershell
wsl --shutdown
```
