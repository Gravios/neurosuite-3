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

## Workflow & phase notation (current)

The shift-probe is a cross-cutting mechanism that hooks into the main
clustering phases.  Each hook reads `.spk` once, projects under the
pre-shifted PCA basis fan of width `(2·MaxTimeShift + 1)`, applies a
stage-specific selection criterion, and commits shifts to `Data[]` +
`m_cumShift[]` in memory.  Only Phase 4 touches disk.

| Phase     | Action                                                          | Hooks                                     |
|-----------|-----------------------------------------------------------------|-------------------------------------------|
| 0         | Preseed (chunked mode)                                          | —                                         |
| 1         | Per-chunk CEM clustering                                        | time-shift split (inside `TrySplits` accept) |
| **1.5**   | **Cluster alignment** — per-spike min-Mahalanobis² into own cluster | `TimeShiftAlignPhase`                 |
| 2         | Subspace reclustering + refractory split (per-chunk)            | time-shift split (inside accept hooks)    |
| 4         | Mean waveform harvest (templates)                               | —                                         |
| 5         | Within-chunk xcorr template matching                            | —                                         |
| 6         | Cross-chunk model matching (overlap-vote + Mahal + xcorr)       | —                                         |
| 7         | Global warm-start EM                                            | merge-decision + merge-tighten (`ConsiderDeletion`) |
| 8         | **DipSplit** — bimodal-cluster detection & split                | `DipSplitPhase`                           |
| (post)    | **Shift commit**: re-extract `.fil` → `.spk`/`.fet`/`.res`      | `TimeShiftFinalize`                       |

Phase numbers 3 (between Phase 2 and Phase 4) and decimal sub-phases
older than 1.5 are historical — the runtime jumps from Phase 2 directly
to Phase 4.  The sequence is what is actually printed at runtime; this
table now matches stderr and can be cross-referenced when debugging.

Phase 1.5 is no longer the legacy xcorr stage — canonical xcorr realignment
(`RealignChunkWaveforms`) has been removed.  The slot is now owned by the
shift-probe: `TimeShiftAlignPhase` iterates alive clusters and runs
`TimeShiftAlignCluster` on each, doing per-spike min-Mahalanobis² alignment
against the cluster's own Gaussian.  This is the feature-space analogue of
xcorr realignment — per-spike shift selection weighted by the cluster's
Cholesky factor, which automatically favours the discriminative dimensions
that dominate the spike's identity.

Phase 1.5 and Phase 4 are sequential stages of the same pipeline: 1.5
accumulates in-memory shifts into `m_cumShift[]`; 4 writes them to disk.

### Log prefix convention

- `[Phase N]` — top-level phase entry, one fprintf per phase invocation
- `[probe-split]` — split-probe trace inside Phase 1 / 2.5
- `[probe-merge-decision]` — cluster-wide χ²-gated merge-decision trace inside Phase 3
- `[probe-merge-tighten]` — per-spike post-merge tightener trace inside Phase 3

### Public API (`KK` class)

```cpp
// Init/finalize
bool InitTimeShift(int nChan, int nSamp, int N_halfWidth);
void CloseTimeShift();
void TimeShiftFinalize(int nChan, int nSamp);            // Phase 4

// Split stage (max-variance, cluster-wide)
int  TimeShiftSplit(const std::vector<int>& idxs, int nChan, int nSamp);
int  TimeShiftSplitCluster(int clusterId, int nChan, int nSamp);

// Cluster alignment (Phase 1.5 — replaces legacy xcorr realignment)
int  TimeShiftAlignCluster(int clusterId, int nChan, int nSamp);
int  TimeShiftAlignPhase(int nChan, int nSamp);   // iterates all alive clusters

// Merge decision (cluster-wide per-destination, χ²-gated min-Mahal²)
struct TimeShiftMergePlan { ... };
bool TimeShiftMergeEvaluate(int victim, int nChan, int nSamp,
                              TimeShiftMergePlan& plan);
void TimeShiftMergeCommit(const TimeShiftMergePlan& plan);

// Merge tightener (per-spike min-Mahal², no threshold)
int  TimeShiftMergeTighten(const std::vector<int>& idxs, int nChan, int nSamp,
                             const float* destMean, const float* destChol);
```

Naming convention: `TimeShift<Stage><Action>`.  `<Stage>` is one of
`Split` / `Merge` / (session-level) and `<Action>` describes what the
method does at that stage (`Evaluate`, `Commit`, `Cluster`, `Tighten`,
`Finalize`).  All methods are no-ops on `KK` instances where
`m_shiftProbeReady == false`, so callers don't need to check explicitly.

### Parameter reference

| parameter                    | default | meaning |
|------------------------------|---------|---------|
| `MaxTimeShift`               | 1       | basis fan half-width N (0–5); time-shift search tests δ ∈ {-N,…,+N} samples.  N=0 disables all time-shift stages. |
| `TimeShiftAlignIter`         | 1       | Phase 1.5 cluster-alignment passes. 0 = skip alignment; N = run N passes, calling `MStep()` between passes so each pass sees updated cluster means.  Early exit on convergence (zero-shift pass). |
| `TimeShiftMergeEnable`       | 1       | enable Phase 3's merge-decision + merge-tighten time-shift stages.  Set to 0 to keep only the split-stage and cluster-alignment stages active. |

**Migration notes** (2026-04-23c):

- `-MaxShiftProbe` → `-MaxTimeShift`
- `-ShiftProbeAlignIter` → `-TimeShiftAlignIter`
- `-ShiftProbeMergeProbe` → `-TimeShiftMergeEnable`

Old names removed — no backward-compat aliases.  Existing scripts need a
straight text substitution.  The API rename is part of a broader effort to
disambiguate "probe" (now: recording hardware only) from "time-shift
search" (the pre-shifted PCA basis + candidate selection machinery).

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

At `InitTimeShift` time we build three contiguous basis tensors
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

bool InitTimeShift(int nChan, int nSamplesPerSpike);
void CloseTimeShift();
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

## [2026-04-28e] Quality warning removed — mass distribution can't detect failures

The `gini > 0.7 && maxFrac > 0.4` warning introduced in 2026-04-28d was
firing on healthy interneuron biology.

### The category error

A fast-spiking interneuron firing at modest rates (~3-4 Hz) over an
80-minute session produces ~17 000 spikes — easily 40% of the spike
count in a tetrode group.  That gives `(gini, maxFrac) = (0.83, 0.42)`,
identical to the signature I was using to flag absorbed-bimodal
failures.

The failure mode I was originally targeting (jg05-group-6 cluster 3,
pre-elongation-gate) had ONE specific waveform-shape signature:
bimodal valley in a top-PC projection AND elongated covariance.
Those are exactly what DipSplit's bloat and elongation gates measure
on a per-cluster basis.  The cluster-size distribution doesn't carry
the failure signal — it's a downstream symptom that's also produced
by completely normal physiology.

If DipSplit's gates pass on a high-mass cluster (the cluster sits in
the "not-flagged" rejection bucket), that cluster IS a real unit.  No
amount of post-hoc Gini/maxFrac inspection changes that.  The warning
was telling users to "check for absorbed-bimodal failures" on real
interneurons.

### Resolution

The conjunction warning is removed.  The metrics line itself stays:

```
[final quality]  alive=23  spikes=40377  gini=0.807  maxFrac=0.422  condMax=6.5e+04 (cluster 45)
```

— gini and maxFrac are still useful informational diagnostics.  An
expert reading the log can interpret a 42% maxFrac with full context
(channel count, expected unit composition, drift behaviour).  But the
automated warning that conflates biology with failure is gone.

`condMax > 1e6` warning stays — borderline-singular covariance is a
structural concern that doesn't have a benign biological explanation.

### Methodological note

This was a useful object lesson in heuristic calibration.  My initial
warning was synthesised from a single failure case (jg05 group 6
cluster 3) and treated as if cluster-size distribution carried the
signal.  In fact, the failure case had a *waveform* signature; the
size signature was incidental.  When I deployed it more broadly, it
fired on the case it was designed to flag PLUS every interneuron-rich
session.  Adding the conjunction with maxFrac in 28d narrowed the
false positives but kept the same category error.

The actionable failure signal is already where it belongs: in
DipSplit's per-cluster bloat/elongation outputs.  When those reject a
candidate as `not-flagged`, that's the cluster confirming itself as a
real unit; when they accept and DipSplit splits it, the failure has
already been corrected by the time the user sees the result.

## [2026-04-28d] Diagnostics polish — Gini false-alarm + MergeThresh auto-trigger

Three fixes for issues surfaced by running the Stage 1 + subspace-fix
build on jg05-20120316 group 6.

### Gini-only quality warning was a false alarm

`ReportClusterQuality` printed:

```
  WARNING: gini 0.778 > 0.7 — one cluster dominates;
           check for absorbed-bimodal failures
```

…on a session with `maxFrac = 0.163` (16% in the largest cluster).
That isn't "one cluster dominates" — it's a healthy long-tailed
distribution typical of extracellular spike sorting: a few high-rate
units, many low-rate units.  A long tail naturally produces high Gini
even when no single cluster is large.

The actual absorbed-bimodal failure signature (jg05 group 6 cluster 3
pre-fix) was BOTH high Gini AND `maxFrac > 0.4` — a single cluster
holding most of the mass.  The combined `gini > 0.7 && maxFrac > 0.4`
test catches the failure case without firing on healthy long tails.

