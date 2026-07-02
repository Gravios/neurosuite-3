# Error-matrix compute — profiling and optimization roadmap

The error matrix is a purely qualitative aid for visual curation: each cell
`(i,j)` is the mean, over cluster *i*'s spikes, of the row-normalised posterior
of that spike under cluster *j*. Nothing downstream is derived from it. This note
records where the full-recompute time actually goes (measured, not assumed) and
the remaining levers, so the next change is chosen from data.

Reference session: `sirotaA-jg-000005-20120312.clu.stderiv.5.fiber_session_curated`
— ~3520 error-matrix clusters, 525 725 spikes, 24 PCA dims. Card: RTX PRO 6000
Blackwell (FP64 throttled ~1/64 of FP32). All figures from `NS3_ERRORMATRIX_TIMING`.

## Where the time went (and the arc so far)

Full cold-seed recompute, FP64, ~3519 clusters:

| phase (host `computeProbabilities`) | ms |
|---|---:|
| duplicate (spikesByCluster + clusterInfoMap) | 2 |
| meanCov (per-cluster mean + covariance) | 25 |
| **alloc (new Array `nSpikes×nClusters` ≈ 14.8 GB)** | **~2340** |
| cholesky (per-cluster factor + packed L) | 10 |
| hostArrays (h_features / h_chol / h_means / …) | 36 |
| gpu (`GpuDispatch::computeProbabilities`) | ~2040 |
| — of which: upload / kernel / download | 74 / 1580 / 380 |
| aggregate (OpenMP reduction → matrix) | ~360 |
| **FULL total** | **~5200** |

Arc: ~8.4 s originally →

- **copy-back removed** (GPU writes straight into the probabilities buffer;
  identical row-major layout): −5.3 s. The single-threaded ~1.86e9-element
  copy was the serial twin of the OpenMP aggregation.
- **device-side memset** (zero `d_prob` on the GPU instead of a ~15 GB H2D
  upload of the zeroed host buffer): upload 417 → ~74 ms.
- **redundant `fillWithZeros` removed**: only ~120 ms. NB: this was expected to
  save ~half of `alloc` and did **not** — see below.

## Why `alloc` dominates, and why removing `fillWithZeros` barely helped

`new Array<double>(nSpikes, nClusters)` allocates ~14.8 GB and, via
`make_unique<T[]>`, **value-initialises** it (zeroes). The dropped
`fillWithZeros()` was a *second* zeroing pass — but over pages the constructor
had already faulted in and made resident, so it ran at memory bandwidth
(~120 ms), not the ~1.2 s guessed.

The real cost is the constructor's **first touch** of a fresh 14.8 GB buffer:
faulting in ~3.6 M 4 KiB pages plus the zero write. At ~20–40 GB/s the write
alone is only ~0.4–0.7 s, so the majority of the ~2.3 s is **page-fault
overhead**, and it is paid **on every recompute** because the buffer is freshly
allocated each time. The incremental path allocates its own full
`nSpikes×nClusters` `raw` buffer as well, so it pays the same tax (and is not the
fast path here).

FP32 was removed as a dead end: with the copy-back gone the compute is bound by
this host buffer and the PCIe transfer, not FP64 arithmetic. FP32 halved only the
kernel (~1.6 → ~0.8 s) while adding conversion passes and a slower pageable
download — net roughly even, plus ~5e-4 error and a softmax-underflow pitfall.

## Remaining levers (ranked)

### 1. GPU-side aggregation — biggest, cleanest, largest change
Reduce the per-spike posteriors into the `nClusters × nClusters` matrix **on the
device** and copy back only that (~99 MB) instead of the 14.8 GB intermediate.
Removes `alloc` (~2.3 s), `download` (~0.38 s), and host `aggregate` (~0.36 s) in
one stroke — the full recompute would approach the kernel time (~1.6 s) plus a
small reduction. PCIe traffic drops ~150×.

Requires: a reduction kernel keyed by cluster spans, so the spike→cluster mapping
(`spikesByCluster` position→feature-row plus each cluster's
`firstSpikePosition`/`nbSpikes`) must be uploaded. Must be reconciled with the
incremental raw-column cache, which today reuses **host-side** per-cluster
columns; GPU-side aggregation either keeps a device-resident raw cache or accepts
full GPU recompute (which, at ~1.6 s, may simply make incremental unnecessary).
Needs on-hardware CUDA iteration.

### 2. Reusable host scratch — contained, mid-term
Keep a persistent probabilities buffer sized to the largest cluster count seen.
Because merges only *reduce* the cluster count, it is allocated (and faulted)
once and reused thereafter, removing `alloc` (~2.3 s) on every subsequent
recompute. Requires either a non-owning `Array` view over an external buffer
(Array currently owns via `unique_ptr`) or a fixed max-column stride threaded
through the kernel and aggregation. Trades ~15 GB resident RAM (of 64 GB). Also
benefits the incremental path's `raw` buffer.

### 3. Huge pages — small, platform-specific
Back the buffer with transparent huge pages (`madvise(MADV_HUGEPAGE)` on an
aligned mmap) to cut 4 KiB-fault count ~512×. Could bring `alloc` toward its
bandwidth floor (~0.5 s). Linux/THP-config dependent and uncertain; contained.

## Recommendation

GPU-side aggregation (1) is the correct long-term target — it deletes the 14.8 GB
intermediate entirely and makes precision, allocation, and download largely moot.
Reusable scratch (2) is the lower-risk interim win if per-merge latency needs to
drop before (1) is built. Both need building and testing on the target GPU/host;
neither is a safe blind one-liner, which is why this note precedes the code.
