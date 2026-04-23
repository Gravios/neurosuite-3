# klustakwikExp — experimental fork of KlustaKwik

## Why a separate fork

This directory is a full copy of `src/klustakwik/` with one additional
algorithmic feature: **post-split shift-probe refeaturization**.  The change
touches the hottest clustering inner loop (`TrySplits`) and two per-chunk
split passes (`SubspaceReclusterPerChunk`, `RefractorySplitPerChunk`).
Because the behaviour of `TrySplits` affects every CEM run regardless of
mode, keeping this work in a parallel build target avoids destabilising
the canonical spike sorter while the approach is evaluated.

Both binaries coexist in the build tree:

| target          | source dir              | binary                   |
|-----------------|-------------------------|--------------------------|
| `KlustaKwik`    | `src/klustakwik/`       | `KlustaKwik` (or `_cpu`) |
| `KlustaKwikExp` | `src/klustakwikExp/`    | `KlustaKwikExp` (or `_cpu`) |

The canonical binary is unaffected by anything in this directory.

## Algorithm

### Problem

Phase 1.5 of the canonical pipeline xcorr-aligns each spike to its current
cluster mean-waveform template and re-extracts from `.fil`.  It runs ONCE,
after Phase 2.5's splitting passes.  If a cluster is still a mixture at
the moment Phase 1.5 fires, the template mean sits between the two
subpopulations' true peaks — alignment then drags unit A toward unit B's
peak and vice versa, reducing the sub-sample alignment signal that would
have separated them.  Subsequent splits see features smeared together by
the very alignment that was meant to help.

### Approach — pre-shifted PCA bases, three candidates per spike

After every accepted split:

1. For each new child cluster, test δ ∈ {−1, 0, +1}.
2. For each spike, read its `.spk` waveform once and project against THREE
   pre-shifted eigenvector bases in a single pass — one dot product per δ,
   all three accumulators stepping through the same raw samples.
3. For each candidate δ, sum per-spatial-dim variance across the child.
4. Pick the δ that MAXIMISES total variance, then commit by writing the
   trial features into `Data[]`, shifting the normalised timestamp column,
   and accumulating `m_cumShift[p] += δ`.
5. Call `MStep()` + `EStep()` so the next split trial sees the refreshed
   model.

### Pre-shifted bases (no modulo in the hot loop)

Circularly shifting a spike's waveform by δ is equivalent to index-shifting
the eigenvector row by −δ, with zero-padding at the tail.  Since PCs of
biological spike waveforms taper to zero at the window edges by
construction, the dropped/padded entry contributes negligibly at ±1 sample.

At `InitShiftProbe` time we build three contiguous basis tensors
`E_{−1}, E_0, E_{+1}` (and three shifted means `μ_{−1}, μ_0, μ_{+1}`).  At
projection time the inner loop is a single pass over `j` with three
accumulators; every raw sample is loaded exactly once and feeds all three
candidates.  No modulo, no branching — vectorises naturally on CPU, maps
one thread per (channel × PC) on GPU.

The max-variance criterion is the OPPOSITE of standard realignment.
Standard realignment minimises within-cluster variance (sharpens the
Gaussian).  Here we want to SPREAD any residual mixture structure along
whichever axis can expose it — so the next `TrySplits` call operates on
features where mixtures are maximally revealed rather than hidden.

### Why circular shift, not `.fil` re-extraction

At ±1 sample the wrap-around is negligible: on a typical waveform window
(`nSamplesPerSpike` ≈ 32–64) with `data2use` ≈ 0.9 × window, the one
wrapped sample almost always falls outside the PCA basis's support and
contributes nothing to the projection.  Avoiding `.fil` reads and in-place
`.spk` writes during the probe:

- eliminates disk I/O on the inner split loop (per-call cost ≈
  |cluster| × 3 × O(pca_projection), fully RAM-resident after a single
  `.spk` read per spike);
- makes the probe reversible in memory (no on-disk state to roll back);
- matches the specification: only `.res` (via the normalised time column
  of `Data[]`) and `.fet` (the PCA feature columns of `Data[]`) are
  updated in memory during the probe.

`.fil` re-extraction is deferred to exactly ONE final pass.

### Finalization

After all CEM completes and `SaveOutput` has written the final `.clu`:

```
K1.FinalizeShiftProbe(NbChannels, NbSamplesPerSpike);
```

This hands the accumulated `m_cumShift[]` vector to the existing
`RefeaturizeFromShifts` + `WritePhase15Checkpoint` path.
`RefeaturizeFromShifts` re-extracts from `.fil` at
`rawTs + cumShift − PeakSampleIndex` for every spike whose cumulative
shift is non-zero (entries of 0 are skipped).  `WritePhase15Checkpoint`
atomically rewrites `.spk` and `.fet` via `.pending` files, scoped to
exactly those spikes whose final committed shift is non-zero.

### Three integration points

| site                                  | semantics |
|---------------------------------------|-----------|
| `TrySplits` accept                    | After `newScore < Score` commits the split, probe both children; MStep/EStep before the next iteration. |
| `SubspaceReclusterPerChunk` accept    | Between `cls[]` update and ChunkModel mean/cov build — ensures Phase 2 matching sees refined features. |
| `RefractorySplitPerChunk` accept      | Same pattern as above. |

Each hook is gated: skip if the smaller child has < 20 spikes, or if it is
< 10% of the parent (cost/benefit not worth it for near-trivial carve-offs).

### Global ceiling

`m_shiftProbeMaxShiftAbs` (default 1) hard-clamps |cumulative shift|.
A candidate δ that would push a spike beyond this ceiling silently falls
back to the no-shift features for that spike only — other spikes in the
same cluster can still accept the winning δ.  This prevents a cluster
from iteratively wandering off over many splits.

## New public API (KK.h)

```cpp
struct ShiftProbePcaBasis { ... };
ShiftProbePcaBasis m_shiftPcaBasis;
std::vector<int>   m_cumShift;
bool               m_shiftProbeReady;
int                m_shiftProbeMaxShiftAbs;   // default 1

bool InitShiftProbe(int nChan, int nSamplesPerSpike);
void CloseShiftProbe();
int  ShiftProbeAndCommitCluster(int clusterId, int nChan, int nSamplesPerSpike);
int  ShiftProbeAndCommitSpikes (const std::vector<int>& globalSpikeIndices,
                                int nChan, int nSamplesPerSpike);
void FinalizeShiftProbe(int nChan, int nSamplesPerSpike);
```

No change to the command-line interface or the YAML schema.  Behaviour
is automatic when `Phase15Iters > 0`, `NbChannels > 0`,
`NbSamplesPerSpike > 0`, `.pca.N` exists, and `.spk.N` is readable.
Any of those missing disables the probe (runtime-equivalent to canonical
KlustaKwik).

## Build

```
cmake -B build && cmake --build build --target KlustaKwikExp_cpu
```

Both binaries can be built together by running `cmake --build build` with
no target argument.  Disable the fork entirely with:

```
cmake -B build -DNS_SKIP_KLUSTAKWIKEXP=ON
```

## Build string

The startup banner identifies this binary as:

```
KlustaKwikExp  SESSION.fet.N  [build 2026-04-22 shift-probe]
```

## GPU backends

The three-accumulator projection maps cleanly onto one thread per
(channel × PC) in a work-group of one spike.  Kernels exist for all three
supported backends:

| backend | file                   | launch            | smem used               |
|---------|------------------------|-------------------|-------------------------|
| CUDA    | `shiftprobe_cuda.cu`   | `<<<nMem, nPCA>>>`| `nChan × nSamp × int16` |
| HIP     | `shiftprobe_hip.cpp`   | `hipLaunchKernelGGL` (nMem × nPCA, wavefront-rounded) | same |
| SYCL    | `shiftprobe_sycl.cpp`  | `parallel_for` over `nd_range` with `local_accessor` | same |

Dispatch: the CPU hot path is always present.  When a GPU context is
available AND the cluster has ≥ 128 spikes, the kernel dispatcher runs
the projection on-device and `memcpy`s trial features back for the
variance-selection step.  Below that threshold the CPU path wins on
latency, so the dispatcher skips the kernel entirely.  If a GPU call
returns failure (e.g. transient alloc, smem limit) the code falls through
to the CPU path — no user-visible error.

After a commit that changes `Data[]`, a full `gpu_upload_data` re-upload
happens before the next MStep/EStep so the device view stays consistent.
A scatter-upload would be cheaper; a full upload is simpler and still
much cheaper than the EM steps that follow it.

Build strings that select a backend:

```
cmake -B build -DUSE_CUDA=ON                # Blackwell (RTX 5070 Ti etc.)
cmake -B build -DUSE_HIP=ON                 # ROCm (RDNA/CDNA)
cmake -B build -DUSE_SYCL=ON                # Intel Arc/Xe, oneAPI
```

The CPU target `KlustaKwikExp_cpu` is always built.

## File diff summary versus `src/klustakwik/`

```
 CMakeLists.txt           — project/target/lib names renamed for coexistence
 KK.h                     — +~60 lines: ShiftProbePcaBasis (pre-shifted),
                            accumulator, GPU ctx forward decl, API
 KK.cpp                   — +~400 lines: four new functions + three hook
                            sites; GPU dispatch in _AndCommitSpikes
 KlustaKwik.cpp           — +~12 lines: Init/Finalize calls + banner
 shiftprobe_cuda.cu       — new: CUDA three-accumulator kernel + host wrappers
 shiftprobe_hip.cpp       — new: HIP mirror of CUDA kernel
 shiftprobe_sycl.cpp      — new: SYCL mirror of CUDA kernel
 CHANGES.md               — this file (new)
```

The inherited canonical changelog is preserved as
`CHANGES-inherited-from-canonical.md` for historical reference.

All other files are byte-identical to their canonical counterparts.

## [2026-04-23] Parameterised shift range + merge-step probe

Three new parameters expose and extend the probe:

| parameter                    | default | range | description |
|------------------------------|---------|-------|-------------|
| `MaxShiftProbe`              | 1       | 0–5   | half-width `N` of the pre-shifted basis fan.  The probe builds `(2N+1)` pre-shifted copies of the PCA basis (one per δ ∈ {−N,…,+N}) and tests them all in a single fanned inner loop.  `N=0` disables the probe entirely (falls back to canonical KlustaKwik behaviour).  Larger `N` catches wider mis-alignments at `(2N+1)/3×` cost per probe call. |
| `ShiftProbeReplacesPhase15`  | 1       | 0/1   | When `1`, skip the canonical Phase 1.5 xcorr realignment path in `KK::CEM` — the shift-probe's accumulated `m_cumShift[]` owns Phase 1.5 and `FinalizeShiftProbe` handles the single `.fil` re-extract pass.  Set to `0` to run both (diagnostic / A-B comparison). |
| `ShiftProbeMergeProbe`       | 1       | 0/1   | When `1`, apply a min-Mahalanobis shift probe to each batch of spikes reassigned during `ConsiderDeletion` (cluster deletion / implicit merge).  Each reassigned spike picks INDEPENDENTLY the δ that best fits its receiving cluster's Gaussian.  Set to `0` to disable just the merge hook while keeping split-probe active. |

### Split-step changes

* Pre-shifted bases now exist for every δ ∈ {−N,…,+N} (was: fixed {−1, 0, +1}).
  Storage is `[cand][ch][k*data2use + j]` with `cand ∈ [0, 2N]` and `δ = cand − N`.
* CPU inner loop switched from three named accumulators (`accM/acc0/accP`)
  to a stack array `double acc[2·kShiftProbeNmax + 1]` (= 11 worst case).
  The compile-time upper bound keeps the array register-allocatable; the
  runtime loop count is known loop-invariant so compilers unroll it cleanly.
* CUDA / HIP / SYCL kernels mirror the CPU change: per-thread
  `double accs[SP_CAND_MAX]`, fanned inner loop, basis base-pointer
  reconstruction inside the loop body (cheap; index arithmetic dominated
  by the FMA chain).  All three kernels now accept `N_basis` as a launch
  parameter.
* The `m_shiftProbeMaxShiftAbs` global clamp on cumulative per-spike shift
  is set equal to `MaxShiftProbe` at init (coupled by default).  Can be
  decoupled later if the user wants a wider global clamp than the basis
  fan width.

### Merge-step probe (new)

When `ShiftProbeMergeProbe != 0`, `ConsiderDeletion` now integrates the
shift probe at TWO stages: a cluster-wide DECISION probe before the merge
is accepted, and a per-spike TIGHTENER after commitment.

#### Stage 1: shift-aware merge decision

