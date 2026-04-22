# ndmanager-plugins — Preprocessing Pipeline

ndmanager-plugins is a collection of command-line tools that convert raw
acquisition data into the formats required for spike sorting and then carry
those outputs through secondary analyses (drift estimation, collision
decomposition, spike subtraction, re-extraction, source localisation). It
consists of C++ processing binaries (some with optional CUDA acceleration),
Python post-sorting analysis modules, and Bash pipeline scripts that
orchestrate them.

All scripts take a single optional argument: the path to the session `.yaml`
parameter file. When omitted, the current directory name is used as the
session basename. XML parameter files are no longer read directly by the
pipeline — convert legacy files once with `ndm_xml2yaml`.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| CMake, C++20 compiler | Build system | Yes |
| OpenMP | Parallel processing throughout the pipeline | Recommended |
| libhdf5 | AlphaOmega `.mat` (HDF5 v7.3) conversion (`process_aomconvert`) | Yes |
| libxml2 | Legacy XML parameter-file reading (via `ndm_xml2yaml` only) | Yes |
| yaml-cpp | YAML parameter-file reading | Yes |
| python3-yaml | YAML reading in bash pipeline scripts | Yes |
| numpy | `ndm_estimatedrift`, `ndm_applydrift`, `ndm_decomposecollisions`, `ndm_localise`, `ndm_setupgroups` | Yes |
| scipy | Sub-pixel parabolic interpolation in `process_estimatedrift.py` | Optional |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale) | `process_extractleds`, `ndm_transcodevideo` | Optional |
| CUDA Toolkit ≥ 12.8 | GPU acceleration for `process_medianfilter`, `process_medianthreshold` | Optional |

The CUDA filter (`process_medianfilter`) falls back to an OpenMP CPU path
automatically when CUDA is absent.

---

## Pipeline variants and file extensions

Two spike-detection transforms are supported. The choice is encoded
in the filename extension of every downstream artefact. `.clu` and
`.res` have no variant — they are always written under the canonical
extensions regardless of which detection pipeline was used.

| Canonical | stderiv D variant | Writer |
|---|---|---|
| `.res.N` | *(no variant)* | `ndm_extractspikes*`, `ndm_reextractspikes*` |
| `.spk.N` | `.spkD.N` | `ndm_extractspikes` vs `ndm_extractspikes_stderiv` |
| `.fet.N` | `.fetD.N` | `ndm_pca` vs `ndm_pca_stderiv` |
| `.pca.N` | `.pcaD.N` | `ndm_pca` vs `ndm_pca_stderiv` |
| `.clu.N` | *(no variant)* | `ndm_klustakwik`, klusters |

Within a single session, groups can freely mix pipelines. Every downstream
tool that reads waveforms or features auto-detects both extensions and
prefers the D variant when both are present, or picks the one the group
was sorted with. See `src/klustakwik/CHANGES.md` for the fallback logic
inside KlustaKwik, and `CHANGES-reextract-v2.md` for the cross-pipeline
re-extract shim.

---

## Pipeline overview

### AlphaOmega recordings

```
AlphaOmega .mat file
        │
        ▼
ndm_aom2dat                  → SESSION.dat, SESSION.yaml
        │
        ├─ ndm_hipass         → SESSION.fil
        ├─ ndm_lfp            → SESSION.lfp
        ├─ ndm_extractspikes  → SESSION.res.N, SESSION.spk.N     (raw pipeline)
        │     OR
        │  ndm_extractspikes_stderiv → SESSION.res.N, SESSION.spkD.N   (stderiv pipeline)
        ├─ ndm_spikecleaner   → removes spikes with flat / railed channels
        ├─ ndm_denoiseuniform → removes uniform-noise events (raw only)
        ├─ ndm_pca            → SESSION.fet.N, SESSION.pca.N     (raw → raw)
        │     OR
        │  ndm_pca_stderiv    → SESSION.fetD.N, SESSION.pcaD.N   (stderiv → stderiv)
        │
        ▼
ndm_klustakwik               → SESSION.clu.N  [reads calibrated params from YAML]
        │
        ├─ [klusters — manual curation]
        │
        ├─ ndm_reextractspikes{,_stderiv}  → masked second-pass detection +
        │                                     in-place shadow-clustered merge
        │   └─ ndm_subcluster_unmatched   → re-cluster the "unmatched" shadow bin
        │
        ├─ ndm_stripdat       → SESSION-spkclean.dat     (raw / model / botm subtraction)
        │   └─ ndm_redetectspikes   → same flow, re-merged into clu/res/spk
        │         └─ ndm_pca + ndm_klustakwik → re-sort merged set
        │
        ├─ ndm_decomposecollisions  → SESSION.col.N
        ├─ ndm_localise             → SESSION.loc.N (per-spike source position)
        └─ ndm_estimatedrift        → SESSION.drift
              └─ ndm_applydrift     → SESSION.chunks.N (per-sibling-group adaptive chunks)
```

### Other acquisition systems (Neuralynx, CED/Spike2, Amplipex)

The full pipeline is orchestrated by `ndm_start`. Typical execution order:

```
[Format conversion]
ndm_ncs2dat         → SESSION-nlx.dat, SESSION-nlx.sync
ndm_nev2evt         → SESSION-nlx.nlx.evt
ndm_nvt2spots       → SESSION.spots, SESSION.sts
ndm_resample        → resampled .dat (if multiple hardware rates)
ndm_mergedat        → SESSION.dat
ndm_concatenate     → SESSION.dat, SESSION.cat.evt

[Channel setup]
ndm_reorderchannels → reordered SESSION.dat
ndm_setupgroups     → writes anatomicalDescription + spikeDetection groups from probe library
ndm_recolorchannels → update channel colours in YAML (cosmetic; legacy tool)

[Signal processing]
ndm_hipass          → SESSION.fil
ndm_lfp             → SESSION.lfp

[Spike detection — pick one variant per group]
ndm_spikegrouper              → refines spikeDetection groups (optional, after ndm_hipass)
ndm_extractspikes             → SESSION.res.N, SESSION.spk.N                  (raw)
    OR
ndm_extractspikes_sdiff       → spatial-Laplacian detection, raw .spk output  (legacy variant)
    OR
ndm_extractspikes_stderiv     → spatial + temporal derivative detection,
                                stderiv-transformed waveforms in .spkD.N

[Post-detection cleaning]
ndm_spikecleaner              → drop waveforms with flat / railed channels
ndm_denoiseuniform            → drop uniform-amplitude events (raw pipeline)

[PCA feature extraction — match the detection variant]
ndm_pca          → SESSION.fet.N,  SESSION.pca.N       (raw input/output)
    OR
ndm_pca_stderiv  → SESSION.fetD.N, SESSION.pcaD.N      (stderiv input/output)

[Automatic sorting]
ndm_klustakwik      → SESSION.clu.N    (auto-detects .fet vs .fetD per group)

[Cleanup]
ndm_clean           → removes intermediate files

[Post-sorting analyses — run after KlustaKwik + Klusters curation]
ndm_reextractspikes{,_stderiv} → second-pass masked detection + shadowcluster merge
ndm_subcluster_unmatched       → re-cluster the unmatched bin from reextract
ndm_stripdat                   → SESSION-spkclean.dat (raw / model / botm subtraction)
ndm_redetectspikes             → merges recovered spikes; then re-run ndm_pca + ndm_klustakwik
ndm_decomposecollisions        → SESSION.col.N
ndm_localise                   → SESSION.loc.N
ndm_estimatedrift              → SESSION.drift
ndm_applydrift                 → SESSION.chunks.N for sibling groups, optional re-sort
```

Each step is skipped (exit `1`) if its output already exists, making the pipeline
safely re-entrant after interruption.

---

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | Output already exists — step skipped (not an error in batch mode) |
| `2` | Input files missing — step skipped |
| `3` | General error |
| `20` | Missing mandatory parameter |

---

## AlphaOmega conversion

### `ndm_aom2dat` — AlphaOmega `.mat` (HDF5 v7.3) → `.dat` + `.yaml`

Converts an AlphaOmega `.mat` recording to the neurosuite `.dat` binary and
generates a complete session YAML. The YAML includes calibrated per-group
KlustaKwik parameters (`MergeThresh`, `MaxClusters`, merge iterations) for each
spike group, computed from the probe topology. `process_aomconvert` streams the
HDF5 data in configurable chunks; peak RAM is `chunkSamples × nChannels × 2`
bytes regardless of recording length.

HDF5 file locking is disabled at `main()` entry
(`setenv("HDF5_USE_FILE_LOCKING", "FALSE")`) so the binary works cleanly on
NTFS / fuseblk mounts where the default locking mode fails.

**Input:** `matFile` (path to AlphaOmega `.mat` file)
**Output:** `SESSION.dat`, `SESSION.yaml`

The `topology` parameter defines mixed probe configurations. Format:
`FIRST-LAST:SIZE,...` (1-based channel numbers, inclusive). Group type is
inferred from SIZE: `4` → tetrode, `1` → single electrode, any other value
→ linear/silicon shank.

```yaml
- name: ndm_aom2dat
  parameters:
  - {name: matFile,      value: recording.mat,   status: Mandatory}
  - {name: topology,     value: 1-16:16,17-32:4, status: Optional}
  - {name: groups,       value: 8,               status: Optional}
  - {name: chunkSamples, value: 10000000,         status: Optional}
```

When `topology` is absent, channels are split uniformly into groups of
`groups` channels. `chunkSamples` defaults to 10,000,000 (≈ 610 MB for 32
channels — tuned for 64+ GB RAM).

---

## Format conversion scripts

### `ndm_ncs2dat` — Neuralynx `.ncs` → `.dat`

Converts Neuralynx Continuously Sampled files to interleaved int16 `.dat`
format.

**Input:** `../SESSION-nlx/SESSION-nlx.ncs` (one file per channel)
**Output:** `SESSION-nlx.dat`, `SESSION-nlx.sync`

