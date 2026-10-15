# `ndm_localise` — per-spike source localisation (`.spk` → `.loc`)

## Synopsis

```bash
ndm_localise [session.yaml] [electrodeGroup]
```

**Input:** per group `session.spk.N`, optionally `session.clu.N`; site
geometry from the session YAML (and the probe library)

**Output:** `session.loc.N` per group (see [`.loc`](../formats/loc.md));
`session.loc.yaml` metadata, shared by all groups

## Description

Estimates where each spike's neuron sits relative to the probe by fitting a
point-source (monopole) model to the spike's per-channel peak-to-peak
amplitudes:

```
V_i = A / sqrt((x_i - x_s)² + (y_i - y_s)² + z_s²)
```

`(x_i, y_i)` are the site positions in µm (y = height above the shank tip),
`(x_s, y_s)` the source position in the shank plane, `z_s` its distance from
the shank face and `A` an unsigned amplitude (ADC counts × µm). The fit is a
bounded least-squares fit of log amplitudes (`scipy.optimize.least_squares`),
started from the amplitude-weighted centre of mass with `z_s` = 25 µm. Bounds:
x and y within `maxDistUm` of the array extent, `z_s` in [1, `maxDistUm`] µm.
A fit fails (x, y, z = NaN, A = 0) if the optimiser fails or the source is
farther than `maxDistUm` from every site.

With `electrodeGroup`, only that group is processed; otherwise every
`spikeDetection` group. The helper is `process_localise.py`; it needs Python 3
with `numpy`, `scipy` and `pyyaml` (checked at start, exit 1 if missing). A YAML
parameter file is required (exit 11 without one).

Per group, the step is skipped when `session.spk.N` is missing (status 11) or
`session.loc.N` exists and `overwrite` is not `1` (status 10); the run goes on
with the next group and exits 0. A helper failure stops the run (exit 1).

The per-group files are read under their **untagged** names:
`session.spk.N` (or `session.spkD.N` when present) and `session.clu.N`
(binary). Spikes of clusters 0 and 1 are fitted but their residual is set to
NaN; without a `.clu` every spike counts as a unit spike.

### Site geometry

Positions are looked up by channel **number** (via
`scripts/probe_geometry.py`), so a group may list its channels in any order.
For each group the first source that covers every channel wins:

1. `sitePositions_um` in the group's `spikeDetection.channelGroups` entry
   (written by [`ndm_setupgroups`](ndm_setupgroups.md)), if it has a
   non-null entry for every channel. If it is present but has null entries,
   step 2 is skipped too and steps 3–4 apply.
2. The probe file of the group's `probeId` (matched against `probeId`, or the
   legacy `id`, in the top-level `probes:` list). Site *j* of
   `probeFile.sites.geometry` belongs to channel `channelOffset + j`, or
   `channelOffset + channelMap.map[j]` (see [`.probe`](../formats/probe.md)).
   If a channel has no position, the first sites of shank `shankIndex` are
   used in file order (or the probe's `geometry_groups` / linear
   `spacing_um` layout).
3. A `geometry:` list of `[x, y]` pairs in the group entry.
4. A straight line of sites at 50 µm pitch (with a warning).

The probe file is searched as given, then in `probeLibrary`,
`$NEUROSUITE_PROBE_PATH`, `~/.local/share/neurosuite/probes/`,
`/usr/local/share/neurosuite/probes/`, `/usr/share/neurosuite/probes/` and
`probes/` in the working directory. The positions used are recorded in
`session.loc.yaml`.

## Parameters

Set in the `ndm_localise` block (see [parameters](../parameters.md)):

| Name | Default | Meaning |
|---|---|---|
| `method` | `monopole` | `monopole`: fit x, y, z and A. `com`: amplitude-weighted centre of mass, x and y only (z and residual NaN, A = sum of peak-to-peak amplitudes). Here `method` names the inverse model, not a file variant. |
| `clusterFilter` | `good` | Passed to the helper (`all`, `good`, `single`) but currently has no effect. |
| `maxDistUm` | `200` | Largest accepted distance (µm) from the nearest site; also the upper bound of `z_s`. |
| `nBootstrap` | `0` | Passed to the helper but not implemented: no uncertainty estimate or `.loc.std.N` is written. |
| `overwrite` | `0` | `1`: delete and recompute an existing `session.loc.N`. |
| `probeLibrary` | empty | Extra directory searched first for probe files. |

`nSamples` (default 32) and the channel list come from the group's
`spikeDetection.channelGroups` entry.

## Viewing results

Open `session.loc.N` with NumPy (`np.fromfile(path, '<f4').reshape(-1, 5)`)
and plot `y_um` against `x_um`, or colour by cluster from the `.clu`.

## See also

- [`.loc`](../formats/loc.md) — output format
- [`.probe`](../formats/probe.md) — probe geometry files
- [`ndm_setupgroups`](ndm_setupgroups.md) — writes `probeId`, `shankIndex`, `sitePositions_um`
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
