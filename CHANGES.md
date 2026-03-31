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
