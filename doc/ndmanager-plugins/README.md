# ndmanager-plugins — Preprocessing Pipeline

ndmanager-plugins is a collection of command-line tools that convert raw acquisition data into the
formats required for spike sorting. It consists of C++ processing binaries (some with optional
CUDA acceleration) and Bash pipeline scripts that call them.

All scripts take a single argument: the path to the session `.xml` or `.yaml` parameter file.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| CMake, C++17 compiler | Build system | Yes |
| OpenMP | Parallel processing in `process_resample`, `process_pca` | Recommended |
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

The full pipeline is orchestrated by `ndm_start`. Typical execution order for a
silicon probe session:

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
ndm_pca             → SESSION.fet.N

[Cleanup]
ndm_clean           → removes intermediate files

[Post-sorting analysis — run after KlustaKwik + Klusters curation]
ndm_decomposecollisions → SESSION.col.N
ndm_estimatedrift       → SESSION.drift
```

Each step is skipped (exit `10`) if its output already exists, making the pipeline safely
re-entrant after interruption.

---

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | General error |
| `10` | Output already exists — step skipped (not an error in batch mode) |
| `11` | Input files missing — step skipped |

---

## Format conversion scripts

### `ndm_ncs2dat` — Neuralynx .ncs → .dat

Converts Neuralynx Continuously Sampled files to interleaved int16 `.dat` format.

**Input:** `../SESSION-nlx/SESSION-nlx.ncs` (one file per channel)
**Output:** `SESSION-nlx.dat`, `SESSION-nlx.sync`

```yaml
- name: ndm_ncs2dat
  parameters:
    - name: suffixes
      value: nlx
    - name: reverse
      value: false
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
    - name: suffixes
      value: "nlx smr"
    - name: samplingRates
      value: "32000 30000"
    - name: nChannels
      value: "64 32"
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

### `ndm_setupgroups` — build channel groups from probe library (.yaml → updated .yaml)

Reads the `probes:` section of the session YAML, loads each referenced `.probe` file from the
probe library, and writes `anatomicalDescription.channelGroups` and
`spikeDetection.channelGroups` into the session YAML in-place.

Must be run **before** `ndm_extractspikes`. Position: after `ndm_reorderchannels`, before
`ndm_spikegrouper`.

One anatomical group and one mirrored spikeDetection group are created per shank. The two
tables are always in sync: the spikeDetection groups use the same channel list in the same order,
annotated with `nSamples`, `peakSampleIndex`, and `nFeatures`.

`probeId` and `shankIndex` metadata are written into each anatomical group so that
`ndm_estimatedrift` can reconstruct probe geometry. The `probes[].anatomicalGroups` field is
updated with the assigned group IDs.

**Channel assignment** — when `channelMap.map` is null (the default for all supplied NeuroNexus
files), channels are assigned sequentially:

```
shank i → [offset + i × n_per_shank, …, offset + (i+1) × n_per_shank − 1]
```

where `offset` = `probes[].channelOffset` and `n_per_shank` = `probeFile.sites.count_per_shank`.

**Probe library search order** (lowest → highest priority):

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH  (colon-separated)
SESSION_DIR/probes/
```

The `probeLibrary` parameter overrides or appends to this search path.

**Example session YAML** (before running `ndm_setupgroups`):

```yaml
probes:
  - id: 0
    probeFile: neuronexus/Buzsaki64.probe
    label: "left CA1"
    channelOffset: 0
  - id: 1
    probeFile: neuronexus/Buzsaki64.probe
    label: "right CA1"
    channelOffset: 64
```

After running (Buzsaki64: 8 shanks × 8 channels each):

```yaml
probes:
  - id: 0
    probeFile: neuronexus/Buzsaki64.probe
    label: "left CA1"
    channelOffset: 0
    anatomicalGroups: [1, 2, 3, 4, 5, 6, 7, 8]
  - id: 1
    ...
    anatomicalGroups: [9, 10, 11, 12, 13, 14, 15, 16]

anatomicalDescription:
  channelGroups:
    - channels: [{id: 0, skip: 0}, …, {id: 7, skip: 0}]
      probeId: 0
      shankIndex: 0
    …

spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3, 4, 5, 6, 7]
      nSamples: 52
      peakSampleIndex: 26
      nFeatures: 3
    …
```

```yaml
- name: ndm_setupgroups
  parameters:
    - name: nSamples
      value: 52
    - name: peakSampleIndex
      value: 26
    - name: nFeatures
      value: 3
    - name: overwrite
      value: false
```

`ndm_setupgroups` operates only on YAML parameter files.

---

## Signal processing

### `ndm_hipass` — high-pass filter (.dat → .fil)

Applies a median-subtraction high-pass filter. Window half-length controls the effective
cutoff: `windowHalfLength=16` at 20 kHz ≈ 625 Hz. Uses CUDA when available, otherwise
falls back to OpenMP.

**Input:** `SESSION.dat`  
**Output:** `SESSION.fil`

```yaml
- name: ndm_hipass
  parameters:
    - name: windowHalfLength
      value: 16
      status: Mandatory
    - name: chunkSize
      value: 32000000
      status: Optional
```

`chunkSize` controls how much data is processed in one GPU/CPU pass.

### `ndm_lfp` — create LFP file (.dat → .lfp)

Downsamples the wideband `.dat` to the LFP sampling rate (typically 1250 Hz).

### `ndm_extractspikes` — detect and extract spikes (.fil → .res + .spk)

Detects threshold crossings and extracts waveform snippets.

```yaml
- name: ndm_extractspikes
  parameters:
    - name: thresholdFactor
      value: 1.5
    - name: refractoryPeriod
      value: 25
    - name: peakSearchLength
      value: 50
```

### `ndm_pca` — PCA feature extraction (.spk → .fet)

Computes principal component features from spike waveforms. `process_pca` is
OpenMP-parallelised across electrode groups.

```bash
ndm_pca session.yaml       # all electrode groups
ndm_pca session.yaml 3     # single electrode group
```

---

## Electrode group discovery

### `ndm_spikegrouper` — automatic spike group refinement (.fil → updated YAML)

Discovers optimal spike detection channel groups from the high-pass filtered data. Must be run
after `ndm_hipass` and before `ndm_extractspikes`. Operates on the existing
`spikeDetection.channelGroups` (set by `ndm_setupgroups`) and may split or merge groups.
The `anatomicalDescription` section is left untouched.

```yaml
- name: ndm_spikegrouper
  parameters:
    - name: maxMergedSize
      value: 12
    - name: minChannels
      value: 4
```

---

## Post-sorting analysis

These plugins run **after** KlustaKwik spike sorting and manual curation in Klusters.

### `ndm_decomposecollisions` — template-matching collision decomposition (.col.N)

Identifies and decomposes spike waveforms that represent two near-simultaneous spikes whose
waveforms have overlapped in the recording (collisions). Never modifies the original
`.clu.N`/`.res.N`/`.spk.N` files.

**Algorithm:**
1. Build mean waveform templates from curated clusters (min `minSpikesTemplate` spikes required).
2. Flag candidate collision spikes: normalized cross-correlation with all single-unit templates
   falls below `corrThreshold`.
3. Fit all same-shank pairwise template combinations with time offsets up to `maxShiftSamp`
   samples. Accept the best-fitting pair when residual RMS fraction < `residualThreshold`.
4. Write collision flags and decomposition components to `SESSION.col.N` YAML sidecars.

```yaml
- name: ndm_decomposecollisions
  parameters:
    - name: maxShiftSamp
      value: 10
    - name: corrThreshold
      value: 0.85
    - name: residualThreshold
      value: 0.25
    - name: minSnrRms
      value: 4.0
    - name: minSpikesTemplate
      value: 30
    - name: excludeNoise
      value: true
```

### `ndm_estimatedrift` — spatial probe drift estimation (.drift)

Estimates in-vivo probe drift from curated spike-sorting results. Requires `.res.N`, `.clu.N`,
and optionally `.spk.N` for each spike group.

**Primary method — per-unit spatial-profile cross-correlation:**
For each well-isolated unit (≥ `minSpikes` in both the reference window and the target window):

1. Build the unit's amplitude-vs-depth fingerprint (mean PTP per electrode site) in each window.
2. Cross-correlate the reference-window fingerprint against the target-window fingerprint along
   the depth axis. The lag at the correlation peak — interpolated to sub-site precision with a
   parabolic fit — is the depth shift of that unit's spatial footprint (µm).
3. Shank drift estimate = weighted median of per-unit shifts, with weights = √(n_ref × n_win).

**Fallback method — population amplitude-profile cross-correlation:**
Pooled multi-unit spatial fingerprint cross-correlated per window. Does not require isolated
units; used when fewer than `minUnits` units are tracked.

**Output:** `SESSION.drift` (YAML) containing per-probe, per-shank drift trajectories in µm.

```yaml
- name: ndm_estimatedrift
  parameters:
    - name: windowSec
      value: 60
    - name: minUnits
      value: 3
    - name: minSpikes
      value: 20
    - name: excludeNoise
      value: true
    - name: probeLibrary
      value: ""
```

`ndm_estimatedrift` reads `probeId` / `shankIndex` from `anatomicalDescription.channelGroups`
(written by `ndm_setupgroups`) to reconstruct site geometry without re-reading the probe library.
The `probeLibrary` parameter is used only as a fallback when that metadata is absent.

---

## Video processing

### `ndm_extractleds`

Detects bright pixels per video frame by thresholding the luma channel. Requires FFmpeg dev
headers at build time.

### `ndm_transcodevideo`

Converts video to MPEG-1 or H.264 using FFmpeg.

---

## Format conversion

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

```yaml
- name: ndm_clean
  parameters:
    - name: wideband
      value: true
    - name: hipass
      value: true
    - name: spots
      value: false
```

---

## File formats

### `.dat` / `.lfp` / `.fil` — binary signal files

Raw int16, little-endian, channel-interleaved, no header. File size = `nSamples × nChannels × 2` bytes.

### `.res.N` — spike timestamps

Plain text. First line: total spike count. Subsequent lines: one sample index per spike.

### `.spk.N` — spike waveforms

Raw int16, no header. Per spike: `nSamples × nChannels` samples, channels interleaved.

### `.fet.N` — PCA feature vectors

Plain text. First line: feature count per spike. Subsequent lines: space-separated feature
integers, last column is spike timestamp (sample index).

### `.col.N` — collision decomposition results (YAML)

Written by `ndm_decomposecollisions`. One entry per candidate collision spike:

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

Written by `ndm_estimatedrift`. Per-probe, per-shank drift in µm per time window:

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
            - {t_start: 0.0, t_end: 60.0, drift_um: 0.0}
            - {t_start: 60.0, t_end: 120.0, drift_um: -1.8}
```

### `.probe` — probe configuration (YAML)

Read by `ndm_setupgroups` and `ndm_estimatedrift`. Stored in the probe library at
`${CMAKE_INSTALL_DATADIR}/neurosuite/probes/`. Entries in the session YAML `probes:` section
reference files by path relative to the library root (e.g. `neuronexus/Buzsaki64.probe`).

---

## Performance notes

`process_medianfilter` (used by `ndm_hipass`) processes data in configurable chunks. CUDA
targets: sm_86 (Ampere), sm_89 (Ada Lovelace), sm_100 (Hopper), sm_120 (Blackwell).
Requires CUDA ≥ 12.8 for sm_120.

`process_resample` and `process_pca` use OpenMP; each channel or electrode group is dispatched
independently.

`process_setupgroups.py`, `process_estimatedrift.py`, and `process_decomposecollisions.py`
require Python 3 with `pyyaml` and `numpy`. `scipy` is optional in `process_estimatedrift.py`
(used for sub-pixel interpolation when available).

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Windows | CPU (native) or full pipeline via WSL2 | [install/windows.md](install/windows.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |
