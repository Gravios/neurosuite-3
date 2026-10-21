# Apple GPU (Metal) backend — integration plan

*Written 2026-10-21 against `0d1c903`. Status: decisions taken (§8); Phase 0
(Accelerate) implemented, awaiting a run on a Mac; Phases 1–5 not started.*

## 1. Goal and scope

Give the macOS build the same GPU acceleration as Linux and Windows. It adds a
fourth backend, **Metal**, next to CUDA, HIP and SYCL. The scope is Apple-silicon
Macs (M1 and later) running macOS 14 or newer.

Intel Macs are out of scope. They do have Metal-capable AMD and Intel GPUs, but:
- CI is arm64 only;
- Homebrew/Qt support for Intel Macs is fading;
- limiting the backend to Apple silicon (Apple7+ GPU family, MSL 3) lets the
  kernels rely on simdgroup operations and float atomics without feature checks.

None of CUDA, HIP, SYCL or OpenCL is a usable route on a Mac:
- **CUDA:** no driver.
- **HIP:** ROCm is Linux and Windows only.
- **SYCL:** oneAPI has no macOS GPU backend.
- **OpenCL:** deprecated and frozen at 1.2.

So every GPU site needs its own Metal implementation. The OpenMP CPU paths
already work on macOS. Where a site cannot move to Metal (see §3), it stays on
the CPU, either as it is or accelerated with Apple's **Accelerate** framework.

## 2. Inventory of GPU sites (from the source, not from the README)

| # | Site | CUDA file (lines, kernels) | Other backends | GPU precision today | Library calls |
|---|---|---|---|---|---|
| 1 | KiloKlustaKwik CEM (E/M/C-step, score, deletion loss) | `kiloklustakwik/src/KK_cuda.cu` (607, 9) | HIP, SYCL | **float** | — |
| 2 | KK shift-probe projection | `kiloklustakwik/src/shiftprobe_cuda.cu` (445, 1) | HIP, SYCL | int16 waves, **double** eigvecs/means | — |
| 3 | KK fiber mean-shift / assign | `kiloklustakwik/src/fiber_gpu_cuda.cu` (223, 2) | CUDA only | **double** | — |
| 4 | Realign cross-correlation (KK + Klusters) | `libklustersshared/src/xcorr/realign_xcorr_cuda.cu` (238, 3) | HIP, SYCL, OMP | int16 in, **float** | — |
| 5 | Klusters Grouping Assistant (Mahalanobis error matrix) | `klusters/src/groupingassistant_cuda.cu` (456, 5) | HIP, SYCL | **double**, or **float** when `errorMatrixLowPrecision` | — |
| 6 | Klusters PCA refine | `klusters/src/pca_refine_cuda.cu` (343, 1) | CUDA only | **float** | — |
| 7 | NeuroScope spectrogram | `libneurosuite-core/.../spectral/spectralgpu.cu` (214, 2) | CUDA only | **double** | cuFFT `D2Z` |
| 8 | `process_butterworth` (ndm_hipass) | `process_butterworth_cuda.cu` (122, 1) | CUDA only | **float by default on GPU** (`useDouble = precision<0 ? !gpu : precision==1`) | — |
| 9 | `process_medianfilter` | `process_medianfilter_cuda.cu` (398, 1) | CUDA only | int16 | — |
| 10 | `process_medianthreshold` (ndm_detectspikes) | `process_medianthreshold_cuda.cu` (191, 2) | CUDA only | int16, double sigma per channel | — |
| 11 | `process_spikegrouper` | `process_spikegrouper_cuda.cu` (421, 5) | CUDA, OMP | int16, double sigma/threshold | — |
| 12 | `process_extractemg` | `process_extractemg_cuda.cu` (233, 1) | HIP, SYCL | **double** | cuBLAS `Dgemv` |
| 13 | `process_decomposecollisions` (opt-in `--gpu true`) | inside `process_decomposecollisions.cpp` | CUDA only | **float** | cuBLAS `Sgemm` |

Sites 2, 3, 6, 7 and 13 are missing from the README's GPU table, or folded into
other rows. The README table should be completed whatever happens with this plan.

## 3. The deciding constraint: no fp64 on Apple GPUs

The Metal Shading Language has no `double`. Each site therefore falls into one
of three classes:

**A. Already fp32 or integer: port directly.**
- Sites 1, 4, 6 and 13 are float.
- Sites 9, 10 and 11 are int16 data. Their per-channel sigma (median/0.6745) is
  one value per channel: compute it on the host in double, or in float in the
  kernel, and the `.res` output is unchanged.
- Site 8 already defaults to float SOS on the GPU, so its Metal path matches the
  CUDA default. A request for `precision=1` (double) goes to the CPU path, with a
  one-line notice.

**B. fp64 today, but fp32 is plausible: port, then validate.**
- **Site 2, shift probe.** Cast the eigenvectors and means to float on upload, as
  `pca_refine_cuda.cu` already does for site 6. Accept the result only if the
  chosen shifts match the CPU's on the reference sessions.
- **Site 5, Grouping Assistant.** Metal offers only the existing FP32 path.
  Selecting FP64 runs the CPU/OpenMP path. The CUDA comment already notes the
  error matrix is qualitative, with about 5e-4 error in FP32.

**C. fp64-bound: stay on the CPU and use Accelerate.**
- **Site 7, spectrogram.** This is a double real-to-complex FFT. MPSGraph FFTs are
  float only. The CPU path already works on macOS (FFTW or `realfft.h`); the
  optional upgrade is vDSP's double-precision DFT (`vDSP_DFT_zrop_CreateSetupD`),
  parallel across segments.
- **Site 12, EMG.** This is a `Dgemv`. Use Accelerate's `cblas_dgemv` on the CPU.
  On Apple silicon it uses the AMX units and should be close to GPU speed for a
  matrix-vector product, which is memory-bound anyway.
- **Site 3, fiber mean-shift.** This is iterative mean-shift in double, where
  convergence is sensitive to precision. It stays on CPU/OpenMP. Revisit only if
  profiling on a Mac shows it dominates the run time.

## 4. Architecture

### 4.1 The backend fits the existing pattern

The repo already has the shape a fourth backend needs:

- **Runtime dispatchers** (`realign_xcorr_dispatch.cpp`,
  `groupingassistant_gpu_dispatch.cpp`, `pca_refine_gpu_dispatch.cpp`) probe the
  backends in order. Each backend exports `extern "C" xxx_<backend>_available()` /
  `xxx_<backend>_compute(...)`, and `#ifndef USE_<BACKEND>` stubs fill in for
  backends that were not built. Metal adds a `USE_METAL` block and a
  `Backend::Metal` value. The probe order becomes CUDA → HIP → SYCL → **Metal** →
  OMP; on a Mac only Metal and OMP are ever present.
- **Per-binary backends.** `KiloKlustaKwik`, `process_butterworth`,
  `process_medianthreshold`, `process_spikegrouper` and the other plugins build a
  GPU executable when a backend is found, and otherwise fall back to the CPU one.
  On macOS with `USE_METAL=ON`, the canonical name (`KiloKlustaKwik`,
  `process_butterworth`, …) is the Metal build, and `*_cpu` stays the reference.
  This matches the `KiloKlustaKwik` / `KiloKlustaKwik_cpu` convention the scripts
  already rely on.

### 4.2 Language: metal-cpp, with Objective-C++ only where MPS is needed

- **metal-cpp (decided).** Apple's header-only C++ wrapper for Metal (Apache-2.0), vendored
  under `third_party/metal-cpp`. It keeps host code in C++20 under the existing
  conventions: no `m_`, no C-style casts. Its objects are manually
  retain/release'd, so they get a small RAII holder in core (§4.4).
- **Objective-C++ (`.mm`) only for Metal Performance Shaders.** metal-cpp does not
  wrap MPS. The only site that would use MPS is 13 (`MPSMatrixMultiplication` for
  the `Sgemm`), and a hand-written tiled GEMM kernel is a reasonable alternative.
  Decide when that site is reached.

### 4.3 How kernels are built and loaded

Kernels are written in the Metal Shading Language (`.metal`). There are two ways
to get them into the binary:

1. **Runtime compile (decided default).** CMake embeds each `.metal` file as a
   string, using the same generated-`.inc` technique as `pipelinescriptdefs.inc`.
   At first use the code calls `device->newLibrary(source, options)` and caches the
   compute pipeline states. This needs only the OS, not Xcode. Homebrew builds with
   only the Command Line Tools, and the CI runner, can then build the Metal backend.
2. **Offline `.metallib` (optional, `NS_METAL_PRECOMPILE=ON`).** Uses
   `xcrun -sdk macosx metal` and `metallib`, which need full Xcode (recent versions
   ship the Metal toolchain as a separate download). It gives faster first launch
   and compile errors at build time. The `.metallib` is embedded as bytes, so the
   installed command-line plugins never have to locate a sidecar file.

Both paths share one `ns_metal_add_kernels(<target> SOURCES …)` CMake helper in a
new `cmake/MetalBackend.cmake`, alongside `GpuBackends.cmake`.

### 4.4 Shared core support (new, in libneurosuite-core)

`neurosuite/gpu/metalcontext.{h,cpp}`, compiled only when `APPLE AND USE_METAL`,
provides:
- the default device and one command queue, created lazily; `available()` returns
  false when `MTLCreateSystemDefaultDevice()` returns nil;
- a cache of compute pipelines, keyed by kernel name;
- **shared-storage buffers.** Apple-silicon memory is unified, so
  `MTLResourceStorageModeShared` buffers are visible to CPU and GPU. Page-aligned
  host vectors can be wrapped with `newBuffer(bytesNoCopy…)`, which removes the
  explicit uploads and downloads the CUDA paths manage;
- a dispatch helper that splits long jobs into several command buffers. macOS can
  terminate a GPU job that runs too long without yielding. The existing CUDA chunk
  loops (Butterworth rows, spectral batches) already have the right shape: reuse
  their chunk sizes.
- no new environment knob. There is no repo-wide GPU-disable variable today:
  the plugins pick the CPU by running the `*_cpu` binary or `--gpu false`, and
  Klusters by its preferences. Metal follows the same switches. A shared
  `NS_GPU_DISABLE` would be a separate, cross-backend patch.

### 4.5 CUDA → MSL translation reference

| CUDA | Metal Shading Language |
|---|---|
| `__global__ void k(...)` | `kernel void k(... [[buffer(n)]])` |
| `blockIdx`, `threadIdx`, `blockDim` | `[[threadgroup_position_in_grid]]`, `[[thread_position_in_threadgroup]]`, `[[threads_per_threadgroup]]` |
| `__shared__` / dynamic `extern __shared__` | `threadgroup` arrays / `setThreadgroupMemoryLength` |
| `__syncthreads()` | `threadgroup_barrier(mem_flags::mem_threadgroup)` |
| `__shfl_down_sync`, warp reductions | `simd_shuffle_down`, `simd_sum` (simdgroup = 32 threads) |
| `atomicAdd(float*)` | `atomic_fetch_add_explicit` on `device atomic_float*` (MSL 3) |
| `long int` indices | `long` / `ulong`; MSL has 64-bit integers |
| `--use_fast_math` | `fastMathEnabled` / `mathMode` compile option; leave it **off** for validation runs |
| `<<<grid, block, shmem>>>` | `dispatchThreadgroups(grid, block)` |
| max 1024 threads per block | max 1024 per threadgroup; query `maxTotalThreadsPerThreadgroup` per pipeline |

The **HIP** files are the closest starting point for sites 1, 2, 4, 5 and 12: they
are already a mechanical translation away from CUDA intrinsics.

## 5. Phases (one concern per patch)

Each phase ends with a validation gate (§6). Patch counts are rough.

### Phase 0: Accelerate CPU paths (no GPU, testable in CI): done, see §9

1. Spectral: `spectralfft.cpp` already has two providers, FFTW (`HAVE_FFTW`)
   and the built-in `realfft.h`. Add vDSP as a third (`HAVE_VDSP`, preferred on
   `APPLE`), and keep `spectralgpu_cpu.cpp` as the batch path. A differential test
   checks it against the `realfft.h` path, with a relative error tolerance of 1e-12
   on the PSD. Homebrew's FFTW is already NEON-optimised, so the gain over FFTW may
   be small: benchmark before merging. *(1–2 patches)*
2. EMG: the CPU path (`process_extractemg_gpu_cpu.cpp` and the loops in
   `process_extractemg.cpp`) is hand-written. Replace the `Dgemv`-shaped loops
   with Accelerate's `cblas_dgemv` on `APPLE`, matching the cuBLAS / rocBLAS calls
   on the GPU paths. *(1 patch)*
3. Benchmark both against the current CPU path on an M-series Mac before
   deciding anything else. If Accelerate already gives the speed users need, the
   class-C sites are finished.

### Phase 1: Infrastructure and the first kernel

4. `cmake/MetalBackend.cmake`: `USE_METAL` (default ON when `APPLE`), detection
   (`find_library(Metal)`, `Foundation`, `QuartzCore`), the
   `ns_metal_add_kernels` helper, and vendored metal-cpp. Off everywhere else, so
   Linux and Windows builds are unchanged. *(1 patch)*
5. `MetalContext` in libneurosuite-core, plus a smoke-test kernel (saxpy) as a
   unit test that **skips** with exit code 0 and a message when no device is
   present. *(1 patch)*
6. **First real port: `process_medianthreshold`** (2 small int16 kernels, exact
   integer output). It proves the whole chain: CMake, embedding, context, the
   canonical binary name, the `.res` round trip. *(1 patch)*

### Phase 2: The remaining ndmanager plugins

7. `process_medianfilter` (1 kernel, int16). *(1 patch)*
8. `process_spikegrouper` (5 kernels: scatter, abs, median, detect,
   coincidence). *(1–2 patches)*
9. `process_butterworth` (1 kernel, float SOS; the Metal build forces `precision`
   to single, or falls back to the CPU when double is asked for). *(1 patch)*
10. `process_decomposecollisions` (2 `Sgemm` calls; MPS in a `.mm` file, or a
    tiled GEMM kernel). *(1–2 patches)*

### Phase 3: Klusters

11. Realign cross-correlation (3 float kernels). It sits behind an existing
    dispatcher and is shared with KiloKlustaKwik, so it is the cleanest dispatcher
    integration. *(1 patch kernel + 1 patch dispatch)*
12. PCA refine (1 float kernel; dispatcher exists). *(1 patch)*
13. Grouping Assistant, FP32 path only; FP64 → CPU with a status-bar note.
    *(1–2 patches)*

### Phase 4: KiloKlustaKwik

14. The CEM kernels (9, all float), ported from `KK_hip.cpp`. M-step
    accumulation uses float atomics. Because atomic order is not deterministic,
    run-to-run results can differ in the last bits, exactly as on CUDA.
    *(2–3 patches: E/C-step, M-step, score/deletion)*
15. Shift probe (float eigvecs/means, class B). *(1 patch)*
16. Fiber mean-shift stays on CPU/OpenMP (class C); no patch unless profiling
    shows otherwise.

### Phase 5: CI, docs, release notes

17. `ci-macos.yml`: configure with `USE_METAL=ON`. This proves the host code
    compiles and that **every kernel compiles at run time** through a
    `--metal-selftest` test, which builds every embedded library. It runs where a
    device is exposed and skips otherwise. Numerical GPU tests are not expected to
    run on hosted runners: GitHub's macOS VMs expose at best a limited
    paravirtualized GPU. *(1 patch)*
18. README GPU table: a Metal column, plus the rows missing today (§2).
    INSTALL.md macOS section, BUILD.md, ROADMAP, CHANGELOG. *(1–2 patches)*

**Total:** about 20–25 patches. Phases 0–2 are self-contained and useful on their
own. Phase 4 has the largest payoff and the largest validation burden.

## 6. Validation protocol

None of this can be validated in the current environment, which has no Mac. Every
GPU claim needs a run on an Apple-silicon Mac, as the reference sessions require.

| Output | Test | Pass criterion |
|---|---|---|
| `.res` from medianthreshold / spikegrouper | Metal vs `_cpu` binary | identical files |
| `.fil` / filtered int16 (medianfilter, Butterworth) | Metal vs CPU at the same precision | max \|Δ\| ≤ 1 LSB; Butterworth float vs double ≤ the CUDA-float figure |
| Spectral PSD (vDSP) | vs current CPU path | relative error ≤ 1e-12 |
| EMG (Accelerate) | vs current CPU path | relative error ≤ 1e-12 |
| Realign shifts / PCA refine | Metal vs OMP | identical integer shifts on ≥ 99.9 % of spikes; same as the CUDA-vs-OMP rate |
| Grouping Assistant FP32 | Metal vs CUDA FP32 | max \|Δ\| in the error matrix ≤ 1e-3 |
| KK `.clu` | Metal vs `KiloKlustaKwik_cpu` | cluster agreement (ARI, matched confusion) **no worse than CUDA vs CPU** on jg05-20120316 group 7 and eb05-20251118 group 25 |

KK results are trajectory-dependent: float EM steps diverge from the CPU after a
few iterations. Byte equality is therefore not the target; the baseline is "as
close as CUDA is". Record the CUDA-vs-CPU figure on the Linux workstation first,
so the Mac result has something to compare against.

## 7. Risks

- **Nobody on the team has an Apple-silicon Mac.** This is the gating risk.
  Phases 1+ should not be merged as "working" until someone runs §6. Options:
  borrow a Mac for a validation day; ask a collaborating lab; rent a hosted Mac
  (e.g. a cloud M-series instance) for the validation runs only.
- **Float-only GPU.** Mitigated by the A/B/C classification. The risk is a class-B
  site failing validation; the fallback is the CPU path, which costs nothing
  extra.
- **GPU watchdog on long kernels.** Mitigated by command-buffer chunking (§4.4).
  Must be tested with a full-length session, not only a short excerpt.
- **metal-cpp / Xcode drift.** Vendor a pinned metal-cpp; the runtime-compile
  default avoids depending on the Xcode toolchain.
- **Four backends to keep in sync per site.** Every kernel change now needs four
  edits where it needed three. The existing dispatcher tests (OMP vs GPU
  differential) should become a per-backend matrix, so drift is caught where
  hardware exists.

## 8. Decisions (2026-10-21)

1. The Mac is in scope: all phases, starting with Phase 0.
2. Host code on Apple's C++ wrapper (metal-cpp); Objective-C++ only for an MPS
   call, if one is ever needed.
3. Kernels embedded as source and compiled at run time; an offline `.metallib`
   stays an optional extra.
4. Apple silicon only.
5. Validation on an Apple-silicon Mac the project owner will arrange.

## 9. Phase 0 as implemented (2026-10-21)

- **Spectral:** `RealFftPlan` has a vDSP provider (`HAVE_VDSP`). Auto order:
  FFTW exact, vDSP exact, else the next power of two with vDSP or radix-2;
  `NS_SPECTRAL_FFT=fftw|vdsp|radix2` overrides it. Each plan checks vDSP's
  packing and scale against a direct DFT and falls back if they differ from
  Apple's documentation; concurrent calls lease setups from a per-plan pool.
- **EMG:** `IcaBackend::Accelerate` (`--backend accelerate`): two
  `cblas_dgemv` per cache-sized row block, so Z is read once per step. It is
  **not** taken by `--backend auto` until a benchmark shows it ahead of the
  OpenMP kernel (the step is memory-bound).
- **Tests:** `core_test_spectral` (every provider vs a direct DFT, timing per
  provider) and `ndm_test_extractemg_pow3` (`--bench N nch` for a real size);
  the macOS CI job runs both as a required step.
- **To do on a Mac:** run both tests, plus
  `process_extractemg_pow3_test --bench 20000000 64`. If vDSP beats FFTW,
  consider putting vDSP first in Auto. If Accelerate beats the CPU kernel,
  add it to `icaPickBackend`'s Auto order.

Next is Phase 1, up to `process_medianthreshold`: it proves the toolchain end
to end on the simplest kernel, with an exact-match test.
