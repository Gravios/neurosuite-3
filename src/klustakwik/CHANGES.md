# KlustaKwik — Changes from v1.7

## Overview

This fork modernises the v1.7 codebase (last updated June 2004) to compile under
C++17, adds algorithmic improvements for speed and accuracy on modern multi-hour
recordings, and introduces a CUDA backend for GPU-accelerated sorting.
The `.clu` output format and all file conventions are unchanged.

---

## Correctness fixes

### `ConsiderDeletion`: uninitialised `candidateClass`

The original code used `candidateClass` without initialisation, causing undefined
behaviour on the deletion path. Initialised to `-1` with an early-exit guard
before use.

### `Cholesky` / `TriSolve`: heap allocation in inner loop

Both functions allocated temporary `Array<float>` objects on every call —
two heap allocations per cluster per E-step. Rewritten to operate on raw `float*`
directly. No semantic change; eliminates `nClusters x nIter x 2` allocations
per run.

---

## C++17 modernisation

### Source portability

`<fstream.h>` and `<iostream.h>` (pre-standard headers removed in GCC 3.x)
replaced with their standard equivalents. All translation units now compile
cleanly under `-std=c++17 -Wall -Wextra`.

### `Array.h`

| Issue | Fix |
|---|---|
| Missing `#include <iostream>` despite using `cerr` | Added |
| No move constructor/assignment | Added — eliminates copies when returning `Array` from functions |
| `operator[]` const overload returned non-const `T&` | Fixed |
| `Array2` used pointer-to-pointer heap layout | Replaced with single flat allocation (row-major, cache-friendly) |
| `abort()` on bounds violation | Changed to `std::runtime_error` (catchable) |
| `SetSize` left allocation uninitialised | Now zero-initialises via `new T[n]()` |

### `Error()`, `snprintf`, `[[noreturn]]`

All `sprintf` calls with `STRLEN`-sized buffers replaced with `snprintf`.
`Error()` marked `[[noreturn]]` to enable correct compiler flow analysis.

### Build system

`CMakeLists.txt` replaces the original `makefile`. Supports CPU-only and CUDA
builds from the same file; OpenMP detected automatically via `find_package(OpenMP)`.

---

## New algorithm: farthest-point seeding

**Parameter:** `-InitMethod farthest` (default)

Replaces random cluster initialisation. Seeds are chosen periphery-inward:

1. Centre 0 is the global centroid of the data.
2. Each subsequent centre is the data point with the largest minimum Mahalanobis
   distance from the current set.

This places seeds at the natural spatial boundaries between clusters rather than
allowing multiple seeds to compete for the same dense region. In practice this
reduces the number of EM iterations required to converge by 60-75% on typical
tetrode data. The original random initialisation is still available with
`-InitMethod random`.

---

## New algorithm: two-phase CEM

**Parameter:** `-TimeMergeIter N` (default 30; set 0 to disable)

The CEM run is split into two phases:

**Phase 1** runs with the time dimension excluded. Cluster geometry in PCA space
alone determines assignments. Farthest-point seeding operates in this reduced
space.

**Phase 2** reintroduces the time dimension and runs `TimeMergeIter` additional
iterations at full dimensionality. Units active in different time epochs may be
distinguished; units that are spatially identical but temporally non-overlapping
can be separated or merged appropriately.

---

## New algorithm: three-phase temporal chunking

**Parameters:** `-ChunkMinutes`, `-SamplingRate`, `-MergeThresh`, `-GlobalMergeIter`

Designed for long recordings (hours) where electrode drift causes a unit's
waveform to shift progressively through PCA space. A single global Gaussian must
then span the entire drift trajectory, inflating covariance and reducing separation
from neighbouring units.

### Phase 0 — temporal partition

The session is divided into windows of `ChunkMinutes` minutes. Window boundaries
are computed from the raw sample timestamps captured during `LoadData`.
`-SamplingRate` must match the actual recording rate; the default of 20000 Hz will
produce wrong boundaries if your hardware differs. Any window below the minimum
spike count (`nStartingClusters x nSpatialDims x 3`) is merged into its
predecessor.

### Phase 1 — per-chunk two-phase CEM (parallel)

Each chunk runs an independent two-phase CEM in a self-contained `KK` sub-object.
Chunks are processed in parallel using OpenMP (`schedule(dynamic)` accounts for
variable chunk sizes). Each sub-object owns its own Cholesky storage and does not
interact with the global `kSv` state, making the parallel execution data-race-free.

Per-chunk cluster IDs are packed as `chunkIdx x MaxPossibleClusters + localClusterId`
to form globally unique keys for the Phase 2 lookup table.

### Phase 2 — cross-chunk model matching

