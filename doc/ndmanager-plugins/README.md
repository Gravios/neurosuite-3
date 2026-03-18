# ndmanager-plugins — Preprocessing Pipeline

ndmanager-plugins is a collection of command-line tools that convert raw acquisition data into the
formats required for spike sorting. It consists of C++ processing binaries (some with optional
CUDA acceleration) and Bash pipeline scripts that call them.

All scripts take a single optional argument: the path to the session `.yaml` parameter file.
When omitted, the current directory name is used as the session basename.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| CMake, C++20 compiler | Build system | Yes |
| OpenMP | Parallel processing in `process_resample`, `process_pca` | Recommended |
| libhdf5 | AlphaOmega .mat (HDF5 v7.3) conversion (`process_aomconvert`) | Yes |
| libxml2 | XML parameter file reading | Yes |
| yaml-cpp | YAML parameter file reading | Yes |
| python3-yaml | YAML reading in bash pipeline scripts | Yes |
| numpy | `ndm_estimatedrift`, `ndm_decomposecollisions`, `ndm_setupgroups` | Yes |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale) | `process_extractleds`, `ndm_transcodevideo` | Optional |
| CUDA Toolkit ≥ 12.8 | GPU acceleration for `process_medianfilter`, `process_medianthreshold` | Optional |

The CUDA filter (`process_medianfilter`) falls back to an OpenMP CPU path automatically when
CUDA is absent.

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
        ├─ ndm_extractspikes  → SESSION.res.N, SESSION.spk.N
        ├─ ndm_denoiseuniform → removes uniform-noise events from .spk.N/.res.N
        ├─ ndm_pca            → SESSION.fet.N
        │
        ▼
ndm_klustakwik               → SESSION.clu.N  [reads calibrated params from YAML]
        │
        ├─ [klusters — manual curation]
        │
        ├─ ndm_stripdat       → SESSION-spkclean.dat
        │       └─ ndm_redetectspikes → merge recovered spikes
        │              └─ ndm_pca + ndm_klustakwik → re-sort merged set
        │
        ├─ ndm_decomposecollisions  → SESSION.col.N
        └─ ndm_estimatedrift        → SESSION.drift
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

[Signal processing]
ndm_hipass          → SESSION.fil
ndm_lfp             → SESSION.lfp

[Spike detection]
ndm_spikegrouper    → refines spikeDetection groups (optional, after ndm_hipass)
ndm_extractspikes   → SESSION.res.N, SESSION.spk.N
ndm_denoiseuniform  → removes uniform-noise events (optional, before ndm_pca)
ndm_pca             → SESSION.fet.N

[Automatic sorting]
ndm_klustakwik      → SESSION.clu.N

[Cleanup]
ndm_clean           → removes intermediate files

[Post-sorting analysis — run after KlustaKwik + Klusters curation]
ndm_stripdat              → SESSION-spkclean.dat
ndm_redetectspikes        → merges recovered spikes; then re-run ndm_pca + ndm_klustakwik
ndm_decomposecollisions   → SESSION.col.N
ndm_estimatedrift         → SESSION.drift
```

Each step is skipped (exit `1`) if its output already exists, making the pipeline safely
re-entrant after interruption.

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

### `ndm_aom2dat` — AlphaOmega .mat (HDF5 v7.3) → .dat + .yaml

Converts an AlphaOmega `.mat` recording to the neurosuite `.dat` binary and generates a
complete session YAML. The YAML includes calibrated per-group KlustaKwik parameters
(MergeThresh, MaxClusters, merge iterations) for each spike group, computed from the probe
topology. `process_aomconvert` streams the HDF5 data in configurable chunks; peak RAM is
`chunkSamples × nChannels × 2 bytes` regardless of recording length.

**Input:** `matFile` (path to AlphaOmega `.mat` file)
**Output:** `SESSION.dat`, `SESSION.yaml`

The `topology` parameter defines mixed probe configurations. Format: `FIRST-LAST:SIZE,...`
(1-based channel numbers, inclusive). Group type is inferred from SIZE: `4` → tetrode,
`1` → single electrode, any other value → linear/silicon shank.

```yaml
- name: ndm_aom2dat
  parameters:
  - {name: matFile,      value: recording.mat,   status: Mandatory}
  - {name: topology,     value: 1-16:16,17-32:4, status: Optional}
  - {name: groups,       value: 8,               status: Optional}
  - {name: chunkSamples, value: 10000000,         status: Optional}
