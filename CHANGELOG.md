# CHANGES — neurosuite-3

Top-level changelog for this fork, consolidating every change relative
to the original Neurosuite toolchain.  Entries are grouped by date from
most recent at top.  Deep per-topic technical notes live in
`doc/design/<topic>.md`, indexed at the end.

---

## 2026-06-07 — Klusters interactive fixes, matrix-view selection, optimisation scoping, data-model foundation

A maintenance + investigation session spanning Klusters interaction bugs, a
threading race, build-system optimisation hygiene, and the first piece of the
cluster data-model rework.

**Klusters — cluster palette Up/Down skipped rows.** Up/Down navigation chose
the geometrically nearest item by *column* first, breaking ties by row.  Because
palette items are cluster-number labels of differing widths on a free-flow grid,
their visual-rect centres do not align into columns, so a single keystroke could
jump several rows.  Reworked to pick the adjacent *row* first, then the nearest
column within it — movement is now provably one row.  Left/Right (list order)
and the wrap-around path are unchanged.

**Klusters — data race in min/max recomputation.** `Data::minMaxDimensionCalculation`
(run on `minMaxThread`) snapshotted the spike table into a block-scoped local
that was destroyed immediately, then read the *live* `spikesByCluster` in its
inner loop using offsets from the cluster-map snapshot.  A concurrent pointer
swap from a reassignment (`prepareUndo`) or undo/redo could make offsets and
data come from different tables — a stale result or an out-of-bounds read.  Now
the spike-table snapshot is held for the whole scan and read in place; the
worker runs entirely on private copies.  No other worker thread reads these
tables directly (they use the mutex-guarded dict + status-flag protocol).

**Klusters — redundant `minMaxThread` wait-loops.** The eleven
`while(!minMaxThread->wait()){}` join sites in `data.cpp` were dead loops around
a blocking `QThread::wait()` (Forever deadline never returns false); replaced
with a plain `wait()`.  Behaviour-preserving.

**Build system — `-ffast-math` / `-march=native` made explicit per-target
opt-ins.** Investigation found the global flag block in
`cmake/ZenOptimizations.cmake` applies flags via
`set(CMAKE_<lang>_FLAGS_<cfg> … CACHE STRING … FORCE)`, which is a no-op (a
normal-variable shadow keeps the default), so the intended global
`-march=native` / `-funroll-loops` / `-ffast-math` were **never reaching the
compiler** — only per-target flags (KiloKlustaKwik, a couple of plugins, CUDA)
took effect; LTO (set via `CMAKE_INTERPROCEDURAL_OPTIMIZATION`) was verified to
be genuinely active.  Rather than silently flip everything global (non-portable
binaries), `-ffast-math` and `-march=native` are now opt-in `INTERFACE` targets
(`ns_fast_math`, `ns_native_arch`): CXX-only, Release/RelWithDebInfo only, gated
on `NS_ZEN_OPT`, always defined.  `ns_fast_math` is wired into the KlustaKwik
numeric core; `ns_native_arch` into the self-contained CPU signal-processing
plugins (extractspikes/reextract/refeaturize variants, denoiseuniform,
medianfilter, drifttracker).  `process_pca*` is deliberately excluded (its math
is in GSL — recompiling the plugin does nothing — and it is reproducibility-
sensitive).  Measurement on real kernels: `-march=native` alone is a modest,
loop-shape-dependent win (~5% on the denoise kernel, ~2.5× on long FP reduction
chains); the large speedups need `-ffast-math` (reassociation), which is left
opt-in for the numerically-insensitive kernels only.  The broken global
mechanism is documented in `ROADMAP.md` and left unfixed pending a portability
decision.

**Roadmap — performance and data-model debt recorded.**  Added a
performance/optimisation section (profiling-first plan, memory-bandwidth
reduction via `float`-where-safe and pass fusion, V-cache blocking for
reuse-heavy drift/PCA math, GPU feed path, OpenMP scaling, PGO) and a Klusters
data-model debt entry.

**Klusters — `SpikeAssignment` sparse-set store (prototype, not yet wired).**
Cluster reassignment is currently O(total spikes): every edit rebuilds the whole
`spikesByCluster` table and pushes a full table copy onto the undo stack.  Added
a header-only, Qt-free `SpikeAssignment` (`clusterOf[]` label array == the `.clu`
column, packed per-cluster `members` sparse set, `slotOf[]` back-index) giving
O(spikes moved) edits and delta-based O(moved) undo/redo, plus a standalone test
(randomised equivalence vs an independent reference, invariant checks,
undo-depth trimming, and an O(moved)-vs-O(N) perf demonstration: ~50× at 5M
spikes vs a conservative full-copy baseline).  Built alongside the existing
model; integration into `Data` (replacing the table and the reader-thread
concurrency rework the in-place model requires) is deferred to the real build,
where it must be validated for bit-identical `.clu` output.

A companion `SelectionGrid2D` (also header-only, Qt-free, with a standalone
test) addresses the *other* half of reassignment latency — the O(spikes-in-
edited-clusters) region-test scan that identifies which spikes a lasso selects.
It is a uniform grid over the displayed 2D projection (rebuilt only when the
dimension pair changes) that tests only points in cells overlapping the polygon
bounding box; the test validates it returns exactly the brute-force result over
300 random polygons and shows a ~77× query speedup for a small lasso over 2M
points.  Predicate parity with `QRegion::contains` is a wiring-time concern,
documented in-header.

**Klusters — error matrix now highlights selected pairs.** `ErrorMatrixView`
already supported multi-pair selection (Ctrl-click to add/remove), but
`drawMatrix` drew no highlight, so the selected cells were invisible against the
probability colours — making it hard to see and curate pairs on a dense matrix.
Added a yellow cosmetic-pen outline for every pair in `selectedPairs`, placed by
the same world-coordinate cell layout the matrix uses (column = `pair.first`,
row = `pair.second`); a cosmetic pen keeps the outline a constant 2 px at any
zoom, and pairs whose cluster no longer exists are skipped.

**Klusters — template matrix multi-select + per-pair highlight.** The template
matrix was single-pair: a click set one `selectedA`/`selectedB` and only that
one cell was outlined.  It now accumulates a selection the same way the error
matrix does — Ctrl-click toggles a pair in/out (rebuilding the shown-cluster set
from what remains on toggle-off), a plain click replaces it with one pair — and
`drawMatrix` outlines every selected pair, not just the most recent.  This backs
the group-by-`G` workflow: the selected pairs' clusters are added to the active
view and `G` (a window-level shortcut) groups them, so no Apply step is needed
for grouping.  `selectedA`/`selectedB` still track the most-recent pair, so the
threshold slider / Apply single-pair refinement tool is unchanged.

All three matrix-view changes are syntax-checked against Qt headers but not yet
visually verified — this environment has no Qt6 build or display.