For each pair of adjacent chunks `(k, k+1)`, every cluster from chunk `k` is
compared against every cluster from chunk `k+1` using the symmetric Mahalanobis
distance in the spatial dimensions (time excluded):

```
d_sym(A, B) = 0.5 * [mahal(mean_A, cov_B) + mahal(mean_B, cov_A)]
```

Only immediately adjacent chunk boundaries are evaluated; clusters from
non-neighbouring chunks are never compared directly. Longer-range connections
arise through Union-Find transitivity: A-B and B-C edges imply A and C are in
the same global component without a direct A-C comparison.

Noise clusters (`localClusterId == 0`) from all chunks are unconditionally merged
into a single global noise component before any Mahalanobis comparisons. The merge
threshold has no effect on noise.

If the merge produces more global clusters than `MaxPossibleClusters`, the run
falls back to two-phase CEM on the full session with a diagnostic message.

### Phase 3 — global warm-start EM

The full dataset is reassembled. `Class[]` is seeded from the globally matched
labels and a short full-dimensional EM pass (at most `GlobalMergeIter` iterations)
refines the models. A drifting unit now fits a tilted ellipse in PCA+time space
rather than a bloated spherical approximation. In practice this converges in
1-5 iterations on real recordings.

---

## New parameter: `-SaveIntermediates`

**Default: `1`** (original behaviour)

When `1`, the `.clu` file is written to disk every time the outer cluster-count
loop finds a new best score — up to `MaxClusters - MinClusters + 1` writes per
electrode. This provides partial results if the run is interrupted.

When `0`, all mid-run `.clu` writes are suppressed. The in-memory `BestClass[]`
array is always kept current; a single write occurs at the end. Recommended on
network storage where repeated `open`/`close` round-trips are expensive.

---

## Logging defaults changed

`Screen`, `Log`, and `Verbose` now default to `0`. By default the binary runs
completely silently and produces no `.klg` log file. All `std::cout` calls that
previously bypassed the `Screen`/`Log` flags have been routed through `Output()`.

To restore verbose behaviour:

```bash
KlustaKwik session 1 -Screen 1 -Log 1 -Verbose 1
```

---

## Startup banner and parallelism diagnostics

An unconditional summary is printed to **stderr** immediately after data loads,
before any CEM work begins. This gives confirmation that the binary is running,
data was read successfully, and parallelism is (or is not) active — regardless
of the `-Screen`/`-Log` settings.

The banner also detects and reports parallelism problems explicitly rather than
failing silently:

```
# Normal — all cores available
KlustaKwik  session.fet.1
  344094 spikes, 25 dims, clusters 2-50
  mode: chunked  chunk=5.0 min  SR=32572  ~16 chunks
  parallel: 16 OpenMP threads

# OMP_NUM_THREADS is limiting thread count
  parallel: 1 of 16 cores  (OMP_NUM_THREADS=1 limits parallelism —
                             unset it or set to 16 to use all cores)

# Built without -fopenmp
  WARNING: built without OpenMP — chunks run serially.
           Recompile with -fopenmp to enable parallelism.
```

A `K=N/M start=S/T` progress line updates in-place during the outer loop so the
current cluster count is always visible in the terminal without enabling full
logging.

### OpenMP thread count under WSL2

A common configuration issue on Windows Subsystem for Linux: WSL2 can be
restricted to fewer cores than the Windows host has, causing `omp_get_num_procs()`
to return 1 even on a 16-core machine. If the banner reports fewer threads than
expected, check:

```bash
nproc                                    # cores WSL2 is reporting
echo "OMP_NUM_THREADS=${OMP_NUM_THREADS}" # if set to 1, unset it
ldd KlustaKwik | grep -i omp             # confirms OpenMP is linked
```

If `nproc` itself is 1, the WSL2 VM is restricted. Create or edit
`C:\Users\<Username>\.wslconfig` on the Windows host:

```ini
[wsl2]
processors=16    ; match to the host physical core count
```

Then run `wsl --shutdown` from PowerShell and reopen the terminal.

---

## CUDA backend

A CUDA implementation of the three inner-loop kernels (E-step, M-step, C-step)
is provided in `KK_cuda.cu`. Enabled by building with `-DUSE_CUDA=ON`.

Data layout is transposed at upload time for coalesced memory access:

```
CPU layout:  Data[point * nDims + dim]    ->  GPU: Data[dim * nPoints + point]
CPU layout:  LogP[point * nClusters + c]  ->  GPU: LogP[c * nPoints + point]
```

Expected speedup on a mid-range GPU (RTX 4070 class) over a 16-core CPU:
approximately 20x per CEM iteration for 500k spikes, 17 features, 10 clusters.
The Cholesky decomposition remains on the CPU (O(D^3) with D <= 17 is ~1700
operations — not worth GPU dispatch overhead).

---

## Phase 1.5: waveform realignment

