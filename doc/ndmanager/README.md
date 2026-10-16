# ndmanager — Session Manager

ndmanager is a Qt6 GUI for managing electrophysiology recording sessions. It opens a `.yaml`
session parameter file, provides a tabbed interface for editing all acquisition and processing
parameters, and dispatches the `ndmanager-plugins` preprocessing pipeline — either step by step
or as a full batch.

> **ndmanager reads and writes YAML only.** A legacy `.xml` parameter file no longer opens;
> convert it first with [`ndm_xml2yaml`](../ndmanager-plugins/commands/ndm_xml2yaml.md)
> (`ndm_xml2yaml session.xml` writes `session.yaml`).

**Contents:** [Dependencies](#dependencies) · [Launching](#launching) · [What ndmanager manages](#what-ndmanager-manages) · [The Probes tab](#the-probes-tab) · [Running the preprocessing pipeline](#running-the-preprocessing-pipeline) · [Pipeline tab — editable node graph](#pipeline-tab--editable-node-graph) · [Built-in session templates](#built-in-session-templates) · [Parameter file format](#parameter-file-format) · [Installation](#installation)

---

## Dependencies

As declared in `src/ndmanager/CMakeLists.txt` and `src/ndmanager/src/CMakeLists.txt`:

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 ≥ 6.4 (Core, Gui, Widgets) | UI framework | Yes |
| Qt6 Svg | SVG icons (`ND_HAVE_QT_SVG`) | Optional |
| libklustersshared ≥ 2.1.0 | Shared YAML layer and widget library | Yes — build first |
| yaml-cpp | YAML parameter file reading/writing | Yes |

`libklustersshared` is not available as a distro package and must be built from source before
ndmanager. See the [install guide](install/linux.md).

---

## Launching

```bash
# Open an existing session
ndmanager session.yaml
ndmanager /data/session/          # a directory opens <dir>/<dir>.yaml
ndmanager                         # no argument: <cwd>/<cwd>.yaml if it exists
ndmanager --new                   # an empty Untitled document (what File ▸ New starts
                                  # when a session is already open in this window)
ndmanager --model template.yaml   # a template as the model for a new session; Save asks
                                  # for a name (File ▸ Use Template… with a session open)

# Start a new session from the project template: File ▸ New from Project Template… (Ctrl+Shift+N)

# Crash-safe wrapper — forces software OpenGL to avoid MESA/Zink segfaults
# on Ubuntu 24.04 systems where Vulkan is present but incomplete
scripts/ndmanager-safe session.yaml
```

Installing ndmanager registers the MIME type `application/nphys-yaml` (a `.yaml`/`.yml` file
with an `acquisitionSystem:` key near its top — any other YAML stays plain `application/yaml`)
and the desktop entry opens it, so file managers offer ndmanager for session files.

---

## What ndmanager manages

The `.yaml` parameter file is the central manifest for an entire session. ndmanager provides a
GUI page for every section of that file.

**Acquisition system** — channel count, bit depth (nBits), sampling rate, voltage range,
amplification factor, and ADC offset.

**Electrode groups** — which channels belong to each anatomical and spike-detection group.
Each group carries its waveform window length (`nSamples`), peak sample index, and the number
of PCA features per channel (`nFeatures`). Channels can be color-coded; those colors
propagate to neuroscope's trace display.

**Probes** — which physical probes are connected, what `.probe` configuration file each uses,
its first recording channel (`channelOffset`), and a user label, plus a geometry editor for each
probe. The Probes tab is the GUI entry point for the `probes:` YAML section; it fills the
anatomical and spike groups itself (one per shank), or `ndm_setupgroups` does it from the
command line.

**Field potentials** — LFP sampling rate.

**Video** — frame rate, position-tracking sampling rate, and LED detection threshold. Video
dimensions (width/height) are stored in the top-level `video` YAML section and are separate
from the neuroscope-specific rotation and flip settings.

**Plugin parameters** — one page per installed `ndm_*` script, listed under **Plugins** in
collapsible branches by purpose (Conversion, Preparation, Detection, Sorting, …; see
[Plugins by purpose](#plugins-by-purpose)). Each page exposes the
parameters that script reads from the parameter file, with Mandatory/Optional/Dynamic status
indicators. Parameters set here land in the `programs[name=...].parameters` section of the
YAML (tier-2 of the three-tier resolution: per-group override → global session override → bash
default). For KiloKlustaKwik specifically, each `spikeDetection.channelGroups[g]` entry may also
carry an embedded `klustakwik:` sub-block with per-group calibrated values (tier-1, highest
priority) — these are typically written by `ndm_aom2dat` from probe topology. See
[ndmanager-plugins](../ndmanager-plugins/README.md) for the complete parameter reference per
script.

### Editing a plugin's parameters

The parameter table edits like a spreadsheet: double-click a cell, press F2, or just start
typing over the selected one. Tab and Shift+Tab move from cell to cell and leave the table
after the last one (or before the first one), so the keyboard never gets stuck in it. In
expert mode, **Add** appends a row with its name ready for typing, **Remove** deletes the
selected rows, and **Move Up** / **Move Down** change the order the description lists the
parameters in. Outside expert mode only the Value column can be edited.

A program description (its `.yaml`, or what the plugin prints for `--ndm-describe`) may give each
parameter three optional keys after `name`, `value` and `status`:

| Key | Meaning |
| --- | --- |
| `type` | `bool`, `int`, `float` or `choice:a\|b\|c` (also `boolean`, `integer`, `double`, `number`, `enum:…`); anything else, or none, is free text |
| `default` | the value the plugin uses when the field is left empty; shown in the Value cell's tooltip |
| `hidden` | `true` hides the row outside expert mode (it is still saved) |

The Value cell's editor follows the type: a list for `bool` (true/false, or 1/0, yes/no, on/off —
whichever spelling the value already uses) and for `choice`, a field that only takes a number for
`int` and `float` (C-locale, `1e-3` included), a plain field otherwise. A value the type does
not take — typed, pasted or set any other way — is put back to its last good value, with the
reason next to the cell. An empty value is always accepted: it means "not set". The three keys
are kept when the table is edited and written back when the description is saved.

A value changed here also reaches the nodes of that plugin in the [Pipeline tab](#pipeline-tab--editable-node-graph),
replacing any inspector edit of the same parameter not yet applied.

### Plugins by purpose

The Plugins entry of the left-hand tree and the Pipeline tab's palette both group plugins into
collapsible branches by purpose. A plugin's branch is, in order of precedence:

1. the `category` its description gives (`program.category` in its `.yaml`, or in what it
   prints for `--ndm-describe`), kept in the session file's `programs:` entry;
2. its category in the built-in table of the plugins this repository ships
   (`src/ndmanager/tools/pipeline_script_defs.py`);
3. **Other**.

Branches follow the built-in table's order (Orchestrator, Conversion, Preparation, Grouping,
Detection, Alignment, Features, Sorting, Post-sort, Analysis, LFP, Output), then any other
category a description names, alphabetically, then Other. Within a branch, plugins keep the
run order. A branch that empties is removed; one you collapse stays collapsed while the list
changes. Clicking a branch shows the Plugins overview page.

```yaml
program:
  name: ndm_myqc
  help: Unit quality metrics.
  category: quality          # shown as "Quality"
  parameters:
    - {name: minRate, value: 0.1, status: Optional, type: float, default: 0.05}
```

---

## The Probes tab

The Probes tab exposes the `probes:` section of the session YAML and is the starting point for
any silicon probe session. It has three panes: the probe list (with **+**, **−** and
**Replace…**), an inspector under it, and the probe's geometry editor on the right.

### Fields

| Inspector field | YAML key | Description |
|---|---|---|
| (list entry) | `probes[].probeId` | Assigned when the probe is added (one more than the highest id). Used by `ndm_setupgroups`, `ndm_estimatedrift`, `ndm_localise` and the groups' `probeId`. Legacy sessions may use `probes[].id`; both are read, only `probeId` is written |
| Label | `probes[].label` | Free text; defaults to the probe file's `model` |
| Probe file | `probes[].probeFile` | Read-only here; set by **+** and **Replace…** (see below) |
| Channel offset | `probes[].channelOffset` | The probe's first recording channel, absolute and 0-based. Follows the previous probe (its offset + its channel count) unless you type a value, which is then kept; typing the automatic value makes it follow again |
| Anatomical groups / Spike groups | `probes[].anatomicalGroups` | Read-only: the groups generated for the probe's shanks |

There is no library field in the tab: `probeLibraryPath` is a top-level key of the session YAML
(edit it in the file). ndmanager writes it back only when the session names one.

### Probe files

`probeFile` is a path. An absolute path is used as is; a relative one is looked up, first match
wins:

```
<session directory>/                  (ndmanager's own copies, below)
<session directory>/probes/
probeLibraryPath                      (top-level key of the session YAML)
$NEUROSUITE_PROBE_PATH                (colon-separated, in order)
$NS3_PROBE_LIBRARY_PATH               (legacy name, colon-separated)
neurosuite/probes/ under each generic data directory:
    $XDG_DATA_HOME (~/.local/share), then $XDG_DATA_DIRS (/usr/local/share, /usr/share)
```

`ndm_setupgroups` searches the same directories in the same order (with its `probeLibrary`
parameter after `<session>/probes/`).

Adding a probe (**+**: the empty template, or *From library…* / *From file…* in its drop-down)
copies the file into the session directory as `<session>.probe.<id>.probe`, where `<session>`
is the session file's name without `.yaml`, and points the entry at that copy. **Replace…** does
the same for the selected probe. Existing files are only overwritten after asking. The session
must have been saved once (so it has a directory). A probe whose file is a library file
(`neuronexus/Buzsaki64.probe`, e.g. from `ndm_setupgroups`) gets its session copy the first time
its geometry is edited.

**−** removes the entry; its session copy is deleted when the session is next saved without it
(not before — closing without saving keeps it). Library files are never deleted. After
**Save As** into another directory the session's own probe files are copied there.

### Probe library

The probe library is a directory tree of `.probe` YAML files installed at
`${CMAKE_INSTALL_DATADIR}/neurosuite/probes/` (typically
`/usr/local/share/neurosuite/probes/`): 43 NeuroNexus models in `neuronexus/` and three general
files in `general/` (`empty.probe`, the template **+** copies, `tetrode.probe` and
`tetrode-array-8.probe`). The format is described in
[`.probe`](../ndmanager-plugins/formats/probe.md).

### Geometry editor

The editor shows the selected probe's connector, shanks and sites as a logical graph and as a
physical layout (µm). Every edit — inspector fields, adding or deleting shanks and sites, undo
and redo, dragging a site or a shank — is written to the probe's session copy at once; **Save**
writes it again, **Save As…** writes the geometry to another `.probe` file (e.g. into a
library) without changing which file the session uses. A site's channel ("Hw channel") is
relative to the probe's channel offset; the file lists each shank's sites top first and
`channelMap.map` is rebuilt from the sites' channels on every save (`null` when they ascend
with depth). "Above tip" is the site's height above its shank's tip, the `y` the file stores;
changing a shank's length keeps every site at its height above the tip. Two sites on one
channel cannot be saved (the status line says why). The file has one `shanks.length_mm`: the
longest shank's length is written.

### Channel groups

Whenever the probes, their offsets or their geometry change the channel layout, the tab rebuilds
the **Anatomical Groups** and **Spike Groups** pages: one group per shank, channels in file
order, plus one anatomical group holding every channel no probe uses. A group whose channels
were already a group keeps its channel order, skip flags and spike parameters (`nSamples`,
`peakSampleIndex`, `nFeatures`, `sdiffPairs`); new groups get 52 / 26 / 3. Edits that leave the
layout unchanged (moving a site, a label) leave both pages alone. If a probe's file cannot be
read, the other probes keep their channels and the pages are not touched; the status line says
which file is missing.

On save each group gets the `probeId` of the probe its channels are on and the `shankIndex` of
their shank (removed if they span shanks), and an existing `sitePositions_um` is refreshed
from the probe file; a group on no probe loses a stale `probeId`.

### Workflow

1. Open or create the session YAML and set the channel count; save it once.
2. On the **Probes** tab add one probe per physical probe, in channel order; check each
   channel offset and edit the geometry if needed.
3. Check the **Anatomical Groups** and **Spike Groups** pages, then save.

Alternatively list the probes in `probes:` and run `ndm_setupgroups` (from the Plugins tab or
the command line), which writes the groups, `probeId`, `shankIndex` and `sitePositions_um` into
the YAML. It refuses to replace existing groups unless its `overwrite` parameter is true.

---

## Running the preprocessing pipeline

Plugins can be launched from ndmanager in three ways:

- **Individual step** — right-click a plugin page and select *Run*. Useful for reprocessing
  one step after a parameter change.
- **Full batch** — use **Actions → Run All** to execute the complete pipeline in the correct
  order, equivalent to calling `ndm_start` on the command line.
- **Pipeline Designer** — see the **Pipeline** tab below for an editable node-graph view
  whose order is what `ndm_start` dispatches at runtime.

Plugin output streams into a docked process widget at the bottom of the window. Exit codes are
reported; code `10` (output already exists, step skipped) is shown as a notice rather than an
error.

---

## Pipeline tab — editable node graph

The **Pipeline** entry in the parameter tree (peer to *Plugins*) shows the session's pipeline
as an editable directed graph. The root is **`ndm_start`** — always present, gold-accented
with a "★ ROOT" badge so the entry point is unmistakable. Every other node is a freely
editable `ndm_*` plugin: drag from the palette to add, drag node bodies to position, drag
from a node's output port to another's input port to connect, click edges to select,
**Delete** to remove the selected node or edge. An edge that would close a cycle, or that
ends on the root, is refused; the reason appears in a notice bar under the toolbar.

When you open a session for the first time, the graph contains **only the `ndm_start` root**.
You build the pipeline from scratch by dragging plugins from the palette. The result is saved
to a separate `<session>.ndm.<name>.pipeline` YAML file (see [Saving the graph](#saving-the-graph)
below); the next time you open the session, the saved pipeline is auto-loaded.

The palette lists every `ndm_*` plugin that reads parameters from the session (except
`ndm_xml2yaml` and `ndm_recolorchannels`, which edit the parameter file rather than process
data) as a tree with a collapsible branch per purpose (see [Plugins by purpose](#plugins-by-purpose));
every category in the built-in table gets a branch. Drag an entry onto the canvas, or
double-click it or press Enter on it to add it to the right of the graph (Enter on a branch
opens or closes it). A node dropped onto the canvas starts with the parameters of that
plugin's block in `templates/template.yaml`, with the template's values; a node loaded from a
pipeline file keeps the file's values. *Extract Spikes (SDiff / StDeriv)* and *PCA (StDeriv)* are
aliases that run Detect / Extract Spikes or PCA with that method; they have no parameters of
their own — set them on *Detect Spikes*, *Extract Spikes* and *PCA*.

The list is generated: `src/ndmanager/tools/pipeline_script_defs.py` writes
`src/ndmanager/src/pipelinescriptdefs.inc` from the template plus a table of palette labels,
tooltips and categories. To add a plugin, give it a block in `templates/template.yaml`, add
its row to that table, and rerun the script; `test_template_parameters.py` and
`test_pipeline_designer_defs.py` (in `src/ndmanager-plugins/test/`) fail until the script,
the template and the designer agree.

The toolbar carries a preset combo (Pipeline A / C / D / Full), **Save** / **Save As**
buttons, an **Apply Pipeline** button (pushes the graph's order into the Plugins tab, see
below), and a **Clear** button, which empties the graph back to a fresh `ndm_start` root (its
flags reset to their defaults) and counts as an unsaved edit.

```
   ┌─────────────┐    ┌────────────┐    ┌───────────────────┐    ┌─────────┐    ┌────────────────┐
   │ ndm_start ★ │ ─→ │ ndm_hipass │ ─→ │ ndm_extractspikes │ ─→ │ ndm_pca │ ─→ │ ndm_klustakwik │
   └─────────────┘    └────────────┘    └───────────────────┘    └─────────┘    └────────────────┘
        ROOT
```

### Sticky-root semantics

`ndm_start` is the orchestrator entry point. The Pipeline tab enforces these invariants:

| Invariant | How |
|---|---|
| Always present | Clear rebuilds the root; loading a pipeline file without one adds one (and says so) |
| Exactly one | The palette doesn't offer `ndm_start` and drops of a second are rejected; loading a file keeps its first `ndm_start` and drops any other (with its edges), with a warning |
| Never deletable | Backspace/Delete are ignored when the root is selected |
| Indegree 0 | Edge-completion refuses any drag that ends on the root (its input port is hidden); loading drops edges into it, with a warning |
| First in the exported list | **Apply** puts `ndm_start` first, and the session file's `programs:` list is written in the Plugins tab's order, so after Apply + save `programs[0]` is `ndm_start` |

The graph must also be acyclic: the canvas refuses an edge that closes a cycle, and loading a
hand-edited file drops, in file order, each edge that would (`ndm_start` would otherwise skip
every node on or after the cycle).

Selecting the root in the inspector exposes its seven legacy flag parameters (`wideband`,
`events`, `video`, `concatenation`, `spikes`, `lfp`, `clean`). These are no longer used to
gate dispatch — the graph order is authoritative — but they survive as a compatibility hint
for the bash `ndm_start` falling back to legacy mode (see below).

### Saving the graph

The graph is saved to a separate `<session>.ndm.<name>.pipeline` YAML file alongside the
session — **not** inside the session YAML's `programs:` block. This keeps pipeline editing
independent of the session schema and lets you keep multiple named variants per session.

| Action | Where | Shortcut | Writes |
|---|---|---|---|
| **Save Pipeline** | File menu, Pipeline toolbar | Ctrl+Alt+P | `<session>.ndm.default.pipeline` |
| **Save Pipeline As…** | File menu, Pipeline toolbar | Ctrl+Alt+Shift+P | `<session>.ndm.<name>.pipeline` (prompts for name; asks before overwriting) |
| **Load Pipeline…** | File menu | — | replaces graph from selected `*.pipeline` file |

`<session>` is the session file's path without its `.yaml` / `.yml` / `.xml` extension, so
both saves need a session file of their own: on a document made with **New** or from a
template that has not been saved yet, they refuse with "Save the session file first".

Pipeline files are minimal YAML — `nodes` (id, type, pos, enabled, params) and `edges`
(from, to). Parameter values are written so they read back exactly (quotes, spaces, tabs and
newlines included); in a hand-edited file a non-string value such as `0.50` or `yes` is read
as its YAML text. Names are sanitised to `[a-z0-9_-]` (lowercased, whitespace → underscore,
other characters dropped). The `.ndm.` infix namespaces our pipeline files so they don't
collide with `.pipeline` files produced by other tools (Klusters, snakemake, nextflow, …)
in the same session directory. Reading a pipeline file needs `python3` with PyYAML on `PATH`.

When you open a session, ndmanager auto-loads `<session>.ndm.default.pipeline` if it
exists. If no such file exists, the Pipeline tab shows just the `ndm_start` root node —
you build the graph from scratch via the palette. If the file exists but cannot be loaded
(YAML error, `python3` or PyYAML missing), a red notice in the Pipeline tab says why and that
the graph shown is *not* that pipeline, and **Save Pipeline** asks before overwriting the
file. Repairs made while loading (unknown plugin types, duplicate ids, a second root, edges
into the root or closing a cycle) are listed in the notice too.

Graph edits not yet written to the default pipeline (Save Pipeline, or Save As named
`default`) count as unsaved changes when you close the document or quit, and the close prompt
offers to save them; a Save As copy under another name leaves them unsaved, since the default
file is still the old graph. Saving the session file (Ctrl+S) does **not** write the pipeline
file.

### Apply Pipeline vs Save Pipeline

These are two separate actions:

- **Apply Pipeline** — orders the Plugins tab as the graph runs; the next document save
  (Ctrl+S) writes that order to the session YAML's `programs:` block, with `ndm_start` first.
  It does not throw away what the Plugins tab holds:
  - a plugin already in the Plugins tab keeps its page — its tuned values, help text and
    script; only parameters you edited in the node inspector since the last Apply are
    written onto it;
  - a plugin new to the Plugins tab gets a page with its node's values;
  - several nodes of one plugin type give one entry, at the first one's place;
  - plugins that are not in the graph, or only on disabled nodes, stay in the list after the
    graph's plugins — their entries hold the parameter overrides those plugins read.

- **Save Pipeline** — writes the graph itself (positions, edges, node params) to a
  `.pipeline` file. Doesn't touch the session YAML or the Plugins tab. The runtime
  `ndm_start` reads the plugin *order* from this file directly.

At run time every plugin reads its **parameters** from its entry in the session file's
`programs:` block, never from the pipeline file — the node parameters in a pipeline file are
only the designer's copy. So: change parameters in the Plugins tab (or edit them in the node
inspector and **Apply**), and save the session; design the order in the graph and **Save
Pipeline**.

### Runtime behavior: `ndm_start` reads its own program list

```sh
cd /data/Rat               # the directory holding Rat.yaml and the session directories
ndm_start Rat.yaml         # or: ndm_start Rat  — or plain ndm_start when the directory is named Rat
ndm_start Rat.yaml Rat-20200101 Rat-20200102   # only these directories
```

`ndm_start [parameter_file] [directory ...]` runs in the current directory. The parameter
file argument is reduced to its stem (directory part and `.yaml` / `.yml` / `.xml` dropped)
and `<stem>.yaml`, `.yml` or `.xml` must exist **in the current directory** — `ndm_start
/elsewhere/Rat.yaml` does not read `/elsewhere`. Without arguments the stem is the current
directory's name. Without directory arguments, the directories are discovered as `*-<suffix>`
entries (first suffix of `ndm_start`'s `suffixes` parameter) with that suffix stripped; each is
created if needed and gets a copy of the parameter file as `<directory>.yaml`.

It chooses its source of plugin order once, in this priority:

1. `<stem>.ndm.${NDM_PIPELINE:-default}.pipeline` in the current directory — the file
   ndmanager's Save Pipeline writes beside the same parameter file. The `NDM_PIPELINE` env
   var selects a named variant:

   ```sh
   NDM_PIPELINE=best ndm_start Rat.yaml           # uses Rat.ndm.best.pipeline
   NDM_PIPELINE=experimental ndm_start Rat.yaml   # uses Rat.ndm.experimental.pipeline
   ndm_start Rat.yaml                             # uses Rat.ndm.default.pipeline
   ```

   A missing `default` file falls through to 2 without a message; a missing file named with
   `NDM_PIPELINE` is an error (ndm_start stops rather than run another pipeline). A file that
   yields no plugins (empty, unreadable) falls through with a warning.

2. **In-YAML graph** (`programs[0]` is `ndm_start`) — the plugins after it, in list order. A
   session file saved by ndmanager after Apply Pipeline has this form; older ndmanager builds
   saved the programs in alphabetical order, which puts `ndm_start` after `ndm_a…` – `ndm_s…`
   plugins, so such files usually take path 3 until re-saved after Apply.

3. **Hard-coded flag-driven `do_*` sequence** — original ndm_start, kept for sessions
   with no graph at all. The seven flag parameters (`wideband`, `events`, `video`,
   `concatenation`, `spikes`, `lfp`, `clean`) on the `ndm_start` entry still gate this
   path.

Plugins are classified per-session or per-directory inside the bash; per-session plugins
run inside the directory loop (one invocation per session file), per-directory plugins run
once per directory after the per-session phase.

Existing session YAMLs continue to work without any changes — they take path 2 or 3.

Per-plugin failures inside graph mode are logged as warnings and the next plugin still
runs; this matches the user expectation that one bad sort shouldn't stop LFP downsampling.
A plugin name in the graph that isn't on PATH (typo, deleted, external) is also a warning,
not a fatal error.

### Notes

- The graph order **is** the runtime order: the designer and `ndm_start` run the same
  topological sort (Kahn's algorithm, ties broken in node order — file order for a loaded
  pipeline), so Apply and the saved file agree with what `ndm_start` dispatches.
- Disabling a node in the inspector skips it at runtime — the topo-sort still emits the
  node (so positions are preserved), but the dispatcher checks `enabled` and skips it.
  (`ndm_start` itself is never disable-able.)
- The toolbar's **Preset** combo loads canonical chains (Pipeline A / C / D / Full); each
  preset prepends `ndm_start` automatically. Save Pipeline after loading a preset to
  persist it.
- For the full design, see [`../design/ndm-start-root.md`](../design/ndm-start-root.md).

---

## Record Curated Units

The session's `units` table (Units page) is the record of **finished**
curation. Klusters never writes it: it saves each clustering's cluster
information (structure, type, isolation distance, quality, notes) beside the
clustering, as `<session>.cli.<method>.<group>[.<tag>]`
([format](../ndmanager-plugins/formats/cli.md)).

When a group's curation is done, **Actions ▸ Record Curated Units…** lists
those files next to the session — group, number of units, when saved — with
the newest file of each group ticked. Each ticked file replaces its group's
rows in the Units table; other groups are kept, and one file per group may be
chosen. The table is then modified like any edit: **File ▸ Save** writes it
to the session YAML. The session must have been saved once (the files are
found next to it).

---

## Query

**Actions ▸ Query…** searches every `*.yaml` / `*.yml` file under a chosen directory
(subdirectories included) for a value and reports a File | Path | Value table. The query is a
path of keys separated by `/`; a segment can also be a list index or `*` for any key or item,
e.g. `acquisitionSystem/samplingRate` or `spikeDetection/channelGroups/*/nSamples`. Files
that are not valid YAML are named in the report. XPath is not supported.

---

## Built-in session templates

**File ▸ New from Project Template…** (Ctrl+Shift+N) creates a session parameter file from the
project template, `templates/template.yaml`: it asks where to save the new file (normally
`SESSION/SESSION.yaml`), writes the template there with today's date in `generalInfo.date`,
and opens it. The template lists every plugin with every parameter it reads, and its comments
— what each section is for, which script reads each key, the optional parameters left
commented out — stay in the new file and survive later saves. Fill in the fields left empty
(`nChannels`, `samplingRate`, the probes and groups).

The template is installed as `<datadir>/ndmanager/templates/session-template.yaml`
(e.g. `/usr/local/share/ndmanager/templates/`). If it is not found there or beside the
executable (`../share/ndmanager/templates/`), ndmanager asks for the file;
`NEUROSUITE_SESSION_TEMPLATE=/path/to/template.yaml` points it at another one.

**File ▸ New** starts from `ndManagerDefault.yaml` instead (a short document, or your own
default saved with **Save as Default** as `ndmanager/ndManagerDefault.yaml` in the writable
applications directory, `~/.local/share/applications/ndmanager/ndManagerDefault.yaml` on
Linux), and **File ▸ Use Template…** opens any parameter file
as the model for a new one. The `Template-*.yaml` files in `src/ndmanager/src/` (installed
beside the project template) cover common tetrode and octrode configurations.

### Your own templates: export a session, apply it to others

**File ▸ Export Session as Template…** saves how the open session is set up so other
sessions can reuse it. The dialog asks for a name, an optional description and the
sections to include, one checkbox each; the session's unsaved edits are included. By
default the template holds everything except what belongs to this session alone:
general information, `sourceRecording` and the units are left unchecked.
Templates go to your template folder, `<AppDataLocation>/templates` (on Linux under
`~/.local/share/`), as `<name>.template.yaml`. Browse… writes the file anywhere else.

**File ▸ Apply Template to Session…** lists the templates in that folder by name
(Other File… takes any file, including an ordinary session file), and the sections the
chosen one holds. Each checked section **replaces** the session's own; the rest of the
session is kept. By default the processing sections are checked: field potentials,
anatomical and spike groups, NeuroScope display and plugins. The session's recording setup
(acquisition system, extra files, video, probes) and its units are unchecked. If the
template was made for a different channel count and you apply groups, display or probes
without its acquisition system, the dialog warns you. Applying rebuilds the pages from the
merged content. Unsaved plugin scripts, descriptions and pipeline edits are offered for
saving first; unsaved session parameters are kept. Nothing is written until you save.

A template is an ordinary parameter file plus a `template:` block, so it also opens with
**Use Template…**:

```yaml
template:
  name: CA1 4-shank
  description: Buzsaki64, 20 kHz
  createdFrom: rat01-20260901.yaml
  created: 2026-10-16
  sections: [acquisitionSystem, probes, anatomicalDescription, spikeDetection, neuroscope, programs]
parameters: {version: '1.0', creator: ndManager}
acquisitionSystem: ...
```

A section is a top-level key; `probes` carries `probeLibraryPath` with it. A top-level
block ndmanager does not know about is a section too, exported by default and applied only
when checked. The section rules live in `libklustersshared` (`sessiontemplate.h`) and are
covered by `klustersshared_sessiontemplate_test`.

---

## Parameter file format

Session parameter files are YAML. An old XML parameter file does not open in ndmanager;
`ndm_xml2yaml` converts it.

Saving writes ndmanager's fields over the file the session was opened from and keeps
everything else in it: keys and blocks ndmanager has no page for (`sourceRecording`,
extra `acquisitionSystem` keys, a group's `probeId` / `shankIndex` / `sitePositions_um` /
`klustakwik:` block, ...) survive a save, including Save As. A group deleted in ndmanager
is removed with its keys. **Save as Default** is the exception: the default template is
written from ndmanager's fields alone, so one session's blocks do not reach every new file.

The file's layout survives too: comments (including commented-out parameters), inline lists
such as `channels: [0, 1, 2, 3]`, quoting (`value: 'false'` stays a string) and the spacing
around values are kept, and saving an unchanged session rewrites the same text. Comments
belong to the key or list item below them, so a program's comments follow the program when
the list is reordered and leave with it when it is removed. Lists ndmanager creates are written
inline when they hold plain values. NeuroScope and Klusters save the same way.

### YAML schema (abbreviated)

```yaml
parameters:
  version: "1.0"
  creator: "ndManager"
generalInfo:
  date: "2024-01-15"
  experimenters: "gravio"
  description: "CA1 silicon probe"
acquisitionSystem:
  nBits: 16
  nChannels: 64
  samplingRate: 32552
  voltageRange: 20
  amplification: 1000
  offset: 0
fieldPotentials:
  lfpSamplingRate: 1250
video:
  samplingRate: 25
  width: 640
  height: 480
files:
  - extension: lfp
    samplingRate: 1250
probeLibraryPath: /opt/probes        # optional; searched before the installed libraries
probes:
  - probeId: 0
    probeFile: neuronexus/Buzsaki64.probe    # or session.probe.0.probe (a session copy)
    label: "left CA1"
    channelOffset: 0                         # absolute first channel
    anatomicalGroups: [1, 2, 3, 4, 5, 6, 7, 8]
anatomicalDescription:
  channelGroups:
    - channels:
        - {id: 0, skip: 0}
        - {id: 1, skip: 0}
      probeId: 0
      shankIndex: 0
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3, 4, 5, 6, 7]
      nSamples: 52
      peakSampleIndex: 26
      nFeatures: 3
      probeId: 0
      shankIndex: 0
      sitePositions_um: [[0, 0], [22, 0], [0, 20], [22, 20],
                          [0, 40], [22, 40], [0, 60], [22, 60]]
      klustakwik:
        MergeThresh: 39.13      # χ²(12, 0.9999) for a tetrode
        MaxClusters: 20
        GlobalMergeIter: 50
programs:
  - name: ndm_setupgroups
    parameters:
      - name: nSamples
        value: 52
        status: Optional
```

For the full YAML schema see [libklustersshared](../libklustersshared/README.md#yaml-schema-reference).

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