**Klusters — error matrix pan/zoom.** The error matrix descends from
`BaseFrame` (world-coordinate `ZoomWindow`) but had overridden the press handler
to repurpose clicks for pair selection, which also disabled the inherited
rubber-band zoom — leaving no way to magnify a dense matrix to see and pick
pairs.  Pan/zoom now drives the existing `ZoomWindow` without taking the
selection click, mirroring the template matrix's gestures: Ctrl + Left-drag pans
(armed on press, engaged only past a 3 px threshold so a quick Ctrl-click still
reaches the Ctrl-add selection path; the world delta is the difference of two
`viewportToWorld` conversions so the grabbed point tracks the cursor), Ctrl +
wheel zooms around the cursor, and double-click resets to the full matrix
(restoring `BaseFrame`'s standard gesture).  Plain click is unchanged.

**Build — Qt6 audit + dead Qt5 shim removal.**  Verified the whole C++ tree is
on Qt6: every `find_package` is Qt6, every link target is `Qt6::`, resources use
`qt_add_resources`, and there are no qmake `.pro`/`.pri` files.  A scan of all
585 source files found no Qt6-removed APIs in use — `QRegExp` appears only in a
comment, `setResizeMode(QListWidget::Adjust)` and `QLabel::setMargin` are the
still-valid `QListView`/`QLabel` members (checked against the Qt6 headers), every
`SkipEmptyParts` is `Qt::`-qualified, and no bare `endl` sits on a `QTextStream`.
The only Qt5 *code* artefacts were four orphaned `ECMQt4To5Porting.cmake` KDE
porting shims (klusters, libklustersshared, ndmanager, neuroscope); a whole-repo
grep confirmed nothing `include()`s them or calls their `qt5_*` macros, so they
were removed.  The remaining "Qt5" strings in the tree are provenance comments
only.  Syntax-validation for this fork now runs against real Qt6 headers (the
edited and patched Qt files — clusterPalette, data, errormatrixview,
templatematrixview — all pass a full Qt6 `-fsyntax-only` compile).

**Build — CMake pipeline audit fixes.**  A full audit of the CMake pipeline
(top-level + all subproject roots, configured end-to-end against real Qt6) drove
several fixes:

- **-march=native is now a global opt-out** (`NS_NATIVE_ARCH`, default ON)
  instead of an opt-in `ns_native_arch` target.  The project ships no binaries —
  every user builds from source — so compiling for the host ISA is the right
  default; pass `-DNS_NATIVE_ARCH=OFF` for a portable/cross build.  This also
  retired the dead `set(CMAKE_<LANG>_FLAGS_<CFG> … CACHE … FORCE)` block (a
  verified no-op) in favour of a working `add_compile_options()` (C/CXX,
  Release/RelWithDebInfo, GNU/Clang-family).  `-ffast-math` stays opt-in via
  `ns_fast_math` (it perturbs results, not just portability).
- **Optional converter plugins degrade gracefully.**  `process_extractleds`
  (FFmpeg) and `process_aomconvert` (HDF5) used hard `REQUIRED` dependency checks
  while being added unconditionally, so a single missing optional dependency
  aborted the whole monorepo configure.  They now probe quietly and skip with an
  informative message (mirroring `process_resample`).
- **nphys-data** (data-only) is declared `LANGUAGES NONE` so it no longer forces
  a compiler, and its `CPack` include is guarded to standalone builds.
- **kiloklustakwik** defers `CMAKE_CUDA_ARCHITECTURES` to the top-level value
  (incl. native auto-detect) when set, instead of overriding it with a different
  list.
- **Standardisation:** `cmake_minimum_required` unified to `3.22...3.31` across
  the top-level and every subproject root; the inert
  `ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE` keywords in the `NS_INSTALL_DEPS`
  `execute_process` calls replaced with `COMMAND_ECHO STDOUT`.

Remaining (noted, not yet changed): four targets (aomconvert, decomposecollisions,
resample, kiloklustakwik) hardcode `-march=native` directly in their own
CUDA/SYCL/ExternalProject flag lists, so they stay native even under
`NS_NATIVE_ARCH=OFF` — redundant now that native is global, to be cleaned up for
full portable-build consistency.

**Build — GPU-backend detection deduplicated.**  The CUDA architecture list, the
oneAPI/icpx search hints, the nvcc location search, and the pre-`project()` icpx
compiler auto-selection were duplicated between the monorepo top-level and the
kiloklustakwik subproject.  Factored into a new `cmake/GpuBackends.cmake` module
(`ns_gpu_find_cuda_compiler`, `ns_gpu_select_cuda_arch`,
`ns_gpu_select_sycl_compiler` + `NS_GPU_CUDA_ARCH_FALLBACK`, `NS_GPU_SYCL_HINTS`);
the two consumers shed ~110 net lines, with the logic extracted verbatim so
behaviour is unchanged.  klusters intentionally keeps its inline detection so it
remains buildable standalone against an installed libklustersshared rather than
taking a repo-layout dependency.  The GPU-enabled probe branches (nvcc found,
native-arch, icpx compiler switch) are not exercised in a toolkit-less sandbox
and should be verified on a GPU toolchain.

---

## 2026-05-16 — Cluster-mean alignment stack + .res/.spk/.fet consistency

Consolidates patches 64, 69–87, spanning several weeks of work on
post-clustering spike alignment and the disk-commit pipeline that
follows it.  Detailed per-patch entries live in
`src/kiloklustakwik/CHANGES.md`; what follows is the integrating
summary.

**Symptom.** After `KlustaKwikExp` completed on a stderiv session,
cluster mean waveforms in Klusters showed a residual ±3 sample
temporal dispersion despite the alignment phases reporting success.
The dispersion was visible in the cluster-mean view, in the
template-matrix mismatch heat-map, and (subtly) in cluster-quality
metrics.

**Root causes — three independent.**

1. **Template never centred before xcorr** (patch64).
   `RunPhase2bMode3Chunk` built the per-cluster mean template, then
   ran xcorr of each spike against it — but never shifted the mean
   so its peak landed on `PeakSampleIndex`.  xcorr therefore pushed
   every spike toward the mean's *natural* peak, which itself was
   off-`PeakSampleIndex` by whatever residual detection jitter the
   chunk's spikes happened to share.  Spikes whose required shift
   exceeded `MaxTimeShift` were silently rejected and stayed at
   their detection positions, leaving cluster spikes scattered up
   to ±5 samples around their cluster mean.

2. **Live-nudge in Klusters had a raw-source ambiguity** (patches
   69, 72).  Both Klusters' `realignSpikes` and KKExp's Phase 2b m3
   alignment block did `.fil`-or-`.dat` silent fallback when picking
   the raw source for re-extraction.  When `.spk` was extracted from
   `.fil` but `.fil` had been deleted or recomputed with different
   filter settings, the code silently re-extracted from `.dat`,
   accumulating mixed-source content in `.spk` on every nudge.
   patch69 added an RMS-based verifier to Klusters; patch72 ported
   it into KKExp.

3. **`WritePhase15Checkpoint` wrote `.spk`/`.fet` but not `.res`**
   (patch85).  When alignment generated non-zero cumulative shifts,
   the checkpoint re-extracted shifted waveforms into `.spk`
   (anchored at `rawTs + sh - PeakSampleIndex`) and wrote shifted
   timestamps into the `.fet` last column, but **never updated
   `.res` itself**.  Klusters then displayed `.spk` windows whose
   peaks aligned with each other but whose stored timestamps
   contradicted the window content — visible as the ±3-sample peak
   scatter.

**Fix path — phased.**

`RunPhase2bMode3Chunk` was rewritten as an iterative xcorr loop
(patches 71–74) that pre-aligns the template to `PeakSampleIndex`
before correlating, accumulates cumulative shifts in `m_cumShift[]`,
re-projects features against the pre-shifted PCA basis at each
iter, and emits a post-pass `.res` monotonicity warning when adjacent
spikes' new timestamps cross.  An RMS-based raw-source verifier
guards the entry to the alignment block; verification failure aborts
this chunk's alignment without polluting `m_cumShift` from prior
phases.

Two `AlignPcaCenter` modes were added on top of the xcorr loop
(patches 83–84) to address residual dispersion that xcorr alone
couldn't reach:

- Mode 1: post-xcorr PCA-centring refine pass.  After the iter loop
  converges, for each spike sweep shifts in `[-maxShift, +maxShift]`
  and minimise the squared distance from the spike's PCA
  representation to the cluster mean's PCA centroid.  Writes
  `m_cumShift[]`, so the disk commit captures it.

- Mode 2: in-memory circular-shift PCA-centring replacement.
  Replaces the entire xcorr iter loop with a circular-shift sweep,
  picking each spike's argmin distance directly.  Does NOT write
  `m_cumShift[]` — purely diagnostic / corrective in memory.

`WritePhase15Checkpoint` was made `.res`-consistent (patch85): after
rewriting `.spk` and `.fet`, the checkpoint also reads the original
`.res`, writes a `.pending` companion with `rawTs + sh` for each
shifted spike, then atomically renames it into place.  All three
files now move together via the `.*.pending` mechanism.

**Residual dispersion after patch85 — stderiv sample-0 error**
(patches 86, 87).  Even with `.res`/`.spk`/`.fet` consistent on disk,
the user's stderiv pipeline still showed sub-sample-level dispersion
in cluster means.  Tracked down to `WritePhase15Checkpoint`'s
per-spike re-extract: for the stderiv variant, it has no cross-spike
state, so the temporal first-difference at the s=0 sample of every
shifted spike uses `prev_sdiff = 0` instead of the actual previous
sample's spatial derivative.  This injects a sample-0 error in every
shifted `.spkD` window — invisible per-spike but accumulates into
cluster-mean dispersion.

Fix: add a `-R` mode to all three `process_extractspikes` variants
(patch86) that reads existing `.res.<grp>` as authoritative
timestamps instead of detecting, then mmaps `.fil` and re-extracts at
exactly `(ts - timeBefore)` for each.  The stderiv variant reads one
extra raw sample at `(ws - 1)` to compute the correct `prev_sdiff`
for the s=0 temporal-diff.  No cross-chunk state needed because
mmap-and-jump is per-spike independent in this read pattern.

KKExp now invokes the appropriate extractor in `-R` mode
automatically at the end of `TimeShiftFinalize` (patch87) when
non-zero shifts were committed.  Pipeline detection via
`m_timeShiftBasis.isStderiv`; sdiff users set
`KKEXP_REEXTRACT_TOOL` explicitly.  Auto-invoke is disabled by
`KKEXP_AUTO_REEXTRACT=0`.

**Klusters polish — separate track, same dates.**  patches 75, 79–82
addressed UX issues surfaced by the alignment debugging work:

- Time dimension excluded from auto-selected features in
  `slotRecluster` (patch75) — including the timestamp column as a
  clustering feature was causing drift-induced spurious splits in
  recluster.
- Mean-subtracted subdimensional recluster mode added for
  single-cluster selection (patches 76–78), exposing sub-structure
  the canonical PCA basis can't separate.  Three patches because
  the first revealed two integration bugs (`reclusteringSpikesByCluster`
  not populated, and a return-value enum collision with
  `OpenSaveCreateReturnMessage`).
- Error and Template matrices now auto-show on document open (patch79),
  removing four mouse clicks from the standard curation entry path.
- Template matrix gained `Ctrl+drag` pan + `Ctrl+wheel` zoom
  (patch80) for sessions with cluster counts that overflow the
  default cell size.
- Session YAML is now staged at the recluster temp basename
  (patch81) — `KlustaKwikYaml.cpp` was looking up `<tempBase>.yaml`
  which never exists, silently defaulting `SamplingRate` to 20000.
- A PCA-projection-energy-maximising alignment refine pass was added
  to `realignSpikes` (patch82), opt-in via a checkbox in the realign
  dialog.  Per-spike fresh `.fil` re-extract per candidate shift,
  argmax of energy in the basis.

### Files changed

| Area | Files |
|---|---|
| KKExp alignment + disk commit | `src/kiloklustakwik/KK.cpp`, `KK.h`, `KlustaKwik.cpp` |
| extractspikes `-R` mode | `src/ndmanager-plugins/src/process_extractspikes/`, `..._sdiff/`, `..._stderiv/` |
| Klusters interactions | `src/klusters/src/klusters.cpp`, `klustersdoc.cpp`/`.h`, `data.cpp`/`.h`, `templatematrixview.cpp`/`.h`, `spikerealigndialog.cpp`/`.h`, `prefgenerallayout.ui`, `configuration.cpp`/`.h` |
| Live-nudge raw-source verify | `src/klusters/src/klustersdoc.cpp` |

### Environment knobs introduced

| Variable | Effect | Default |
|---|---|---|
| `KKEXP_AUTO_REEXTRACT` | `0` disables patch87 auto-invoke | unset (= enabled) |
| `KKEXP_REEXTRACT_TOOL` | Override tool binary name (e.g. `process_extractspikes_sdiff`) | auto from `isStderiv` |
| `KKEXP_REEXTRACT_SDIFF_ORDER` | Pass `-d <0..3>` to stderiv tool | `3` (ALLPAIRS) |
| `KKEXP_REEXTRACT_VERBOSE` | Pass `-v` to the tool | unset |

### CLI knobs introduced

`process_extractspikes`, `process_extractspikes_stderiv`,
`process_extractspikes_sdiff` all accept:

- `-R` — skip detection; read `<basename>.res.<grp>` and re-extract
  waveforms at those exact timestamps.  Threshold / refractory /
  peak-search args ignored in this mode.

KKExp recognises:

- `-AlignPcaCenter <0|1|2>` — 0 disables PCA-centring (xcorr only);
  1 adds it as a refine pass after xcorr (writes `m_cumShift`); 2
  replaces xcorr entirely with in-memory circular shifts (no
  `m_cumShift` writes).

### Migration notes

For sessions clustered with pre-patch85 KKExp where the dispersion
is already on disk: the cleanest recovery is to apply all of 85–87
then re-run KKExp in mode 1 (`-AlignPcaCenter 1`).  Mode 1 writes
`m_cumShift`, which triggers `WritePhase15Checkpoint`, which now
keeps `.res`/`.spk`/`.fet` consistent, which the patch87 auto-invoke
then refreshes from `.fil` via the patch86 `-R` path.  Mode 2 alone
won't fix existing-disk dispersion (it never writes to disk by
design).

