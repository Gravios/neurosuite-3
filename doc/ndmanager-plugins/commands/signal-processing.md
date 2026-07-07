# Signal processing

Short utilities for signal processing.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_hipass` — high-pass filter (`.dat` → `.fil`)

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


---

## `ndm_bandpass` — band-pass filter (`.dat` → `.fil`)

An alternative to `ndm_hipass` that band-passes rather than only high-passes.
The median-subtraction high-pass sets the lower edge (`windowHalfLength`, exactly
as in `ndm_hipass`, preserving spike-waveform shape), then a linear-phase FIR
low-pass (`process_lowpass`) attenuates noise above the spike band
(`lowpassCutoff`, typically ~6 kHz). The output is written to `SESSION.fil` just
like `ndm_hipass`, so downstream spike extraction is unchanged — run **either**
`ndm_hipass` **or** `ndm_bandpass`, not both.

Internally it composes two passes so the expensive median high-pass keeps its
CUDA path: `process_medianfilter` writes a temporary, then `process_lowpass`
(CPU/OpenMP) produces the final `.fil`. The FIR is linear phase (constant group
delay), so it introduces no waveform distortion beyond a fixed, compensated
shift.

**Input:** `SESSION.dat`
**Output:** `SESSION.fil`

```yaml
- name: ndm_bandpass
  parameters:
  - {name: windowHalfLength,  value: 16,         status: Mandatory}
  - {name: lowpassCutoff,     value: 6000,       status: Mandatory}
  - {name: lowpassHalfLength, value: 32,         status: Optional}
  - {name: chunkSize,         value: 134217728,  status: Optional}
```


---

## `ndm_lfp` — create LFP file (`.dat` → `.lfp`)

Downsamples the wideband `.dat` to the LFP sampling rate (typically 1250 Hz).
Automatically uses `SESSION-spkclean.dat` (from `ndm_stripdat`) when it is
present.



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
