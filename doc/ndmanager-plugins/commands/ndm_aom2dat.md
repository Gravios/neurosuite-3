# `ndm_aom2dat` — AlphaOmega `.mat` → `.dat`

## Synopsis

```bash
ndm_aom2dat [session.yaml]
```

**Input:** the AlphaOmega `.mat` file named by `matFile` (MATLAB v7.3 / HDF5); `SESSION.yaml` must already exist

**Output:** `SESSION.dat` (and, by design, `acquisitionSystem.nChannels` / `samplingRate` patched in `SESSION.yaml` — see the known issue below)

## Description

Converts an AlphaOmega recording to an interleaved int16 `SESSION.dat`
with `process_aomconvert`. Only the `.dat` is written: channel groups are
not set. Configure them afterwards in the ndManager Probes tab or with
[`ndm_setupgroups`](ndm_setupgroups.md).

Every `CRAW_NNN` dataset in the file becomes one channel, in sorted name
order; the datasets may sit at the root of the file or one group deep (a
MATLAB struct saved with `-v7.3`). The sampling rate is read from
`CRAW_001_KHz`. Samples are copied as stored (int16, no rescaling); when
the channels differ in length, all are cut to the shortest.

The file is read in chunks of `chunkSamples` samples per channel, so peak
memory is about `chunkSamples × nChannels × 2` bytes × 2 (read and
interleave buffers) whatever the recording length. HDF5 file locking is
turned off, so the `.mat` can be read from NTFS/fuseblk and some NFS
mounts.

After the conversion the session YAML is patched:

- `acquisitionSystem.nChannels` is written if unset, or replaced if it
  differs from the channel count in the `.mat`;
- `acquisitionSystem.samplingRate` is written only if unset or `0`.

> **Known issue:** the patch calls `yaml_write`, which `ndm_functions`
> does not define, so the YAML is currently left unchanged ("command not
> found" in the log). Check both values after conversion and set them by
> hand; `process_aomconvert --dry-run file.mat SESSION` prints them.

The step is skipped (exit 10) when `SESSION.dat` exists, and stops with
exit 11 when the parameter file or the `.mat` file is missing.

## Parameters

| Name | Default | Meaning |
|---|---|---|
| `matFile` | — | Mandatory. Path to the `.mat` file, absolute or relative to the session directory. |
| `chunkSamples` | 10000000 | Optional. Samples per channel per read chunk (≈ 610 MB for 32 channels). Reduce on machines with less than 32 GB RAM. |

```yaml
- name: ndm_aom2dat
  parameters:
    - name: matFile
      status: Mandatory
    - name: chunkSamples
      value: 10000000
      status: Optional
```

## Running `process_aomconvert` directly

`ndm_aom2dat` always passes `--no-yaml`. Run on its own,
`process_aomconvert [options] input.mat SESSION` also writes a complete
`SESSION.yaml` (overwriting an existing one) with spike groups and
per-group KiloKlustaKwik settings:

| Option | Meaning |
|---|---|
| `-t`, `--topology SPEC` | Groups as `FIRST-LAST:SIZE,...` (1-based, inclusive channel ranges). `SIZE` 4 → tetrode, 1 → single electrode, other → linear shank. Example: `1-16:16,17-32:4`. |
| `-g`, `--groups N` | Uniform group size when `--topology` is absent (default 8). |
| `-c`, `--chunk N` | Samples per read chunk (default 10000000). |
| `-n`, `--no-yaml` | Write the `.dat` only. |
| `-d`, `--dry-run` | Print the file metadata (channels, samples, sampling rate, duration, µV/count, gain, groups) and exit. |

## See also

- [Format conversion](format-conversion.md) — Neuralynx, CED/Spike2, Amplipex converters
- [`ndm_plx2dat`](ndm_plx2dat.md) — Plexon converter
- [`ndm_setupgroups`](ndm_setupgroups.md) — channel groups from a probe library
- [Pipeline overview](../pipeline.md)
- [First-time spike sort](../../workflows/first-time-sort.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
