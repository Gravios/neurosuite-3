# KlustaKwik

Automatic spike sorting via Classification EM (CEM). Reads a `.fet` feature file
produced by ndm_pca and writes a `.clu` cluster assignment file.

This is a modernised fork of [KlustaKwik v1.7](http://sourceforge.net/projects/klustakwik)
(Hazan, Zugaro & Buzsáki 2006). The core algorithm is unchanged; the additions are
described in [CHANGES.md](CHANGES.md).

---

## Building

Requires a C++17 compiler and CMake ≥ 3.18. OpenMP is detected automatically and
enables parallel chunk processing when available.

```bash
# CPU-only
cmake -B build -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF
cmake --build build -j$(nproc)

# With CUDA — NVIDIA GPUs (requires CUDA Toolkit >= 11)
cmake -B build -DUSE_HIP=OFF -DUSE_SYCL=OFF
cmake --build build -j$(nproc)

# With HIP — AMD GPUs (requires ROCm >= 5.0)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF
cmake --build build -j$(nproc)

# With SYCL — Intel Arc/Xe GPUs (requires oneAPI Base Toolkit >= 2023.1)
cmake -B build -DUSE_CUDA=OFF -DUSE_HIP=OFF
cmake --build build -j$(nproc)
```

When none of the three backend flags are explicitly set, CMake auto-detects in
priority order CUDA > HIP > SYCL and selects the first toolkit it finds.
Every GPU build also produces `KlustaKwik_cpu` as a CPU-only fallback binary.

---

### CUDA installation (NVIDIA)

Install the [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads)
(>= 11, recommended >= 12) for your distribution. On Ubuntu/Debian:

```bash
# Ubuntu 22.04 / 24.04 — network installer
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update
sudo apt install cuda-toolkit-12-x
```

Verify the installation:

```bash
nvcc --version
nvidia-smi
```

No extra CMake flags are needed; `check_language(CUDA)` will locate `nvcc`
automatically. To target specific GPU generations, override the architecture
list at configure time:

```bash
# Ampere only (RTX 30xx)
cmake -B build -DCMAKE_CUDA_ARCHITECTURES="86" -DUSE_HIP=OFF -DUSE_SYCL=OFF
```

---

### HIP installation (AMD ROCm)

Install [ROCm](https://rocm.docs.amd.com/en/latest/deploy/linux/index.html)
(>= 5.0) for your distribution. On Ubuntu 22.04 / 24.04:

```bash
# Add the ROCm package repository
sudo apt install wget gnupg
wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo apt-key add -
echo "deb [arch=amd64] https://repo.radeon.com/rocm/apt/debian/ jammy main" \
    | sudo tee /etc/apt/sources.list.d/rocm.list
sudo apt update
sudo apt install rocm-dev hipcc
```

Add ROCm to your environment (add to `~/.bashrc` to make permanent):

```bash
export PATH=/opt/rocm/bin:$PATH
export ROCM_PATH=/opt/rocm
```

Verify the installation:

```bash
hipcc --version
rocminfo | grep "Marketing Name"
```

Build KlustaKwik with HIP:

```bash
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF
cmake --build build -j$(nproc)
```

CMake locates `hipcc` automatically via `ROCM_PATH` or the default path
`/opt/rocm/bin`. The default GPU architecture list covers RDNA2 (RX 6000),
RDNA3 (RX 7000), and CDNA2 (Instinct MI210/MI250). To target specific
architectures:

```bash
# RDNA3 only (RX 7900 XTX etc.)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
      -DKK_HIP_ARCHS="gfx1100;gfx1101;gfx1102"

# RDNA2 only (RX 6800 XT etc.)
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
      -DKK_HIP_ARCHS="gfx1030;gfx1031;gfx1032"
```

A full list of gfx identifiers is available via `rocminfo` (look for
`Name: gfxNNNN` under each agent).

**Note on wavefront size:** RDNA wavefronts are 64 threads wide (vs CUDA
warps at 32). The default block size of 256 threads (4 wavefronts) is safe
on all supported architectures. For CDNA2 datacenter cards which benefit from
higher occupancy, raise it at configure time:

```bash
cmake -B build -DUSE_CUDA=OFF -DUSE_SYCL=OFF \
      -DKK_HIP_ARCHS="gfx90a" -DKK_HIP_BLOCK=512
```

---

### SYCL installation (Intel Arc / Xe)

Install the [Intel oneAPI Base Toolkit](https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit.html)
(>= 2023.1), which provides the `icpx` DPC++ compiler and the SYCL runtime.
On Ubuntu/Debian:

```bash
# Add the Intel oneAPI repository
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
    | gpg --dearmor | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] \
    https://apt.repos.intel.com/oneapi all main" \
    | sudo tee /etc/apt/sources.list.d/oneAPI.list
sudo apt update
sudo apt install intel-basekit
```

Activate the oneAPI environment (add to `~/.bashrc` to make permanent):

```bash
source /opt/intel/oneapi/setvars.sh
```

Verify the installation:

```bash
icpx --version
sycl-ls        # lists available SYCL devices; your Arc GPU should appear
```

Build KlustaKwik with SYCL:

```bash
source /opt/intel/oneapi/setvars.sh   # if not already in .bashrc
cmake -B build -DUSE_CUDA=OFF -DUSE_HIP=OFF
cmake --build build -j$(nproc)
```

This produces `KlustaKwik_sycl` (also installed as `KlustaKwik`). The binary
is compiled ahead-of-time for Intel Arc (Alchemist) and Iris Xe targets. The
JIT fallback is still available at runtime for any SYCL-capable device not
explicitly listed at compile time.

The work-group size defaults to 256. To tune it:

```bash
cmake -B build -DUSE_CUDA=OFF -DUSE_HIP=OFF -DKK_SYCL_WG=512
```

---

To compile manually without CMake:

```bash
g++ -std=c++17 -O2 -fopenmp -DNDEBUG -o KlustaKwik KlustaKwik.cpp KK.cpp param.c
```

---

## Usage

```
KlustaKwik FileBase ElecNo [options]
```

`FileBase` and `ElecNo` together locate the input file `FileBase.fet.ElecNo` and
determine the names of all output files.

### Minimal examples

```bash
# Two-phase farthest-point mode (default)
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12

# Three-phase temporal chunking for long recordings with electrode drift
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 \
    -ChunkMinutes 5 -SamplingRate 32572

# With screen output
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 -Screen 1
```

### Output files

| File | Contents |
|---|---|
| `FileBase.clu.ElecNo` | Cluster assignment per spike (1-based; label 1 = noise) |
| `FileBase.klg.ElecNo` | Run log (created only when `-Log 1`) |
| `FileBase.model.ElecNo` | Gaussian model parameters (omitted with `-fSaveModel 0`) |

### Startup banner

Regardless of `-Screen` and `-Log`, the binary always prints a brief summary to
**stderr** before the first CEM call. This confirms that data loaded successfully
and shows whether parallelism is active:

```
KlustaKwik  jg05-20120316.fet.7
  344094 spikes, 25 dims, clusters 2-50
  mode: chunked  chunk=5.0 min  SR=32572  ~16 chunks
  parallel: 16 OpenMP threads
  K=2/50 start=1/1
```

The `K=N/M` line updates in-place as the outer loop advances. If parallelism is
not working correctly, the banner will say so — see
[Troubleshooting](#troubleshooting) below.

---

## Parameters

All parameters are optional. Defaults reflect typical tetrode recordings at 20 kHz.
Parameter names are case-sensitive. Unrecognised names are **silently ignored** by
the parser — double-check spelling if a setting appears to have no effect.

### Cluster range

| Parameter | Default | Description |
|---|---|---|
| `MinClusters` | `2` | Minimum number of starting clusters |
| `MaxClusters` | `10` | Maximum number of starting clusters |
| `MaxPossibleClusters` | `100` | Hard ceiling on cluster count; also sizes internal arrays |
| `nStarts` | `1` | Random restarts per starting cluster count |

### Algorithm selection

| Parameter | Default | Description |
|---|---|---|
| `InitMethod` | `farthest` | Seed strategy: `farthest` (periphery-inward) or `random` |
| `TimeMergeIter` | `30` | Iterations for the Phase 2 time-merge pass. Set `0` to skip |
| `ChunkMinutes` | `0` | Enables three-phase temporal chunking when > 0. Set to the expected stationarity window, typically 3-10 min for silicon probes |
| `SamplingRate` | `20000` | Samples per second. **Must match the actual recording rate** when `ChunkMinutes > 0`; controls conversion of raw sample timestamps to chunk boundaries |

### Temporal chunking (three-phase mode)

Only relevant when `ChunkMinutes > 0`.

| Parameter | Default | Description |
|---|---|---|
| `MergeThresh` | `30` | Symmetric Mahalanobis^2 threshold for matching clusters across adjacent chunk boundaries. Approximates chi^2(nSpatialDims, p=0.99) by default. Increase if units fail to link across chunks; decrease if unrelated units are being merged |
| `GlobalMergeIter` | `20` | Maximum EM iterations for the Phase 3 global warm-start pass |

### EM convergence

| Parameter | Default | Description |
|---|---|---|
| `MaxIter` | `500` | Maximum EM iterations per CEM run |
| `FullStepEvery` | `10` | Full E-step every N iterations; intermediate steps use a faster approximate update |
| `ChangedThresh` | `0.05` | Convergence criterion: fraction of points that must change assignment to continue |
| `SplitEvery` | `50` | Attempt cluster splits every N iterations |
| `PenaltyMix` | `0` | Blends BIC (`0`) and AIC (`1`) as the model-complexity penalty |

### Input / output

| Parameter | Default | Description |
|---|---|---|
| `UseFeatures` | `11111111111100001` | Binary mask selecting which feature columns to load. Length must match the feature count in the `.fet` header; the trailing `1` selects the timestamp column. Use `all` to select every column |
| `StartCluFile` | _(none)_ | Path to an existing `.clu` file to use as the initial assignment |
| `RandomSeed` | `1` | RNG seed (applies to `InitMethod random`) |
| `fSaveModel` | `1` | Write Gaussian model file. Set `0` to skip |
| `SaveIntermediates` | `1` | Write `.clu` on every new best score during the cluster-count sweep. Set `0` to suppress mid-run writes and perform only the single final write — recommended on network storage |

### Logging

All output is silent by default. The stderr startup banner is always printed
and is not controlled by these flags.

| Parameter | Default | Description |
|---|---|---|
| `Screen` | `0` | Print per-iteration progress to stdout |
| `Log` | `0` | Write per-iteration progress to `FileBase.klg.ElecNo` |
| `Verbose` | `0` | Also print the full parameter table at startup. Requires `Screen 1` or `Log 1` |

### Diagnostic / rarely changed

| Parameter | Default | Description |
|---|---|---|
| `DistDump` | `0` | Dump the log-probability matrix to `DISTDUMP` after each E-step. Large; for debugging only |
| `DistThresh` | `6.908` | Log-odds threshold for noise assignment (ln 1000) |
| `Debug` | `0` | Print per-cluster mean and covariance at each M-step |

---

## Algorithm overview

### Default: two-phase farthest-point CEM

**Phase 1 — spatial EM.** Features are clustered using only the PCA dimensions,
with the timestamp excluded. Initial centres are chosen by farthest-point seeding:
the first centre is the global centroid; each subsequent centre is the data point
with the largest minimum Mahalanobis distance from the current set. This
periphery-inward strategy places seeds at the natural boundaries of the data
rather than having multiple seeds compete for the same dense core, reducing EM
iterations by 60-75% compared to random seeding.

**Phase 2 — time-merge pass.** The timestamp is reintroduced and `TimeMergeIter`
additional EM iterations run at full dimensionality. Units active in different
time epochs can be distinguished; units that drift slightly over the session will
now fit a tilted ellipse in PCA+time space rather than an inflated sphere.

### Optional: three-phase temporal chunking

Enabled with `ChunkMinutes > 0`. Designed for multi-hour recordings where
electrode drift causes a unit's PCA trajectory to shift substantially over the
session, making a single global Gaussian a poor fit.

**Phase 0 — partition.** The session is divided into windows of `ChunkMinutes`
minutes using the raw sample timestamps captured at load time. Any window below
the minimum spike count (`nStartingClusters x nSpatialDims x 3`) is merged into
its predecessor.

**Phase 1 — per-chunk CEM (parallel).** Each chunk runs an independent two-phase
CEM. Chunks are processed in parallel across CPU cores using OpenMP
(`schedule(dynamic)` accounts for variable chunk sizes). Each chunk sub-object
owns its own Cholesky storage and does not touch any shared state, making the
parallel execution data-race-free.

**Phase 2 — cross-chunk model matching.** For each pair of immediately adjacent
chunks `(k, k+1)`, every cluster from chunk `k` is compared against every cluster
from chunk `k+1` using the symmetric Mahalanobis distance in the spatial
dimensions. Pairs within `MergeThresh` are linked in a Union-Find structure.
Transitivity handles continuously drifting units: A-B and B-C edges imply A and C
belong to the same global unit without requiring a direct A-C comparison.
Non-adjacent chunks are never compared. Noise clusters from all chunks are
unconditionally merged to a single global noise class before any distance
comparisons run.

**Phase 3 — global warm-start EM.** The full dataset is reassembled with globally
matched labels as the starting assignment. A short EM pass at full dimensionality
(at most `GlobalMergeIter` iterations) refines the models. This typically
converges in 1-5 iterations on real recordings.

If the merge produces more global clusters than `MaxPossibleClusters` (indicating
`MergeThresh` is too small), the run falls back to two-phase CEM on the full
session with a diagnostic message to stderr.

---

## Typical invocations

```bash
# Standard tetrode recording, two-phase mode
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12

# Suppress intermediate .clu writes (recommended on NFS / network storage)
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 -SaveIntermediates 0

# 80-minute silicon probe recording at 32572 Hz, chunked mode
KlustaKwik jg05-20120316 7 \
    -MinClusters 2 -MaxClusters 20 \
    -UseFeatures 1111111111111111111111111 \
    -ChunkMinutes 5 -SamplingRate 32572 \
    -MergeThresh 30 -GlobalMergeIter 20 -TimeMergeIter 30 \
    -SaveIntermediates 0

# Re-run from an existing sort
KlustaKwik session 1 -StartCluFile session.clu.1

# Original v1.7 behaviour (random init, no time-merge, full logging)
KlustaKwik session 1 -InitMethod random -TimeMergeIter 0 \
    -Screen 1 -Log 1 -Verbose 1
```

---

## Integration with ndm_pca / NeuroSuite

Pass `-SamplingRate` from the XML session descriptor so chunk boundaries are
calculated correctly:

```xml
<process cmd="KlustaKwik %filebase% %electrodeNo%
    -MinClusters 2 -MaxClusters 12
    -ChunkMinutes 5 -SamplingRate %samplingRate%
    -SaveIntermediates 0"/>
```

---

## Troubleshooting

### The program runs on a single core despite -ChunkMinutes

The startup banner identifies the exact cause before the first CEM call, so you
do not have to wait for the run to finish to diagnose the problem.

**Case 1 — `OMP_NUM_THREADS` is set to 1:**

```
parallel: 1 of 16 cores  (OMP_NUM_THREADS=1 limits parallelism —
                           unset it or set to 16 to use all cores)
```

This is the most common cause. Fix it in the current shell:

```bash
unset OMP_NUM_THREADS
```

To make it permanent, add this to `~/.bashrc`:

```bash
export OMP_NUM_THREADS=$(nproc)
```

You can also override it inline for a single run without touching the environment:

```bash
OMP_NUM_THREADS=16 KlustaKwik session 1 -ChunkMinutes 5 ...
```

**Case 2 — running under WSL2 with CPU allocation restricted:**

```
parallel: 1 of 1 cores
```

If `nproc` returns 1 (or far fewer cores than the Windows host has), WSL2 is only
being given 1 CPU by its configuration. On the Windows host, create or edit
`C:\Users\<YourUsername>\.wslconfig`:

```ini
[wsl2]
processors=16    ; set to the number of physical cores on the host
```

Then restart WSL from an elevated PowerShell or Command Prompt:

```powershell
wsl --shutdown
```

Reopen your WSL terminal. `nproc` should now return the configured value and
KlustaKwik will use all of them without any environment variable needed. You can
confirm the host core count in Task Manager > Performance > CPU.

**Case 3 — binary not linked with OpenMP:**

```
WARNING: built without OpenMP — chunks run serially.
         Recompile with -fopenmp to enable parallelism.
```

Rebuild with the flag explicitly:

```bash
g++ -std=c++17 -O2 -fopenmp -DNDEBUG -o KlustaKwik KlustaKwik.cpp KK.cpp param.c
```

When using CMake, check that `find_package(OpenMP)` succeeded during
configuration — the output should include a line like
`-- Found OpenMP_CXX: -fopenmp (found version "4.5")`.
If it is missing, install the OpenMP runtime (`sudo apt install libomp-dev` on
Debian/Ubuntu) and re-run CMake.

You can verify a built binary at any time:

```bash
ldd KlustaKwik | grep -i omp    # should print something like libgomp.so.1
```

### A parameter has no effect

Parameter names are case-sensitive and unrecognised names are silently ignored.
Run with `-Screen 1 -Verbose 1` to print the full parameter table at startup and
confirm the value was accepted.

Common mistakes:

- `-SaveIntermediates` is frequently misspelled (e.g. `-SaveIntermeiates`)
- `-SamplingRate` is often omitted in chunked mode; the default of 20000 will
  produce wrong chunk boundaries if your recording rate differs

### Chunk merge produces too many global clusters

```
WARNING: MergeChunkModels produced N global clusters >= MaxPossibleClusters (100).
  This means MergeThresh=30.0 is too small ...
  Falling back to CEMTwoPhase on the full session.
```

Either increase `MergeThresh` (try 50-100) or raise `MaxPossibleClusters` to
accommodate the actual unit count. If the fallback produces a good result, the
recording may not have enough drift to need chunking.

### Runtime is much longer than expected

With `MaxClusters=50` and 25 features, each per-chunk CEM at K=50 is heavy.
The outer loop runs every K from `MinClusters` to `MaxClusters` sequentially;
a K=2..50 sweep on 16 chunks is 49 parallel batch jobs, each potentially taking
several minutes at high K. Consider a first pass with `MaxClusters=20` to find
the score plateau before committing to a wider search.

---

## References

Hazan L, Zugaro M, Buzsáki G (2006). Klusters, NeuroScope, NDManager: a free
software suite for neurophysiological data processing and visualization.
*Journal of Neuroscience Methods* 155:207-216.

Svihra M, Bhatt D, et al. (2020). KlustaKwik — spike sorting for large scale
multi-channel recordings. GitHub: klusta-team/klustakwik.
