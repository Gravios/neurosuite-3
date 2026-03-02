# neuroscope — Signal Visualiser

NeuroScope is a Qt6 viewer for multi-channel electrophysiology data. It displays wideband, LFP, and high-pass filtered signals as scrollable traces and simultaneously overlays spike waveforms, cluster assignments, event markers, and animal position. It reads both `.xml` and `.yaml` parameter files; YAML reading is handled by `NeuroscopeYamlReader`, which delegates to `ParameterYamlReader` in `libklustersshared`.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml) | UI framework | Yes |
| libxml2 | XML parameter file reading | Yes |
| yaml-cpp | YAML parameter file reading | Yes |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale) | Video display | Optional |

NeuroScope does **not** depend on `libklustersshared` and can be built independently of ndmanager and klusters.

---

## Launching

```bash
# Preferred — loads all associated files automatically
neuroscope session.xml
neuroscope session.yaml

# Open directly from any signal file
neuroscope session.dat
neuroscope session.lfp

# Open from a spike file to inspect a specific electrode group
neuroscope session.spk.1
```

NeuroScope searches the same directory for all companion files (`.lfp`, `.fil`, `.spk.N`, `.clu.N`, `.evt`, `.pos`) and loads whichever it finds.

---

## Files consumed

| File | Content |
|---|---|
| `session.xml` / `session.yaml` | Parameter file — channel count, sampling rates, electrode groups, display colours |
| `session.dat` | Wideband signal (int16, channel-interleaved, no header) |
| `session.lfp` | LFP signal (same binary format as `.dat`, lower sampling rate) |
| `session.fil` | High-pass filtered signal (same binary format as `.dat`) |
| `session.spk.N` | Spike waveforms for electrode group N |
| `session.res.N` | Spike timestamps for electrode group N (integer sample indices) |
| `session.clu.N` | Cluster assignments for electrode group N |
| `session.evt` | Event file (ms timestamps + labels, one per line) |
| `session.pos` | Animal position (x,y per video frame) |

All binary files (`.dat`, `.lfp`, `.fil`, `.spk.N`) are raw int16, channel-interleaved, with no header. The number of channels and sampling rates are taken from the parameter file.

---

## Interface overview

The main window shows multi-channel traces as a vertically stacked scrollable panel. The horizontal axis is time; the vertical axis stacks channels according to the order and grouping defined in the parameter file.

**Navigation:** scroll with the scrollbar or arrow keys; zoom the time axis with `+` / `-` or the mouse wheel; type a time into the toolbar to jump there; page with `Page Up` / `Page Down`; jump between events with `Ctrl+Right` / `Ctrl+Left`.

### Signal layers

Each layer is toggled independently from the View menu:

- **Wideband** (`.dat`) — full-bandwidth signal at acquisition rate.
- **LFP** (`.lfp`) — downsampled local field potential.
- **High-pass** (`.fil`) — median-filtered signal used for spike detection.
- **Spikes and clusters** (`.spk.N`, `.clu.N`, `.res.N`) — waveform snippets drawn at detected timestamps, coloured by cluster. Cluster colours are shared with klusters.
- **Events** (`.evt`) — vertical marker lines with text labels. Multiple event files from the same session are supported simultaneously.
- **Animal position** (`.pos`) — docked trajectory panel showing a moving dot at the animal's position for the currently displayed time window.

### Channel panel

The left-hand panel lists channels organised by anatomical group. Channels can be shown or hidden individually, reordered by drag-and-drop, and scaled with per-channel gain sliders. Group-level operations (hide/show all channels in a group, rescale group) are available from the context menu.

### YAML display settings

When opening a YAML parameter file, neuroscope reads display preferences from the `neuroscope` section:

- `neuroscope/miscellaneous/screenGain` — default vertical scale
- `neuroscope/miscellaneous/traceBackgroundImage` — optional background image path
- `neuroscope/video/rotate` and `flip` — video orientation for position overlay
- `neuroscope/channels/colors` — per-channel colours (wideband, anatomy group, spike group)
- `neuroscope/channels/offsets` — per-channel vertical offsets

On save, only these `neuroscope/` sub-keys are updated; the rest of the parameter file is preserved via `ParameterYamlModifier`.

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
