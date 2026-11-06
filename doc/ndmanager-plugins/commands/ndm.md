# `ndm` — one command for the steps

## Synopsis

```bash
ndm <step> [arguments]            # run ndm_<step>: ndm detectspikes = ndm_detectspikes
ndm configure [session]           # open the session in ndmanager
ndm list probes [session]         # probes: entries, their files, the groups on them
ndm list spikegroups [session]    # spike groups: channels, window, site positions
ndm list programs [session]       # the steps under programs:, with the values set
ndm list steps                    # every step, by stage
ndm help [step]                   # the overview, or the step's own help
```

`[session]` is `SESSION.yaml` (the extension may be left out) or the session
folder. Without it, the session is the one named after the current folder, as
for every step: in `rat01-20261015/` it is `rat01-20261015.yaml`.

## Description

`ndm` is a front end to the `ndm_*` steps and to ndmanager; it changes nothing
itself.

- **`ndm <step> [arguments]`** runs `ndm_<step>` with the arguments unchanged,
  so `ndm pca 3 --method stderiv` is `ndm_pca 3 --method stderiv`. The full name
  works too (`ndm ndm_pca`). The step is looked up beside `ndm` first (where an
  install puts them), then on `PATH`. A step that does not exist is refused
  with the closest names ("did you mean: detectspikes …"), exit status 2. The
  deprecated aliases (`extractspikes_sdiff`, `extractspikes_stderiv`,
  `pca_stderiv`) still run but are not listed.
- **`ndm configure [session]`** starts `ndmanager` with the session; without one,
  ndmanager opens the current folder's session itself.
- **`ndm help`** (or `ndm` alone) prints the usage and every step by stage, with
  one line each. **`ndm help <step>`** prints the step's own `--help`.
- **`ndm list steps`** prints the same step table.

### `ndm list probes`

One row per `probes:` entry: its id, `probeFile`, `channelOffset`, where the
file was found, vendor and model, shanks, sites, layout, substrate, and the
spike groups on it (`group/shank`, or the entry's `anatomicalGroups` when no
group names a `probeId`). Probe files are found as
[`ndm_setupgroups`](ndm_setupgroups.md) finds them: the session folder,
`probes/` in it, `probeLibraryPath`, `$NEUROSUITE_PROBE_PATH`, then the data
directories. The notes column flags:

- `not found`: no such file on that search path;
- `estimated <date>`: written by [`ndm_estimategeometry`](ndm_estimategeometry.md);
- `template`: a wire-bundle layout drawn, not measured;
- `geometry_backup`: the file keeps an earlier layout.

### `ndm list spikegroups`

One row per spike group: channels (as ranges, e.g. `16-18,20`), how many,
`nSamples`, `peakSampleIndex`, `nFeatures`, `probeId/shankIndex`, where the
site positions come from, and the skipped channels. The positions come from
`sitePositions_um` in the group, the probe file (marked estimated or template),
or nowhere (`none`, or why the probe file did not give them).

### `ndm list programs`

Every step under `programs:`, how many of its parameters have a value, and
those values (long ones cut at 70 characters).

### `ndm list groupids`

One line per spike group: its number, a tab, and its channels as ranges
(`16-18,20`). For scripts and the tab completion. The numbers are the ones the steps take.

## Tab completion

In bash, Tab completes:

| After | Completes |
|---|---|
| `ndm ` | `help`, `configure`, `list` and every step |
| `ndm list ` / `ndm help ` | the `list` targets / the steps |
| `ndm <step> -` | the step's options, and `--help` |
| `ndm <step> --method ` | the option's values: `standard sdiff stderiv`; `--methodOrder` 0–3, `--layout` `bundle free` |
| `ndm <step> --inputExtension ` | the extensions of the session's files (`dat`, `fil`, `lfp`, …) |
| `ndm <step> ` | session files; for a step that takes a spike group, the session's group numbers too |
| `ndm pca rat01 ` | the group numbers of `rat01.yaml` |

The steps that take a group are those whose usage line names one:
`alignspikes_pca`, `applydrift` (a source group, then any number of
targets), `autoklusta`, `denoiseuniform`, `estimategeometry`, `klustakwik`,
`localise`, `pca`, `pca_multichannel`, `refeaturize`, `spikecleaner` and
`subcluster_unmatched`. The group numbers come from the session named on the
command line, else the folder's own; nothing is offered without one.

The options are read from the usage line each step prints on a wrong command
line, so a new step or option completes without a change to the completion
script.

## Files

- `ndm` (bash) and `ndm_list.py` (Python 3 with `pyyaml`; it uses
  `probe_geometry.py` and `process_setupgroups.py`), installed with the steps.
- Tab completion: `share/bash-completion/completions/ndm` (see
  [Tab completion](#tab-completion)). bash-completion loads it by name the
  first time `ndm` is completed; without bash-completion, `source` it from
  `~/.bashrc`. It completes `ndm`, not the `ndm_*` names.

## See also

- [Command reference](README.md) — every step
- [`ndm_start`](ndm_start.md) — run a whole pipeline

---

*Part of the [ndmanager-plugins](../README.md) reference.*
