# `ndm_estimatedrift` — estimate probe drift from curated clusters (`.res` + `.clu` + `.spk` → `.drift`)

## Synopsis

```bash
ndm_estimatedrift [session]
```

**Input:** per spike group `SESSION.res.N`, `SESSION.clu.N` and, for the
waveform-based estimate, `SESSION.spkD.N` or `SESSION.spk.N`; site geometry
from the session YAML and its probe file; `SESSION.dat` (only to size the
binary drift signal).

**Output:** `SESSION.drift` (YAML, see [`.drift`](../formats/drift.md)) and,
when `SESSION.dat` exists, one binary drift signal `SESSION.dat.drift.P` per
probe.

> **File names.** The helper composes the untagged names above itself; it
> does not resolve method-tagged files (`SESSION.clu.<method>.N`,
> `.spk.<method>.N`, see [naming](../formats/naming.md)).  A session sorted
> with the current pipeline needs untagged copies or links of its `.res`,
> `.clu` and `.spk` for this command to see them.  `.spkD.N` (a retired name)
> is preferred over `.spk.N` when both exist.

## Description

Estimates in-vivo probe drift, in µm, from spike-sorting results that have
been curated in Klusters.  Run it after [`ndm_klustakwik`](ndm_klustakwik.md)
and curation.  The step is skipped (exit 10) when `SESSION.drift` already
exists — delete it to re-estimate.  It fails with exit 11 when no group has a
`.clu`.

For each spike group, the recording is cut into windows of `windowSec`
seconds.  Units with fewer than `minSpikes` spikes over the session are
ignored; with `excludeNoise`, clusters 0 (artefact) and 1 (MUA) are too.
The **reference window** is the first window in which at least `minUnits`
units each fire `minSpikes` spikes; drift is reported relative to it (0 µm
there).  Two estimates are made per window:

- **Per-unit cross-correlation (primary).**  For each unit active in both
  the reference and the current window, its mean peak-to-peak
  amplitude-per-site profile is cross-correlated against the reference
  profile along depth, with parabolic interpolation for sub-site precision.
  Units whose pairwise distances to the other units change by more than
  5 µm are flagged as outliers and excluded.  The window's estimate is the
  weighted median of the remaining unit shifts; the weight is chosen by
  `weightMode`.
- **Population cross-correlation (fallback).**  The same cross-correlation
  on the amplitude profile pooled over all non-noise spikes in the window.
  Needs the `.spk` file.

When at least `minUnits` inlier units contribute, the window's `drift_um`
is a weighted combination of the two (per-unit estimate weighted by the
number of inlier units, population estimate weight 1); otherwise the
population estimate is used alone.  Without a `.spk` file only the
per-unit path runs, on spike counts, and gives no real displacement.

A group whose `.clu` holds only noise clusters, or that never reaches
`minUnits` units in a window, gets a placeholder entry with `skipReason`
instead of windows; [`ndm_applydrift`](ndm_applydrift.md) then falls back to
another shank.  For each probe with at least one estimated shank, a
probe-level trace (mean of its shanks weighted by their reference-window unit
counts) is written as well; `ndm_applydrift` prefers it.

For each probe, the trace of the first shank (lowest `shankIndex`) that has
windows — not the probe-level average — is then interpolated linearly from
window midpoints to every recording sample and written as
`SESSION.dat.drift.P` (int16 µm), which [`ndm_driftcorrect`](ndm_driftcorrect.md)
reads.  `P` is the probe's 1-based position in the YAML `probes:` list.

### Site geometry

Each group's site depths are looked up **by channel number**, so a group may
list its channels in any order:

1. `sitePositions_um` written on the `spikeDetection` group, if present
   (one `[x, y]` per channel, in the group's channel order);
2. otherwise the probe file: the group's `probeId` / `shankIndex` (on the
   spike group, else on the anatomical group with the same index, else
   probe 0) selects the `probes:` entry, and each channel's `y` is read from
   `sites.geometry` at site `channel − channelOffset` (or through
   `channelMap.map`) — see [`.probe` geometry and channels](../formats/probe.md#geometry-and-channels).
   This is `scripts/probe_geometry.py`, shared with `ndm_driftcorrect`,
   `ndm_setupgroups` and `ndm_localise`.

Profiles are sorted by depth before cross-correlation, so the sign of the
shift does not depend on the channel order.  The site spacing used to
convert lags to µm is the median spacing of the group's depths.

Probe files named in `probes[].probeFile` are searched, in order: as given,
the YAML's top-level `probeLibraryPath`, the `probeLibrary` parameter,
`$NEUROSUITE_PROBE_PATH`, `~/.local/share/neurosuite/probes`,
`/usr/share/neurosuite/probes`, `./probes`.  If no probe entry or probe file
is found, the group falls back to 8 sites at 50 µm spacing.

## Parameters

Set under `programs:` → `- name: ndm_estimatedrift` → `parameters:` (see
[parameters](../parameters.md)).

| Name | Default | Meaning |
|---|---|---|
| `windowSec` | `60` | Window length, seconds. |
| `minUnits` | `3` | Units needed in a window to define the reference window, and inlier units needed for the per-unit estimate to be used. |
| `minSpikes` | `20` | Spikes a unit needs in a window (and over the session) to count. |
| `excludeNoise` | `true` | Ignore clusters 0 and 1. |
| `weightMode` | `geometry` | Per-unit weight in the weighted median: `count` = √(n_ref × n_win); `sharpness` = cross-correlation peak curvature × √(active sites in both windows) × the count term; `geometry` = `sharpness` × how strongly the unit's reference profile changes along the shank (down-weights far-field units). |
| `nWorkers` | `1` | Worker processes, one shank each; `0` = all CPUs. |
| `probeLibrary` | — | Extra directory searched for probe files (see above). |

```yaml
- name: ndm_estimatedrift
  parameters:
    - {name: windowSec,    value: 60,       status: Optional}
    - {name: minUnits,     value: 3,        status: Optional}
    - {name: minSpikes,    value: 20,       status: Optional}
    - {name: excludeNoise, value: 'true',   status: Optional}
    - {name: probeLibrary,                  status: Optional}
    - {name: weightMode,   value: geometry, status: Optional}
    - {name: nWorkers,     value: 1,        status: Optional}
```

Also read from the YAML: `acquisitionSystem` `samplingRate`, `nChannels`,
`nBits`, and each spike group's `nSamples` (default 32).  The outlier
threshold (5 µm) is fixed by the wrapper.

## See also

- [`ndm_applydrift`](ndm_applydrift.md) — chunk boundaries from the drift trace
- [`ndm_driftcorrect`](ndm_driftcorrect.md) — re-interpolate waveforms with `SESSION.dat.drift.P`
- [`ndm_drifttracker`](ndm_drifttracker.md) — drift-aware cluster merging (no probe geometry)
- [`.drift` format](../formats/drift.md), [`.probe` format](../formats/probe.md)
- [Drift correction workflow](../../workflows/drift-correction.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
