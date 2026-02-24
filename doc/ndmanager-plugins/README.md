# ndmanager-plugins — Preprocessing Pipeline

ndmanager-plugins is a collection of command-line tools that convert raw acquisition data into the formats required for spike sorting. It consists of C++ processing binaries (some with optional CUDA acceleration) and Bash pipeline scripts that call them.

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
| FFmpeg (libavcodec, libavformat, libavutil, libswscale) | `process_extractleds`, `ndm_transcodevideo` | Optional |
| CUDA Toolkit ≥ 12.8 | GPU acceleration for `process_medianfilter`, `process_medianthreshold` | Optional |

The CUDA filter (`process_medianfilter`) falls back to an OpenMP CPU path automatically when CUDA is absent.

---

## Pipeline overview

The full pipeline is orchestrated by `ndm_start`. Typical execution order for a Neuralynx session:

```
ndm_ncs2dat         → SESSION-nlx.dat, SESSION-nlx.sync
ndm_nev2evt         → SESSION-nlx.nlx.evt
ndm_nvt2spots       → SESSION.spots, SESSION.sts
ndm_resample        → resampled .dat (if multiple hardware rates)
ndm_mergedat        → SESSION.dat
ndm_concatenate     → SESSION.dat, SESSION.cat.evt
ndm_hipass          → SESSION.fil
ndm_lfp             → SESSION.lfp
ndm_extractspikes   → SESSION.res.N, SESSION.spk.N
ndm_pca             → SESSION.fet.N
ndm_clean           → removes intermediate files
```

Each step is skipped (exit `10`) if its output already exists, making the pipeline safely re-entrant after interruption.

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

**Input:** `../SESSION-nlx/SESSION-nlx.nev`
**Output:** `SESSION-nlx.nlx.evt`

### `ndm_nvt2spots` — Neuralynx .nvt → .spots / .sts

Extracts LED positions and frame timestamps from Neuralynx Video Tracker files.

### `ndm_smr2evt`, `ndm_smi2sts`, `ndm_tsp2sts`

CED Spike2 events, Neuralynx MPEG subtitle timestamps, and Amplipex timestamps respectively. See XML parameter references in the legacy documentation.

---

## Channel and data manipulation

### `ndm_resample`

Resamples per-subsession `.dat` files from their original hardware rates to the common target rate. Required when different acquisition devices ran at different rates. The underlying `process_resample` binary is OpenMP-parallelised across channels.

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

Permutes channel order to match the physical probe layout.

### `ndm_concatenate`

Concatenates multi-session `.dat`, `.pos`, and `.evt` files. Creates a `.cat.evt` file with subsession boundary timestamps.

---

## Signal processing

### `ndm_hipass` — high-pass filter (.dat → .fil)

Applies a median-subtraction high-pass filter. Window half-length controls the effective cutoff: `windowHalfLength=16` at 20 kHz ≈ 625 Hz. Uses CUDA when available, otherwise falls back to OpenMP.

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

`chunkSize` controls how much data is processed in one GPU/CPU pass. Reduce from the 64 GiB default if host RAM is constrained.

### `ndm_lfp` — create LFP file (.dat → .lfp)

Downsamples the wideband `.dat` to the LFP sampling rate (typically 1250 Hz). All channels retained.

### `ndm_extractspikes` — detect and extract spikes (.fil → .res + .spk)

Detects threshold crossings and extracts waveform snippets. Threshold is `thresholdFactor × noise_RMS` from a configurable noise window.

```yaml
- name: ndm_extractspikes
  parameters:
    - name: thresholdFactor
      value: 1.5
    - name: refractoryPeriod
      value: 25
    - name: peakSearchLength
      value: 50
    - name: start
      value: 0
    - name: duration
      value: 60
```

### `ndm_pca` — PCA feature extraction (.spk → .fet)

Computes principal component features from spike waveforms. Number of components per channel set by `<nFeatures>` in each electrode group. `process_pca` is OpenMP-parallelised: electrode groups processed concurrently.

```bash
ndm_pca session.xml       # all electrode groups
ndm_pca session.xml 3     # single electrode group
```

---

## Video processing

### `ndm_extractleds`

Detects bright pixels per video frame by thresholding the luma channel. Requires FFmpeg dev headers at build time.

### `ndm_transcodevideo`

Converts video to MPEG-1 or H.264 using FFmpeg.

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

Plain text. First line: feature count per spike. Subsequent lines: space-separated feature integers, last column is the spike timestamp (sample index).

### `.evt` — event files

Plain text. Each line: timestamp in milliseconds, a space, and a label string.

### `.spots` — LED detections

Plain text, space-separated: `frame  nPixels  x  y  sx  sy  Y  Cr  Cb`

### `.pos` — linearised position

Plain text. One `x y` integer pair per line, one per video frame.

---

## Performance notes

`process_medianfilter` (used by `ndm_hipass`) processes data in configurable chunks. CUDA targets: sm_86 (Ampere), sm_89 (Ada Lovelace), sm_100 (Hopper), sm_120 (Blackwell). Requires CUDA ≥ 12.8 for sm_120.

`process_resample` and `process_pca` use OpenMP; each channel or electrode group is dispatched independently.

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Windows | CPU (native) or full pipeline via WSL2 | [install/windows.md](install/windows.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |
