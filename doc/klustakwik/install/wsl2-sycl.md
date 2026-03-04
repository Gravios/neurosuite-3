# KlustaKwik — WSL2 Installation, Intel Arc / SYCL

Under WSL2 the GPU driver lives on the Windows host. The Linux side only needs the Level-Zero and OpenCL runtime packages — not the Windows GPU driver. The oneAPI compiler (`icpx`) is installed inside WSL exactly as on bare-metal Ubuntu.

WSL2 GPU passthrough requires Intel GPU driver version **31.0.101.4887 or newer** on the Windows host. Check in Windows Device Manager → Display Adapters → Intel GPU → Properties → Driver tab.

## Step 1 — Install oneAPI and Level-Zero runtime

Follow **[doc/gpu/README.md — Intel SYCL / oneAPI — WSL2](../../gpu/README.md#wsl2-intel-arc-via-wsl2-gpu-passthrough)** for complete installation instructions.

After sourcing the environment, verify that the GPU is visible:

```bash
source /opt/intel/oneapi/setvars.sh
sycl-ls   # should show ext_oneapi_level_zero:gpu:0
```

## Step 2 — Build

Identical to bare-metal Linux — see [linux-sycl.md Step 2–3](linux-sycl.md).

## Verify

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu KlustaKwik --help
```
