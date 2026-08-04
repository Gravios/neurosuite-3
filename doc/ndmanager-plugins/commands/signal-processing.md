# Signal processing

Short utilities for signal processing.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_hipass` — high-pass filter (`.dat` → `.fil`)

Applies a median-subtraction high-pass filter. `windowHalfLength=16` at
30 kHz ≈ 600 Hz cutoff. Uses CUDA when available, otherwise OpenMP.

**Input:** `SESSION.dat` (or `SESSION.<inputExtension>`)
**Output:** `SESSION.fil`

- **`inputExtension`** *(optional, default `dat`)* — the file to filter, without
  the leading dot. Point it at an alternative, externally-processed wide-band
  source (e.g. `car.dat`) to filter that instead of the raw recording. It must
  still be wide-band data; the filter high-passes it as usual.

```yaml
- name: ndm_hipass
  parameters:
    - name: windowHalfLength
      value: 16
      status: Mandatory
    - name: chunkSize
      value: 134217728
      status: Optional
    - name: inputExtension
      value: dat
      status: Optional
```


---

## `ndm_bandpass` — band-pass filter (`.dat` → `.fil`)

An alternative to `ndm_hipass` that band-passes the wideband `.dat` rather than
only high-passing it. `ndm_hipass` removes everything **below** ~600 Hz but
leaves the whole upper spectrum (up to Nyquist) in the `.fil`; `ndm_bandpass`
additionally attenuates everything **above** the spike band, so the signal fed
to spike detection carries energy only where spikes actually live.

**Input:** `SESSION.dat` &nbsp;·&nbsp; **Output:** `SESSION.fil`

Run **either** `ndm_hipass` **or** `ndm_bandpass` — both write `SESSION.fil`, so
running both is redundant (the second refuses, because the output already
exists).

### What it does

Extracellular spike energy sits roughly between 300 Hz and 5–7 kHz; there is
essentially nothing spike-related above ~7 kHz, so higher-frequency content in
the `.fil` is noise that only hurts detection and clustering. `ndm_bandpass`
sets the two edges independently:

- **Lower edge** — the same median-subtraction high-pass as `ndm_hipass`
  (`windowHalfLength`). The median is non-linear and edge-preserving, so it
  strips slow baseline drift without rounding off the sharp spike waveform.
- **Upper edge** — a linear-phase FIR low-pass (`lowpassCutoff`). A windowed-sinc
  FIR is used rather than an IIR because IIR phase distortion would smear the
  spike shape; the symmetric kernel has constant group delay — only a fixed shift,
  which is compensated — so waveforms are preserved.

Internally it runs two passes so the expensive median high-pass keeps its CUDA
path when available: `process_medianfilter` writes a temporary
(`SESSION.fil.hp.tmp`, removed on exit), then `process_lowpass` (CPU/OpenMP)
produces the final `.fil`.

### Parameters

- **`windowHalfLength`** — median high-pass half-window; sets the lower cutoff,
  identical to `ndm_hipass`. 16 samples ≈ 600 Hz at ~30 kHz.
- **`lowpassCutoff`** — upper cutoff in Hz; must be below Nyquist. ~6000 suits a
  32 kHz octrode (keeps the spike band, drops HF noise).
- **`lowpassHalfLength`** *(optional, default 32)* — FIR half-length; the filter
  has `2·n+1` taps. More taps → sharper transition at the cutoff, at slightly
  more compute. 32 (65 taps) is a sensible default.
- **`chunkSize`** *(optional)* — bytes processed per pass (default 128 MiB,
  capped internally at 512 MiB). Reduce if RAM is tight.
- **`inputExtension`** *(optional, default `dat`)* — the file to filter, without
  the leading dot; identical in meaning to `ndm_hipass`'s. Point it at an
  alternative wide-band source (e.g. `car.dat`) to band-pass that instead of the
  raw recording.

Note that `ndm_bandpass` is slower than `ndm_hipass`: it reads and writes the
full-size `.fil` an extra time for the low-pass pass. The median stage, which
dominates the cost, still runs on the GPU; the low-pass itself is a cheap short
FIR.

**Input:** `SESSION.dat` (or `SESSION.<inputExtension>`)
**Output:** `SESSION.fil`

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
    - name: inputExtension
      value: dat
      status: Optional
```


