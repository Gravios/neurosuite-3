# klustakwik — Automatic Spike Sorter

KlustaKwik performs automatic spike sorting via Classification EM (CEM).
It reads a `.fet.N` or `.fetD.N` feature file produced by `ndm_pca` or
`ndm_pca_stderiv` and writes a `.clu.N` cluster assignment file. GPU
acceleration is available for the E-step distance computations via CUDA
(NVIDIA), HIP (AMD ROCm), or SYCL (Intel Arc/oneAPI).

The pipeline is a five-phase algorithm (Phase 0–2 plus two refinement
phases 1.5 and 2.5) designed for multi-hour extracellular recordings with
electrode drift. On shorter recordings, setting `-ChunkMinutes 0` reduces
the pipeline to the original two-phase CEM.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| C++20 compiler | Build | Yes |
| CMake ≥ 3.21 | Build | Yes |
| OpenMP | Parallel chunk / run / item processing and CPU fallback | Strongly recommended |
| yaml-cpp | Session-YAML parsing (`KlustaKwikYaml`) | Yes |
| CUDA Toolkit ≥ 11 (≥ 12.8 for sm_120 / Blackwell) | NVIDIA GPU acceleration | Optional |
| ROCm / HIP SDK ≥ 5.0 | AMD GPU acceleration | Optional |
| Intel oneAPI Base Toolkit ≥ 2023.1 | Intel Arc/Xe GPU acceleration | Optional |

When none of the GPU backend flags are explicitly set, CMake auto-detects
in priority order CUDA > HIP > SYCL. Every GPU build also produces
`KlustaKwik_cpu` as a CPU-only fallback binary.

---

## Usage

```
KlustaKwik FileBase ElecNo [options]
```

`FileBase` and `ElecNo` together identify the input file `FileBase.fet.ElecNo`.
If a `FileBase.fetD.ElecNo` exists and a `FileBase.fet.ElecNo` does not,
the D variant is loaded automatically (`pickInputPath` — see the
"File extension fallback" section below).

### Examples

```bash
# Standard two-phase farthest-point mode (SamplingRate auto-detected from YAML)
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 -ChunkMinutes 0

# Three-phase chunked mode for long recordings with electrode drift (default)
KlustaKwik session 1 -MinClusters 2 -MaxClusters 12 \
    -ChunkMinutes 10 -ChunkOverlapMinutes 2 -ChunkPreseedFraction 0.08

# Resume from an existing sort (e.g., after klusters editing)
KlustaKwik session 1 -StartCluFile session.clu.1

# Chunked 80-minute silicon probe with full pipeline: 3 restarts per chunk,
# subspace reclustering inside Phase 2.5, and iterative cross-chunk template
# matching. Suppress intermediate saves.
KlustaKwik jg05-20120316 7 \
    -MinClusters 2 -MaxClusters 20 \
    -ChunkMinutes 10 -ChunkOverlapMinutes 2 \
    -nRuns 3 -SubspaceRecluster 1 -SubspaceReclusterDepth 2 \
    -TemplateMatchIters 10 -CrossChunkTemplateScore 0.90 \
    -MergeThresh 42.0 -GlobalMergeIter 50 -TimeMergeIter 50 \
    -SaveIntermediates 0
```

### Output files

| File | Contents |
|---|---|
| `FileBase.clu.ElecNo` | Cluster assignment per spike. `0` = artefact, `1` = noise/MUA, `≥ 2` = single units |
| `FileBase.klg.ElecNo` | Run log (only when `-Log 1`) |
| `FileBase.model.ElecNo` | Gaussian model parameters (omit with `-fSaveModel 0`) |
| `FileBase.fet.ElecNo.pending` / `FileBase.fetD.ElecNo.pending` | Phase 1.5 refeaturization checkpoint; atomically renamed to the original extension on success |
| `FileBase.spk.ElecNo.pending` / `FileBase.spkD.ElecNo.pending` | Phase 1.5 waveform checkpoint (same rename rule) |

---

## File extension fallback

KlustaKwik auto-detects raw vs stderiv session files. For each session
file it needs (`.spk`, `.fet`, `.pca`), it resolves the path via
`pickInputPath`, which prefers the canonical extension and falls back
to the D variant if canonical is absent:

```
pickInputPath:  .fet.N   → exists → load
                         → missing → try .fetD.N
                .spk.N   → same logic
                .pca.N   → same logic
```

