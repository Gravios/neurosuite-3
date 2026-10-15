# Channel and data manipulation

Scripts that resample, merge, select, reorder and concatenate `.dat`
files, plus a legacy colour editor for XML parameter files.

**Contents:** [`ndm_resample`](#ndm_resample--resample-per-suffix-dat-files) · [`ndm_mergedat`](#ndm_mergedat--merge-per-suffix-dat-files) · [`ndm_extractchannels`](#ndm_extractchannels--keep-a-subset-of-channels) · [`ndm_reorderchannels`](#ndm_reorderchannels--permute-channels) · [`ndm_recolorchannels`](#ndm_recolorchannels--channel-colours-in-an-xml-file) · [`ndm_concatenate`](#ndm_concatenate--concatenate-the-sessions-of-a-directory)

Except for `ndm_recolorchannels`, each takes the session YAML (or nothing,
to use the current directory name) as its only argument and reads its
parameters from the session YAML (see [parameters](../parameters.md)).
A missing parameter file skips the step (exit 11). For a multi-system
recording the order is: converters ([format conversion](format-conversion.md))
→ `ndm_resample` → `ndm_mergedat` → `ndm_extractchannels`.

`ndm_resample`, `ndm_extractchannels` and `ndm_reorderchannels` rewrite
their input in place and do not check whether they have already run:
running one twice applies it twice.

---

## `ndm_resample` — resample per-suffix `.dat` files

### Synopsis

```bash
ndm_resample [session.yaml]
```

**Input:** `SESSION-SUFFIX.dat` for each suffix

**Output:** the same files, resampled in place

### Description

Resamples each per-suffix `.dat` written by a converter from its
hardware sampling rate to `acquisitionSystem.samplingRate` (the rate of
the final `SESSION.dat`), with `process_resample -n <nChannels> -f <rate>,<target>`
(libsamplerate, fastest sinc converter, one resampler per channel,
OpenMP-parallel across channels).

If `suffixes`, `nChannels` and `samplingRates` are all unset, nothing is
done (exit 0). Otherwise all three are required and must have the same
number of entries; `acquisitionSystem.samplingRate` is also required. A
missing `SESSION-SUFFIX.dat` skips the step (exit 11).

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffixes` | — | Suffixes of the files to resample. |
| `samplingRates` | — | Original sampling rate of each file, one per suffix. |
| `nChannels` | — | Channel count of each file, one per suffix. |

Example for a Neuralynx + Spike2 recording (the template leaves the
values empty):

```yaml
- name: ndm_resample
  parameters:
    - name: suffixes
      value: nlx smr
      status: Mandatory
    - name: samplingRates
      value: 32556 25000
      status: Mandatory
    - name: nChannels
      value: 64 8
      status: Mandatory
```

---

## `ndm_mergedat` — merge per-suffix `.dat` files

### Synopsis

```bash
ndm_mergedat [session.yaml]
```

**Input:** `SESSION-SUFFIX.dat` for each suffix

**Output:** `SESSION.dat` (the inputs are deleted)

### Description

Interleaves the channels of simultaneously recorded `.dat` files into one
`SESSION.dat`: the channels of the first suffix come first, then those of
the second, and so on (`process_merge -t`). Files of different lengths
are truncated to the shortest. The inputs are deleted after a successful
merge. Skipped when `SESSION.dat` exists (exit 10).

- With a single suffix, `SESSION-SUFFIX.dat` is renamed to `SESSION.dat`.
- With `suffixes` and `nChannels` both unset, a single `SESSION-*.dat` is
  renamed to `SESSION.dat`; more than one such file is an error.
- `nChannels` must have one entry per suffix.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffixes` | — | Suffixes of the files to merge, in output channel order. |
| `nChannels` | — | Channel count of each file, one per suffix. |

---

## `ndm_extractchannels` — keep a subset of channels

### Synopsis

```bash
ndm_extractchannels [session.yaml]
```

**Input:** `SESSION.dat`

**Output:** `SESSION.dat`, rewritten in place

### Description

Writes only the listed channels, in the listed order
(`process_extractchannels`). If `nChannels` and `channels` are both unset,
nothing is done (exit 0); otherwise both are required. Update
`acquisitionSystem.nChannels` to the new count afterwards.

Besides plain channel numbers, `process_extractchannels` accepts two
forms in the list:

- `5*1.5` — channel 5 multiplied by a gain of 1.5;
- `5-2` — channel 5 minus channel 2 (re-referencing).

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `nChannels` | — | Number of channels in `SESSION.dat` before extraction. |
| `channels` | — | Space-separated list of channels to keep, numbered from 0. |

---

## `ndm_reorderchannels` — permute channels

### Synopsis

```bash
ndm_reorderchannels [session.yaml]
```

**Input:** `SESSION.dat`

**Output:** `SESSION.dat`, rewritten in place

### Description

Rewrites `SESSION.dat` with its channels in the listed order
(`process_extractchannels`). The list must contain exactly
`acquisitionSystem.nChannels` entries (an error otherwise); if `channels`
is unset, nothing is done (exit 0). Use it to bring the acquisition
channel order into probe order before
[`ndm_setupgroups`](ndm_setupgroups.md) when the two differ.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `channels` | — | Space-separated list of all channel indices (0-based), in the new order. |

`acquisitionSystem.nChannels` is read from the session YAML and is
required.

---

## `ndm_recolorchannels` — channel colours in an XML file

### Synopsis

```bash
ndm_recolorchannels -c FIRST[-LAST],RRGGBB [-c ...] SESSION.xml
```

**Input / output:** `SESSION.xml`, edited in place

### Description

Sets the NeuroScope display colour of a range of channels (0-based,
inclusive) to a hexadecimal colour without `#`. It works on legacy XML
parameter files only and takes no parameters from the `programs:`
section. For a YAML session, edit the NeuroScope channel colours in
the YAML or in ndManager. Display only — it has no effect on any
processing.

```bash
# tetrode 1 red, tetrode 2 green, channel 17 blue
ndm_recolorchannels -c 0-3,ff0000 -c 4-7,00ff00 -c 17,0000ff session.xml
```

---

## `ndm_concatenate` — concatenate the sessions of a directory

### Synopsis

```bash
ndm_concatenate [DIR.yaml]
```

**Input:** every `*.dat` in the current directory except `DIR.dat`, the `*.evt` files of each, and `SESSION.spots` for the duration check

**Output:** `DIR.dat`, `DIR.cat.evt`, `DIR.all.evt`

### Description

Joins the sessions recorded in one directory (`DIR` is the name given on
the command line, by default the directory name). Files are taken in
`ls` order. For each session `SESSION.dat`:

- **`DIR.dat`** — the `.dat` files appended in order. With a single
  session the file is moved to `DIR.dat` and `SESSION.dat` becomes a
  symbolic link to it.
- **`DIR.cat.evt`** — two events per session, `beginning of SESSION` and
  `end of SESSION`, in milliseconds from the start of `DIR.dat`.
- **`DIR.all.evt`** — the events of every `SESSION*.evt`, shifted by the
  session start. Events at or after the session end are dropped, with a
  warning.

Each output is skipped if it already exists (`DIR.all.evt` is skipped
when any `DIR.*.evt` other than `DIR.cat.evt` exists). `.pos` files are
not concatenated.

Durations are computed with `duration` from `acquisitionSystem.nChannels`,
`samplingRate` and `nBits` (all required). When `spotsSamplingRate` is
set, the duration of `SESSION.spots` is compared with that of the
wideband file and a warning is printed if they differ by more than
`maxShift` seconds.

Keep only per-session `.dat` files in the directory when running it: any
other `.dat` (for example `SESSION-emgclean.dat`) is concatenated too.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `spotsSamplingRate` | `50` (template) | Sampling rate of the `.spots` files (video or tracker frame rate). If unset, the duration check is skipped with a warning; concatenation proceeds. The template marks it Mandatory. |
| `maxShift` | 3 | Optional. Wideband/video duration difference, in seconds, above which a warning is printed. |

```yaml
- name: ndm_concatenate
  parameters:
    - name: spotsSamplingRate
      value: 50
      status: Mandatory
```

---

## See also

- [Format conversion](format-conversion.md) — the converters that write the per-suffix `.dat` files
- [`ndm_setupgroups`](ndm_setupgroups.md) — channel groups from a probe library
- [`.dat` format](../formats/dat.md) · [`.evt` format](../formats/evt.md)
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
