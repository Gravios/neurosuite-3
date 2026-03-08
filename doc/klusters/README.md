# klusters — Spike Sorting GUI

Klusters is an interactive manual spike-sorting application. It loads feature vectors, cluster assignments, spike waveforms, and session parameters for one electrode group at a time, and lets the researcher inspect, split, merge, and reassign clusters across multiple synchronised views. All editing operations are fully undoable, and a background autosave thread writes crash-recovery copies on a configurable schedule.

> **XML support has been removed.** Legacy `.xml` parameter files must be converted with `ndm_xml2yaml` (from ndmanager-plugins) before opening a session in klusters-3.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, PrintSupport) | UI framework | Yes |
| libklustersshared | Shared YAML reader/writer and widget library | Yes — build first |

`libklustersshared` is not available as a distro package. Build it from `src/libklustersshared/` before building klusters.

---

## Launching

```bash
# Open a specific electrode group directly
klusters session.fet.1

# Open via parameter file — klusters will ask which group to load
klusters session.yaml
```

When opened from a `.fet.N` file, klusters automatically locates the paired `.clu.N`, `.spk.N`, and `.yaml` parameter file in the same directory.

**Legacy formats still accepted at open time:**
- `.par` / `.par.N` — original KlustaKwik text parameter pair
- `.xml` — legacy neurosuite XML (read-only; save output will always be YAML)
- `.fet.N` in text format (auto-detected alongside binary format)

---

## Files read and written

| File | Role |
|---|---|
| `session.fet.N` | **Required.** Feature vectors — primary input (binary int64 or legacy text) |
| `session.clu.N` | Cluster assignments — read on open, **overwritten on save** |
| `session.spk.N` | Spike waveforms — optional; enables Waveform View |
| `session.yaml` | Session parameters (nChannels, samplingRate, waveform geometry, cluster notes) |
| `session.res.N` | Spike timestamps — used by Trace View for overlay tick marks |
| `session.dat` / `session.fil` | Raw / filtered signal — opened by Trace View |
| `session.pca.N` | PCA eigenvectors — used by spike realignment to reproject features |
| `session.#.clu.N` | Autosave (crash-recovery) files written periodically |

### Feature file format (`.fet.N`)

Binary format (neurosuite-3 default):
```
int32_t  nDimensions          (4-byte header)
nSpikes × nDimensions × int64_t   (row-major; last column is timestamp in samples)
```

Text format (legacy KlustaKwik):
```
nDimensions
feat_0 feat_1 … feat_n-1 timestamp
feat_0 feat_1 … feat_n-1 timestamp
…
```

klusters auto-detects the format by reading the first 4 bytes. Values 48–57 (ASCII digits) indicate text format; other values indicate binary. The last column in both formats is always the spike timestamp; only the preceding columns are used as clustering features.

---

## Interface layout

The window is divided into:

- **Left — Cluster palette**: coloured buttons, one per cluster ID. Shows/hides clusters, drives per-cluster colour editing, and optionally shows cluster metadata.
- **Centre — Display tabs**: one tab per open view. The default opening tab is the **Overview Display** (cluster scatter + waveform side by side).
- **Top — Toolbars**: Main toolbar (tools), Parameters bar (axis selectors, waveform/correlogram controls), Actions bar (recluster toggle, auto-select features), Cluster bar (next/previous spike).

---

## Cluster palette

Each cluster is represented by a coloured icon button. The palette drives everything: clicking buttons selects which clusters appear in the views.

### Selection

| Action | Method |
|---|---|
| Select one cluster | Click its button |
| Add cluster to selection | `S` key while focus is on that cluster's button |
| Pin current selection (accumulate) | `S` on each cluster in turn |
| Clear S-pin set (keep single visual) | `S` on an already-pinned cluster |
| Select All | `Ctrl+A` |
| Select All except 0 and 1 | `Ctrl+Shift+A` (skips noise/MUA) |
| Deselect All | `Ctrl+U` |

The `S` key (S-pin) is specifically designed to accumulate a working set without requiring Ctrl+click in the palette. The set of S-pinned clusters is unioned with the current visual selection; all views respond to the combined set.

### Colours

Double-click a cluster button to open a colour picker. Colours are saved to the YAML parameter file under the `units` section on save.

### Cluster information panel

**Settings → Show Cluster Info** (or `Settings` menu toggle) expands each cluster button to show five user-editable metadata fields:

| Field | Description |
|---|---|
| **Structure** | Brain region or anatomical label (e.g. `CA1`, `DG`) |
| **Type** | Unit type (e.g. `p`, `i` for pyramidal/interneuron) |
| **ID** | Free-form identifier |
| **Quality** | Sorting quality rating (e.g. `1`, `2`, `3`) |
| **Notes** | Free-form text |

