# Format conversion — Neuralynx, CED/Spike2, Amplipex

Converters that turn raw acquisition files into `.dat`, `.evt`, `.spots`
and `.sts` files. AlphaOmega and Plexon recordings have their own pages:
[`ndm_aom2dat`](ndm_aom2dat.md) and [`ndm_plx2dat`](ndm_plx2dat.md).

**Contents:** [Raw-data layout](#raw-data-layout) · [`ndm_ncs2dat`](#ndm_ncs2dat--neuralynx-ncs--dat) · [`ndm_smr2dat`](#ndm_smr2dat--ced-spike2-smr--dat) · [`ndm_nev2evt`](#ndm_nev2evt--neuralynx-nev--evt) · [`ndm_smr2evt`](#ndm_smr2evt--ced-spike2-smr--evt) · [`ndm_nvt2spots`](#ndm_nvt2spots--neuralynx-nvt--spots--sts) · [`ndm_smi2sts`](#ndm_smi2sts--neuralynx-smi--sts) · [`ndm_tsp2sts`](#ndm_tsp2sts--amplipex-tsp--sts)

## Raw-data layout

All of these scripts take the session YAML (or nothing, to use the current
directory name) as their only argument, and read their parameters from the
session YAML (see [parameters](../parameters.md)). The parameter file must
exist; otherwise the step is skipped (exit 11).

Raw files are looked up in a sibling directory named after the current
directory plus a **suffix** that identifies the acquisition system:

```
../DIR-SUFFIX/SESSION-SUFFIX.<ext>
```

`DIR` is the name of the current directory and `SESSION` the session name
(the same thing for a single-session directory). For example, with
`suffixes: nlx smr`, `ndm_ncs2dat` reads `../DIR-nlx/SESSION-nlx*.ncs` and
`ndm_smr2dat` reads `../DIR-smr/SESSION-smr.smr`. Outputs are written to
the current directory. Per-suffix `.dat` files (`SESSION-nlx.dat`, …) are
then brought to a common rate by [`ndm_resample`](channel-manipulation.md#ndm_resample--resample-per-suffix-dat-files)
and combined into `SESSION.dat` by [`ndm_mergedat`](channel-manipulation.md#ndm_mergedat--merge-per-suffix-dat-files).

Scripts that take `suffixes` accept a space-separated list; a leading `-`
on a suffix is ignored. Scripts that take `suffix` use a single value.

### Neuralynx synchronisation files

The Neuralynx converters (`process_nlxconvert`) use two side files to line
up streams that were recorded with acquisition pauses:

- **`.restart`** — times of "restart acquisition" events. A script uses an
  existing `SESSION-SUFFIX.restart`; if there is none and
  `../DIR-SUFFIX/SESSION-SUFFIX.nev` exists, it reads the `.nev` and writes
  a new `.restart`. With neither, restarts are guessed from gaps in the
  record timestamps (a gap longer than `gap` seconds).
- **`.sync`** — the gaps found in the wideband `.ncs` records, written by
  `ndm_ncs2dat`. The event and video converters use an existing
  `SESSION-SUFFIX.sync`, or derive it from the `.ncs` files; with neither,
  they warn that their output cannot be synchronised with the wideband
  signals.

Run `ndm_ncs2dat` first so that the other Neuralynx converters find both
files.

---

## `ndm_ncs2dat` — Neuralynx `.ncs` → `.dat`

### Synopsis

```bash
ndm_ncs2dat [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX*.ncs` (one file per channel), optionally `SESSION-SUFFIX.restart` or `../DIR-SUFFIX/SESSION-SUFFIX.nev`

**Output:** `SESSION-SUFFIX.dat`, `SESSION-SUFFIX.sync`, and `SESSION-SUFFIX.restart` when it was derived from the `.nev`

### Description

For each suffix, converts all Neuralynx Continuously Sampled channel files
to one interleaved int16 `.dat` and writes the `.sync` file
(`process_nlxconvert -w -s`). Suffixes whose `.dat` already exists are
skipped; if any was skipped the script exits 10 after converting the
others. A suffix with no `.ncs` files stops the script (exit 11).

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffixes` | `nlx` (template) | Mandatory. Suffixes of the directories holding the `.ncs` files. |
| `reverse` | `false` (template) | Mandatory. `true` inverts the signals (Neuralynx records them inverted). |
| `gap` | 1 | Optional. Gap threshold, in whole seconds, for detecting acquisition restarts. |
| `ignoreEvents` | `false` | Optional. `true` ignores `.nev` and `.restart` files and guesses restarts from the timestamps — use when the `.nev` lists start/stop events incorrectly. |

```yaml
- name: ndm_ncs2dat
  parameters:
    - name: reverse
      value: 'false'
      status: Mandatory
    - name: suffixes
      value: nlx
      status: Mandatory
```

---

## `ndm_smr2dat` — CED Spike2 `.smr` → `.dat`

### Synopsis

```bash
ndm_smr2dat [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX.smr`

**Output:** `SESSION-SUFFIX.dat`

### Description

For each suffix, converts the ADC channels of a Spike2 file to an
interleaved int16 `.dat` (`process_smrconvert -d`). Suffixes whose `.dat`
already exists are skipped; if any was skipped the script exits 10 after
converting the others.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffixes` | — | Mandatory. Suffixes of the files to convert. |
| `reverse` | `true` (template) | Mandatory. `true` inverts the signals. |
| `channelsExcluded` | — | Channels to leave out, one entry per suffix, in suffix order. Each entry is a comma-separated list of Spike2 channel numbers, or `-` for none (e.g. `"3,4 -"`). The legacy name `excluded` is also read. The template marks it Mandatory; the script treats it as optional. |

```yaml
- name: ndm_smr2dat
  parameters:
    - name: reverse
      value: 'true'
      status: Mandatory
    - name: suffixes
      status: Mandatory
    - name: channelsExcluded
      status: Mandatory
```

---

## `ndm_nev2evt` — Neuralynx `.nev` → `.evt`

### Synopsis

```bash
ndm_nev2evt [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX.nev`; `SESSION-SUFFIX.restart` and `SESSION-SUFFIX.sync` if present (otherwise derived from the `.nev` and the `.ncs` files)

**Output:** `SESSION-SUFFIX.nlx.evt`

### Description

For each suffix, converts the Neuralynx event records to the text
[`.evt`](../formats/evt.md) format read by NeuroScope, with timestamps
synchronised to the wideband `.dat` (`process_nlxconvert -e`). Suffixes
whose `.nlx.evt` already exists are skipped (exit 10 at the end); a
missing `.nev` gives a warning and the suffix is skipped.

When the `.restart` or `.sync` file has to be generated here, it is named
after the output (`SESSION-SUFFIX.nlx.restart`, `SESSION-SUFFIX.nlx.sync`),
not `SESSION-SUFFIX.restart`, so later steps do not reuse it. Running
`ndm_ncs2dat` first avoids this.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffixes` | `nlx` (template) | Mandatory. Suffixes of the directories holding the `.nev` files. |
| `gap` | 1 | Optional. Gap threshold, in whole seconds, for detecting acquisition restarts. |

---

## `ndm_smr2evt` — CED Spike2 `.smr` → `.evt`

### Synopsis

```bash
ndm_smr2evt [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX.smr`

**Output:** one `SESSION-SUFFIX.eNN.evt` per non-empty Spike2 event channel (`NN` = channel number, zero-padded to two digits; channels ≥ 100 give `SESSION-SUFFIX.NNN.evt`)

### Description

For each suffix, writes the event channels of the Spike2 file as
[`.evt`](../formats/evt.md) files (`process_smrconvert -e`). Existing
event files are overwritten. A missing `.smr` stops the script (exit 11).

If `rename` is set, the script then runs that program with the parameter
file as its only argument, to rename the events and the event files. Two
lab-specific renamers are installed: `process_renameevents_SPWR` and
`process_renameevents_TMaze`.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffixes` | — | Suffixes of the files to convert. The script stops if it is unset, although the template marks it Optional. |
| `rename` | — | Optional. Program to run afterwards to rename events and event files. |

---

## `ndm_nvt2spots` — Neuralynx `.nvt` → `.spots` / `.sts`

### Synopsis

```bash
ndm_nvt2spots [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX.nvt`; `.restart` / `.nev` and `.sync` / `.ncs` as for the other Neuralynx converters

**Output:** `SESSION.spots`, `SESSION.sts`

### Description

Converts the Neuralynx Video Tracker records to LED spots (`.spots`, one
line per detected spot per frame) and video-frame timestamps (`.sts`)
synchronised to the wideband `.dat` (`process_nlxconvert -v`). Skipped
when `SESSION.spots` exists (exit 10). Also run by
[`ndm_extractleds`](video.md#ndm_extractleds--led-positions-from-video-or-tracker-data)
when `useTrackerData` is `true`.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffix` | — | Mandatory. Suffix of the directory holding the `.nvt` file. |
| `gap` | 1 | Optional. Gap threshold, in whole seconds, for detecting acquisition restarts. |
| `ignoreEvents` | `false` | Optional. `true` ignores `.nev` and `.restart` files and guesses restarts from the timestamps. |

---

## `ndm_smi2sts` — Neuralynx `.smi` → `.sts`

### Synopsis

```bash
ndm_smi2sts [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX*.smi`; `.restart` / `.nev` and `.sync` / `.ncs` as for the other Neuralynx converters

**Output:** `SESSION.sts`

### Description

Extracts video-frame timestamps from the subtitle (`.smi`) files that
Neuralynx writes next to its MPEG videos (`process_nlxconvert -v`).
Skipped when `SESSION.sts` exists (exit 10). Neuralynx does not control
MPEG timing, so these timestamps can be off by hundreds of milliseconds;
use them only when no usable `.nvt` file exists. Run automatically by
[`ndm_extractleds`](video.md#ndm_extractleds--led-positions-from-video-or-tracker-data)
when `.smi` files are present.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffix` | — | Mandatory. Suffix of the directory holding the `.smi` files. |
| `gap` | 1 | Optional. Gap threshold, in whole seconds, for detecting acquisition restarts. |

---

## `ndm_tsp2sts` — Amplipex `.tsp` → `.sts`

### Synopsis

```bash
ndm_tsp2sts [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX.tsp`, `../DIR-SUFFIX/SESSION-SUFFIX.meta`

**Output:** `SESSION.sts`

### Description

Converts Amplipex video-frame timestamps to `.sts`. The value of
`TimeStamp of the start of recording` in the `.meta` file is subtracted
from the first column of the `.tsp` file and the result multiplied by
1000. Skipped when `SESSION.sts` exists (exit 10); a missing `.tsp` or
`.meta` skips the step (exit 11). Run automatically by
[`ndm_extractleds`](video.md#ndm_extractleds--led-positions-from-video-or-tracker-data)
when a `.tsp` file is present.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffix` | — | Mandatory. Suffix of the directory holding the `.tsp` and `.meta` files. |

---

## See also

- [Channel manipulation](channel-manipulation.md) — resample and merge the per-suffix `.dat` files
- [Video processing](video.md) — LED extraction from video files
- [`.dat` format](../formats/dat.md) · [`.evt` format](../formats/evt.md)
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
