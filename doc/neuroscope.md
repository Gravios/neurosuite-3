# neuroscope — Signal Visualiser

NeuroScope is a Qt6 viewer for multi-channel electrophysiology data. It displays wideband, LFP, and high-pass filtered signals as scrollable traces, and simultaneously overlays spike waveforms, cluster assignments, event markers, and animal position — all driven from the session `.xml` parameter file.

---

## Launching

```bash
# Open via the parameter file (preferred — loads all associated files automatically)
neuroscope session.xml

# Open directly from any signal file
neuroscope session.dat
neuroscope session.lfp

# Open from a spike file to inspect a specific electrode group
neuroscope session.spk.1
```

NeuroScope automatically searches the same directory for all companion files (`.lfp`, `.fil`, `.spk.N`, `.clu.N`, `.evt`, `.pos`) and loads whichever it finds.

---

## Display overview

The main window shows multi-channel traces as a vertically stacked scrollable panel. The horizontal axis is time; the vertical axis stacks channels according to the order and grouping defined in the `.xml`. Channels from the same electrode group share a colour by default.

**Navigation controls:**

- Scroll horizontally with the scrollbar or arrow keys
- Zoom in/out on the time axis with `+` / `-` or the mouse wheel
- Jump to a specific time by typing in the time box in the toolbar
- Page through the recording with `Page Up` / `Page Down`
- Jump to the next or previous event with `Ctrl+Right` / `Ctrl+Left`

---

## Signal layers

Multiple signal types can be displayed simultaneously as overlapping layers. Each layer is toggled from the **View** menu or the corresponding toolbar button.

### Wideband traces (`.dat`)

The full-bandwidth signal at the acquisition sampling rate. Each channel is drawn as a trace; amplitude is controlled by the global gain slider in the toolbar. Individual channel gains can be adjusted in the channel list panel.

### LFP traces (`.lfp`)

The downsampled local field potential signal. Displayed at the LFP sampling rate (typically 1250 Hz). LFP and wideband can be displayed simultaneously in separate panels or overlaid.

### High-pass traces (`.fil`)

The median-filtered high-pass signal used for spike detection. Useful for verifying that threshold crossings are genuine spikes rather than artefacts.

### Spikes and clusters (`.spk.N`, `.clu.N`, `.res.N`)

Spike waveforms are drawn as coloured overlays on the traces at their detected timestamps, coloured by cluster assignment. Clusters can be shown or hidden individually from the cluster palette panel.

### Events (`.evt`)

Event timestamps are drawn as vertical lines across all channels. Multiple `.evt` files can be loaded; each is assigned a distinct colour. Hovering over an event line shows its label in the status bar.

### Animal position (`.pos`)

A small position plot showing the animal's trajectory over the full session can be opened as a docked panel. The current playback position is indicated in the trajectory plot as a moving dot.

---

## Channel and group controls

The left-hand channel list panel shows every channel with its colour swatch and group membership. From this panel you can:

- **Show / hide** individual channels or entire electrode groups
- **Reorder** channels by drag-and-drop (affects display only, not the underlying files)
- **Rescale** individual channels by right-clicking and adjusting gain
- **Reset** all channels to the default gain

---

## Printing

**File → Print** prints the current view to a printer or PDF. For multi-panel displays, each panel is printed on a separate page. The file path, current time position, and display parameters are printed as a footer.

---

## File formats consumed

| File | Content |
|---|---|
| `session.xml` | Parameter file — channel count, sampling rates, electrode groups |
| `session.dat` | Wideband signal (int16, channel-interleaved, no header) |
| `session.lfp` | LFP signal (same format as `.dat`, lower sampling rate) |
| `session.fil` | High-pass filtered signal (same format as `.dat`) |
| `session.spk.N` | Spike waveforms for electrode group N |
| `session.res.N` | Spike timestamps for electrode group N (sample indices) |
| `session.clu.N` | Cluster assignments for electrode group N |
| `session.evt` | Event file (ms timestamps + labels) |
| `session.pos` | Animal position (x,y per video frame) |

See [doc/ndmanager-plugins.md](ndmanager-plugins.md) for byte-level format descriptions.

---

## Build

```bash
cmake -B build/neuroscope neuroscope -DCMAKE_BUILD_TYPE=Release
cmake --build build/neuroscope -j$(nproc)
sudo cmake --install build/neuroscope
```

**Qt6 modules required:** `Core`, `Gui`, `Widgets`, `Xml`

Does not depend on `libklustersshared`. See the top-level `README.md` for the overall build order.
