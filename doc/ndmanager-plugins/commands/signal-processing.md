# Signal processing — filters, LFP and channel slices

**Contents:** [`ndm_hipass`](#ndm_hipass--high-pass-filter-dat--fil) ·
[`ndm_bandpass`](#ndm_bandpass--band-pass-filter-dat--fil) ·
[`ndm_lfp`](#ndm_lfp--create-lfp-file-dat--lfp) ·
[`ndm_slicebinary`](#ndm_slicebinary--channel-subset-ext--extslicetag)

Each command takes the session parameter file (or nothing, to use the current
directory's name) as its only argument, and reads `nChannels`, `nBits` and
`samplingRate` from `acquisitionSystem`.

**`inputExtension`** — all four commands read `SESSION.<inputExtension>`
(default `dat`, or `lfp` for `ndm_slicebinary`). A leading dot is ignored
(`.lfp` = `lfp`). When the parameter is set, that file must exist: a missing
one stops the step with an error (exit 1). Only when the parameter is unset and
the default input is missing does the step print "skipping this step" and exit
11. `emgclean` reads the EMG-cleaned recording: `ndm_extractemg` links
`SESSION.emgclean` to the `SESSION-emgclean.dat` it writes. See
[parameters](../parameters.md) for how `inputExtension` is resolved.

---

## `ndm_hipass` — high-pass filter (`.dat` → `.fil`)

### Synopsis

```bash
ndm_hipass [session.yaml]
```

**Input:** `SESSION.dat` (or `SESSION.<inputExtension>`), int16

**Output:** `SESSION.fil`

### Description

Removes the low-frequency content below the spike band. The filter is chosen
with `filter`:

- **`butterworth`** *(default)* — zero-phase Butterworth (`process_butterworth`).
  Second-order sections designed in double precision, run forward then
  backward: no phase distortion, magnitude squared (−6 dB at each corner,
  effective order 2 × `order`). Linear, so the `.fil` contains nothing the
  `.dat` does not. Arithmetic is double on the CPU and single on a GPU
  (`precision: auto`). In double the output equals scipy's `sosfiltfilt`
  rounded to int16; single changes ~0.04% of samples by 1 LSB on the reference
  data (1 of 59,675 threshold detections moved by 1 sample). Uses CUDA when
  built with it and a device is present, otherwise OpenMP. Setting `lowPassHz`
  makes it a band-pass.
- **`median`** — the median-subtraction high-pass (`process_medianfilter`).
  Non-linear: the output is `x[t] − median(window)`, which is exactly 0
  whenever the centre sample is the window median — at many sign changes and
  along steep monotonic stretches such as spike downstrokes (about 5% of
  samples, runs up to ~17). At `windowHalfLength: 16` and 32.5 kHz its
  effective −3 dB corner is ~800 Hz, with a +2.8 dB bump near 1.2–1.5 kHz on
  pure tones, and it adds broadband content above 10 kHz that the `.dat` does
  not have.

A progress bar (stage `FILTER` for `butterworth`, `MEDIAN` for `median`)
shows the run on the terminal, like the spike detectors', ending with ✓ (✗ on
a failure). It is drawn on the terminal itself, so it shows under logged
output; without a terminal it prints a line every 5 %. The `median` filter's
CUDA path has no bar.

Skips with exit 10 if `SESSION.fil` exists. Any other `filter` value is an
error.

### Parameters

| Name | Default | Filter | Meaning |
|---|---|---|---|
| `filter` | `butterworth` | — | `butterworth` or `median` |
| `highPassHz` | 800 | butterworth | High-pass corner, Hz (−6 dB after zero phase) |
| `lowPassHz` | 0 (none) | butterworth | Optional low-pass corner, Hz; makes the filter a band-pass |
| `order` | 3 | butterworth | Design order of each stage (×2 after zero phase) |
| `precision` | `auto` | butterworth | `auto` (double CPU / single GPU), `double` or `single` |
| `windowHalfLength` | — | median | Median window half-length in samples; **required** with `median` |
| `chunkSize` | 134217728 (128 MiB) | median | Bytes per pass; capped at 512 MiB |
| `inputExtension` | `dat` | both | File to filter, e.g. an externally re-referenced `car.dat`; must still be wide-band |

```yaml
- name: ndm_hipass
  parameters:
    - name: filter
      value: butterworth
      status: Optional
    - name: highPassHz
      value: 800
      status: Optional
    - name: chunkSize
      value: 134217728
      status: Optional
```

---

## `ndm_bandpass` — band-pass filter (`.dat` → `.fil`)

### Synopsis

```bash
ndm_bandpass [session.yaml]
```

**Input:** `SESSION.dat` (or `SESSION.<inputExtension>`)

**Output:** `SESSION.fil`

### Description

An alternative to `ndm_hipass` with `filter: median` that also removes the
content above the spike band. Spike energy lies roughly between 300 Hz and
5–7 kHz; above ~7 kHz the `.fil` holds only noise that hurts detection and
clustering. The two edges are set separately:

- **Lower edge** — the median-subtraction high-pass of `ndm_hipass`
  (`windowHalfLength`), with the same properties.
- **Upper edge** — a linear-phase windowed-sinc FIR low-pass
  (`lowpassCutoff`). The symmetric kernel has a constant group delay, which is
  compensated, so waveforms are not distorted.

It runs two passes so the median stage keeps its CUDA path:
`process_medianfilter` writes `SESSION.fil.hp.tmp` (removed on exit), then
`process_lowpass` (CPU/OpenMP) writes `SESSION.fil`. It is therefore slower
than `ndm_hipass`: the full-size file is read and written once more.

Run **either** `ndm_hipass` **or** `ndm_bandpass`: both write `SESSION.fil`,
and the second one skips with exit 10. For a linear band-pass, use
`ndm_hipass` with `filter: butterworth` and `lowPassHz` instead.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `windowHalfLength` | — (**required**) | Median high-pass half-window, samples; sets the lower edge as in `ndm_hipass` |
| `lowpassCutoff` | — (**required**) | Upper cutoff, Hz; must be below Nyquist. ~6000 suits a 32 kHz recording |
| `lowpassHalfLength` | 32 | FIR half-length *n*; the filter has 2*n*+1 taps. More taps give a sharper transition |
| `chunkSize` | 134217728 (128 MiB) | Bytes per pass, both stages; capped at 512 MiB |
| `inputExtension` | `dat` | File to filter, as for `ndm_hipass` |

```yaml
- name: ndm_bandpass
  parameters:
    - name: windowHalfLength
      value: 16
      status: Mandatory
    - name: lowpassCutoff
      value: 6000
      status: Mandatory
    - name: lowpassHalfLength
      value: 32
      status: Optional
    - name: chunkSize
      value: 134217728
      status: Optional
```

---

## `ndm_lfp` — create LFP file (`.dat` → `.lfp`)

### Synopsis

```bash
ndm_lfp [session.yaml]
```

**Input:** `SESSION.dat`, `SESSION-spkclean.dat`, or `SESSION.<inputExtension>`

**Output:** `SESSION.lfp`

### Description

Resamples a wide-band file to the LFP rate with `process_resample`. Skips
with exit 10 if `SESSION.lfp` exists.

The source is chosen in this order:

1. **`inputExtension` other than `dat`** — that file, always. The
   spike-cleaned dat is not used even when it exists, because `ndm_stripdat`
   cleans `SESSION.dat` only and cannot produce a spike-free version of
   another file. Combining it with `subtractSpikes: yes` is an error.
2. **`SESSION-spkclean.dat`** (from [`ndm_stripdat`](ndm_stripdat.md)), if it
   exists.
3. Otherwise `subtractSpikes` decides: `auto` (or unset) means `yes` when any
   `SESSION.spk.*` file exists and `no` otherwise; `yes` runs `ndm_stripdat`
   first and resamples its output; `no` resamples `SESSION.dat`.

A `SESSION-spkclean.dat` that `ndm_lfp` created itself is deleted once the
`.lfp` is written; one produced beforehand is left in place.

The source must be wide-band, not the `.fil`: the `.fil` has lost everything
below the spike band, which is what the LFP is made of.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `samplingRate` | — (**required**) | Target rate, Hz; should equal `fieldPotentials.lfpSamplingRate` (1250 in the template) |
| `subtractSpikes` | `auto` | `auto`, `yes` or `no` (see above) |
| `inputExtension` | `dat` | Source file (see above) |

```yaml
- name: ndm_lfp
  parameters:
    - name: samplingRate
      value: 1250
      status: Mandatory
    - name: subtractSpikes
      value: 'auto'
      status: Optional
```

---

## `ndm_slicebinary` — channel subset (`.<ext>` → `.<ext>.slice.<tag>`)

### Synopsis

```bash
ndm_slicebinary [session.yaml]
```

**Input:** `SESSION.<inputExtension>` (default `lfp`)

**Output:** `SESSION.<inputExtension>.slice.<tag>` and the sidecar
`SESSION.<inputExtension>.slice.<tag>.info`

### Description

Copies a set of channels, optionally over a time window, out of a session
binary into a smaller binary (`process_slicebinary`). The source is opened
read-only. Use it to pull one shank or a few reference channels out of a
96-channel `.lfp`.

The output extension mirrors the input, so the name says which stream the
data came from:

| `inputExtension` | Output |
|---|---|
| `lfp` (default) | `SESSION.lfp.slice.<tag>` |
| `dat` | `SESSION.dat.slice.<tag>` |
| `fil` | `SESSION.fil.slice.<tag>` |

An empty `slicedChannels` means no slice was requested: the step says so and
exits 0, so an unconfigured node does not fail a pipeline run. Re-running with
an existing `tag` skips with exit 10; pick another tag or remove the file.

**Sample width.** `nChannels` and `nBits` come from `acquisitionSystem`. 16-
and 32-bit samples are supported; anything else is refused. A file whose
length is not a whole number of records is refused too — but reading a 32-bit
file as 16-bit usually divides evenly, so this check is a safety net, not a
guarantee.

**Sampling rate.** `timeRange` is converted to records with the rate of the
stream being cut: `fieldPotentials.lfpSamplingRate` when `inputExtension` is
`lfp`, otherwise `acquisitionSystem.samplingRate`. The same `2,4` is 2500
records of a 1250 Hz `.lfp` and 65104 records of a 32552 Hz `.dat`. A
down-sampled source under another extension (e.g. `eeg`) is therefore given the
acquisition rate.

### The `.info` sidecar

The slice is headerless, so six lines are written next to it:

```
nChannels: 96
nBits: 16
slicedChannels: 0,1,4,5
inputExtension: lfp
samplingRate: 1250
timeRange: 0,600
```

- `nChannels` is the **source** width; the slice's width is the length of
  `slicedChannels`. With `nBits` it fixes the byte layout.
- `samplingRate` is the rate used above (`unknown` if not set).
- `timeRange` is the window as configured, in seconds, or `whole`.

Grouping, anatomy and geometry are not copied: they follow from
`slicedChannels` and the parent session. The sidecar is not a parameter file.
NeuroScope looks for a parameter file by stripping only the last
dot-component, so opening `SESSION.lfp.slice.shank0` looks for
`SESSION.lfp.slice.yaml`.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `slicedChannels` | — | 0-based channels in output order, comma- or space-separated (`0,1,4` = `0 1 4`). Channels may be reordered or repeated. Only plain integers: the gain (`5*1.5`) and reference (`5-2`) forms of `process_extractchannels` are refused. Empty: no slice, exit 0 |
| `tag` | — (**required** when `slicedChannels` is set) | Output name component: letters, digits, `-`, `_`; a dot, slash or whitespace is refused |
| `timeRange` | whole file | `start,end` in seconds, half-open `[start, end)`, each bound rounded to the nearest sample. End must exceed start; a window past the end of the file is refused, not clamped |
| `inputExtension` | `lfp` | File to slice (`dat`, `fil`, …) |

```yaml
- name: ndm_slicebinary
  parameters:
    - name: slicedChannels
      value: ''
      status: Mandatory
    - name: tag
      value: ''
      status: Mandatory
```

---

## See also

- [`.dat` / `.lfp` / `.fil`](../formats/dat.md)
- [Spike detection](spike-detection.md) — reads the `.fil`
- [`ndm_stripdat`](ndm_stripdat.md) — writes `SESSION-spkclean.dat`
- [Channel manipulation](channel-manipulation.md) — `ndm_extractchannels`, `ndm_resample`

---

*Part of the [ndmanager-plugins](../README.md) reference.*
