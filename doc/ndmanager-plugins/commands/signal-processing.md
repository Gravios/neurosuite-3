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

## `ndm_lfp` — create LFP file (`.dat` → `.lfp`)

Downsamples the wideband `.dat` to the LFP sampling rate (typically 1250 Hz).
Automatically uses `SESSION-spkclean.dat` (from `ndm_stripdat`) when it is
present.



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