```yaml
- name: ndm_ncs2dat
  parameters:
  - {name: suffixes, value: nlx,   status: Mandatory}
  - {name: reverse,  value: false, status: Mandatory}
```

### `ndm_smr2dat` — CED/Spike2 `.smr` → `.dat`

**Input:** `../SESSION-smr/SESSION-smr.smr`
**Output:** `SESSION-smr.dat`

### `ndm_nev2evt` — Neuralynx `.nev` → `.evt`

Converts Neuralynx event files to the plain-text `.evt` format used by
NeuroScope.

### `ndm_nvt2spots` — Neuralynx `.nvt` → `.spots` / `.sts`

Extracts LED positions and frame timestamps from Neuralynx Video Tracker
files.

### `ndm_smr2evt`, `ndm_smi2sts`, `ndm_tsp2sts`

CED Spike2 events, Neuralynx MPEG subtitle timestamps, and Amplipex
timestamps respectively.

---

## Channel and data manipulation

### `ndm_resample`

Resamples per-subsession `.dat` files from their original hardware rates to
the common target rate. The underlying `process_resample` binary is
OpenMP-parallelised across channels.

```yaml
- name: ndm_resample
  parameters:
  - {name: suffixes,      value: "nlx smr"}
  - {name: samplingRates, value: "32000 30000"}
  - {name: nChannels,     value: "64 32"}
```

### `ndm_mergedat`

Interleaves channels from multiple `.dat` files into a single multiplexed file.

### `ndm_extractchannels`

Removes unwanted channels, writing only the listed channels in listed order.

### `ndm_reorderchannels`

Permutes channel order to match the physical probe layout. Must be run before
`ndm_setupgroups` if the ADC channel numbering does not already match probe
order.

### `ndm_recolorchannels`

Legacy cosmetic tool — updates channel colour attributes in the YAML. Has no
effect on signal processing; provided for compatibility with older
NeuroScope-driven workflows.

### `ndm_concatenate`

Concatenates multi-session `.dat`, `.pos`, and `.evt` files. Creates a
`.cat.evt` file with subsession boundary timestamps.

---

## Group setup

### `ndm_setupgroups` — build channel groups from probe library

Reads the `probes:` section of the session YAML, loads each referenced
`.probe` file from the probe library, and writes
`anatomicalDescription.channelGroups` and `spikeDetection.channelGroups`
into the session YAML in-place.

Must be run **before** `ndm_extractspikes{,_sdiff,_stderiv}`. For
AlphaOmega sessions, `ndm_aom2dat` writes the groups directly —
`ndm_setupgroups` is not needed.

`probeId`, `shankIndex`, and `sitePositions_um` (inline electrode
geometry) are written into each anatomical and spike group so that
`ndm_estimatedrift` and `ndm_localise` can reconstruct probe geometry
without re-loading the probe file. The `probes[].anatomicalGroups`
field is updated with the assigned group IDs. The canonical probe
key is `probes[].probeId` (older files used `probes[].id`; both are
still read but only `probeId` is written).

**Channel assignment** — when `channelMap.map` is null (all supplied NeuroNexus files):

```
shank i → [offset + i × n_per_shank, …, offset + (i+1) × n_per_shank − 1]
```

**Probe library search order** (lowest → highest priority):

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH  (colon-separated)
SESSION_DIR/probes/
```

```yaml
- name: ndm_setupgroups
  parameters:
  - {name: nSamples,        value: 52,    status: Optional}
  - {name: peakSampleIndex, value: 26,    status: Optional}
  - {name: nFeatures,       value: 3,     status: Optional}
  - {name: overwrite,       value: false, status: Optional}
```

---

## Signal processing

### `ndm_hipass` — high-pass filter (`.dat` → `.fil`)

Applies a median-subtraction high-pass filter. `windowHalfLength=16` at
30 kHz ≈ 600 Hz cutoff. Uses CUDA when available, otherwise OpenMP.

**Input:** `SESSION.dat`
**Output:** `SESSION.fil`

```yaml
- name: ndm_hipass
  parameters:
  - {name: windowHalfLength, value: 16,        status: Mandatory}
  - {name: chunkSize,        value: 134217728,  status: Optional}
```

### `ndm_lfp` — create LFP file (`.dat` → `.lfp`)

Downsamples the wideband `.dat` to the LFP sampling rate (typically 1250 Hz).
Automatically uses `SESSION-spkclean.dat` (from `ndm_stripdat`) when it is
present.

---

## Spike detection

Three detection variants are provided. Pick one per electrode group.
Each writes timestamps to `.res.N` and waveforms to the group's
`spk` extension (`.spk.N` for raw, `.spkD.N` for stderiv).

### `ndm_extractspikes` — raw threshold detection

Detects threshold crossings on the high-pass filtered signal and extracts
raw waveform snippets from `.fil`. Writes the waveform with no transform;
this is the "traditional" pipeline.

```yaml
- name: ndm_extractspikes
  parameters:
  - {name: thresholdFactor,  value: 1.5, status: Mandatory}
  - {name: refractoryPeriod, value: 25,  status: Mandatory}
  - {name: peakSearchLength, value: 50,  status: Mandatory}
