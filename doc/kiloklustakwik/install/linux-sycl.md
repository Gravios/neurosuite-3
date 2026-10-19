# KiloKlustaKwik — Linux Installation, Intel Arc / SYCL (bare metal)

KiloKlustaKwik is part of the top-level neurosuite-3 build
(**[doc/INSTALL.md → Linux](../../INSTALL.md#linux)**). The top-level build
does **not** pick the oneAPI compiler by itself: with the oneAPI environment
sourced, configure with

```bash
cmake -B build -DCMAKE_CXX_COMPILER=icpx -DCMAKE_C_COMPILER=icx -DUSE_CUDA=OFF -DUSE_HIP=OFF
```

([BUILD.md §4](../../BUILD.md#4-gpu-backends)). With the default compiler,
KiloKlustaKwik builds without SYCL. To build KiloKlustaKwik alone, add the
`NS_SKIP_*` flags shown in [linux-cpu.md](linux-cpu.md). The build steps
below are for `src/kiloklustakwik` on its own.

## Step 1 — Install oneAPI and the Intel GPU runtime

Install the oneAPI Base Toolkit as in **[doc/gpu/README.md — Intel SYCL / oneAPI](../../gpu/README.md#intel-sycl--oneapi-intel-arc)**.
A bare-metal Arc GPU also needs Intel's GPU compute runtime (Level Zero and
OpenCL), installed per Intel's client-GPU driver documentation for your
Ubuntu release; that page does not cover it.

After sourcing the oneAPI environment, verify:

```bash
source /opt/intel/oneapi/setvars.sh
clinfo -l        # should show Intel(R) OpenCL Graphics GPU
sycl-ls          # should show level_zero:gpu and opencl:gpu entries
```

## Step 2 — System packages

```bash
sudo apt install cmake build-essential libyaml-cpp-dev
```

yaml-cpp is required; configure fails without it.

## Step 3 — Build

A standalone configure selects `icpx`/`icx` itself when the oneAPI
environment is sourced (it searches `$CMPLR_ROOT/bin`,
`$ONEAPI_ROOT/compiler/latest/bin` and `/opt/intel/oneapi/compiler/latest/bin`)
and no compiler was chosen with `-DCMAKE_CXX_COMPILER` or `CXX`.

```bash
cd /path/to/neurosuite-3/src/kiloklustakwik

cmake -B build \
  -DUSE_CUDA=OFF -DUSE_HIP=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

The build produces `KiloKlustaKwik_sycl` and `KiloKlustaKwik_cpu`; the install
also copies `KiloKlustaKwik_sycl` as `KiloKlustaKwik`. With `ocloc` present
the kernels are compiled ahead of time for `KK_EXP_SYCL_DEVICES` (default
`mtl-m,acm-g10,acm-g11,dg2,pvc`); without it the build is JIT-only.

## Verify

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:gpu KiloKlustaKwik --help
KiloKlustaKwik_cpu --help
```

## Note on Meteor Lake iGPUs

Integrated Arc GPUs on Meteor Lake (Core Ultra) share memory with the CPU — SYCL USM allocations are zero-copy on this device. KiloKlustaKwik's feature matrices are typically small enough that transfer overhead is negligible, making the iGPU effective for this workload.