---

## `ndm_lfp` — create LFP file (`.dat` → `.lfp`)

Downsamples the wideband `.dat` to the LFP sampling rate (typically 1250 Hz).
Automatically uses `SESSION-spkclean.dat` (from `ndm_stripdat`) when it is
present.

**Input:** `SESSION.dat`, `SESSION-spkclean.dat`, or `SESSION.<inputExtension>`
**Output:** `SESSION.lfp`

### Parameters

- **`samplingRate`** — target LFP rate in Hz (default 1250). Should match
  `fieldPotentials.lfpSamplingRate`.
- **`subtractSpikes`** *(optional, default `auto`)* — whether to strip spikes
  before resampling. `auto` runs `ndm_stripdat` first if `.spk.N` files exist;
  `yes` always runs it; `no` resamples the raw source unconditionally. When
  `ndm_lfp` invokes `ndm_stripdat` itself, the cleaned dat is removed once the
  `.lfp` is written; a cleaned dat you produced beforehand is left in place.
- **`inputExtension`** *(optional, default `dat`)* — the file to downsample,
  without the leading dot, as on `ndm_hipass` / `ndm_bandpass`.

### Source precedence

`inputExtension` → `SESSION-spkclean.dat` → `SESSION.dat`.

Any `inputExtension` other than `dat` selects the source outright: the
spike-cleaned dat is **not** used even when it exists, because `ndm_stripdat`
cleans `SESSION.dat` and cannot produce a spike-free version of another file —
resampling it would silently substitute a different signal. For the same reason
`inputExtension` ≠ `dat` together with `subtractSpikes=yes` is refused rather
than resolved one way or the other. `subtractSpikes=auto` resolves to `no`.

Note that a processed source used here should be **wide-band**, not the `.fil` —
`.fil` has already had everything below the spike band removed, which is exactly
the content the LFP is made of.

```yaml
- name: ndm_lfp
  parameters:
    - name: samplingRate
      value: 1250
      status: Mandatory
    - name: subtractSpikes
      value: auto
      status: Optional
    - name: inputExtension
      value: dat
      status: Optional
```



---

## `ndm_slicebinary` — channel subset (`.<ext>` → `.<ext>.slice.<tag>`)

Copies a set of channels out of an existing session binary into its own smaller
binary. The source is opened read-only and never modified, so slicing is always
safe to re-run.

**Input:** `SESSION.<inputExtension>` (default `lfp`)
**Output:** `SESSION.<inputExtension>.slice.<tag>` plus a six-field `.info` sidecar

Use it to pull one shank, one anatomical group, or a handful of reference
channels out of a 96-channel `.lfp` so downstream analysis loads megabytes
instead of gigabytes, without maintaining a second copy of the recording.

### Output naming

The output extension **mirrors the input** rather than being fixed to `lfp`:

| `inputExtension` | output |
|---|---|
| `lfp` (default) | `SESSION.lfp.slice.<tag>` |
| `dat` | `SESSION.dat.slice.<tag>` |
| `fil` | `SESSION.fil.slice.<tag>` |

A slice of the wide-band `.dat` is still wide-band data. Naming it
`.lfp.slice.<tag>` would tell every downstream consumer it had been decimated to
the LFP rate when it had not — the file would be a lie about its own sampling
rate, and nothing about a headerless binary could contradict it.

### The `.info` sidecar

A sliced binary is headerless, so five numbers are written next to it — enough to
parse the file and map every column back to the recording:

```
nChannels: 96
nBits: 16
slicedChannels: 0,1,4,5
inputExtension: lfp
samplingRate: 1250
timeRange: 0,600
```

- `nChannels` and `nBits` together fix the byte layout. Without both, the file
  cannot be indexed at all, and a wrong guess yields a file of the right length
  full of wrong numbers — the record-divisibility check only catches a mismatch
  that leaves a remainder.
- `nChannels` is the **source** width, which is what makes `slicedChannels`
  readable. The slice's own width is the length of that list.
- `samplingRate` follows the stream that was cut, not `acquisitionSystem`:
  slicing the `.lfp` records 1250, slicing the `.dat` or `.fil` records the
  acquisition rate. Recording 32552 for an LFP slice would misdate every sample
  in it.
- `timeRange` is the window as configured, in seconds, or `whole` when none was
  set. With `samplingRate` it gives the absolute position of the excerpt in the
  parent recording, which is otherwise unrecoverable from the bytes.

Nothing derivable is duplicated here. Channel grouping, anatomy and spike
geometry are all functions of `slicedChannels` against the parent session, so the
parent answers those questions exactly — whereas a copy written beside the slice
is the one that goes stale the moment the parent is regrouped.

This is a provenance record, not a session descriptor. Opening a slice in
neuroscope still needs a parameter file, and note that neuroscope derives one by
stripping only the **final** dot-component of the data file name: opening
`SESSION.lfp.slice.shank0` makes it look for `SESSION.lfp.slice.yaml`, which
every tag would share. Naming the output `SESSION.slice.<tag>.lfp` instead would
let neuroscope find a per-tag `SESSION.slice.<tag>.yaml` unaided.

### Parameters

- **`slicedChannels`** — channels to copy, 0-based, in the order they should
  appear in the output. Comma- or whitespace-separated (`0,1,4` and `0 1 4` are
  equivalent). Order is preserved, so the list can also **reorder** channels, and
  a channel may be **repeated** to duplicate it. Empty means "no slice
  requested": the stage says so and succeeds, so an unconfigured node does not
  fail a whole pipeline run.

  Only plain integers are accepted. Unlike `process_extractchannels`, the gain
  (`5*1.5`) and post-hoc reference (`5-2`) spellings are **refused** rather than
  honoured — a slice is meant to be a byte-exact excerpt, and a stray character
  silently producing a *derived* signal is invisible downstream.

- **`tag`** — names the output. Letters, digits, `-` and `_` only; a tag holding a
  dot, a slash or whitespace is refused, since it is a filename component and not
  a path. Re-running with the same tag is refused because the output exists —
  pick another tag or remove the file.

- **`timeRange`** *(optional)* — temporal window to copy, as `start,end` in
  **seconds** (e.g. `0,600` for the first ten minutes). Empty means the whole
  recording. The window is half-open — `[start, end)` — so adjacent ranges tile
  without overlapping or dropping a sample, and each bound is rounded to the
  nearest sample.

  Seconds are converted with the sampling rate of the **stream being cut**, not
  the acquisition rate: the same `2,4` is 2500 records of a 1250 Hz `.lfp` and
  65104 records of a 32552 Hz `.dat`. A window running past the end of the file
  is refused rather than clamped — a short file that reads as success gives no
  sign the window was not the one asked for.

- **`inputExtension`** *(optional, default `lfp`)* — the file to slice, without
  the leading dot, as on `ndm_hipass` / `ndm_lfp`.

### Sample width

`nChannels` and `nBits` are read from the session's `acquisitionSystem` block, so
a slice cannot disagree with the recording it came from. 16- and 32-bit are
supported and anything else is refused rather than reinterpreted.

The engine also refuses a file whose length is not a whole number of records,
which catches a wrong channel count or sample width — but **only when the
mismatch leaves a remainder**. Reading a 32-bit file as 16-bit usually still
divides evenly, so this is a safety net, not a guarantee. That is why the width
comes from the session rather than being guessed.

```yaml
- name: ndm_slicebinary
  parameters:
    - name: slicedChannels
      value: '0,1,4,5'
      status: Mandatory
    - name: tag
      value: shank0
      status: Mandatory
    - name: timeRange
      value: '0,600'
      status: Optional
    - name: inputExtension
      value: lfp
      status: Optional
```

---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