```

### `ndm_extractspikes_sdiff` — spatial-Laplacian detection (legacy variant)

Drop-in replacement for `ndm_extractspikes`. Applies a discrete Laplacian
across probe channels before thresholding to suppress common-mode noise,
while writing original (un-differentiated) waveforms to `.spk.N`.
Superseded in most workflows by `ndm_extractspikes_stderiv`; kept for
compatibility with earlier sessions.

### `ndm_extractspikes_stderiv` — spatial + temporal derivative detection

Detection runs on `stderiv[t, ch] = sdiff[t, ch] − sdiff[t−1, ch]`,
where `sdiff` is the spatial derivative across the group's channels.
Written waveforms are **transformed** — the `.spkD.N` file contains
the stderiv-space waveform directly, not the raw signal. This is the
recommended pipeline for high-density probes: common-mode noise is
rejected twice (spatial, then temporal) and downstream PCA sees a
dramatically cleaner signal.

Four spatial orders are supported:

| Order | Name | Formula |
|---|---|---|
| `0` | SDIFF_NONE | `s[i] = x[i]` (temporal first-difference only) |
| `1` | SDIFF_FIRST | `s[i] = x[i] − x[i+1]` (nearest-neighbour) |
| `2` | SDIFF_LAPLACIAN | `s[i] = x[i] − 0.5 × (x[i-1] + x[i+1])` (discrete Laplacian) |
| `3` | SDIFF_ALLPAIRS | `s[i] = n × x[i] − Σⱼ x[j]` (default; no probe-order requirement) |

Orders 1 and 3 produce a rank-deficient transform (one linearly
dependent channel), handled automatically by `ndm_pca_stderiv`.

### `ndm_spikecleaner` — drop flat / railed waveforms

Examines every `spikeDetection` group and removes spike waveforms where
one or more channels are entirely flat: either all-zero (ADC cutout,
hardware disconnection) or stuck at a constant non-zero value (railed
amplifier, DAC fault). Run after any of the extraction variants and
before PCA — these degenerate waveforms would otherwise produce
degenerate feature vectors and corrupt cluster models.

### `ndm_denoiseuniform` — remove uniform-noise events

Removes electrically uniform events (common-mode artefacts, motion noise,
electrical interference) from `.spk.N`/`.res.N` after raw spike extraction.
Run before `ndm_pca`. Backs up originals to `SESSION_denoise_backup/`.

For the stderiv pipeline, the detection stage already rejects most
common-mode events, so this step is usually not needed.

```yaml
- name: ndm_denoiseuniform
  parameters:
  - {name: uniformityThreshold, value: 0.30, status: Optional}
  - {name: removeFlat,          value: 1,    status: Optional}
  - {name: dryRun,              value: 0,    status: Optional}
```

---

## Electrode group discovery

### `ndm_spikegrouper` — automatic spike group refinement (`.fil` → updated YAML)

Discovers optimal spike detection channel groups from the high-pass
filtered data. Must be run after `ndm_hipass` and before
`ndm_extractspikes`. The `anatomicalDescription` section is left untouched.

```yaml
- name: ndm_spikegrouper
  parameters:
  - {name: maxMergedSize, value: 12, status: Mandatory}
  - {name: minChannels,   value: 4,  status: Mandatory}
