# klustakwik — Automatic Spike Sorter

KiloKlustaKwik performs automatic spike sorting via Classification EM (CEM). It reads a `.fet` feature file produced by `ndm_pca` and writes a `.clu` cluster assignment file. GPU acceleration is available for the E-step distance computations via CUDA (NVIDIA), HIP (AMD ROCm), or SYCL (Intel Arc/oneAPI).

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| C++17 compiler | Build | Yes |
| CMake ≥ 3.21 | Build | Yes |
| OpenMP | Parallel chunk processing and CPU fallback | Strongly recommended |
| CUDA Toolkit ≥ 11 | NVIDIA GPU acceleration | Optional |
| ROCm / HIP SDK ≥ 5.0 | AMD GPU acceleration | Optional |
| Intel oneAPI Base Toolkit ≥ 2023.1 | Intel Arc/Xe GPU acceleration | Optional |

When none of the GPU backend flags are explicitly set, CMake auto-detects in priority order CUDA > HIP > SYCL. Every GPU build also produces `KiloKlustaKwik_cpu` as a CPU-only fallback binary.

---

## Usage

```
KiloKlustaKwik FileBase ElecNo [options]
```

`FileBase` and `ElecNo` together identify the input file `FileBase.fet.ElecNo`.

### Examples

```bash
# Standard two-phase farthest-point mode
KiloKlustaKwik session 1 -MinClusters 2 -MaxClusters 12

# Three-phase temporal chunking for long recordings with electrode drift
KiloKlustaKwik session 1 -MinClusters 2 -MaxClusters 12 \
    -ChunkMinutes 5 -SamplingRate 32572

# Resume from an existing sort
KiloKlustaKwik session 1 -StartCluFile session.clu.1

# 80-minute silicon probe, chunked mode, all features, suppress intermediate saves
KiloKlustaKwik jg05-20120316 7 \
    -MinClusters 2 -MaxClusters 20 \
    -UseFeatures 1111111111111111111111111 \
    -ChunkMinutes 5 -SamplingRate 32572 \
    -MergeThresh 30 -GlobalMergeIter 20 -TimeMergeIter 30 \
    -SaveIntermediates 0
```

### Output files

| File | Contents |
|---|---|
| `FileBase.clu.ElecNo` | Cluster assignment per spike (1-based; cluster 1 = noise/unsorted) |
| `FileBase.klg.ElecNo` | Run log (only when `-Log 1`) |
| `FileBase.model.ElecNo` | Gaussian model parameters (omit with `-fSaveModel 0`) |

---

## Parameters

### Clustering control

| Parameter | Default | Description |
|---|---|---|
| `-MinClusters N` | `2` | Minimum number of clusters to try |
| `-MaxClusters N` | `12` | Maximum number of clusters to try |
| `-MaxPossibleClusters N` | `1000` | Hard upper bound on cluster count |
| `-nStarts N` | `3` | Number of random restarts |
| `-RandomSeed N` | `1` | Random seed (use `0` to seed from time) |
| `-UseFeatures BITS` | all `1`s | Binary mask over feature dimensions; `0` skips that dimension |
| `-PenaltyMix F` | `1.0` | Blend between BIC (`0`) and CEM (`1`) penalty |

### Convergence

| Parameter | Default | Description |
|---|---|---|
| `-MaxIter N` | `500` | Maximum EM iterations per run |
| `-FullStepEvery N` | `20` | Run a full E-step every N iterations (mini-batch otherwise) |
| `-ChangedThresh F` | `0.001` | Stop if fewer than this fraction of spikes change class |

### Two-phase CEM (default when `ChunkMinutes` is not set)

KiloKlustaKwik uses a two-phase algorithm by default:

- **Phase 1** — spatial-only EM using all feature dimensions except the last (time). Centres are seeded with the farthest-point heuristic (`InitCentresFarthestPoint`), which gives better initial separation than random assignment.
- **Phase 2** — short merge pass (`TimeMergeIter` iterations) that reintroduces the time dimension. This allows temporally drifting clusters to be identified without allowing time to dominate the spatial clustering phase.