Double-click a cluster button (when the info panel is visible) to edit any field via a dialog. Metadata is persisted in the YAML parameter file's `units` section alongside cluster colours.

---

## Views

Multiple views can be open simultaneously as tabs. Each view type can be opened from the **Displays** menu:

| View type | Menu | Description |
|---|---|---|
| Cluster Display | Displays → New Cluster Display | 2-D scatter plot of feature projections |
| Waveform Display | Displays → New Waveform Display | Per-cluster mean ± SD waveforms |
| Correlation Display | Displays → New Correlation Display | Auto- and cross-correlograms |
| Overview Display | Displays → New Overview Display | Scatter + waveform side by side (default on open) |
| Grouping Assistant | Displays → New Grouping Assistant Display | Misclassification probability matrix |
| Trace Display | Displays → New Trace Display | Raw signal with spike overlays |

All views are synchronised: selecting a cluster in any view highlights it in all others.

### Cluster Display (scatter plot)

Displays spike feature vectors as a 2-D scatter plot. The x and y axes are selected from drop-down menus in the **Parameters** toolbar.

- **Axis dimensions**: any pair of feature dimensions (0-indexed; the last dimension, which is the timestamp, is available as an axis and is useful for detecting drift).
- **Point size**: `=` / `Shift+=` to increase; `-` to decrease.
- **Lasso selection**: draw a polygon around spikes to select them for reassignment.
- **Zoom**: `Z` to activate zoom tool; scroll to zoom.
- **Time selection** (`W`): click and drag on the time axis to show only spikes within a time range.

Multiple Cluster Displays can be open simultaneously with different axis pairs, allowing quality assessment in several projection planes at once.

### Waveform Display

Shows spike waveforms for the selected clusters on all channels of the electrode group.

**Presentation modes** (Waveforms menu):

| Mode | Key | Description |
|---|---|---|
| Sample mode (default) | — | Shows the N most-recent spikes per cluster (N set in Parameters bar) |
| Time Frame mode | `T` | Shows all spikes from a time window set in the Parameters bar (start + duration in seconds) |
| Overlay | `O` | All clusters drawn in the same colour space on top of each other |
| Mean ± SD | `M` | Shows the cluster mean waveform with ±1 SD envelope; hides individual spikes |

**Amplitude controls** (Waveforms menu):

| Action | Key |
|---|---|
| Increase amplitude (all channels) | `I` |
| Decrease amplitude (all channels) | `D` |
| Increase selected channel amplitudes | `Ctrl+Shift+I` |
| Decrease selected channel amplitudes | `Ctrl+Shift+D` |

**Scale modes** affect how the y-axis is normalised across channels:

| Scale | Key | Description |
|---|---|---|
| Scale by Maximum | `Shift+M` | Each channel normalised to its global maximum across all displayed clusters |
| Scale by Asymptote | `Shift+A` | Each channel normalised to its asymptotic noise level (shoulder) |
| Uniform Scale | `Shift+U` | All channels share the same fixed scale |
| Shoulder Line | `L` | Toggles a dotted reference line at the shoulder level |

### Correlogram Display

Shows auto-correlograms (diagonal) and cross-correlograms (off-diagonal) for selected clusters.

**Parameters** (set in the Parameters toolbar):

| Parameter | Description |
|---|---|
| Bin size | Width of each correlogram bin in milliseconds |
| Half duration | Time span on each side of zero lag (total window = 2 × half duration) |

**Amplitude controls** (Correlations menu):

| Action | Key |
|---|---|
| Increase correlogram amplitude | `Shift+I` |
| Decrease correlogram amplitude | `Shift+D` |

**Scale modes** (Correlations menu):

| Scale | Description |
|---|---|
| Scale by Maximum | Normalise each correlogram to its own peak |
| Scale by Asymptote | Normalise to the flat baseline (shoulder) |
| Uniform Scale (Raw) | No normalisation — raw counts |

Correlograms are computed on a background thread and update asynchronously. `Actions → Update Display` forces a recompute.

### Overview Display

The default view on open. Shows the Cluster Display (scatter) and Waveform Display side by side in a single tab. All controls from both views are available.

### Grouping Assistant Display

Computes a cluster × cluster misclassification probability matrix using a probabilistic classifier (`GroupingAssistant::computeMeanProbabilities()`).

Entry (r, c) is the mean posterior probability that a spike from cluster r was actually generated by cluster c. The diagonal should be close to 1.0; high off-diagonal values indicate:

- High (r, c) alone → spikes of cluster r are frequently misclassified as cluster c (possible contaminant).
- High (r, c) **and** (c, r) symmetrically → the two clusters are strongly overlapping and are candidates for merging.

**Actions → Update Error Matrix** (`U`) recomputes the matrix after editing.

### Trace Display

Shows raw or high-pass filtered signal (`session.dat` or `session.fil`). Spike timestamps from the current electrode group are overlaid as coloured tick marks.

- Navigate with the start / duration fields in the Parameters toolbar.
- **Next Spike** (`Ctrl+Shift+F`) and **Previous Spike** (`Ctrl+Shift+B`) scroll the trace to the next or previous spike of the selected clusters.
- Select Time tool (`W`): click a time range in the trace view to restrict the Waveform Display to that window (Time Frame mode).
- Show Labels toggle (`Ctrl+L`): display channel names on the trace view.

---

## Cluster editing operations

All operations push onto the undo stack. The full session history is preserved until the file is closed.

| Operation | Shortcut / Menu | Description |
|---|---|---|
| Group (merge) clusters | `G` | Merges all selected clusters into the lowest-numbered one |
| Split clusters | `S` | Draw a lasso in the scatter plot; `S` assigns the enclosed spikes to a new cluster |
| New cluster from lasso | `C` | Creates a new cluster ID and assigns the lasso-enclosed spikes to it |
| Delete selected spikes → artefact (0) | `A` | Moves lasso-enclosed spikes to cluster 0 (artefact) |
| Delete selected spikes → noise (1) | `N` | Moves lasso-enclosed spikes to cluster 1 (noise/MUA) |
| Delete cluster(s) → artefact (0) | `Shift+Delete` | Moves all spikes of selected clusters to cluster 0 |
| Delete cluster(s) → noise (1) | `Delete` | Moves all spikes of selected clusters to cluster 1 |
| Undo | `Ctrl+Z` | Undoes the last editing operation |
| Redo | `Ctrl+Y` | Redoes the last undone operation |
| Renumber clusters | `R` | Reassigns cluster IDs to be contiguous starting from 2 (0 and 1 are always noise/artefact) |
| Renumber and Save | `Ctrl+Shift+S` | Renumbers then saves in one step |
| Update Display | — | Forces all views to recompute from current cluster assignments |

**Cluster 0** is the artefact pseudo-cluster (waveforms that are not spikes). **Cluster 1** is the noise/MUA pseudo-cluster. Both are always shown at the top of the palette. Spikes sent to these clusters are not deleted — they remain in the feature file and can be retrieved by undo.

---

## Saving

| Action | Shortcut | Description |
|---|---|---|
| Save | `Ctrl+S` | Overwrites `session.clu.N` in place |
| Save As | `Ctrl+Shift+A` | Saves to a new `.clu.N` path |
| Renumber and Save | `Ctrl+Shift+S` | Renumbers cluster IDs, then saves |

The cluster user information (structure, type, quality, etc.) is saved to the `units` section of the YAML parameter file.

---

## Update modes

**Settings → Immediate Update** (default): all views redraw immediately after every editing operation. Appropriate for small datasets.

**Settings → Delayed Update**: views are not updated until **Actions → Update Display** is triggered manually. Useful for large datasets where recomputing all views after every lasso operation is slow.

---

## Automatic reclustering (KlustaKwik)

Klusters can launch KlustaKwik on the current electrode group:

1. Select the clusters to recluster (or select all).
2. **Actions → Recluster** (`Shift+R`) or toolbar button.
3. KlustaKwik runs as a subprocess; its stdout streams into the embedded process widget.
4. When KlustaKwik finishes, klusters reloads the `.clu.N` file automatically and all views update.
5. **Actions → Abort Reclustering** cancels a running KlustaKwik process.

KlustaKwik parameters (executable path, UseFeatures, MaxClusters, etc.) are configured in **Settings → Preferences → KlustaKwik**.

### Automatic feature selection

When **Auto-select features** is enabled in the Preferences (and the **N feat** spinbox appears in the Parameters toolbar), klusters computes per-feature variance across all spikes in the selected clusters and passes only the top-N high-variance features to KlustaKwik via the `UseFeatures` parameter.

**Algorithm:**

1. Feature variances are pooled across all spikes from every selected cluster (clusters 0 and 1 are excluded even if selected).
2. Features are ranked by descending variance.
3. The **N feat** spinbox sets a ceiling on the number of features passed.
4. A variance drop-off threshold (5 % of the top feature's variance) trims features that have fallen to noise level — if only 4 of the requested 10 carry meaningful information, only those 4 are passed.
5. At least one feature is always selected.
6. The timestamp dimension (last column) is never included.

When no clusters are selected or auto-select is disabled, klusters falls back to passing all PCA feature dimensions (timestamp excluded).

---

## Spike realignment

**Actions → Realign Spikes…** (`Shift+L`) opens the realignment dialog for the selected clusters.

### How it works

For each spike in the selected clusters:

1. The cluster mean waveform is computed.
2. The spike's waveform is cross-correlated against the mean template at each lag within ±maxShift samples.
3. If the peak correlation exceeds `minScore`, the spike is shifted to the optimal lag: its `.res.N` timestamp is updated, the waveform snippet is re-extracted from `session.fil` or `session.dat` at the corrected sample offset, and PCA features are reprojected using the stored `.pca.N` eigenvectors.
4. If a timestamp shift would violate chronological order, the affected `.res`/`.spk`/`.clu`/`.fet` rows are swapped.

After realignment completes, the **Realignment Review Dialog** shows before/after mean waveforms and reports the number of spikes shifted and timestamps reordered. The result can be **accepted** (changes written to disk) or **rejected** (full in-memory rollback, no disk writes).

**Actions → Abort Realignment** cancels realignment in progress.

Realignment parameters (executable path and arguments) are set in **Settings → Preferences → General**.

---

## Autosave and crash recovery

A background thread periodically saves `session.#.clu.N` files (where `#` is a rotating index) to protect against crashes. The autosave interval is configured in **Settings → Preferences → General**.

On opening a session, klusters detects orphaned autosave files and offers to restore from the most recent one.

---

## Keyboard shortcuts reference

### File

| Action | Shortcut |
|---|---|
| Open | `Ctrl+O` |
| Import File | `Ctrl+I` |
| Save | `Ctrl+S` |
| Renumber and Save | `Ctrl+Shift+S` |
| Print | `Ctrl+P` |
| Quit | `Ctrl+Q` |

### Edit

| Action | Shortcut |
|---|---|
| Undo | `Ctrl+Z` |
| Redo | `Ctrl+Y` |
| Select All | `Ctrl+A` |
| Select All except 0 and 1 | `Ctrl+Shift+A` |

### Actions

| Action | Shortcut |
|---|---|
| Delete artifact cluster(s) | `Shift+Delete` |
| Delete noisy cluster(s) | `Delete` |
| Group (merge) clusters | `G` |
| Renumber clusters | `R` |
| Update error matrix | `U` |
| Recluster (KlustaKwik) | `Shift+R` |
| Realign spikes | `Shift+L` |

### Tools

| Action | Shortcut |
|---|---|
| Zoom | `Z` |
| New cluster from lasso | `C` |
| Split clusters | `S` |
| Delete artifact spikes (lasso) | `A` |
| Delete noisy spikes (lasso) | `N` |
| Select time | `W` |

### Waveforms

| Action | Shortcut |
|---|---|
| Time Frame mode | `T` |
| Overlay presentation | `O` |
| Mean ± SD presentation | `M` |
| Increase amplitude | `I` |
| Decrease amplitude | `D` |
| Scale by Maximum | `Shift+M` |
| Scale by Asymptote | `Shift+A` |
| Uniform Scale | `Shift+U` |
| Shoulder line toggle | `L` |

### Correlations

| Action | Shortcut |
|---|---|
| Increase correlogram amplitude | `Shift+I` |
| Decrease correlogram amplitude | `Shift+D` |

### Trace

| Action | Shortcut |
|---|---|
| Next spike | `Ctrl+Shift+F` |
| Previous spike | `Ctrl+Shift+B` |
| Show labels | `Ctrl+L` |

### Displays

| Action | Shortcut |
|---|---|
| Rename active display | `Ctrl+R` |
| Close active display | `Ctrl+W` |

### Settings

| Action | Shortcut |
|---|---|
| Increase point size | `=` or `Shift+=` |
| Decrease point size | `-` |
| Preferences | `P` |

---

## Parameters toolbar reference

The **Parameters** toolbar (Settings → Show Parameters) shows context-sensitive controls depending on the active view:

| Control | Context | Description |
|---|---|---|
| X / Y dimension selectors | Cluster View | Feature dimensions assigned to the scatter plot axes |
| Start (s) | Waveform View (Time Frame mode) | Beginning of the time window shown |
| Duration (s) | Waveform View (Time Frame mode) | Length of the time window shown |
| # spikes | Waveform View (Sample mode) | Maximum number of spikes displayed per cluster |
| Bin size (ms) | Correlogram View | Width of each correlogram bin |
| Half duration (ms) | Correlogram View | Half-width of the correlogram time window |
| N feat | All (when auto-select is on) | Maximum number of features passed to KlustaKwik |

---

## Printing

**File → Print** (`Ctrl+P`) prints the currently active view to the system print dialog.

---

## GPU acceleration

The Grouping Assistant matrix computation and the spike realignment cross-correlation use the same GPU dispatch as KlustaKwik: CUDA → HIP → SYCL → OpenMP CPU fallback. The CPU path is always compiled as a baseline.

See the [GPU installation guide](../gpu/README.md) for CUDA, ROCm/HIP, and Intel oneAPI/SYCL setup.

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
