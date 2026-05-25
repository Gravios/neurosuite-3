# KlustaKwikExp Phase-4 Rewrite — Design Document

## Scope

Replace the current post-Phase-2 logic in `KK::RunChunkedCEM` (Phases 3, 4,
and parts of 5) with a refinement loop that combines:

1. ISI-conditional within-cluster waveform-adaptation modelling (rank-2
   per-cluster: rested mean + adaptation shape × scalar × time-constant).
2. Waveform-space KNN splitting/reassignment in ISI-residual space.
3. xcorr-based cluster-pair merging with sub-sample peak refinement and
   amplitude-scaled residual-energy scoring.
4. A randomised scheduler that alternates between split and merge passes
   to escape local optima.

`process_drifttracker`'s machinery (time-windowed mean templates with
Tikhonov smoothing) remains unchanged and is invoked as a post-process,
not duplicated inside KKE.

## Pipeline order (after Phase 2 completes)

```
              Phase-2 .clu  (per-chunk subspace-reclustered)
                       │
                       ▼
         ┌─────────────────────────────────┐
   3.    │  Per-chunk feature-space KNN     │   per_chunk_knn[p] = list of K
         │  graph (causal-windowed brute    │   feature-nearest neighbours
         │  force, O(N · cand · nDims))     │   restricted to spikes within
         └─────────────────────────────────┘   ±causalWindowSec of t_p
                       │
                       ▼
         ┌─────────────────────────────────┐
   4.    │  proxy_isi computation           │   proxy_isi[p] = t_p − t_{j*}
         │  (cluster-free; uses KNN graph)  │   where j* = most-recent
         │                                  │   feature-space neighbour
         └─────────────────────────────────┘   with t_j < t_p
                       │
                       ▼
         ┌─────────────────────────────────┐
   5.    │  ISI-conditional adaptation fit  │   per cluster c:
         │  per cluster c (alternating LS)  │     w₀_c, v_c, α_c, τ_c
         │                                  │   + flag if ill-conditioned
         └─────────────────────────────────┘
                       │
                       ▼
         ┌─────────────────────────────────┐
   6.    │  Waveform residual computation   │   r_p = w_p − α_c·h(τ_c, isi)·v_c
         │  (used for KNN split + scoring)  │   for c = current cluster of p
         └─────────────────────────────────┘
                       │
                       ▼
         ┌─────────────────────────────────┐
   7.    │  Cluster quality scoring         │   q_c = pooled intra-cluster
         │  (variance in residual space)    │     residual variance, CV-norm.
         │                                  │   sets priority queue for split.
         └─────────────────────────────────┘
                       │
                       ▼
         ┌─────────────────────────────────┐
   8.    │  Randomised refinement loop      │
         │  (iterates up to MaxRefineIter)  │
         │                                  │
         │   At each iteration, pick one    │
         │   operation by weighted random   │
         │   choice:                        │
         │                                  │
         │   (a) KNN-split: pick worst-q    │
         │       cluster as source, use     │
         │       low-q clusters as          │
         │       references, majority-vote  │
         │       in residual space.         │
         │                                  │
         │   (b) xcorr-merge: pick best-    │
         │       quality pair by mutual-    │
         │       best xcorr score (with     │
         │       sub-sample refinement and  │
         │       amplitude-scaled scoring), │
         │       merge if both clusters'    │
         │       ISI-models match.          │
         │                                  │
         │   (c) refit-adaptation: re-fit   │
         │       (w₀, v, α, τ) for changed  │
         │       clusters; recompute        │
         │       residuals + quality.       │
         │                                  │
         │   Accept move iff total cluster- │
         │   quality score improves OR a    │
         │   small uphill move (simulated   │
         │   annealing) is accepted under   │
         │   cooling schedule.              │
         └─────────────────────────────────┘
                       │
                       ▼
              Phase-5 (cross-chunk merge)
              Phase-6 (global warm-start EM)
              [unchanged from existing impl]
                       │
                       ▼
              Post-process: process_drifttracker
                  (existing standalone binary;
                   handles spatial drift via
                   per-window template tracking)
```

## Module breakdown

| Module                    | File(s)                  | Purpose                                                       |
|---------------------------|--------------------------|---------------------------------------------------------------|
| Feature-space KNN graph   | `feat_knn.{h,cpp}`       | Causal-windowed brute-force KNN in feature space, OMP-parallel|
| Proxy-ISI                 | `proxy_isi.{h,cpp}`      | Per-spike "time since most recent same-unit-ish spike"        |
| Adaptation model          | `adapt_model.{h,cpp}`    | Per-cluster (w₀, v, α, τ) fit via alternating least squares   |
| Cluster quality           | `clust_quality.{h,cpp}`  | CV-normalised intra-cluster residual variance                 |
| Improved xcorr matcher    | `xcorr_match.{h,cpp}`    | Time-domain xcorr + parabolic sub-sample refinement +         |
|                           |                          | amplitude-scaled residual-energy score                        |
| Waveform-KNN split        | `wave_knn_split.{h,cpp}` | KKE port of klusters' `splitClusterByKnnVsReferences`         |
| Refinement scheduler      | `refine_loop.{h,cpp}`    | Randomised choice between split/merge/refit with SA cooling   |
| Phase-4 orchestrator      | (inlined in `KK.cpp`)    | Glue: drives the loop, owns the data, calls modules           |

Each module is independently testable with its own small test driver under
`test/` (mirroring the existing `dipsplit` test pattern).

