# kiloklustakwik — Automatic Spike Sorter

KiloKlustaKwik performs automatic spike sorting via Classification EM (CEM).
It reads the method-tagged feature file `<base>.fet.<Method>.<elec>` produced
by `ndm_pca` and writes `<base>.clu.<Method>.<elec>`. `-Method` defaults to
`standard`; the `ndm_klustakwik` wrapper passes the session's method. GPU
acceleration is available for the E-step distance computations via CUDA
(NVIDIA), HIP (AMD ROCm), or SYCL (Intel Arc/oneAPI).

The pipeline runs as a sequence of **stages** (see *Pipeline stages* below),
organised as a per-chunk block (Stage 2.x, independent per chunk) followed by
a global block (Stage 3.x, from the cross-chunk merge onward), designed for
multi-hour extracellular recordings with electrode drift. On shorter
recordings, setting `-ChunkMinutes 0` reduces the pipeline to the original
two-phase CEM.

**Contents:** [Pipeline stages](#pipeline-stages) · [Dependencies](#dependencies) · [Usage](#usage) · [File naming](#file-naming) · [Parameters](#parameters) · [Pipeline phases in detail](#pipeline-phases-in-detail) · [CEM algorithm](#cem-algorithm) · [GPU dispatch](#gpu-dispatch) · [Inline drift estimation (disabled)](#inline-drift-estimation-disabled) · [Installation](#installation)


---

## Pipeline stages

Stages are the labels printed in the run log (e.g. `[Stage 2.4] Per-cluster CEM`).
The per-chunk block (Stage 2.x) is independent per chunk; the global block
(Stage 3.x) begins at the cross-chunk merge.

| Stage | Name | Purpose |
|---|---|---|
| **1** | Chunking | Split the session into overlapping time windows; optional global preseed. |
| **2.1** | Initial chunk CEM | Per-chunk Classification-EM (`nRuns` runs; best per chunk kept). |
| **2.2** | DipSplit | Per-chunk bimodality splitter on the initial clustering. |
| **2.3** | Refractory split | Split clusters whose ISI shows refractory-period contamination. |
| **2.4** | Per-cluster CEM | Re-cluster each cluster in its own (sub)space. |
| **2.5** | DipSplit | Bimodality splitter (post per-cluster CEM). |
| **2.6** | Hull split | HDBSCAN-style mutual-reachability k-NN splitter. |
| **2.7** | Per-channel split | Amplitude/phase split per channel. |
| **2.8** | Chunk re-CEM | Warm-start re-CEM of the chunk (`2.8 m3` = residual-PCA mode). |
| **2.9** | DipSplit | Bimodality splitter (post re-CEM). |
| **2.10** | k-NN split | Klusters-faithful WaveKnn splitter. |
| **2.11** | Split↔merge loop | Alternating WaveKnn/FullCem split + within-chunk template merge (incl. mean-waveform `harvest`, `split`, `realign` sub-steps). |
| **2.12** | Neighborhood-remix split | Pool neighboring clusters and re-split. |
| **2.13** | Variance-targeted split | Split clusters with high residual/signal variance ratio. |
| **3.1** | Cross-chunk merge | Stitch per-chunk clusters into global units (overlap-vote + edge-xcorr; `Pass2` = eigen-residual gate). |
| **3.2** | Global warm-start EM | EM over the merged global solution (`GlobalMergeIter`). |
| **3.3** | Post-merge realignment | Re-tighten clusters / energy-COM after the merge. |
| **3.4** | Mean-subtraction merge | Residual-template merge of similar global clusters. |
| **3.5** | Shift commit / re-extract | Commit per-spike shifts to disk (`commit`) and re-extract `.spk` from `.fil` (`reextract`). Runs only when time-shift alignment is enabled (off by default). |
| **3.6** | Per-spike realignment | Klusters-faithful per-spike realignment (off by default; `-KlustersRealignEnable 1`). |
| **3.7** | CCG refractory-dip merge | Merge over-splits whose cross-correlogram shows a shared refractory hole. |

`[Align]` (un-numbered) marks the legacy per-spike time-shift alignment blocks
(off by default; superseded by `ndm_alignspikes`).

<details><summary>Old run-log label → new stage (banners renumbered)</summary>

`[Phase 0]`→`[Stage 1]` · `[Phase 1]`→`[Stage 2.1]` · `[Phase 1a]`→`[Align]` ·
`[Phase 1b]`→`[Stage 2.2]` · `[Phase 2]`→`[Stage 2.3]` · `[Phase 2a]`→`[Stage 2.4]` ·
`[DipSplit]` (unchanged; runs at 2.2/2.5/2.9) · `[Phase 2b]`→`[Stage 2.8]` ·
`[Phase 2b m3]`→`[Stage 2.8 m3]` · `[Phase 2b.5]`→`[Stage 2.10]` ·
`[Phase 3]`→`[Stage 2.11 harvest]` · `[Phase 4]`→`[Stage 2.11]` ·
`[Phase 4b]`→`[Stage 2.11 split]` · `[Phase 4 realign]`→`[Stage 2.11 realign]` ·
`[Phase 4c]`→`[Stage 2.12]` · `[Phase 8]`→`[Stage 2.13]` ·
`[Phase 4→5]`/`[Phase 5]`→`[Stage 3.1]` · `[Phase 6e]`→`[Stage 3.1 Pass2]` ·
`[Phase 6]`→`[Stage 3.2]` · `[Phase 6a]`→`[Stage 3.3]` · `[Phase 6b]`→`[Stage 3.4]` ·
`[Phase 6c]`→`[Stage 3.5 commit]` · `[Phase 6d]`→`[Stage 3.5 reextract]` ·
`[Phase 7c]`→`[Stage 3.6]` · `[Phase 9 CCG]`→`[Stage 3.7 CCG]`

</details>


## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| C++20 compiler | Build | Yes |
| CMake ≥ 3.22 | Build | Yes |
| OpenMP | Parallel chunk / run / item processing and CPU fallback | Strongly recommended |
| yaml-cpp | Session-YAML parsing (`KlustaKwikYaml.cpp`) | Yes |
| CUDA Toolkit ≥ 11 (≥ 12.8 for sm_120 / Blackwell) | NVIDIA GPU acceleration | Optional |
| ROCm / HIP SDK ≥ 5.0 | AMD GPU acceleration | Optional |
| Intel oneAPI Base Toolkit ≥ 2023.1 | Intel Arc/Xe GPU acceleration | Optional |

One GPU backend is compiled per build. When none of the backend flags is
set, CMake auto-detects in priority order CUDA > HIP > SYCL. Every build also
produces `KiloKlustaKwik_cpu`, a CPU-only binary.

---

## Usage

```
KiloKlustaKwik FileBase ElecNo [options]
```

`FileBase`, `-Method` (default `standard`) and `ElecNo` identify the input
`FileBase.fet.<Method>.ElecNo` (see [File naming](#file-naming)).
`NbChannels`, `NbSamplesPerSpike`, `PeakSampleIndex`, `NbTotalChannels` and
`SamplingRate` are read from `FileBase.yaml`; pass them as options only to
override the YAML or to run without one. Running `KiloKlustaKwik` with fewer
than two arguments prints every parameter with its default.

Options are `-Name value` pairs. The parser ignores option names it does not
know, so a misspelt option silently has no effect; check the spelling against
the default-parameter listing.

### Examples

```bash
# Two-phase farthest-point mode (no chunking)
KiloKlustaKwik session 1 -MinClusters 2 -MaxClusters 12 -ChunkMinutes 0

# Chunked mode (the default) with explicit chunk sizes
KiloKlustaKwik session 1 -Method stderiv \
    -ChunkMinutes 7 -ChunkOverlapMinutes 4 -ChunkPreseedFraction 0.1

# Drift-adaptive chunk boundaries written by ndm_applydrift
KiloKlustaKwik session 1 -ChunkFile session.chunks.1

# Curate an existing sort (reassign + split + merge)
KiloKlustaKwik session 1 -RefineExisting session.clu.standard.1 -RefineMode full

# Chunked 80-minute silicon-probe session: 3 runs per chunk, fixed merge
# threshold, global warm-start EM enabled
KiloKlustaKwik jg05-20120316 7 \
    -ChunkMinutes 10 -ChunkOverlapMinutes 2 \
    -nRuns 3 -TemplateMatchIters 10 -CrossChunkTemplateScore 0.90 \
    -MergeThresh 42.0 -GlobalMergeIter 50 -TimeMergeIter 50
```

### Output files

| File | Contents |
|---|---|
| `FileBase.clu.<Method>.ElecNo` | Cluster id per spike ([format](../ndmanager-plugins/formats/clu.md)). Ids are renumbered without gaps: the noise class is written as `1`, the other clusters as `2, 3, …`. `0` is never written. |
| `FileBase.klg.ElecNo` | Run log (only with `-Log 1`) |
| `FileBase.model.ElecNo` | Gaussian model parameters (only with `-fSaveModel 1`) |
| `FileBase.{spk,fet}.<Method>.ElecNo.pending` | Stage 3.5 shift-commit checkpoint, renamed over the resolved `.spk` / `.fet` on success. Written only when time-shift alignment is enabled (off by default). |

---

## File naming

KiloKlustaKwik resolves its inputs through the shared chain-of-custody policy
(`custody.hpp`, see [naming.md](../ndmanager-plugins/formats/naming.md)):

| File | Resolution |
|---|---|
| `.fet`, `.pca`, `.clu` | Method-specific, strict: `<base>.<type>.<Method>.<elec>`. There is no fallback to an untagged or `.fetD` name; a missing file stops the run with `Could not open file …`. |
| `.spk`, `.res` | Shared: the first that exists of `<Method>`, the method's waveform token (for a `_D…` feature-space method), `standard`, then the untagged `<base>.<type>.<elec>`. |
| `.fil`, `.yaml` | Session-wide: `<base>.fil`, `<base>.yaml`. |

The output `.clu` is written as `<base>.clu.<Method>.<elec>`, so groups sorted
with different methods coexist in one session. The startup banner prints the
`.fet` path it loaded, with `(non-standard method)` when `-Method` is not
`standard`.

---

## Parameters

The tables list the main options. Every option and its default is printed by
running `KiloKlustaKwik` with no arguments.

> **Empirical priors.** `-PriorFile PATH` loads a per-probe prior YAML
> (built by `kk_build_prior.py` from accumulated curation logs) that
> supplies `MinClusters`, `MaxClusters`, `MergeThresh` and `PenaltyMix`
> (and preseed centres). A parameter given on the command line wins over the
> prior; the prior wins over the built-in defaults. The `ndm_*` orchestrators
> find the prior with `kk_resolve_prior.py` and pass it. See
> [`../workflows/empirical-priors.md`](../workflows/empirical-priors.md).

### Input and clustering control

| Parameter | Default | Description |
|---|---|---|
| `-Method STR` | `standard` | Method token of the input/output files (`standard`, `stderiv`, `sdiff`, …; see [naming.md](../ndmanager-plugins/formats/naming.md#method-tokens)) |
| `-MinClusters N` | `2` | Minimum number of clusters |
| `-MaxClusters N` | `200` | Maximum number of clusters. Must not exceed `MaxPossibleClusters`. |
| `-MaxPossibleClusters N` | `500` | Size of every per-cluster array (hard cap on cluster ids) |
| `-nRuns N` | `20` | Chunked mode: independent CEM runs **per chunk**; each chunk keeps its best-scoring run (`0` is treated as `1`). Not used in non-chunked mode. |
| `-nStarts N` | `1` | Non-chunked mode: random starts per K in the `MinClusters…MaxClusters` sweep |
| `-RandomSeed N` | `1` | Random seed |
| `-UseFeatures STR` | `all` | Feature selection. `all` uses every feature in the `.fet` file; a bit string (`"11111111111111110"`) skips dimensions. |
| `-PenaltyMix F` | `0.0` | Model-order penalty: `0` = AIC, `1` = BIC, in between a linear blend; clamped to [0, 1] |
| `-PriorFile PATH` | — | Empirical prior YAML (see above) |

### Convergence

| Parameter | Default | Description |
|---|---|---|
| `-MaxIter N` | `500` | Maximum EM iterations per run |
| `-SplitEvery N` | `8` | Attempt cluster splits every N iterations |
| `-FullStepEvery N` | `10` | Run a full E-step every N iterations |
| `-ChangedThresh F` | `0.05` | A full E-step is forced when more than this fraction of spikes changed class |
| `-DistThresh F` | `6.9` (ln 1000) | Log-likelihood distance beyond which a cluster is skipped in partial E-steps |
| `-SplitRecurseDepth N` | `8` | Maximum `TrySplits` recursion depth |

### Two-phase CEM (active when `ChunkMinutes = 0`)

Spatial-only EM followed by a short merge pass that reintroduces the
time dimension:

- **Phase 1** — spatial-only EM using all feature dimensions except the
  last (time). Centres are seeded by `-InitMethod` (farthest-point by
  default).
- **Phase 2** — short merge pass (`TimeMergeIter` iterations) with the time
  dimension reintroduced, so temporally drifting clusters can be joined
  without letting time dominate the spatial clustering.

| Parameter | Default | Description |
|---|---|---|
| `-TimeMergeIter N` | `100` | Phase 2 merge iterations (`0` disables two-phase mode) |
| `-InitMethod STR` | `farthest` | Initialisation: `farthest` (deterministic), `kmeans++` (D²-weighted random) or `random` |

### Chunked CEM (active when `ChunkMinutes > 0` or `-ChunkFile` is given; default)

Splits the recording into overlapping time windows, sorts each independently
(OpenMP across chunks and per-chunk runs), refines each chunk (Stages 2.2–2.13),
then stitches per-chunk clusters into global units (Stage 3.1) and refines the
global solution (Stages 3.2–3.7).

| Parameter | Default | Description |
|---|---|---|
| `-ChunkMinutes F` | `7` | Chunk duration in minutes; `0` disables chunked mode (two-phase CEM instead) |
| `-ChunkOverlapMinutes F` | `4` | Trailing overlap appended to the next chunk; seeds cross-chunk matching. `0` disables. |
| `-ChunkPreseedFraction F` | `0.1` | Fraction of all spikes used for the Stage 1 global preseed; `0` disables |
| `-ChunkFile PATH` | — | Read chunk boundaries from a [`.chunks.N`](../ndmanager-plugins/formats/chunks.md) file (written by `ndm_applydrift`) instead of uniform `ChunkMinutes` chunks |
| `-PreseedCacheFile PATH` | — | Cache file for the Stage 1 preseed centres: loaded when present and its key (dimensions, seed, fraction, …) matches, written otherwise |
| `-SamplingRate F` | from YAML | Sample rate, used to convert minutes to samples |
| `-MergeThresh F` | `0` = auto | Mahalanobis² threshold for merging. `0` auto-calibrates to χ²(nDims, 0.99) from the `.fet` dimensionality (printed as `[auto] MergeThresh = …`); a positive value is used as given. |
| `-GlobalMergeIter N` | `0` | Stage 3.2 global warm-start EM iterations; `0` skips the stage |

### Template matching

| Parameter | Default | Description |
|---|---|---|
| `-TemplateMatchIters N` | `10` | Maximum within-chunk template-merge iterations (Stage 2.11) |
| `-TemplateMatchScore F` | `0.85` | Minimum waveform cross-correlation for a within-chunk template merge |
| `-CrossChunkTemplateScore F` | `0.80` | Minimum waveform cross-correlation for a cross-chunk match (Stage 3.1) |

### Per-cluster (subspace) re-clustering

Each per-chunk cluster is re-clustered by its own CEM (Stage 2.4), to split
over-merged clusters whose separation the chunk-level feature space does not
capture.

| Parameter | Default | Description |
|---|---|---|
| `-SubspaceRecluster N` | `1` | Enable (`1`) or disable (`0`) per-cluster re-clustering |
| `-SubspaceDims N` | `0` | `0` = use all spatial feature dimensions. `N > 0` = use the top-N spatial features ranked by within-cluster (2.4) or within-chunk (2.8) variance. |

### Output

| Parameter | Default | Description |
|---|---|---|
| `-SaveIntermediates N` | `0` | `0` = write the `.clu` once at the end; `1` = also write it at intermediate points |
| `-Log N` | `0` | Write the `.klg` log file |
| `-Screen N` | `0` | Print progress to stdout |
| `-Verbose N` | `0` | Verbose per-iteration output |
| `-fSaveModel N` | `0` | Write the `.model` file with Gaussian parameters (debugging) |

### Seeding and refining an existing sort

| Parameter | Default | Description |
|---|---|---|
| `-StartCluFile PATH` | — | Run an initial CEM from the clusters of an existing `.clu` (give the full path, e.g. `session.clu.standard.1`) |
| `-RefineExisting PATH` | — | Curate an existing `.clu` instead of clustering from scratch: its clusters become full-Gaussian models, then reassign → split → merge. With `-ChunkFile`, the merge step is drift-aware (clusters concentrated in disjoint chunks merge more readily). |
| `-RefineMode STR` | `full` | `reassign`, `split`, `merge`, `full` or `off` |
| `-RefineIters N` | `5` | CEM iterations in the reassign step |
| `-RefineMergeThresh F` | `0` | Merge threshold; `0` = inherit `MergeThresh` (auto χ²) |
| `-RefineSplitMinDepth F` | `0.4` | DipSplit valley-depth threshold in the split step |
| `-RefineLockNoiseClu N` | `1` | Never modify clusters 0 and 1 |

The `KlustaKwikRefine` script (installed with the binary) wraps
`-RefineExisting`: `KlustaKwikRefine <session> <group> [--method M] [--mode …]
[--iters N] [--chunks PATH] [--dry-run] [-- extra KK flags]`. The seed is
`<session>.clu.<method>.<group>` (method default `standard`), the file
KiloKlustaKwik `-Method <method>` writes the result to; the wrapper backs it up
to `.bak-refine` first and picks up `<session>.chunks.<group>` automatically.
Run it with no arguments for the full usage.

---

## Pipeline phases in detail

> **Note:** the `Phase N` numbers in this section are the original *coarse*
> conceptual grouping and predate the stage renumbering above. For the labels
> printed in the run log, see the **Pipeline stages** table.

### Phase 0 — preseed (Stage 1, chunked mode)

`ChunkPreseedFraction` of all spikes are sampled globally and fed
through a fast CEM to produce a pre-seeded cluster set. Each per-chunk
CEM then starts from this global seed rather than a farthest-point
initialisation over local spikes, which helps align per-chunk cluster
identities at chunk boundaries. `-PreseedCacheFile` caches the result
between runs.

Set `ChunkPreseedFraction 0` to disable.

### Phase 1 — per-chunk CEM (Stage 2.1)

Each chunk runs an independent CEM in a self-contained `KK`
sub-object. The parallel axis is `(chunk × run)`: with `nRuns = 3`, each
chunk produces 3 candidate solutions, and the best-scoring one becomes
that chunk's contribution. Chunk sub-objects own their own Cholesky
storage and do not share state with the global sort object.

Per-chunk cluster IDs are packed as `chunkIdx × MaxPossibleClusters +
localClusterId`. Raising `MaxPossibleClusters` increases the per-chunk
ID range but also raises memory use proportionally.

### Time-shift alignment and shift commit (Stage 3.5, off by default)

In-pipeline per-spike time-shift alignment is disabled by default
(`ndm_alignspikes`, run before clustering, replaces it); `-MaxTimeShift` and
`-TimeShiftAlignIter` are ignored unless one of the
`-TimeShiftAlignAfterPhase*` / `-TimeShiftAlignPostMerge` switches is set.
When alignment ran and shifted spikes, `TimeShiftFinalize`
(`KK_timeshift.cpp`) commits the shifts at the end of the run (Stage 3.5
commit): it re-extracts the shifted waveforms from `.fil`, reprojects them
through the `.pca.<Method>.N` eigenvectors, writes `.pending` files that are
renamed over the resolved `.spk` / `.fet`, then re-extracts `.spk` from
`.fil` (Stage 3.5 reextract).

### Phase 2 — cross-chunk merge (Stage 3.1)

Per-chunk cluster models are matched across chunk boundaries using
(i) vote matching via shared overlap-window spikes, (ii) Mahalanobis
distance, and (iii) normalised cross-correlation of the per-cluster mean
waveforms (`CrossChunkTemplateScore`). A match causes a `Union()` of the
two packed cluster IDs.

### Phase 3 — global warm-start EM (Stage 3.2)

After the unions have been applied, an EM run on the merged solution
refines Gaussian parameters globally. `GlobalMergeIter` bounds the
iteration count; the default `0` skips this stage.

### Phase 4 family — chunked-CEM refinement

The chunked-CEM mode adds several refinement phases that run inside and
after the main CEM loop. Brief notes here; see
[`src/kiloklustakwik/CHANGES.md`](../../src/kiloklustakwik/CHANGES.md) and the
[Phase 8 session notes](../history/SESSION_SUMMARY_phase8_klusters_automerge.md) for the
detailed rationale and CLI of each.

**Phase 4b — alternating split / merge (Stage 2.11)**
Alternates split passes (`WaveKnnSplitPerChunk` and `FullCemSplitPerChunk`)
with within-chunk template-matching merges. Off by default; enable with
`-AlternatingSplitMergeEnable 1`. The `QualityWeightedSplit` dispatcher
(`-QualityWeightedSplitEnable 1`, off by default) routes each candidate
source cluster to FullCem (high ISI contamination) or WaveKnn (high
waveform variance).

**Phase 4c — neighborhood remix split (Stage 2.12)**
Optionally runs one to a few iterations of a neighborhood-pooled split:
each random source is pooled with its N nearest clusters, the
super-cluster is re-split via FullCEM, and a tightness mask
(`ρ = V_res / P_sig` on signal-support channels) skips clusters that are
already tight. Enable with `-Phase4cRemixEnable 1`. Best suited to
sessions with refractory-contaminated multi-unit clusters that have real
Gaussian-mixture sub-structure.

**Phase 8 — variance-targeted knn-split (Stage 2.13)**
Iterates the WaveKnn split machinery on the high-variance clusters only —
those whose ρ (the same metric Phase 4c uses) exceeds
`Phase8VarianceThreshold`. **FullCEM is intentionally skipped**: the
target case is diffuse clusters with no mixture modes for the EM to
find; WaveKnn redistributes their spikes to nearby reference clusters
by k-NN voting instead. Phase 4c masks low-ρ clusters; Phase 8 targets
high-ρ clusters. Enable with `-Phase8VarianceSplitEnable 1`.

**Phase 5 / 5b — cross-chunk consolidation (Stage 3.1)**
Cross-chunk merging unifies clusters across chunks via template matching
(5) and optionally mean-subtraction with drift transforms (5b,
`-CrossChunkTransformDriftEnable 1`).

**Phase 7c — klusters-faithful realignment (Stage 3.6)**
Realignment pass using klusters' median-template alignment algorithm.
Off by default; enable with `-KlustersRealignEnable 1`.

**Phase 9 — post-Phase-4 iterated realign**
`-KlustersRealignAfterPhase4 1` (off by default) interleaves realignment
with later phase iterations to reduce alignment-jitter artifacts. Heavy;
cached.

Recommended starting configuration for diffuse-cluster sessions:

```
-Phase4cRemixEnable                0    # default; wrong tool for diffuse data
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
split is kept if it improves the score. `SplitRecurseDepth` bounds how
deep the recursion can go.

---

## GPU dispatch

KiloKlustaKwik's computationally intensive steps (E-step, M-step, C-step,
`ConsiderDeletion`, `InitCentresFarthestPoint`) are GPU-parallelisable.
The `KK.h` dispatch layer maps generic `gpu_*` function calls to the
backend chosen at compile time:

| Backend | Compile flag | Auto-detect priority | Binary |
|---|---|---|---|
| CUDA | `USE_CUDA` | 1st | `KiloKlustaKwik` |
| HIP (AMD ROCm) | `USE_HIP` | 2nd | `KiloKlustaKwik` |
| SYCL (Intel Arc) | `USE_SYCL` | 3rd | `KiloKlustaKwik_sycl` (installed also as `KiloKlustaKwik`) |
| OpenMP (CPU) | always built | fallback | `KiloKlustaKwik_cpu` (installed also as `KiloKlustaKwik` in a CPU-only build) |

GPU data layout uses a transposed (dimension-major) format for coalesced
memory access: `Data[d * nPoints + p]` instead of the CPU's point-major
`Data[p * nDims + d]`.

Chunk sub-objects in chunked CEM always run on the CPU (the main sort
object holds the GPU context; chunk objects do not), so chunked mode
benefits from OpenMP across chunks and runs rather than from the GPU
directly. The global stages run on the GPU if available.

**Shared memory caveat:** the CUDA M-step mean kernel can exceed the
48 KB per-SM shared memory cap on pre-Blackwell hardware when
`MaxPossibleClusters × nDims` is large; a global-atomics fallback
kernel is always compiled. Blackwell (RTX 5000 series, sm_120) has
128 KB per SM and does not hit this limit in practice.

---

## Inline drift estimation (disabled)

`_RunInlineDriftEstimation()` in `KlustaKwik.cpp` can run
`process_estimatedrift.py` and `process_applydrift.py` as subprocesses
after the final `.clu` write, producing `SESSION.drift` and
`SESSION.chunks.N` so the next KiloKlustaKwik invocation can use adaptive
chunks (`-ChunkFile`). It needs `probeId` and `probeFile` in the session
YAML and a `.fil` file.

The call is commented out in `main()` (`KlustaKwik.cpp`); re-enabling it is
a code change. Run `ndm_estimatedrift` / `ndm_applydrift` instead.

---

## Installation

KiloKlustaKwik is built by the top-level neurosuite-3 build: follow
**[doc/INSTALL.md](../INSTALL.md)** (build options and GPU backends:
[doc/BUILD.md](../BUILD.md)). To build only KiloKlustaKwik, pass the other
`NS_SKIP_*` flags ([INSTALL.md → Installing one program only](../INSTALL.md#installing-one-program-only)).
The pages below cover the GPU-specific prerequisites and the standalone
build of `src/kiloklustakwik`.

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Linux (Ubuntu / Debian) | AMD ROCm / HIP | [install/linux-hip.md](install/linux-hip.md) |
| Linux (Ubuntu / Debian) | Intel Arc / SYCL | [install/linux-sycl.md](install/linux-sycl.md) |
| Linux — WSL2 | Intel Arc / SYCL | [install/wsl2-sycl.md](install/wsl2-sycl.md) |
| Windows (native) | NVIDIA CUDA | untested — use WSL2; [install/windows-cuda.md](install/windows-cuda.md) |
| Windows (native) | AMD HIP | untested — use WSL2; [install/windows-hip.md](install/windows-hip.md) |
| Windows (native) | Intel Arc / SYCL | untested — use WSL2; [install/windows-sycl.md](install/windows-sycl.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |
