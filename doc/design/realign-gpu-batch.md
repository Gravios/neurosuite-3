# Cross-cluster GPU-batched PCA-Center Align All — design

**Status:** design only. The device kernels in this design cannot be compiled or
run in the patch sandbox (no CUDA/HIP/SYCL toolchain, no GPU), so this is meant
to be implemented and validated on the RTX hardware. It is grounded in the
actual current interfaces (`pca_refine_dispatch.h`, `KlustersDoc::realignSpikes`)
as of patch 0051.

---

## 1. What the GPU can and cannot accelerate (read this first)

The realign of one cluster is a sandwich:

| Phase | Where | Cost | On GPU? |
|------|-------|------|---------|
| A — read one wide window per spike from `.fil` | CPU / disk | `N` seek+read (random) | **No** — disk I/O |
| B — pack PCA basis to flat float | CPU | tiny | n/a |
| C — evaluate every (spike × candidate) PCA energy, argmax shift | **GPU** | `N·M·chForPca·kComp·d2u` | **Yes** |
| D — re-read chosen frame per refined spike, rebuild `wavBuf` | CPU / disk | `nRefined` seek+read | **No** — disk I/O |
| writeback — in-memory pending update + sort-order correction | CPU | grows? (TBD) | **No** |

**Only Phase C runs on the GPU.** Phases A and D are `.fil` disk reads and stay
on the CPU no matter what. So "push the entire process to the GPU" concretely
means: **batch Phase C across all clusters** so the kernel is launched once (or a
few times) over all spikes of all clusters, instead of 3000 separate launches
each with its own upload/download. Per-cluster basis packing/upload also collapses
to once-per-cluster-resident.

**Where the win is.** With 3000 clusters, most are small; for a small cluster the
fixed per-launch + transfer overhead can dominate the actual compute, and small
clusters below `gpuThreshold()` don't even use the GPU today (they run the CPU
per-candidate loop). Batching amortizes launch overhead across all clusters and
lets the small ones ride along on the device. For a handful of very large
clusters the existing per-cluster path already amortizes launch well, so they
gain little.

**Where the win is NOT.** If the realign is dominated by Phase A/D disk I/O or by
writeback, batching Phase C will not move the wall clock much. **This is exactly
what `NS3_REALIGN_TIMING=1` (patch 0050) measures.** Run it first:

- `compute` bucket large and roughly flat per cluster → Phase C / I/O bound;
  batching helps the launch-overhead portion (confirm with a quick test: does
  `compute` per small cluster shrink toward the kernel time when batched?).
- `writeback` bucket growing with cluster index → the bottleneck is the in-memory
  pending/sort path, and **this design will not help** — fix that instead.

Do not build section 3+ until the timing says `compute` is the dominant,
batchable cost.

---

## 2. Current dispatcher interface (unchanged, for reference)

```c++
// pca_refine_dispatch.h
int PcaRefineGpu::refine(
    int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
    int kComp, int d2u, int rShift, int maxShift, int centered, int useStder,
    const int16_t* rawWindowsCM,   // K * nChan * wideLen, channel-major
    const float*   pcaEvec,        // chForPca * kComp * d2u
    const float*   pcaMeans,       // chForPca * d2u  (or null)
    int*           bestShifts);    // out: K
```

`K` = spikes in the cluster, `M = 2*maxShift+1` candidates. One call = one cluster.

---

## 3. Batched dispatcher API (new)

Add a sibling that takes `G` clusters concatenated, with per-cluster offsets and
per-cluster bases. Geometry that is constant across a group (`wideLen`, `nSamp`,
`nChan`, `maxShift`, `M`, `centered`, `useStder`) stays scalar; geometry that can
differ per cluster basis (`chForPca`, `kComp`, `d2u`, `rShift`) goes per-cluster.

```c++
// pca_refine_dispatch.h
struct RefineBatchDesc {
    int   G;                       // number of clusters
    long long totalK;              // sum of per-cluster spike counts
    int   M, wideLen, nSamp, nChan;
    int   maxShift, centered, useStder;

    const long long* kOffset;      // [G+1] prefix sum of spike counts (CSR-style)
    const int* chForPca;           // [G]
    const int* kComp;              // [G]
    const int* d2u;                // [G]
    const int* rShift;             // [G]

    const long long* evecOffset;   // [G+1] prefix sum into pcaEvec (floats)
    const long long* meansOffset;  // [G+1] prefix sum into pcaMeans (or null)

    const int16_t* rawWindowsCM;   // totalK * nChan * wideLen, concatenated
    const float*   pcaEvec;        // concatenated per-cluster bases
    const float*   pcaMeans;       // concatenated (or null)
    int*           bestShifts;     // out: totalK
};

int PcaRefineGpu::refineBatch(const RefineBatchDesc& d);  // 0 == success
```

`bestShifts[k]` for global spike index `k` is read back exactly as the per-cluster
path fills `bestShiftGpu[i]`. The host maps global `k` back to `(cluster g, local
i)` via `kOffset`.

---

## 4. Kernel design (per backend)

One spike = one thread block (mirrors the current per-cluster kernel, which
already "sums across nChan in one block", hence the `nChan <= 256` guard).

```
block  = global spike k
threads= over (candidate m, component reduction)  // tune per backend
for m in 0..M-1:
    energy[m] = 0
    for ch in 0..chForPca[g]-1:
        for comp in 0..kComp[g]-1:
            proj = 0
            for u in 0..d2u[g]-1:
                x = window[k][ch][ rShift[g] + m + u ]      // int16 -> float
                if centered: x -= means[g][ch][u]
                proj += evec[g][ch][comp][u] * x
            energy[m] += proj*proj
bestShifts[k] = (argmax_m energy[m]) - maxShift
```

