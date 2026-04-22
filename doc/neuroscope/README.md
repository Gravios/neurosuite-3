# neuroscope — Signal Visualiser

NeuroScope is a Qt6 viewer for multi-channel electrophysiology data. It displays wideband, LFP, and high-pass filtered signals as scrollable traces and simultaneously overlays spike waveforms, cluster assignments, event markers, and animal position. Session state is stored in `.yaml` parameter files; YAML I/O is handled by `NeuroscopeYamlReader` / `NeuroscopeYamlWriter` within the neuroscope component and by `SessionYamlReader` / `SessionYamlWriter` for the viewer's own session state.

> **XML support has been removed.** Legacy `.xml` parameter files must be converted with `ndm_xml2yaml` (from ndmanager-plugins) before opening in neuroscope-3.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets) | UI framework | Yes |
| yaml-cpp | YAML parameter file reading/writing | Yes |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale) | Video display for position overlay | Optional |

NeuroScope does **not** depend on `libklustersshared` and builds independently of ndmanager and klusters.

---

## Launching

```bash
# Preferred — loads all associated files automatically from the parameter file
neuroscope session.yaml

# Open directly from any signal file; neuroscope infers the base name
neuroscope session.dat
neuroscope session.lfp

# Open from a spike file to inspect a specific electrode group
neuroscope session.spk.1
```

NeuroScope searches the same directory for all companion files (`.lfp`, `.fil`, `.spk.N`, `.clu.N`, `.evt.*`, `.pos`) and loads whichever it finds alongside the base session.

---

## Files consumed

| File | Content |
|---|---|
| `session.yaml` | Parameter file — channel count, sampling rates, electrode groups, display colours |
| `session.dat` | Wideband signal (int16, channel-interleaved, no header) |
| `session.lfp` | LFP signal (same binary format as `.dat`, lower sampling rate) |
| `session.fil` | High-pass filtered signal (same binary format as `.dat`) |
| `session.spk.N` | Spike waveforms for electrode group N (int16, sample-major layout) |
| `session.res.N` | Spike timestamps for electrode group N (binary int64 sample indices, no header) |
| `session.clu.N` | Cluster assignments for electrode group N (binary: int32 nClusters header + int32 cluster IDs; legacy text format still accepted) |
| `session.evt` / `session.evt.abc` | Event file — millisecond timestamps + text labels |
| `session.pos` | Animal position — binary x,y pairs at video sampling rate |
| `session.nrs` | NeuroScope session file — saved viewer state (open files, display config, colours) |

All raw signal files (`.dat`, `.lfp`, `.fil`, `.spk.N`) are int16, channel-interleaved, with no header. The number of channels and sampling rates come from the parameter file.

---

## Interface overview

The main window has three areas:

- **Left dock — Channel panel**: two tabs, *Display* and *Spike*. Lists channels organised by anatomical group (Display tab) or electrode group (Spike tab). Checkboxes show/hide individual channels.
- **Centre — Trace view**: the scrollable signal canvas with all overlaid data layers.
- **Right dock — Cluster and Event palettes**: collapsible panels, one per loaded cluster file and one per loaded event file, showing coloured unit/event buttons.

---

## Navigation

| Action | Key / Control |
|---|---|
| Scroll forward / backward | Arrow keys or scrollbar |
| Zoom time axis in / out | `+` / `-` or mouse wheel on trace view |
| Jump to time | Type milliseconds into the toolbar time field and press Enter |
| Page forward | `Page Down` |
| Page backward | `Page Up` |
| Jump to next event | `Ctrl+F` |
| Jump to previous event | `Ctrl+B` |
| Jump to next spike (selected cluster) | `Ctrl+Shift+F` |
| Jump to previous spike (selected cluster) | `Ctrl+Shift+B` |
| Auto-advance (playback to end) | `Ctrl+Shift+Space` (toggle) |
| Accelerate playback | `Ctrl+Up` |
| Decelerate playback | `Ctrl+Down` |

---

## Signal layers

Each layer is toggled independently from the **View** menu. All layers share the same horizontal time axis and are drawn on top of the raw traces.

### Wideband (`.dat`)
Full-bandwidth signal at acquisition sampling rate. Loaded automatically on open.

### LFP (`.lfp`)
Downsampled local field potential. Loaded automatically when present. The LFP sampling rate is read from the parameter file (`lfpSamplingRate`); typical values are 1250 or 1600 Hz.

### High-pass (`.fil`)
Median-filtered, high-pass signal used for spike detection. Loaded automatically when present.

---

## Loading cluster files

Cluster files are loaded via **File → Load Cluster File(s)…** A cluster file must follow the naming convention `basename.clu.N` or `basename.N.clu` (where N is an integer electrode group number). NeuroScope automatically locates the corresponding `.res.N` (timestamps) and `.spk.N` (waveform snippets) files in the same directory.

### What gets loaded

| File | Role | Required |
|---|---|---|
| `session.clu.N` | Integer cluster ID per spike | Yes |
| `session.res.N` | Spike timestamps (sample indices) | Yes — inferred from `.clu.N` path |
| `session.spk.N` | Waveform snippets for each spike | No — enables waveform display mode |