The chosen variant is propagated through every subsequent open
inside KlustaKwik — `LoadData`, `RealignChunkWaveforms`,
`RefeaturizeFromShifts`, the four template-match mean-waveform harvests,
and `WritePhase15Checkpoint` — so stderiv-sorted and raw-sorted groups
can coexist in a single session without cross-contamination.

The `WritePhase15Checkpoint` `.pending` filenames are derived from the
**picked** path, so a successful rename writes back to `.spkD.N` / `.fetD.N`
on the D-variant side, not to canonical names.

`.res.N` and `.clu.N` are always read / written under canonical extensions
— they have no D variant.

Startup banner displays `(stderiv variant)` on stderr when the D variant
was chosen.

---

## Parameters

> **Empirical priors.** When KlustaKwik is invoked from
> `ndm_subcluster_unmatched`, the four headline parameters
> (`MinClusters`, `MaxClusters`, `MergeThresh`, `PenaltyMix`) can be
> taken from a per-probe prior YAML built once via `kk_build_prior.py`
> from accumulated curation logs. See
> [`../workflows/empirical-priors.md`](../workflows/empirical-priors.md)
> for the workflow. Per-shank overrides in the session yaml
> (`extraInfos`) still win over the prior; the prior wins over the
> static defaults below. Direct `KlustaKwik`-on-the-CLI invocations
> always use the static defaults — priors are only consumed by the
> `ndm_*` orchestrator scripts that call `kk_resolve_prior.py`.

### Clustering control

| Parameter | Default | Description |
|---|---|---|
| `-MinClusters N` | `2` | Minimum number of clusters to try |
| `-MaxClusters N` | `12` | Maximum number of clusters to try |
| `-MaxPossibleClusters N` | `100` | Hard upper bound on cluster count (affects cross-chunk packed-ID range; see Phase 2.5 notes below) |
| `-nStarts N` | `1` | Number of random restarts per pipeline (outer loop) |
| `-nRuns N` | `0` | Number of independent CEM restarts **per chunk** (chunked mode); each chunk keeps its best-scoring result. `0` disables (preserves original per-chunk loop). |
| `-RandomSeed N` | `1` | Random seed (use `0` to seed from time) |
| `-UseFeatures STR` | `all` | Feature selection. `all` (default) uses every feature in the `.fet` file. A bit string (`"11111111111111110"`) can skip specific dimensions. |
| `-PenaltyMix F` | `0.0` | Blend between BIC (`0`) and CEM (`1`) penalty |

### Convergence

| Parameter | Default | Description |
|---|---|---|
| `-MaxIter N` | `500` | Maximum EM iterations per run |
| `-SplitEvery N` | `50` | Attempt a cluster split every N iterations |
| `-FullStepEvery N` | `10` | Run a full E-step every N iterations (mini-batch otherwise) |
| `-ChangedThresh F` | `0.05` | Stop if fewer than this fraction of spikes change class |
| `-DistThresh F` | `6.9` | Mahalanobis distance threshold for spike-to-cluster assignment |
| `-SplitRecurseDepth N` | `1` | How many levels of recursive splitting `TrySplits` explores. `0` = no recursive splits; `1` = one level; higher values multiply the cost of `TrySplits`. |

### Two-phase CEM (active when `ChunkMinutes = 0`)

Spatial-only EM followed by a short merge pass that reintroduces the
time dimension:

- **Phase 1** — spatial-only EM using all feature dimensions except the
  last (time). Centres seeded with the farthest-point heuristic
  (`InitCentresFarthestPoint`), which gives better initial separation
  than random assignment.
- **Phase 2** — short merge pass (`TimeMergeIter` iterations) with time
  dimension reintroduced. Allows temporally drifting clusters to be
  identified without letting time dominate the spatial clustering phase.

| Parameter | Default | Description |
|---|---|---|
| `-TimeMergeIter N` | `30` | Phase 2 merge iterations (set `0` to disable two-phase mode entirely) |
| `-InitMethod STR` | `farthest` | Initialisation method (`farthest` or `random`) |

### Chunked CEM (active when `ChunkMinutes > 0`, default)

Splits the recording into temporal windows, sorts each independently
(OpenMP across chunks and per-chunk runs), optionally realigns waveforms
at chunk boundaries (Phase 1.5), optionally reclusters each cluster in a
reduced subspace (Phase 2.5), and merges per-chunk models globally using
template matching (Phase 2) plus a final EM refinement.