```

---

## PCA feature extraction

### `ndm_pca` — raw-waveform PCA (`.spk` → `.fet`, `.pca`)

Computes principal component features from raw spike waveforms.
`process_pca` is OpenMP-parallelised across electrode groups.
Writes `.fet.N` (features, with timestamp column) and `.pca.N` (the
eigenvector basis used for projection — needed by klusters realignment
and by reextract workflows).

```bash
ndm_pca session.yaml       # all electrode groups (parallel)
ndm_pca session.yaml 3     # single electrode group
```

### `ndm_pca_stderiv` — stderiv-waveform PCA (`.spkD` → `.fetD`, `.pcaD`)

PCA on stderiv-transformed waveforms. Reads `.spkD.N`, passes the
waveforms through `process_pca_stderiv` (channel reduction for
rank-deficient orders 1 and 3), then through `process_pca` on the
reduced channel set. Writes `.fetD.N` and `.pcaD.N`. Klusters and
KlustaKwik auto-detect the D variant at open time.

For SDIFF_FIRST and SDIFF_ALLPAIRS (orders 1 and 3), one channel is
linearly dependent on the others; `process_pca_stderiv` drops it before
PCA so `pca.nChannels = nElectrodes − 1` for those orders. SDIFF_NONE
and SDIFF_LAPLACIAN (orders 0 and 2) preserve full rank, so
`pca.nChannels = nElectrodes`. Downstream consumers (shadowcluster,
klusters, KlustaKwik) accept `pca.nChannels ≤ nChanGroup` and skip
the dropped channel automatically.

---

## Automatic spike sorting

### `ndm_klustakwik` — run KlustaKwik for each spike group (`.fet.N`/`.fetD.N` → `.clu.N`)

Runs KlustaKwik sequentially for each `spikeDetection` group. KlustaKwik
is internally multi-threaded (OpenMP across chunks and runs); groups
run sequentially to avoid CPU contention. Accepts either `.fet.N` or
`.fetD.N` per group — the choice is made inside KlustaKwik at `LoadData`
via `pickInputPath`, and the picked variant is propagated through every
subsequent session-file open (Phase 1.5 checkpoint, template matching,
realignment), so stderiv-sorted and raw-sorted groups can coexist in a
single session without cross-contamination.

**Three-tier parameter resolution** — every KlustaKwik parameter is
resolved in priority order:

1. `spikeDetection.channelGroups[g].klustakwik.<param>` — per-group
   calibrated values (highest priority). `ndm_aom2dat` writes
   probe-type-calibrated defaults here:
   `MergeThresh = χ²(nCh × 3, 0.9999)`, `MaxClusters` by probe type,
   merge iterations scaled by √(`nSpatialDims`/24).

2. `programs[ndm_klustakwik].parameters.<param>` — global session
   override (this block). Edit to change a parameter for all groups at
   once.

3. Built-in bash defaults in the `ndm_klustakwik` script.

The same resolution function (`read_kk_param`, hoisted into
`ndm_functions`) is used by `ndm_subcluster_unmatched` so sandbox
re-clustering sees identical parameters to the main sort.

**Calibrated defaults** (written by `ndm_aom2dat`):

| Probe type | `nSpatialDims` | `MergeThresh` | `MaxClusters` | `GlobalMergeIter` |
|---|---|---|---|---|
| linear 16ch | 48 | 93.22 | 40 | 70 |
| tetrode 4ch | 12 | 39.13 | 20 | 50 |
| single 1ch | 3 | 21.11 | 5 | 40 |

**Global session-level parameters** (tier 2 — same for all groups):

```yaml
- name: ndm_klustakwik
  parameters:
  - {name: minClusters,              value: 2,        status: Optional}
  - {name: useFeatures,              value: all,      status: Optional}
  - {name: nStarts,                  value: 1,        status: Optional}
  - {name: nRuns,                    value: 3,        status: Optional}
  - {name: initMethod,               value: farthest, status: Optional}
  - {name: chunkMinutes,             value: 10.0,     status: Optional}
  - {name: chunkOverlapMinutes,      value: 2.0,      status: Optional}
  - {name: chunkPreseedFraction,     value: 0.05,     status: Optional}
  - {name: splitRecurseDepth,        value: 1,        status: Optional}
  - {name: subspaceRecluster,        value: 1,        status: Optional}
  - {name: subspaceReclusterDepth,   value: 2,        status: Optional}
  - {name: subspaceDims,             value: 3,        status: Optional}
  - {name: templateMatchIters,       value: 10,       status: Optional}
  - {name: templateMatchScore,       value: 0.90,     status: Optional}
  - {name: crossChunkTemplateScore,  value: 0.90,     status: Optional}
  - {name: maxIter,                  value: 500,      status: Optional}
```

See `doc/klustakwik/README.md` for full parameter semantics and the
phase diagram of the chunked CEM pipeline.

```bash
ndm_klustakwik session.yaml    # all groups sequentially
ndm_klustakwik session.yaml 3  # single group
```

---

## Post-sorting: second-pass re-extraction

### `ndm_reextractspikes` / `ndm_reextractspikes_stderiv` — masked second-pass detection + shadow merge

Second-pass spike detection that runs at a reduced threshold while
masking out timestamps already present in `.res.N`. New spikes are
then shadow-clustered into the existing `.clu.N`: each new spike is
assigned by robust Mahalanobis distance (per-dim median + MAD × 1.4826)
in PCA feature space to the nearest eligible parent cluster; spikes
exceeding the χ² threshold from every parent land in a single
"unmatched" bin.

Two variants share all logic except detection signal and waveform
value space:

- `ndm_reextractspikes` — detection on the raw high-pass signal;
  waveforms written raw to `.spk.N`.
- `ndm_reextractspikes_stderiv` — detection on the stderiv signal
  (same spatial + temporal first-difference transform as
  `ndm_extractspikes_stderiv`); waveforms **also written in stderiv
  space** to `.spkD.N` so they stay in the same value space as the
  reference rows and the `.pcaD.N` basis they will be projected
  through.

Both scripts are transactional: Pass 2 writes to a sandbox stem
(`$mergeStem.*`) and only commits (atomic `mv`) to the live session
files after every output file is verified present and non-empty. On
failure, live files are byte-identical to their pre-run state. Per-group
backups (`*.res.N.bak`, `*.spk.N.bak` / `*.spkD.N.bak`, `*.clu.N.bak`,
`*.fet.N.bak` / `*.fetD.N.bak`) are written on first merge so a full
rollback is always a `mv` away.

```yaml
- name: ndm_reextractspikes_stderiv
  parameters:
  - {name: reextractThresholdFactor, value: 0.75, status: Optional}
  - {name: reextractMaskHalfWidth,   value: 16,   status: Optional}
  - {name: reextractMinClusterSize,  value: 50,   status: Optional}
  - {name: reextractChi2,            value: 0.9999, status: Optional}
  - {name: reextractExtraFeat,       value: 0,    status: Optional}
  - {name: sdiffOrder,               value: 3,    status: Optional}
```

See `CHANGES-reextractspikes.md` and `CHANGES-reextract-v2.md` for
algorithm details and extension-handling nuances, and the 2026-04-22
entry in `CHANGES.md` for the stderiv value-space fix.

### `ndm_subcluster_unmatched` — re-cluster the unmatched bin

After a reextract pass, each group's `.clu.N` contains a single large
"unmatched" cluster holding every new spike that exceeded the χ²
threshold from every eligible parent. In typical sessions this bin is
heterogeneous — mixing genuinely novel units with tail-of-distribution
outliers from existing units.

`ndm_subcluster_unmatched` extracts the unmatched bin to a sandbox
`.fet` / `.res` / `.spk` triple, runs a full KlustaKwik invocation on it
(27 parameters, parity with `ndm_klustakwik` via the shared
`read_kk_param` function), maps the resulting cluster IDs back into
the host session's `.clu.N` above the existing max ID, and preserves
backups. Designed to be run once per group after every reextract pass;
the sandbox clustering uses the same three-tier parameter resolution
as `ndm_klustakwik`.

---

## Post-sorting: waveform subtraction

### `ndm_stripdat` — subtract spikes to produce a cleaned `.dat`

Subtracts spike waveforms from the raw `.dat` to produce
`SESSION-spkclean.dat`. Three subtraction modes:

| Mode | Description |
|---|---|
| `model` (default) | Per-cluster template model with projection-based amplitude scaling and ISI-driven burst-shape compensation. Uses `.clu.N` when present. |
| `raw` | Subtracts raw `.spk.N` waveforms directly. Used when no curation has happened yet. |
| `botm` | Bayes-Optimal Template Matching (Proepper 2015). ~100× better residuals than `model` mode on the synthetic test fixture while preserving the noise baseline. |

The original `.dat` is **never modified**. `SESSION-spkclean.dat` is always
overwritten on re-run so it can be regenerated after each curation pass.
`ndm_lfp` automatically uses `SESSION-spkclean.dat` when it exists.

When `autoStrip=true`, the per-group strip sidecars are generated from
YAML `Quality` labels automatically — no manual file maintenance between
curation rounds.

```yaml
- name: ndm_stripdat
  parameters:
  - {name: redetectSpikes,   value: 'no',      status: Optional}
  - {name: stripClusters,    value: 'default', status: Optional}
  - {name: subtractionMode,  value: 'model',   status: Optional}
  - {name: autoStrip,        value: 'true',    status: Optional}
  - {name: autoStripQuality, value: '1,2,3',   status: Optional}
```

See `CHANGES-subtractspikes-fixes.md` and
`modeling-recommendations.md` for the BOTM derivation and a side-by-side
comparison with the Layer 1/2 template model.

### `ndm_redetectspikes` — second-round detection on cleaned `.dat`

After `ndm_stripdat`, runs the full spike detection pipeline
(`ndm_hipass` → `ndm_extractspikes`) on `SESSION-spkclean.dat` to
recover spikes that were missed in the first pass. Merges newly
detected spikes into the existing `.res` / `.spk` / `.clu` files via
`process_mergespikes`, then deletes `.fet.N` (spike order changed).
Re-run `ndm_pca` and `ndm_klustakwik` to rebuild features and re-sort
the merged set.

**Iterative refinement workflow:**

```bash
ndm_klustakwik session.yaml     # initial sort
# (klusters — curate pass 1)
ndm_stripdat session.yaml       # subtract pass-1 clusters
ndm_redetectspikes session.yaml # detect missed spikes
ndm_pca session.yaml            # rebuild features
ndm_klustakwik session.yaml     # re-sort merged set
# (klusters — curate pass 2)
ndm_lfp session.yaml            # LFP from improved cleaned dat
```

`ndm_reextractspikes` is an alternative, less destructive path — it
appends new spikes to the existing `.res.N` in chronological order
rather than overwriting the feature file.

---

## Post-sorting analysis

### `ndm_decomposecollisions` — collision decomposition (`.col.N`)

Identifies and decomposes spike waveforms representing two
near-simultaneous spikes. Never modifies the original `.clu.N` /
`.res.N` / `.spk.N` files. Requires curated `.clu.N` files.

**Algorithm:** (1) Build mean templates from curated clusters.
(2) Flag candidates with normalised cross-correlation below
`corrThreshold`. (3) Fit all same-shank pairwise template combinations
with shifts up to `maxShiftSamp`. (4) Accept when residual RMS fraction
< `residualThreshold`.

```yaml
- name: ndm_decomposecollisions
  parameters:
  - {name: maxShiftSamp,      value: 10,    status: Optional}
  - {name: corrThreshold,     value: 0.85,  status: Optional}
  - {name: residualThreshold, value: 0.25,  status: Optional}
  - {name: minSnrRms,         value: 4.0,   status: Optional}
  - {name: minSpikesTemplate, value: 30,    status: Optional}
  - {name: excludeNoise,      value: true,  status: Optional}