### Known limitations

- After re-extracting `.spkD`, the `.pcaD` basis is still computed
  from the pre-alignment dispersed data.  For maximally-clean
  features, run `ndm_pca_stderiv` + recompute `.fetD` after the
  KKExp run; this is not chained automatically.
- The sdiff pipeline (`.spk` produced by `process_extractspikes_sdiff`)
  cannot be auto-detected by KKExp — it shares the `.spk` extension
  with the vanilla pipeline.  sdiff users must export
  `KKEXP_REEXTRACT_TOOL=process_extractspikes_sdiff`.

---

## 2026-04-27 — ndm_start root + .ndm.<n>.pipeline files + empty-graph default

Restores the editable Pipeline Designer (rolled back the toggle-only
view shipped on 2026-04-26) and finishes the design with three changes:

**1. Sticky `ndm_start` root.** Every graph has `ndm_start` pinned at
position 0. Undeletable, indegree-0, single-instance, gold "★ ROOT"
badge in the header. The legacy seven flag parameters (`wideband`,
`events`, `video`, `concatenation`, `spikes`, `lfp`, `clean`) live
on the root node's params for backward-compat fallback.

**2. Pipeline files in their own format.** The graph is no longer
serialised inside the session YAML's `programs:` block — instead it
lives in a sibling file:

```
session.yaml                             ← session schema (unchanged)
session.ndm.default.pipeline             ← auto-loaded on document open
session.ndm.best.pipeline                ← named variants via Save As
session.ndm.experimental.pipeline
```

The `.ndm.` infix namespaces our pipeline files so they don't collide
with `.pipeline` files that might be generated by other tools (Klusters,
snakemake, nextflow, etc.) in the same session directory.

New File-menu actions:
- **Save Pipeline** (Ctrl+Alt+P) — writes `<session>.ndm.default.pipeline`
- **Save Pipeline As…** (Ctrl+Alt+Shift+P) — prompts for a name, writes
  `<session>.ndm.<n>.pipeline`
- **Load Pipeline…** — file dialog filtered to `*.pipeline`

The Pipeline tab toolbar also has matching Save / Save As buttons.

Sanitisation rules: pipeline names are lowercased, whitespace collapses
to underscore, all characters outside `[a-z0-9_-]` are stripped.
Existing-file overwrite prompts on Save As.

**3. Empty-graph default.** Loading a session that doesn't already
have a saved pipeline (no `programs[0]==ndm_start` entry) now starts
with an *empty graph containing only the `ndm_start` root*, instead
of being pre-populated with every plugin from the YAML's parameter
pool. The user builds the pipeline from scratch via the palette;
their work is saved separately to `<session>.ndm.default.pipeline`.

**Bash dispatcher priority:**
1. `<template>.ndm.${NDM_PIPELINE:-default}.pipeline` (env-var
   selectable for CI)
2. In-YAML graph (`programs[0]==ndm_start`) — legacy form, kept for
   backward compat
3. Hard-coded flag-driven `do_*` sequence — original ndm_start,
   kept for sessions with no graph at all

Per-plugin failures are warnings, not aborts. A graph reference to a
plugin not on PATH → warning + skip. Existing session YAMLs continue
to work unchanged.

See `doc/design/ndm-start-root.md` for the full design and
`doc/ndmanager/README.md#pipeline-tab` for the operational
walkthrough.

---

## 2026-04-27 — ndm_start as the graph root (replaced)

*Replaced by the entry above on the same day.* This earlier draft
serialised pipelines inside the session YAML's `programs:` block;
the final design moved them to separate `<session>.ndm.<n>.pipeline`
files so the graph is independent of the session schema and named
variants can coexist.

---

## 2026-04-26 — graphical Pipeline tab in ndmanager (superseded)

*Superseded by the 2026-04-27 entry above.* The toggle-only view
shipped here was rolled back in favour of the editable node-graph
designer that already existed in the repo (commit `4e0de6e`),
extended with sticky-root semantics for `ndm_start`. The
`PipelinePage` class introduced by this entry was deleted; the
parameter-page signal additions (`parameterValueChanged`,
`setParameterValue`) were retained for potential future use.

---

## 2026-04-26 — graphical Pipeline tab in ndmanager (original)

New **Pipeline** entry in ndmanager's parameter tree showing
`ndm_start` and its seven dispatch branches (`wideband`, `events`,
`video`, `concatenation`, `spikes`, `lfp`, `clean`) as a clickable
node graph. Each branch corresponds to one of the boolean flags
read by `ndm_start`'s bash, with the sub-step plugins it invokes
(`ndm_resample`, `ndm_extractspikes`, `ndm_pca`, …) shown as
read-only context nodes underneath.

**Bidirectional sync.** Toggle a branch in the graph and the
matching cell in the `ndm_start` ProgramPage's parameter table
updates immediately; edit the table cell directly and the graph
follows. Saving the document writes the new flag values under
`programs[ndm_start].parameters` — no schema changes.

**Default handling.** Sessions that don't include `ndm_start` in
their `programs:` block show the graph in defaults-only read-only
mode. Adding `ndm_start` via Plugins → Add binds the page
automatically. Toggling a flag that was relying on the script
default (e.g. `clean` not present) appends an explicit row so the
next CLI run sees the GUI's choice.

The orchestration order itself stays hard-coded in `ndm_start`'s
bash — only the on/off flags are GUI-editable. Re-ordering would
require making `ndm_start` data-driven and is intentionally deferred.

See `doc/design/pipeline-tab.md` for the full design and
`doc/ndmanager/README.md#pipeline-tab` for the operational
walkthrough.

---

## 2026-04-25 — per-probe empirical KlustaKwik priors

Adds `kk_build_prior.py` / `kk_resolve_prior.py` and wires them into
`ndm_subcluster_unmatched`. Lets you train empirical KlustaKwik
defaults once per probe-type-and-shank from accumulated Klusters
curation logs, then reuse them silently across sessions, animals, and
re-extraction passes.

**The workflow:**

1. Curate a few sessions on a shank in Klusters as usual. Annotate
   cluster quality (Good / Uncertain) — that gates which clusters
   feed the prior. Klusters writes `session.curation_log.<group>.jl`
   for every editing operation.
2. Run `kk_build_prior.py logs… --session-yaml … --electrode-group N
   --out-dir ~/.ndm/priors`. One YAML per shank.
3. `ndm_subcluster_unmatched` invokes `kk_resolve_prior.py` silently
   for every shank as it runs. When a prior matches, four headline
   values (`MinClusters`, `MaxClusters`, `MergeThresh`, `PenaltyMix`)
   become the defaults for `read_kk_param`. Per-session `extraInfos`
   overrides still win.
4. New animal on the same probe → same probe_id resolves the same
   prior automatically. New probe type → resolver misses; falls back
   to script defaults; build a new prior after curating.

**Probe identity:** SHA-256 of the canonicalised
`(nChannels, [{channels:sorted, nSamples, peakSampleIndex, nFeatures}…])`
tuple from the session YAML — sample rate deliberately excluded since
it's acquisition config, not probe identity. When the signature
exactly matches an installed `Template-*.yaml`, the friendly name
(e.g. `4-octrodes-32552Hz`) is used as the human-readable id;
hex-hash fallback otherwise. The hash is stored in every prior YAML
as `probe_signature_hash` for fail-loud verification.

**Best-effort everywhere.** Missing resolver, missing prior file, or
hash mismatch all fall through cleanly to script-builtin defaults.
The behaviour with no priors built is exactly the previous behaviour.

See `doc/design/kk-prior.md` for the full design and
`doc/workflows/empirical-priors.md` for the operational
walkthrough.

---

## 2026-04-22 — process_shadowcluster inflation guard + dedicated KK tier for sub-clustering

**Symptom.** `ndm_reextractspikes_stderiv` on `jg05-20120316` group 7:

```
  eligible parents : 13 / 16 clusters
  new spikes       : 162365
    clu 2  (size=282)  shadow=18  +1257 new
    clu 4  (size=710)  shadow=20  +19 new
    clu 6  (size=1868)  shadow=22  +108912 new         ← 60× inflation
    clu 11 (size=4388) shadow=27  +1 new
  unmatched        : 52176
```

108 912 new spikes (67 %) force-matched into a single shadow
(`clu 6`, whose reference size was 1 868).  These spikes bypass the
unmatched bin entirely, so `ndm_subcluster_unmatched` can't touch them:
they surface in Klusters as one giant opaque shadow attached to an
otherwise unremarkable cluster.

**Root cause.** `process_shadowcluster` v1.0 did pure Mahalanobis 1-NN
with a global χ² gate (`p=0.9999`, `d²=54.234` on `df=21`).  A
reference cluster with a heavy-tailed member distribution has its
per-dim MAD × 1.4826 σ's over-estimated; the resulting ellipsoid is
over-wide and absorbs whatever falls nearby.  No downstream parameter
— KlustaKwik or otherwise — can correct this after the fact because
the absorbed spikes are tagged with the parent's shadow id before any
clustering step runs.

### Fix — three coordinated changes

**1. Inflation guard in `process_shadowcluster` (v1.1).**
New CLI args `--inflationRatio R` (default 5.0) and `--inflationCap C`
(default 500).  A parent may absorb at most
`max(C, R × |parent|)` new spikes.  Implementation:

- Pass 1 (parallel, expensive): compute `bestK/bestD²` per new spike;
  record in `tentativeBestK / tentativeBestD2`.
- Pass 2 (serial, cheap): for each over-absorbing parent, sort
  candidate spikes by `d²` ascending, keep the closest `cap_k`, demote
  the tail to the unmatched bin.

Serial cost is `O(n_assigned log n_assigned)` per over-absorbing
parent; typically 1-3 parents trip the cap per group, so the overhead
is negligible relative to the main assignment loop (nFeatDim × nEligible
distance evaluations per spike).  Verbose output now reports both
accepted and demoted counts per parent and a total-demoted summary.
Setting either knob to 0 disables the guard (preserves v1.0 behaviour
bit-for-bit).

**2. Tighter default `chi2P`.**  0.9999 → 0.999.  `d²=54.234` vs
`d²≈46.80` on `df=21`.  With robust MAD σ's the 0.9999 gate proved
over-permissive in practice; 0.999 is still generous — robust-stats
literature typically uses 0.99 for outlier rejection — while being
significantly more selective.  The inflation guard is the hard
backstop; this is the per-spike gate.

**3. Caller-block tier in `read_kk_param`.**  `ndm_functions` gains
an optional `-b <caller_block>` flag.  Resolution order is now four
tiers when `-b` is given:

```
  1. spikeDetection.channelGroups[g].klustakwik.<n>    (per-group)
  2. programs[<caller_block>].parameters.<n>           (caller override)
  3. programs[ndm_klustakwik].parameters.<n>           (global fallback)
  4. built-in default                                  (safety net)
```

Tier 2 is skipped when `-b` is absent or equals `ndm_klustakwik`,
preserving backward compatibility.  `ndm_subcluster_unmatched` now
passes `-b ndm_subcluster_unmatched` at every call site (27 retrofit
sites).

### New YAML block: `programs[ndm_subcluster_unmatched]`

Populated in `templates/template.yaml` with outlier-bin-tuned defaults
that deliberately diverge from `programs[ndm_klustakwik]`:

| knob                      | ndm_klustakwik | ndm_subcluster_unmatched | rationale |
|---------------------------|---------------:|-------------------------:|-----------|
| `maxPossibleClusters`     |            500 |                     1000 | outlier bin holds dozens of low-SNR units |
| `mergeThresh`             |           42.0 |                     30.0 | below χ²(21, 0.99)≈38.9; prevents collapse of distinct low-SNR units |
| `nStarts`                 |              3 |                        5 | more CEM local minima in outlier bin |
| `phase15Iters`            |              1 |                        2 | sharper xcorr realignment in low-SNR regime |
| `subspaceRecluster`       |              0 |                        1 | split sub-units that coalesce in full 21D |
| `subspaceReclusterDepth`  |              0 |                        2 | moderate recursion depth |
| `splitRecurseDepth`       |              1 |                        1 | (unchanged — existing default is already right here) |
| `templateMatchScore`      |            0.0 |                     0.85 | waveform-similarity assist (safe in sparse bin) |
| `penaltyMix`              |              0 |                     0.25 | AIC mix counters disproportionate BIC pressure |

Users can override any subset per-session; unset keys fall through to
`programs[ndm_klustakwik]` (tier 3).

### New re-extract YAML parameters

Added to both `programs[ndm_reextractspikes]` and
`programs[ndm_reextractspikes_stderiv]`:

| name                        | default | effect |
|-----------------------------|--------:|--------|
| `reextractChi2`             |   0.999 | **CHANGED** from 0.9999 |
| `reextractInflationRatio`   |     5.0 | new — passed as `--inflationRatio` to `process_shadowcluster` |
| `reextractInflationCap`     |     500 | new — passed as `--inflationCap` |
| `reextractAutoSubcluster`   |       0 | new — when 1, `ndm_subcluster_unmatched` runs automatically after the merge |

XML descriptors (`descriptions/ndm_reextractspikes{,_stderiv}.xml`)
updated in lock-step.

### Files touched

```
src/ndmanager-plugins/src/process_shadowcluster/process_shadowcluster.cpp
src/ndmanager-plugins/scripts/ndm_functions
src/ndmanager-plugins/scripts/ndm_subcluster_unmatched
src/ndmanager-plugins/scripts/ndm_reextractspikes
src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv
src/ndmanager-plugins/descriptions/ndm_reextractspikes.xml
src/ndmanager-plugins/descriptions/ndm_reextractspikes_stderiv.xml
templates/template.yaml
```

### Backward compatibility

- `process_shadowcluster` without `--inflationRatio`/`--inflationCap`
  on the command line defaults to `5.0`/`500` (guard active).  Sessions
  that must preserve exact v1.0 behaviour should set
  `reextractInflationRatio: 0` (or `reextractInflationCap: 0`) in the
  session YAML.
- `read_kk_param` without `-b` preserves the prior three-tier order
  bit-for-bit.
- Existing sessions with no `programs[ndm_subcluster_unmatched]` block
  continue to work — tier 2 is empty, tier 3 (`programs[ndm_klustakwik]`)
  supplies every parameter as before.  Only sessions regenerated from
  the new `template.yaml` or hand-edited to add the block get the
  outlier-bin-tuned defaults.
- `.clu.N` cluster-id convention (shadow-offset and unmatched-bin id)
  is unchanged.

### Expected post-fix behaviour for the `jg05-20120316` g7 case

- `clu 6` shadow capped at `max(500, 5 × 1868) = 9340` new spikes
  (down from 108 912; the other ~99 572 are demoted to the unmatched
  bin).
- Unmatched bin grows from 52 176 → ~151 748, and now contains the
  population that was previously hidden inside `clu 6`'s shadow.
- `ndm_subcluster_unmatched` (auto-invoked if
  `reextractAutoSubcluster=1`, otherwise manual) runs KlustaKwik with
  the outlier-bin-tuned parameter block on that 151 748-spike bin and
  discovers its sub-structure rather than leaving it as one amorphous
  pile.
- In Klusters, the operator now sees a realistic distribution of
  low-amplitude units instead of one giant green shadow attached to
  cluster 6.

---

## 2026-04-22 — process_reextractspikes_stderiv writes transformed .spkD (correctness)

**Value-space mismatch** between `.spkD` files produced by the main
pipeline and those produced by the re-extract pipeline:

- `process_extractspikes_stderiv` (main) writes **stderiv-transformed**
  waveforms to `.spkD.N` (spatial derivative across group channels
  + temporal first-difference with `sdiff[-1]=0` boundary).
- `process_reextractspikes_stderiv` (re-extract second pass) was
  writing **raw ADC** waveforms to the same `.spkD.N` at the new-spike
  rows, creating a file that contained two different value spaces
  interleaved — reference rows stderiv, new rows raw.

Downstream consequences:

- `process_shadowcluster::projectSpike` projects every new-spike row
  through the `.pcaD` basis unchanged.  `.pcaD` was trained on
  stderiv-transformed waveforms, so projecting raw-amplitude rows
  through that basis yields features concentrated in an arbitrary
  manifold.  The robust-Mahalanobis classifier then assigns ~all new
  spikes to a single shadow cluster, producing the observed
  "162,358 new spikes → cluster 22, one green mass at the far right
  of cluster 5" symptom in the `jg05-20120316-7` reference session.
- Klusters' waveform view reads `.spkD` bytes directly, so reference
  rows show biphasic stderiv shapes while new rows show raw-ADC
  monophasic shapes — visible side-by-side in the per-cluster
  waveform pane of the same electrode group.

### Fix

`process_reextractspikes_stderiv` Pass-2 write loop now applies the
full stderiv transform before writing to `.spkD`:

1. Spatial derivative per time sample via the already-available
   `computeSDiff` helper (ported unchanged from
   `process_extractspikes_stderiv`, identical for SDIFF_NONE /
   SDIFF_FIRST / SDIFF_LAPLACIAN / SDIFF_ALLPAIRS).
2. Temporal first-difference in-place with `sdiff[-1]=0` boundary.
   `prev[]` stores the unclamped sdiff value so subsequent
   iterations subtract from the pre-clamp value, preserving
   bit-identity with the main extractor's output.

Output layout is sample-major `[s*nChanG + ci]`, matching
`process_extractspikes_stderiv`'s `.spkD` layout exactly.  No change
to the `.res.N` write path or to the detection-stage threshold
computation.

### Rolling back previously-corrupted sessions

Any `.spkD.N` files that have been merged in-place by the buggy
version contain raw waveforms at the new-spike rows.  The script's
`.bak` preservation lets the operator revert:

```bash
mv session.spkD.7.bak  session.spkD.7
mv session.res.7.bak   session.res.7
mv session.clu.7.bak   session.clu.7
mv session.fetD.7.bak  session.fetD.7   # if present
```

Re-run `ndm_reextractspikes_stderiv` with the fixed binary after the
rollback.  Groups that have not yet been merged (no `.bak` sidecars)
are unaffected.

### Files

- `src/ndmanager-plugins/src/process_reextractspikes_stderiv/process_reextractspikes_stderiv.cpp`
  — Pass-2 write now applies the stderiv transform
- `src/ndmanager-plugins/src/process_reextractspikes_stderiv/process_reextractspikes_stderiv.h`
  — docstring corrected
- `src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv`
  — header and inline comments corrected (no behavioural change in
  bash; the binary carries the fix)

---

## 2026-04-22 — fetD completion, klusters focus fixes, Pipeline C nudge correctness

Committed as `f2e7fd4 bugfix(klusters) fix focus issue after spike realignment`.
Despite the commit-message framing, this change set closes out three
long-standing concerns together: incomplete `.fetD`/`.spkD`/`.pcaD`
propagation inside KlustaKwik, focus-chain bugs across the klusters
cluster-operation slots, and a correctness bug where `nudge` and
`realignSpikes` silently corrupted `.fetD` on Pipeline-C sessions
(raw `.spk` + stderiv `.fetD`/`.pcaD`).

### KlustaKwik — `.fetD`/`.spkD`/`.pcaD` fallbacks beyond `LoadData`

Prior work introduced `pickInputPath` (prefer canonical, fall back to
D variant) and wired it into `LoadData`.  This session extends the
same resolution to every other session-file open inside KlustaKwik:

- `RealignChunkWaveforms` — in-place `.spk` rewrite now resolves the
  picked variant.  The `r+b` open path is unchanged; its existing
  graceful "skipping" fallback handles read-only mounts.
- `RefeaturizeFromShifts` — both the `.pca` model load and the `.spk`
  circular-shift fallback (when `.fil` is unavailable) use
  `pickInputPath`.
- Four Phase 1.6 / Phase 2 template-match mean-waveform harvests
  (`KK.cpp` ~2674, ~2750, ~3331, ~3449) — each pass opens the correct
  variant.
- `WritePhase15Checkpoint` — `.spk` and `.fet` originals now come
  from `pickInputPath`, and the `.pending` names are derived from the
  *picked* paths rather than canonical literals.  On success the
  `rename(.pending → original)` now commits back to `.spkD.N` /
  `.fetD.N` when that's what was loaded, instead of creating a ghost
  canonical file alongside the real D variant.
- Startup banner shows `(stderiv variant)` when `.fetD.N` was loaded.
- Build tag bumped to `[build 2026-04-21 fetD]`.

### ndm_klustakwik — accept `.fet.N` OR `.fetD.N`

The input-file check split the parameter-file requirement from the
feature-file requirement.  The feature-file check now accepts either
`.fet.N` or `.fetD.N`, matching what KlustaKwik itself resolves.
Stderiv-only groups no longer emit spurious "input file missing"
warnings.

### Three-tier parameter resolution — latent bug fixed

`ndm_klustakwik` referenced seven variables in its `KlustaKwik`
invocation that were never read via `read_kk_param`:
`subspaceDims`, `subspaceRecluster`, `subspaceReclusterDepth`,
`templateMatchScore`, `templateMatchIters`, `splitRecurseDepth`,
`crossChunkTemplateScore`.  Every session up to this point was
launching KlustaKwik with empty-string values for those flags and
silently relying on `param.c`'s fallback to compile-time defaults —
so per-group YAML overrides for subspace reclustering and template
matching were being ignored by the main sort.

Fixes:
- Added `read_kk_param` calls for all seven with sensible defaults
  (`subspaceRecluster=1`, `subspaceReclusterDepth=2`, `subspaceDims=3`,
  `templateMatchIters=10`, others `0`/`0.0`).
- Hoisted `read_kk_param` from `ndm_klustakwik` into `ndm_functions`
  so wrapper scripts can use identical three-tier resolution.  Passes
  `-s ndm_klustakwik` to `read_script_parameter` so tier-2 always
  reads from the single canonical program block regardless of which
  script is calling.

**User-visible effect:** subsequent main-sort runs will actually
apply subspace reclustering on groups whose YAML configures it;
cluster counts per group will likely shift.  Keep pre-patch `.clu.N`
files as `.clu.N.bak-presubspace` before re-running.

### ndm_subcluster_unmatched — full-parity sandbox KK

The sandbox KlustaKwik invocation passed only five flags
(`-MinClusters`, `-MaxClusters`, `-MaxPossibleClusters`,
`-UseFeatures`, `-MergeThresh`).  With `nStarts=1` and
`SubspaceRecluster=0` (defaults), CEM on a 186k-spike outlier bin
predictably collapsed to a single cluster, yielding
`promoted=0 noise_kept=186981`.

Rewritten invocation now passes all 27 KK flags via the shared
`read_kk_param`, matching `ndm_klustakwik` line-for-line except for
the three throw-away-session values (`-UseFeatures all`,
`-fSaveModel 0`, `-SaveIntermediates 0`).  Parity verified by
flag-set diff.

### klusters — palette focus after destructive / batch operations

Every cluster-list operation that auto-selected the next cluster was
then calling `activeView()->focusClusterView()`, which stole focus
to the 2D scatter and silently broke arrow-key navigation.  Four
slots fixed to restore palette focus instead:

- `slotMoveClustersToNoise` (`Delete` key) — `klusters.cpp:2507`
- `slotMoveClustersToArtefact` (`Shift+Delete`) — `klusters.cpp:2526`
- `slotReclusterFinished` — `klusters.cpp:3262`
- `slotRealignFinished` accept branch — removed the
  `focusClusterView()` call that was overriding the
  immediately-prior `setFocusToList()`
- `slotRealignFinished` reject branch — added symmetric palette
  focus restoration (previously left focus on whatever widget the
  modal review dialog destroyed, silently unpredictable)

Leaves existing `focusClusterView` calls untouched where the user's
next action is plausibly 2D inspection (undo/redo, group, nudge
buttons).

### klusters — nudge/realign Pipeline C correctness

`nudgeClusterTimestamps` and `realignSpikes` used a single flag
(`isStderivSession` / `isStderivRealign`) derived from
`m_origSpkPath.contains(".spkD.")` to gate *both* the `.spk` write
branch and the PCA basis + feature-reprojection branches.  These are
genuinely independent signals that only coincide in Pipelines A (both
raw) and D (both stderiv).

On Pipeline C (raw `.spk` + stderiv `.fetD`/`.pcaD`), the single-flag
check returned `false`, so PCA selected `.pca.N` (wrong basis — the
file doesn't exist, or worse exists with different rank) and feature
reprojection skipped the stderiv transform.  Nudge was silently
writing raw-space projections into `.fetD.N` rows, mixing feature
spaces inside one file.

Split into two flags:

- `spkIsTransformed = m_origSpkPath.contains(".spkD.")` — governs
  the `.spk` write branch (apply `applyStderivTransform` iff on-disk
  `.spk` is in stderiv space, else write raw).
- `fetIsStderiv = m_origFetPath.contains(".fetD.")` — governs PCA
  basis selection (`.pcaD.N` vs `.pca.N`) and whether the raw
  waveform is transformed before projecting onto eigenvectors.

Decision gates after the refactor:

| Site | Flag used | File |
|---|---|---|
| realign `.spk` write | `spkIsTransformed` | klustersdoc.cpp:3230 |
| nudge `.spk` write | `isStderivSpk` (= `spkIsTransformed`) | klustersdoc.cpp:3779 |
| nudge feature reprojection (`makeFetRow`) | `isStderivFet` (= `fetIsStderiv && pca.valid()`) | klustersdoc.cpp:3689, 3712 |
| PCA basis file selection | `fetIsStderiv` | klustersdoc.cpp:2599, 3531 |

Pipeline A and Pipeline D sessions are semantically unchanged (both
flags evaluate identically to the old single flag).  Pipeline C is
now correct.

### Files touched this session

- `src/klustakwik/KlustaKwik.cpp` — banner, build tag
- `src/klustakwik/KK.cpp` — 7 call sites use `pickInputPath`;
  `WritePhase15Checkpoint` preserves D variant through `.pending`
- `src/klusters/src/klusters.cpp` — 5 focus-restoration sites
- `src/klusters/src/klustersdoc.cpp` — `nudge` / `realignSpikes`
  Pipeline-C detection split
- `src/ndmanager-plugins/scripts/ndm_functions` — shared
  `read_kk_param`
- `src/ndmanager-plugins/scripts/ndm_klustakwik` — local
  `read_kk_param` removed; 7 missing param reads added
- `src/ndmanager-plugins/scripts/ndm_subcluster_unmatched` —
  full-parity sandbox KK invocation

---

## Earlier changes — KlustaKwik

### nRuns flag (new)

`-nRuns N` sets the number of independent CEM restarts **per chunk** in chunked
mode (`ChunkMinutes > 0`). Each chunk independently runs `CEMTwoPhase` N times
with different random seeds and keeps its best-scoring result before handing off
to Phase 2. Phase 0 preseed, Phase 1.5 realignment, and Phase 2 merge each run
only once per pipeline invocation. This is the per-chunk analogue of `nStarts`
in non-chunked mode.  Setting `nRuns 3`
runs three independent pipeline iterations each seeded differently
(`srand(RandomSeed + run)`).  `MinClusters` and `MaxClusters` revert to their
intended role: per-chunk `TrySplits` bounds only, not an iteration driver.
`nRuns 0` (default) preserves the original loop for backward compatibility.

### Phase 1.5: xcorr alignment + .fil re-extraction (reworked)

Phase 1.5 now runs after all per-chunk `CEMTwoPhase` jobs complete and before
Phase 2 cross-chunk model matching.  It proceeds in two steps:

**Step 1 — `RealignChunkWaveforms`**
For each chunk, builds a per-cluster mean waveform from `.spk`, XcorrDispatch-aligns
each spike against the mean, records the shift in `spikeShifts[]`.
Home-chunk first-write-wins: overlap spikes that appear in both chunk k and k+1 get
their shift from chunk k (the chunk where they naturally live).

**Step 2 — `RefeaturizeFromShifts`**
For each shifted spike, re-extracts the aligned waveform from the `.fil` broadband file
at `(rawTs − shift − PeakSampleIndex)`, selecting only the group's channels via
`GroupChannelIds`.  Projects through the saved PCA eigenvectors (PCAE format,
`.pca.N`), re-normalises using the per-dimension min/range from `LoadData()`, updates
`Data[]` and corrects the normalised timestamp before Phase 2.
Falls back to circular shift from `.spk` (with wrap-around caveat) when `.fil` is not
available (e.g. after `ndm_stripdat`).

New globals auto-detected from YAML at startup:
- `NbTotalChannels` ← `acquisitionSystem.nChannels`
- `PeakSampleIndex` ← `spikeDetection.channelGroups[g].peakSampleIndex`
- `GroupChannelIds` ← channel list for this electrode group

All three can be overridden on the command line.

### New YAML fields read by KlustaKwikYaml

`KKYamlSpikeParams` now exposes: `nTotalChannels`, `peakSampleIndex`, `channelIds`,
`probeId`, `shankIndex`, `probeFile`, `probeLibraryPath`, `sitePositions`.

### Inline drift estimation (disabled by default)

`_RunInlineDriftEstimation()` is registered but not called unless `probeId` and
`probeFile` are set in the YAML and the `.fil` is present.  Invokes
`process_estimatedrift.py` + `process_applydrift.py` as subprocesses after the final
`.clu` write, producing `SESSION.drift` and `SESSION.chunks.N` for the next run.

### TrySplits — split trial fixes (2026-04-02)

- `CEM(nullptr, 0)` → `CEM(nullptr, 1)` in the split trial: the split candidate
  CEM now runs with `enableSplits=true`, letting `TrySplits` find the best
  multi-cluster solution rather than a single random initialisation.
- Split trial `nStartingClusters` raised from 3 → 13 (noise + 12 real), giving
  the split CEM sufficient starting clusters to explore the full structure of each
  candidate cluster.
- Infinite recursion fix: `static thread_local int _trySplitsDepth` counter added.
  Split trials at depth > `SplitRecurseDepth` use `CEM(nullptr, 0)` (no further
  recursion). Safe for OMP parallel regions.
- New `-SplitRecurseDepth N` (default 1): how many levels of recursive splitting
  `TrySplits` explores.  `0` = original behaviour; `1` = one level; `2` = two.

### Phase 2.5 SubspaceReclusterPerChunk: 3-phase parallel rewrite

Refactored from a single OMP parallel-for-over-chunks to a three-phase model:

- **Phase A (serial):** For each `(chunk, cluster)` pair, compute cluster mean,
  covariance, top-k eigenvectors, project into whitened k-space, normalise,
  compute null score. Produces a flat `WorkItem` list.
- **Phase B (parallel OMP):** Each work item runs `nRuns` × `startK=2..maxSubK`
  restarts of `CEMTwoPhase` (splits enabled via depth saturation) in a local `KK`
  object. Thread cap = `min(nThreads, nItems)`. `schedule(dynamic)` for load balance.
- **Phase C (serial):** Apply accepted splits to `perChunkClass`/`perChunkModels`.
  Serial because `nextLocalId` is per-chunk shared state.

**Depth saturation:** Before each Phase B `CEMTwoPhase` call,
`_trySplitsDepth = SplitRecurseDepth - SubspaceReclusterDepth` is set, so
`TrySplits` fires once but its split trials are non-recursive.

**ID overflow guard (Phase C):** The packed key `chunkIdx * MaxPossibleClusters +
localClusterId` overflows into adjacent chunk's range if `localClusterId >=
MaxPossibleClusters`. Phase C now counts required new IDs before committing each
split and skips if `nextLocalId + nNewIds >= MaxPossibleClusters`. This was the
root cause of the "random cluster" bug.

New `-SubspaceReclusterDepth N` (default 0): controls `TrySplits` recursion depth
inside SubspaceRecluster sub-CEMs, independently of `-SplitRecurseDepth`.

### MergeChunkModels: iterative cross-chunk passes

The entire chunk-pair loop (votes + Mahal + xcorr) now iterates up to
`-TemplateMatchIters` times, breaking when no new `Union()` calls occur.  Enables
cascade matching: a new xcorr merge in pair (k, k+1) can unlock
previously-unresolved clusters in pair (k+1, k+2).

### Progress banners

Phase 1 OMP loop now emits per-run progress to `stderr` inside a critical section:

```
  [chunk 3/9  run 2/3] score=-1243.6  nclusters=12
```

### Parallelism architecture (2026-04-03)

Phase 1 and Phase 2.5 (`SubspaceReclusterPerChunk`) both flattened to `(chunk×run)`
and `(item×run)` parallel axes respectively. Serial reduction after each selects
best run, restores `Class[]`, runs `MStep`, harvests models. `pointPacked` always
rebuilt downstream from `perChunkClass` unconditionally.

### fet/fetD fallback (1st wave, prior session)

Introduced `pickInputPath` in `KlustaKwik.cpp`: prefers canonical
(`.fet`/`.spk`/`.pca`) and falls back to stderiv D variant
(`.fetD`/`.spkD`/`.pcaD`) when canonical is absent.  Wired into `LoadData` so
reextract-style scripts that produce only the D variant no longer need symlink
shims at the KlustaKwik entry point.

The 2026-04-22 commit completes the propagation to every other session-file open
inside KlustaKwik (see top of this file).

---

## Earlier changes — ndmanager-plugins

### process_setupgroups.py

- `parse_probe()` now returns `(shanks, geometries)` — per-site `[x_um, y_um]`
  extracted from `probeFile.sites.geometry` in shank-major order.
- `build_spike_group()` writes `probeId`, `shankIndex`, and `sitePositions_um` onto
  each `spikeDetection.channelGroups` entry so all downstream consumers
  (KlustaKwik, `process_estimatedrift`, `process_localise`) can resolve electrode
  geometry without re-loading the probe file.
- `probes[].id` normalised to `probes[].probeId` on write.

### process_estimatedrift.py

- `read_clu()` handles both binary (`int32` header + `int32` ids, written by
  KlustaKwik and Klusters) and legacy text format.  Previously text-only, causing
  silent failure on binary `.clu` files.
- `n_samp` no longer hardcoded to 52; read from YAML `nSamples` per group via
  `--n-samples-per-group` (comma-separated, one per group).
- `build_group_probe_map()` three-tier resolution: spike group `probeId` first,
  then anatomical group cross-reference, then fallback.
- `build_probe_entry_map()` accepts both `probeId` (canonical) and legacy `id`.
- `get_depths()` reads `sitePositions_um` directly from the YAML group when present,
  returning full-length array with `NaN` for null entries (missing geometry).
- `amplitude_com()` masks `NaN`-depth sites.
- `xcorr_shift()` filters `NaN` from depths before computing inter-site spacing.

### process_localise.py

- `site_positions_from_yaml()` reads inline `sitePositions_um` first; only falls back
  to probe-file lookup when the field is absent or has fewer entries than `n_sites`.

### ndm_estimatedrift

- Builds `--n-samples-per-group` from YAML `nSamples` per group.
- All `xml_read`/`xml_count` → `yaml_read`/`yaml_count`.

### All 15 ndm scripts — YAML migration

`xml_read`/`xml_count` replaced with `yaml_read`/`yaml_count` (69 replacements):
`ndm_concatenate`, `ndm_denoiseuniform`, `ndm_extractspikes`,
`ndm_extractspikes_sdiff`, `ndm_hipass`, `ndm_klustakwik`, `ndm_lfp`,
`ndm_localise`, `ndm_pca`, `ndm_redetectspikes`, `ndm_reorderchannels`,
`ndm_resample`, `ndm_spikegrouper`, `ndm_start`, `ndm_stripdat`.

### ndm_klustakwik (earlier)

- `nRuns` parameter read and passed to KlustaKwik.
- `NbSamplesPerSpike` no longer needs to be passed explicitly (auto-detected from
  YAML), but `nbSamplesPerSpike` is kept in the parameter block for override.

See `doc/design/reextractspikes-v1.md`, `doc/design/reextract-v2.md`,
`doc/design/decomposecollisions.md`, `doc/design/subtractspikes-botm.md`
for the full detail of the recent plugin patch series.

---

## Earlier changes — jg05-20120316.yaml (sample session)

- All 13 `spikeDetection.channelGroups` entries annotated with `probeId`,
  `shankIndex`, and `sitePositions_um` (inline electrode site geometry).
- Groups 1–8: Buzsaki64L octrode geometry (0–140 µm depth per shank, 8 sites).
- Groups 9–13: A1x32-6mm-50-177 linear geometry (0–1550 µm, 32 sites total,
  tiled across 5 ndm_spikegrouper subgroups).
- All 9 `anatomicalDescription.channelGroups` annotated with `probeId`/`shankIndex`.
- `probes[].id` → `probes[].probeId` (canonical key).
- `ndm_klustakwik.nbSamplesPerSpike` corrected: 32 → 41 (matches actual `.spk`
  files).
- Probe 1 entry corrected to reference `A1x32-6mm-50-177.probe`.

---

## Deep technical references

| Topic | File |
|---|---|
| KlustaKwik canonical-engine history (v1.7 → neurosuite-3 diff) | `src/kiloklustakwik/CHANGES-inherited-from-canonical.md` |
| KiloKlustaKwik-internal changes (DipSplit, time-shift merging) | `src/kiloklustakwik/CHANGES.md` |
| `reextractspikes` + shadow clustering (first spec) | `doc/design/reextractspikes-v1.md` |
| `ndm_reextractspikes{,_stderiv}` — extension handling, symlink shims | `doc/design/reextract-v2.md` |
| `process_decomposecollisions` — collision decomposition bug fixes | `doc/design/decomposecollisions.md` |
| `process_subtractspikes` — including `botm` mode (Proepper 2015) | `doc/design/subtractspikes-botm.md` |
| `neuroscope` — cluster raster / overlay stall fixes | `doc/design/neuroscope-raster.md` |
| `neuroscope` — full subsystem audit | `doc/design/neuroscope-audit.md` |
| `templates/template.yaml` — parameter block refresh | `doc/design/template-yaml.md` |
| Per-probe empirical KK priors (`kk_build_prior` / `kk_resolve_prior`) | `doc/design/kk-prior.md` |
| ndmanager Pipeline tab (editable node graph + YAML-driven `ndm_start` dispatcher) | `doc/design/ndm-start-root.md` |
| Hardware / OS tuning recipe | `doc/design/optimization.md` |
| Modeling comparison (Layer 1/2 vs BOTM) | `doc/design/modeling-l1-vs-botm.md` |
| KK prior operational workflow | `doc/workflows/empirical-priors.md` |
