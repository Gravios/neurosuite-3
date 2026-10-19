# `ndm_applydrift` — drift-adaptive KiloKlustaKwik chunk boundaries (`.drift` → `.chunks.N`)

## Synopsis

```bash
ndm_applydrift [session] [source-group] [target-groups...]

ndm_applydrift               # source from sourceGroup (default 1); all other groups
ndm_applydrift 7             # source group 7; all other groups
ndm_applydrift 7 3 5 9       # source group 7; groups 3, 5, 9
```

**Input:** `SESSION.drift` (from [`ndm_estimatedrift`](ndm_estimatedrift.md)).
With `runKlustaKwik`, also `SESSION.fet.<method>.N` for each target group.

**Output:** `SESSION.chunks.N` for the source group and every target group
(see [`.chunks.N`](../formats/chunks.md)).  With `runKlustaKwik`, a new
`SESSION.clu.<method>.N` for each target group.

## Description

Turns a drift trace into chunk boundaries for KiloKlustaKwik's chunked CEM,
so that each chunk covers a stretch of roughly constant probe position
instead of a fixed number of minutes.  The session name may be omitted (it
is then taken from the current directory); a first argument that is not a
parameter file is read as the source group.  The source group given on the
command line overrides `sourceGroup`; target groups default to every spike
group except the source.

**Choosing the trace.**  If the source group's probe has a probe-level
averaged trace in `SESSION.drift` (written whenever at least one of its shanks
was estimated), that trace is used.  Otherwise the source group's own windows are
used.  If the source group is missing from the file or was skipped by
`ndm_estimatedrift` (`skipReason`), the nearest usable shank on the same probe
is used instead, then any usable shank on another probe; the output files
keep the requested names.  With no usable shank anywhere the command fails.

**Cutting.**  Scanning the windows in order, a boundary is placed at the
start of a window when the range of `drift_um` values since the previous
boundary reaches `threshUm` and the current chunk is at least `minChunkSec`
long.  Windows without an estimate count as 0 µm.  Boundaries start at 0 and
end at the last window's end, so they align with drift-window edges.  The
same boundaries are written to every target group's file, and to the source
group's.  If the drift range never reaches `threshUm`, each file holds a
single chunk, which disables chunking for those groups.

Existing `.chunks.N` files are overwritten.  The command exits 11 when
`SESSION.drift` is missing.

**Re-sorting.**  With `runKlustaKwik: true`, the `KlustaKwik` binary is run
for each target group (not the source group) with
`-ChunkFile SESSION.chunks.N`, `-Method <method>`, `-SaveIntermediates 0` and
the KlustaKwik parameters below; `-UseFeatures all` uses every column of
the `.fet`, the timestamp included.  Groups without `SESSION.fet.<method>.N` are skipped.  The
group's existing `.clu.<method>.N` is replaced.  Other KiloKlustaKwik
parameters (the `ndm_klustakwik` block and its tiers) are not read or passed
here.

## Parameters

Set under `programs:` → `- name: ndm_applydrift` → `parameters:` (see
[parameters](../parameters.md)).

| Name | Default | Meaning |
|---|---|---|
| `sourceGroup` | `1` | Spike group whose drift trace is used, when not given on the command line. |
| `threshUm` | `5.0` | Drift range, µm, that starts a new chunk. |
| `minChunkSec` | `30.0` | Shortest chunk, seconds. |
| `runKlustaKwik` | `false` | Re-run KlustaKwik on each target group with its new chunk file. |
| `method` | `standard` | Method token of the sort: features read from `.fet.<method>.N`, `-Method <method>`. |
| `minClusters` | `2` | `-MinClusters` (only with `runKlustaKwik`). |
| `maxClusters` | `100` | `-MaxClusters`. |
| `mergeThresh` | `35` | `-MergeThresh`. |
| `globalMergeIter` | `30` | `-GlobalMergeIter`. |
| `timeMergeIter` | `35` | `-TimeMergeIter`. |

Also read from the YAML: `acquisitionSystem/samplingRate` (written into the
file header and passed to KlustaKwik).

The helper `process_applydrift.py` also accepts `--shank-specific`, which
ignores the probe-level average and uses the source group's own trace; the
wrapper does not pass it.

## See also

- [`ndm_estimatedrift`](ndm_estimatedrift.md) — writes `SESSION.drift`
- [`ndm_klustakwik`](ndm_klustakwik.md) — sorting; it does not pass `-ChunkFile`, so `.chunks.N` is used only by the `runKlustaKwik` path here or a direct `KlustaKwik … -ChunkFile` call
- [`.drift` format](../formats/drift.md), [`.chunks.N` format](../formats/chunks.md)
- [Drift correction workflow](../../workflows/drift-correction.md)
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
