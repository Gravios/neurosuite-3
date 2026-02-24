# klusters — Spike Sorting GUI

Klusters is an interactive manual spike-sorting application. It loads the feature, cluster, spike-waveform, and parameter files for one electrode group at a time and lets the researcher inspect, split, merge, and reassign clusters across multiple synchronised views. All changes are fully undoable, and a background autosave thread writes crash-recovery copies on a configurable schedule.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml, PrintSupport) | UI framework | Yes |
| libklustersshared | Shared Qt6 widget library | Yes — build first |

`libklustersshared` is not available as a distro package and must be built from source before klusters.

---

## Launching

```bash
# Open a specific electrode group directly
klusters session.fet.1

# Open via parameter file — klusters will ask which group to load
klusters session.xml
klusters session.yaml
```

When opened from a `.fet.N` file, klusters automatically locates the paired `.clu.N`, `.spk.N`, and `.xml` (or `.yaml`) files in the same directory.

---

## Files read and written

| File | Role |
|---|---|
| `session.fet.N` | **Required.** PCA feature vectors — primary input |
| `session.clu.N` | Cluster assignments — read on open, **overwritten on save** |
| `session.spk.N` | Spike waveforms — read for the Waveform View |
| `session.xml` / `session.yaml` | Session parameters (nChannels, samplingRate, waveform geometry) |

Saving writes only the `.clu.N` file. Cluster metadata (structure, type, isolation distance, quality, notes) is written back into the parameter file `<units>` block.

---

## Views

**Cluster View** — 2D scatter plot of feature space. X and Y axes are freely selectable feature dimensions. Tools: Zoom (`Z`), New Cluster (`C`), Split (`S`), Delete Noisy Spikes (`N`), Delete Artefact Spikes (`A`), Select Time (`W`).

**Waveform View** — spike waveforms coloured by cluster. Sample mode (random subset) or time-frame mode (`T`). Toggle overlay/side-by-side with `O`, mean±SD with `M`.

**Correlation View** — auto- and cross-correlograms as a matrix of histograms. Scaling: by maximum (`Shift+M`), by asymptote (`Shift+A`), or uniform counts (`Shift+U`).

**Trace View** — raw filtered signal (`.fil`) with spike tick marks. Requires `session.fil` to be present.

**Grouping Assistant** — adds an Error Matrix showing cluster pair misclassification probabilities. Update with `U`.

---

## Key cluster operations

| Action | Shortcut |
|---|---|
| Group (merge) selected clusters | `G` |
| Delete as artefact | `Shift+Del` |
| Delete as noise | `Del` |
| Renumber clusters | `R` |
| Undo | `Ctrl+Z` |
| Save | `Ctrl+S` |
| Recluster (external program) | `Shift+R` |

**Select All Except 0 and 1** (`Ctrl+Shift+A`) selects all user-defined clusters — use this before **Recluster** to re-run KlustaKwik on the entire sorting.

---

## Reclustering integration

Configure the executable path and arguments in **Settings → Preferences**. On triggering recluster (`Shift+R`):

1. A `.fet` file is written containing only spikes from selected clusters.
2. The configured executable launches as a child process; output streams into a Process View tab.
3. On completion, new `.clu` assignments are read back and all views update.

---

## Preferences

**Settings → Preferences:**

- **Undo depth** (default 3)
- **Background colour**
- **Channel positions** — vertical layout in Waveform and Trace views; should match the physical probe geometry
- **Reclustering executable** — e.g. `/usr/local/bin/KlustaKwik`
- **Reclustering arguments** — e.g. `-MinClusters 2 -MaxClusters 20`
- **Autosave** — enable and set interval in minutes

---

## Full keyboard reference

### Tools (Cluster View)
`Z` zoom · `C` new cluster · `S` split · `A` delete artefact spikes · `N` delete noisy spikes · `W` select time · `+`/`-` scatter point size

### Actions
`G` group · `Del` delete as noise · `Shift+Del` delete as artefact · `R` renumber · `U` update error matrix · `Shift+R` recluster

### Waveforms
`T` time-frame/sample toggle · `O` overlay toggle · `M` mean+SD toggle · `I`/`D` amplitude up/down

### Correlations
`Shift+M` scale by max · `Shift+A` scale by asymptote · `Shift+U` uniform scale · `L` asymptote line · `Shift+I`/`Shift+D` amplitude

### Traces
`Ctrl+Shift+I`/`D` all-channel amplitude · `Ctrl+Shift+F`/`B` next/previous spike · `Ctrl+L` channel labels

### File and display
`Ctrl+O` open · `Ctrl+S` save · `Ctrl+Shift+S` renumber and save · `Ctrl+Z` undo · `Ctrl+Shift+Z` redo · `Ctrl+A` select all · `Ctrl+Shift+A` select all except 0 and 1 · `Ctrl+W` close display · `Ctrl+R` rename display

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
