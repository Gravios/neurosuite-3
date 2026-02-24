# neuroscope — Signal Visualiser

NeuroScope is a Qt6 viewer for multi-channel electrophysiology data. It displays wideband, LFP, and high-pass filtered signals as scrollable traces and simultaneously overlays spike waveforms, cluster assignments, event markers, and animal position — all driven from the session `.xml` parameter file.

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
| `session.xml` / `session.yaml` | Parameter file — channel count, sampling rates, electrode groups |
| `session.dat` | Wideband signal (int16, channel-interleaved, no header) |
| `session.lfp` | LFP signal (same format as `.dat`, lower sampling rate) |
| `session.fil` | High-pass filtered signal (same format as `.dat`) |
| `session.spk.N` | Spike waveforms for electrode group N |
| `session.res.N` | Spike timestamps for electrode group N (sample indices) |
| `session.clu.N` | Cluster assignments for electrode group N |
| `session.evt` | Event file (ms timestamps + labels) |
| `session.pos` | Animal position (x,y per video frame) |

---

## Interface overview

The main window shows multi-channel traces as a vertically stacked scrollable panel. The horizontal axis is time; the vertical axis stacks channels according to the order and grouping defined in the parameter file.

**Navigation:** scroll with the scrollbar or arrow keys; zoom the time axis with `+` / `-` or the mouse wheel; jump to a time by typing in the toolbar; page through with `Page Up` / `Page Down`; jump to events with `Ctrl+Right` / `Ctrl+Left`.

**Signal layers** — each toggled from the View menu:

- Wideband traces (`.dat`) — full-bandwidth signal at acquisition rate
- LFP traces (`.lfp`) — downsampled local field potential
- High-pass traces (`.fil`) — median-filtered signal used for spike detection
- Spikes and clusters (`.spk.N`, `.clu.N`, `.res.N`) — waveforms drawn at detected timestamps, coloured by cluster
- Events (`.evt`) — vertical lines with labels; multiple files supported
- Animal position (`.pos`) — docked trajectory panel with a moving position dot

**Channel panel** — left-hand panel for showing/hiding individual channels or groups, reordering by drag-and-drop, and rescaling individual channel gains.

See [ndmanager-plugins/README.md](../ndmanager-plugins/README.md) for byte-level file format descriptions.

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
