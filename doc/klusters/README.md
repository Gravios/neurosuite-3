# klusters — Spike Sorting GUI

Klusters is an interactive manual spike-sorting application. It loads the feature, cluster, spike-waveform, and parameter files for one electrode group at a time and lets the researcher inspect, split, merge, and reassign clusters across multiple synchronised views. All changes are fully undoable, and a background autosave thread writes crash-recovery copies on a configurable schedule.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml, PrintSupport) | UI framework | Yes |
| libklustersshared | Shared YAML layer and widget library | Yes — build first |

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

When opened from a `.fet.N` file, klusters automatically locates the paired `.clu.N`, `.spk.N`, and `.xml` (or `.yaml`) parameter file in the same directory. Both XML and YAML formats are supported and detected from the file extension.

---

## Files read and written

| File | Role |
|---|---|
| `session.fet.N` | **Required.** PCA feature vectors — primary input |
| `session.clu.N` | Cluster assignments — read on open, **overwritten on save** |
| `session.spk.N` | Spike waveforms — read for the Waveform View |
| `session.xml` / `session.yaml` | Session parameters (nChannels, samplingRate, waveform geometry) |
| `session.res.N` | Spike timestamps — read when available; used by the Trace View |
| `session.dat` / `session.fil` | Raw signal — loaded by Trace View when present |
| `session.#.clu.N` | Autosave files written periodically by the background save thread |

---

## Views

Klusters supports multiple synchronised views that can be docked, floated, or closed independently.

### Cluster View (scatter plot)

Displays spike feature vectors as a 2-D scatter plot. The x/y axes can be assigned to any feature dimension via a pop-up menu. Points are coloured by cluster membership. Selections made here propagate instantly to all other views.

Multiple Cluster Views can be open simultaneously with different axis pairs, making it straightforward to assess cluster quality in several projection planes at once.

### Waveform View

Shows the mean ± SD waveform for each cluster on each channel of the electrode group. Overlays from multiple clusters can be enabled for direct comparison.

### Autocorrelogram / Cross-correlogram View

Displays per-cluster autocorrelograms and between-cluster cross-correlograms. Computed on a background thread to avoid blocking the UI.

### Error Matrix View

Evaluates cluster separation quality using the Grouping Assistant's probabilistic misclassification estimator (`GroupingAssistant::computeMeanProbabilities()`). Fills a cluster × cluster matrix where entry (c1, c2) is the mean posterior probability that a spike of cluster c1 actually belongs to c2. Clusters with high off-diagonal entries are poor candidates for merging and good candidates for further splitting.

### Trace View

Shows the raw wideband or high-pass filtered signal. Spike timestamps from the current group are overlaid as tick marks. Opens `session.dat` or `session.fil` using parameters from the session file.

---

## Cluster editing operations

All operations are undoable (Edit → Undo / Ctrl+Z) and redoable.

| Operation | How |
|---|---|
| Select clusters | Click or lasso in scatter plot; Ctrl+click for multi-select |
| Move spikes | Drag a lasso around spikes in scatter plot, then assign to target cluster |
| Merge clusters | Select two or more clusters, then Edit → Merge |
| Split a cluster | Select one cluster, draw a split boundary, Edit → Split |
| Delete spikes (→ noise) | Select spikes, Edit → Move to Noise (assigns to cluster 1) |
| Undo / Redo | Edit → Undo / Redo; full undo stack maintained for the session |

---

## Automatic reclustering (KlustaKwik)

Klusters can launch KlustaKwik on the current electrode group directly from the GUI:

1. Select the clusters to recluster (or select all).
2. Choose **Actions → Recluster** (or the toolbar button).
3. KlustaKwik runs as a subprocess using the parameters in **Settings → Preferences → KlustaKwik**. Its stdout streams into the embedded process widget.
4. When KlustaKwik finishes, klusters reloads the `.clu.N` file automatically and the new assignment is reflected in all views.

KlustaKwik parameters passed by klusters are set in the Preferences dialog. The executable path defaults to `KlustaKwik` on `PATH`.

---

## Spike realignment

Klusters includes interactive spike realignment via normalised cross-correlation, using the same algorithm as the standalone `SpikeRealign` tool. Configure the executable path and default arguments in **Settings → Preferences → General → Realignment**.

For batch realignment of entire sessions outside the GUI, use [SpikeRealign](../spikerealign/README.md) directly.

---

## Autosave and crash recovery

A configurable background thread periodically writes `session.#.clu.N` files (where `#` is the save index) so work is not lost on crash. The autosave interval and whether autosave is enabled are set in **Settings → Preferences → General**. On restart, klusters detects orphaned autosave files and offers to restore from them.

---

## GPU acceleration (Grouping Assistant)

The Error Matrix computation (`GroupingAssistant`) uses the same GPU dispatch mechanism as KlustaKwik. If a CUDA, HIP, or SYCL backend is available at build time, the posterior probability matrix is computed on the GPU. The CPU path (OpenMP) is always compiled as a fallback.

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
