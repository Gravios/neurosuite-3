# `ndm_plx2dat` — Plexon `.plx` / `.pl2` → `.dat` + `.yaml` metadata

## Synopsis

```bash
ndm_plx2dat [session.yaml]
```

**Input:** `plexonFile` (a Plexon `.plx` or `.pl2` recording — OmniPlex or MAP)

**Output:** `session.dat`, `session.plexon.yaml`; the session `.yaml` is updated in place

## Description

Exports one continuous stream of a Plexon recording — by default the
wideband `WB` channels — as a neurosuite `.dat`, and records everything
readable from the Plexon headers in the session YAML. The container is
detected from the file signature (`PLEX` at byte 0, or `0xFE` + `PLEXON` at
byte 10 for PL2), so the same step handles both.

Two programs do the work:

- **`process_plxconvert`** (C++, no dependencies) parses the file, writes
  `session.dat` and the metadata file `session.plexon.yaml`.
- **`process_plxyaml.py`** merges that metadata into the session YAML. With
  `ruamel.yaml` installed the YAML keeps its comments and layout; otherwise
  PyYAML rewrites it and the step says so.

### The `.dat`

Raw int16 counts exactly as Plexon stored them — no rescaling — interleaved,
sample-major. The µV value of a count is `sourceRecording.export.channels[].uvPerCount`
(PLX: Plexon's per-version gain formulas; PL2: the channel header's units per
count).

The timeline is rebuilt from the block timestamps: each block lands at
`round((timestamp − t0) / ticksPerSample)`. A paused recording therefore
keeps its true time base — the pause is zero-filled and listed under
`sourceRecording.export.gaps` — and `.dat` sample *n* corresponds to Plexon
tick `t0 + n × ticksPerSample`, so Plexon's spike and event timestamps map
straight onto the `.dat`. `t0` is the first sample of the exported stream
(`firstSampleTimestamp_ticks`), or 0 with `padToZero: true`.

Channels of the stream that recorded nothing are kept as all-zero channels by
default, so `.dat` channel *k* stays Plexon channel *k+1* (`WB01` → 0) for
probe mapping; `keepEmpty: false` drops them.

### What goes into the YAML

| Block | Contents |
|---|---|
| `acquisitionSystem` | `nBits`, `nChannels`, `samplingRate`, `voltageRange` / `amplification` (chosen so `voltageRange·10⁶ / (2¹⁶·amplification)` = µV per count), `offset`, plus `bitResolution_uV`, `nSamples`, `duration_s`. Each change is printed. |
| `generalInfo` | `date` from the recording date and `description` from the Plexon file comment — **only when still empty**. |
| `sourceRecording` | `header` (version / application, recording date, clock, channel-header counts, PL2 offsets and record counts), `export` (stream, time origin, segments and gaps, the per-channel table: name, source/channel, rate, µV/count, enabled, has-data, device, electrode type), `streams` (every continuous stream with channel counts, rates and scales), `spikeChannels` (threshold in counts and µV, waveform length, pre-threshold samples, per-unit spike counts), `eventChannels` (event counts), `deviceSettings` (OmniPlex `<Settings>` values such as filter cut-offs and poles, PL2 only), `deviceText` and per-channel `headerText` (other readable text in the PL2 headers and settings region — device chain, electrode and input types), `findings` (every inconsistency found). |

The same `sourceRecording` block stays in `session.plexon.yaml`. Keep that
file: ndmanager rewrites the session YAML from its own model when you save,
which drops blocks it does not know, `sourceRecording` included.

Anatomical and spike-detection groups are **not** written — use
[`ndm_setupgroups`](ndm_setupgroups.md) or the Probes tab in ndmanager after
conversion.

## Parameters

```yaml
- name: ndm_plx2dat
  parameters:
  - {name: plexonFile,   value: recording.pl2, status: Mandatory}
  - {name: stream,       value: WB,            status: Optional}
  - {name: channels,     value: 1-32,          status: Optional}
  - {name: keepEmpty,    value: 'true',        status: Optional}
  - {name: padToZero,    value: 'false',       status: Optional}
  - {name: chunkSamples, value: ~,             status: Optional}
```

| Parameter | Default | Meaning |
|---|---|---|
| `plexonFile` | — | Path to the `.plx` / `.pl2`, absolute or relative to the session directory. |
| `stream` | `WB`, else the fastest stream with data | Channel-name prefix of the stream: `WB` (wideband), `SPKC` (spike-filtered continuous), `FP` (field potentials), `AI` (auxiliary analog), … All channels of the stream must share one sampling rate. |
| `channels` | all | Subset and order within the stream: 1-based indices and ranges and/or names, e.g. `1-16,18` or `WB01,WB05`. |
| `keepEmpty` | `true` | Keep stream channels without data as zero channels. |
| `padToZero` | `false` | Start the `.dat` at tick 0 instead of at the first sample. |
| `chunkSamples` | 2²¹ / nChannels | Output samples per write chunk (≈ 4 MB). Much larger chunks are measurably slower. |

`process_plxconvert --dry-run recording.pl2` prints the streams, channel
counts, scales and spike/event totals without writing anything — the quick
way to choose `stream` and `channels`.

## Validation and format provenance

Plexon publishes neither container as an open specification.

- **PLX** follows the header structures of Plexon's `Plexon.h` as read by
  python-neo's `PlexonRawIO`.
- **PL2** follows the clean-room format notes of the
  [OpenReadout](https://github.com/openreadout/openreadout) project
  (`docs/formats/plexon.md`), derived from hex dumps of public files;
  Plexon's own PL2 reader is a Windows DLL. PL2 support is therefore checked
  for internal consistency on every run — the record walk must end on the
  footer, the per-unit spike counts in the channel headers must equal the
  spike records, the end-of-recording record must agree with the file
  header, channels of one stream must share a block layout, blocks must sit
  on the sample grid — and each failed check is reported in
  `sourceRecording.findings` rather than ignored.

When a recording exists in both containers, converting each and comparing
the two `.dat` files (`cmp`) is an independent check on a real file.

The test `src/ndmanager-plugins/test/test_plx2dat.py` writes synthetic PLX
and PL2 files with its own encoder, converts them, and checks the samples,
gap placement, PLX/PL2 byte identity, options and metadata, plus negative
controls (truncated files, a wrong header record type, mixed rates,
off-grid blocks, header/record spike-count mismatch, a non-Plexon file).

## See also

- [`ndm_aom2dat`](ndm_aom2dat.md) — AlphaOmega equivalent
- [Format conversion](format-conversion.md) — Neuralynx, CED/Spike2, Amplipex
- [`ndm_setupgroups`](ndm_setupgroups.md) — build channel groups after conversion
- [`.dat` format](../formats/dat.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
