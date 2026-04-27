# `ndm_aom2dat` — AlphaOmega `.mat` → `.dat` + `.yaml`

## Synopsis

```bash
ndm_aom2dat [session.yaml]
```

**Input:** `matFile` (path to AlphaOmega `.mat` file, HDF5 v7.3)
**Output:** `session.dat`, `session.yaml`

## Description

Converts an AlphaOmega `.mat` recording to the neurosuite `.dat`
binary and generates a complete session YAML. The YAML includes
calibrated per-group KlustaKwik parameters (`MergeThresh`,
`MaxClusters`, merge iterations) for each spike group, computed from
the probe topology — the same calibration used by
[`ndm_klustakwik`](ndm_klustakwik.md).

`process_aomconvert` streams the HDF5 data in configurable chunks;
peak RAM is `chunkSamples × nChannels × 2` bytes regardless of
recording length.

HDF5 file locking is disabled at `main()` entry
(`setenv("HDF5_USE_FILE_LOCKING", "FALSE")`) so the binary works
cleanly on NTFS / fuseblk mounts where the default locking mode
fails.

## Parameters

```yaml
- name: ndm_aom2dat
  parameters:
  - {name: matFile,      value: recording.mat,   status: Mandatory}
  - {name: topology,     value: 1-16:16,17-32:4, status: Optional}
  - {name: groups,       value: 8,               status: Optional}
  - {name: chunkSamples, value: 10000000,        status: Optional}
```

The `topology` parameter defines mixed probe configurations. Format:
`FIRST-LAST:SIZE,...` (1-based channel numbers, inclusive). Group
type is inferred from `SIZE`: `4` → tetrode, `1` → single electrode,
any other value → linear/silicon shank.

When `topology` is absent, channels are split uniformly into groups
of `groups` channels. `chunkSamples` defaults to 10,000,000
(≈ 610 MB for 32 channels — tuned for 64+ GB RAM).

## See also

- [Format conversion](format-conversion.md) — Neuralynx, CED/Spike2,
  Amplipex equivalents
- [`ndm_setupgroups`](ndm_setupgroups.md) — alternative path: build
  channel groups from a probe library when topology can't be
  inferred from `.mat` headers
- [Pipeline overview](../pipeline.md) — full AlphaOmega pipeline
- [First-time spike sort](../../workflows/first-time-sort.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
