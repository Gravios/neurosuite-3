# klustakwik — Automatic Spike Sorter

KlustaKwik performs automatic spike sorting via Classification EM (CEM). It reads a `.fet` feature file produced by `ndm_pca` and writes a `.clu` cluster assignment file. GPU acceleration is available for the E-step distance computations via CUDA (NVIDIA), HIP (AMD ROCm), or SYCL (Intel Arc/oneAPI).

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| C++17 compiler | Build | Yes |
| CMake ≥ 3.21 | Build | Yes |
| OpenMP | Parallel chunk processing | Strongly recommended |
| CUDA Toolkit ≥ 11 | NVIDIA GPU acceleration | Optional |
| ROCm / HIP SDK ≥ 5.0 | AMD GPU acceleration | Optional |
| Intel oneAPI Base Toolkit ≥ 2023.1 | Intel Arc/Xe GPU acceleration | Optional |

When none of the GPU backend flags are explicitly set, CMake auto-detects in priority order CUDA > HIP > SYCL. Every GPU build also produces `KlustaKwik_cpu` as a CPU-only fallback binary.

---

## Usage

```
KlustaKwik FileBase ElecNo [options]
```

`FileBase` and `ElecNo` together identify the input file `FileBase.fet.ElecNo`.

### Minimal examples

```bash
# Two-phase farthest-point mode (default)
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12

# Three-phase temporal chunking for long recordings with electrode drift
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 \
    -ChunkMinutes 5 -SamplingRate 32572

# Re-run from an existing sort
KlustaKwik session 1 -StartCluFile session.clu.1

# 80-minute silicon probe recording, chunked mode, all features
KlustaKwik jg05-20120316 7 \
    -MinClusters 2 -MaxClusters 20 \
    -UseFeatures 1111111111111111111111111 \
    -ChunkMinutes 5 -SamplingRate 32572 \
    -MergeThresh 30 -GlobalMergeIter 20 -TimeMergeIter 30 \
    -SaveIntermediates 0
```

### Output files

| File | Contents |
|---|---|
| `FileBase.clu.ElecNo` | Cluster assignment per spike (1-based; label 1 = noise) |
| `FileBase.klg.ElecNo` | Run log (only when `-Log 1`) |
| `FileBase.model.ElecNo` | Gaussian model parameters (omit with `-fSaveModel 0`) |

---

## Parameters

### Cluster range

| Parameter | Default | Description |
|---|---|---|
| `MinClusters` | `2` | Minimum number of starting clusters |
| `MaxClusters` | `10` | Maximum number of starting clusters |
| `MaxPossibleClusters` | `100` | Hard ceiling on cluster count |
| `nStarts` | `1` | Random restarts per starting cluster count |

### Algorithm selection

| Parameter | Default | Description |
|---|---|---|
| `InitMethod` | `farthest` | Seed strategy: `farthest` or `random` |
| `TimeMergeIter` | `30` | Iterations for Phase 2 time-merge pass; `0` to skip |
| `ChunkMinutes` | `0` | Enables three-phase temporal chunking when > 0 |
| `SamplingRate` | `20000` | Samples per second — **must match the actual recording rate** when `ChunkMinutes > 0` |

### Temporal chunking

Only relevant when `ChunkMinutes > 0`.

| Parameter | Default | Description |
|---|---|---|
| `MergeThresh` | `30` | Symmetric Mahalanobis² threshold for matching clusters across chunks |
| `GlobalMergeIter` | `20` | Maximum EM iterations for Phase 3 global warm-start pass |

### EM convergence

| Parameter | Default | Description |
|---|---|---|
| `MaxIter` | `500` | Maximum EM iterations per CEM run |
| `ChangedThresh` | `0.05` | Convergence: fraction of reassigned points to continue |
| `SplitEvery` | `50` | Attempt splits every N iterations |
| `PenaltyMix` | `0` | Blends BIC (`0`) and AIC (`1`) penalty |

### Input / output

| Parameter | Default | Description |
|---|---|---|
| `UseFeatures` | `11111111111100001` | Binary mask selecting feature columns. `all` selects every column |
| `StartCluFile` | _(none)_ | Path to an existing `.clu` file to use as initial assignment |
| `SaveIntermediates` | `1` | Write `.clu` on every new best score; set `0` on network storage |

### Logging (all silent by default)

| Parameter | Default | Description |
|---|---|---|
| `Screen` | `0` | Print per-iteration progress to stdout |
| `Log` | `0` | Write per-iteration progress to `.klg` file |
| `Verbose` | `0` | Print full parameter table at startup (requires Screen or Log) |

Parameter names are case-sensitive. Unrecognised names are silently ignored — run with `-Screen 1 -Verbose 1` to confirm a setting was accepted.

---

## Algorithm overview

**Two-phase mode (default):** Phase 1 clusters using only PCA dimensions with farthest-point seeding. Phase 2 reintroduces the timestamp dimension for `TimeMergeIter` additional iterations, allowing units active in different epochs to be distinguished.

**Three-phase chunking (`ChunkMinutes > 0`):** Phase 0 partitions the session into windows. Phase 1 runs independent per-chunk CEM in parallel (OpenMP). Phase 2 matches clusters across adjacent chunk boundaries using symmetric Mahalanobis distance in a Union-Find structure. Phase 3 runs a short global warm-start EM pass on the full dataset with matched labels. Designed for multi-hour recordings where electrode drift makes a single global Gaussian a poor fit.

---

## Startup banner

The binary always prints a brief summary to stderr before the first CEM call:

```
KlustaKwik  jg05-20120316.fet.7
  344094 spikes, 25 dims, clusters 2-50
  mode: chunked  chunk=5.0 min  SR=32572  ~16 chunks
  parallel: 16 OpenMP threads
  K=2/50 start=1/1
```

---

## Troubleshooting

### Runs on a single core despite `-ChunkMinutes`

Check the banner. Common causes:

**`OMP_NUM_THREADS=1` is set:**
```bash
unset OMP_NUM_THREADS          # or:
export OMP_NUM_THREADS=$(nproc)
```

**WSL2 CPU allocation too small** — on Windows host, edit `C:\Users\<user>\.wslconfig`:
```ini
[wsl2]
processors=16
```
Then `wsl --shutdown` and restart.

**Built without OpenMP** — check `ldd KlustaKwik | grep -i omp` and rebuild with `-fopenmp`.

### `MergeChunkModels produced N global clusters >= MaxPossibleClusters`

`MergeThresh` is too small. Try 50–100, or raise `MaxPossibleClusters`.

### Runtime much longer than expected

`MaxClusters=50` with many chunks is heavy. Try a first pass with `MaxClusters=20` to find the score plateau.

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Linux (Ubuntu / Debian) | AMD ROCm / HIP | [install/linux-hip.md](install/linux-hip.md) |
| Linux (Ubuntu / Debian) | Intel Arc / SYCL (bare metal) | [install/linux-sycl.md](install/linux-sycl.md) |
| Linux — WSL2 | Intel Arc / SYCL | [install/wsl2-sycl.md](install/wsl2-sycl.md) |
| Windows (native) | CPU / OpenMP | included in CUDA guide |
| Windows (native) | NVIDIA CUDA | [install/windows-cuda.md](install/windows-cuda.md) |
| Windows (native) | AMD HIP | [install/windows-hip.md](install/windows-hip.md) |
| Windows (native) | Intel Arc / SYCL | [install/windows-sycl.md](install/windows-sycl.md) |
| macOS | CPU / OpenMP only (no GPU support) | [install/macos.md](install/macos.md) |
