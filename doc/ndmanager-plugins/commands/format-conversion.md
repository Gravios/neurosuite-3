# Format conversion scripts

Short utilities for format conversion scripts.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_ncs2dat` — Neuralynx `.ncs` → `.dat`

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


---

## `ndm_smr2dat` — CED/Spike2 `.smr` → `.dat`

**Input:** `../SESSION-smr/SESSION-smr.smr`
**Output:** `SESSION-smr.dat`


---

## `ndm_nev2evt` — Neuralynx `.nev` → `.evt`

Converts Neuralynx event files to the plain-text `.evt` format used by
NeuroScope.


---

## `ndm_nvt2spots` — Neuralynx `.nvt` → `.spots` / `.sts`

Extracts LED positions and frame timestamps from Neuralynx Video Tracker
files.


---

## `ndm_smr2evt`, `ndm_smi2sts`, `ndm_tsp2sts`

CED Spike2 events, Neuralynx MPEG subtitle timestamps, and Amplipex
timestamps respectively.



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
