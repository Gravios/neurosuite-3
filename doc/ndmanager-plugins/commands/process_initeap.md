# `process_initeap` — pre-construct the `.eap` / `.tcl` template-class layer

## Synopsis

```bash
process_initeap --session S --group G
    [--method M] [--n-cells N] [--param-file P] [--overwrite 0|1]
```

**Output:** `<base>.eap.<group>` (an all-absent membership matrix) and
`<base>.tcl.<group>` (`nCells` `free` class slots), for one spike group. Existing
files are left in place unless `--overwrite 1` is passed (a pre-existing layer is
reported and treated as success, not an error).

## Description

Sets up the EAP template-class layer for a spike group before any template or
decollision exists. It writes an all-absent [`.eap`](../formats/eap.md) matrix
(`nSpikes × nCells` int8, every cell `EAP_ABSENT`) and a [`.tcl`](../formats/tcl.md)
registry of `nCells` `free` slots.

- **`nSpikes`** is the group's spike count, taken from its `.res` (membership is
  method-independent, so any method's `.res` is accepted via the custody resolver).
- **`T = nCells`** (the number of template-class columns) comes from `--n-cells` if
  given, else the `nCells:` entry of the group's `spikeDetection` block in
  `--param-file`, else **128**.

The files it writes are the **untagged, canonical** copies; later curation stages
inherit the base `.eap` membership until they are edited, and the `.tcl` is shared
across all stages (see the [naming convention](../formats/naming.md)).

## Options

| Option | Default | Meaning |
|---|---|---|
| `--session S` | — | session base (the `.res`/`.spk` anchor) |
| `--group G` | — | 1-based spike group |
| `--method M` | `standard` | preferred variant used to resolve the group's `.res` |
| `--n-cells N` | YAML `nCells:`, else 128 | override the pre-allocated class count `T` |
| `--param-file P` | — | session YAML providing the per-group `nCells:` |
| `--overwrite 0\|1` | `0` | rewrite an existing `.eap` / `.tcl` |

---

*Part of the [ndmanager-plugins](../README.md) command reference.*
