# neurosuite-3 roadmap

A living document tracking work in flight, queued, and considered.  Updated as
work completes, scope shifts, or priorities change.

This roadmap is descriptive of *intent*, not a contract.  Items move between
sections, get retired, or get re-scoped.  The status indicators are:

- **🔥 active** — currently being worked on
- **🎯 next** — queued for the immediate next session
- **📋 planned** — committed-to, scheduled for upcoming work
- **💭 considered** — under evaluation; may move to planned or be dropped
- **✅ done** — completed (kept for ~1 quarter for reference, then archived)
- **❌ dropped** — considered and rejected, with rationale (kept indefinitely
  so we don't relitigate)

**Contents:** [Now](#now) · [Soon](#soon) · [Later](#later) · [Done](#done) · [Dropped](#dropped) · [Cross-cutting concerns](#cross-cutting-concerns)

---

## Now

### 🔥 active

*(Nothing currently in active flight at session boundary.)*

### 🎯 next

**Workstation live pass over the compile-verified GUI work.**
Much of the October GUI work (template curation, the paint refactor, the
Klusters input/Overview redesign, NeuroScope's input adoption) is
behaviour-preserving by construction but was only compile + link verified
in the cloud build.  One pass on the reference session, following the
"Live-pass status" / "Open" notes in the 2026-10-05 → 10-09 `CHANGELOG.md`
entries: the lineage overlay (right-click menu, fold, band, ribbon, delete,
amplitude tracking), the four matrix template strips + click-to-overlay,
each repainted view after the paint refactor, and the Klusters + NeuroScope
mouse gestures and Preferences ▸ Input rebind/persist/keymap round-trip.
Since 2026-10-17 also the radial context menu on a real desktop (tested only on
Xvfb): Right-click on each view, the lineage overlay keeping its own
right-click menu, the X / Y wedges greyed in a display without a cluster
view, transparent corners under a compositor, Preferences ▸ Radial menu edits
on the reference session; and the action icons under the desktop's own icon
theme (the Standard rows now come from it).
Check Preferences ▸ Input / Reset all before suspecting a regression (a
stale saved override reads exactly like one).  Estimate: 1 session.

**Make the CI `-Wshadow` warnings a hard gate.**
CI exists (`.github/workflows/ci-linux.yml`, Ubuntu 24.04 / 25.10 / 26.04 /
26.10, CPU, Qt6) and builds
with `-Wall -Wextra -Wshadow`, but warnings are non-fatal: klusters and
libklustersshared are shadow-clean, the other components are not yet
audited.  Promote to `-Werror=shadow` per component as each is cleaned.
Selectively `#pragma GCC diagnostic ignored "-Wunused-parameter"` on the
few Qt slot signature sites that need it; don't blanket-disable.  Also
outstanding from the Qt 6.4–6.12 work: the per-release matrix covers
6.4, 6.9, 6.10 and 6.11; 6.12 still needs a job (none of these releases ships it).  Estimate: half a session per component.

---

## Soon

### 📋 planned

**Apple GPU (Metal) backend.**
macOS has no CUDA, HIP or SYCL, so every GPU site needs a Metal
implementation beside the others.  Decided: Apple silicon only (M1+, macOS
14+), host code on Apple's C++ wrapper (metal-cpp, vendored), kernels
embedded as source and compiled at run time (no full Xcode needed), one more
`USE_METAL` block in the existing runtime dispatchers.  Apple GPUs have no
fp64, so the sites split three ways: fp32/int16 ones port directly (KK CEM,
realign xcorr, PCA refine, decomposecollisions, medianfilter,
medianthreshold, spikegrouper, Butterworth in single precision);
the shift probe and the Grouping Assistant (FP32 option) port if a float
version validates; the spectrogram (cuFFT D2Z), EMG (Dgemv) and the fiber
mean-shift stay on the CPU, with Accelerate.  Phases: 0 Accelerate, 1
CMake + MetalContext + `process_medianthreshold`, 2 the other plugins
(medianfilter, butterworth, spikegrouper; decomposecollisions has nothing
to accelerate), 3 Klusters (realign xcorr, FP32 error matrix; PCA refine
stays on the CPU), 4 KiloKlustaKwik (CEM steps, xcorr; the shift probe is
disabled in every build, so nothing to port), 5 CI and docs (a lint of the
kernel signatures, `metal_kernels_compile`, `NS_DISABLE_METAL`,
`scripts/metal_validate.sh`) — all done 2026-10-21 and run by the macOS CI.
**Left: validation on an Apple-silicon Mac.**  Every GPU result needs
`scripts/metal_validate.sh` on the reference sessions against the CPU
paths; for KiloKlustaKwik the bar is "no further from the CPU than CUDA
is", so record the CUDA-vs-CPU figure on the workstation first.  Full plan:
[`doc/design/metal-backend.md`](../design/metal-backend.md).

**Wire `process_pca` exit paths to call `setFailed()`.**
The `setFailed()` API exists on `ProgressBar` but `process_pca.cpp`'s
`exit(1)` paths don't call it.  Means red ✗ on direct CLI failure.
Five lines of code.  Estimate: trivial.

**Session-schema migrator chain.**
The writer and modifier already stamp `parameters.version` with
`kSessionSchema` (`"1.0"`, `version_contract.hpp`).  Still to do: have
`ParameterYamlReader` check that version and build the migrator chain
infrastructure (no migrators yet — just the chain).  Cheaper to do now,
while the schema is small, than after a breakage.  Estimate: 1 session for
infrastructure; migrators added as the schema evolves.

**KiloKlustaKwik experimental sweeps.**
Run with `-TimeMergeIter 100 -ChunkPreseedFraction 0.08`.  Scale vote-match
floor to `max(3, overlapSpikes/500)` in overlap merge.  Re-run
`eval_tier1.py` sweeps with corrected invocation (prior runs produced NaN
metrics).  Establishes performance baseline for ongoing algorithm work.
Estimate: 1-2 sessions including analysis writeup.

**KiloKlustaKwik 5-phase audit.**
Run the same audit pipeline that hit klusters and libklustersshared over
`src/kiloklustakwik/`.  Expected yield: legacy code paths.  (The
`MergeThresh` and `UseFeatures` default issues once listed here are fixed:
`MergeThresh 0` auto-calibrates to χ², `UseFeatures` defaults to `"all"`.)
Estimate: 2-3 sessions.

**Refactor duplicated `_RunChunkedCEMFromPoints` body in KiloKlustaKwik.**
From memory entries, this is duplicated.  Standalone refactor; doesn't
require the full audit.  Estimate: half a session.

**Drop the Template Library tab.**
The last planned step of template curation.  The left-column Template
palette replaced it as the editing surface; the tab
(`DisplayType::TEMPLATE_LIBRARY`, `createDisplay`, the auto-show pref) is
kept only as the advanced-CRUD fallback (merge / rename / update /
regenerate) until the palette has proven out in the live pass above.
Move or retire that CRUD first.  Estimate: 1 session.

**Templates for every feature space.**
Template generation writes a `.wtf` per `.spk` variant, but the variants other
than the `.clu`'s own can be stale after realign / nudge.  Build them at the
time of the median-waveform computation for every `.spk` type, from waveforms
synced to the current spike times (Actions → Sync Feature Spaces provides the
synced `.spk`).  Design first — when to trigger, and how the per-variant
`.wtf` relate to the method-less `.wti`.  Estimate: 1–2 sessions.

**fiber-kit `.mti`/`.mtf` reader mirror.**
fiber-kit's `neuro_io.read_wtl` reads `.wtl` v2 since fiber-kit patch 0006
(2026-10-16), so lineage files from Klusters now read on both sides.  The
committed template model (`.mti`/`.mtf`, the `.wti`/`.spk` layouts) has no
fiber-kit reader yet.  A separate fiber-kit patch, never cross-applied from
ns3.  Estimate: a quarter session.

### 💭 considered

**One write path for the session YAML.**
All three GUIs read and write the session `.yaml` through libklustersshared,
but through two writers: ndManager saves with `ParameterYamlWriter` (the whole
document from its model, keeping keys it does not model), NeuroScope with
`ParameterYamlModifier` (patches its own sections in place). Klusters only
reads. Both writers share `yamlformat::emitWithLayout`, so the output looks
the same, but each section's encoding exists twice. Make one a mode of the
other (e.g. `ParameterYamlWriter` with a "only these sections" set) so a
section is written by one piece of code. Also: `klusters/src/driftgeometry.cpp`
and `ndmanager/src/probepage.cpp` parse the session file with yaml-cpp
themselves before handing the root to the shared `readProbesSection` (read
only; could go through `ParameterYamlReader::getRawRoot()`), and
`klusters/src/parameterxmlmodifier.cpp` is unbuilt dead code to delete.

**Klusters — events from spike annotation.**
Let a user label stretches of the recording from the spiking itself and
save them as an event file: for example, mark where a unit's waveform
changes shape between network states, so the change becomes a time-stamped
event usable alongside the session's other events.  Klusters already carries
dormant event support inherited from NeuroScope's trace view
(`eventsprovider.*`, `TraceWidget::addEventProvider`, and the event drawing
in `traceview.cpp`) that nothing creates or calls today.  It is kept in place
while it stays benign: two latent bugs in it were fixed during the warning
cleanup (0834 — the loaded event count was never stored; 0835 — the
single-column layout skipped selected events), and neither could be reached
by a user.  The work would wire a provider to the trace view, add the
annotation gesture (e.g. from a waveform or time selection), and write the
result in the event file format NeuroScope reads.

**Template curation — open UX questions.**
From the template-curation live runs, none blocking:
- how to return from a single-node overlay to the full class band in
  review mode, without changing primary or toggling edit mode;
- node / class labels on the matrices' extra template cells;
- exact per-spike out-of-window shading vs the current cheap range-overlap;
- whether the template matrices should also gate under Templates mode;
- a per-node cue (shade / alpha, or roots only) so a class's overlaid node
  means are distinguishable (the circular-shift stagger was rejected);
- showing a class's unit mean±std on bare class-select, before any fold;
- the oblique-basis scope still reads `.wti`; consumers of the *final*
  templates should read `.mti`/`.mtf`.

**Input system — remaining options.**
The Klusters and NeuroScope migrations are complete.  Optional:
cross-active-scope conflict reporting (only within-scope clashes are
flagged today); the policy on an in-scope conflict (block vs steal); and
user-droppable keymap presets (an `install(DIRECTORY keymaps/ …)` rule plus
a second `loadKeymapsFromDir` scan — presets are compiled in only).

**Paint structure — optional polish.**
The three refactor tiers are done.  Only if they clearly pay: a world/device
coordinate RAII guard (instead of manual `resetTransform`), an overlay
registry (register overlay objects instead of overriding
`paintWidgetOverlays`, worthwhile only if the overlay set grows), and
folding the Residual/Drift/Template matrices from `MatrixViewport` onto a
shared base.

**Probe-tab branch merge.**
Half-merged probe migration polluting dead-code analysis (`getShankIndex`,
`readAnatomyGroupMeta`, `writeAnatomyGroupMeta`, `getProbeId`,
`getSiblingElectrodeGroups` all flagged as dead).  Either finish the merge
or revert the partial state.  Decision needed: does Gravio plan to land it
soon, or table it longer?

**Resolve `Validator::fixup` analyzer false positives.**
Build base-class table from `class X : public Y` declarations; exempt
matched virtual override names from dead-code reports.  ~30 lines of
analyzer script.  Improves signal/noise across all future audit passes.
Estimate: half a session.

---

## Later

### 💭 considered

**Branching discipline review.**
Audit churn suggests feature-branch-per-topic flow would help.  Current
state appears to be everything-on-main, which means regressions in topic A
ship along with topic B with no isolation.  May be unnecessary if Gravio
prefers the current cadence; if so, drop this item.

**Memory hygiene cycle.**
Periodic prune of stale tarball-version-specific memory entries as work
completes.  Facts that belong in `STANDARDIZATION.md` shouldn't also live as
memory entries (duplication risk: STANDARDIZATION says one thing, memory
says another, they drift).  Process item rather than code item.


---

## Done

(Recent completions, kept for reference.  Items here move to
git history / `CHANGELOG.md` after roughly a quarter.)

<details>
<summary>Completed items</summary>

### ✅ `pickInputPath` deployment in KiloKlustaKwik

`pickInputPath` now resolves every input through `custody::resolve` by the
`Method` parameter (`KlustaKwik.cpp`), and the `.spk` / `.pca` / `.clu` read
sites use it or the shared-artifact resolver.  The D-suffix files it once
fell back to are retired.

### ✅ neuroscope and ndmanager audits *(2026-10)*

NeuroScope: [`doc/design/neuroscope-audit.md`](../design/neuroscope-audit.md).
ndManager: audit fixes of 2026-10-15 and 2026-10-16 in `CHANGELOG.md`.

### ✅ `ZenOptimizations.cmake` global flags

The global block now uses `add_compile_options` (so `-funroll-loops` and
`-march=native` really apply), with `-march=native` behind its own
`NS_NATIVE_ARCH` option (default `ON`, `OFF` for portable builds).

### ✅ Action-icon standard *(2026-10-18)*

An action's icon is a row in one table keyed by the action's stable id,
applied in one pass: generic actions from the desktop theme by freedesktop
name with the bundled copy as fallback, Klusters' own icons by hyphenated
`.qrc` alias, painted icons from the palette (they follow light / dark).
Fixed on the way: *Renumber and Save* had never shown its icon.  Klusters
follows it (`actionicons.h`, `klusters_test_actionicons`); NeuroScope and
ndManager still set icons at their creation sites.  Reference:
`STANDARDIZATION.md` §2.11, recipe `RECIPES.md` §2.6.  Open: artwork for the
actions that have none (the Recluster family, the sorts, Hierarchy).

### ✅ Klusters radial context menu *(2026-10-17 → 10-18)*

Right-click in a view opens a hierarchical ring menu (flick or click, Esc
backs out a ring) over the menu-bar actions; the lasso undo-vertex and the
lineage overlay keep their right-click.  Sort holds the sort actions under
short labels; X / Y wedges step the feature dimensions with the mouse wheel.
The layout is data (`radiallayout.h`, settings key `radialMenu/layout`) and
is edited in Preferences ▸ Radial menu (catalogue, drag-and-drop tree, live
preview).  Fixed on the way: the views' right-click filter swallowed every
registry-bound right press, and a right-click on a panel title bar opened two
menus.  Reference: `doc/klusters/README.md`, "Radial context menu".  Open:
the live pass above.

### ✅ Collision decomposition in Klusters *(2026-10-02 → 10-09)*

`process_decomposecollisions` runs from Klusters' Plugins menu: its
descriptor (`plugins/descriptions/process_decomposecollisions.xml`,
integration `decollide-apply`) prefills the open group, and Klusters applies
the accepted records of the `.col` sidecar it writes through the same
grow-and-stage path as the targeted decollide (a lasso against a pinned
two-cluster oblique basis, key D in the pending-lasso mode).  Reference:
`doc/workflows/collision-decomposition.md`.

### ✅ PCA-basis reader accepts more than 64 channels

The fixed `nCh > 64` rejection in the Klusters `.pca` reader is gone: the
bound is now the group's own channel count (`klustersdoc_realign.cpp`), so
Neuropixels-class probes load their PCA bases.  See `STANDARDIZATION.md` §6.6.

### ✅ `klustakwikExp` → `klustakwik` merge

Resolved by making the Exp tree the trunk: it was renamed to KiloKlustaKwik
(`src/kiloklustakwik/`) and the old canonical `klustakwik` was retired. With a
single engine tree the cross-tree declaration pollution that caused
false-positive dead code is also gone.

### ✅ NeuroScope input system on the shared engine *(2026-10-09 → 10-10)*

The `input::` engine moved from Klusters into `libklustersshared` and now
drives NeuroScope too: every menu shortcut is mirrored into the registry
with a startup conflict audit, the TraceView mouse monolith is decomposed
into per-tool-mode commands, the Position/Spectral zoom goes through the
registry, and NeuroScope gets the shared Preferences ▸ Input page, keymap
layouts, cheat-sheet and an active-tool indicator.  The audit's first
catch: two silently dead shortcuts (Ctrl+Shift+A, Ctrl+Shift+S), now
resolved.  Palette navigation stays inline by design.  Reference:
`src/neuroscope/docs/INPUT_SYSTEM.md`.

### ✅ Klusters input-binding architecture + Overview redesign *(2026-10-06 → 10-08)*

Every key, mouse button and wheel binding on the registry and rebindable in
a generated Preferences ▸ Input page (key / button / wheel editors, conflict
tint, reset-all), with keymap layouts and a registry-driven cheat-sheet.
The Overview Display now navigates uniformly (Ctrl+wheel zoom, Ctrl-drag
pan), defaults to a plain pointer, has the four curation tools as Ctrl+1–4
toggles, and picks on a normal click.  Decided along the way: full
command/scope/resolver from the start; modifier gestures rebindable via
`ModMatch::AtLeast`; modal keys are `Locked` (read-only in Preferences);
matrices share navigation by composition, not a base class; TraceView's
press split per mode.  The four structural inline key handlers are the
documented ceiling, not unfinished work.  Reference:
`src/klusters/docs/INPUT_SYSTEM.md`.

### ✅ Klusters paint-structure refactor *(2026-10-06)*

Monotonic `invalidate()` across all five classic views (150 sites); a
`BufferedView` base owning the double buffer (Waveform / Cluster /
Correlation migrated; ErrorMatrix and Trace bespoke by design); WaveformView
channel-geometry helpers.  Kills the "coalesced paint dropped my redraw"
bug class.  Reference: `src/klusters/docs/VIEW_SYSTEM.md`.

### ✅ Template curation in the feature space *(2026-10-05 → 10-06)*

Hand-built template lineages (drift roots + adapt/collision leaves) edited on
the feature scatter (Shift+E), nodes as running mean/std/count summaries,
`.wtl` v2 + separate `.mti`/`.mtf` model files, native generation in
neurosuite-core, a Template palette, a mean±std band over the waveforms,
and cluster×template strips on all four curation matrices.  Follow-ups
(drop the Template Library tab, fiber-kit `.wtl` v2 mirror, open UX
questions) are under Soon.

### ✅ Project docs: standardization, formats, tests, CI

`STANDARDIZATION.md` and a thin `CLAUDE.md` landed (the former now in
`doc/dev/`), ending cold-start drift.  Per-format references exist in
`doc/ndmanager-plugins/formats/` (including `.col`) and
`src/libneurosuite-core/docs/FILE_FORMATS.md`.  A ctest suite (format I/O
for `.spk` / `.wti` / `.wtl`, custody conformance, the input registry,
template lineage, …) and CI builds (`.github/workflows/ci-*.yml`) are in
place.  Golden-file tests for the template-library formats
(`core_test_golden_formats`) and `.res` / `.fet` / `.col` robustness and
round-trip checks (`core_test_neurofileio_robust`) have since been added.

### ✅ libklustersshared 5-phase audit + dead-code triage *(this session)*

29 in-scope files audited.  Phase 1 (5 typo fixes), Phase 3 (m_-prefix
strip from parameteryaml + progressbar, ~45 substitutions, constructor
param renames to avoid `-Wshadow`), one dead-code deletion (`safeGet3`).
Phases 2/4/5 found nothing actionable — the codebase was already in
markedly better shape than klusters had been.  Shipped:
`libshared-audit.tar.gz` (12 files including AUDIT.md).

### ✅ progressbar modernization *(this session)*

Six iterations from 80-char hard-width Unicode block bar to minimalist
horizontal-line bar with `/dev/tty` output channel, eraseline-prefixed
redraws, green ✓ / red ✗ completion markers, group-numbered labels
(`[PCA-7]`), and parallel-execution friendly behavior.  Bash helper
`print_group_failed STEP GROUP` in `ndm_functions` plus wrapper-script
integration in `ndm_pca` and `ndm_pca_stderiv`.  Suppressed noisy
`process_pca_stderiv: transformed N spikes` stderr line.  Shipped:
`klusters-progressbar-modern-v6.tar.gz` (12 files).

### ✅ Klusters dipsplit post-commit confirm UX *(this session)*

Three-iteration tarball series.  Bug fixes:
1. Pre-mutation of `clusterInfoMap` before `prepareUndo` polluting the
   undo snapshot (segfault on undo) → rewrote following
   `integrateBasinLabeling` discipline.
2. Multiple single-cluster signals for multi-cluster commit causing view
   inconsistency → rewrote as recluster-shape clone using
   `prepareReclusteringUndo` + `addNewClustersToView` recluster variant +
   `emit newClustersAdded(emptiedClusters)`.
3. Focus didn't return to palette after Esc → `clusterPalette->setFocusToList()`
   instead of `view->focusClusterView()`.

Shipped: `klusters-dipsplit-postcommit-v3.tar.gz` (8 files).

### ✅ Klusters 5-phase audit *(prior sessions)*

13 files +1062/-591 net.  Established the audit pipeline (5 phases +
dead-code triage) that's now reused for libklustersshared and queued for
KiloKlustaKwik.

</details>

---

## Dropped

(Considered and rejected, with rationale.  Kept indefinitely so we don't
relitigate.)

<details>
<summary>Dropped items and why</summary>

### ❌ Multi-line cursor-positioning ProgressBar for parallel groups

Considered during progressbar modernization: each parallel group gets its own
row, ANSI cursor-position escapes coordinate writes.  **Dropped** in favor
of single-line shared `/dev/tty` with `\x1b[2K` erase-line prefix.
Rationale: the simpler approach matches what apt and brew do; it works
reliably across terminal emulators; the visual cost (occasional flicker
between groups) is acceptable.  Multi-line would have added ~50 LOC of
fragile coordination code with marginal gain.

### ❌ Reserving 2 cells in barWidth for the completion ✓ marker

Considered: shrink the bar by 2 cells throughout the run so the completed
state with " ✓" exactly fits 80 columns.  **Dropped** when Gravio relaxed
the hard-80 constraint to "anything under 100 is fine."  Rationale:
unnecessary complication for a non-binding constraint.

### ❌ Adding `m_` prefix to legacy code

Considered briefly mid-session as direction of "Phase 3 m_-prefix
homogenisation" before Gravio corrected.  **Dropped** because the project
convention is bare names (no prefix) — klusters has zero `m_` on members.  The audit direction is **strip**, not add.  Re-recorded
in `STANDARDIZATION.md` §6.1 as a known Claude failure mode.

</details>

---

## Cross-cutting concerns

These don't fit the time-horizon structure but are worth tracking.

### Documentation debt

- Klusters has internals references (`src/klusters/docs/`: input, view,
  document model, concurrency, plugin API) and NeuroScope an input-system
  reference; KiloKlustaKwik and ndmanager have no equivalents yet
- No format reference yet for the `.mti` / `.mtf` model files (they reuse
  the `.wti` / `.spk` schema; a short note in `doc/ndmanager-plugins/formats/`
  would do)

### Test debt

- `ParameterYamlReader` has no golden-file test of its own (the writer,
  modifier and template round-trips are covered by the `klustersshared_`
  tests)
- The GUI work is untestable in the cloud build; behaviour depends on the
  workstation live pass (see Next) against `jg05-20120316` group 7 or
  `eb05-20251118` group 25
- CI builds four Ubuntu releases (Qt 6.4, 6.9, 6.10, 6.11; CPU) plus a
  Windows/MSVC job, with non-fatal warnings; no -Wshadow gate yet (see Next)

### Schema debt

- `parameters.version` is written but not yet checked on read; no migrator
  chain (planned, see Soon)
- Probe migration partially landed, polluting analyzer signal (decision
  needed, see Considered)

### Algorithmic debt (KiloKlustaKwik)

- `_RunChunkedCEMFromPoints` body duplicated
- Multiple sweep regimes not yet baselined

All listed under Soon as "KiloKlustaKwik 5-phase audit" + "experimental sweeps."

### Performance / optimization

Profiling comes first — none of the items below should be actioned before the
measurement that identifies the actual bottleneck.

- **Decisive profiling pass (do first):** time a single full read-pass of the
  131 GB session vs a single processing-pass, and run Nsight Systems on one
  clustering run, to classify the pipeline as IO- / bandwidth- / compute- /
  GPU-bound before writing any code. Storage is ext4 on fast NVMe and the
  plugins already do large-block / mmap reads, so IO is not expected to be the
  bottleneck; bandwidth and the GPU feed path are the suspects.
- **Memory-bandwidth reduction (streaming kernels):** compute in `float`
  instead of `double` where precision allows (halves traffic, doubles SIMD
  width) — NOT for PCA/drift; and fuse pipeline stages so each data chunk is
  streamed once (filter → detect → featurize) instead of one full pass per
  stage. Bandwidth, not ALU, is why `-march=native` measured ~5% on the
  streaming kernels.
- **V-cache blocking (reuse-heavy kernels):** cache-block the covariance /
  pairwise-distance math in `process_drifttracker` and PCA so working sets fit
  the 9800X3D's 96 MB L3 — the one place the X3D premium is actually earned.
  Pure sequential filters won't benefit.
- **GPU feed path:** pinned host memory + H2D/D2H overlap via streams; move the
  KKE stderiv path off CPU (see KiloKlustaKwik items). Likely a bigger win than
  device-kernel tuning if the GPU is starved rather than saturated.
- **OpenMP scaling:** verify the threaded plugins actually scale past ~4
  threads (plateau ⇒ bandwidth-bound, confirming the above). `process_refeaturize_stderiv`
  is still single-threaded — parallelise if it is hot.
- **PGO:** the one untapped compile-time lever (~10–20% on branchy CPU compute:
  KK CPU path, detection plugins), using the reference sessions as the training
  workload. Cost: two-phase build + profile refresh.
- **Build system:** done — `cmake/ZenOptimizations.cmake` applies
  `-funroll-loops` and `-march=native` with `add_compile_options` (the latter
  behind `NS_NATIVE_ARCH`, default `ON`), `-ffast-math` per target through
  `ns_fast_math`, and LTO via `CMAKE_INTERPROCEDURAL_OPTIMIZATION`
  ([BUILD.md §3](../BUILD.md#3-build-options)).

### Algorithmic debt (Klusters data model)

- **Cluster reassignment is O(total spikes), not O(moved spikes).** Each edit
  (`createNewCluster` / `deleteSpikesFromClusters` / `groupClusters` /
  `moveSpikeSubset`) rebuilds the entire `spikesByCluster` table (`moveClusters`
  memcpys every cluster) and pushes a full table copy onto the undo stack. With
  high spike counts this dominates interactive latency. Two independent costs:
  (1) the region-test scan that identifies movers — O(spikes in the edited
  clusters); (2) the commit + undo snapshot — O(total spikes).
- **Proposed direction (large refactor, validate before adopting):** replace the
  contiguous cluster-grouped table with a label model — `clusterOf[spike]`
  (file-order, maps directly to `.clu`) plus a per-cluster *sparse set*
  (packed `members` vector + `posInCluster` index) for O(1) add/remove and
  cache-friendly per-cluster iteration. Makes the commit O(moved). Pair with
  **delta-based undo** (store changed `(spike, from, to)` triples, O(moved)
  memory) instead of full snapshots. Optionally add a **uniform 2D grid** over
  the currently displayed projection (rebuilt O(N) on dim-pair change) to cut
  the selection scan to points near the polygon. Touches every data-model
  consumer (views, all worker threads, save, undo) — must be validated against
  the current rebuild path for bit-identical `.clu` output on a reference
  session. See design notes from the session this item was added.

---

*Last updated: 2026-10-19.  Update this date whenever items shift between
sections, complete, or are added.*