```

When `topology` is absent, channels are split uniformly into groups of `groups` channels.
`chunkSamples` defaults to 10,000,000 (≈ 610 MB for 32 channels — tuned for 64+ GB RAM).

---

## Format conversion scripts

### `ndm_ncs2dat` — Neuralynx .ncs → .dat

Converts Neuralynx Continuously Sampled files to interleaved int16 `.dat` format.

**Input:** `../SESSION-nlx/SESSION-nlx.ncs` (one file per channel)
**Output:** `SESSION-nlx.dat`, `SESSION-nlx.sync`

```yaml
- name: ndm_ncs2dat
  parameters:
  - {name: suffixes, value: nlx,   status: Mandatory}
  - {name: reverse,  value: false, status: Mandatory}
```

### `ndm_smr2dat` — CED/Spike2 .smr → .dat

**Input:** `../SESSION-smr/SESSION-smr.smr`
**Output:** `SESSION-smr.dat`

### `ndm_nev2evt` — Neuralynx .nev → .evt

Converts Neuralynx event files to the plain-text `.evt` format used by NeuroScope.

### `ndm_nvt2spots` — Neuralynx .nvt → .spots / .sts

Extracts LED positions and frame timestamps from Neuralynx Video Tracker files.

### `ndm_smr2evt`, `ndm_smi2sts`, `ndm_tsp2sts`

CED Spike2 events, Neuralynx MPEG subtitle timestamps, and Amplipex timestamps respectively.

---

## Channel and data manipulation

### `ndm_resample`

Resamples per-subsession `.dat` files from their original hardware rates to the common target
rate. The underlying `process_resample` binary is OpenMP-parallelised across channels.

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
`ndm_setupgroups` if the ADC channel numbering does not already match probe order.

### `ndm_concatenate`

Concatenates multi-session `.dat`, `.pos`, and `.evt` files. Creates a `.cat.evt` file with
subsession boundary timestamps.

---

## Group setup

### `ndm_setupgroups` — build channel groups from probe library

Reads the `probes:` section of the session YAML, loads each referenced `.probe` file from the
probe library, and writes `anatomicalDescription.channelGroups` and
`spikeDetection.channelGroups` into the session YAML in-place.

Must be run **before** `ndm_extractspikes`. For AlphaOmega sessions, `ndm_aom2dat` writes
the groups directly — `ndm_setupgroups` is not needed.

`probeId` and `shankIndex` metadata are written into each anatomical group so that
`ndm_estimatedrift` can reconstruct probe geometry. The `probes[].anatomicalGroups` field
is updated with the assigned group IDs.

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

### `ndm_hipass` — high-pass filter (.dat → .fil)

Applies a median-subtraction high-pass filter. `windowHalfLength=16` at 30 kHz ≈ 600 Hz
cutoff. Uses CUDA when available, otherwise OpenMP.

**Input:** `SESSION.dat`
**Output:** `SESSION.fil`

```yaml
- name: ndm_hipass
  parameters:
  - {name: windowHalfLength, value: 16,        status: Mandatory}
  - {name: chunkSize,        value: 134217728,  status: Optional}
```

### `ndm_lfp` — create LFP file (.dat → .lfp)

Downsamples the wideband `.dat` to the LFP sampling rate (typically 1250 Hz). Automatically
uses `SESSION-spkclean.dat` (from `ndm_stripdat`) when it is present.

### `ndm_extractspikes` — detect and extract spikes (.fil → .res + .spk)

Detects threshold crossings and extracts waveform snippets.

```yaml
- name: ndm_extractspikes
  parameters:
  - {name: thresholdFactor,  value: 1.5, status: Mandatory}
  - {name: refractoryPeriod, value: 25,  status: Mandatory}
  - {name: peakSearchLength, value: 50,  status: Mandatory}
```

### `ndm_extractspikes_sdiff` — spatial-derivative spike extraction

Drop-in replacement for `ndm_extractspikes`. Applies a discrete Laplacian across probe
channels before thresholding to suppress common-mode noise, while writing original
(un-differentiated) waveforms to `.spk` files. Recommended for high-density probes.

### `ndm_denoiseuniform` — remove uniform-noise events (.spk.N, .res.N → updated in-place)

Removes electrically uniform events (common-mode artefacts, motion noise, electrical
interference) from `.spk.N`/`.res.N` after spike extraction. Run before `ndm_pca`.
Backs up originals to `SESSION_denoise_backup/`.

```yaml
- name: ndm_denoiseuniform
  parameters:
  - {name: uniformityThreshold, value: 0.30, status: Optional}
  - {name: removeFlat,          value: 1,    status: Optional}
  - {name: dryRun,              value: 0,    status: Optional}
```

### `ndm_pca` — PCA feature extraction (.spk → .fet)

Computes principal component features from spike waveforms. `process_pca` is
OpenMP-parallelised across electrode groups; all groups run in parallel.

```bash
ndm_pca session.yaml       # all electrode groups (parallel)
ndm_pca session.yaml 3     # single electrode group
```

---

## Electrode group discovery

### `ndm_spikegrouper` — automatic spike group refinement (.fil → updated YAML)

Discovers optimal spike detection channel groups from the high-pass filtered data. Must be run
after `ndm_hipass` and before `ndm_extractspikes`. The `anatomicalDescription` section is
left untouched.

```yaml
- name: ndm_spikegrouper
  parameters:
  - {name: maxMergedSize, value: 12, status: Mandatory}
  - {name: minChannels,   value: 4,  status: Mandatory}
```

---

## Automatic spike sorting

### `ndm_klustakwik` — run KlustaKwik for each spike group (.fet.N → .clu.N)

Runs KlustaKwik sequentially for each `spikeDetection` group. KlustaKwik is internally
multi-threaded; groups run sequentially to avoid CPU contention.

**Three-tier parameter resolution** — every KlustaKwik parameter is resolved in priority order:

1. `spikeDetection.channelGroups[g].klustakwik.<param>` — per-group calibrated values
   (highest priority). `ndm_aom2dat` writes probe-type-calibrated defaults here:
   `MergeThresh = χ²(nCh×3, 0.9999)`, `MaxClusters` by probe type, merge iterations
   scaled by √(`nSpatialDims`/24).

2. `programs[ndm_klustakwik].parameters.<param>` — global session override (this block).
   Edit to change a parameter for all groups at once.

3. Built-in bash defaults in the `ndm_klustakwik` script.

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
  - {name: minClusters,          value: 2,        status: Optional}
  - {name: useFeatures,          value: all,      status: Optional}
  - {name: nStarts,              value: 3,        status: Optional}
  - {name: initMethod,           value: farthest, status: Optional}
  - {name: chunkMinutes,         value: 10.0,     status: Optional}
  - {name: chunkOverlapMinutes,  value: 2.0,      status: Optional}
  - {name: chunkPreseedFraction, value: 0.05,     status: Optional}
  - {name: maxIter,              value: 500,      status: Optional}
```

**Chunked CEM phases:**
- **Phase 0 (preseed):** `ChunkPreseedFraction` of all spikes, globally, to seed chunk starts
- **Phase 1:** per-chunk independent CEM
- **Phase 2:** cross-chunk merge using overlap spike vote matching (`ChunkOverlapMinutes`)
- **Phase 3:** global warm-start EM refinement (`GlobalMergeIter`)

```bash
ndm_klustakwik session.yaml    # all groups sequentially
ndm_klustakwik session.yaml 3  # single group
```

---

## Post-sorting pipeline

### `ndm_stripdat` — subtract spikes to produce a cleaned .dat

Subtracts spike waveforms from the raw `.dat` to produce `SESSION-spkclean.dat`. When `.clu.N`
files are present, a per-cluster template model is used (projection-based amplitude scaling,
ISI-driven burst shape compensation for bursting neurons). When no `.clu.N` exists for a
group, raw `.spk.N` waveforms are subtracted directly.

The original `.dat` is **never modified**. `SESSION-spkclean.dat` is always overwritten on
re-run so it can be regenerated after each curation pass. `ndm_lfp` automatically uses
`SESSION-spkclean.dat` when it exists.

### `ndm_redetectspikes` — second-round spike detection on cleaned .dat

After `ndm_stripdat`, runs the full spike detection pipeline (`ndm_hipass` →
`ndm_extractspikes`) on `SESSION-spkclean.dat` to recover spikes that were missed in the
first pass. Merges newly detected spikes into the existing `.res`/`.spk`/`.clu` files via
`process_mergespikes`, then deletes `.fet.N` (spike order changed). Re-run `ndm_pca` and
`ndm_klustakwik` to rebuild features and re-sort the merged set.

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

---

## Post-sorting analysis

### `ndm_decomposecollisions` — collision decomposition (.col.N)

Identifies and decomposes spike waveforms representing two near-simultaneous spikes. Never
modifies the original `.clu.N`/`.res.N`/`.spk.N` files. Requires curated `.clu.N` files.

**Algorithm:** (1) Build mean templates from curated clusters. (2) Flag candidates with
normalised cross-correlation below `corrThreshold`. (3) Fit all same-shank pairwise
template combinations with shifts up to `maxShiftSamp`. (4) Accept when residual RMS
fraction < `residualThreshold`.

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

### `ndm_estimatedrift` — spatial probe drift estimation (.drift)