**Requires:** chunked CEM mode (`-ChunkMinutes > 0`)

After Phase 1 per-chunk EM has assigned stable cluster labels, each spike's
waveform is realigned to its chunk-cluster mean using normalised circular
cross-correlation across all channels simultaneously. The aligned waveforms
are written back to the `.spk` file in-place before Phase 2 model matching.

Alignment uses the shared `XcorrDispatch` kernel from `src/shared/xcorr/`,
which selects the best available compute backend at runtime (CUDA → HIP → SYCL
→ OpenMP CPU). The sign convention matches the Klusters interactive realignment:
a positive lag `τ` means the spike peak is late by `τ` samples relative to the
template; the waveform is rolled forward by `aligned[s] = original[(s+τ) % N]`.

Overlap spikes (which appear in two adjacent chunks due to `ChunkOverlapMinutes`)
are deferred: chunk `k` skips the writeback for any spike that also belongs to
chunk `k+1`, so that chunk `k+1` can compute a clean, unbiased mean before
writing. This prevents double-alignment artifacts at chunk boundaries.

The realignment is enabled automatically when `NbChannels > 0 && NbSamplesPerSpike > 0`.
Both are populated by YAML auto-detect (see below); no explicit command-line
flags are required.

---

## YAML auto-detect for spike-group parameters

`KlustaKwikYaml.cpp` reads three parameters directly from `<FileBase>.yaml`
(or `.yml`) at startup, removing the need for the calling script to pass them
explicitly:

- `NbChannels` — from `spikeDetection.channelGroups[ElecNo-1].channels.channel` count
- `NbSamplesPerSpike` — from `spikeDetection.channelGroups[ElecNo-1].nSamples`
- `SamplingRate` — from `acquisitionSystem.samplingRate`

Command-line values always override YAML. Startup messages of the form
`KlustaKwik: NbChannels=8  (from YAML, group 1)` confirm what was detected.

`NbBytesPerSample` is **not** read from YAML; the ndmanager pipeline always
produces `int16` `.spk` files regardless of ADC bit depth, so the default of 2
is always correct unless you are using a custom 32-bit extractor.

---

## Thread safety: Cholesky storage moved to KK instance

`pChol` and `pBestChol` were originally pointers in the `KlustaSave` global
singleton. Every `KK` instance's `AllocateCholeskyVecs()` wrote to this global,
and every `EStep()` read from it, making parallel execution across chunk
sub-objects a data race.

Both pointers are now per-instance members of `KK`. Each chunk sub-object
allocates and owns its own Cholesky storage. The global `kSv` struct no longer
participates in the Cholesky path.

A `suppressBestSave` flag on each sub-object prevents chunk-level `SaveBestMeans()`
calls from writing into the outer loop's `kSv.BestScoreSave`, which would corrupt
the global best-score tracking during parallel execution.

---

## File inventory

| File | Status | Description |
|---|---|---|
| `KlustaKwik.cpp` | Modified | Main entry point, parameter handling, outer loop, startup banner |
| `KlustaKwik.h` | Modified | Extern declarations for all globals |
| `KlustaKwikYaml.cpp` | New | Reads `NbChannels`, `NbSamplesPerSpike`, `SamplingRate` from YAML |
| `KlustaKwikYaml.h` | New | `KKYamlSpikeParams` struct and `kkReadYamlSpikeParams()` declaration |
| `KK.cpp` | Modified | EM engine: all algorithm changes live here |
| `KK.h` | Modified | KK class definition; per-instance Cholesky, `suppressBestSave`, `RealignChunkWaveforms` |
| `KlustaSave.h` | Modified | Removed `pChol`/`pBestChol` (moved to `KK` for thread safety) |
| `Array.h` | Modified | See C++17 modernisation above |
| `param.c` / `param.h` | Unchanged | Parameter parsing (original) |
| `CMakeLists.txt` | New | Replaces `makefile`; CPU/CUDA/HIP/SYCL targets; yaml-cpp wiring |
| `KK_cuda.cu` | New | CUDA kernels for E/M/C-step |
| `KK_cuda.h` | New | Host-callable CUDA wrapper declarations |
| `KK_hip.cpp` | New | HIP (AMD ROCm) kernels for E/M/C-step |
| `KK_hip.h` | New | Host-callable HIP wrapper declarations |
| `KK_sycl.cpp` | New | SYCL (Intel oneAPI) kernels for E/M/C-step |
| `KK_sycl.h` | New | Host-callable SYCL wrapper declarations |
| `README.md` | New | User-facing documentation |
| `CHANGES.md` | New | This file |
| `ReleaseNotes.txt` | Retained | Original v1.5-v1.7 release notes |
| `../shared/xcorr/` | New | Shared normalised cross-correlation library (OMP/CUDA/HIP/SYCL) |