## Data conventions

All modules assume the following layout, matching existing KKE conventions:

- **Features**: `Data` array, `[nPoints × nDims]`, point-major. Last dim
  (`nDims - 1`) is the normalised timestamp. For algorithms that need
  spatial features only, use `[0, nDims - 2)`.
- **Timestamps**: raw timestamps in seconds reconstructed via
  `t_raw = Data[p*nDims + (nDims-1)] * (timeRawMax - timeRawMin) + timeRawMin`.
- **Class labels**: `Class` array, `[nPoints]`, int. 0 = noise, 1 = MUA-like,
  ≥ 2 = real clusters (matches existing KKE convention).
- **Waveforms**: stored on disk in `.spk.N` (or `.spkD.N` for stderiv
  pipelines). Read on demand via `ReadOneSpikeWave()`. Sample-major,
  channel-minor layout: `spike[s * nChan + ch]`.

## Open design questions

These are tagged for explicit decision before each module is implemented:

1. **KNN graph: brute-force vs approximate.** For per-chunk sizes
   (~10⁴–10⁵ spikes), causal-windowed brute force is O(N·cand·D) with
   `cand` = avg spikes per `causalWindowSec`. At 200 Hz × 5 s = 1000
   candidates, 25 dims, 10⁵ spikes: ~2.5e9 ops, OMP-parallel → seconds.
   **Decision: brute force, defer approximate KNN until profiling demands it.**

2. **Adaptation model rank.** Rank-2 (w₀ + amplitude-mode) is the
   minimum useful; rank-3 would add a width/slope mode. Higher rank
   needs more spikes per cluster to fit stably.
   **Decision: start with rank-2, add rank-3 as a runtime flag.**

3. **Adaptation kernel form.** Single exponential `h(τ) = exp(-isi/τ_c)`
   is the simplest and physically motivated (Na⁺ channel recovery).
   Alternative: two-exponential (fast Na⁺ + slow K⁺/AHP).
   **Decision: single exponential v1; two-exponential as v2.**

4. **τ_c search.** Grid search over {5, 10, 20, 50} ms vs. continuous
   optimisation. Grid is robust and fast; continuous is more precise
   but adds nonlinearity to the alternating-LS fit.
   **Decision: 4-point grid + parabolic interpolation around best.**

5. **Random scheduler weights.** Initial probabilities for
   (split, merge, refit) — start uniform 1/3 each, tune empirically.
   **Decision: configurable via YAML, default uniform.**

6. **SA cooling vs strict-accept-only.** Strict accept is simpler and
   deterministic; SA cooling can escape some local optima.
   **Decision: strict accept v1, add optional SA in a follow-up.**

## Integration with existing KKE code

- Insertion point: replace the body of the Phase-4 loop in
  `RunChunkedCEM` (currently calls to `WithinChunkTemplateMatch` after
  Phase-3 mean-waveform harvest). Both copies of this code in `KK.cpp`
  (lines ~3640 and ~4790) get the same replacement.
- The Phase-3 mean-waveform harvest is reused as-is for initial
  per-cluster waveform estimates; subsequent refits happen via
  `adapt_model::Fit`.
- Phase-5 (`MergeChunkModels`) and Phase-6 (warm-start EM) are unchanged.
- All new modules live under `src/klustakwikExp/` alongside `dipsplit.*`.
- Existing `libklustersshared/xcorr/realign_xcorr_omp.cpp` is kept and
  `xcorr_match` builds on it (calls into it for the raw xcorr inner
  loop, adds parabolic refinement and amplitude-scaled scoring around it).

## Validation plan

1. **Unit tests per module** (in `test/`):
   - `feat_knn`: synthetic 2D Gaussians, verify recovered neighbour
     ordering matches scipy.spatial.cKDTree on a saved fixture.
   - `proxy_isi`: synthetic Poisson process with known rate, verify
     proxy-ISI distribution matches expected exponential.
   - `adapt_model`: synthetic spikes with known (w₀, v, α, τ) injected,
     verify recovery within tolerance.
   - `clust_quality`: hand-constructed pure vs contaminated cluster,
     verify quality score ordering.
   - `xcorr_match`: synthetic shifted templates with known sub-sample
     lag, verify recovery within 0.1 sample.
   - `wave_knn_split`: hand-built two-Gaussian mixture, verify
     reassignment.

2. **Integration test** on a single known-good session:
   - Run existing pipeline → save `.clu` snapshot.
   - Run new Phase-4 → compare cluster count, ISI-violation %, manual
     spot-check of merged pairs.
   - Acceptance: ≤10% increase in cluster count for over-split data,
     ≥30% reduction in ISI-violation pairs for contaminated data,
     no regression on visual cluster shape in klusters.

3. **Performance budget**:
   - Per-chunk Phase-4 wall time ≤ 2× existing implementation.
   - Memory overhead ≤ 1.5× existing implementation.

## Build order

1. `proxy_isi` (depends on nothing; pure CPU module).
2. `feat_knn` (used by proxy_isi but proxy_isi can use a stub initially;
    extract & generalise after).
3. `clust_quality` (depends on adapt_model output but degrades
   gracefully to raw-waveform variance if adaptation hasn't run).
4. `adapt_model`.
5. `xcorr_match`.
6. `wave_knn_split`.
7. `refine_loop`.
8. Phase-4 orchestrator wiring.

Each module is reviewable independently and can be benchmarked against
synthetic data before integration.
