# CHANGES — neurosuite-3

All changes relative to the original Neurosuite toolchain unless noted.

---

## KlustaKwik

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

---

## ndmanager-plugins

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

### All 15 ndm scripts

`xml_read`/`xml_count` replaced with `yaml_read`/`yaml_count` (69 replacements):
`ndm_concatenate`, `ndm_denoiseuniform`, `ndm_extractspikes`,
`ndm_extractspikes_sdiff`, `ndm_hipass`, `ndm_klustakwik`, `ndm_lfp`,
`ndm_localise`, `ndm_pca`, `ndm_redetectspikes`, `ndm_reorderchannels`,
`ndm_resample`, `ndm_spikegrouper`, `ndm_start`, `ndm_stripdat`.

### ndm_klustakwik

- `nRuns` parameter read and passed to KlustaKwik.
- `NbSamplesPerSpike` no longer needs to be passed explicitly (auto-detected from
  YAML), but `nbSamplesPerSpike` is kept in the parameter block for override.

---

## jg05-20120316.yaml

- All 13 `spikeDetection.channelGroups` entries annotated with `probeId`,
  `shankIndex`, and `sitePositions_um` (inline electrode site geometry).
- Groups 1–8: Buzsaki64L octrode geometry (0–140 µm depth per shank, 8 sites).
- Groups 9–13: A1x32-6mm-50-177 linear geometry (0–1550 µm, 32 sites total,
  tiled across 5 ndm_spikegrouper subgroups).
- All 9 `anatomicalDescription.channelGroups` annotated with `probeId`/`shankIndex`.
- `probes[].id` → `probes[].probeId` (canonical key).
- `ndm_klustakwik.nbSamplesPerSpike` corrected: 32 → 41 (matches actual `.spk` files).
- Probe 1 entry corrected to `A1x32-6mm-50-177.probe` (was `A1x32-6mm-50-177.probe`
  but referenced via the wrong label `A1x32-6mm-50-177`).

---

## KlustaKwik — session 6 (2026-04-02)

### TrySplits: split trial fixes

- `CEM(nullptr, 0)` → `CEM(nullptr, 1)` in the split trial: the split candidate
  CEM now runs with `enableSplits=true`, letting `TrySplits` find the best
  multi-cluster solution rather than a single random initialisation. Previously
  the split trial only got one EM pass from a random seed.
- Split trial `nStartingClusters` raised from 3 → 13 (noise + 12 real), giving
  the split CEM sufficient starting clusters to explore the full structure of each
  candidate cluster.
- Infinite recursion fix: `static thread_local int _trySplitsDepth` counter added.
  Split trials at depth > `SplitRecurseDepth` use `CEM(nullptr, 0)` (no further
  recursion). Safe for OMP parallel regions.

### New parameter: `-SplitRecurseDepth N` (default 1)

Controls how many levels of recursive splitting `TrySplits` explores.
`0` = original behaviour (no recursive splits); `1` (default) = one level;
`2` = two levels. Each level multiplies the cost of `TrySplits` proportionally.

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

### New parameter: `-SubspaceReclusterDepth N` (default 0)

Controls `TrySplits` recursion depth inside SubspaceRecluster sub-CEMs,
independently of `-SplitRecurseDepth`. Default `0` = one controlled level of
splitting (TrySplits fires but split trials are non-recursive).

### MergeChunkModels: iterative cross-chunk passes

The entire chunk-pair loop (votes + Mahal + xcorr) now iterates up to
`-TemplateMatchIters` times, breaking when no new `Union()` calls occur.
Enables cascade matching: a new xcorr merge in pair (k, k+1) can unlock
previously-unresolved clusters in pair (k+1, k+2).

### Progress banners

Phase 1 OMP loop now emits per-run progress to `stderr` inside a critical section:
```
  [chunk 3/9  run 2/3] score=-1243.6  nclusters=12
```

### Build date

Updated to `2026-04-02 chunked-pipeline`.
