# ndmanager-plugins feature roadmap

A focused roadmap for `src/ndmanager-plugins/` — the preprocessing pipeline
that runs between raw acquisition data and klusters / kiloklustakwik input.

This is a **feature** roadmap (what plugins should do that they don't yet),
distinct from the project-wide `ROADMAP.md`.  Audit and infrastructure work
that touches plugins (compiler-flag CI, schema versioning, etc.) is tracked
in the project roadmap; here we focus on **plugin functionality**.

Status indicators match the project ROADMAP convention:
**🔥 active**, **🎯 next**, **📋 planned**, **💭 considered**, **✅ done**, **❌ dropped**.

---

## Pipeline overview

```
            raw acquisition (.dat from amplifier)
                        │
                        ▼
            ┌───────────────────────────┐
            │  format conversion        │  ndm_aom2dat, ndm_smr2dat,
            │  (vendor → .dat)          │  ndm_ncs2dat, ndm_nev2evt, ...
            └───────────────────────────┘
                        │
                        ▼
            ┌───────────────────────────┐
            │  signal conditioning      │  ndm_hipass, ndm_lfp,
            │  (filter, drift, denoise) │  ndm_estimatedrift, ndm_applydrift,
            │                           │  ndm_denoiseuniform, ndm_resample
            └───────────────────────────┘
                        │
                        ▼
            ┌───────────────────────────┐
            │  spike extraction         │  ndm_extractspikes
            │  ┌─ raw waveform ─────────┤    ndm_extractspikes_sdiff
            │  ├─ spatial-derivative ───┤    ndm_extractspikes_stderiv
            │  └─ spatio-temporal ──────┤
            └───────────────────────────┘
                        │
                        ▼
            ┌───────────────────────────┐
            │  feature extraction       │  ndm_pca (raw .spk)
            │  ┌─ canonical PCA ────────┤    ndm_pca_stderiv (.spkD → .fetD)
            │  └─ stderiv PCA ──────────┤    ndm_refeaturize (rebuild .fet from new .spk)
            └───────────────────────────┘
                        │
                        ▼
            ┌───────────────────────────┐
            │  clustering (KiloKlustaKwik)  │  ndm_klustakwik
            └───────────────────────────┘
                        │
                        ▼
            ┌───────────────────────────┐
            │  refinement / curation    │  ndm_subcluster_unmatched
            │  helpers                  │    ndm_decomposecollisions (offline)
            └───────────────────────────┘  ndm_redetectspikes (post-curation re-run)
                        │
                        ▼
                klusters (interactive sorting)
```

This pipeline structure organises the roadmap.

---

## Now

### 🎯 next

**`ndm_pca` and friends: the v6 progressbar drop wiring.**
The v6 progressbar tarball needs application — once Gravio extracts it and
runs the build, the per-group `[PCA-7]` labels and red ✗ failure markers
go live across `ndm_pca` and `ndm_pca_stderiv`.  Then verify on
`jg05-20120316` group 7.  Failure-marker testing requires forcing one
group to fail (e.g. `chmod -r` on its `.spk.N` input) — add a brief test
recipe to docs.  Estimate: trivial, just verification.

**Wire `setFailed()` in `process_pca` exit paths.**
Five `exit(1)` sites in `process_pca.cpp`; before each, call
`progress->setFailed(); delete progress;`.  Means red ✗ on direct CLI use,
not just from wrapper-script failure detection.  Also: do the same for
`process_nlxconvert` and any other plugin using `ProgressBar` heap-style.
Estimate: half a session.

**Audit `process_pca_stderiv` exit paths same as above.**
The stderr-summary line was suppressed in the v6 drop; need to make sure
the failure paths still emit a useful exit code (not silently return 0
with no output).  Estimate: 30 minutes.

---

## Soon

### 📋 planned

**Group label propagation to the rest of the parallel-orchestration plugins.**
The `[PCA-7]` group-label pattern is now standard in `process_pca`.
Replicate to other plugins that run per-group in parallel:
- `process_extractspikes` (called from `ndm_extractspikes` per group)
- `process_extractspikes_sdiff`, `process_extractspikes_stderiv`
- `process_reextractspikes`, `process_reextractspikes_stderiv`
- `process_refeaturize`, `process_refeaturize_stderiv`
- `process_shadowcluster`
- `process_subcluster_unmatched` (already in memory entries as built but
  not group-labelled)

Each gets a `-g N` CLI option, the wrapper passes `-g $electrodeGroup`,
the bar shows `[<step>-<N>]`.  Mechanical sweep, ~10 plugins × 5 minutes
each.  Estimate: 1 session.

**Stale `.docbook~` files removed from tree.**
8 backup files (`*.docbook~`) committed by accident — vim swap residue.
They serve no purpose; `git rm` and update `.gitignore` to add `*~`.
Estimate: trivial.

**Docbook coverage extension.**
Currently 10 of ~50 wrappers have man pages.  Sweep the major
orchestrators (`ndm_pca`, `ndm_pca_stderiv`, `ndm_klustakwik`,
`ndm_extractspikes` family, `ndm_hipass`, `ndm_lfp`) and write `.docbook`
files for them.  Estimate: 1-2 sessions; can be incremental.

**Reverse direction: `ndm_yaml2xml`.**
`ndm_xml2yaml` exists for the one-shot XML → YAML migration but no
reverse tool.  This means YAML-only sessions can't be downgraded if a
user needs to fall back to legacy ndManager.  Should be straightforward
mirror of the existing Python script.  Decision needed: is the YAML
migration considered one-way?  If yes, drop this item.  Estimate: 1
session if proceeded with.

**`ndm_clean` cleanup audit.**
84-line wrapper that removes intermediate files.  Memory tracks no
recent changes here, but pipeline additions (`.fetD.N`, `.col.N`,
`.spkD.N`, `.res.pending`, `process_decomposecollisions` outputs) may
have introduced new file types not yet in the cleanup list.  Sweep and
update.  Estimate: half a session.

### 💭 considered

**Unified diagnostic logging convention across plugins.**
Plugins currently emit diagnostic info in inconsistent ways:
- `process_pca_stderiv` had `transformed N spikes (k/n channels; ...)` on
  stderr (recently suppressed)
- `process_extractspikes` writes nothing
- Other plugins variously print to stderr or stdout

Should there be a `--verbose` / `-v` standard that controls this?  All
plugins respect it identically?  Decision needed: is this worth the
effort, or do you prefer the current "each plugin does its own thing"?

**Unified output-directory convention.**
Plugins write output files in different places:
- Most write to CWD
- `ndm_pca` writes `.fet.N.tmp` first, then `process_mergefeatures` to
  final location
- `process_decomposecollisions` writes to `.col.N` adjacent to inputs

Standardising would simplify wrapper scripts and improve cleanability,
but introducing the standard means churning every plugin's CLI.  Decision
needed.

**`process_*` Python plugins → C++ rewrite policy.**
Five plugins are Python: `process_applydrift.py`, `process_decomposecollisions.py`
(deprecated, removed), `process_estimatedrift.py`, `process_localise.py`,
`process_setupgroups.py`.  `process_decomposecollisions` was already
ported to C++ this past quarter.  Should the others follow?  Considerations:
- `applydrift` and `estimatedrift` are I/O-bound; Python is fine
- `localise` involves matrix work; C++ would be faster
- `setupgroups` is config plumbing; Python is appropriate

Decision needed: case-by-case rewrite, or only when bottleneck identified?

---

## Later

### 📋 planned

**Pipeline self-documentation / DAG export.**
Each plugin/wrapper currently knows its inputs and outputs implicitly.
Adding a `--describe` flag that emits a YAML manifest:
```yaml
plugin: ndm_pca
inputs:
  - {path: "{session}.spk.{group}", role: waveforms}
  - {path: "{session}.res.{group}", role: timestamps}
outputs:
  - {path: "{session}.fet.{group}", role: features}
parameters:
  - {name: nFeatures, source: "spikeGroup({group}).nFeatures"}
```
Lets external tools reconstruct the full DAG, generate Snakemake/Nextflow
pipelines, and answer "what depends on this file?" questions.  Useful
for users running custom workflows.  Estimate: 2-3 sessions; benefits
compound across all subsequent automation work.

**Resumable pipeline state.**
Long sessions (`jg05-20120316` is large) take hours.  If `process_pca`
on group 8 fails after groups 1-7 succeeded, you currently rerun
everything because there's no per-group state tracking.  A
`.ndm_state.yaml` per session that tracks which plugin × group
combinations have completed would let `ndm_pca` (and friends) skip
already-done work.  Estimate: 2 sessions for the state machinery; 1
session per plugin for adoption.

**Plugin metadata for klusters integration.**
Klusters' "redetect spikes" / "realign" / "reextract" buttons currently
shell out to `ndm_*` wrappers.  If plugins exposed metadata (description,
parameters, inputs/outputs) klusters could build a more flexible
pipeline-action UI driven by what plugins are installed.  Tied to the
`--describe` work above.  Estimate: 1-2 sessions after `--describe`.

### 💭 considered

**`process_validatedat`: input-integrity checker.**
A new plugin that scans a `.dat` file for likely corruption (constant
samples spanning seconds, NaN/inf in float files, sample-rate mismatch
against XML/YAML metadata, channel-count mismatch).  Useful as a
pre-flight check before long pipelines.  Decision needed: is data
corruption a real problem in your acquisition flow, or extremely rare?

**`process_concat` modernisation.**
The existing `ndm_concatenate` wraps no current C++ binary; it does
shell-level `cat` of `.dat` files.  Works but can't validate channel
ordering or sample rate consistency between inputs.  A C++ binary that
reads input metadata and verifies coherence would prevent silent
multi-session corruption.  Decision needed: is this a real failure mode
or paranoia?

**Plugin parallelism abstraction.**
The parallel-group pattern (`(once $i) > log 2>&1 &; wait; cat log`) is
duplicated across `ndm_pca`, `ndm_pca_stderiv`, `ndm_extractspikes_*`,
`ndm_refeaturize_*`, `ndm_reextractspikes_*`.  Five+ copies of nearly
identical code.  A `parallel_per_group` shell function in `ndm_functions`
that takes a command template would reduce duplication and make
parallelism behaviour change-able in one place.  Decision needed: worth
the refactor, or leave-as-is given it works?

**GPU acceleration of `process_pca`.**
Currently OpenMP-parallel CPU.  PCA on `.spk.N` is the longest single
step in the typical pipeline.  cuBLAS SGEMM + eigh on the covariance
matrix would be ~10-50× faster on Gravio's hardware (RTX 5070 Ti).  But:
adds CUDA dependency to the plugins (currently only KiloKlustaKwik depends
on CUDA), changes deployment story.  Decision needed: is the speedup
worth the deployment complexity?

**`process_localise.py` → spatial localisation as first-class.**
Spike localisation (estimating x/y/z spike origin from amplitude
across channels) is currently Python-only and standalone.  If
localisation features become part of the clustering feature space (some
modern sorters use spatial position as a feature), this plugin would
become integral and deserve C++ implementation + integration with the
PCA path.  Decision needed: research direction first, then engineering.

---

## Cross-cutting

### Wrapper script consistency

The wrapper scripts have grown organically.  Cross-cutting cleanups
worth doing in a consolidated pass:

1. **Bash strict mode adoption.**  Scripts that `source ndm_functions`
   inherit whatever shell options the user has.  Adding `set -euo pipefail`
   to `ndm_functions` would catch many silent failures.  Risk: may break
   existing scripts that rely on lenient behaviour.  Audit needed before
   adoption.

2. **Quoting consistency.**  Many wrappers don't quote `$variable` uses,
   meaning paths with spaces explode.  shellcheck pass over the whole
   `scripts/` directory would find these.

3. **`ndm_functions` modularisation.**  Currently 1278 lines; growing.
   Split by category: `ndm_functions_io` (file utilities),
   `ndm_functions_progress` (banners, progress integration),
   `ndm_functions_groups` (electrode-group iteration),
   `ndm_functions_xml` / `ndm_functions_yaml` (config readers).  Single
   `source ndm_functions` still works (it sources the submodules).

### Plugin metadata standardisation

Toward the `--describe` future: even before that, a `plugin_manifest.yaml`
in each `process_*/` directory that declares inputs/outputs/parameters
would let CMake auto-register plugins, build the man-page index, and
generate the wrapper-script skeleton from the manifest.  Estimate: 1
session for the schema + 1 session per plugin for adoption.

### Diagnostic noise reduction

The progressbar v6 drop suppressed `process_pca_stderiv`'s noisy
"transformed N spikes" line.  Audit other plugins for similar noise:
- `process_extractspikes` — quiet
- `process_pca` — quiet
- `process_medianfilter` — TBD, check
- `process_decomposecollisions` — TBD, check
- `xpathReader` — TBD, often verbose

Standard: plugins should print **only** errors and (under `-v`) major
phase transitions.  Per-spike, per-channel, per-record progress info
goes through `ProgressBar`, not stderr.

---

## Done

(Recent completions, kept for ~1 quarter.)

### ✅ Modern progressbar across `ndm_pca` / `ndm_pca_stderiv` *(this session)*

Six-iteration progressbar modernisation: `/dev/tty` output channel
(bypasses shell redirections), minimalist `━`/`─` horizontal-line style,
green ✓ / red ✗ completion markers, group-numbered labels (`[PCA-7]`),
`\x1b[2K\r` erase-line redraw prefix to handle parallel-execution text
intrusions.  Bash `print_group_failed STEP GROUP` helper in
`ndm_functions` plus failure-detection wiring in `ndm_pca` and
`ndm_pca_stderiv` wrappers.  Suppressed `process_pca_stderiv` noisy
stderr summary line.  Shipped: `klusters-progressbar-modern-v6.tar.gz`.

### ✅ progressbar dedupe (canonical sources in libklustersshared)

`process_pca` and `process_nlxconvert` previously shipped their own
copies of `progressbar.{h,cpp,h}` and `customtypes.h`.  Plugin
CMakeLists now reference the canonical sources in
`src/libklustersshared/src/klustersshared/`; duplicates removed.

### ✅ `process_decomposecollisions` C++ rewrite

Full C++ rewrite from Python, with OpenMP CPU parallelism and cuBLAS
SGEMM GPU batch correlation.  Binary `.col.N` format defined.  u1 fixed
at τ=0; only u2 circularly shifted; self-pairs excluded.  Amplitude
clamping uses per-template projection coefficient distributions
(1st/99th percentile ±50% extension).  Python version `git rm`'d.

### ✅ `ndm_decomposecollisions`, `ndm_reextractspikes`, `ndm_reextractspikes_stderiv`, `ndm_subcluster_unmatched` added to `install(PROGRAMS …)` in `scripts/CMakeLists.txt`.

---

## Dropped

### ❌ Multi-line bar dance for parallel groups

Considered: each parallel `process_pca` group draws its progress bar at a
fixed line offset using ANSI cursor-position escapes.  Visually elegant
but ~50 LOC of fragile coordination across processes that don't share
state.  **Dropped** in favour of single-line `\x1b[2K`-prefixed shared
display — matches what apt and brew do, robust across terminal emulators,
acceptable visual cost.

### ❌ ProgressBar via stderr-only (no /dev/tty)

Considered: emit the bar to stderr instead of opening `/dev/tty`.
Simpler, more portable.  **Dropped** because the wrapper scripts'
parallel pattern `(once $i) > log 2>&1 &` redirects both stdout and
stderr — the stderr-only approach would lose the live bar entirely in
the parallel case.  `/dev/tty` bypasses shell redirection, which is
exactly what the use case needs.

### ❌ Reserving 2 cells for ✓ marker in barWidth

Considered: pre-shrink the bar by 2 cells so `[STEP] ━━━...━━━ ✓` is
exactly 80 cols on completion.  **Dropped** when Gravio relaxed the
hard-80 constraint to "anything under 100 is fine."  The bar is 80
during the run; completion is 82.

### ❌ ProgressBar eighths-block fractional leading edge in horizontal-line style

Considered as part of v4: keep the U+258F-U+2589 vertical eighths-blocks
for fractional progress indication at the leading edge.  **Dropped** in
v4 because the vertical eighths don't visually compose with the
horizontal-line `━`/`─` style — they'd be tall vertical bars between
two thin horizontal lines, mismatched.  Whole-cell fill only; the
eighths-resolution change-detection in the redraw path is retained for
debounce purposes.

---

*This roadmap is descriptive of intent.  Items move sections, retire,
or get re-scoped as work progresses.  Update the date below when you
make changes.*

*Last updated: 2026-04-30.*