```

Results are visualised with `collision_viewer.py`.

### `ndm_localise` — per-spike source localisation (`.loc.N`)

Fits a monopole (point-source) extracellular potential model per spike:

```
V_i(x_s, y_s, z_s, A) = A / sqrt((x_i - x_s)² + (y_i - y_s)² + z_s²)
```

where `(x_i, y_i)` are the known electrode site positions (from the
YAML `sitePositions_um` inline or from the probe library) and
`(x_s, y_s)` is the source position in the plane of the shank; `z_s`
is the perpendicular distance from the shank face. `A` is an unsigned
amplitude. Writes `.loc.N` with one row per spike
(`x_s, y_s, z_s, A`, + fit residual).

### `ndm_estimatedrift` — spatial probe drift estimation (`.drift`)

Estimates in-vivo probe drift from curated spike-sorting results.
Requires `.res.N`, `.clu.N`, and optionally `.spk.N`. Reads `probeId` /
`shankIndex` / `sitePositions_um` from the YAML (written by
`ndm_setupgroups` or `ndm_aom2dat`) to reconstruct site geometry.

**Primary method:** per-unit amplitude-vs-depth fingerprint
cross-correlation with sub-site parabolic interpolation.
Weighted-median drift estimate per shank.
**Fallback:** population-level spatial fingerprint cross-correlation
when unit yield is low.

```yaml
- name: ndm_estimatedrift
  parameters:
  - {name: windowSec,    value: 60,   status: Optional}
  - {name: minUnits,     value: 3,    status: Optional}
  - {name: minSpikes,    value: 20,   status: Optional}
  - {name: excludeNoise, value: true, status: Optional}
```

Accepts both the binary and legacy text `.clu` formats. `n_samp` is
read from YAML per group (one value per electrode group, passed via
`--n-samples-per-group`).

### `ndm_applydrift` — propagate curated drift to sibling shanks

Reads `SESSION.drift`, extracts the drift timeseries for a source
spike group, and writes adaptive chunk-boundary files (`SESSION.chunks.N`)
for each sibling group on the same probe. Each sibling's KlustaKwik run
can then be re-driven with chunk boundaries aligned to the real drift
trajectory rather than uniform time windows — dramatically improving
per-chunk cluster quality on chronic recordings with significant drift.

```bash
ndm_applydrift session.yaml 7           # source group 7; all siblings
ndm_applydrift session.yaml 7 3 5 9     # source group 7; siblings 3, 5, 9 only
```

With `-r` (re-run) passed on the command line, `ndm_klustakwik` is
immediately invoked for each target group after its `.chunks.N` is
written.

---

## Video processing

### `ndm_extractleds`

Detects bright pixels per video frame by thresholding the luma channel.
Requires FFmpeg dev headers at build time.

### `ndm_transcodevideo`

Converts video to MPEG-1 or H.264 using FFmpeg.

---

## Format conversion (YAML)

### `ndm_xml2yaml` — convert XML parameter file to YAML

```bash
ndm_xml2yaml session.xml             # writes session.yaml in the same directory
ndm_xml2yaml session.xml output.yaml # explicit output path
```

After conversion, populate the `probes:` section and run
`ndm_setupgroups` to rebuild the channel groups in the YAML format.
The rest of the pipeline works from YAML only — there is no XML-reading
path in the current extraction, PCA, or KlustaKwik stages.

---

## Cleanup

### `ndm_clean`

Removes intermediate files after a successful pipeline run.

---

## Shared infrastructure

### `ndm_functions`

Sourced by every `ndm_*` script. Provides:

- `yaml_read`, `yaml_count` — pyyaml-based querying of the session YAML.
- `read_script_parameter` — three-tier parameter resolution for pipeline
  scripts (per-group YAML → global YAML → bash default).
- `read_kk_param` — same three-tier resolution scoped to
  `programs[ndm_klustakwik].parameters`, used by both `ndm_klustakwik`
  and `ndm_subcluster_unmatched` so the sandbox sort is parameter-
  identical to the main sort.
- `check_command_status`, `outputs_exist`, `check_commands_installed`,
  `print_header`, `echo_info`, `echo_warning`, `echo_error` — logging
  and exit-code conventions.

Not a user-facing tool, but the file to edit when adding a new plugin
or a new three-tier parameter.

---

## File formats

### `.dat` / `.lfp` / `.fil` — binary signal files

Raw int16, little-endian, channel-interleaved, no header.
File size = `nSamples × nChannels × 2` bytes.

### `.res.N` — spike timestamps

Binary. No header. `nSpikes × int64_t`, little-endian. One sample index
per spike. File size = `nSpikes × 8` bytes.

### `.spk.N` / `.spkD.N` — spike waveforms

Binary. No header. `nSpikes × nSamples × nChannels` int16 samples,
**sample-major** layout (all channels for sample 0 first, then all
for sample 1, etc.). File size = `nSpikes × nSamples × nChannels × 2`
bytes.

- `.spk.N`: raw waveform snippets from `.fil` (`ndm_extractspikes`,
  `ndm_extractspikes_sdiff`, `ndm_reextractspikes`).
- `.spkD.N`: stderiv-transformed waveforms — spatial derivative across
  group channels + temporal first-difference with `sdiff[-1] = 0`
  boundary. Written by `ndm_extractspikes_stderiv` and
  `ndm_reextractspikes_stderiv`; bit-identical in layout and value
  space between the two.

### `.fet.N` / `.fetD.N` — PCA feature vectors

Binary. `int32_t` nDimensions header (= `nChannels × nPCs + 2`,
including extra features and timestamp). Then
`nSpikes × nDimensions × int64_t`, row-major. Last column is the spike
timestamp (sample index). Written by `process_mergefeatures`; read by
KlustaKwik and klusters. Canonical `.fet.N` pairs with `.pca.N`;
`.fetD.N` pairs with `.pcaD.N`.

### `.pca.N` / `.pcaD.N` — PCA eigenvector basis

Binary. 5-int32 header
(`nCh`, `data2use`, `nComp`, `isCentered`, `recShift`), followed by
per-channel double-precision means and eigenvectors. Written by
`process_pca`; read by klusters (realignment, nudge) and
`process_shadowcluster` to reproject waveforms when new spikes arrive
after the initial sort.

### `.clu.N` — cluster assignments

Binary (canonical) or plain-text (legacy). Binary format: `int32_t`
header (number of clusters) followed by `int32_t` cluster IDs, one per
spike. Text format: one line with the cluster count, then one cluster
ID per spike, newline-separated.

Cluster ID conventions: `0` = noise/artefact, `1` = unsorted MUA,
`≥ 2` = candidate single units. Same spike order as `.res.N`.

### `.col.N` — collision decomposition results (YAML)

Written by `ndm_decomposecollisions`. One entry per candidate collision
spike. Original `.clu.N` / `.res.N` / `.spk.N` files are not modified.

```yaml
collisions:
  format: '1.0'
  spikeGroup: 1
  spikes:
    - spikeIndex: 4721
      isCollision: true
      components:
        - unit: 3
          shift: -2
          amplitude: 0.97
        - unit: 7
          shift: 8
          amplitude: 0.84
