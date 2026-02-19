# KlustaKwik — Automatic Spike Sorting

KlustaKwik is a command-line spike-sorting program that clusters spike waveform features using an expectation-maximisation (EM) algorithm fitting a mixture of Gaussians with a Bayesian complexity penalty. It takes a `.fet.N` feature file as input and writes a `.clu.N` cluster assignment file.

An optional CUDA backend offloads the three compute-intensive EM steps (E-step, M-step, C-step) to the GPU, giving approximately 20× speedup on current hardware.

---

## Usage

```bash
# Basic — let KlustaKwik search between 2 and 20 clusters
KlustaKwik session 1

# Fix the random seed for reproducible results
KlustaKwik session 1 -MinClusters 2 -MaxClusters 20 -RandomSeed 42

# Process all electrode groups in a loop
for group in $(seq 1 12); do
    KlustaKwik session $group -MinClusters 2 -MaxClusters 20 -RandomSeed 42
done
```

**Arguments:** `KlustaKwik <FileBase> <ElectrodeNumber> [options]`

`FileBase` is the session base name (without extension). `ElectrodeNumber` is the electrode group index (matches the `.N` suffix on the `.fet` and `.clu` files).

---

## Options

| Option | Default | Description |
|---|---|---|
| `-MinClusters N` | 2 | Minimum number of clusters to try |
| `-MaxClusters N` | 20 | Maximum number of clusters to try |
| `-nStarts N` | 3 | Number of random restarts per cluster count |
| `-RandomSeed N` | time-based | RNG seed; set for reproducible output |
| `-MaxIter N` | 10000 | Maximum EM iterations per run |
| `-PenaltyMix f` | 1.0 | Weight of the Bayesian complexity penalty |
| `-UseFeatures str` | all 1s | Binary string selecting which feature columns to use (e.g. `11110001`) |

---

## Input and output files

| File | Direction | Content |
|---|---|---|
| `session.fet.N` | Read | PCA feature vectors (produced by `ndm_pca`) |
| `session.clu.N` | Written | Cluster assignments, one integer per spike |
| `session.model.N` | Read (optional) | Starting model from a previous run |
| `session.model.N` | Written | Final fitted model (can seed a subsequent run) |

The `.fet.N` format is plain text: first line is the number of features per spike; subsequent lines are space-separated feature integers with the spike timestamp (sample index) as the last column.

The `.clu.N` format is plain text: first line is the number of clusters (including noise cluster 1); subsequent lines are cluster IDs in the same order as the `.fet.N` file.

---

## Integration with klusters

KlustaKwik is the default reclustering executable configured in klusters (**Settings → Preferences → Reclustering executable**). When triggered from klusters with `Shift+R`, klusters writes a temporary `.fet` file for the selected clusters, launches `KlustaKwik` as a child process with the configured arguments, and reads back the resulting `.clu` assignments when it finishes. See [doc/klusters.md](klusters.md) for the full reclustering workflow.

---

## Verifying output

A test dataset is included in `klustakwik/test/`:

```bash
cd klustakwik/test
KlustaKwik test 1 -MinClusters 2 -MaxClusters 5 -RandomSeed 1
diff test.clu.1 test_res.clu.1   # should be identical
```

With the same random seed, output is bit-identical to KlustaKwik v1.7 on the same data.

---

## GPU acceleration

When built with CUDA, KlustaKwik offloads the three inner-loop EM steps to the GPU:

- **E-step** — per-spike Mahalanobis distance computation for all clusters
- **M-step** — mean and covariance accumulation
- **C-step** — cluster assignment update

Typical performance on a 500 k spike, 17-feature, 10-cluster problem:

| Step | Ryzen 7 9800X3D | RTX 5070 Ti | Speedup |
|---|---|---|---|
| E-step | ~80 ms/iter | ~2 ms/iter | ~40× |
| M-step | ~15 ms/iter | ~1 ms/iter | ~15× |
| C-step | ~5 ms/iter | ~0.3 ms/iter | ~15× |
| **Total per iteration** | **~100 ms** | **~5 ms** | **~20×** |

CUDA architecture targets: sm_86 (Ampere), sm_89 (Ada Lovelace), sm_100 (Hopper), sm_120 (Blackwell). Building for sm_120 requires CUDA ≥ 12.8.

---

## C++17 modernisation notes

This fork modernises KlustaKwik v1.7 to C++17 with no algorithmic changes — output is bit-identical to v1.7 with the same random seed:

- `Array2` backing storage rewritten from pointer-to-pointer heap to a single flat row-major allocation (cache-friendly, eliminates N+1 heap allocations)
- `Cholesky()` and `TriSolve()` rewritten to operate on raw `float*` directly (eliminates ~2 heap allocations × nClusters × nIterations)
- `CandidateClass` uninitialised variable fixed
- `sprintf` → `snprintf` throughout
- `Error()` marked `[[noreturn]]`
- CUDA kernels in `KK_cuda.cu` / `KK_cuda.h` for E-step, M-step, C-step, and deletion loss
- CMake build system replaces legacy Makefile; CUDA detected automatically

See `klustakwik/README_MODERNISATION.md` for the complete change log and GPU kernel design notes.

---

## Build

### CPU-only

```bash
cmake -B build/klustakwik klustakwik -DUSE_CUDA=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/klustakwik -j$(nproc)
sudo cmake --install build/klustakwik
```

### With CUDA

```bash
cmake -B build/klustakwik klustakwik \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.1/bin/nvcc \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/klustakwik -j$(nproc)
sudo cmake --install build/klustakwik
```

CUDA is detected automatically when `nvcc` is on `PATH`. Use `-DCMAKE_CUDA_COMPILER` to specify a non-default location. For CUDA installation instructions on Ubuntu 24.04, Debian 12, and WSL 2, see `doc/ndmanager-dependencies.docx`.

Does not depend on Qt or `libklustersshared`.