`g` is found from `k` by a binary search on `kOffset` (or pass a precomputed
`spikeCluster[totalK]` array — simpler, costs `totalK` ints). Per-cluster scalars
(`chForPca[g]`, bases via `evecOffset[g]`) are read once per block into shared
memory.

The arithmetic per spike is **identical** to the current kernel, so a batched run
must produce bit-identical `bestShifts` to running the clusters one at a time —
that is the correctness oracle (section 7).

Backends: CUDA, HIP, SYCL each get a `refineBatch` next to `refine`. The CPU
fallback backend implements `refineBatch` as an OpenMP parallel-for over
`k = 0..totalK-1` reusing the existing scalar inner math.

---

## 5. Host orchestration (klustersdoc.cpp)

A new `realignSpikesBatch(const QList<int>& clusterIds, ...)` (or a batch flag on
the existing path) does:

1. **Load the group basis once** (already cached as of patch 0051) and pack it to
   flat float once. With a shared group basis, `pcaEvec`/`pcaMeans` are the *same*
   for every cluster, so `evecOffset` can point all clusters at one copy (no
   duplication). Keep the per-cluster offset arrays for generality.
2. **For each cluster**: gather its spikes (the existing `gidx` / `clusterTs`
   path), run **Phase A** to fill that cluster's slice of `rawWindowsCM`, record
   `validRow`. This is the unavoidable CPU/disk I/O; it can be overlapped with the
   GPU by double-buffering (read chunk *n+1* while the kernel chews chunk *n*).
3. **Memory bound + chunking.** The current code caps one cluster's windows at
   512 MB. Replace with a *global* working-set cap: accumulate clusters into a
   chunk until the next cluster would exceed the cap, launch `refineBatch` for the
   chunk, then continue. Each chunk is one kernel launch. (RTX 5070 Ti: a 1–2 GB
   working set is comfortable; pick from VRAM probe.)
4. **One `refineBatch` per chunk** → `bestShifts` for all spikes in the chunk.
5. **Phase D per cluster**: apply `bestShifts`, re-read the chosen frames, rebuild
   `wavBuf`, then the existing writeback. Edge-clamped spikes (`validRow[i]==0`)
   still drop to the CPU per-candidate handling exactly as today, so `nClamped`
   accounting is preserved.

Spikes from clusters too small / edge-clamped are handled by the CPU path as now —
batching is purely an acceleration of the GPU-eligible spikes.

---

## 6. CPU-batched fallback

`refineBatch` on the CPU backend = OpenMP `parallel for` over the concatenated
`totalK` spikes. This is independently useful: it removes the per-cluster thread
pool teardown/spawn between clusters and gives one big balanced parallel region
instead of 3000 small ones. **It is fully syntax-checkable and runtime-testable
without a GPU**, so it can land and be validated first, with the device kernels
following.

---

## 7. Correctness validation plan

1. **Oracle test.** On a real session, run Align-All the current way, save the
   resulting `cumShift`/`bestShift` per spike; run the batched way; assert the
   per-spike chosen shifts are identical. Any divergence is a kernel bug.
2. **Single-cluster equivalence.** `refineBatch` with `G==1` must equal `refine`
   bit-for-bit — add to the existing `kk`/`klusters` ctest suite as
   `klusters_test_refine_batch` (pure host, CPU backend, no GPU needed).
3. **Chunk-boundary test.** Verify a cluster that lands at a chunk boundary gets
   the same result as when it's mid-chunk (offsets/indexing bug catcher).
4. **Edge-clamp parity.** `nClamped`/`nRefined` totals must match the per-cluster
   path on the same session.

---

## 8. Files to touch

- `pca_refine_dispatch.h` / `pca_refine_gpu_dispatch.cpp` — add `refineBatch` +
  `RefineBatchDesc`, dispatch to the active backend.
- `pca_refine_gpu.h` + the CUDA/HIP/SYCL kernel sources — add the batched kernel.
- CPU backend — add the OpenMP `refineBatch`.
- `klustersdoc.cpp` — `realignSpikesBatch` orchestration (chunking, double-buffer).
- `klusters.cpp` — `slotPcaAlignAllClusters` calls the batch path instead of
  queuing one worker per cluster; progress bar advances per chunk-applied cluster.
- `tests/` — `klusters_test_refine_batch` (CPU oracle).

---

## 9. Expected payoff & risks

- **Payoff** scales with (number of GPU-eligible clusters) × (per-launch +
  transfer overhead). Best case is the 3000-small-cluster session; modest for
  few-large-cluster sessions.
- **Floor.** Phases A/D disk I/O and writeback are untouched. If they dominate
  (timing will say), payoff is small — do not invest the kernel work until the
  timing rules this out.
- **Risk.** Indexing bugs across the CSR offset arrays; chunk-boundary off-by-one;
  shared-memory sizing per backend (Blackwell 128 KB/SM is generous but the SYCL
  path on other hardware is tighter). The `G==1` and oracle tests catch the math;
  the chunk-boundary test catches the indexing.

---

## 10. Suggested sequence

1. **(done)** patch 0050 — timing. **Run it.**
2. If `compute` dominates: land the **CPU `refineBatch` + host orchestration +
   `G==1` oracle test** first (all validatable here / on CPU). Measure the
   no-GPU improvement (one big OpenMP region vs 3000 small workers).
3. Then add the CUDA kernel, validate with the oracle test on the RTX box, then
   HIP/SYCL.
