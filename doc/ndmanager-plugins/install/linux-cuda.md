# ndmanager-plugins — Linux Installation, NVIDIA CUDA

The plugins are built with the rest of neurosuite-3
(**[Installing neurosuite-3 → Linux](../../INSTALL.md#linux)**). This page is
only about their CUDA paths. Every program also has a CPU path, so CUDA is
optional.

## Which programs use CUDA

| Program | Used by | CUDA build switch |
|---|---|---|
| `process_butterworth` | `ndm_hipass` (default `filter: butterworth`) | built with CUDA whenever `nvcc` is found |
| `process_medianfilter` | `ndm_hipass` with `filter: median`, `ndm_bandpass` | built with CUDA whenever `nvcc` is found |
| `process_medianthreshold` | `ndm_detectspikes` (`standard` method), `ndm_redetectspikes`, `ndm_reextractspikes` | built with CUDA whenever `nvcc` is found |
| `process_spikegrouper` | `ndm_spikegrouper` | built with CUDA whenever `nvcc` is found |
| `process_decomposecollisions` | `ndm_decomposecollisions` | `PDC_USE_CUDA` (default `ON`); `process_decomposecollisions_cpu` is always built and is installed as `process_decomposecollisions` when there is no CUDA build |
| `process_extractemg` | `ndm_extractemg` | `USE_CUDA`, `USE_HIP`, `USE_SYCL` (each default `ON`; CUDA > HIP > SYCL > CPU) |

In the full build the first four use the architectures the top-level
configure chose: `native` (the GPU `nvidia-smi` sees), otherwise the fallback
list, or `-DCMAKE_CUDA_ARCHITECTURES`. Only a standalone configure of the
plugins (`cmake -S src/ndmanager-plugins`) sets `86 89 100 120` itself;
compute capabilities 10.0 and 12.0 (Blackwell) need **CUDA ≥ 12.8** there.

## Step 1 — Install the CUDA Toolkit

Follow **[doc/gpu/README.md — NVIDIA CUDA](../../gpu/README.md#nvidia-cuda)**
(CUDA 12.8, Secure Boot MOK signing).

## Step 2 — Build

With `nvcc` on `PATH`, build as in [INSTALL.md](../../INSTALL.md#linux); no
extra flags are needed. Build options and GPU backends are in
[BUILD.md §4](../../BUILD.md#4-gpu-backends). The system packages are those of
the [CPU guide](linux-cpu.md#system-packages) (including `libgsl-dev`).

## Verify

The programs have no `--version` option. Check the configure output instead:

```
-- process_butterworth: CUDA build (native)
-- process_medianfilter: CUDA build (native)
-- process_medianthreshold: CUDA build (native)
-- process_spikegrouper: CUDA build (native)
```

(`86;89;100;120` in a standalone plugin configure, or the list you passed.)

`CPU build` on those lines means CMake did not find `nvcc`: check
`nvcc --version` and re-configure from a clean build directory.

## CPU-only build with CUDA installed

`-DUSE_CUDA=OFF` does not stop the four programs above from building with
CUDA: they call `check_language(CUDA)` themselves. In a plugins-only build it
only affects `process_extractemg`; `process_decomposecollisions` has its own
`-DPDC_USE_CUDA=OFF`. To build the four on CPU, configure from a fresh build
directory in a shell where `nvcc` is not on `PATH`, and check that the
configure output says `CPU build` for them.

## `chunkSize` (median filter only)

`ndm_hipass` reads `chunkSize` only with `filter: median`. It is the number of
bytes `process_medianfilter` processes per pass: default 128 MiB, and values
above 512 MiB are clamped to 512 MiB. Lower it if the GPU runs out of memory:

```yaml
- name: ndm_hipass
  parameters:
    - name: chunkSize
      value: 67108864    # 64 MiB
```
