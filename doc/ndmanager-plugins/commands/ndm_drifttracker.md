# `ndm_drifttracker` — drift-aware cluster merging (`.res` + `.clu` + `.spk` → `.clu.N.drift`)

## Synopsis

```bash
ndm_drifttracker [session]
```

**Input:** per spike group `SESSION.res.N`, `SESSION.clu.N` and
`SESSION.<spkSuffix>.N` (default `SESSION.spk.N`); the session YAML.

**Output:** per spike group `SESSION.clu.N.drift` (merged cluster IDs, same
binary layout as `.clu`) and `SESSION.drift.N.yaml` (per-window report).
`SESSION.clu.N` is not modified.

> **File names.** Only the untagged names above are read; method-tagged
> files (`SESSION.clu.<method>.N`, `.spk.<method>.N`, see
> [naming](../formats/naming.md)) are not resolved, so a group sorted with the
> current pipeline is skipped unless untagged copies or links exist.

## Description

Merges clusters that follow the same drifting unit.  A single global mean
waveform per cluster (as in KiloKlustaKwik's mean-subtraction merge) makes a
drifting unit look different from itself across the session; this command
tracks each cluster's mean waveform through time instead.  Per spike group:

1. **Windowed templates.**  Windows of `windowMin` minutes, stepping by
   `windowMin − overlapMin`.  A cluster's mean waveform is defined in a
   window when it has at least `minWinSpikes` spikes there.
2. **Smoothing.**  Each cluster's sequence of templates is smoothed over time
   with a Tikhonov (ridge) penalty of weight `lambda` on window-to-window
   change; windows where the cluster is undefined are interpolated from their
   neighbours.
3. **Pair comparison.**  For two clusters defined together in at least
   `minWinOverlap` windows, the residual in each shared window is the
   smallest normalised squared difference over cyclic time shifts of up to
   `±maxShift` samples; the pair's distance is the mean over shared windows.
4. **Merge.**  Pairs with distance below `mergeThresh` are merged
   transitively, the smallest cluster ID surviving.  A merged group never
   grows beyond 8 original clusters.

Each cluster's centre of mass is also tracked as a diagnostic.  The wrapper
passes no site positions, so the position axis is the channel's index within
the group (units "channels"), not probe geometry; the probe file is not read.

The group's channel count, `nSamples`, `peakSampleIndex` and the sampling
rate are read from the YAML by `process_drifttracker` itself.

A group is skipped silently when its `.res.N` or `.clu.N` is missing, with a
warning when its `.<spkSuffix>.N` is missing, and with a message when
`SESSION.clu.N.drift` already exists (delete it to re-run).  A failure on any
group stops the run (exit 1).

To adopt the result, review `SESSION.drift.N.yaml` (windows, per-window
cluster counts, centre-of-mass trajectories, applied and rejected merges) and
replace the `.clu` with `SESSION.clu.N.drift`.

## Parameters

Set under `programs:` → `- name: ndm_drifttracker` → `parameters:` (see
[parameters](../parameters.md)).

| Name | Default | Meaning |
|---|---|---|
| `windowMin` | `5.0` | Window length, minutes. |
| `overlapMin` | `2.5` | Overlap between consecutive windows, minutes. |
| `minWinSpikes` | `30` | Spikes a cluster needs in a window to define its template there. |
| `lambda` | `1.0` | Ridge weight on template change between windows; higher = smoother. |
| `mergeThresh` | `0.05` | Mean normalised residual below which two clusters merge. |
| `maxShift` | `3` | Half-width of the cyclic shift search, samples. |
| `minWinOverlap` | `3` | Windows two clusters must share before they are compared. |
| `spkSuffix` | `spk` | Waveform file type: `spk` reads `SESSION.spk.N`; `spkD` reads the retired stderiv name `SESSION.spkD.N`. |

```yaml
- name: ndm_drifttracker
  parameters:
    - {name: windowMin,     value: 5.0,  status: Optional}
    - {name: overlapMin,    value: 2.5,  status: Optional}
    - {name: minWinSpikes,  value: 30,   status: Optional}
    - {name: lambda,        value: 1.0,  status: Optional}
    - {name: mergeThresh,   value: 0.05, status: Optional}
    - {name: maxShift,      value: 3,    status: Optional}
    - {name: minWinOverlap, value: 3,    status: Optional}
    - {name: spkSuffix,     value: spk,  status: Optional}
```

`process_drifttracker SESSION GROUP [options]` can also be run directly; it
has further options the wrapper does not set (per-channel gain fit,
`--channel-positions` file, position filter, shared probe-wide drift,
cross-temporal pairing of non-overlapping clusters, `--max-merge-chain`,
`--dry-run`).  See `process_drifttracker --help`.

## See also

- [`ndm_estimatedrift`](ndm_estimatedrift.md) — probe drift in µm from curated clusters
- [`.clu` format](../formats/clu.md), [`.spk` format](../formats/spk.md)
- [Drift correction workflow](../../workflows/drift-correction.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