Motivation: a single biological unit can fragment into two clusters when
the spike detector triggers at different peak samples on different
instances (sample 14 on some, sample 15 on others).  The mean waveforms
are then temporally offset and Mahalanobis-inflated relative to each
other — canonical `ConsiderDeletion` never merges them because the loss
calculation assumes each spike stays at its current (misaligned) feature
vector.

Algorithm (`EvaluateShiftAwareMergeDecision`):

1. After the canonical min-loss candidate `victim` is selected, if
   `minLoss >= deltaPen` (i.e. the merge was rejected at δ=0) but
   `minLoss < deltaPen + 8·|deltaPen|` (not hopelessly far), load all
   of `victim`'s spike waveforms and project under every δ ∈ {−N,…,+N}
   using the same pre-shifted bases as the split probe.
2. Group victim's spikes by their `Class2[p]` destination.
3. For each destination sub-batch, compute the AGGREGATE Mahalanobis²
   at each candidate δ under that destination's Gaussian.
4. Apply a χ² threshold: accept the best non-zero δ only if it beats
   δ=0 by more than `χ²(nDims, 0.95) × sub_batch_size`.  This gives
   each spike a per-spike "improvement budget" equal to the 95th
   percentile of its null-hypothesis χ² distribution and suppresses
   noise-driven false shifts.  `χ²(nDims, 0.95)` is computed via the
   Wilson-Hilferty inverse (matching the existing inline formula at
   KK.cpp:2811 / :3521 for `MergeThresh` calibration warnings).
5. Translate the chosen-δ aggregate-Mahal² reductions into the log-
   probability units used by `DeletionLoss`:
   `ΔDeletionLoss(dest) = 0.5 × (agg_δ* − agg_δ=0) ≤ 0`.
6. If `minLoss + ΔDeletionLoss_total < deltaPen`, accept the merge
   and commit the planned per-destination cluster-wide shifts before
   the reassignment.

Rationale for cluster-wide (not per-spike) δ in the decision: a true
temporal duplicate gives a consistent winning δ across all its spikes
— that consistency is the signal.  Per-spike scatter is noise and is
suppressed by the χ² threshold.

Computational cost: one .spk read per victim spike, one (2N+1)-candidate
projection, and O(M × kCand × nDims²) Cholesky forward-substitutions.
Bounded by the "hopeless candidate" gate.

#### Stage 2: per-spike post-merge tightener

Runs after stage 1's cluster-wide commit and the reassignment.  Each
reassigned spike independently picks the δ ∈ {−N,…,+N} that minimises
its Mahalanobis² to its new home, bounded by `m_shiftProbeMaxShiftAbs`.
No χ² threshold applied — residual per-spike misalignments are expected
to be small and a tightened fit is always preferable.

Combined semantics (user-selected "both" mode): decision commits the
systematic cluster-wide offset; tightener cleans up per-spike residuals.
Total committed shift per spike is bounded by the same hard clamp that
governs single-call probes.

### Selection criteria summary

|                   | split (max-var)           | merge decision (χ²-gated min-Mahal)  | merge tightener (min-Mahal)       |
|-------------------|---------------------------|--------------------------------------|-----------------------------------|
| Scope             | per cluster               | per destination sub-batch            | per spike                         |
| Objective         | **maximise** Σ var(fᵢ)   | **minimise** Σ mahal²(x,μ_dest)     | **minimise** mahal²(x,μ_dest)     |
| Threshold         | any improvement           | χ²(nDims,0.95) × sub_batch_size      | any improvement (bounded by clamp)|
| When              | inside split-accept       | inside ConsiderDeletion (before)     | inside ConsiderDeletion (after)   |
| Rationale         | expose mixture            | accept temporally-duplicated merges  | polish per-spike fits             |

### Phase 1.5 gating

Both invocation sites of the canonical xcorr realignment inside `KK::CEM`
(`KK.cpp` line ~2719 and ~3376) are now gated on
`!m_shiftProbeReady || ShiftProbeReplacesPhase15 == 0`.  When the shift-probe
is active and `ShiftProbeReplacesPhase15=1` (both defaults), the canonical
Phase 1.5 is skipped with a log message — `FinalizeShiftProbe` runs the
single re-extract pass from `.fil` at the end of the driver using the
probe's accumulated `m_cumShift[]`.  Rationale: running both paths would
let the canonical xcorr overwrite the probe's committed shifts with
newly-computed xcorr values, wasting the probe's work.