`SamplingRate` is auto-detected from `<FileBase>.yaml` at startup. Pass
`-SamplingRate` explicitly only when running without a YAML file or to
override it.

| Parameter | Default | Description |
|---|---|---|
| `-ChunkMinutes F` | `10` | Chunk duration in minutes; `0` disables chunked mode (falls back to two-phase) |
| `-ChunkOverlapMinutes F` | `2` | Overlap between adjacent chunks in minutes; reduces boundary artefacts and seeds cross-chunk template matching |
| `-ChunkPreseedFraction F` | `0.05` | Fraction of all spikes used for Phase 0 global preseed; `0` disables |
| `-ChunkBoundaries PATH` | — | Read adaptive chunk boundaries from a `.chunks.N` file (written by `ndm_applydrift`) instead of computing uniform chunks from `ChunkMinutes` |
| `-SamplingRate F` | auto from YAML | Recording sample rate; used to convert minutes to sample counts |
| `-MergeThresh F` | `30.0` | Maximum inter-chunk Mahalanobis distance (d²) for merging two chunk models. Calibrate as `χ²(nSpatialDims, 0.9999)` for a given group — e.g. 54.23 for 21 feature dimensions |
| `-GlobalMergeIter N` | `20` | EM iterations on the globally merged solution (Phase 3) |

### Phase 1.5: per-chunk realignment + re-featurization

After all per-chunk runs complete, Phase 1.5 realigns waveforms at the
peak sample using normalised cross-correlation against the per-cluster
mean template, re-extracts the shifted waveform from the raw `.fil` (or
circular-shifts from `.spk` if `.fil` is not present), reprojects through
the saved `.pca.N` / `.pcaD.N` eigenvectors, and writes a transactional
checkpoint to `.pending` files that are atomically renamed to the session
files on success.

This phase is always run when `ChunkMinutes > 0`. It has no user-exposed
parameters — the algorithm is controlled by session geometry (sample
rate, peak sample index, channels per group) read from the YAML.

See `src/klustakwik/CHANGES.md` for algorithm details and the
`RefeaturizeFromShifts` fall-back path.

### Phase 2: cross-chunk template matching + merge

Phase 2 iteratively matches per-chunk cluster models across chunk
boundaries using three criteria: (i) vote matching via shared
overlap-window spikes, (ii) per-dimension Mahalanobis distance, and (iii)
normalised cross-correlation of the per-cluster mean waveforms. A
cross-chunk match causes a `Union()` operation between the two packed
cluster IDs. The loop iterates until no new unions occur or
`TemplateMatchIters` is exceeded.

| Parameter | Default | Description |
|---|---|---|
| `-TemplateMatchIters N` | `10` | Max cross-chunk match iterations |
| `-TemplateMatchScore F` | `0.90` | Waveform-xcorr threshold for merging two chunk models |
| `-CrossChunkTemplateScore F` | `0.90` | Stricter xcorr threshold for non-overlapping chunk pairs (pairs that don't share spikes in an overlap region) |

### Phase 2.5: subspace reclustering (optional)

Every per-chunk cluster is projected into the top-`SubspaceDims`
eigenvectors of its own covariance, normalised by the eigenvalues
(whitening), and re-run through a shallow CEM with splits enabled. New
child clusters that survive are grafted back into `perChunkClass`.

Designed to rescue overmerged clusters from Phase 1 where the global
feature-space distance doesn't capture the true split (e.g. two units
with similar PCA footprints but different waveform shapes).

| Parameter | Default | Description |
|---|---|---|
| `-SubspaceRecluster N` | `1` | Enable (`1`) or disable (`0`) Phase 2.5 |
| `-SubspaceReclusterDepth N` | `2` | `TrySplits` recursion depth inside the sub-CEM (independent of `-SplitRecurseDepth`). `0` = one controlled level (TrySplits fires once, split trials are non-recursive). |
| `-SubspaceDims N` | `3` | Number of top eigenvectors used for the whitened subspace |

### Output

| Parameter | Default | Description |
|---|---|---|
| `-SaveIntermediates N` | `1` | Write `.clu` whenever a new best score is found. Set `0` to suppress mid-run writes and produce only a final file |
| `-Log N` | `0` | Write `.klg` log file |
| `-Screen N` | `0` | Print progress to stdout |
| `-Verbose N` | `0` | Verbose per-iteration output |
| `-fSaveModel N` | `1` | Write `.model` file with Gaussian parameters |

### Seeding

| Parameter | Default | Description |
|---|---|---|
| `-StartCluFile PATH` | — | Initialise from an existing `.clu` file. Cluster IDs are renumbered; `0` and `1` retain their noise/artefact meaning. |

---

## Pipeline phases in detail

### Phase 0 — preseed (chunked mode, optional)

`ChunkPreseedFraction` of all spikes are sampled globally and fed
through a fast CEM to produce a pre-seeded cluster set. Each per-chunk
Phase 1 then starts from this global seed rather than a farthest-point
initialisation over local spikes, which helps align per-chunk cluster
identities at chunk boundaries.

Set `ChunkPreseedFraction 0` to disable.

### Phase 1 — per-chunk two-phase CEM

Each chunk runs an independent two-phase CEM in a self-contained `KK`
sub-object. The parallel axis is `(chunk × run)`: if `nRuns = 3`, each
chunk produces 3 candidate solutions, and the best-scoring one becomes
that chunk's contribution. Chunk sub-objects own their own Cholesky
storage and do not share state with the global sort object, so the
parallel execution is data-race-free.

Per-chunk cluster IDs are packed as `chunkIdx × MaxPossibleClusters +
localClusterId`. Raising `MaxPossibleClusters` increases the per-chunk
ID range but also raises memory use proportionally; if you see "packed
ID overflow" warnings in verbose logs, raise `MaxPossibleClusters`.

### Phase 1.5 — realignment + refeaturize

See the section above under "Chunked CEM".

### Phase 2 — cross-chunk merge via template matching

Three-criterion iterative merge loop (votes + Mahalanobis + xcorr),
described above. `Union()` operations happen inside the outer
`TemplateMatchIters` loop; the loop exits early when no new unions
occur.

### Phase 2.5 — subspace reclustering (optional)

See the section above.

### Phase 3 — global warm-start EM

After Phase 2's unions have been applied, a final EM run on the merged
solution refines Gaussian parameters globally. `GlobalMergeIter` bounds
the iteration count.

### Phase 4 family — chunked-CEM refinement (patches 0040–0070)

The chunked-CEM mode (`ChunkMinutes > 0`, default) adds several
refinement phases that run inside and after the main CEM loop. Brief
notes here; see `src/klustakwikExp/CHANGES.md` and
`SESSION_SUMMARY_phase8_klusters_automerge.md` (top-level) for the
detailed rationale and CLI of each.

**Phase 4b — alternating split / merge**
Inside the CEM loop, alternates split passes (`WaveKnnSplitPerChunk`
and `FullCemSplitPerChunk`) with within-chunk template-matching merges.
Default on (`-AlternatingSplitMergeEnable 1`). The `QualityWeightedSplit`
dispatcher (`-QualityWeightedSplitEnable 1`) routes each candidate
source cluster to FullCem (high ISI contamination) or WaveKnn (high
waveform variance) so each splitter gets the work it's best at.

**Phase 4c — neighborhood remix split**
After Phase 4 converges, optionally runs one to a few iterations of a
neighborhood-pooled split: each random source is pooled with its N
nearest clusters, the super-cluster is re-split via FullCEM, and a
tightness mask (`ρ = V_res / P_sig` on signal-support channels) skips
clusters that are already tight. Enable with `-Phase4cRemixEnable 1`.
Best suited to sessions with refractory-contaminated multi-unit
clusters that have real Gaussian-mixture sub-structure.

**Phase 8 — variance-targeted knn-split** *(new in patch 0070,
2026-05-29)*
Iterates Phase 4b's WaveKnn-split machinery on the high-variance
clusters only — those whose ρ (the same metric Phase 4c uses) exceeds
`Phase8VarianceThreshold`. **FullCEM is intentionally skipped**: the
target case is diffuse clusters with no mixture modes for the EM to
find; WaveKnn redistributes their spikes to nearby reference clusters
by k-NN voting instead. Phase 4c masks low-ρ clusters; Phase 8 targets
high-ρ clusters — same metric, inverted eligibility, complementary
roles. Enable with `-Phase8VarianceSplitEnable 1`. For diffuse-cluster
sessions (the user's typical case), Phase 8 is the right tool;
Phase 4c can be disabled to skip its wasted FullCEM cycles.

**Phase 5 / 5b — cross-chunk consolidation**
After per-chunk refinement, cross-chunk merging unifies clusters across
chunks via template matching (5) and optionally mean-subtraction with
drift transforms (5b, `-CrossChunkTransformDriftEnable 1`).

**Phase 7c — klusters-faithful realignment**
Final realignment pass using klusters' median-template alignment
algorithm. `-KlustersRealignEnable 1` (default on).

**Phase 9 — post-Phase-4 iterated realign**
`-KlustersRealignAfterPhase4 1` interleaves realignment with later
phase iterations to reduce alignment-jitter artifacts. Heavy; cached.

Recommended starting configuration for diffuse-cluster sessions:

```
-Phase4cRemixEnable                0    # skip — wrong tool for diffuse data
-Phase8VarianceSplitEnable         1
-Phase8VarianceSplitMaxIters       3
-Phase8VarianceThreshold           0.10
-Phase8VarianceSignalChannelFraction 0.1
-Phase8VarianceMinClusterSize      0
```

---

## CEM algorithm

Classification EM extends standard EM by adding a hard classification
step (C-step) after each E-step. The penalty term penalises models with
too many clusters, providing automatic model-order selection without
requiring an exact cluster count.

Inner loop: `M-step → E-step → C-step → ConsiderDeletion` until
convergence. `ConsiderDeletion` removes clusters that contribute too
little to the score, respecting the `MinClusters` floor.

`TrySplits()` is called every `SplitEvery` iterations to attempt
splitting poorly-fitting clusters. Split candidates are evaluated by
running a mini-CEM on just the spikes of the candidate cluster; the
split is kept if it improves the score. `SplitRecurseDepth` controls
how deep the recursion can go; at the saturation depth, `TrySplits`
falls back to a single non-recursive split attempt.

---

## GPU dispatch

KlustaKwik's computationally intensive steps (E-step, M-step, C-step,
`ConsiderDeletion`, `InitCentresFarthestPoint`) are GPU-parallelisable.
The `KK.h` dispatch layer maps generic `gpu_*` function calls to the
active backend at compile time:

| Backend | Compile flag | Priority |
|---|---|---|
| CUDA | `USE_CUDA` | 1st |
| SYCL (Intel Arc) | `USE_SYCL` | 2nd |
| HIP (AMD ROCm) | `USE_HIP` | 3rd |
| OpenMP (CPU) | always built | fallback |

GPU data layout uses a transposed (dimension-major) format for coalesced
memory access: `Data[d * nPoints + p]` instead of the CPU's point-major
`Data[p * nDims + d]`.

Chunk sub-objects in chunked CEM always run on the CPU (the main sort
object holds the GPU context; chunk objects do not), so chunked mode
benefits from OpenMP across chunks and runs rather than from the GPU
directly. Phase 3 (the global merge) runs on the GPU if available.

**Shared memory caveat:** the CUDA M-step mean kernel can exceed the
48 KB per-SM shared memory cap on pre-Blackwell hardware when
`MaxPossibleClusters × nDims` is large; a global-atomics fallback
kernel is always compiled. Blackwell (RTX 5000 series, sm_120) has
128 KB per SM and does not hit this limit in practice.

---

## Inline drift estimation (disabled by default)

If the session YAML contains both `probeId` and `probeFile` fields in
the spike-detection group, and a `.fil` file is present,
`_RunInlineDriftEstimation()` can be invoked to run
`process_estimatedrift.py` and `process_applydrift.py` as subprocesses
after the final `.clu` write. Produces `SESSION.drift` and
`SESSION.chunks.N` so the next KlustaKwik invocation can use adaptive
chunks.

Not called by default — registered as a post-write hook that can be
enabled in `KlustaKwikYaml.cpp`. Intended for long chronic recordings
where drift is severe enough to make uniform chunk boundaries
counter-productive.

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Linux (Ubuntu / Debian) | AMD ROCm / HIP | [install/linux-hip.md](install/linux-hip.md) |
| Linux (Ubuntu / Debian) | Intel Arc / SYCL | [install/linux-sycl.md](install/linux-sycl.md) |
| Linux — WSL2 | Intel Arc / SYCL | [install/wsl2-sycl.md](install/wsl2-sycl.md) |
| Windows (native) | NVIDIA CUDA | [install/windows-cuda.md](install/windows-cuda.md) |
| Windows (native) | AMD HIP | [install/windows-hip.md](install/windows-hip.md) |
| Windows (native) | Intel Arc / SYCL | [install/windows-sycl.md](install/windows-sycl.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |
