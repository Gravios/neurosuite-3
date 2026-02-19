# klusters — Spike Sorting GUI

Klusters is an interactive manual spike-sorting application. It loads the feature, cluster, spike-waveform, and parameter files for one electrode group at a time and lets the researcher inspect, split, merge, and reassign clusters across multiple synchronised views. All changes are fully undoable, and a background autosave thread writes crash-recovery copies on a configurable schedule.

---

## Launching

```bash
# Open a specific electrode group directly
klusters session.fet.1

# Open via the XML parameter file — klusters will ask which electrode group to load
klusters session.xml

# Relative or absolute paths are both accepted
klusters /data/rat07-2012-03-16/rat07-2012-03-16.fet.3
```

When opened from a `.fet.N` file, klusters automatically locates the paired `.clu.N`, `.spk.N`, and `.xml` (or legacy `.par.N` / `.par`) files in the same directory. If both `.xml` and `.par`-format parameter files are found, the `.xml` takes precedence with a warning.

---

## Files read and written

| File | Role |
|---|---|
| `session.fet.N` | **Required.** PCA feature vectors — primary input |
| `session.clu.N` | Cluster assignments — read on open, **overwritten on save** |
| `session.spk.N` | Spike waveforms — read for the Waveform View |
| `session.xml` | Session parameters (nChannels, samplingRate, waveform geometry) |
| `session.par.N` / `session.par` | Legacy parameter files used when no `.xml` exists |

Saving writes only the `.clu.N` file. Cluster metadata (structure, type, isolation distance, quality, notes) is written back into the `.xml` `<units>` block.

---

## Interface overview

The workspace is organised into **displays** — tabbed panels each containing one or more docked views. Multiple displays can be open simultaneously; all reflect the same underlying data and update together. Displays can be added from the **Displays** menu, renamed with `Ctrl+R`, and closed with `Ctrl+W`.

The **cluster palette** on the left lists every cluster with a colour swatch and visibility checkbox. Cluster 0 is artefacts; cluster 1 is unsorted noise. Click a swatch to change its colour. When **Settings → Show Cluster Info** is enabled, each entry also shows editable metadata fields (double-click to edit).

---

## Views

### Cluster View

A 2-D scatter plot of feature space. Each spike is a point coloured by cluster membership. The X and Y axes are selected from spinboxes in the parameter bar — dimension indices correspond to columns in the `.fet.N` file, with the last column always being the spike timestamp. Selecting a time-axis dimension draws regular vertical grid lines that reveal cluster drift.

**Interaction modes** (Tools menu or toolbar):

| Tool | Key | Action |
|---|---|---|
| Zoom | `Z` | Rubber-band zoom; right-click to zoom back out |
| New Cluster | `C` | Draw a polygon; spikes inside become a new cluster |
| Split Clusters | `S` | Draw a polygon; spikes inside vs. outside of the selected clusters become two new clusters |
| Delete Noisy Spikes | `N` | Lasso spikes and move them to cluster 1 (noise) |
| Delete Artefact Spikes | `A` | Lasso spikes and move them to cluster 0 (artefact) |
| Select Time | `W` | Draw a time window; the Trace View scrolls to that interval |

Scatter point size is adjusted globally with `+` / `-`.

### Waveform View

Shows spike waveforms for each selected cluster superimposed in the cluster's colour. Channels are arranged vertically according to the probe layout set in Preferences.

**Presentation modes:**

- **Sample mode** (default) — a fixed number of randomly sampled spikes per cluster; count set in the parameter bar spinbox
- **Time frame mode** (`T`) — only spikes within the current time window; start and duration set in the parameter bar

**Display options** (Waveforms menu):

| Option | Key | Effect |
|---|---|---|
| Overlay | `O` | All clusters drawn on top of each other; toggle for side-by-side |
| Mean and Std Dev | `M` | Show mean ± 1 SD ribbon instead of individual traces |
| Increase Amplitude | `I` | Scale waveform gain up |
| Decrease Amplitude | `D` | Scale waveform gain down |

### Correlation View

Auto- and cross-correlograms for all pairs of selected clusters, displayed as a matrix of histograms. Bin size and half-time-frame are set in the parameter bar; the constraint `total_window = (2k+1) × bin_size` is enforced automatically.

**Scaling modes** (Correlations menu):

| Mode | Key | Effect |
|---|---|---|
| Scale by Maximum | `Shift+M` | Each correlogram normalised to its own peak |
| Scale by Asymptote | `Shift+A` | Normalised to the shoulder (firing rate baseline) |
| Uniform Scale | `Shift+U` | Raw counts, common y-axis across all panels |
| Asymptote Line | `L` | Draw a dotted reference line at the shoulder level |

Correlogram amplitude: `Shift+I` / `Shift+D`.

### Overview Display

A single display containing all three views above (Cluster + Waveform + Correlation) docked together. The standard starting layout for a sorting session. Open via **Displays → New Overview Display**.

### Grouping Assistant Display

Extends the Overview with an **Error Matrix View** docked below the Cluster View. The Error Matrix computes, for every pair of clusters, the mean posterior probability that a spike assigned to cluster A actually belongs to cluster B under a Gaussian mixture model. High off-diagonal values flag candidate merge pairs. Press `U` (or **Actions → Update Error Matrix**) to recompute after any cluster modification.

### Trace View

Shows the raw filtered signal (`session.fil`) as scrollable multi-channel traces, with spikes from selected clusters overlaid as coloured tick marks at their timestamps. Open via **Displays → New Trace Display**.

- Adjust all channel amplitudes: `Ctrl+Shift+I` / `Ctrl+Shift+D`
- Toggle channel labels: `Ctrl+L`
- Navigate between spikes of the active cluster: `Ctrl+Shift+F` (next) / `Ctrl+Shift+B` (previous)

Requires `session.fil` to be present. When a Cluster View has the time dimension on one axis, using the **Select Time** tool (`W`) in that view scrolls the Trace View to the selected interval.

---

## Cluster operations

All operations are undoable (`Ctrl+Z`) up to the configured undo depth.

| Action | Shortcut | Effect |
|---|---|---|
| Delete Artifact Cluster(s) | `Shift+Del` | Merge selected clusters into cluster 0 |
| Delete Noisy Cluster(s) | `Del` | Merge selected clusters into cluster 1 |
| Group Clusters | `G` | Merge all selected clusters into one new cluster |
| Update Display | — | Redraw all views (delayed-update mode only) |
| Renumber Clusters | `R` | Assign consecutive IDs starting from 2; save automatically |
| Update Error Matrix | `U` | Recompute misclassification probabilities |
| Recluster | `Shift+R` | Run external reclustering program on selected clusters |
| Abort Reclustering | — | Kill the running reclustering process |

**Select All** (`Ctrl+A`) selects all clusters. **Select All Except 0 and 1** (`Ctrl+Shift+A`) selects all user-defined clusters — use this before **Recluster** to re-run KlustaKwik on the entire sorting.

---

## Reclustering integration

Klusters can launch an external executable (by default `KlustaKwik`) on any selected subset of clusters without leaving the application. Configure the executable path and arguments in **Settings → Preferences**.

On triggering recluster (`Shift+R`):

1. A new `.fet` file is written containing only spikes from the selected clusters.
2. The configured executable is launched as a child process; its output streams into a Process View in a new display tab.
3. On completion, the new `.clu` assignments are read back and all open displays update.

---

## Selection modes

- **Immediate Update** (default) — every palette change or tool action redraws all views instantly. Good for datasets up to ~500 k spikes.
- **Delayed Update** — changes are batched until **Actions → Update Display**. Use for large datasets where redraws are slow.

Toggle via **Settings → Immediate Update** / **Delayed Update**.

---

## Saving

| Action | Shortcut | Effect |
|---|---|---|
| Save | `Ctrl+S` | Overwrite `session.clu.N` in a background thread |
| Save As | — | Write to a new file path |
| Renumber and Save | `Ctrl+Shift+S` | Renumber clusters consecutively then save |

The autosave thread (configurable interval) writes a crash-recovery copy to a temporary file. If klusters detects a recovery file on startup it offers to restore it.

---

## Full keyboard shortcut reference

### File
| Key | Action |
|---|---|
| `Ctrl+O` | Open |
| `Ctrl+I` | Import (legacy format) |
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Renumber and Save |
| `Ctrl+P` | Print active display |
| `Ctrl+W` | Close active display |
| `Ctrl+Q` | Quit |

### Edit
| Key | Action |
|---|---|
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Ctrl+A` | Select All |
| `Ctrl+Shift+A` | Select All Except 0 and 1 |

### Tools (Cluster View)
| Key | Action |
|---|---|
| `Z` | Zoom mode |
| `C` | New Cluster |
| `S` | Split Clusters |
| `A` | Delete Artefact Spikes |
| `N` | Delete Noisy Spikes |
| `W` | Select Time |
| `+` | Increase scatter point size |
| `-` | Decrease scatter point size |

### Actions
| Key | Action |
|---|---|
| `Shift+Del` | Delete Artifact Cluster(s) |
| `Del` | Delete Noisy Cluster(s) |
| `G` | Group Clusters |
| `R` | Renumber Clusters |
| `U` | Update Error Matrix |
| `Shift+R` | Recluster |

### Waveforms
| Key | Action |
|---|---|
| `T` | Toggle Time Frame / Sample mode |
| `O` | Toggle Overlay / Side-by-side |
| `M` | Toggle Mean+SD / All waveforms |
| `I` | Increase waveform amplitude |
| `D` | Decrease waveform amplitude |

### Correlations
| Key | Action |
|---|---|
| `Shift+M` | Scale by Maximum |
| `Shift+A` | Scale by Asymptote |
| `Shift+U` | Uniform Scale |
| `L` | Toggle Asymptote Line |
| `Shift+I` | Increase correlogram amplitude |
| `Shift+D` | Decrease correlogram amplitude |

### Traces
| Key | Action |
|---|---|
| `Ctrl+Shift+I` | Increase all channel amplitudes |
| `Ctrl+Shift+D` | Decrease all channel amplitudes |
| `Ctrl+Shift+F` | Next spike |
| `Ctrl+Shift+B` | Previous spike |
| `Ctrl+L` | Show / hide channel labels |

### Display management
| Key | Action |
|---|---|
| `Ctrl+R` | Rename active display tab |
| `Ctrl+W` | Close active display tab |
| `F1` | Open handbook |

---

## Preferences

Accessed via **Settings → Preferences**:

- **Number of undos** — undo stack depth (default 3)
- **Background colour** — applies to all views
- **Channel positions** — vertical layout of channels in Waveform and Trace views; should match the physical probe geometry
- **Reclustering executable** — path to the automatic clustering binary (e.g. `/usr/local/bin/KlustaKwik`)
- **Reclustering arguments** — flags passed to the reclustering executable, e.g. `-MinClusters 2 -MaxClusters 20`
- **Autosave** — enable crash-recovery autosave and set the interval in minutes

---

## Build

```bash
cmake -B build/klusters klusters -DCMAKE_BUILD_TYPE=Release
cmake --build build/klusters -j$(nproc)
sudo cmake --install build/klusters
```

Requires `libklustersshared` 2.0.0 (Qt6 build) installed first. See the top-level `README.md` for build order.

**Qt6 modules required:** `Core`, `Gui`, `Widgets`, `Xml`, `PrintSupport`
