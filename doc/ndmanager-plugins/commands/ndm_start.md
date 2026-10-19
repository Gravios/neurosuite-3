# `ndm_start` — batch pipeline orchestrator (template → per-directory runs)

## Synopsis

```bash
ndm_start [template.yaml] [base1 base2 ...]
NDM_PIPELINE=<name> ndm_start [template.yaml] [base1 ...]
```

**Input:** a template parameter file (`.yaml`, `.yml` or legacy `.xml`) in the
current directory, the data directories `<base>-<suffix>/` beside it, and
optionally a pipeline file `<template>.ndm.<name>.pipeline`

**Output:** one directory `<base>/` per recording day holding `<base>.yaml`,
one `<session>.yaml` per session, every file the dispatched plugins write, and
(with `log`) a log file `<base>-YYYY-MM-DD-HH:MM:SS.log` in the current
directory

## Description

`ndm_start` runs a list of `ndm_*` plugins over every recording directory under
the current directory. It does no processing itself: it prepares each
directory, decides which plugins to run and in which order, and calls them one
after another.

Run it from the directory that holds the template and the data directories.
Only the base name of the first argument is used (`.xml`, `.yaml`, `.yml` are
stripped), and the parameter file is looked up in the current directory as
`<name>.yaml`, then `.yml`, then `.xml`. Without arguments the template is
named after the current directory. If no parameter file is found, `ndm_start`
exits with 11.

### Directory layout

Data directories are named `<prefix>-YYYYMMDD[-description]-<suffix>`, one
per acquisition system recording in parallel; the files inside are named
`<prefix>-YYYYMMDD-nn-<description>-<suffix>[-NNN].<ext>`. The suffixes come
from the `suffixes` parameter (a leading `-` is ignored).

```
RadialMaze.yaml                          template
RadialMaze-Rat-15-20090718-wideband/     data for base RadialMaze-Rat-15-20090718
RadialMaze-Rat-15-20090718-filtered/
RadialMaze-Rat-15-20090719-wideband/
...
```

The bases to process are the names given on the command line, or, without
them, every `*-<suffix>` entry for the **first** suffix with that suffix
removed. If there are none, `ndm_start` stops with an error (exit 1).

For each base `B` (a name that exists as a regular file is skipped with a
warning):

1. Create `B/` if needed, copy the template into it and `cd` there.
2. Write the date found in `B` (`YYYYMMDD` or `YYYY-MM-DD`) into the `date:`
   field (`<date>` in XML); warn if the name holds no date.
3. Rename the copy to `B.yaml`. If `B.yaml` already exists and differs, warn
   and ask (`mv -i`) before replacing it.
4. List the sessions: the file names in every `../B-<suffix>/`, cut before
   `-<suffix>`. For each session `S`, copy `B.yaml` to `S.yaml`; if `S.yaml`
   exists and differs, warn and ask (`cp -i`).
5. Run the plugins (see [Modes](#modes)), then return to the parent directory.

Edit the template, not `B.yaml`: a re-run offers to overwrite `B.yaml` with the
template. Answer `n` to keep changes made inside `B/` — including the channel
groups that [`ndm_setupgroups`](ndm_setupgroups.md) writes into `B.yaml`.

### Modes

The source of the plugin list is chosen once, before the directory loop, in
this order:

| # | Mode | Used when | Plugin order |
|---|---|---|---|
| 1 | Pipeline file | `<template>.ndm.<name>.pipeline` exists in the current directory and lists at least one plugin | Topological sort of the file's graph |
| 2 | In-YAML graph | `programs[0]` of the template is `ndm_start` (for an `.xml` template: any `<program>` is named `ndm_start`) | Order of `programs:` |
| 3 | Legacy flags | neither of the above | Fixed sequence gated by the flag parameters |

The chosen mode is printed (`ndm_start: pipeline file '…'`, `in-YAML graph
mode`, or `legacy flag mode`).

**1 — Pipeline file.** `<name>` is `$NDM_PIPELINE`, or `default` when the
variable is unset. The file is the one ndmanager's Pipeline tab writes with
**Save Pipeline** / **Save Pipeline As…** (see
[ndmanager → Pipeline tab](../../ndmanager/README.md#pipeline-tab--editable-node-graph)):

```yaml
nodes:
  - {id: n1, type: ndm_start,  enabled: true, pos: [60, 80],  params: {...}}
  - {id: n2, type: ndm_hipass, enabled: true, pos: [320, 80], params: {...}}
edges:
  - {from: n1, to: n2}
```

Nodes are sorted with Kahn's algorithm; ties keep the file order. Nodes with
`enabled: false` are dropped, and so is the `ndm_start` node. Only `type` and
`enabled` are used: the node `params` in the file are **not** passed to the
plugins, which read their parameters from the session YAML as always. Edges to
unknown ids are ignored, and nodes on a cycle never reach in-degree 0 and are
left out without a message. If the file parses to an empty list, `ndm_start`
warns and falls back to mode 2/3. When `NDM_PIPELINE` names a file that does
not exist, `ndm_start` stops with an error instead of running another
pipeline; a missing `default` file just selects mode 2/3.

```bash
ndm_start RadialMaze.yaml                          # RadialMaze.ndm.default.pipeline
NDM_PIPELINE=best ndm_start RadialMaze.yaml        # RadialMaze.ndm.best.pipeline
```

**2 — In-YAML graph.** Every `programs:` entry after `ndm_start` is run, in
file order (with XML, every `<program>` except `ndm_start`). There is no
`enabled` field here: an entry in the list is run. `templates/template.yaml`
starts with `ndm_start` and lists every plugin, so a session copied from it
unchanged runs all of them; trim `programs:` or use a pipeline file. A
`general` entry is not a plugin and is skipped with a "not found on PATH"
warning.

In modes 1 and 2, each plugin is classed as per-session or per-directory:

- **Per-session** — `ndm_ncs2dat`, `ndm_smr2dat`, `ndm_aom2dat`,
  `ndm_smr2evt`, `ndm_nev2evt`, `ndm_extractleds`, `ndm_transcodevideo`,
  `ndm_extractchannels`, `ndm_mergedat`, `ndm_resample`. Run as `plugin S`
  once per session, in graph order, inside the session loop (step 4).
- **Per-directory** — everything else. Run as `plugin B` once per directory
  after all sessions. A plugin that is not on `PATH` is skipped with a warning.

The flag parameters do not gate anything in these modes; only `suffixes` and
`log` are used.

**3 — Legacy flags.** The fixed sequence of the original `ndm_start`:

| Flag | Steps |
|---|---|
| `wideband` | per session: `ndm_resample` for each `B-<suffix>.dat` present, then `ndm_mergedat`, `ndm_extractchannels` |
| `events` | per session: `ndm_smr2evt` / `ndm_nev2evt` for each `B-<suffix>.smr` / `.nev` present |
| `video` | per session: `ndm_transcodevideo`, `ndm_extractleds` |
| `concatenation` | `ndm_concatenate`, when `.dat` files other than `B.dat` exist |
| `spikes` | `ndm_hipass`, `ndm_detectspikes`, `ndm_extractspikes`, `ndm_pca` |
| `lfp` | `ndm_lfp` |
| `clean` | [`ndm_clean`](ndm_clean.md) |

In this mode the per-session steps are called with the directory base `B`, not
the session name `S`, and no format conversion (`ndm_smr2dat`,
`ndm_ncs2dat`) is run; for per-session work, use mode 1 or 2.

### Failures and re-runs

Each plugin is judged with `step_completed`: exit 0, 10 (outputs exist) and 11
(inputs missing) count as success, anything else as failure.

- **Modes 1 and 2:** a failure prints `'<plugin>' failed for '<B or S>';
  continuing` and the next plugin runs. A failed `ndm_pca` does not stop
  `ndm_lfp`, but later steps that need its output will skip with exit 11.
- **Mode 3:** a failure ends the current flag's block (e.g. a failed
  `ndm_detectspikes` skips `ndm_extractspikes` and `ndm_pca`); the next block
  still runs.

`ndm_start` is safe to re-run. Every plugin checks for its outputs first and
exits 10 if they exist, so a second run skips what is done and resumes at the
first step whose outputs are missing. To redo a step, delete its outputs (or
use that plugin's `overwrite` / `--force` option where it has one). The exit
status of `ndm_start` itself is that of its last command, not a summary of the
plugins; read the log.

### Logging

With `log: true`, all output of each directory (stdout and stderr) is copied
with `tee` to `B-YYYY-MM-DD-HH:MM:SS.log` in the current directory. The file
holds terminal colour codes; read it with `less -R` or `cat`.

## Parameters

Set in the template's `ndm_start` program block (see
[parameters](../parameters.md)). In a pipeline file, the `ndm_start` node's
`params` are not read.

| Name | Default | Meaning |
|---|---|---|
| `suffixes` | — | Space-separated directory suffixes, e.g. `wideband filtered`. The first one finds the bases. Mandatory while `wideband` or `events` is true (the default); needed in every mode to list sessions and find bases. |
| `log` | `true` | Copy each directory's output to a log file. The template sets `false`. |
| `wideband` | `true` | Mode 3: resample, merge and extract channels. |
| `events` | `true` | Mode 3: convert `.smr` / `.nev` events. |
| `video` | `true` | Mode 3: transcode video, extract LEDs. |
| `concatenation` | `true` | Mode 3: concatenate the day's sessions. |
| `spikes` | `true` | Mode 3: high-pass, detect, extract, PCA. In every mode, `true` with no `spikeDetection.channelGroups` prints a warning. |
| `lfp` | `true` | Mode 3: create the `.lfp`. |
| `clean` | `false` | Mode 3: run `ndm_clean`. |

```yaml
- name: ndm_start
  parameters:
    - name: suffixes
      value: nlx
      status: Mandatory
    - name: log
      value: 'false'
      status: Mandatory
```

**Environment:** `NDM_PIPELINE` selects the pipeline file
`<template>.ndm.$NDM_PIPELINE.pipeline` (default `default`).

## See also

- [Pipeline overview](../pipeline.md) — what the plugins do, in order
- [ndmanager → Pipeline tab](../../ndmanager/README.md#pipeline-tab--editable-node-graph) — building and saving pipeline files
- [Design: `ndm_start` as the pipeline root](../../design/ndm-start-root.md)
- [`ndm_functions`](ndm_functions.md) — `step_completed`, exit codes, session listing

---

*Part of the [ndmanager-plugins](../README.md) reference.*
