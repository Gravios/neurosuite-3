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
Check Preferences ▸ Input / Reset all before suspecting a regression (a
stale saved override reads exactly like one).  Estimate: 1 session.

**Make the CI `-Wshadow` warnings a hard gate.**
CI exists (`.github/workflows/ci.yml`, ubuntu-24.04, CPU, Qt6) and builds
with `-Wall -Wextra -Wshadow`, but warnings are non-fatal: klusters and
libklustersshared are shadow-clean, the other components are not yet
audited.  Promote to `-Werror=shadow` per component as each is cleaned.
Selectively `#pragma GCC diagnostic ignored "-Wunused-parameter"` on the
few Qt slot signature sites that need it; don't blanket-disable.  Also
outstanding from the Qt 6.4–6.12 work: a CI build matrix for 6.4 / 6.10 /
6.12.  Estimate: half a session per component.

---

## Soon

### 📋 planned

**Wire `process_pca` exit paths to call `setFailed()`.**
The `setFailed()` API exists on `ProgressBar` but `process_pca.cpp`'s
`exit(1)` paths don't call it.  Means red ✗ on direct CLI failure.
Five lines of code.  Estimate: trivial.

**Complete `pickInputPath` deployment in KiloKlustaKwik.**
`.fet`/`.fetD` dual-extension lookup helper is partial.  Pending sites:
`.spk` (×9), `.pca` (×1), `.clu` (×2) read locations.  Mechanical sweep,
~30 minutes.  Estimate: 1 session.

**Schema versioning for `ParameterYamlReader`.**
Add `schemaVersion: N` field at YAML document root.  Build migrator chain
infrastructure (no migrators yet — just the chain).  Cheaper to do now, while
schema is small, than after a breakage.  Estimate: 1 session for
infrastructure; migrators added as schema evolves.

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

**`process_decomposecollisions` workflow integration.**
Currently a power-user offline tool.  C++ engine + Python viewer, no GUI
hookup.  If this becomes regular curation flow, deserves a Klusters menu
entry.  If not, deserves more visible documentation so future-you doesn't
forget it exists.  Decision: is this becoming routine or staying offline?

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

**neuroscope audit.**
Has not been audited at all.  Likely lower fruit than kiloklustakwik (less
algorithmic complexity, more straightforward Qt code) but unknown until
scanned.  Estimate: 1-2 sessions if proceeded with.

**ndmanager audit.**
Same as neuroscope — unaudited, fruit unknown.  Estimate: 1-2 sessions.

---

## Done

(Recent completions, kept for reference.  Items here move to
git history / `CHANGELOG.md` after roughly a quarter.)

<details>
<summary>Completed items</summary>

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
template lineage, …) and a CI build (`.github/workflows/ci.yml`) are in
place.  Still uncovered by tests: `.res` / `.fet` / `.col` round-trips and
golden-file tests for `ParameterYamlReader` (see Test debt).

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
convention is bare names (no prefix) — klusters has zero `m_` on members
across ~30K LOC.  The audit direction is **strip**, not add.  Re-recorded
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

- No round-trip tests for `.res` / `.fet` / `.col`, and no golden-file tests
  for `ParameterYamlReader`
- The GUI work is untestable in the cloud build; behaviour depends on the
  workstation live pass (see Next) against `jg05-20120316` group 7 or
  `eb05-20251118` group 25
- CI builds one configuration (Qt6, CPU) with non-fatal warnings; no
  -Wshadow gate or Qt-version matrix yet (see Next)

### Schema debt

- No `schemaVersion` field in YAML (planned, see Soon)
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
- **Build system:** `ns_fast_math` / `ns_native_arch` per-target opt-ins landed.
  NOTE: the global flag block in `cmake/ZenOptimizations.cmake` applies flags via
  `set(CMAKE_<lang>_FLAGS_<cfg> ... CACHE STRING ... FORCE)`, which is a no-op
  (a normal-variable shadow keeps the default) — so global `-march=native` /
  `-funroll-loops` were never actually applied; only per-target flags took
  effect. Fix is `string(APPEND CMAKE_<lang>_FLAGS_<cfg> ...)` on the normal
  variable. Deferred pending a decision: fixing it makes `-march=native` truly
  global ⇒ non-portable binaries across the whole tree. (LTO, set via
  `CMAKE_INTERPROCEDURAL_OPTIMIZATION`, was verified to be genuinely active.)

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

*Last updated: 2026-10-11.  Update this date whenever items shift between
sections, complete, or are added.*