| Parameter | Default | Description |
|---|---|---|
| `-TimeMergeIter N` | `10` | Phase 2 merge iterations (set `0` to disable two-phase mode) |

### Three-phase chunked CEM

For long recordings (> ~30 min) where electrode drift causes a cluster to appear as multiple clusters at different time points, the three-phase chunked mode splits the recording into temporal windows, sorts each independently in parallel (OpenMP), and then merges the per-chunk models globally.

| Parameter | Default | Description |
|---|---|---|
| `-ChunkMinutes F` | `0` (disabled) | Chunk duration in minutes; activates chunked mode when > 0 |
| `-SamplingRate F` | — | Required when `ChunkMinutes > 0`; used to convert minutes to sample counts |
| `-MergeThresh F` | `30.0` | Maximum inter-chunk Mahalanobis distance for merging two chunk models |
| `-GlobalMergeIter N` | `10` | EM iterations run on the globally merged solution |

### Output

| Parameter | Default | Description |
|---|---|---|
| `-SaveIntermediates N` | `1` | Write `.clu` whenever a new best score is found. Set `0` to suppress mid-run writes and produce only a final file. |
| `-Log N` | `0` | Write `.klg` log file |
| `-Screen N` | `1` | Print progress to stdout |
| `-Verbose N` | `0` | Verbose per-iteration output |
| `-fSaveModel N` | `1` | Write `.model` file with Gaussian parameters |

### Seeding

| Parameter | Default | Description |
|---|---|---|
| `-StartCluFile PATH` | — | Initialise from an existing `.clu` file |
| `-InitMethod STR` | `"random"` | Initialisation method (`"random"` or `"farthest"`) |

---

## Algorithm phases in detail

### Classification EM (CEM)

CEM extends standard EM by adding a hard classification step (C-step) after each E-step. The penalty term penalises models with too many clusters, providing an automatic model-order selection mechanism that avoids the need to specify the exact number of clusters.

The inner loop runs M-step → E-step → C-step → `ConsiderDeletion` until convergence. `ConsiderDeletion` removes clusters that contribute too little to the score, respecting the `MinClusters` floor set by the user.

`TrySplits()` is called every `SplitEvery` iterations to attempt splitting poorly-fitting clusters. Split candidates are evaluated by running a mini-CEM on just the spikes of the candidate cluster; the split is kept if it improves the score.

### GPU dispatch

KiloKlustaKwik's computationally intensive steps (E-step, M-step, C-step, `ConsiderDeletion`) are GPU-parallelisable. The `KK.h` dispatch layer maps generic `gpu_*` function calls to the active backend at compile time:

| Backend | Compile flag | Priority |
|---|---|---|
| CUDA | `USE_CUDA` | 1st |
| SYCL (Intel Arc) | `USE_SYCL` | 2nd |
| HIP (AMD ROCm) | `USE_HIP` | 3rd |
| OpenMP (CPU) | always built | fallback |

GPU data layout uses a transposed (dimension-major) format for coalesced memory access: `Data[d * nPoints + p]` instead of the CPU's point-major `Data[p * nDims + d]`.

Chunk sub-objects in chunked CEM always run on the CPU (the main sort object holds the GPU context; chunk objects do not), so chunked mode benefits from OpenMP across chunks rather than from the GPU directly.

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Linux (Ubuntu / Debian) | AMD ROCm / HIP | [install/linux-hip.md](install/linux-hip.md) |
| Linux (Ubuntu / Debian) | Intel Arc / SYCL | [install/linux-sycl.md](install/linux-sycl.md) |
| Linux — WSL2 | Intel Arc / SYCL | [install/wsl2-sycl.md](install/wsl2-sycl.md) |
| Windows (native) | CPU / OpenMP | [install/windows-cpu.md](install/windows-cpu.md) |
| Windows (native) | NVIDIA CUDA | [install/windows-cuda.md](install/windows-cuda.md) |
| Windows (native) | AMD HIP | [install/windows-hip.md](install/windows-hip.md) |
| Windows (native) | Intel Arc / SYCL | [install/windows-sycl.md](install/windows-sycl.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |
