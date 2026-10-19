# neuroscope — Signal Visualiser

NeuroScope is a Qt6 viewer for multi-channel electrophysiology data. It displays one wideband, LFP or high-pass filtered file as scrollable traces and overlays spike times and waveforms, cluster assignments, event markers and animal position. Acquisition settings, groups and display defaults come from the `.yaml` parameter file (read with `NeuroscopeYamlReader`, written with `ParameterYamlModifier` from `libklustersshared`); the viewer's own state is kept in the `.nrs` session file (`SessionYamlReader` / `SessionYamlWriter`).

The full user manual is the [NeuroScope Handbook](../site/docs/handbook/01-introduction.md) (**Help → Handbook**, `F1`, in the program).

> **XML support has been removed.** Legacy `.xml` parameter files must be converted with `ndm_xml2yaml` (from ndmanager-plugins) before opening in neuroscope-3. A legacy XML `.nrs` session file is ignored (the data opens with default display settings).

**Contents:** [Dependencies](#dependencies) · [Launching](#launching) · [Files consumed](#files-consumed) · [Interface overview](#interface-overview) · [Navigation](#navigation) · [Trace files and overlays](#trace-files-and-overlays) · [Spectral view](#spectral-view) · [Loading cluster files](#loading-cluster-files) · [Waveform display](#waveform-display) · [Event files](#event-files) · [Position file (`.pos`)](#position-file-pos) · [Channel panel and amplitude controls](#channel-panel-and-amplitude-controls) · [Display options](#display-options) · [Multiple display tabs](#multiple-display-tabs) · [Printing](#printing) · [Saving](#saving) · [Session file (`.nrs`)](#session-file-nrs) · [YAML parameter file — neuroscope section](#yaml-parameter-file--neuroscope-section) · [Preferences](#preferences) · [Keyboard shortcuts](#keyboard-shortcuts) · [Installation](#installation)

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, PrintSupport, Concurrent) | UI framework, printing, background computation | Yes |
| `libklustersshared` (neurosuite-3) | Shared widgets, YAML parameter file reader/writer, `.clu`/`.res` readers | Yes |
| `Neurosuite::spectral` (neurosuite-3) | Multitaper engine for the spectral view | Yes |
| yaml-cpp | YAML parsing (through `libklustersshared`) | Yes |

NeuroScope is built with the rest of neurosuite-3 in one CMake build; see [Installation](#installation).

---

## Launching

```bash
# Open a trace file; neuroscope derives the base name and reads session.yaml / session.nrs
neuroscope session.dat
neuroscope session.lfp

# Open the session file; neuroscope then asks which trace file to show
# (session.dat if the dialog is cancelled)
neuroscope session.nrs
```

Any other file is opened as a trace file with that extension (its sampling rate is looked up in the parameter file, or asked for). Command-line options (`neuroscope --help`): `-r` resolution, `-c` number of channels, `-o` offset, `-m` voltage range, `-a` amplification, `-g` screen gain, `-s` sampling rate, `-t` initial time window (ms). They are ignored when a parameter file exists.

From the trace file name, NeuroScope looks for only two companion files in the same directory: the parameter file `session.yaml` (or `session.yml`) and the session file `session.nrs`. It does **not** search for cluster, event or position files: those are loaded from the **File** menu, and the files listed in `session.nrs` are reloaded on the next open.

---

## Files consumed

| File | Content |
|---|---|
| `session.yaml` | Parameter file — channel count, sampling rates, electrode groups, display colors |
| `session.dat` | Wideband signal (int16, channel-interleaved, no header) |
| `session.lfp` | LFP signal (same binary format as `.dat`, lower sampling rate; `.eeg` is the older name) |
| `session.fil` | High-pass filtered signal (same binary format as `.dat`) |
| `session.res.N` | Spike timestamps for electrode group N (binary int64 sample indices, no header; text also accepted) |
| `session.clu.N` | Cluster assignments for electrode group N (binary: int32 nClusters header + int32 cluster IDs; text also accepted) |
| `session.abc.evt` / `session.evt.abc` | Event file — millisecond timestamps + text labels |
| `session.pos` | Animal position — text, one line of `x y [x y …]` per video sample |
| `session.nrs` | NeuroScope session file — saved viewer state (open files, display config, colors) |

Trace files (`.dat`, `.lfp`, `.fil`) are int16, channel-interleaved, with no header. The number of channels and sampling rates come from the parameter file. `.spk.N` files are not read: waveforms are taken from the trace file itself. The format reference for every file is in [doc/ndmanager-plugins/formats](../ndmanager-plugins/formats/README.md).

---

## Interface overview

The main window has two areas:

- **Left — Palettes**: one tab widget with the tabs *Anatomy* (channels by anatomical group), *Spikes* (channels by spike group), *Spectral* (spectral view settings), and — once files are loaded — *Units* (one box per cluster file) and *Events* (one box per event file).
- **Right — Displays**: one tab per display, each with a trace view, the time controls below it, and (when a position file is loaded) a position view docked at the top.

---

## Navigation

| Action | Key / Control |
|---|---|
| Scroll forward / backward by a quarter window | `Right` / `Left` |
| Double / halve the time window | `+` / `-` |
| Zoom time axis around the pointer | Mouse wheel |
| Zoom in / out at the pointer | `Ctrl`+wheel |
| Pan | `Ctrl`+left drag (not with the Select Channels tool) |
| Jump to time | *Start time* min / s / ms boxes below each display; *Duration* next to them |
| Page forward / backward | Click the scroll bar beside the slider (`Page Down` / `Page Up` work while the scroll bar has focus) |
| Jump to next / previous event | `Ctrl+F` / `Ctrl+B` |
| Jump to next / previous spike | `Ctrl+Shift+F` / `Ctrl+Shift+B` |
| Auto-advance (playback to end) | `Ctrl+Shift+Space` (toggle) |
| Accelerate / decelerate playback | `Ctrl+Up` / `Ctrl+Down` (125 ms steps) |

Next/Previous Event and Spike only move between *activated* items: select the events or clusters in the palette, then `Ctrl+Alt`+click them (the squares turn into triangles). `Ctrl+Alt`+click on a file ID activates all its selected items.

---

## Trace files and overlays

A display shows one trace file — the file that was opened (`.dat`, `.lfp`, `.fil` or any other extension). There is no layer menu.

**File → Load Overlay Trace File…** adds a second `.dat`, `.lfp` or `.eeg` file with the same channel layout, drawn on top of each channel in a contrasting colour (for example the wideband signal over the LFP).

---

## Spectral view

**Traces → Spectral View** (`S`) replaces the traces of the active display with a multitaper time-frequency image of the channels and window shown; `S` again returns to the traces. The *Spectral* palette tab sets the mode (*time-freq* for one channel, *band-chan* for one band across channels), tapers, window, FFT length, step, band, colour map and scale. Changes are staged: press `U` to recompute.

---

## Loading cluster files

Cluster files are loaded via **File → Load Cluster File(s)…** A cluster file must follow the naming convention `basename.clu.N` or `basename.N.clu` (where N is an integer electrode group number). NeuroScope locates the corresponding `.res.N` (timestamps) in the same directory.

### What gets loaded

| File | Role | Required |
|---|---|---|
| `session.clu.N` | Integer cluster ID per spike | Yes |
| `session.res.N` | Spike timestamps (sample indices) | Yes — inferred from `.clu.N` path |

The `.clu` and `.res` files may be binary or text (detected automatically; both of a pair must use the same form). Multiple cluster files can be loaded simultaneously (e.g. groups 1–8 at once). Each gets its own box in the *Units* palette, with independently selectable and colored units.

### Error conditions

- **Missing `.res.N`** — neuroscope reports a missing-file error and does not load the group.
- **Mismatched spike count** — if the number of spikes in `.res.N` and `.clu.N` differ, a count error is reported and the file is not loaded.
- **Duplicate load** — if the same `.clu.N` is already loaded, neuroscope reports it and does not load it again.

### Cluster display modes

Three rendering modes are available from the **Units** menu and can be combined freely:

| Mode | Menu item | Description |
|---|---|---|
| **Raster** (default on) | Units → Raster | Tick marks at each spike timestamp, one row per cluster, in a strip below the traces |
| **Vertical lines** | Units → Vertical Lines | Full-height colored vertical lines at each spike timestamp |
| **Waveforms** | Units → Waveforms | Each spike highlighted on the traces of its spike group (only when the trace file is `.dat` or `.fil`) |

Raster row height is adjustable with `Ctrl++` / `Ctrl+-` (**Units → Increase / Decrease Height**).

When browsing, the spike is placed at a fixed position in the window: **Preferences → General → Align units at … % of the view** (and *Align events at* for events).

### Cluster palette

The *Units* palette shows one colored square per cluster ID. Clicking selects the cluster for display; `Ctrl+Alt`+click activates it for Next/Previous Spike. Middle-click opens a color picker.

| Action | Shortcut |
|---|---|
| Select All | `Ctrl+A` |
| Select All except 0 and 1 | `Ctrl+Shift+A` (excludes noise/MUA) |
| Deselect All | `Ctrl+U` |

Cluster color assignments are saved to the session file and restored on reload.

### Closing a cluster file

**File → Close Cluster File** closes the currently active cluster panel.

---

## Waveform display

When **Units → Waveforms** is enabled, neuroscope highlights a window of the trace around each visible spike, on the channels of the spike group. Two parameters set the window:

- **nSamples** — total number of samples in the waveform window
- **peakSampleIndex** — 1-based index of the spike peak within that window

Both are read from the group's `spikeDetection` entry in the parameter file. They can be changed at runtime via **File → Properties** (*Units* tab) and take effect immediately for all loaded electrode groups.

Default values (used when the parameter file does not specify them) are configured in **Settings → Preferences → Defaults → Units**.

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

Event file names have the form `base.id.evt` or `base.evt.id`, where `id` is exactly three characters with at least one non-digit: `session.rip.evt`, `session.evt.s01`. Other names are rejected. Multiple event files can be loaded simultaneously; the `id` labels each one in the palette. See [evt.md](../ndmanager-plugins/formats/evt.md).

### Loading event files

**File → Load Event File(s)…** opens a multi-file picker. Each loaded event file gets its own box in the *Events* palette, with a distinct color per event description (label).

### Creating a new event file

**File → Create Event File…** prompts for a file name and creates an empty event file. Use the **Add Event** tool (**Tool → Add Event** or the toolbar button and its drop-down; no shortcut) to mark events interactively.

### Editing events

Editing requires **Edit Mode** (`Ctrl+E` or Edit menu).

| Action | Method |
|---|---|
| Add event | Choose the event type under **Tool → Add Event** (or the toolbar drop-down), click in the trace view at the target time |
| Move event | **Select Event** tool (`E`), drag an event marker to a new position |
| Remove event | **Select Event** tool (`E`), select the event, then **Events → Remove Event** (`Ctrl+K`) |
| Undo / Redo | `Ctrl+Z` / the platform Redo key (`Ctrl+Shift+Z` on Linux, `Ctrl+Y` on Windows) |

New event descriptions are entered with **New Event …** in the Add Event menu; existing descriptions appear in the Add Event toolbar dropdown.

### Event palette

Events are listed by description in the *Events* palette. Selecting a description displays it; `Ctrl+Alt`+click activates it for Next/Previous Event navigation (`Ctrl+F` / `Ctrl+B`). Event markers in the position view are toggled with **Positions → Show Events**.

---

## Position file (`.pos`)

**File → Load Position File…** loads an animal trajectory file. The `.pos` file is plain text: one line per video sample, with `x y` for each tracked spot (`x1 y1 x2 y2 …`); negative coordinates mark an undetected spot. See [pos.md](../ndmanager-plugins/formats/pos.md).

Once loaded, a **Position View** is docked in the display, showing the positions during the visible time window (**Positions → Show Position View** toggles it).

Position view parameters are set in **File → Properties** (*Positions* tab):

| Parameter | Description |
|---|---|
| Sampling rate | Video sampling rate of the `.pos` file |
| Width / height | Frame dimensions in pixels |
| Background image | Optional image rendered behind the positions |
| Draw trajectory | Draw the whole-session trajectory on the background |
| Rotation / flip | Orientation correction: 0°/90°/180°/270° with optional horizontal or vertical flip |

When event files are loaded and **Positions → Show Events** is checked, each selected event is drawn on the position view at the position where it occurred.

---

## Channel panel and amplitude controls

### Palettes

The palette area has two channel palettes:

- **Anatomy palette** — channels grouped by anatomical group. Controls what appears in the trace view and how it is laid out.
- **Spikes palette** — channels grouped by spike (electrode) group. Defines the groups written to the parameter file and used for waveform highlighting.

Both palettes support click/`Shift`/`Ctrl`+click selection, drag and drop between groups, and middle-click to change a channel or group color.

### Show / hide channels

| Action | Shortcut |
|---|---|
| Show selected channels | `Ctrl+C` |
| Hide selected channels | `Ctrl+H` |
| Move selected to new group | `Ctrl+G` |
| Remove selected from group | `Shift+Delete` |
| Discard channels (mark dead) | `Delete` |
| Keep channels (un-skip) | `Ctrl+Shift+K` |
| Skip channels (keep their place, do not draw) | Channels → Skip Channels (no shortcut) |
| Synchronize display and spike groups | Channels → Synchronize Groups |

### Amplitude and offset controls

| Action | Shortcut |
|---|---|
| Increase all channel amplitudes | `Ctrl+I` (or `Ctrl+Shift`+wheel up) |
| Decrease all channel amplitudes | `Ctrl+D` (or `Ctrl+Shift`+wheel down) |
| Increase selected channel amplitudes | `Ctrl+Shift+I` |
| Decrease selected channel amplitudes | `Ctrl+Shift+D` |
| Reset selected channel offsets to the defaults | Traces → Reset Selected Channel Offsets |
| Reset selected channel amplitudes | Traces → Reset Selected Channel Amplitudes |
| Set current offsets as defaults (stored in the parameter file) | Traces → Set Current Offsets as Defaults |
| Zero all default offsets | Traces → Set Default Offsets to Zero |
| Autocenter channels (subtract window mean) | Traces → Autocenter Channels (toggle, no shortcut) |

### Color modes

- **Color by Anatomical Groups** — channels in the same anatomical group share a color (Channels menu).
- **Color by Spike Groups** — channels are colored by electrode group (Channels menu).

Both modes propagate to the channel palette coloring.

---

## Display options

| Option | Menu / Shortcut | Notes |
|---|---|---|
| Multiple columns | Traces → Multiple Columns | One column per anatomical group |
| Grey-scale | Traces → Grey-Scale | Renders traces in grey-scale — makes highlighted waveforms stand out |
| Spectral view | Traces → Spectral View (`S`) | See [Spectral view](#spectral-view) |
| Show labels | Traces → Show Labels (`Ctrl+L`) | Channel ID and amplitude next to each trace |
| Display calibration | Settings → Display Calibration | Draws time and voltage scale bars |
| Background image | File → Properties | Static image behind all traces |

---

## Multiple display tabs

Each **Display** tab has its own independent time position, zoom level, visible channel set, selected clusters, selected events, and per-channel gain/offset state. All tabs share the same loaded files.

| Action | Description |
|---|---|
| Displays → New Display (`Ctrl+N`) | Opens a new tab cloned from the active display |
| Displays → Rename Active Display (`Ctrl+R`) | Edits the active tab label |
| Displays → Close Active Display (`Ctrl+W`) | Closes the active tab |

---

## Printing

**File → Print** (`Ctrl+P`) sends the current display to the Qt print dialog in landscape orientation; choose *Print to File (PDF)* to export. All visible layers (traces, clusters, events) are included, and a position view is printed on its own page. To print on a white background, check **Preferences → General → Use white background when printing**.

---

## Saving

**File → Save** (`Ctrl+S`) writes:

- the parameter file `session.yaml` — acquisition settings, sampling rates (including per-extension rates), anatomical and spike groups, skipped channels, and the `neuroscope` section (colors, default offsets, screen gain, video settings). Other sections of an existing file are kept;
- any modified event files;
- the session file `session.nrs`.

**File → Save As** (`Ctrl+Shift+S`) writes the session file to a new location. The session file is also written automatically on close when nothing else changed.

---

## Session file (`.nrs`)

The session file captures the full viewer state. On next open, neuroscope restores the loaded files, colors, channel positions, and time window for every display tab.

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
        - fileUrl: "/path/to/session.rip.evt"
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

NeuroScope reads display defaults from the `neuroscope` block of the session parameter file (e.g. `session.yaml` created by ndmanager). The waveform window comes from `spikeDetection`, and the video size and rate from the top-level `video` block.

```yaml
video:                        # top-level, shared with ndmanager
  samplingRate: 39.0          # video sampling rate (Hz)
  width: 320
  height: 240
neuroscope:
  miscellaneous:
    screenGain: 0.2           # mV/cm at amplitude x1
    traceBackgroundImage: ""  # optional path to a background image
  video:
    rotate: 0                 # 0 | 90 | 180 | 270
    flip: 0                   # 0=none  1=vertical  2=horizontal
    positionsBackground: 0    # 1 = draw the trajectory on the background
    videoImage: ""            # optional background image for the position view
  channels:
    colors:
      - channel: 0
        color: "#ff0000"
        anatomyColor: "#ff0000"
        spikeColor:   "#ff0000"
    offsets:
      - channel: 0
        defaultOffset: 0
  spikes:
    nSamples: 32
    peakSampleIndex: 16
```

---

## Preferences

**Settings → Preferences** opens the global preferences dialog:

| Page | Settings |
|---|---|
| General | Background color; palette tab labels; *Align events at* / *Align units at … % of the view*; *Use white background when printing* |
| Defaults | *Channels*: acquisition defaults (channels, sampling rates, resolution, offset, voltage range, amplification, screen gain, trace background image); *Units*: default waveform samples and peak index; *Positions*: video defaults |
| Appearance | Theme (System / Light / Dark) — applies to all NeuroSuite programs |
| Input | Keyboard and mouse bindings, with named keymap layouts |

---

## Keyboard shortcuts

**Help → Keyboard Shortcuts…** lists every keyboard and mouse binding currently in effect. Bindings are changed on **Settings → Preferences → Input**. The tools have single-key shortcuts: `Z` Zoom, `L` Draw Time Line, `C` Select Channels, `V` Measure, `T` Select Time, `E` Select Event; `S` toggles the spectral view and `U` recomputes it.

---

## Installation

NeuroScope is built and installed with the rest of neurosuite-3; follow [doc/INSTALL.md](../INSTALL.md).