```

### `.drift` — probe drift trajectories (YAML)

Written by `ndm_estimatedrift`. Per-probe, per-shank drift in µm per
time window. Consumed by `ndm_applydrift`.

```yaml
drift:
  format: '1.0'
  method: unit_com
  windowSec: 60.0
  probes:
    - probeId: 0
      shanks:
        - shankIndex: 0
          spikeGroup: 1
          nUnitsTotal: 8
          windows:
            - {t_start: 0.0,  t_end: 60.0,  drift_um: 0.0}
            - {t_start: 60.0, t_end: 120.0, drift_um: -1.8}
```

### `.chunks.N` — adaptive KlustaKwik chunk boundaries

Written by `ndm_applydrift`. One chunk per line:
`start_sample end_sample`. KlustaKwik reads this file (when the
`-ChunkBoundaries` flag is passed) in place of computing uniform chunks
from `ChunkMinutes`.

### `.loc.N` — per-spike source locations (binary)

Written by `ndm_localise`. One row per spike, five float32 values:
`x_s`, `y_s`, `z_s`, `A`, `residual`. Visualised by
`process_localise.py --plot`.

### `.probe` — probe configuration (YAML)

Read by `ndm_setupgroups`, `ndm_estimatedrift`, `ndm_localise`. Stored
in the probe library at `${CMAKE_INSTALL_DATADIR}/neurosuite/probes/`.
Entries in the session YAML `probes:` section reference files by path
relative to the library root (e.g. `neuronexus/Buzsaki64.probe`).

---

## Performance notes

`process_aomconvert` streams HDF5 data in chunks via the HDF5 C
hyperslab API. Default chunk size (10M samples × 32 channels × 2 bytes
≈ 610 MB) is tuned for systems with 64+ GB RAM. Reduce `chunkSamples`
on memory-constrained machines.

`process_medianfilter` (used by `ndm_hipass`) processes data in
configurable chunks. CUDA targets: sm_86 (Ampere), sm_89 (Ada
Lovelace), sm_100 (Hopper), sm_120 (Blackwell). Requires CUDA ≥ 12.8
for sm_120 (RTX 5000 series).

`process_resample`, `process_pca`, `process_pca_stderiv`,
`process_shadowcluster`, `process_reextractspikes`,
`process_reextractspikes_stderiv`, and `process_extractspikes_stderiv`
all use OpenMP. `ndm_pca{,_stderiv}` runs all electrode groups in
parallel with a configurable per-group thread budget
(`threadsPerGroup`).

`process_setupgroups.py`, `process_estimatedrift.py`,
`process_applydrift.py`, `process_decomposecollisions.py`, and
`process_localise.py` require Python 3 with `pyyaml` and `numpy`.
`scipy` is optional in `process_estimatedrift.py` (used for sub-pixel
parabolic interpolation when available).

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Windows | CPU (native) or full pipeline via WSL2 | [install/windows.md](install/windows.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |
