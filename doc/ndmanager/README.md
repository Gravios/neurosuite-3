# ndmanager — Session Manager

ndmanager is a Qt6 GUI for managing electrophysiology recording sessions. It opens a `.yaml`
(preferred) or legacy `.xml` parameter file, provides a tabbed interface for editing all
acquisition and processing parameters, and dispatches the `ndmanager-plugins` preprocessing
pipeline — either step by step or as a full batch.

> **XML is accepted on open for backward compatibility, but the preprocessing pipeline no
> longer reads XML directly** — legacy XML files should be converted to YAML with
> `ndm_xml2yaml` before running the pipeline. ndmanager always saves YAML.

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

# Start from the built-in YAML template
ndmanager /usr/local/share/ndmanager/templates/template.yaml

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
of PCA features per channel (`nFeatures`). Channels can be colour-coded; those colours
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
default). For KlustaKwik specifically, each `spikeDetection.channelGroups[g]` entry may also
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
   (`ndm_estimatedrift`, `ndm_localise`, klustakwik drift-aware chunking) can reconstruct
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

The **Pipeline** entry in the parameter tree (peer to *Plugins*) shows the session's plugin
list as an editable directed graph. The root is **`ndm_start`** — pinned at position 0,
permanently visible, gold-accented with a "★ ROOT" badge so the entry point is unmistakable.
Every other node is a freely editable `ndm_*` plugin: drag from the palette to add, drag node
bodies to position, drag from a node's output port to another's input port to connect, click
edges to select, **Delete** to remove the selected node or edge.

When you open a session for the first time, the graph contains **only the `ndm_start` root**.
You build the pipeline from scratch by dragging plugins from the palette. The result is saved
to a separate `<session>.ndm.<n>.pipeline` YAML file (see [Saving the graph](#saving-the-graph)
below); the next time you open the session, the saved pipeline is auto-loaded.

The toolbar carries a preset combo (Pipeline A / C / D / Full), **Save** / **Save As**
buttons, an **Apply Pipeline** button (pushes the graph into the Plugins tab), and a
**Clear** button (empties the graph back to just the root).

```
   ┌─────────────┐    ┌──────────┐    ┌───────────────┐    ┌─────────┐    ┌────────────┐
   │ ndm_start ★ │ ─→ │ ndm_hipass│ ─→ │ ndm_extractspikes│ ─→ │ ndm_pca │ ─→ │ ndm_klustakwik │
   └─────────────┘    └──────────┘    └───────────────┘    └─────────┘    └────────────┘
        ROOT
```

### Sticky-root semantics

`ndm_start` is the orchestrator entry point. The Pipeline canvas enforces four invariants:

| Invariant | Why |
|---|---|
| Always present, position 0 | Topological sort emits `ndm_start` first; the YAML's `programs[0]` is always `ndm_start` |
| Never deletable | Backspace/Delete are silently ignored when the root is selected |
| Indegree 0 | Edge-completion rejects any drag that ends on the root's input port (the input port itself is hidden) |
| One per graph | The palette doesn't surface `ndm_start`; drops via mime payload are rejected when one already exists |

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
| **Save Pipeline As…** | File menu, Pipeline toolbar | Ctrl+Alt+Shift+P | `<session>.ndm.<name>.pipeline` (prompts for name) |
| **Load Pipeline…** | File menu | — | replaces graph from selected `*.pipeline` file |

Pipeline files are minimal YAML — `nodes` (id, type, pos, enabled, params) and `edges`
(from, to). Names are sanitised to `[a-z0-9_-]` (lowercased, whitespace → underscore,
other characters dropped). The `.ndm.` infix namespaces our pipeline files so they don't
collide with `.pipeline` files produced by other tools (Klusters, snakemake, nextflow, …)
in the same session directory.

When you open a session, ndmanager auto-loads `<session>.ndm.default.pipeline` if it
exists. If no such file exists, the Pipeline tab shows just the `ndm_start` root node —
you build the graph from scratch via the palette.

### Apply Pipeline vs Save Pipeline

These are two separate actions:

- **Apply Pipeline** (existing button) — pushes the topo-sorted plugin list back into the
  Plugins tab, replacing any existing entries. The next document save (Ctrl+S) writes them
  to the session YAML's `programs:` block. Use this when you want the session YAML to
  reflect the graph (so the legacy `programs:`-driven Run All path uses the new order).

- **Save Pipeline** (new) — writes the graph itself (positions, edges, params) to a
  `.pipeline` file. Doesn't touch the session YAML, doesn't change anything in the Plugins
  tab. The runtime `ndm_start` reads this file directly without going through the Plugins
  flow.

A typical workflow: design a graph, **Save Pipeline**, then run `ndm_start session.yaml`
from the command line — the dispatcher reads `session.ndm.default.pipeline` directly. Use
**Apply Pipeline** only when you want the change to reflect in the Plugins tab and the
session YAML.

### Runtime behaviour: `ndm_start` reads its own program list

The `ndm_start` bash chooses its source of plugin order in this priority:

1. `<template>.ndm.${NDM_PIPELINE:-default}.pipeline` — the pipeline file produced by Save
   Pipeline. The `NDM_PIPELINE` env var lets a CI run select between named variants:

   ```sh
   NDM_PIPELINE=best ndm_start session/             # uses .ndm.best.pipeline
   NDM_PIPELINE=experimental ndm_start session/     # uses .ndm.experimental.pipeline
   ndm_start session/                                # uses .ndm.default.pipeline
   ```

2. **In-YAML graph** (`programs[0]==ndm_start`) — the legacy form, kept for backward
   compatibility with sessions saved before the `.pipeline` files existed (or after Apply
   Pipeline + Ctrl+S).

3. **Hard-coded flag-driven `do_*` sequence** — original ndm_start, kept for sessions
   with no graph at all. The seven flag parameters (`wideband`, `events`, `video`,
   `concatenation`, `spikes`, `lfp`, `clean`) on the `ndm_start` node still gate this
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

- The graph order **is** the runtime order. Re-running `ndm_start /path/to/session` after
  Save Pipeline respects the order saved in the file.
- Disabling a node in the inspector skips it at runtime — the topo-sort still emits the
  node (so positions are preserved), but the dispatcher checks `enabled` and skips it.
  (`ndm_start` itself is never disable-able.)
- The toolbar's **Preset** combo loads canonical chains (Pipeline A / C / D / Full); each
  preset prepends `ndm_start` automatically. Save Pipeline after loading a preset to
  persist it.
- For the full design, see [`../design/ndm-start-root.md`](../design/ndm-start-root.md).

---

## Built-in session templates

The session template at `templates/template.yaml` (installed to the system data directory)
covers a full 28-plugin pipeline including the three new analysis plugins. Use it as a starting
point for new sessions. The legacy XML templates in `src/ndmanager/src/` cover common tetrode
and octrode configurations.

---

## Parameter file format

Both XML and YAML formats are fully supported. YAML is preferred for new sessions. The
`ndm_xml2yaml` script converts existing XML session files.

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
