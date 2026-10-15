# `process_initeap` — pre-construct the EAP template-class layer (`.res` → `.eap` + `.tcl`)

## Synopsis

```bash
process_initeap --session S --group G
    [--method M] [--n-cells N] [--param-file P] [--overwrite 0|1]
```

**Input:** the group's `.res` (any method)

**Output:** `S.eap.G` (all-absent membership matrix) and `S.tcl.G`
(registry of `free` class slots)

## Description

Sets up the EAP template-class layer for one spike group before any template
or decollision exists, so later tools only fill an already-sized matrix. It
writes:

- an [`.eap`](../formats/eap.md) matrix of `nSpikes × T` int8 cells, all
  `EAP_ABSENT` (−128);
- a [`.tcl`](../formats/tcl.md) registry with `T` slots, all `free`.

`nSpikes` is the length of the group's `.res`. Spike times are shared across
methods, so the `.res` is looked up under `--method` first, then the
default method and the untagged name, then any other method tag. Binary and
text `.res` are both read.

`T` (the number of template-class columns) is `--n-cells` if given, else the
`nCells:` entry of the group in the `spikeDetection.channelGroups` section of
`--param-file`, else 128.

Both files are written under the untagged, method-less names. Curation stages
(`S.eap.G.<stage>`) inherit this base membership until they are edited; the
`.tcl` is shared by all stages (see [file naming](../formats/naming.md)).

Without `--overwrite 1`, if **either** `S.eap.G` or `S.tcl.G` already exists,
nothing is written and the program exits 0. It exits 1 on a usage error, when
no `.res` is found or readable, when `T < 1`, or when a file cannot be written.

No `ndm_*` driver calls `process_initeap`; run it once per group by hand.
[`ndm_decomposecollisions`](ndm_decomposecollisions.md) creates the layer
itself (same `T` rule) when it is missing.

## Options

| Option | Default | Meaning |
|---|---|---|
| `--session S` | required | Session base (the `.res` anchor) |
| `--group G` | required | 1-based spike group |
| `--method M` | none (default method first) | Preferred method tag when resolving the `.res` |
| `--n-cells N` | YAML `nCells:`, else 128 | Number of class columns `T` |
| `--param-file P` | none | Session YAML that provides the group's `nCells:` |
| `--overwrite 0\|1` | `0` | Replace existing `.eap` / `.tcl` |

Example, for every group of a four-group session:

```bash
for g in 1 2 3 4; do
    process_initeap --session session --group $g --param-file session.yaml
done
```

## See also

- [`.eap`](../formats/eap.md), [`.tcl`](../formats/tcl.md) — the files it writes
- [`ndm_decomposecollisions`](ndm_decomposecollisions.md) — fills the layer with collision constituents

---

*Part of the [ndmanager-plugins](../README.md) reference.*
