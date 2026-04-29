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

Klusters transparently handles both pipeline variants via extension
probing at open time. When both a canonical file and its D variant
exist for a given group, the D variant (stderiv) is preferred. The
picked variant is remembered for the rest of the session so all edits
go back to the same format.

| File | Role |
|---|---|
| `session.fet.N` / `session.fetD.N` | **Required.** Feature vectors — primary input (binary int64 or legacy text) |
| `session.clu.N` | Cluster assignments — read on open, **overwritten on save** |
| `session.spk.N` / `session.spkD.N` | Spike waveforms — enables Waveform View. `.spkD.N` contains stderiv-transformed waveforms |
| `session.yaml` | Session parameters (nChannels, samplingRate, waveform geometry, cluster notes) |
| `session.res.N` | Spike timestamps — used by Trace View and by nudge/realign |
| `session.dat` / `session.fil` | Raw / filtered signal — opened by Trace View; read by nudge/realign for waveform re-extraction |
| `session.pca.N` / `session.pcaD.N` | PCA eigenvectors — used by spike realignment and nudge to reproject features after shifting timestamps |
| `session.{res,spk,fet,clu}.N.pending` | Transactional working copies — all realign/nudge edits go here first and are atomically renamed on save |
| `session.curation_log.N.jl` | **Append-only** JSON-line audit trail of every editing operation in this group's curation history. Schema in [Curation logging](#curation-logging). |
| `session.#.clu.N` | Autosave (crash-recovery) files written periodically |

### Pipeline variant detection

Two flags govern which transforms to apply during nudge / realign:

- **`spkIsTransformed`** = true iff the `.spk` file loaded has the
  `.spkD.` suffix. When writing a re-extracted waveform back, the
  stderiv transform is applied iff this flag is true (Pipeline D) and
  skipped otherwise (raw `.spk`; Pipelines A and C).
- **`fetIsStderiv`** = true iff the `.fet` file loaded has the
  `.fetD.` suffix. When reprojecting features from a re-extracted
  waveform, the stderiv transform is applied before the PCA projection
  iff this flag is true, and the `.pcaD.N` eigenvector basis is used
  rather than `.pca.N`.

These flags are independent. Pipeline C (raw `.spk` + stderiv
`.fetD`/`.pcaD`) is a real configuration that occurs when features
are recomputed from a raw `.spk` via `ndm_pca_stderiv` without
replacing the `.spk` file; klusters handles this correctly by
consulting the two flags separately.

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

### Focus handling after batch operations

After any operation that auto-selects the next cluster in the palette (delete-to-noise, delete-to-artefact, recluster completion, realign accept/reject), focus returns to the palette rather than to the 2-D scatter. This means arrow keys continue to step through clusters without requiring a Tab press to reach the palette.

The one exception is the cluster-editing slots whose natural follow-up is 2-D inspection (undo, redo, group-merge, nudge): these leave focus on the scatter view.

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
| Template Matrix | Displays → New Template Matrix Display | Pairwise waveform cross-correlation matrix |
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

**Actions → Update Error Matrix** (`U`) recomputes the matrix after editing. `U` also refreshes the Template Matrix view if both are open.

### Template Matrix Display

Pairwise waveform similarity matrix for the currently-shown clusters. Each cell (r, c) shows the normalised cross-correlation between the mean waveform of cluster r and the mean waveform of cluster c, summed across all channels of the electrode group.

- **High off-diagonal xcorr** (dark cells close to the diagonal value) identifies clusters whose mean waveforms are almost identical — strong merge candidates.
- **Selection highlighting is asymmetric**: clicking cell (r, c) highlights only that one cell, not the mirror cell (c, r). This lets you trace the direction of a putative merge (for example, when you suspect cluster r is a sub-population of cluster c).
- Pairwise xcorr is computed **on demand** — when a cell is first clicked, the corresponding pair is computed and cached. This keeps matrix open for large (50+) cluster sets cheap.
- The matrix auto-recomputes after Apply and after any cluster-editing operation that invalidates waveforms. Parallel computation via OpenMP.
- The noise cluster (cluster 1) is included in the matrix — useful for identifying clusters that have drifted into MUA.

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
| Split clusters | `2` (or `S` in palette focus) | Draw a lasso in the scatter plot; closing the polygon assigns the enclosed spikes to a new cluster |
| Auto bimodal split (DipSplit) | `Shift+D` | Tests the active cluster for bimodality on its top PCs; if a clean valley is found, splits into two new clusters automatically |
| Watershed split | `Shift+W` | Runs a 2D density watershed on the selected clusters in the active scatter view's X/Y dimensions; each density basin becomes one new cluster, leftover spikes (outside any peak) form a residual cluster |
| New cluster from lasso | `1` | Creates a new cluster ID and assigns the lasso-enclosed spikes to it |
| Delete selected spikes → artefact (0) | menu only (Tools → Delete Artifact Spikes) | Moves lasso-enclosed spikes to cluster 0 (artefact) |
| Delete selected spikes → noise (1) | menu only (Tools → Delete Noisy Spikes) | Moves lasso-enclosed spikes to cluster 1 (noise/MUA) |
| Delete cluster(s) → artefact (0) | `Shift+Delete` | Moves all spikes of selected clusters to cluster 0 |
| Delete cluster(s) → noise (1) | `Delete` | Moves all spikes of selected clusters to cluster 1 |
| Renumber selected to end | `T` (palette focus) | Reassigns the selected cluster IDs to be greater than the current maximum, moving them to the tail of the palette |
| Undo | `Ctrl+Z` | Undoes the last editing operation |
| Redo | `Ctrl+Y` | Redoes the last undone operation |
| Renumber clusters | `R` | Reassigns cluster IDs to be contiguous starting from 2 (0 and 1 are always noise/artefact) |
| Renumber and Save | `Ctrl+Shift+S` | Renumbers then saves in one step |
| Update Display | — | Forces all views to recompute from current cluster assignments |

**Cluster 0** is the artefact pseudo-cluster (waveforms that are not spikes). **Cluster 1** is the noise/MUA pseudo-cluster. Both are always shown at the top of the palette. Spikes sent to these clusters are not deleted — they remain in the feature file and can be retrieved by undo.

### DipSplit — automatic bimodal split

DipSplit (`D`) tests the active cluster for bimodality and splits it
when the test is clear. The pipeline:

1. Compute the cluster's top PCA directions in feature space.
2. Project members onto each top direction; run a kernel-density
   estimate on the 1D projection to detect a valley between two modes.
3. If a valley is found and is deeper than `DipSplit valley depth`
   (preference, default 0.3), refine the split via 2-means in the
   plane of the top two PCs.
4. Apply a **bloat gate**: re-fit a single Gaussian to the cluster and
   check that ≥10% of members fall outside the χ²(d, 0.9) ellipsoid.
   Bloated clusters split; tight ones do not. Disable the gate by
   setting `DipSplit bloat factor` to `0` (the default in Klusters).
5. Apply a BIC two-vs-one gate as a final sanity check.

If all gates pass, the new cluster is created via the same
`commitClusterCreation` plumbing as a manual lasso split, with full
undo support and source-cluster waveform/correlogram cache invalidation.
Every gate decision is recorded in the curation log (see below) for
later analysis.

Preferences live under **Settings → Preferences → General**:

| Preference | Default | Effect |
|---|---|---|
| `DipSplit min size` | 30 | Minimum cluster size to consider |
| `DipSplit bloat factor` | 0 | χ²(d, 0.9) bloat gate; `0` skips it |
| `DipSplit valley depth` | 0.3 | KDE valley depth threshold (0–1) |
| `Autoscale margin` | 5% | Whitespace around per-cluster autoscale (`F` key) |

DipSplit is intentionally conservative — it errs toward not splitting.
Manual lasso split (`S`) remains the primary tool for ambiguous cases.

### Watershed split — density-based 2D segmentation

Watershed (`W` with one or more clusters selected in the palette)
treats the selected spikes' (X, Y) feature-space coordinates — using
the *active scatter view's currently displayed dimensions* — as a 2D
point cloud, builds a density grid, and segments it into basins. Each
basin becomes one new cluster.

The pipeline:

1. Project every spike of every selected cluster onto (`dim_x`,
   `dim_y`) — the X/Y dimensions of the active scatter view at the
   time `W` was pressed. Pressing `W` from a different view runs in a
   different feature subspace.
2. Build a density histogram (default 256×256) and smooth with a
   Gaussian. The kernel sigma defaults to `max(1.5, gridSize / 32)`
   when left at the auto-tune sentinel.
3. Find 8-connected local maxima above `min_peak_height` (auto-tuned
   to 5% of the post-smoothing grid maximum if left at 0).
4. Flood-fill from each peak using a max-heap priority queue, marking
   watershed lines along ridges between basins.
5. Drop basins smaller than `min_basin_size` (auto-tuned to
   `max(20, N / 500)` if left at 0).
6. Map every input spike back to its basin via the cell its (X, Y)
   coordinate falls into.

Spikes that fall outside any basin (on the histogram floor of zero
density after smoothing, or rejected by the min-basin-size filter)
are routed into a **residual cluster** that lands at the tail of the
palette alongside the other new basin-clusters. The source clusters
are fully dissolved — the same renumber-to-tail behaviour as a
recluster (KlustaKwik) action.

Pressing `Shift+W` enters a **live-preview overlay mode**. The selected
clusters' (X, Y) feature points are extracted once, the kernel runs
against the active scatter view's current X/Y dimensions, and the
basin partition is rendered as a translucent coloured overlay
directly on top of the scatter — one colour per basin, transparent
where no basin claimed a cell. A small HUD at the top-left of the
view shows the current parameters and the per-basin spike counts.

While preview is active:

| Key | Action |
|---|---|
| `←` / `→` | Adjust smoothing sigma (1..32 cells, ±1 per press, ±5 with Shift) |
| `↑` / `↓` | Adjust peak threshold (0..50% of grid max, ±1 per press, ±5 with Shift) |
| `Enter` | Commit — apply the partition to the document |
| `Esc` | Cancel — discard the preview and return to normal |

All other keys are blocked while preview is active so you can't
accidentally trigger an unrelated action mid-tune. Preview parameters
persist between invocations within a session.

| Sentinel | Auto-tune resolution |
|---|---|
| `smoothSigma == 0` | `max(1.5, gridSize / 32)` cells |
| `minPeakHeight == 0` | 5% of post-smoothing grid maximum |
| `minBasinSize == 0` | `max(20, totalInputSpikes / 500)` |

Watershed is most useful for:
- Splitting a cluster that the scatter view shows as two visually
  distinct blobs but DipSplit declines to split (e.g. blobs of
  unequal size where the kernel-density valley isn't deep enough).
- Splitting after a coarse recluster pass that grouped multiple
  density modes into one cluster.
- Splitting clusters whose modal structure is only visible in
  specific feature pairs — the user picks the pair via the active
  view, then runs watershed.

Single-basin results (the watershed found one peak in the projection)
are silently no-ops: no new cluster is created and no log entry is
written. This avoids spurious "split into one cluster" actions when
the user picks a feature pair where the cluster is genuinely unimodal.

Internally, watershed routes through the same data-layer pipeline as
recluster (`Data::integrateBasinLabeling` calls into the same
post-load body as `integrateReclusteredClusters`). This means new
cluster IDs always land strictly after the current maximum, the
source cluster IDs are never reused, and a single Ctrl+Z reverts the
whole watershed in one step.

---

## Curation logging

Every editing operation in Klusters writes a JSON-line record to
`session.curation_log.<group>.jl` alongside the `.clu.N` file. The log
is the audit trail for both crash forensics and the empirical-prior
training pipeline (`kk_build_prior.py` in ndmanager-plugins; see
[that workflow](../workflows/empirical-priors.md)).

Each user-facing action emits, at minimum, a paired set of records:
one snapshot per affected cluster from `before` (pre-mutation state)
and one per affected cluster from `after` (post-mutation state),
both tagged with the same `action_idx`. Some actions additionally
emit `ACTION_DETAIL` records carrying algorithm-specific metadata
(see below).

### Top-level record schema

| Field | Description |
|---|---|
| `event` | `SESSION_OPEN`, `ANNOTATE`, `UNDO`, `REDO`, `ACTION_DETAIL`, or empty for action snapshots |
| `action` | The operation kind — see action type table below |
| `phase` | `before`, `after`, or empty (for events / details) |
| `role` | `result`, `source`, or empty |
| `action_idx` | Monotonic index — `before` and `after` records share an index, allowing pairing |
| `cluster_id` | Cluster touched by this record |
| `n_spikes`, `n_clusters_in_group` | State after the action |
| `n_pca_dims`, `n_feat_dims` | Feature-space dimensionality |
| `feat_var_dims` | Per-PCA-dim within-cluster variance |
| `feat_var_frobenius`, `feat_var_top3_mean`, `feat_var_mean` | Aggregate variance stats |
| `l_ratio`, `isolation_dist` | Isolation metrics |
| `nearest_centroid_dist_norm` | Mahalanobis distance to nearest neighbouring cluster |
| `waveform_snr`, `waveform_chan_spread`, `waveform_width_samp` | Waveform morphology |
| `isi_cv` | Inter-spike interval coefficient of variation |

### Action types

| `action` value | User-facing operation | Source clusters | Result clusters | Emits `ACTION_DETAIL`? |
|---|---|---|---|---|
| `GROUP` | Merge selected clusters (`G`) | N | 1 | No |
| `SPLIT` | Single new cluster from polygon, lasso, or DipSplit | 1 | 1 (new) + 1 (residual src) | DipSplit only |
| `SPLIT_N` | Multiple new clusters from polygon (one per source) | N | N (new) + N (residual srcs) | No |
| `RECLUSTER` | KlustaKwik re-run on selected clusters (`Shift+R`) | M | K | No |
| `WATERSHED` | 2D density watershed (`W`) | M | K (basins) + 1 (residual) | Yes |
| `REALIGN` | Spike waveform realignment (`Shift+L`) | unchanged | unchanged | No |
| `NUDGE` | Timestamp ±1 sample shift (`PageUp` / `PageDown`) | 1 | 1 (same id) | No |
| `DELETE_NOISE` | Move whole cluster → cluster 1 (`Delete`) | 1 | — | No |
| `DELETE_ARTEFACT` | Move whole cluster → cluster 0 (`Shift+Delete`) | 1 | — | No |
| `DELETE_REGION_NOISE` | Lasso → cluster 1 (`N`) | 1+ | — | No |
| `DELETE_REGION_ARTEFACT` | Lasso → cluster 0 (`A`) | 1+ | — | No |
| `MOVE_SPIKES` | Manual subset reassignment | 1 | 1 | No |
| `RENUMBER_PARTIAL` | Renumber selected to end (`T` in palette) | renamed | renamed | No |
| `UNDO`, `REDO` | Reverts/replays a previous action | — | — | No |

### `ACTION_DETAIL` records

These carry algorithm-internal state for actions that benefit from
per-decision metadata. Every `ACTION_DETAIL` record shares an
`action_idx` with the parent action's `before` / `after` records, so
joining is trivial in pandas.

**DipSplit** logs the inputs it was called with and the metrics that
drove the accept/reject decision:

```json
{"event":"ACTION_DETAIL","action_idx":47,"algorithm":"dipsplit",
 "source_cluster":12,"new_cluster":47,"reason":"bimodal_split",
 "min_size":30,"bloat_factor":0.0,"valley_thresh":0.3,
 "n_left":342,"n_right":418,"best_pc":0,"best_depth":0.51,
 "mahal2_p90":7.8,"chi2_90":7.78,"delta_bic":-12.4}
```

**Watershed** logs source/result IDs and counts, the feature-space
projection used, both requested and resolved (post-auto-tune)
parameters, and kernel diagnostics:

```json
{"event":"ACTION_DETAIL","action_idx":58,"algorithm":"watershed_2d",
 "source_clusters":"5,7,12","source_counts":"5:1842,7:2400,12:891",
 "total_input_spikes":5133,
 "dim_x":1,"dim_y":2,
 "grid_size":256,
 "smooth_sigma_req":0.0,"smooth_sigma_eff":8.0,
 "min_peak_height_req":0.0,"min_peak_height_eff":12.4,
 "min_basin_size_req":0,"min_basin_size_eff":20,
 "use_local_maxima":true,
 "num_peaks":5,"num_basins":4,
 "new_clusters":"47,48,49,50,51",
 "new_cluster_counts":"47:1320,48:980,49:1640,50:1193,51:0",
 "residual_present":false,"residual_count":0}
```

The req/eff split for `smooth_sigma`, `min_peak_height`, and
`min_basin_size` matters because the live-preview overlay defaults
each parameter to a 0 sentinel that requests auto-tuning from the
data; the resolved values capture what the kernel actually ran with.
`num_peaks` (pre-merge peak count) vs. `num_basins` (post-merge)
distinguishes "watershed found 12 peaks but `min_basin_size` collapsed
8 into neighbours" from "watershed only found 4 peaks total".

When the input has too few spikes (< 50) or the watershed finds only
one basin, the action is silently skipped — no records are written
for the no-op case.

### Logs are append-only

Logs are append-only within a session; reopening Klusters in the same
working copy continues the same log. Logs from many sessions on the
same shank can be combined into a single prior YAML via
`kk_build_prior.py`.

Annotating cluster quality (cluster info panel: Good / Uncertain / Bad)
emits an `ANNOTATE` event that gates which clusters are eligible for
prior training. **Annotate as you go** — the prior pipeline filters
out un-annotated clusters by default.

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
3. If the peak correlation exceeds `minScore`, the spike is shifted to the optimal lag: its timestamp is updated, the waveform snippet is re-extracted from `session.fil` or `session.dat` at the corrected sample offset, and PCA features are reprojected through the appropriate eigenvector basis (`.pcaD.N` for stderiv features, `.pca.N` for raw).
4. If a timestamp shift would violate chronological order, the affected `.res`/`.spk`/`.clu`/`.fet` rows are swapped.

### Pipeline variant awareness

Realignment and nudge both respect the two-flag variant detection
described in the Files section above:

- If the loaded `.spk` is the stderiv variant (`.spkD.N`), the
  re-extracted waveform is transformed (spatial derivative + temporal
  first-difference, with `sdiff[-1] = 0` boundary) before being written
  back, so on-disk `.spkD` stays in stderiv space. If the loaded `.spk`
  is raw, the re-extracted waveform is written back raw.
- If the loaded `.fet` is the stderiv variant (`.fetD.N`), the
  re-extracted waveform is transformed before PCA projection, and the
  `.pcaD.N` eigenvector basis is used. If the loaded `.fet` is raw,
  the `.pca.N` basis is used and no transform is applied.

This correctly handles Pipeline C (raw `.spk` + stderiv
`.fetD`/`.pcaD`) where the two flags disagree.

### Transactional pending-file model

All edits from realign and nudge go into `.pending` sidecar files
(`.spk.N.pending`, `.fetD.N.pending`, etc.) rather than the live
session files. The session files are updated atomically on save:

- On **Apply** (or save), each `.pending` is renamed over its
  corresponding session file in one atomic operation per file. The
  commit window is short (four `mv` calls); an interrupted commit
  leaves the group indexable by the old cluster labels rather than
  producing unreadable files.
- On **Reject**, the `.pending` files are deleted and the in-memory
  state is rolled back — no on-disk changes.
- On crash, the autosave-recovery path and the `.bak` files written
  by upstream scripts (reextract, subcluster) give an independent
  rollback option.

After realignment completes, the **Realignment Review Dialog** shows before/after mean waveforms and reports the number of spikes shifted and timestamps reordered. The result can be **accepted** (pending files renamed; changes persisted) or **rejected** (pending files discarded).

**Actions → Abort Realignment** cancels realignment in progress.

Realignment parameters (maxShift, minScore) are set in **Settings → Preferences → General → Realignment**.

---

## Timestamp nudge

**Actions → Nudge Timestamp ±1** (toolbar buttons, or `Page Up` /
`Page Down`) shifts every spike in the selected cluster by ±1 sample.
Use to correct systematic clock offsets between a cluster and the
detection peak (for example, after a spike-sorter converted template
lag from milliseconds with slight rounding).

Nudge is a lighter-weight variant of realign: instead of doing
normalised cross-correlation, it applies a uniform known shift to every
spike in the cluster, re-extracts the shifted waveform from `.fil`,
reprojects through the PCA basis (using the same variant-aware logic as
realign), and writes the update into the `.pending` files. The Apply /
Reject cycle is the same as realign — nothing is committed until save.

Focus returns to the cluster palette after nudge completes, so you can
immediately step to the next cluster with the arrow keys.

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
| Renumber clusters (full sequential) | `R` |
| Renumber selected to end (palette focus) | `T` |
| Update error matrix + template matrix | `U` |
| Recluster (KlustaKwik) | `Shift+R` |
| Realign spikes | `Shift+L` |
| Nudge timestamps +1 sample | `Page Down` |
| Nudge timestamps −1 sample | `Page Up` |
| New cluster (from selection) | `1` |
| Split selected cluster | `2` |
| DipSplit (auto bimodal split) | `Shift+D` |
| Watershed split (2D density basins) | `Shift+W` |
| Shortcut help dialog | `H` |

### Tools

| Action | Shortcut |
|---|---|
| Zoom | `Z` |
| Split clusters | `S` |

### Waveforms

| Action | Shortcut |
|---|---|
| Overlay presentation | `O` |
| Mean ± SD presentation | `M` |
| Increase amplitude | `I` |
| Decrease amplitude | `D` |
| Scale by Maximum | `Shift+M` |
| Scale by Asymptote | `Shift+A` |
| Uniform Scale | `Shift+U` |
| Shoulder line toggle | `L` |

> Time Frame mode is reachable via Waveforms → Time Frame in the menu.
> No keyboard shortcut is bound — `T` is reserved for the cluster
> palette's "renumber selected to end" action when the palette has focus.

### Correlations

Adjustments to correlogram amplitude are reachable via the Correlations
menu only — no keyboard shortcuts are bound.

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
