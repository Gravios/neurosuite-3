# CHANGES — neurosuite-3

Top-level changelog for this fork, consolidating every change relative
to the original Neurosuite toolchain.  Entries are grouped by date from
most recent at top.  Deep per-topic technical notes live in the
`CHANGES-<topic>.md` siblings indexed at the end.

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

See `CHANGES-reextractspikes.md`, `CHANGES-reextract-v2.md`,
`CHANGES-decomposecollisions-fixes.md`, `CHANGES-subtractspikes-fixes.md`
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
| KlustaKwik internals, v1.7 → neurosuite-3 diff | `src/klustakwik/CHANGES.md` |
| `reextractspikes` + shadow clustering (first spec) | `CHANGES-reextractspikes.md` |
| `ndm_reextractspikes{,_stderiv}` — extension handling, symlink shims | `CHANGES-reextract-v2.md` |
| `process_decomposecollisions` — collision decomposition bug fixes | `CHANGES-decomposecollisions-fixes.md` |
| `process_subtractspikes` — including `botm` mode (Proepper 2015) | `CHANGES-subtractspikes-fixes.md` |
| `neuroscope` — cluster raster / overlay stall fixes | `CHANGES-neuroscope-raster-fixes.md` |
| `templates/template.yaml` — parameter block refresh | `CHANGES-template-yaml.md` |
| Hardware / OS tuning recipe | `OPTIMIZE.md` |
| Modeling comparison (Layer 1/2 vs BOTM) | `modeling-recommendations.md` |
