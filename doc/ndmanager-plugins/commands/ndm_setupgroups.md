# `ndm_setupgroups` — build channel groups from probe library (`probes:` → `channelGroups`)

## Synopsis

```bash
ndm_setupgroups [session.yaml]
```

**Input:** the `probes:` section of the session YAML and the `.probe` files it
names (see [`.probe`](../formats/probe.md))

**Output:** the session YAML, rewritten in place:
`anatomicalDescription.channelGroups`, `spikeDetection.channelGroups` and
`probes[].anatomicalGroups`

## Description

Creates one anatomical group and one spike group per shank of every probe
listed under `probes:`, numbered from 1 in `probes:` order. Run it before
spike detection. AlphaOmega sessions do not need it: `ndm_aom2dat` writes the
groups itself.

For each `probes:` entry:

- `probeFile` is a path relative to the probe library (e.g.
  `neuronexus/Buzsaki64.probe`). An entry without `probeFile` is skipped with
  a warning; a file that cannot be found is an error.
- `probeId` is the probe's id (the legacy key `id` is still read; it is
  rewritten as `probeId`).
- `anatomicalGroups` is replaced with the ids of the groups created for it.

Each **anatomical group** gets `channels` (`{id, skip: 0}`), `probeId` and
`shankIndex` (0-based shank within the probe). Each **spike group** gets the
same channels, in the same order, plus `nSamples`, `peakSampleIndex`,
`nFeatures`, `probeId`, `shankIndex` and `sitePositions_um` — one
`[x_um, y_um]` per channel (`null` where the probe file has no geometry).
`ndm_estimatedrift`, `ndm_driftcorrect` and `ndm_localise` read
`sitePositions_um` and fall back to the probe file when it is absent.

### Channel binding

Channels are bound to sites by channel number, using the rule shared with the
drift and localisation tools and Klusters (`scripts/probe_geometry.py`):

- Site *j* of the probe file (sites listed shank by shank, top site first)
  belongs to channel `offset + j`, or `offset + channelMap.map[j]` when the
  file has a map (a flat list, or one list per shank). Its position is
  `sites.geometry[j]` either way. A map whose length differs from the number
  of sites is an error.
- `sites.count_per_shank` decides which sites belong to which shank: one
  number (every shank the same) or a list with one entry per shank, e.g.
  `[10, 10, 10, 14, 10, 10]`.
- `offset` is the number of channels of all preceding probes plus this entry's
  `channelOffset` (default 0). For probes recorded on consecutive channels,
  leave `channelOffset` at 0; set it only to skip channels between probes.

With the library files (`map: null`), channel numbers therefore ascend with
depth on every shank: Buzsaki64 shank 0 is channels 0–7 at y = 162 … 22 µm.

Sessions set up before the library listed sites top-first carry old
`sitePositions_um`; run `ndm_setupgroups` again with `overwrite: true` to
refresh them.

### Overwriting

If either `anatomicalDescription.channelGroups` or
`spikeDetection.channelGroups` already has entries, the step stops with an
error unless `overwrite` is true. With `overwrite: true` both lists are
replaced **entirely**: per-group settings added by hand — `klustakwik:`
overrides, `sdiffPairs`, a different `nSamples` — are lost and must be
re-entered. The YAML is written back with PyYAML, so comments and the original
formatting of the file are not preserved.

### Probe library search

A `probeFile` is looked up in these directories; when it exists in several,
the last one wins:

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH          (colon-separated, in order)
probeLibrary                    (parameter below)
<session directory>/probes/
```

## Parameters

See [parameters](../parameters.md) for how values are resolved.

| Name | Default | Meaning |
|---|---|---|
| `nSamples` | 52 | Waveform length written to every spike group (use 32 at 20 kHz). |
| `peakSampleIndex` | 26 | 0-based peak position within the waveform, written to every spike group. |
| `nFeatures` | 3 | PCA components per channel, written to every spike group. |
| `probeLibrary` | — | Extra library directory, searched after `$NEUROSUITE_PROBE_PATH` and before `<session>/probes/`. |
| `overwrite` | `false` | Replace existing groups (`true`, `1` or `yes`); otherwise stop if groups exist. |

```yaml
- name: ndm_setupgroups
  parameters:
    - name: nSamples
      value: 52
      status: Optional
    - name: peakSampleIndex
      value: 26
      status: Optional
    - name: nFeatures
      value: 3
      status: Optional
    - name: probeLibrary
      value: ''
      status: Optional
    - name: overwrite
      value: false
      status: Optional
```

Exit status: 11 when the session has no `.yaml`/`.yml` parameter file; 1 for
any error in the helper (existing groups, no `probes:` entries, probe file not
found or unreadable, no groups generated).

## See also

- [`.probe` format](../formats/probe.md) — geometry and channel order
- [`ndm_aom2dat`](ndm_aom2dat.md) — writes the groups for AlphaOmega sessions
- [Spike detection](spike-detection.md) — the next step
- [`ndm_estimatedrift`](ndm_estimatedrift.md), [`ndm_localise`](ndm_localise.md) — read `sitePositions_um`

---

*Part of the [ndmanager-plugins](../README.md) reference.*
