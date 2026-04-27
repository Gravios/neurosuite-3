# `ndm_setupgroups` — build channel groups from probe library

## Synopsis

```bash
ndm_setupgroups [session.yaml]
```

**Input:** `probes:` section of session YAML, plus referenced `.probe` files from the probe library
**Output:** in-place edits to session YAML's `anatomicalDescription.channelGroups` and `spikeDetection.channelGroups`

Must be run **before** `ndm_extractspikes{,_sdiff,_stderiv}`. For
AlphaOmega sessions, `ndm_aom2dat` writes the groups directly —
`ndm_setupgroups` is not needed.

## Description

Reads the `probes:` section of the session YAML, loads each referenced
`.probe` file from the probe library, and writes
`anatomicalDescription.channelGroups` and `spikeDetection.channelGroups`
into the session YAML in-place.

Must be run **before** `ndm_extractspikes{,_sdiff,_stderiv}`. For
AlphaOmega sessions, `ndm_aom2dat` writes the groups directly —
`ndm_setupgroups` is not needed.

`probeId`, `shankIndex`, and `sitePositions_um` (inline electrode
geometry) are written into each anatomical and spike group so that
`ndm_estimatedrift` and `ndm_localise` can reconstruct probe geometry
without re-loading the probe file. The `probes[].anatomicalGroups`
field is updated with the assigned group IDs. The canonical probe
key is `probes[].probeId` (older files used `probes[].id`; both are
still read but only `probeId` is written).

**Channel assignment** — when `channelMap.map` is null (all supplied NeuroNexus files):

```
shank i → [offset + i × n_per_shank, …, offset + (i+1) × n_per_shank − 1]
```

**Probe library search order** (lowest → highest priority):

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH  (colon-separated)
SESSION_DIR/probes/
```

```yaml
- name: ndm_setupgroups
  parameters:
  - {name: nSamples,        value: 52,    status: Optional}
  - {name: peakSampleIndex, value: 26,    status: Optional}
  - {name: nFeatures,       value: 3,     status: Optional}
  - {name: overwrite,       value: false, status: Optional}
```

## See also

- [`ndm_aom2dat`](ndm_aom2dat.md) — alternative entry point for AlphaOmega
- [`.probe` format](../formats/probe.md)
- [Spike detection](spike-detection.md) — typical next step

---

*Part of the [ndmanager-plugins](../README.md) reference.*