Estimates in-vivo probe drift from curated spike-sorting results. Requires `.res.N`, `.clu.N`,
and optionally `.spk.N`. Reads `probeId`/`shankIndex` from `anatomicalDescription.channelGroups`
(written by `ndm_setupgroups` or `ndm_aom2dat`) to reconstruct site geometry.

**Primary method:** per-unit amplitude-vs-depth fingerprint cross-correlation with
sub-site parabolic interpolation. Weighted-median drift estimate per shank.
**Fallback:** population-level spatial fingerprint cross-correlation when unit yield is low.

```yaml
- name: ndm_estimatedrift
  parameters:
  - {name: windowSec,    value: 60,   status: Optional}
  - {name: minUnits,     value: 3,    status: Optional}
  - {name: minSpikes,    value: 20,   status: Optional}
  - {name: excludeNoise, value: true, status: Optional}
```

---

## Video processing

### `ndm_extractleds`

Detects bright pixels per video frame by thresholding the luma channel. Requires FFmpeg dev
headers at build time.

### `ndm_transcodevideo`

Converts video to MPEG-1 or H.264 using FFmpeg.

---

## Format conversion (YAML)

### `ndm_xml2yaml` — convert XML parameter file to YAML

```bash
ndm_xml2yaml session.xml             # writes session.yaml in the same directory
ndm_xml2yaml session.xml output.yaml # explicit output path
```

After conversion, populate the `probes:` section and run `ndm_setupgroups` to rebuild the
channel groups in the YAML format.

---

## Cleanup

### `ndm_clean`

Removes intermediate files after a successful pipeline run.

---

## File formats

### `.dat` / `.lfp` / `.fil` — binary signal files

Raw int16, little-endian, channel-interleaved, no header. File size = `nSamples × nChannels × 2` bytes.

### `.res.N` — spike timestamps

Binary. No header. `nSpikes × int64_t`, little-endian. One sample index per spike.
File size = `nSpikes × 8` bytes.

### `.spk.N` — spike waveforms

Binary. No header. `nSpikes × nSamples × nChannels` int16 samples, sample-major layout
(all channels for sample 0 first, then all for sample 1, etc.).
File size = `nSpikes × nSamples × nChannels × 2` bytes.

### `.fet.N` — PCA feature vectors

Binary. `int32_t` nDimensions header (= `nChannels × nPCs + 2`, including energy and timestamp).
Then `nSpikes × nDimensions × int64_t`, row-major. Last column is the spike timestamp
(sample index). Written by `process_mergefeatures`; read by KlustaKwik.

### `.clu.N` — cluster assignments

Plain text. First line: number of clusters. Subsequent lines: one cluster ID per spike
(0 = noise/artefact, 1 = unsorted MUA, ≥2 = single unit). Same spike order as `.res.N`.

### `.col.N` — collision decomposition results (YAML)

Written by `ndm_decomposecollisions`. One entry per candidate collision spike. Original
`.clu.N`/`.res.N`/`.spk.N` files are not modified.

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

Written by `ndm_estimatedrift`. Per-probe, per-shank drift in µm per time window.

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

### `.probe` — probe configuration (YAML)

Read by `ndm_setupgroups` and `ndm_estimatedrift`. Stored in the probe library at
`${CMAKE_INSTALL_DATADIR}/neurosuite/probes/`. Entries in the session YAML `probes:` section
reference files by path relative to the library root (e.g. `neuronexus/Buzsaki64.probe`).

---

## Performance notes

`process_aomconvert` streams HDF5 data in chunks via the HDF5 C hyperslab API. Default chunk
size (10M samples × 32 channels × 2 bytes ≈ 610 MB) is tuned for systems with 64+ GB RAM.
Reduce `chunkSamples` on memory-constrained machines.

`process_medianfilter` (used by `ndm_hipass`) processes data in configurable chunks. CUDA
targets: sm_86 (Ampere), sm_89 (Ada Lovelace), sm_100 (Hopper), sm_120 (Blackwell).
Requires CUDA ≥ 12.8 for sm_120 (RTX 5000 series).

`process_resample` and `process_pca` use OpenMP; `ndm_pca` runs all electrode groups in
parallel with a configurable per-group thread budget (`threadsPerGroup`).

`process_setupgroups.py`, `process_estimatedrift.py`, and `process_decomposecollisions.py`
require Python 3 with `pyyaml` and `numpy`. `scipy` is optional in `process_estimatedrift.py`
(used for sub-pixel parabolic interpolation when available).

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Windows | CPU (native) or full pipeline via WSL2 | [install/windows.md](install/windows.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |
