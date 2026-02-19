# ndmanager-plugins — Preprocessing Pipeline

ndmanager-plugins is a collection of command-line tools that convert raw acquisition data into the formats required for spike sorting and analysis. It consists of C++ processing binaries (some with optional CUDA acceleration) and Bash pipeline scripts that wrap them.

**All scripts take a single argument:** the path to the session `.xml` parameter file (with or without the `.xml` extension).

Exit codes used by every script:

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | General error |
| `10` | Output files already exist — step skipped (not an error in batch mode) |
| `11` | Input files missing — step skipped |

---

## Pipeline overview

The full pipeline is orchestrated by `ndm_start`. The typical execution order for a Neuralynx session is:

```
ndm_ncs2dat       → SESSION-nlx.dat, SESSION-nlx.sync
ndm_nev2evt       → SESSION-nlx.nlx.evt
ndm_nvt2spots     → SESSION.spots, SESSION.sts
ndm_resample      → resampled .dat files (if multiple hardware rates)
ndm_mergedat      → SESSION.dat (combined from all subsystems)
ndm_extractchannels / ndm_reorderchannels   → channel subset / reordering
ndm_transcodevideo → SESSION.avi / SESSION.m1v
ndm_extractleds   → SESSION.spots (from video, if not using .nvt)
ndm_concatenate   → SESSION.dat, SESSION.cat.evt (multi-session merge)
ndm_hipass        → SESSION.fil
ndm_lfp           → SESSION.lfp
ndm_extractspikes → SESSION.res.N, SESSION.spk.N
ndm_pca           → SESSION.fet.N
ndm_clean         → removes intermediate files
```

Each step is skipped automatically (exit 10) if its output already exists, making the pipeline safely re-entrant after interruption.

---

## `ndm_start` — run the full pipeline

Orchestrates the entire preprocessing workflow across one or more session directories. Reads the template `.xml` to determine which steps to run, then calls each `ndm_*` script in order.

```bash
# Process all subdirectories of the current directory
ndm_start template.xml

# Process specific directories only
ndm_start template.xml rat07-2012-03-16 rat07-2012-03-17
```

**XML parameters:**

```xml
<program>
  <n>ndm_start</n>
  <parameters>
    <parameter><n>suffixes</n><value>nlx</value></parameter>     <!-- required -->
    <parameter><n>wideband</n><value>true</value></parameter>
    <parameter><n>video</n><value>true</value></parameter>
    <parameter><n>events</n><value>true</value></parameter>
    <parameter><n>spikes</n><value>true</value></parameter>
    <parameter><n>lfp</n><value>true</value></parameter>
    <parameter><n>clean</n><value>false</value></parameter>
    <parameter><n>log</n><value>false</value></parameter>
  </parameters>
</program>
```

---

## Format conversion scripts

### `ndm_ncs2dat` — Neuralynx .ncs → .dat

Converts Neuralynx Continuously Sampled (`.ncs`) files to the interleaved int16 `.dat` format. Creates a `.sync` file for downstream event synchronisation. Handles acquisition restart events from `.nev` or `.restart` files.

**Input:** `../SESSION-nlx/SESSION-nlx.ncs` (one file per channel)
**Output:** `SESSION-nlx.dat`, `SESSION-nlx.sync`

```xml
<program>
  <n>ndm_ncs2dat</n>
  <parameters>
    <parameter><n>suffixes</n><value>nlx</value></parameter>
    <parameter><n>reverse</n><value>false</value></parameter>
    <!-- optional: <parameter><n>gap</n><value>1</value></parameter> -->
    <!-- optional: <parameter><n>ignoreEvents</n><value>false</value></parameter> -->
  </parameters>
</program>
```

---

### `ndm_smr2dat` — CED/Spike2 .smr → .dat

Converts Cambridge Electronic Design Spike2 (`.smr`) files to `.dat` format.

**Input:** `../SESSION-smr/SESSION-smr.smr`
**Output:** `SESSION-smr.dat`

```xml
<program>
  <n>ndm_smr2dat</n>
  <parameters>
    <parameter><n>suffixes</n><value>smr</value></parameter>
    <parameter><n>reverse</n><value>false</value></parameter>
    <!-- optional: <parameter><n>excluded</n><value>-</value></parameter> -->
  </parameters>
</program>
```

---

### `ndm_nev2evt` — Neuralynx .nev → .evt

Converts Neuralynx event files to the plain-text `.evt` format used by NeuroScope.

**Input:** `../SESSION-nlx/SESSION-nlx.nev`
**Output:** `SESSION-nlx.nlx.evt`

```xml
<program>
  <n>ndm_nev2evt</n>
  <parameters>
    <parameter><n>suffixes</n><value>nlx</value></parameter>
    <!-- optional: <parameter><n>gap</n><value>1</value></parameter> -->
  </parameters>
</program>
```

---

### `ndm_smr2evt` — CED/Spike2 .smr → .evt

Extracts event data from `.smr` files into `.evt` format. Optionally calls a rename script to relabel events.

**Input:** `../SESSION-smr/SESSION-smr.smr`
**Output:** `SESSION-smr.smr.evt`

```xml
<program>
  <n>ndm_smr2evt</n>
  <parameters>
    <parameter><n>suffixes</n><value>smr</value></parameter>
    <!-- optional: <parameter><n>rename</n><value>my_rename_script.sh</value></parameter> -->
  </parameters>
</program>
```

---

### `ndm_nvt2spots` — Neuralynx .nvt → .spots / .sts

Extracts LED positions and frame timestamps from Neuralynx Video Tracker (`.nvt`) files.

**Input:** `../SESSION-nlx/SESSION-nlx.nvt`
**Output:** `SESSION.spots`, `SESSION.sts`

```xml
<program>
  <n>ndm_nvt2spots</n>
  <parameters>
    <parameter><n>suffix</n><value>nlx</value></parameter>
    <!-- optional: <parameter><n>gap</n><value>1</value></parameter> -->
    <!-- optional: <parameter><n>ignoreEvents</n><value>false</value></parameter> -->
  </parameters>
</program>
```

---

### `ndm_smi2sts` — Neuralynx .smi → .sts

Extracts video frame timestamps from Neuralynx MPEG subtitle index (`.smi`) files. Note: SMI timestamps have poor accuracy (up to hundreds of milliseconds of jitter) because Neuralynx MPEG recording is OS-controlled and not hardware-synchronised. Prefer `ndm_nvt2spots` when a `.nvt` file is available.

**Input:** `../SESSION-nlx/SESSION-nlx.smi`
**Output:** `SESSION.sts`

```xml
<program>
  <n>ndm_smi2sts</n>
  <parameters>
    <parameter><n>suffix</n><value>nlx</value></parameter>
    <!-- optional: <parameter><n>gap</n><value>1</value></parameter> -->
  </parameters>
</program>
```

---

### `ndm_tsp2sts` — Amplipex .tsp/.meta → .sts

Extracts video frame timestamps from Amplipex `.tsp` and `.meta` files.

**Input:** `../SESSION-amp/SESSION-amp.tsp`, `../SESSION-amp/SESSION-amp.meta`
**Output:** `SESSION.sts`

```xml
<program>
  <n>ndm_tsp2sts</n>
  <parameters>
    <parameter><n>suffix</n><value>amp</value></parameter>
  </parameters>
</program>
```

---

## Channel and data manipulation scripts

### `ndm_resample` — resample individual .dat files

Resamples per-subsession `.dat` files from their original hardware sampling rates to the common target rate defined in `//acquisitionSystem/samplingRate`. Required when different acquisition devices ran at different rates and must be merged. The underlying `process_resample` binary is OpenMP-parallelised across channels in 256 K-frame chunks.

**Input:** `SESSION-nlx.dat`, `SESSION-smr.dat`
**Output:** Resampled `.dat` files (overwritten in place)

```xml
<program>
  <n>ndm_resample</n>
  <parameters>
    <parameter><n>suffixes</n><value>nlx smr</value></parameter>
    <parameter><n>samplingRates</n><value>32000 30000</value></parameter>
    <parameter><n>nChannels</n><value>64 32</value></parameter>
  </parameters>
</program>
```

---

### `ndm_mergedat` — merge multiple .dat files into one

Interleaves channels from multiple `.dat` files into a single multiplexed `.dat`. Used when data from different subsystems must be combined.

**Input:** `SESSION-nlx.dat` (64 ch), `SESSION-smr.dat` (32 ch)
**Output:** `SESSION.dat` (96 ch)

```xml
<program>
  <n>ndm_mergedat</n>
  <parameters>
    <parameter><n>suffixes</n><value>nlx smr</value></parameter>
    <parameter><n>nChannels</n><value>64 32</value></parameter>
  </parameters>
</program>
```

---

### `ndm_extractchannels` — extract a channel subset

Removes unwanted channels from a `.dat` file, writing a new file that contains only the listed channels in the listed order.

**Input / Output:** `SESSION.dat` (overwritten)

```xml
<program>
  <n>ndm_extractchannels</n>
  <parameters>
    <parameter><n>nChannels</n><value>128</value></parameter>
    <parameter><n>channels</n><value>0 1 2 3 8 9 10 11</value></parameter>
  </parameters>
</program>
```

---

### `ndm_reorderchannels` — reorder channels in .dat

Permutes the channel order in a `.dat` file to match the physical probe layout. The total channel count is unchanged.

**Input / Output:** `SESSION.dat` (overwritten)

```xml
<program>
  <n>ndm_reorderchannels</n>
  <parameters>
    <parameter><n>channels</n><value>0 2 4 6 1 3 5 7</value></parameter>
  </parameters>
</program>
```

---

### `ndm_concatenate` — concatenate multi-session recordings

Concatenates multiple per-subsession `.dat`, `.pos`, and `.evt` files into a single session-level file. Creates a `.cat.evt` file with timestamps for the start and end of each constituent subsession.

**Input:** All `*.dat` files in the session directory (except the concatenated output)
**Output:** `SESSION.dat`, `SESSION.cat.evt`, `SESSION.all.evt`

```xml
<program>
  <n>ndm_concatenate</n>
  <parameters>
    <parameter><n>spotsSamplingRate</n><value>50</value></parameter>
    <!-- optional: <parameter><n>maxShift</n><value>3</value></parameter> -->
  </parameters>
</program>
```

---

### `ndm_recolorchannels` — update channel colours in .xml

Edits channel colour hex codes in the `.xml` parameter file. Not called by `ndm_start` — standalone utility only.

```bash
# Colour channels 0–3 red, channels 4–7 green, channel 17 blue
ndm_recolorchannels -c 0-3,ff0000 -c 4-7,00ff00 -c 17,0000ff session.xml
```

---

## Signal processing scripts

### `ndm_hipass` — high-pass filter (.dat → .fil)

Applies a median-subtraction high-pass filter to produce a `.fil` file suitable for spike detection. The filter is non-linear (running median), which minimises spike waveform distortion compared to IIR/FIR filters. Window half-length controls the effective cutoff: `windowHalfLength=16` at 20 kHz ≈ 625 Hz.

The underlying `process_medianfilter` binary uses CUDA when available (targets sm_86 / sm_89 / sm_100 / sm_120), otherwise falls back to OpenMP.

**Input:** `SESSION.dat`
**Output:** `SESSION.fil`

```xml
<program>
  <n>ndm_hipass</n>
  <parameters>
    <parameter><n>windowHalfLength</n><value>16</value></parameter>
    <!-- optional: reduce if RAM is constrained (default = 64 GiB) -->
    <parameter><n>chunkSize</n><value>68719476736</value></parameter>
  </parameters>
</program>
```

---

### `ndm_lfp` — create LFP file (.dat → .lfp)

Downsamples the wideband `.dat` to produce a `.lfp` file at the LFP sampling rate (typically 1250 Hz). Retains all channels.

