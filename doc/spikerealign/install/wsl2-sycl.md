# SpikeRealign — WSL2 Installation, Intel Arc / SYCL

## Step 1 — Install oneAPI and Level-Zero runtime

Follow **[doc/gpu/README.md — Intel SYCL / oneAPI — WSL2](../../gpu/README.md#wsl2-intel-arc-via-wsl2-gpu-passthrough)** for complete installation instructions.

## Step 2 — Build

Identical to bare-metal Linux — see [linux-sycl.md](linux-sycl.md).

## Verify

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu SpikeRealign --help
```
