# ndmanager — Session Manager

ndmanager is a Qt6 GUI for managing electrophysiology recording sessions. It opens a `.yaml`
session parameter file and shows its acquisition and processing parameters as pages in a
parameter tree on the left of the window (the **Parameters** dock). It edits the session and
the pipeline graph that the `ndmanager-plugins` preprocessing steps follow; it does not run
them. The steps run in a terminal — see
[Running the preprocessing pipeline](#running-the-preprocessing-pipeline).

> **ndmanager reads and writes YAML only.** A legacy `.xml` parameter file no longer opens;
> convert it first with [`ndm_xml2yaml`](../ndmanager-plugins/commands/ndm_xml2yaml.md)
> (`ndm_xml2yaml session.xml` writes `session.yaml`).

**Contents:** [Dependencies](#dependencies) · [Launching](#launching) · [Menus](#menus) · [What ndmanager manages](#what-ndmanager-manages) · [The Probes page](#the-probes-page) · [Running the preprocessing pipeline](#running-the-preprocessing-pipeline) · [Pipeline page — editable node graph](#pipeline-tab--editable-node-graph) · [Built-in session templates](#built-in-session-templates) · [Parameter file format](#parameter-file-format) · [Installation](#installation)

---

## Dependencies

As declared in `src/ndmanager/CMakeLists.txt` and `src/ndmanager/src/CMakeLists.txt`:

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 ≥ 6.4 (Core, Gui, Widgets) | UI framework | Yes |
| Qt6 Svg | SVG icons (`ND_HAVE_QT_SVG`) | Optional |
| libklustersshared ≥ 2.1.0 | Shared YAML layer and widget library | Yes |
| yaml-cpp | YAML parameter file reading/writing | Yes |

`libklustersshared` is part of this repository and is not a distro package. The top-level
build compiles it together with ndmanager, so there is nothing to build first; see
[INSTALL.md](../INSTALL.md). Only a standalone build of `src/ndmanager` needs an installed
LibKlustersShared 2.1.0.

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
```

On Ubuntu 24.04 systems where Vulkan is present but incomplete, ndmanager can crash in
MESA/Zink at start-up. `scripts/ndmanager-safe session.yaml` (in the source tree; it is not
installed) forces software OpenGL and then starts `/usr/bin/ndmanager`. That path is fixed in
the script, so with any other install prefix (`~/.local` or `/usr/local`, as in
[INSTALL.md](../INSTALL.md)) the wrapper fails; copy it and change its last line to your
`ndmanager`.

Installing ndmanager registers the MIME type `application/nphys-yaml` (a `.yaml`/`.yml` file
with an `acquisitionSystem:` key near its top — any other YAML stays plain `application/yaml`)
and the desktop entry opens it, so file managers offer ndmanager for session files.

---

## Menus

**File**

| Entry | Shortcut | What it does |
|---|---|---|
| New… | Ctrl+N | A new session from `ndManagerDefault.yaml` (see [Built-in session templates](#built-in-session-templates)) |
| New from Project Template… | Ctrl+Shift+N | A new session file from the project template |
| Open… | Ctrl+O | Open a session `.yaml` |
| Open Recent | | Recently opened sessions |
| Use Template… | | Open any parameter file as the model for a new session; Save asks for a name |
| Save… / Save As… | Ctrl+S / Ctrl+Shift+S¹ | Write the session file |
| Save as Default | | Make the open session the starting point of **New…** |
| Export Session as Template… | | [Save chosen sections as a template](#your-own-templates-export-a-session-apply-it-to-others) |
| Apply Template to Session… | | Replace chosen sections with a template's |
| Save Pipeline | Ctrl+Alt+P | Write the Pipeline graph to `<session>.ndm.default.pipeline` |
| Save Pipeline As… | Ctrl+Alt+Shift+P | Write it under another name |
| Load Pipeline… | | Load a `*.pipeline` file into the Pipeline graph |
| Reload | F5 | Read the session file again (unsaved changes are offered for saving first) |
| Close / Quit | Ctrl+W / Ctrl+Q¹ | |

¹ The platform's standard keys; shown here as on Linux. Windows has no standard Save As or
Quit key.

**Actions**

| Entry | What it does |
|---|---|
| Query… | [Look up a value in every YAML file under a directory](#query) |
| Discover Plugins | [Add the installed plugins that describe themselves](#plugin-discovery) |
| Record Curated Units… | [Fill the Units page from Klusters' cluster information](#record-curated-units) |

**Settings**

| Entry | What it does |
|---|---|
| Expert Mode | Shows the Files, Probes, Anatomical Groups and Spike Groups pages, the NeuroScope Channel Colors and Channel Offsets tabs, and lets you add, remove and reorder plugin parameters. On by default; the choice is kept between runs. Switching it reopens the document (unsaved changes are offered for saving first). |
| Theme | System, Light or Dark, shared by all NeuroSuite programs |
| Show Main Toolbar / Show StatusBar | Show or hide them |

**Help** — **Handbook** (F1) opens this manual; **About** shows the program's credits.

---

## What ndmanager manages

The `.yaml` parameter file is the central manifest for an entire session. ndmanager provides a
page in the parameter tree for every section of that file. The Files, Probes, Anatomical
Groups and Spike Groups pages, and the Channel Colors and Channel Offsets tabs of the
NeuroScope page, are shown only in **Settings ▸ Expert Mode** (on by default). Outside expert
mode their content is still read and saved.

**Acquisition system** — channel count, bit depth (nBits), sampling rate, voltage range,
amplification factor, and ADC offset.

**Electrode groups** — which channels belong to each anatomical and spike-detection group.
Each group carries its waveform window length (`nSamples`), peak sample index, and the number
of PCA features per channel (`nFeatures`). Channels can be color-coded; those colors
propagate to neuroscope's trace display.

**Probes** — which physical probes are connected, what `.probe` configuration file each uses,
its first recording channel (`channelOffset`), and a user label, plus a geometry editor for each
probe. The Probes page is the GUI entry point for the `probes:` YAML section; it fills the
anatomical and spike groups itself (one per shank), or `ndm_setupgroups` does it from the
command line.

**Field potentials** — LFP sampling rate.

**Video** — frame rate, position-tracking sampling rate, and LED detection threshold. Video
dimensions (width/height) are stored in the top-level `video` YAML section and are separate
from the neuroscope-specific rotation and flip settings.

**Plugin parameters** — one page per plugin, listed under **Plugins** in collapsible branches
by purpose (Conversion, Preparation, Detection, Sorting, …; see
[Plugins by purpose](#plugins-by-purpose)). The pages are the session file's `programs:`
entries, plus any you add: on the Plugins page **Load…** reads a plugin description (`.yaml`),
and in expert mode **Add** creates an empty entry and **Discover** (also **Actions ▸ Discover
Plugins**) adds the plugins on the search path that describe themselves, such as
`ndm_fiber-kit` (see [Plugin discovery](#plugin-discovery)); the steps this repository ships do
not, their pages come from the session file and the template; **Apply
Pipeline** adds the plugins of the Pipeline graph. Each page exposes the
parameters that script reads from the parameter file, with Mandatory/Optional/Dynamic status
indicators. Parameters set here land in the `programs[name=...].parameters` section of the
YAML (tier-2 of the three-tier resolution: per-group override → global session override → bash
default). For KiloKlustaKwik specifically, each `spikeDetection.channelGroups[g]` entry may also
carry an embedded `klustakwik:` sub-block with per-group calibrated values (tier-1, highest
priority) — these are typically written by `ndm_aom2dat` from probe topology. See
[ndmanager-plugins](../ndmanager-plugins/README.md) for the complete parameter reference per
script.

### Plugin discovery

**Discover Plugins** looks for executables named `ndm_*` in the directories of
`$NDM_PLUGIN_PATH` and then of `$PATH` (a name found earlier hides the same name later). Each
one is first run with `--ndm-version`; only a program whose first output line is
`ndm-plugin-protocol <N>` counts as a plugin, and one with a protocol newer than this
ndmanager understands is skipped. A plugin is then run with `--ndm-describe`, and the
description it prints is added as a page unless a plugin of that name is already listed. The
status bar reports how many were added; a plugin that declares file-format versions this build
does not match is still added, with the details on the console. The `ndm_*` steps shipped with
neurosuite-3 do not answer `--ndm-version`, so Discover does not list them.

### Editing a plugin's parameters

The parameter table edits like a spreadsheet: double-click a cell, press F2, or just start
typing over the selected one. Tab and Shift+Tab move from cell to cell and leave the table
after the last one (or before the first one), so the keyboard never gets stuck in it.
Hovering over a parameter's name shows its part of the plugin's help.

In expert mode, **Add** lists the parameters the plugin reads that the page does not show
yet, with the template's value and a description; filter by typing, pick one or several
(Ctrl/Shift-click) and press **Add** or double-click. A parameter the template sets comes
with the template's value; one the template only mentions in a comment comes empty, which
means the plugin's default, so adding it changes nothing until a value is typed. **Other
Name…** adds an empty row to type a name into, which is also what **Add** does for a plugin
the template does not describe (one found by Discover or made by hand). **Remove** deletes
the selected rows, and **Move Up** / **Move Down** change the order the description lists
the parameters in. Outside expert mode only the Value column can be edited.

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

A value changed here also reaches the nodes of that plugin in the [Pipeline page](#pipeline-tab--editable-node-graph),
replacing any inspector edit of the same parameter not yet applied.

### Plugins by purpose

The Plugins entry of the left-hand tree and the Pipeline page's palette both group plugins into
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

## The Probes page

The Probes page exposes the `probes:` section of the session YAML and is the starting point for
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

There is no library field on the page: `probeLibraryPath` is a top-level key of the session YAML
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
`<prefix>/share/neurosuite/probes/` (`<prefix>` is the install prefix, e.g. `~/.local` or
`/usr/local`): 43 NeuroNexus models in `neuronexus/` and three general
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

Whenever the probes, their offsets or their geometry change the channel layout, the page rebuilds
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
2. On the **Probes** page add one probe per physical probe, in channel order; check each
   channel offset and edit the geometry if needed.
3. Check the **Anatomical Groups** and **Spike Groups** pages, then save.

Alternatively list the probes in `probes:` and run `ndm_setupgroups` in a terminal (or as a
pipeline step), which writes the groups, `probeId`, `shankIndex` and `sitePositions_um` into
the YAML. It refuses to replace existing groups unless its `overwrite` parameter is true.

---

## Running the preprocessing pipeline

ndmanager does not run plugins. Save the session (and the pipeline, if you changed the graph)
and run the steps in a terminal:

```sh
cd /data/Rat/Rat-20200101 && ndm_hipass Rat-20200101.yaml  # one step, in a session directory
cd /data/Rat && ndm_start Rat.yaml   # the whole pipeline, in the order of the Pipeline graph
```

`ndm_start` follows the saved pipeline graph (see
[Runtime behavior](#runtime-behavior-ndm_start-reads-its-own-program-list) below); each step
reads its parameters from its `programs:` entry in the session file. The parameters of every
step are in the [ndmanager-plugins reference](../ndmanager-plugins/README.md).

On Windows the `ndm_*` steps are bash scripts started through `.bat` launchers installed
beside them, so the same commands work in cmd and PowerShell; see
[INSTALL.md → Windows → Run](../INSTALL.md#4-run).

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
tooltips and categories, and `src/ndmanager/src/parametercatalog.inc`, every template
parameter with its help, which the Plugins pages' **Add** offers. To add a plugin, give it a block in `templates/template.yaml`, add
its row to that table, and rerun the script; `test_template_parameters.py` and
`test_pipeline_designer_defs.py` (in `src/ndmanager-plugins/test/`) fail until the script,
the template and the designer agree.

The toolbar carries a preset combo (Pipeline A / C / D / Full), **Save** / **Save As**
buttons, an **Apply Pipeline** button (pushes the graph's order into the Plugins list, see
below), and a **Clear** button, which empties the graph back to a fresh `ndm_start` root (its
flags reset to their defaults) and counts as an unsaved edit.

```
   ┌─────────────┐    ┌────────────┐    ┌───────────────────┐    ┌─────────┐    ┌────────────────┐
   │ ndm_start ★ │ ─→ │ ndm_hipass │ ─→ │ ndm_extractspikes │ ─→ │ ndm_pca │ ─→ │ ndm_klustakwik │
   └─────────────┘    └────────────┘    └───────────────────┘    └─────────┘    └────────────────┘
        ROOT
```

### Sticky-root semantics

`ndm_start` is the orchestrator entry point. The Pipeline page enforces these invariants:

| Invariant | How |
|---|---|
| Always present | Clear rebuilds the root; loading a pipeline file without one adds one (and says so) |
| Exactly one | The palette doesn't offer `ndm_start` and drops of a second are rejected; loading a file keeps its first `ndm_start` and drops any other (with its edges), with a warning |
| Never deletable | Backspace/Delete are ignored when the root is selected |
| Indegree 0 | Edge-completion refuses any drag that ends on the root (its input port is hidden); loading drops edges into it, with a warning |
| First in the exported list | **Apply** puts `ndm_start` first, and the session file's `programs:` list is written in the Plugins list's order, so after Apply + save `programs[0]` is `ndm_start` |

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
exists. If no such file exists, the Pipeline page shows just the `ndm_start` root node —
you build the graph from scratch via the palette. If the file exists but cannot be loaded
(YAML error, `python3` or PyYAML missing), a red notice in the Pipeline page says why and that
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

- **Apply Pipeline** — orders the Plugins list as the graph runs; the next document save
  (Ctrl+S) writes that order to the session YAML's `programs:` block, with `ndm_start` first.
  It does not throw away what the Plugins list holds:
  - a plugin already in the Plugins list keeps its page — its tuned values, help text and
    script; only parameters you edited in the node inspector since the last Apply are
    written onto it;
  - a plugin new to the Plugins list gets a page with its node's values;
  - several nodes of one plugin type give one entry, at the first one's place;
  - plugins that are not in the graph, or only on disabled nodes, stay in the list after the
    graph's plugins — their entries hold the parameter overrides those plugins read.

- **Save Pipeline** — writes the graph itself (positions, edges, node params) to a
  `.pipeline` file. Doesn't touch the session YAML or the Plugins list. The runtime
  `ndm_start` reads the plugin *order* from this file directly.

At run time every plugin reads its **parameters** from its entry in the session file's
`programs:` block, never from the pipeline file — the node parameters in a pipeline file are
only the designer's copy. So: change parameters in the Plugins list (or edit them in the node
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
(`<prefix>/share/ndmanager/templates/`). If it is not found there or beside the
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
(acquisition system, extra files, video, probes), its units, and general information
and `sourceRecording` (when a template holds them) are unchecked. If the
template was made for a different channel count and you apply groups, display or probes
without its acquisition system, the dialog warns you. Applying rebuilds the pages from the
merged content. Unsaved plugin scripts, descriptions and pipeline edits are offered for
saving first; unsaved session parameters are kept. The applied sections take the
template's comments. Nothing is written until you save, and the saved session does not
carry the template's `template:` block.

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

ndmanager is built and installed with the rest of NeuroSuite: see [INSTALL.md](../INSTALL.md)
(Linux, Windows, macOS) and [BUILD.md](../BUILD.md) for build options.