**Input:** `SESSION.dat`
**Output:** `SESSION.lfp`

```xml
<program>
  <n>ndm_lfp</n>
  <parameters>
    <parameter><n>samplingRate</n><value>1250</value></parameter>
  </parameters>
</program>
```

---

### `ndm_extractspikes` — detect and extract spikes (.fil → .res + .spk)

Detects threshold crossings in the high-pass filtered `.fil` file and extracts waveform snippets around each event. Produces one `.res.N` and one `.spk.N` per electrode group.

Detection threshold is estimated from the median absolute deviation of a configurable noise window: `threshold = thresholdFactor × noise_RMS`.

**Input:** `SESSION.xml`, `SESSION.fil`
**Output:** `SESSION.res.N`, `SESSION.spk.N` (one pair per electrode group)

```xml
<program>
  <n>ndm_extractspikes</n>
  <parameters>
    <parameter><n>thresholdFactor</n><value>1.5</value></parameter>
    <parameter><n>refractoryPeriod</n><value>25</value></parameter>     <!-- samples -->
    <parameter><n>peakSearchLength</n><value>50</value></parameter>     <!-- samples -->
    <parameter><n>start</n><value>0</value></parameter>                 <!-- noise window start (s) -->
    <parameter><n>duration</n><value>60</value></parameter>             <!-- noise window duration (s) -->
  </parameters>
</program>
```

---

### `ndm_pca` — PCA feature extraction (.spk → .fet)

Computes principal component features from spike waveforms. Outputs `.fet.N` files ready for KlustaKwik. The number of PCA components per channel is set by `<nFeatures>` in each electrode group in the `.xml`. `process_pca` is OpenMP-parallelised: electrode groups are processed concurrently.

```bash
# All electrode groups
ndm_pca session.xml

# Single electrode group
ndm_pca session.xml 3
```

**Input:** `SESSION.xml`, `SESSION.spk.N`
**Output:** `SESSION.fet.N`

```xml
<program>
  <n>ndm_pca</n>
  <parameters>
    <parameter><n>before</n><value>12</value></parameter>    <!-- samples before peak -->
    <parameter><n>after</n><value>12</value></parameter>     <!-- samples after peak -->
    <parameter><n>extra</n><value>false</value></parameter>  <!-- include peak amplitude as extra feature -->
  </parameters>
</program>
```

`before` and `after` can be a single value applied to all groups, or a space-separated list with one value per group.

---

## Video processing scripts

### `ndm_extractleds` — extract LED positions from video (.avi → .spots)

Detects bright pixels in each video frame by thresholding the luma channel. Outputs a `.spots` file listing all detected spots per frame. Alternatively, can call `ndm_nvt2spots` internally to extract from a Neuralynx `.nvt` file. Requires `process_extractleds` (built only when FFmpeg dev headers are present at cmake time).

**Input:** `../SESSION-vid/SESSION-vid.avi`
**Output:** `SESSION.spots`

```xml
<program>
  <n>ndm_extractleds</n>
  <parameters>
    <parameter><n>suffix</n><value>vid</value></parameter>
    <parameter><n>useTrackerData</n><value>false</value></parameter>
    <parameter><n>threshold</n><value>90</value></parameter>    <!-- luma threshold 0–255 -->
    <parameter><n>extension</n><value>avi</value></parameter>
    <parameter><n>show</n><value>false</value></parameter>
  </parameters>
</program>
```

---

### `ndm_transcodevideo` — transcode video

Converts a video file to MPEG-1 or H.264 using FFmpeg.

**Input:** `../SESSION-vid/SESSION-vid.avi`
**Output:** `SESSION.avi` (H.264) or `SESSION.m1v` (MPEG-1)

```xml
<program>
  <n>ndm_transcodevideo</n>
  <parameters>
    <parameter><n>suffix</n><value>vid</value></parameter>
    <parameter><n>extension</n><value>avi</value></parameter>
    <parameter><n>codec</n><value>x264</value></parameter>    <!-- x264 or m1v -->
    <!-- optional: <parameter><n>frequency</n><value>30</value></parameter> -->
  </parameters>
</program>
```

---

## Cleanup

### `ndm_clean` — remove intermediate files

Deletes intermediate files after a successful pipeline run to recover disk space.

```xml
<program>
  <n>ndm_clean</n>
  <parameters>
    <parameter><n>wideband</n><value>true</value></parameter>   <!-- delete per-session .dat -->
    <parameter><n>xml</n><value>true</value></parameter>
    <parameter><n>hipass</n><value>true</value></parameter>     <!-- delete .fil -->
    <parameter><n>spots</n><value>false</value></parameter>
    <parameter><n>pos</n><value>false</value></parameter>
  </parameters>
</program>
```

---

## File formats

### `.dat` / `.lfp` / `.fil` — binary signal files

All three share the same format: **raw int16, little-endian, channel-interleaved, no header**.

```
[ch0_s0][ch1_s0]…[chN_s0][ch0_s1][ch1_s1]…[chN_s1]…

nChannels   → from .xml //acquisitionSystem/nChannels
samplingRate → from .xml //acquisitionSystem/samplingRate (.dat, .fil)
              from .xml //fieldPotentials/lfpSamplingRate (.lfp)
```

File size in bytes = `nSamples × nChannels × 2`.

### `.res.N` — spike timestamps

Plain text, one integer per line. Values are sample indices into the corresponding `.dat`. The first line is the total spike count.

### `.spk.N` — spike waveforms

Raw int16, no header. For each spike: `nSamples × nChannels` samples, channels interleaved. `nSamples` and `nChannels` for the group come from the `.xml` `<spikeDetection>` block.

### `.fet.N` — PCA feature vectors

Plain text. First line: number of features per spike. Subsequent lines: space-separated feature integers followed by the spike timestamp (sample index) as the last column.

```
7
-423  891  -104  …  4832
 210 -330   512  …  4901
```

### `.evt` — event files

Plain text. Each line: timestamp in milliseconds (float), a space, and a label string.

```
0.000 beginning of rat07-2012-03-16-s1
45832.416 end of rat07-2012-03-16-s1
```

### `.spots` — LED detections

Plain text, space-separated: `frame  nPixels  x  y  sx  sy  Y  Cr  Cb`

### `.sts` — video frame timestamps

Plain text, one timestamp in milliseconds per line.

### `.pos` — linearised position

Plain text, one `x y` integer pair per line, one line per video frame.

---

## CUDA and performance notes

### `process_medianfilter` / `ndm_hipass`

Processes data in configurable chunks (`chunkSize` bytes). With CUDA, each chunk is transferred to GPU, filtered, and transferred back. Reduce `chunkSize` from the 64 GiB default if host RAM is constrained (e.g. set `8000000000` for 8 GB). CUDA targets: sm_86 (Ampere), sm_89 (Ada Lovelace), sm_100 (Hopper), sm_120 (Blackwell). Requires CUDA ≥ 12.8 for sm_120.

### `process_resample` / `ndm_lfp` / `ndm_resample`

OpenMP-parallelised: each channel resampled independently in 256 K-frame chunks, all channels processed concurrently up to the available thread count.

### `process_pca` / `ndm_pca`

Two-level parallelism: electrode groups are dispatched as concurrent background processes (with a thread budget), and within each group `process_pca` uses OpenMP for matrix operations.

---

## Build

```bash
cmake -B build/ndmanager-plugins ndmanager-plugins -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager-plugins -j$(nproc)
sudo cmake --install build/ndmanager-plugins
```

CUDA is detected automatically. To force CPU-only:

```bash
cmake -B build/ndmanager-plugins ndmanager-plugins -DUSE_CUDA=OFF -DCMAKE_BUILD_TYPE=Release
```

FFmpeg is optional. `process_extractleds` is built only when `libavcodec`, `libavformat`, `libavutil`, and `libswscale` dev headers are present.
