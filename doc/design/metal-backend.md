# Apple GPU (Metal) backend — integration plan

*Written 2026-10-21 against `0d1c903`. Status: decisions taken (§8); Phases 0
(Accelerate), 1 (Metal infrastructure, `process_medianthreshold`), 2 (the
other plugins), 3 (Klusters) and 4 (KiloKlustaKwik) implemented and run by
the macOS CI (§9–§13); Phase 5 not started.*

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

- **metal-cpp (decided).** Apple's header-only C++ wrapper for Metal (Apache-2.0),
  fetched at configure time from github.com/apple/metal-cpp at a pinned commit
  (or taken from `NS_METAL_CPP_DIR`), not vendored. It keeps host code in C++20 under the existing
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

Both paths would share one CMake helper (as built: `ns_metal_embed`, §10) in a
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
   (`find_library(Metal)`, `Foundation`), the `ns_metal_embed` helper, and
   metal-cpp fetched at a pinned commit. Off everywhere else, so Linux and
   Windows builds are unchanged. *(1 patch; done, §10)*
5. `MetalContext` in libneurosuite-core, plus a smoke-test kernel as a
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
    *(2–3 patches: E/C-step, M-step, score/deletion)* — as built, the M-step
    uses no atomics and runs are deterministic (§13).
15. Shift probe (float eigvecs/means, class B). *(1 patch)* — dropped: the
    probe is disabled in every build (`shiftprobe_disabled.cpp`), §13.
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

## 10. Phase 1 as implemented (2026-10-21)

- **`cmake/MetalBackend.cmake`:** `USE_METAL` (default ON, macOS only).
  `ns_metal_setup()` finds Metal + Foundation and metal-cpp, which is fetched
  from github.com/apple/metal-cpp at the macOS 15.2 release commit, or taken
  from `NS_METAL_CPP_DIR`. It defines `ns_metalcpp`. `ns_metal_embed(target
  symbol file.metal)` puts a kernel source into a target as
  `extern const char symbol[]`.
- **`Neurosuite::metal`** (`neurosuite/gpu/metalcontext.{h,cpp}`):
  - `MetalContext::instance()`;
  - `pipeline()`: run-time compile with fast math off, cached, thread-safe;
  - shared buffers, `succeeded()`, `threadgroupWidth()`, `MetalRef`,
    `AutoreleaseScope`;
  - metal-cpp's one implementation unit.

  Tested by `core_test_metal`.
- **`process_medianthreshold`:** an exact per-channel histogram of |x| on the
  GPU (32769 bins), with the medians read off it on the CPU using `medianOf()`'s
  definition. The result is identical to the CPU path, and the test
  (`ndm_test_medianthreshold_metal`) requires equality.
  - The CUDA path is not identical to the CPU: it returns the upper middle
    value for even counts and treats -32768 as the largest value. This is
    reported, not changed.
  - Performance: one global atomic per sample. Contention on common bins may
    make it slower than the CPU's `nth_element`; the test prints a timing to
    decide. A two-pass radix histogram in threadgroup memory is the known
    faster design.
- **CI:** the macOS job builds with Metal on (proving the host code and the
  fetch) and runs both tests. They print SKIPPED if the VM has no Metal device.
- **Checked without a Mac:** clang `-fsyntax-only` against the real metal-cpp
  headers, with stand-ins for the few macOS system headers, and the embed
  helper round-trip. The MSL kernels themselves have not been compiled. The
  run-time compile reports any error and the program falls back to the CPU.

## 11. Phase 2 as implemented (2026-10-21)

Each port is tested against its CPU path by a test the macOS CI runs (its
runner exposes a Metal device).

- **`process_medianfilter`** (`ndm_test_medianfilter`): checking the CPU
  path against an independent reference found it corrupted every recording
  larger than one chunk (since 2026-02-20), with a window one frame
  off-centre. The CPU path was fixed first. The CUDA path moved onto the same
  chunking: its own loop could use filtered samples as context, had garbage
  after the end of file, and could hit the grid limit. The Metal kernel then
  filters the same chunks: one thread per (channel, 512-frame tile), with the
  sorted window in threadgroup memory. Output is byte-identical to the CPU.
- **`process_butterworth`** (`ndm_test_butterworth`, a new test of the plugin
  itself: CPU double equals scipy `sosfiltfilt`, and output is independent of
  chunking):
  - the Metal kernel is `bwFilterBlock<float>` in MSL, single precision only;
    `-p double` runs on the CPU;
  - it is within 1 LSB of the CPU's single precision.
- **`process_spikegrouper`** (`ndm_test_spikegrouper_metal`):
  - noise medians (exact histogram) and coincidence counts (lower_bound per
    event) run on the GPU, detection stays on the CPU;
  - integer results, so the groups equal the CPU's.
- **`process_decomposecollisions`: not ported.** Its CUDA path's GPU work is
  two GEMMs:
  - in the screening GEMM, only one column per row is used: one dot product
    per spike, which the CPU path also computes, in O(L);
  - the fitting GEMM's result is never read.

  The decomposition itself is CPU code in both paths, so a Metal port would
  accelerate nothing. The CUDA path could drop both GEMMs (a separate
  clean-up).

Checked without a Mac:
- every host file with clang `-fsyntax-only` against the pinned metal-cpp
  headers;
- every kernel compiled as C++ through a shim (`mslemu`) and run thread by
  thread against the CPU rule, with zero mismatches.

## 12. Phase 3 as implemented (2026-10-21)

- **Realign cross-correlation** (`klustersshared_xcorr_metal_test`):
  - the numerators sum int16 × int16 products, which are exact in 64-bit
    integers, so the kernel needs no floating point at all: one thread per
    (spike, lag), plus one for each spike's energy;
  - the host then runs `xcorr_omp_compute`'s own scoring on those sums, so
    the result is **bit-identical** to the OpenMP back end, near-ties
    included;
  - it has no block-size limit on `maxShift`;
  - `XcorrDispatch` probes CUDA → HIP → SYCL → Metal → OpenMP;
  - Klusters builds it in; KiloKlustaKwik's build will in Phase 4.
- **Grouping Assistant error matrix**, FP32, Klusters' default
  (`klusters_test_groupingassistant_metal`):
  - the three CUDA FP32 kernels (Mahalanobis log-posterior, stabilised
    softmax, per-cluster mean) are written in MSL;
  - spikes are processed in chunks of positions, so it never needs the full
    posterior matrix;
  - it is within 2e-3 of an FP64 reference;
  - the FP64 option and the full posteriors stay on the CPU.

  The dispatcher now tells the caller which GPU routes a back end has.
  HIP/SYCL builds no longer try the CUDA-only aggregation and print a failure
  on every matrix.
- **PCA refine: not ported.**
  - The CUDA kernel's stderiv is fixed (all-pairs, zero seed). The CPU loop
    uses the `.spk`'s own transform, so for stderiv spaces CUDA and CPU
    picked different shifts.
  - This never showed, because refine could not find the raw file and never
    ran.
  - The GPU path is now restricted to the raw feature space. Porting that
    remainder to Metal would help few sessions (the reference sessions are
    stderiv). The OpenMP CPU loop serves Macs.
- **Found while porting** (a separate fix, not Metal): Klusters realign
  wrote circularly rolled windows for every `.pending` or method-tagged
  `.spk`. It now re-cuts from `rawRecordingPath()`, or refuses. See the
  CHANGELOG.

## 13. Phase 4 as implemented (2026-10-21)

- **One macro.** KiloKlustaKwik tested `USE_CUDA || USE_SYCL || USE_HIP` at
  39 sites; `KK_HAS_GPU` (KK.h) replaces them, and Metal is added there.
- **A macOS `KiloKlustaKwik`** is built beside `KiloKlustaKwik_cpu` when the
  monorepo has `Neurosuite::metal`. It includes the realign cross-correlation
  of Phase 3, so template matching uses it.
- **CEM steps** (`KK_metal.{h,cpp,metal}`, `kk_test_metal_cem`), with the
  `gpu_*` entry points and data layouts of the CUDA backend:
  - E-step: one thread per point, the CPU path's formula, KK_cuda's
    DistThresh skip; clusters split over command buffers (watchdog).
  - M-step without atomics: the host counting-sorts the points by class and
    cuts each cluster into tiles of 4096; the GPU sums each tile; the host
    adds the tiles in order, in double. Runs are deterministic, unlike CUDA,
    and the sums are more accurate than the CPU's float accumulator.
  - C-step as `KK::CStep`; score and deletion loss summed on the host in
    double.
  - Shared buffers, so uploads are memcpy. `allocate()` refuses a session
    that does not fit (buffers, nDims > 64, a kernel that does not compile)
    and KiloKlustaKwik stays on the CPU.
- **Found while porting** (a separate fix, all GPU backends):
  `CEMTwoPhase`'s spatial phase lowers `nDims` by one, but the CUDA, HIP and
  SYCL E-steps used the dimensions they were allocated with, so that phase's
  distances were garbage. `gpu_estep` now takes KK's `nDims`/`nDims2`. See
  the CHANGELOG.
- **Shift probe: nothing to port.** It is disabled in every build
  (`shiftprobe_disabled.cpp`); the CUDA/HIP/SYCL files are not compiled.
- **Fiber mean-shift** stays on the CPU (class C), as planned.

Checked without a Mac:
- host code and test with clang `-fsyntax-only -Wall -Wextra -Wshadow`
  against the pinned metal-cpp;
- the kernels compiled as C++ and run behind a CPU stand-in for metal-cpp,
  with each threadgroup's threads as coroutines that switch at every barrier
  (a divergent barrier fails): `kk_test_metal_cem` passes at widths 64, 96,
  256 and 1024, and fails when a kernel is broken on purpose;
- the whole KiloKlustaKwik built against that stand-in, on a synthetic
  session (20000 spikes, 12 Gaussian clusters, 12 features + time,
  `-ChunkMinutes 0`), against `KiloKlustaKwik_cpu`:

  | clusters tried | CPU: clusters, ARI vs truth | Metal: clusters, ARI vs truth | ARI CPU vs Metal |
  |---|---|---|---|
  | 2–30 | 12, 1.000 | 12, 1.000 | 1.000 |
  | 10–16 | 5, 0.345 | 12, 1.000 | 0.345 |
  | 12–14 | 6, 0.427 | 12, 1.000 | 0.427 |

  Float E- and M-steps take a different trajectory from the CPU's, so the
  clusterings differ; §6's bar (no further from the CPU than CUDA is, on the
  reference sessions) still needs a Mac.