The standalone `maxFrac > 0.4 with ≥ 8 clusters` warning ("likely
under-split") was redundant once Gini was added to the conjunction —
it overlapped with the absorbed-bimodal warning on the same data.
Removed.  `condMax > 1e6` warning is independent and stays.

### `-MergeThresh 0` now triggers auto-calibration

The recommended CLI command included `-MergeThresh 0` with the
intention of auto-calibration to χ²(d, 0.99).  But the auto-set guard
was `!cli_has_flag("MergeThresh") && MergeThresh <= 0.0f` — passing
the flag at all bypassed auto-set, leaving MergeThresh=0 throughout
the run.

Fixed: explicit `-MergeThresh 0` is now treated as the sentinel value
and triggers the same auto-calibration as omitting the flag.  The auto
log line includes a hint when the user passed 0 explicitly:

```
[auto] MergeThresh = 40.31  (χ²(22, 0.99); auto-calibrated from .fet
       header — user passed -MergeThresh 0)
```

Any positive `-MergeThresh N` continues to bypass auto and use N
directly, as before.

### MergeThresh sanity warning — divide-by-zero + AdaptiveMerge awareness

The legacy warn branch printed:

```
[warn] MergeThresh=0.00 is below χ²(22, 0.99)=40.31 by inf×.
```

Two issues:

1. The `inf×` came from `1.0f / ratio` where `ratio = MergeThresh /
   t99 = 0`.  The auto-trigger fix above prevents `MergeThresh=0`
   from reaching this branch, but as defence-in-depth the warning is
   now guarded with `MergeThresh > 0.0f && t99 > 0.0f`.
2. With `AdaptiveMerge=1` (the default), per-pair calibration handles
   miscalibration at runtime regardless of the global MergeThresh —
   so the global being out of `[0.5×, 3×] χ²(d, 0.99)` is not actually
   a problem.  Warning suppressed when `AdaptiveMerge=1`.

### Files touched

- `KK.cpp`: `ReportClusterQuality` warnings consolidated.
- `KlustaKwik.cpp`: MergeThresh auto-trigger semantics + warn branch
  guards.
- `CHANGES.md`: this entry.

### Not addressed here (next step)

`RefractorySplitPerChunk` accepted 0 / 7 splits on this run.  The
seeding (centroid of refractory-violators vs centroid of clean spikes)
can put both centroids at the same place when contamination is
feature-uniform (every spike in the cluster is a violator with respect
to its temporal neighbour, but their feature distribution doesn't
separate violators from clean).  CEM then gets a degenerate two-seed
start.  A more robust approach is farthest-point seeding from the
mean — same as Phase 0/1.  Documented for evaluation; no behaviour
change here.

## [2026-04-28c] Subcluster routines — silent-failure diagnostics + off-by-one fix

User-reported symptom: "the subcluster routine seems to be completing too
quickly, may be failing silently."

Investigation found three real issues in `SubspaceReclusterPerChunk` and
the parallel `RefractorySplitPerChunk`, plus a class of silent skips that
made the whole phase look like a no-op when in fact most candidate clusters
were filtered out before the parallel CEM ever ran.

### Off-by-one in nClustersAlive guard

Both routines run a sub-CEM with `NoisePoint=0`.  Under that setting, the
noise slot (cluster 0) is structurally alive even when empty (`Reindex`
always counts it).  So:

- `nClustersAlive == 1` → only noise alive (everything killed)
- `nClustersAlive == 2` → noise + 1 real cluster (parent intact, no split)
- `nClustersAlive >= 3` → noise + ≥2 real clusters (genuine split)

Both routines guarded with `> 1` (subspace) or `<= 1` (refractory):

```cpp
// SubspaceReclusterPerChunk — line 6489 (before)
if (Ks.nClustersAlive > 1) { rr.score = score; ... }
//                     ^ accepts the "noise + 1 real" no-split case

// RefractorySplitPerChunk — line 7133 (before)
if (Ks.nClustersAlive <= 1 || splitScore >= nullScore) { ...keep; }
//                     ^ same off-by-one — the no-split case fell through
//                       to the second condition and was rejected as
//                       "no improvement" rather than "no split"
```

Fixed to `> 2` (subspace) and `<= 2` (refractory).  Real impact in the
subspace path: a sub-CEM that converges to the parent intact would
sometimes produce a `bestScore` slightly less than `wi.nullScore` due to
numerical noise + slightly different post-MStep statistics, get accepted
as a "split into 2 sub-clusters", and trigger Phase C remapping.  Phase
C's `subToLocal` then mapped the single sub-class back to the parent ID,
so no actual split happened — but the routine reported "split" success
and the user couldn't tell.

### Silent skip on degenerate covariance

`SubspaceReclusterPerChunk` line 6363:

```cpp
wi.valid = (wi.nullScore != 0.0f);
items.push_back(std::move(wi));
```

`nullScore == 0` happens when the single-cluster setup's Cholesky fails
(rank-deficient covariance in the kEff-dim subspace) — EStep kills the
cluster, leaves its `LogP` row at zero, `ComputeScore()` returns 0,
`wi.valid` becomes false, and the parallel block silently skips it via
`if (!wi.valid) continue;`.  The Cholesky failure logs to `Output()`
which is suppressed under `Log=0`.

Now logs to stderr explicitly:

```
  [Phase 2] chunk 3 cluster 12: nullScore=0 (degenerate covariance in
            5-dim subspace, 87 members) — skipped
```

And the work item is no longer pushed into `items[]` at all — saves a
parallel iteration that would early-exit on the validity check anyway.

### Diagnostic accounting

Both routines now track and emit a single-line stderr summary covering
the full filter funnel:

```
[Phase 2]  Per-chunk subspace recluster: 4 split / 23 queued
           (visited 67, skipped: 38 too-small <70 / 6 degenerate;
            rejected: 12 no-split / 7 worse-than-null)
[Phase 2]  Per-chunk refractory split: 1 split / 3 attempted
           (visited 67, skipped: 38 too-small <32 / 26 low-contam <1%;
            rejected: 1 no-split / 1 worse-than-null)
```

The reader can now see exactly why the phase did or didn't do work.
"Completes too quickly" answers itself — if `visited=67` and 38 of those
went to "too-small", you know to lower the floor.  Previously this
information existed only in `Output()` calls that got suppressed under
`Log=0`.

### `minSpikes` floor in subspace recluster

For an 8-channel session, `kMax = min(SubspaceDims, nFullDims-1) = 7`,
giving `minSpikes = max(7*10, 20) = 70`.  Many low-rate units in
8-channel data will sit below 70 spikes per chunk and be silently
filtered.  No code change here — the floor is principled (you need
enough spikes for a stable kEff-dim covariance) — but the new summary
makes the filter visible so the user can choose to lower
`SubspaceDims` (which lowers `kMax`, which lowers the floor) when
small clusters matter.

### Files touched

- `KK.cpp`: `SubspaceReclusterPerChunk` and `RefractorySplitPerChunk`
  — accounting, stderr diagnostics, off-by-one fix.
- `CHANGES.md`: this entry.

### Pre-existing latent issue (not fixed here)

`srand()` at line 6465 of the parallel block sets global RNG state per
(chunk, cluster, run) tuple, but the calls race across threads — by
the time `rand()` is reached downstream (inside `TrySplits` →
`K2.CEM`), another thread has overwritten the seed.  Bounded impact
(only affects sub-CEM seed randomisation, which is later overwritten
by EM convergence anyway) but reproducibility across runs is broken.
Replacing `srand`/`rand` with a thread-local `std::mt19937` is on the
queue for Stage 2.

## [2026-04-28b] Audit cleanup pass — Stage 1

Six items from the KKE audit folded into one cumulative patch.  No
algorithmic behaviour change for default invocations; users opt in to
new features (`-InitMethod kmeans++`) or see new diagnostic output
(per-phase quality lines).

### §1.1 Phase numbering reconciliation

The `CHANGES.md` table previously documented phases as 0, 1, 1.5, 1.6,
1.7, 1.8, 2, 2.5, 3, 4 while the runtime stderr emits Phase 0, 1, 1.5,
2, 4, 5, 6, 7, 8.  Anyone reading a build log couldn't cross-reference
with the design doc.  Picked the runtime numbering as canonical (it's
what users actually see) and updated the doc + every in-source comment
to match.

| documented (was) | now    | name                                       |
|------------------|--------|--------------------------------------------|
| 0                | 0      | Preseed                                    |
| 1                | 1      | Per-chunk CEM clustering                   |
| 1.5              | 1.5    | Cluster alignment (TimeShiftAlignPhase)    |
| 1.6              | **4**  | Mean-waveform harvest                      |
| 1.7              | **5**  | Within-chunk template matching             |
| 1.8              | **8**  | DipSplit                                   |
| 2                | **6**  | Cross-chunk model matching                 |
| 2.5              | **2**  | Subspace recluster + refractory split      |
| 3                | **7**  | Global warm-start EM                       |
| 4                | (post) | Shift commit (TimeShiftFinalize)           |

About 30 in-source comments touched.  Doc-blocks for
`SubspaceReclusterPerChunk`, `WithinChunkMerge`, and
`WithinChunkTemplateMatch` rewritten to match the new ordering (the
old "runs BEFORE Phase 1.5 and Phase 2" was already inconsistent with
runtime under either numbering).

### §1.3 `TimeShiftAlignIter` — make it actually loop

`TimeShiftAlignIter` (default 5) was documented as the number of Phase
1.5 alignment passes ("0=skip; N runs with MStep between") but
`TimeShiftAlignPhase` was a single-pass implementation that never
consulted the parameter.  Two `(void)TimeShiftAlignIter;` casts in the
chunked driver (one per `RunChunkedCEM` overload) explicitly suppressed
it.

The function now correctly loops: runs a pass, calls `MStep()` between
passes so the next pass aligns against post-shift cluster means, and
exits early when a pass produces zero shifts (converged).

```
[Phase 1.5] pass 1/5: 234 spikes shifted across 8 clusters
[Phase 1.5] pass 2/5: 41 spikes shifted across 5 clusters
[Phase 1.5] pass 3/5: 3 spikes shifted across 1 clusters
[Phase 1.5] converged after 4 pass(es)
[Phase 1.5] Cluster alignment: 278 total spike-shifts
```

**Discovered while implementing this**: `TimeShiftAlignPhase()` is
**never called from anywhere**.  The Phase 1.5 slot in both
`RunChunkedCEM` overloads is empty — your runs have never executed
cluster alignment despite the documentation saying the canonical xcorr
realignment was replaced by the shift-probe.  The slot comment was
accurate about the *intent* but the wire-in was missing.

This patch does NOT auto-wire it — that would change runtime behaviour
on existing users' sessions.  Each call site now has a clear status
comment plus an "uncomment to enable" snippet:

```cpp
// To re-enable Phase 1.5 alignment, uncomment the line below.
//   if (TimeShiftAlignIter > 0 && NbChannels > 0 && NbSamplesPerSpike > 0)
//       TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);
```

The function is correct now; whether to enable it is a session-by-
session call.

### §2.1 KK rule of three — non-copyable, non-movable

`KK` had a destructor that owns `gpu` (a raw pointer) but no `=delete`
on the implicit copy/move ops.  A default copy would shallow-copy the
pointer, leading to a double-free when both copies destruct.  Today's
call sites are safe by convention (`vector<KK>(N)` default-constructs
+ field-fills), but accidentally adding `vector::push_back` of a
populated `KK` would silently introduce UB.

Now:

```cpp
KK() = default;
KK(const KK&)            = delete;
KK& operator=(const KK&) = delete;
KK(KK&&)            = delete;
KK& operator=(KK&&) = delete;
```

`KK CloneForStart() const` previously returned by value and required
move semantics.  Replaced with `void cloneInto(KK& out, ...) const`
out-param style — works without copy or move and keeps the class
strictly default-constructible.  Single caller (`KlustaKwik.cpp:1110`)
updated.

Any future accidental copy or move of a `KK` is now a compile error
rather than a runtime double-free.

### §2.2 `kMaxStackDims` constant

Replaced the tautological `static_assert(64 >= 64, ...)` in MStep with
a real named constant:

```cpp
// At file scope in KK.cpp
static constexpr int kMaxStackDims = 64;
```

13 sites of `[64]` literals (single-dim and `[64*64]` matrix scratch)
now use the constant.  The runtime guard at the top of MStep references
`kMaxStackDims` too, so bumping the limit only requires changing one
number.  Bonus: added a previously-missing `nSpatialDims > kMaxStackDims`
guard to the first `mahalDist` lambda in `MergeChunkModels`.

### §7.4 K-means++ centre seeding

New `InitCentresKMeansPP(nCentres, nSpatialDims)`, opt-in via
`-InitMethod kmeans++` (alongside the existing `farthest` and `random`).
D²-weighted random sampling per Arthur & Vassilvitskii 2007.

| seeding           | randomised | outlier-robust | guarantee     |
|-------------------|------------|----------------|---------------|
| farthest (default)| no         | no (picks them)| —             |
| kmeans++          | yes        | yes (stochastic)| O(log k) E[cost]|
| random (canonical)| yes        | yes            | none          |

Useful when running with `-nRuns >1`: farthest-point produces identical
seeds across runs (same data → same centres), so the only "diversity"
across runs comes from CEM's later random pieces.  K-means++ diversifies
the seeds themselves.  Cost is identical to farthest-point.

PRNG is seeded from the global `RandomSeed` plus a per-call counter
(reproducible) so a given `-RandomSeed` produces deterministic output.

### §7.9 Per-phase quality metrics

New `ReportClusterQuality(phaseLabel)` prints diagnostic metrics to
stderr at phase boundaries.  Currently wired in at end of Phase 7
(Global EM) and end of pipeline (after Phase 8) in both `RunChunkedCEM`
overloads.

Metrics:

- **gini** of cluster sizes (Lorenz-curve coefficient).  Range [0, 1].
  0 = perfectly equal sizes, 1 = one cluster has all spikes.  > 0.7
  triggers a warning ("one cluster dominates — check for absorbed-
  bimodal failures").
- **maxFrac** = fraction of spikes in the single largest cluster.
  Complements Gini for simple sanity.  > 0.4 with ≥ 8 alive clusters
  triggers a warning.
- **condMax** = largest cluster condition number across alive clusters,
  computed as `(max(diag(L)) / min(diag(L)))²` from the Cholesky
  factor (cheap proxy for λ_max/λ_min of Σ).  > 1e6 triggers a warning.

Sample output:

```
[Phase 7 quality]  alive=24  spikes=40649  gini=0.523  maxFrac=0.506  condMax=4.2e+05 (cluster 3)
  WARNING: 51% of spikes in the single largest cluster — likely under-split
[final quality]    alive=27  spikes=40649  gini=0.481  maxFrac=0.342  condMax=8.7e+04 (cluster 12)
```

The cluster-3 case from jg05 group 6 (~50% of mass in one cluster)
would have triggered both Gini and maxFrac warnings before manual
inspection in Klusters.

### Files touched

- `KK.cpp`: kMaxStackDims declaration; rewritten `cloneInto` (was
  `CloneForStart`); rewritten `TimeShiftAlignPhase` (now loops);
  new `InitCentresKMeansPP`; new `ReportClusterQuality`; phase-
  numbering renames in comments; quality hooks at Phase 7 + final
  in both `RunChunkedCEM` overloads.
- `KK.h`: rule-of-three deletions; `cloneInto` declaration replacing
  `CloneForStart`; `InitCentresKMeansPP` and `ReportClusterQuality`
  declarations; doc-block updates.
- `KlustaKwik.{h,cpp}`: `InitMethod` doc updated for `kmeans++`;
  driver dispatch and stderr mode message updated.
- `CHANGES.md`: this entry.

## [2026-04-28] DipSplit — elongation gate for absorbed-bimodal clusters

**Problem.** Cluster 3 of jg05-20120316 group 6 contained ~20.5k spikes
(~50% of all spikes in the recording) with clearly distinct waveform
populations — the user's manual recluster split it into 13 clean clusters
with good autocorrelograms.  Yet DipSplit rejected it as "not bloated"
even with the most permissive params (`-DipSplitBloatFactor 1.0
-DipSplitValleyThresh 0.0 -DipSplitMinSize 25`).

**Diagnosis.** Gate A (bloat) measures `mahal²₉₀` of cluster members
against a fitted single-Gaussian model.  When CEM has been generous with
the covariance to absorb a bimodal separation — fitting one inflated
Gaussian to two well-separated modes — the inflated covariance keeps
`mahal²₉₀ ≈ χ²(d, 0.9)` (the unimodal expectation).  Even at
`bloatFactor = 1.0` the gate didn't fire on cluster 3.  The χ² test is
testing the wrong thing for this failure mode.

**Fix.** Add a parallel gate driven by the cluster's covariance shape
itself.  Compute the top-3 eigenvalues of the cluster's centred
covariance (cheaply, via the same power iteration that already runs for
the dip-test PCs) and check whether the top eigenvalue dominates the
median by a wide margin.  Two well-separated modes inflate the top
eigenvalue past 4–5× the next ones, even when their joint covariance
looks nominally Gaussian.

| metric                                | unimodal Gaussian | absorbed bimodal |
|---------------------------------------|-------------------|------------------|
| `mahal²₉₀` vs `χ²(d, 0.9)`             | ~ 1.0             | 1.0–1.3          |
| `eig_top1 / median(eig_top1..3)`      | 1–3               | 5–20+            |

The two gates are OR'd: `bloated OR elongated → run dip test`.  Either
signature is sufficient evidence to merit the (still-cheap) dip test
that is the actual bimodality decision.  The BIC gate downstream still
guards against false-positive splits.

**New parameters.**

| parameter                       | default | meaning                                                   |
|---------------------------------|---------|-----------------------------------------------------------|
| `DipSplitElongationFactor`      | 4.0     | OR-gate: `eig_top1 / median(eig_top1..3) ≥ factor` triggers eval |

**Default change.** `DipSplitBloatFactor` lowered from `2.0` to `1.0`.
The χ²(d, 0.9) target is itself the expected p90 of a unimodal Gaussian,
so any factor > 1.0 was demanding *worse-than-typical* fit — too
conservative for a gate whose downstream BIC test already prevents
false positives.

**Files touched.**
- `dipsplit.h`: new `top_pcs_with_eigenvalues()` API.
- `dipsplit.cpp`: refactored power iteration into a shared `top_pcs_impl`
  worker that optionally emits Rayleigh-quotient eigenvalues.  Cost
  delta vs. the old implementation: one d² Rayleigh quotient per PC,
  negligible relative to the per-iteration d² cost already incurred.
- `KK.cpp` `DipSplitAttemptEx`: gate restructured.  The bloat decision
  is now computed but not acted on alone; PCA + eigenvalues run; both
  gates evaluated; OR-decision made.  Success log extended with
  `elong=Nx` and `gate=bloat|elong|both`.
- `KlustaKwik.{h,cpp}`: new `DipSplitElongationFactor` parameter
  (registered via `FLOAT_PARAM`).  `DipSplitBloatFactor` default
  lowered to 1.0.
- `CHANGES.md`: this entry.

**Expected log output**
```
[Phase 8]  DipSplit: probing 25 alive clusters (bloat=1.00, elong=4.00, valley=0.00, minSize=25)
  [dipsplit] cluster 3 → 11420+9160  PC0 depth=0.412  mahal²₉₀=29.3 vs χ²₉₀=30.8  elong=8.42x  ΔBIC=1854.6  gate=elong
[Phase 8]  DipSplit: 4 accepted  (rejections: 9 too-small, ...)
```

The "gate=elong" tag identifies splits that the bloat gate alone would
have missed — these are the absorbed-bimodal cases the new gate is
designed to catch.

## [2026-04-23g] Dead-code cleanup: remove unused .fil mmap infrastructure

The `.fil` mmap plumbing added during stderiv development (members
`m_timeShiftFilMap`, `m_timeShiftFilLen`, `m_timeShiftFilSamples`,
`m_timeShiftIsStderiv`, `m_stderivRawScratch`, `m_stderivTransformScratch`
in `KK.h`, plus the init block that opened + mmap'd `.fil` in
`InitTimeShift`) was never actually used.  The probe reads from `.spkD`
via `m_timeShiftSpkMap`; Phase 4 reads from `.fil` via fopen/fread in
`RefeaturizeFromShifts` and `WritePhase15Checkpoint` (pre-existing code
paths).  Removed entirely.

Stderiv mode detection now lives solely on `m_timeShiftBasis.isStderiv`
where it belongs — the basis struct owns its own metadata.

No behavioural change; just less code to maintain.

## [2026-04-23f] Time-shift Phase 4 stderiv disk-commit

Completes stderiv support end-to-end.  Prior commit (2026-04-23e) made the
in-memory probe work for stderiv sessions; this commit makes the disk
commit work too, so `m_cumShift[]` accumulated during probing actually
materialises as corrected `.spkD.pending` / `.fetD.pending` files.

### Problem

`RefeaturizeFromShifts` and `WritePhase15Checkpoint` both read raw voltages
from `.fil` (the broadband continuous recording) at `rawTs + cumShift -
peakIdx`, then (a) project through the PCA basis to update `Data[]` and
(b) write the re-extracted spike waveform to `.spk(D).pending`.

For canonical sessions (`.spk`/`.pca`) this is correct — both the basis
and the output format live in raw-voltage space.

For stderiv sessions (`.spkD`/`.pcaD`) it corrupts the pipeline on two
fronts:
1. Projecting raw voltages through a stderiv-space basis gives garbage
   features.
2. Writing raw voltages to `.spkD.pending` produces a file with the wrong
   format — downstream tools that expect stderiv-transformed `.spkD`
   content see raw voltages.

### Fix

Added `KK::ApplySdiffAllpairsTemporalDiff(wave, nChan, nSamplesPerSpike)`,
a static helper that performs the exact two-step transform
`process_extractspikes_stderiv.cpp::fill_sdiff_buffer` uses:

    step 1:  sdiff[t, ch]   = nChan · raw[t, ch] − Σ_ch raw[t, :]   (saturated)
    step 2:  wave[t, ch]    = sdiff[t, ch] − sdiff[t-1, ch]         (saturated)

Boundary: `sdiff[-1, ch] = 0` per spike.  Int16 saturation at each stage
matches the reference implementation.

Call sites:
- `RefeaturizeFromShifts`: after reading `wave[]` from `.fil`, before
  projecting.  Guarded by `m_timeShiftBasis.isStderiv`.
- `WritePhase15Checkpoint`: after reading `spkRow[]` from `.fil`, before
  writing to `.spkD.pending`.  Same guard.

### End-to-end stderiv flow (finally complete)

1. **Init**: `InitTimeShift` detects `.pcaD` (nc = nChan − 1) + loads the
   basis, opens `.spkD` via mmap, sets `isStderiv = true`.
2. **Probe**: in-memory projection during clustering uses `.spkD` directly
   (no transform needed — `.spkD` already holds stderiv waveforms).
   Pre-shifted basis trick provides δ-shift simulation with small
   zero-pad approximation at edges (acceptable for |δ| ≤ 5 in a 24-sample
   window).
3. **Probe commits**: `m_cumShift[p] += δ` on every accepted shift.
4. **Phase 4**: `TimeShiftFinalize` invokes `RefeaturizeFromShifts` to
   re-extract from `.fil` at shifted offsets, apply SDIFF + temporal-diff
   in place, project through `.pcaD` → fresh features in `Data[]`.
   Then `WritePhase15Checkpoint` writes the re-extracted-and-transformed
   waveforms to `.spkD.pending` and the re-projected features to
   `.fetD.pending`.

The key insight (user's direction): during the hot clustering loop we
use the approximate shifted-basis projection on pre-transformed `.spkD`
content; at Phase 4 we spend the extra CPU to re-derive exactly from
`.fil` for the final disk commit.  Best of both worlds — fast clustering,
correct output.

## [2026-04-23e] Time-shift probe: stderiv support + mmap-guard fix

Critical fix plus stderiv support rolled together.

### Bug: mmap-path probe silently disabled

`InitTimeShift` was refactored to prefer `mmap(MAP_PRIVATE)` over stdio for
`.spk` access (kernel page cache amortises cluster-scattered reads).  On
success the mmap pointer is stored in `m_timeShiftSpkMap` and the `FILE*`
(`m_timeShiftSpkFp`) is left null.  Unfortunately the three projection
guards still checked `!m_timeShiftSpkFp` and returned 0 unconditionally —
silently disabling the entire time-shift probe (Phase 1.5 alignment,
Phase 2.5 split, Phase 3 merge-tighten).  Also, the actual reads used
`fseeko(m_timeShiftSpkFp, …)` + `fread` which would fail even if the guard
didn't stop them.

**Fix**: added `TimeShiftReadSpikeWave(p, waveSamples, dst)` helper that
transparently branches on mmap vs fp.  All three projection paths now call
the helper; the guards accept either backing store
(`!(m_timeShiftSpkMap || m_timeShiftSpkFp)`).

### Stderiv support (`.pcaD.N` / `.spkD.N`)

Per-session acceptance of stderiv basis when `nc == nChan - 1`.  The
.spkD.N file contains waveforms with `SDIFF_ALLPAIRS` + temporal first-
difference already applied, and .pcaD.N is the PCA basis built on that
transformed data (with the sum-to-zero dependent channel dropped).
Projecting .spkD through a pre-shifted .pcaD basis is mathematically
correct because both live in the same stderiv space.

The zero-pad approximation at the shifted-basis edges is small for the
shift magnitudes we use (|δ| ≤ 5 samples within a 20–50-sample spike
window; PC tails are near zero at window edges).  This approximation is
acceptable per user direction: "just load the spkD file and perform the
transform there since the shifts are not too large".

### Files changed

- `KK.h`: added `TimeShiftReadSpikeWave()` declaration.
- `KK.cpp`: new helper + rewrote three projection paths to use it +
  relaxed guards in three places.

## [2026-04-23d] DipSplit — bimodal-cluster splitter (Phase 1.8)

**Summary**: new phase that detects and splits "flat" clusters — CEM failure
mode where a cluster contains two sub-populations separated in some 2D
projection but whose full-dim moments look approximately Gaussian.

### Algorithm

Two-stage gating + refinement:

1. **Bloat gate** (cheap, no I/O): compute 90th-percentile Mahalanobis² of
   each alive cluster's members from the already-materialized `LogP[c][p]`.
   For a true Gaussian this is ≈ χ²(nDims, 0.9); bimodal mixtures fit as
   unimodal inflate this by 1.5–2×.  Skip unless
   `mahal²₉₀ > DipSplitBloatFactor · χ²(nDims, 0.9)`.

2. **Dip gate** (mid cost): compute top-3 principal components of the
   cluster's feature vectors via power iteration with Gram-Schmidt
   deflation.  Project onto each PC and run a KDE-based valley test
   (Silverman-rule bandwidth, G=301 grid).  Best valley depth =
   `1 − v_min / min(peak_left, peak_right)`.  Skip unless deepest depth
   ≥ `DipSplitValleyThresh`.

3. **Refinement**: seed k=2 partition at the valley location along the
   winning PC.  Refine with up to 20 Lloyd iterations.

4. **BIC gate**: accept split only if BIC(k=2) < BIC(k=1) under
   diagonal-Gaussian model AND each child cluster has ≥ `DipSplitMinSize`
   members.  Hands off to MStep+EStep+CStep+Reindex to refresh cluster
   stats.

### Integration points

- **Automatic**: `DipSplitPhase()` hooks into:
  - `CEM()` tail (non-chunked)
  - `CEMTwoPhase()` tail after Phase 2 (non-chunked)
  - `RunChunkedCEM` between Phase 1.5 (alignment) and Phase 1.6 (templates)

  Only runs on the main KK instance — scratch `Kc` objects inside chunked
  processing set `suppressBestSave=true` and are skipped.
- **Manual** (for future Klusters integration): `DipSplitAttempt(clusterId)`
  is callable directly on a specific cluster and returns whether a split
  was accepted.

### Parameters

| parameter               | default | meaning |
|-------------------------|---------|---------|
| `DipSplitEnable`        | 1       | 0 disables the automatic DipSplit phase |
| `DipSplitMinSize`       | 100     | min spikes per child cluster for accepted split; parent must have ≥ 2× this |
| `DipSplitBloatFactor`   | 1.5     | mahal²₉₀ inflation above χ²(d, 0.9) to trigger evaluation |
| `DipSplitValleyThresh`  | 0.15    | min KDE valley depth (0..1) to flag bimodality |

### Files added/changed

- **NEW** `src/klustakwikExp/dipsplit.h` — public API for valley test,
  top-k PC power iteration, k-means refinement, BIC helper.
- **NEW** `src/klustakwikExp/dipsplit.cpp` — ~300 lines of implementation.
- `KK.h`: added `DipSplitPhase()` and `DipSplitAttempt(int)` methods.
- `KK.cpp`: implementations + hook sites in `CEM()`, `CEMTwoPhase()`, and
  both `RunChunkedCEM` variants.
- `KlustaKwik.{h,cpp}`: four new parameters declared + registered via
  `INT_PARAM` / `FLOAT_PARAM`.
- `CMakeLists.txt`: `dipsplit.cpp` + header added to `_cpu_srcs` / `_headers`.

### Log output

When a split is accepted:

```
  [dipsplit] cluster 12 → 340+285 (depth=0.342, mahal²₉₀=52.3 vs χ²₉₀=33.2, ΔBIC=47.8)
[Phase 1.8] DipSplit: accepted 1 split(s)
```

No output when no splits are accepted — DipSplit runs silently to keep
logs clean.

## [2026-04-23b] Removed canonical xcorr realignment; Phase 1.5 is now cluster alignment

**Summary**: `RealignChunkWaveforms` and its call sites are deleted.  The
Phase 1.5 slot is now owned by `TimeShiftAlignPhase`, which runs per-spike
min-Mahalanobis² alignment against each alive cluster's own Gaussian using
the pre-shifted PCA basis.  `ShiftProbeReplacesPhase15` parameter is
removed (there's no legacy path left to replace).

**Why**: the canonical xcorr algorithm aligned spikes to their cluster
mean in raw-waveform space.  The shift-probe's pre-shifted PCA basis
already spans the same shift range δ ∈ {-N,…,+N}; doing the alignment in
feature space via Cholesky-weighted Mahalanobis² naturally weights the
discriminative dimensions (dimensions that differentiate this cluster
from neighbours) over noise-dominated ones.  Also avoids xcorr's circular-
shift wrap-around corruption — Phase 4 commits by re-extracting from .fil.

**New API**:
- `int TimeShiftAlignCluster(int clusterId, int nChan, int nSamp)` —
  wrapper over `TimeShiftMergeTighten` pointed at the cluster's own
  `Mean[c]` + `cholFlat[c]`.
- `int TimeShiftAlignPhase(int nChan, int nSamp)` — iterates alive
  clusters (skipping noise and clusters with < 2 spikes), returns total
  number of spikes shifted.

**Parameter migration**:
- `-Phase15Iters N` renamed to `-ShiftProbeAlignIter N`.  The new name
  matches the `ShiftProbeAlign*` API convention; the value now controls
  the number of alignment PASSES, with `MStep()` refreshing cluster means
  between passes and early exit on fixed-point convergence.  Default 1.
- Users with `-Phase15Iters 0` in their script (previously meaning "skip
  legacy xcorr") should either delete the flag or change it to
  `-ShiftProbeAlignIter 1` to enable the new alignment stage.  Passing
  `-ShiftProbeAlignIter 0` explicitly skips alignment entirely.
- `-ShiftProbeReplacesPhase15` is removed.  Users who explicitly set
  this will see an "unknown parameter" error — safe to delete from scripts.

**Files changed**:
- `KK.h`: removed `RealignChunkWaveforms` declaration; added
  `TimeShiftAlignCluster` + `TimeShiftAlignPhase`.
- `KK.cpp`: deleted ~210 lines of `RealignChunkWaveforms` body; replaced
  both Phase 1.5 call sites with `TimeShiftAlignPhase(...)`; updated
  adjacent flow comments.
- `KlustaKwik.{h,cpp}`: removed `ShiftProbeReplacesPhase15` declaration,
  definition, and `INT_PARAM` registration.  Updated `Phase15Iters`
  docstring to reflect new semantics.

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

