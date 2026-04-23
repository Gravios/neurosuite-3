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
