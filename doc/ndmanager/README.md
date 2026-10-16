# ndmanager — Session Manager

ndmanager is a Qt6 GUI for managing electrophysiology recording sessions. It opens a `.yaml`
(preferred) or legacy `.xml` parameter file, provides a tabbed interface for editing all
acquisition and processing parameters, and dispatches the `ndmanager-plugins` preprocessing
pipeline — either step by step or as a full batch.

> **XML is accepted on open for backward compatibility, but the preprocessing pipeline no
> longer reads XML directly** — legacy XML files should be converted to YAML with
> `ndm_xml2yaml` before running the pipeline. ndmanager always saves YAML.

**Contents:** [Dependencies](#dependencies) · [Launching](#launching) · [What ndmanager manages](#what-ndmanager-manages) · [The Probes tab](#the-probes-tab) · [Running the preprocessing pipeline](#running-the-preprocessing-pipeline) · [Pipeline tab — editable node graph](#pipeline-tab--editable-node-graph) · [Built-in session templates](#built-in-session-templates) · [Parameter file format](#parameter-file-format) · [Installation](#installation)

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml) | UI framework | Yes |
| libklustersshared | Shared YAML layer and widget library | Yes — build first |
| libxml2 | XML parameter file reading | Yes |
| yaml-cpp | YAML parameter file reading/writing | Yes |

`libklustersshared` is not available as a distro package and must be built from source before
ndmanager. See the [install guide](install/linux.md).

---

## Launching

```bash
# Open an existing session
ndmanager session.xml
ndmanager session.yaml

# Start a new session from the project template: File ▸ New from Project Template… (Ctrl+Shift+N)

# Crash-safe wrapper — forces software OpenGL to avoid MESA/Zink segfaults
# on Ubuntu 24.04 systems where Vulkan is present but incomplete
scripts/ndmanager-safe session.yaml
```

The format (XML or YAML) is detected automatically from the file extension. Both formats are
read by dispatching to either `XmlReader` or `NdManagerYamlReader`; the same UI is presented
regardless of which format is on disk.

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
its first ADC channel (`channelOffset`), and a user label. The Probes tab is the GUI entry
point for the `probes:` YAML section; the groups in the Electrode Groups tab are then populated
automatically by running `ndm_setupgroups`.

**Field potentials** — LFP sampling rate.

**Video** — frame rate, position-tracking sampling rate, and LED detection threshold. Video
dimensions (width/height) are stored in the top-level `video` YAML section and are separate
from the neuroscope-specific rotation and flip settings.

**Plugin parameters** — one page per installed `ndm_*` script. Each page exposes the
parameters that script reads from the parameter file, with Mandatory/Optional/Dynamic status
indicators. Parameters set here land in the `programs[name=...].parameters` section of the
YAML (tier-2 of the three-tier resolution: per-group override → global session override → bash
default). For KiloKlustaKwik specifically, each `spikeDetection.channelGroups[g]` entry may also
carry an embedded `klustakwik:` sub-block with per-group calibrated values (tier-1, highest
priority) — these are typically written by `ndm_aom2dat` from probe topology. See
[ndmanager-plugins](../ndmanager-plugins/README.md) for the complete parameter reference per
script.

---

## The Probes tab

The Probes tab exposes the `probes:` section of the session YAML and is the starting point for
any silicon probe session.

### Fields

| Field | YAML key | Description |
|---|---|---|
| Probe ID | `probes[].probeId` | Integer identifier used by `ndm_setupgroups`, `ndm_estimatedrift`, `ndm_localise`. Legacy sessions may still use `probes[].id`; both keys are accepted on read, only `probeId` is written |
| Probe file | `probes[].probeFile` | Path relative to the probe library (e.g. `neuronexus/Buzsaki64.probe`) |
| Label | `probes[].label` | Free-text label displayed in NeuroScope and Klusters |
| Channel offset | `probes[].channelOffset` | First ADC channel index for this probe (0-based) |
| Anatomical groups | `probes[].anatomicalGroups` | List of anatomical group IDs assigned to this probe. Populated automatically by `ndm_setupgroups` |
| Library path | session-level | Override for the probe library search path (optional; leave blank to use the system default) |

### Probe library

The probe library is a directory tree of `.probe` YAML files installed at
`${CMAKE_INSTALL_DATADIR}/neurosuite/probes/` (typically
`/usr/local/share/neurosuite/probes/`). The supplied NeuroNexus library covers 50 probe models.

The **Library path** field overrides or appends to the default search path. The search order
from lowest to highest priority is:

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH  (colon-separated environment variable)
SESSION_DIR/probes/     (session-local probes placed next to the .yaml file)
Library path field      (set in the Probes tab)
```

### Workflow

1. Open or create a session YAML.
2. On the **Probes** tab: add one row per physical probe, set `probeFile` and `channelOffset`.
3. Save the session.
4. Run `ndm_setupgroups` (from the Plugins tab or command line). This populates the Electrode
   Groups tab automatically, writing `probeId`, `shankIndex`, and `sitePositions_um` (inline
   electrode geometry) into each anatomical and spike-detection group so downstream tools
   (`ndm_estimatedrift`, `ndm_localise`, kiloklustakwik drift-aware chunking) can reconstruct
   geometry without re-reading the probe file.
5. Verify channel assignments on the Electrode Groups tab, then proceed with the pipeline.

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
data), grouped by category. A node dropped onto the canvas starts with the parameters of that
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

Graph edits not yet written with Save Pipeline / Save As count as unsaved changes when you
close the document or quit. Saving the session file (Ctrl+S) does **not** write the
pipeline file.

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

   A missing file falls through to 2 without a message; a file that yields no plugins (empty,
   unreadable) falls through with a warning.

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
default saved with **Save as Default**), and **File ▸ Use Template…** opens any parameter file
as the model for a new one. The `Template-*.yaml` files in `src/ndmanager/src/` (installed
beside the project template) cover common tetrode and octrode configurations.

---

## Parameter file format

Both XML and YAML formats are fully supported. YAML is preferred for new sessions. The
`ndm_xml2yaml` script converts existing XML session files.

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
probes:
  - probeId: 0
    probeFile: neuronexus/Buzsaki64.probe
    label: "left CA1"
    channelOffset: 0
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