Multiple cluster files can be loaded simultaneously (e.g. groups 1–8 at once). Each gets its own panel in the right-hand cluster palette, with independently selectable and coloured units.

### Error conditions

- **Missing `.res.N`** — neuroscope reports a missing-file error and does not load the group.
- **Mismatched spike count** — if the number of lines in `.res.N` and `.clu.N` differ, a count-error is reported and the file is not loaded.
- **Duplicate load** — if the same `.clu.N` is already loaded, neuroscope offers to reload it.

### Cluster display modes

Three rendering modes are available from the **Units** menu and can be combined freely:

| Mode | Menu item | Description |
|---|---|---|
| **Raster** (default on) | Units → Raster | Vertical tick marks at each spike timestamp, one row per cluster, rendered as a strip above or below the traces |
| **Vertical lines** | Units → Vertical Lines | Full-height coloured vertical lines at each spike timestamp |
| **Waveforms** | Units → Waveforms | Waveform snippets from `.spk.N` drawn in-place at each spike timestamp |

Raster row height is adjustable with `Ctrl++` / `Ctrl+-`.

The position of the raster/line/waveform strip relative to the traces is set in **Preferences → Defaults** (cluster position: between traces, at top, or at bottom).

### Cluster palette

The right-hand palette shows one coloured button per cluster ID. Clicking selects that cluster (used for Next/Previous Spike navigation). Double-click opens a colour picker. Right-click provides show/hide options.

| Action | Shortcut |
|---|---|
| Select All | `Ctrl+A` |
| Select All except 0 and 1 | `Ctrl+Shift+A` (excludes noise/MUA) |
| Deselect All | `Ctrl+U` |

Cluster colour assignments are saved to the session file and restored on reload.

### Closing a cluster file

**File → Close Cluster File** closes the currently active cluster panel.

---

## Waveform display

When Waveforms mode is enabled and `.spk.N` is present, neuroscope draws the snippet for each visible spike centred on the spike timestamp. Two parameters control the window:

- **nbSamples** — total number of samples in the waveform snippet
- **peakIndex** — sample index (0-based) of the spike peak within that window

These are read from the parameter file at startup. They can be changed at runtime via **File → Properties** and take effect immediately for all loaded electrode groups.

Default values (used when the parameter file does not specify them) are configured in **Preferences → Defaults**. These defaults seed the Properties dialog on first open.

---

## Event files

### Format

Event files are plain text with one event per line:

```
timestamp_ms  label
```

The timestamp is a floating-point millisecond value; the label is a free-form string. Example:

```
1234.5  stimulus onset
5678.0  reward
```

Multiple event files can be loaded simultaneously, distinguished by their file suffix: `session.evt`, `session.evt.a`, `session.evt.stim`, etc.

### Loading event files

**File → Load Event File(s)…** opens a multi-file picker. Each loaded `.evt` file gets its own panel in the event palette, with a distinct colour per event description (label).

### Creating a new event file

**File → Create Event File…** prompts for a file name and creates an empty `.evt` file. Use the **Add Event** tool (toolbar button or `E` key) to mark events interactively.

### Editing events

Editing requires **Edit Mode** (`Ctrl+E` or Edit menu).

| Action | Method |
|---|---|
| Add event | Select Add Event tool (`E`), click in the trace view at the target time |
| Move event | Select Event tool, drag an event marker to a new position |
| Remove event | Select Event tool, select the event, then **Events → Remove Event** (`Ctrl+K`) |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` |

New event descriptions are entered in a popup dialog on first use; existing descriptions appear in the Add Event toolbar dropdown.

### Event palette

Events are listed by description in the event palette. Selecting a description enables Next/Previous Event navigation (`Ctrl+F` / `Ctrl+B`). Event markers on the position trajectory view are toggled with **View → Show Events**.

---

## Position file (`.pos`)

**File → Load Position File…** loads an animal trajectory file. The `.pos` file is a binary sequence of x,y coordinate pairs sampled at the video frame rate.

Once loaded, a **Position View** panel docks below the trace view showing the full-session trajectory. A moving dot tracks the animal's current position for the visible time window.

Position view parameters are set in **File → Properties**:

| Parameter | Description |
|---|---|
| Video sampling rate | Frames per second of the `.pos` file |
| Video width / height | Frame dimensions in pixels |
| Background image | Optional image rendered behind the trajectory path |
| Rotation / flip | Orientation correction: 0°/90°/180°/270° with optional horizontal or vertical flip |

When event files are loaded and **View → Show Events** is checked in the position view, each event type is drawn as a dot on the trajectory at the position where it occurred.

---

## Channel panel and amplitude controls

### Palettes

The left dock has two channel palettes:

- **Display palette** — channels grouped by anatomical group. Controls what appears in the trace view.
- **Spike palette** — channels grouped by electrode group. Controls which channels appear in waveform overlays.

Both palettes support click/Ctrl+click selection, checkboxes, and right-click context menus for group-level operations.

### Show / hide channels

| Action | Shortcut |
|---|---|
| Show selected channels | `Ctrl+C` |
| Hide selected channels | `Ctrl+H` |
| Move selected to new group | `Ctrl+G` |
| Remove selected from group | `Shift+Delete` |
| Discard channels (mark dead) | `Delete` |
| Keep channels (un-discard) | `Ctrl+Shift+K` |
| Skip channels | `Ctrl+Shift+S` |
| Synchronise display and spike groups | Channels → Synchronise Groups |

### Amplitude and offset controls

| Action | Shortcut |
|---|---|
| Increase all channel amplitudes | `Ctrl+I` |
| Decrease all channel amplitudes | `Ctrl+D` |
| Increase selected channel amplitudes | `Ctrl+Shift+I` |
| Decrease selected channel amplitudes | `Ctrl+Shift+D` |
| Reset selected channel offsets to zero | Traces → Reset Selected Channel Offsets |
| Reset selected channel amplitudes | Traces → Reset Selected Channel Amplitudes |
| Set current offsets as session defaults | Traces → Set Current Offsets as Defaults |
| Zero all default offsets | Traces → Set Default Offsets to Zero |
| Autocenter channels (subtract window mean) | `Ctrl+Shift+A` (toggle) |

### Colour modes

- **Colour by Anatomical Groups** — channels in the same anatomical group share a colour (Channels menu).
- **Colour by Spike Groups** — channels are coloured by electrode group (Channels menu).

Both modes propagate to the left channel panel colouring.

---

## Display options

| Option | Menu / Shortcut | Notes |
|---|---|---|
| Multiple columns | Traces → Multiple Columns | Splits channels across two horizontal columns |
| Grey-scale | Traces → Grey-Scale | Renders traces in greyscale — useful for printing |
| Show labels | Traces → Show Labels (`Ctrl+L`) | Channel name labels on the left edge of the trace view |
| Display calibration | Settings → Display Calibration | Draws a scale bar overlay |
| Background image | File → Properties | Static image behind all traces |

---

## Multiple display tabs

Each **Display** tab has its own independent time position, zoom level, visible channel set, selected clusters, selected events, and per-channel gain/offset state. All tabs share the same loaded providers.

| Action | Description |
|---|---|
| Displays → New Display | Opens a new tab cloned from the active display |
| Displays → Rename Active Display | Edits the active tab label |
| Displays → Close Active Display | Closes the active tab |

---

## Printing

**File → Print** (`Ctrl+P`) sends the current trace view to the system print dialog in landscape orientation. All visible layers (traces, clusters, events) are included.

---

## Session file (`.nrs`)

**File → Save** / **Save As** writes a `session.nrs` YAML file capturing the full viewer state. On next open, neuroscope restores the loaded files, colours, channel positions, and time window for every display tab.

Structure:

```yaml
neuroscope_session:
  version: "3.0.0"

  files:
    - type: 0            # 0=cluster  1=spike  2=event  3=position
      url: "/path/to/session.clu.1"
      date: "2024-01-15T10:30:00"
      items:
        - id: "3"        # cluster or event ID / description
          color: "#e03030"

  displays:
    - tabLabel: "Display 1"
      showLabels: 0
      startTime: 0          # ms
      duration: 3200        # ms
      multipleColumns: 0
      greyScale: 0
      autocenterChannels: 0
      positionView: 0
      showEvents: 0
      rasterHeight: 40
      spikePresentations: [1]   # 0=lines  1=raster  2=waveforms
      clustersSelected:
        - fileUrl: "/path/to/session.clu.1"
          clusters: [3, 5, 7]
      eventsSelected:
        - fileUrl: "/path/to/session.evt"
          events: [1, 2]
      clustersSkipped: []
      eventsSkipped: []
      spikesSelected: []
      channelPositions:
        - channel: 0
          gain: 1.0
          offset: 0
      channelsSelected: [0, 1, 2, 3]
      channelsShown:    [0, 1, 2, 3, 4, 5, 6, 7]
```

---

## YAML parameter file — neuroscope section

NeuroScope reads display defaults from the `neuroscope` block of the session parameter file (e.g. `session.yaml` created by ndmanager). On save, only `neuroscope/` sub-keys are updated; the rest of the parameter file is preserved.

```yaml
neuroscope:
  miscellaneous:
    screenGain: 0.2           # default vertical scale (mV per pixel)
    traceBackgroundImage: ""  # optional path to a background image
  video:
    samplingRate: 39.0        # video frame rate (fps)
    width: 320
    height: 240
    rotate: 0                 # 0 | 90 | 180 | 270
    flip: 0                   # 0=none  1=vertical  2=horizontal
  channels:
    colors:
      - id: 0
        anatomyColor: "#ff0000"
        spikeColor:   "#ff0000"
    offsets:
      - id: 0
        offset: 0
  spikes:
    nbSamples: 32
    peakIndex: 16
```

---

## Preferences

**Settings → Preferences** opens the global preferences dialog. Relevant settings:

| Setting | Description |
|---|---|
| Background colour | Trace view canvas background |
| Default nbSamples | Waveform window length when not specified in parameter file |
| Default peakIndex | Waveform peak sample position |
| Cluster position | Raster/line strip placement: between traces, at top, or at bottom |
| Event position | Same for event markers |

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
