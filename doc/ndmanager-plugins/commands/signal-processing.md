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

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
