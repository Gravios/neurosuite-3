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

---

## Now

### 🔥 active

*(Nothing currently in active flight at session boundary.)*

### 🎯 next

**Land `STANDARDIZATION.md` + `CLAUDE.md` at repo root.**
Drop-in for the standardization work just produced.  Adds `STANDARDIZATION.md`
at root and a thin `CLAUDE.md` pointing to it plus
`doc/klusters/CODING-STYLE-AUDIT.md`.  Eliminates cold-start drift on future
sessions.  Estimate: trivial.

**CI gate on `-Wshadow -Wall -Wextra`.**
GitHub Actions matrix entry, ~30 lines YAML.  Locks in the m_-prefix audit
gains; catches regressions silently.  Selectively `#pragma GCC diagnostic
ignored "-Wunused-parameter"` on the few Qt slot signature sites that need
it; don't blanket-disable.  Estimate: half a session.

**Fix `pca.nCh <= 64` reader bound in `klustersdoc.cpp`.**
Real bug.  Reader silently invalidates PCA bases when nCh > 64; breaks
Neuropixels-class probes.  Raise the bound and audit dependent code paths.
Estimate: 1 session including verification.

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
`src/kiloklustakwik/`.  Expected high yield: legacy code with known issues
(`MergeThresh` calibration, `UseFeatures` default string, `probabilities`
init).  Estimate: 2-3 sessions.

**Refactor duplicated `_RunChunkedCEMFromPoints` body in KiloKlustaKwik.**
From memory entries, this is duplicated.  Standalone refactor; doesn't
require the full audit.  Estimate: half a session.

### 💭 considered

**Probe-tab branch merge.**
Half-merged probe migration polluting dead-code analysis (`getShankIndex`,
`readAnatomyGroupMeta`, `writeAnatomyGroupMeta`, `getProbeId`,
`getSiblingElectrodeGroups` all flagged as dead).  Either finish the merge
or revert the partial state.  Decision needed: does Gravio plan to land it
soon, or table it longer?

**`klustakwikExp` → `klustakwik` merge — DONE.**
Resolved by making the Exp tree the trunk: it was renamed to KiloKlustaKwik
(`src/kiloklustakwik/`) and the old canonical `klustakwik` was retired. With a
single engine tree the cross-tree declaration pollution that caused
false-positive dead code is also gone.

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

### 📋 planned

**Test infrastructure scaffold.**
Currently zero unit tests.  Minimal scaffold: round-trip tests for binary
formats (`.res`, `.spk`, `.fet`, `.col`) and golden-file tests for
`ParameterYamlReader` against known-good session YAMLs.  Catches
regressions invisible to visual inspection.  Estimate: 1-2 sessions to
build the scaffold; ongoing maintenance thereafter.

**File format documentation.**
Add `doc/formats/` with one `.md` per binary format.  `.col.N` particularly
needs this (32B header + 32B params + template table + record table layout
currently lives only in C++ comments).  Makes the project legible to future
contributors and to external tooling (NWB conversion, custom analysis
scripts).  Estimate: 1 session.

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
git history / `doc/CHANGES.md` after roughly a quarter.)

### ✅ libklustersshared 5-phase audit + dead-code triage *(this session)*

29 in-scope files audited.  Phase 1 (5 typo fixes), Phase 3 (m_-prefix
strip from parameteryaml + progressbar, ~45 substitutions, constructor
param renames to avoid `-Wshadow`), one dead-code deletion (`safeGet3`).
Phases 2/4/5 found nothing actionable — the codebase was already in
markedly better shape than klusters had been.  Shipped:
`libshared-audit.tar.gz` (12 files including AUDIT.md).

### ✅ progressbar modernisation *(this session)*

Six iterations from 80-char hard-width Unicode block bar to minimalist
horizontal-line bar with `/dev/tty` output channel, eraseline-prefixed
redraws, green ✓ / red ✗ completion markers, group-numbered labels
(`[PCA-7]`), and parallel-execution friendly behaviour.  Bash helper
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

---

## Dropped

(Considered and rejected, with rationale.  Kept indefinitely so we don't
relitigate.)

### ❌ Multi-line cursor-positioning ProgressBar for parallel groups

Considered during progressbar modernisation: each parallel group gets its own
row, ANSI cursor-position escapes coordinate writes.  **Dropped** in favour
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

---

## Cross-cutting concerns

These don't fit the time-horizon structure but are worth tracking.

### Documentation debt

- `doc/formats/` doesn't exist yet (planned, see Later)
- `CHANGES.md` not updated since last few features
- Klusters internal architecture doc is good (`doc/klusters/CODING-STYLE-AUDIT.md`),
  but no equivalents for KiloKlustaKwik, ndmanager, neuroscope
- `STANDARDIZATION.md` exists but not yet at repo root (queued, see Next)

### Test debt

- Zero unit tests checked in (planned to address, see Later)
- All verification depends on running against `jg05-20120316` group 7 or
  `eb05-20251118` group 25
- No CI build matrix yet (planned to address, see Next)

### Schema debt

- No `schemaVersion` field in YAML (planned, see Soon)
- Probe migration partially landed, polluting analyzer signal (decision
  needed, see Considered)

### Algorithmic debt (KiloKlustaKwik)

- `MergeThresh` default uncalibrated for typical feature dimensionality
- `UseFeatures` default string causing session duration = 0
- `probabilities` initialisation order
- `_RunChunkedCEMFromPoints` body duplicated
- Multiple sweep regimes not yet baselined

All listed under Soon as "KiloKlustaKwik 5-phase audit" + "experimental sweeps."

---

*Last updated: 2026-04-30.  Update this date whenever items shift between
sections, complete, or are added.*
