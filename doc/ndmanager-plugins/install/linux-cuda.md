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

The first four set `CMAKE_CUDA_ARCHITECTURES` to `86 89 100 120` in their own
CMakeLists, overriding any `-DCMAKE_CUDA_ARCHITECTURES` you pass. Compute
capability 12.0 (Blackwell) needs **CUDA ≥ 12.8**, so with these four the
toolkit must be 12.8 or newer whenever `nvcc` is on `PATH`, whatever GPU you
have.

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
-- process_butterworth: CUDA build (86;89;100;120)
-- process_medianfilter: CUDA build (86;89;100;120)
-- process_medianthreshold: CUDA build (86;89;100;120)
-- process_spikegrouper: CUDA build (86;89;100;120)
```

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
