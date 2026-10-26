<h1><picture>
  <source media="(max-width: 600px)" srcset="banner-narrow.png">
  <img src="banner.png" width="100%" alt="klusters — Spike Sorting GUI">
</picture></h1>

Klusters is an interactive manual spike-sorting application. It loads feature vectors, cluster assignments, spike waveforms, and session parameters for one electrode group at a time, and lets the researcher inspect, split, merge, and reassign clusters across multiple synchronized views. All editing operations are fully undoable, and a background autosave thread writes crash-recovery copies on a configurable schedule.

**Contents:** [Launching](#launching) · [Files read and written](#files-read-and-written) · [Interface layout](#interface-layout) · [Keyboard and mouse](#keyboard-and-mouse) · [Cluster palette](#cluster-palette) · [Hierarchical clustering (fibers and atoms)](#hierarchical-clustering-fibers-and-atoms) · [Views](#views) · [Cluster editing operations](#cluster-editing-operations) · [More curation tools](#more-curation-tools) · [Curation logging](#curation-logging) · [Saving](#saving) · [Update modes](#update-modes) · [Automatic reclustering (KiloKlustaKwik)](#automatic-reclustering-kiloklustakwik) · [Spike realignment](#spike-realignment) · [Timestamp nudge](#timestamp-nudge) · [Autosave and crash recovery](#autosave-and-crash-recovery) · [Preferences](#preferences) · [Parameters toolbar reference](#parameters-toolbar-reference) · [Printing](#printing) · [GPU acceleration](#gpu-acceleration) · [Installation](#installation)

---

## Launching

```bash
klusters rat01.clu.standard.5    # the cluster file of electrode group 5
klusters rat01.fet.standard.5    # or the features of a group not clustered yet
```

Klusters opens one electrode group at a time, from **File → Open** (`Ctrl+O`)
or the command line. The file name must have the form
`<base>.<type>[.<method>].<group>` — usually the `.clu`, or the `.clc`, `.fet`
or `.par.N` of the group; a name without a group number, such as
`session.yaml`, is refused. The method and group in the name select every
other file of the group (see [Files read and written](#files-read-and-written)),
and the session parameters are read from `<base>.yaml` in the same directory.

If there is no `<base>.yaml`, the old KiloKlustaKwik text parameter files
`<base>.par` and `<base>.par.N` are read instead. Legacy `.xml` parameter files
are not read; convert them with `ndm_xml2yaml` (ndmanager-plugins).

---

## Files read and written

Klusters resolves every file it needs through the shared
[variant naming convention](../ndmanager-plugins/formats/naming.md). The
**variant** (method — `standard` or `stderiv`) is read from the `.clu` you
open: opening `rat01.clu.stderiv.5` selects the `stderiv` variant, and
every sibling (`.spk`, `.fet`, `.pca`, `.res`, …) is then resolved in that
same method (with `.spk`/`.res` falling back to `.standard`/untagged, since
they are shared across variants). The variant is remembered for the session
so all edits go back to the same files.

| File | Role |
|---|---|
| `session.fet.<method>.N` | **Required.** Feature vectors — primary input (binary int64). **MethodSpecific** (`fet.standard.N` / `fet.stderiv.N`), resolved strictly in the session variant |
| `session.clu.<method>.N` | Cluster assignments — read on open, **overwritten on save**. Opening it fixes the session variant. In a hierarchical session this is the **fiber (parent)** layer |
| `session.clc.<method>.N` | **Hierarchical sessions only.** Child (**atom**) layer — the over-split micro-clusters nested inside the `.clu` fibers. Read on open, regenerated on save. See [Hierarchical clustering](hierarchical-clustering.md) |
| `session.clp.<method>.N` | **Hierarchical sessions only.** Atom→fiber map (which fiber each atom belongs to). Regenerated on save, with a `.bak` backup |
| `session.spk.<method>.N` | Spike waveforms — enables Waveform View. **Shared** (raw): one copy across variants, resolved to the session method then `.standard` then untagged. The old `.spkD.N` is retired — stderiv is a downstream PCA-time transform |
| `session.yaml` | Session parameters (nChannels, samplingRate, waveform geometry, cluster notes) |
| `session.res.N` | Spike timestamps — used by Trace View and by nudge/realign |
| `session.dat` / `session.fil` | Raw / filtered signal — opened by Trace View; read by nudge/realign for waveform re-extraction |
| `session.pca.<method>.N` | PCA eigenvectors — used by spike realignment and nudge to reproject features after shifting timestamps. **MethodSpecific** (`pca.standard.N` / `pca.stderiv.N`) |
| `session.<type>.<method>.N.pending` | Transactional working copies (res/spk/fet/clu) — all realign/nudge edits go here first; on save each is copied beside its original and renamed over it |
| `session.<type>.<method>.N.saving` | Short-lived: a new version written by a save before it is installed. A leftover one with no `.commit` journal is the remains of a save that failed before installing anything and can be deleted |
| `.session.clu.<method>.N.commit`, `<file>.rollback` | Short-lived: the save's journal and the previous versions it moves aside while installing. A journal left by a crash is completed automatically on the next open |
| `session.cur.<method>.<group>[.<stage>]` | **Append-only** JSON-line audit trail of every editing operation in this group's curation history. Schema in [Curation logging](#curation-logging). |
| `session.cli.<method>.N` | Cluster information (structure, type, quality, notes) of the clustering, saved with it — [format](../ndmanager-plugins/formats/cli.md) |
| `.session.clu.<method>.N.autosave`, `.session.clc.<method>.N.autosave` | Crash-recovery copies of the unit (and atom) ids, written while there are unsaved changes; removed by a save or a normal close |

### File variant resolution

Klusters resolves files through the shared
[chain-of-custody policy](../ndmanager-plugins/formats/naming.md)
(`custody.hpp` / `parseAnchor`). The session's **method** comes from the
`.clu` you open; `.spk` / `.fet` / `.pca` / … are then resolved in that
method, with the shared `.spk` / `.res` falling back to `.standard` and
then the untagged legacy name.

Whether the loaded features are in the **stderiv** domain is decided by the
*resolved method*, not by a filename suffix: it is `true` iff the session
method is `stderiv`. This drives the nudge / realign re-projection — when
the method is `stderiv`, the stderiv transform is applied before the PCA
projection and the `pca.stderiv.N` basis is used; otherwise the raw path
and `pca.standard.N` are used. Because the raw `.spk` is shared across
variants, a stderiv session applies the transform on re-extraction rather
than reading a separate stderiv waveform file (the old `.spkD.N`).

### Feature spaces — one `.clu` through another method

A group often has several method **spaces** on disk — `standard`,
`stderiv_C5`, a `_D` lag space such as `stderiv_C5_D34` — each with its own
`.fet` (and `.spk`, `.pca`) over the **same spikes in the same order**. The
**Space** box at the far right of the Parameters toolbar shows the space
whose features and waveforms are loaded and lists every space the group has.
Pick another to load its `.fet` and `.spk` under the clusters you are
curating, to compare how the same clusters separate in a different feature
space; pick the `(.clu)` entry to go back.

- Your cluster assignment, unsaved edits and undo history are kept; only the
  feature values, the scatter and the waveforms change. A space whose `.fet`
  or `.spk` does not match the open spike count is listed greyed out, with the
  reason as its tooltip.
- **Save** always writes the `.clu` under the method it was opened with,
  whichever space is shown.
- **Realign**, **Align All**, **nudge** and the automatic post-edit realign
  work from any space (Preferences → Curation → Realignment → *Realign and nudge from any
  feature space; keep every space aligned*, on by default). They run in the
  `.clu`'s own space — Klusters swaps to it and back — and every other space
  then follows the spikes that moved: each moved spike is re-extracted from
  the `.fil` into every other waveform file, in that space's own domain, and
  re-projected into every space's features. Only the moved spikes are touched;
  a space that was already behind gets a full sync instead. The results are
  working copies, written by **Save**. This costs a `.fil` read of the moved
  spikes and a rewrite of them in each space per realign or nudge; with the
  preference off, these operations are off while another space is shown and
  the other spaces keep their old spike times until synced.
- **Save As**, **decollide** and **template generation** still need the
  `.clu`'s own space. **Recluster** works, using the shown space's features.
- **Ctrl+Tab** shows the next space and **Ctrl+Shift+Tab** the previous one
  (Actions → Next / Previous Feature Space; rebindable in Preferences →
  Input). Spaces that cannot be loaded are skipped. Plain Tab / Shift+Tab
  still move keyboard focus around the window.
- The box is locked while saving, realigning or reclustering.
- Every space is held in memory: its `.fet` table and its `.spk` waveforms are
  loaded when the document opens (and refreshed after a switch, a sync or a
  Save, reading only files that changed), so switching, syncing and waveform
  display never wait on the disk. The status bar says how much is held. This
  costs memory: roughly the size of each space's `.fet` plus each distinct
  `.spk` (a `_D` space shares its waveform token's).

This works because every file of a group is indexed by the same spike row,
and Klusters keeps it that way: realign and nudge rewrite each spike in its own
row and never move rows. A spike's new time may pass a neighbouring spike's;
that does not matter, because every `.spk` and `.fet` follows the one `.res`.

**Putting rows back in time order.** **Actions → Sort Spikes by Time** sorts
the saved `.res` by time and moves the same rows in every file of the group that
is indexed by spike: the group's `.res`, each waveform file (`.spk`), each
method's `.fet`, and every `.clu` and `.clc` (stage-tagged copies too). A file
shared by several spaces — the one `.res`, or the `.spk.stderiv_C5` that
`stderiv_C5_D34` also reads — is one file and is permuted once. It works on the saved files, so save
first; it then reopens the document, which clears undo history. A file without
the group's spike count is left as is, and an `.eap` template-class file or a
`.col` decollide sidecar is listed as not reordered (regenerate it). The sort
closes the document, rewrites the files, and opens it again; if the document
cannot be closed yet, nothing is sorted. All files are written first and
swapped in together; if anything fails, every file is left as it was.

**Keeping spaces in sync.** Realign and nudge rewrite only the `.clu` space's
`.spk` and `.fet`. A `_D` space has no waveform file of its own — it reads its
waveform token's (`stderiv_C5_D34` reads `.spk.stderiv_C5`) — so with the
`.clu` in `stderiv_C5` the lag space's waveforms are already current and only
its `.fet` is stale; a space over a different waveform file (`standard`, say)
is stale in both. **Actions → Sync Feature Spaces** brings every other space up
to date: it re-extracts each waveform file the `.clu` space does not use from
the `.fil` at the current spike times (once, however many spaces read it) and
re-projects every other space's `.fet` through its own `.pca` basis (lag `_D`
spaces included). To do this automatically, turn on **Preferences →
Curation → Realignment → Sync a feature space to the current spike times when switching to
it**; each switch then costs one `.fil` pass over the group. Synced files are
working copies: **Save** writes them, closing without saving discards them,
and undoing a realignment drops them (re-sync afterwards). A space that cannot
be synced — no `.pca` basis, a basis that does not fit the waveforms, or one
trained in the other domain (standard vs stderiv) — is reported and keeps its
existing files. Spaces that read the same `.spk` are synced together, and if
any one of them cannot be, none of them is.

### Feature file format (`.fet.<method>.N`)

```
int32_t  nDimensions                     (4-byte header)
nSpikes × nDimensions × int64_t          (row-major; last column is the spike time in samples)
```

Only this binary layout is read; the old KiloKlustaKwik text `.fet` is not.
The last column is the spike time; the columns before it are the clustering
features. Full description: [`.fet` format](../ndmanager-plugins/formats/fet.md).

---

## Interface layout

The window is divided into:

- **Left — Cluster palette**: colored buttons, one per cluster ID. Shows/hides clusters, drives per-cluster color editing, and optionally shows cluster metadata.
- **Center — Display tabs**: one tab per open view. The default opening tab is the **Overview Display** (cluster scatter + waveform side by side).
- **Top — Toolbars** (each can be hidden from the **Settings** menu):
  - **Main**: Open, Save, Print, Undo, Redo.
  - **Actions**: Delete Artifact Cluster(s), Delete Noisy Cluster(s), Update Display, Update Error Matrix, Reorder Clusters by Similarity, Group Clusters, Auto-Merge.
  - **Tools**: New Cluster, Split Clusters, Multi-Lasso, Split by Lassos, Delete Artifact Spikes, Delete Noisy Spikes.
  - **Cluster bar**: Previous Spike, Next Spike (trace view).
  - **Parameters**: feature axes, waveform and correlogram settings, the feature space box — see [Parameters toolbar reference](#parameters-toolbar-reference).

### Radial context menu (experimental)

**Right-click** in a view (cluster, waveform, correlogram, trace, the curation
matrices) opens a ring menu centred on the cursor. Two right-clicks keep their
own meaning: while a lasso is being drawn, Right still removes the last point,
and with the lineage overlay engaged it opens the overlay's menu. By default the root ring holds Tools,
Clusters, Recluster, Undo, Redo, Hierarchy, Sort, the two feature axes
**X: n** and **Y: n**, and **N: n**; hovering a group opens its actions as the next ring out. Sort holds the Actions ▸ Sort Clusters entries
under short labels (Count, Time, SNR, Spectral, …); the status bar shows each
one's full name.

The X and Y wedges (marked with a pair of small arrows) set the cluster view's
feature dimensions: hover one and turn the mouse wheel to step that axis through
the features, wrapping at the ends. The scatter redraws behind the menu as you
scroll, the toolbar's Features (x,y) boxes follow, and clicking these wedges
never closes the menu. They are greyed in a display without a cluster view.
The **N** wedge does the same for the toolbar's **N feat** box, the number of
features auto-selected for reclustering (1 to 25, stopping at the ends); it is
live while **Auto-select features** is on (Preferences ▸ Curation ▸ Reclustering), in any
view, and greyed otherwise.
Each wedge is the menu-bar action itself, so a greyed wedge is an action that is
unavailable right now, a dot marks a checked tool, and the status bar shows the
full name and shortcut of the wedge under the cursor.

- **Flick:** hold the button, drag towards the wedge, release on it.
- **Click:** release without moving; the menu stays open; click a wedge.
- **Cancel:** release or click in the hub, click outside, or press Esc (Esc
  first closes the outermost ring).

**Editing the wheel — Preferences ▸ Radial menu.** The page shows the wheel as
a tree: top-level rows are the root ring, clockwise from 12 o'clock, and a
group's rows are the ring it opens.

- **Add:** pick actions in *Available actions* (every menu-bar action, by menu,
  plus the X, Y and N feat value wedges; the filter box searches names) and click *Add to
  Wheel* or double-click. They go into the selected group, or after the
  selected row.
- **Organise:** drag rows to reorder them or to move them into another group
  (a group takes rows, an action does not), or use *Up* / *Down*. *New Group*
  adds a group; *Remove* deletes the selected rows (a group with its rows).
- **Rename:** double-click a group to rename it; double-click an action's
  *Wedge label* to give it a short name on the wheel (empty: the action's own
  text). For the X / Y / N wedges the label is the prefix before the number.
- **Preview:** below the tree, the wheel as it will look (root ring and one ring
  out). Selecting a row opens its group's ring; hovering works as in the real
  menu, without triggering anything.
- *Restore Default* brings back the shipped wheel. Apply or OK saves the layout
  (in the Klusters settings, `radialMenu/layout`) and the next right-click uses
  it. Rows naming an action this version does not have show in red and are
  skipped; groups more than four rings deep do not open.

The chord is `app.radialMenu` in **Preferences ▸ Input** and can be rebound
(for example to Shift+Right); plain Right-click then opens the older Sort
Clusters list again. On
an X11 desktop without a compositor the corners around the rings may show as
black instead of transparent.

---

## Keyboard and mouse

**Help → Keyboard Shortcuts** (`H`) lists every live key and mouse binding,
including the ones you have changed. Bindings are edited in **Settings →
Preferences → Input**, which also saves and loads named keymap layouts
(*Keymap layout*: Apply, Save As…, Rename, Delete, Reset all to defaults).
The defaults for the most-used actions:

| Key | Action |
|---|---|
| `Ctrl+O` / `Ctrl+S` | Open / Save |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+A` / `Ctrl+Shift+A` | Select all clusters / all except 0 and 1 |
| `S`, `Esc` `Esc` | Toggle a cluster in the palette selection; clear the selection |
| `G` / `Shift+G` | Group the selected clusters / Auto-Merge |
| `Delete` / `Shift+Delete` | Move the selected clusters to noise (1) / artefact (0) |
| `Ctrl+1` / `Ctrl+2` | New Cluster / Split Clusters lasso tool |
| `Ctrl+3` / `Ctrl+4` / `Ctrl+5` | Delete Artifact Spikes / Delete Noisy Spikes / Multi-Lasso tool |
| `R` / `T` | Renumber clusters / move the selected clusters to the end |
| `U` | Update the error, template and residual matrices |
| `E` | Next matrix tab |
| `Shift+D` / `Shift+W` | DipSplit / Watershed split |
| `Shift+R` | Recluster with KiloKlustaKwik |
| `Shift+L` / `Page Up` / `Page Down` | Realign / nudge the selected cluster +1 / −1 sample |
| `A` / `F` | Auto-scale / t-SNE presentation in the Cluster Display |
| `O` / `M` / `I` / `D` | Waveforms: overlay / mean and SD / amplitude up / down |
| `=` / `-` | Point size up / down |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous feature space |
| `Tab` / `Shift+Tab` | Move focus: cluster palette → child palette → toolbar fields |
| `P` | Preferences |
| `F1` | This handbook |

Mouse, in the Cluster Display: with a lasso tool on, left-click adds a
vertex, right-click removes the last one and middle-click closes the lasso.
`Ctrl`+wheel zooms and `Ctrl`+drag pans (also in the matrices). Right-click
elsewhere opens the [radial menu](#radial-context-menu-experimental).

---

## Cluster palette

Each cluster is represented by a colored icon button. The palette drives everything: clicking buttons selects which clusters appear in the views.

### Selection

| Action | Method |
|---|---|
| Select one cluster | Click its button |
| Add cluster to selection | `S` key while focus is on that cluster's button |
| Pin current selection (accumulate) | `S` on each cluster in turn |
| Clear S-pin set (keep single visual) | `S` on an already-pinned cluster |
| Select All | `Ctrl+A` |
| Select All except 0 and 1 | `Ctrl+Shift+A` (skips noise/MUA) |
| Clear the selection | `Esc` twice in quick succession |

The `S` key (S-pin) is specifically designed to accumulate a working set without requiring Ctrl+click in the palette. The set of S-pinned clusters is unioned with the current visual selection; all views respond to the combined set.

### Colors

Double-click a cluster button to open a color picker. Colors last for the session; they are not saved.

### Cluster information panel

**Settings → Show Cluster Info** (or `Settings` menu toggle) expands each cluster button to show the user-editable metadata fields (the non-empty ones, in this order):

| Field | Description |
|---|---|
| **Structure** | Brain region or anatomical label (e.g. `CA1`, `DG`) |
| **Type** | Putative cell type (e.g. `pyramidal`, `interneuron`) |
| **Isolation distance** | Cluster isolation distance |
| **Quality** | Sorting quality rating (e.g. `1`, `2`, `3`) |
| **Notes** | Free-form text |
| **Region** | The named region (e.g. `hippocampal formation`) |
| **Division** | The broad division (e.g. `archicortex`, `cerebral cortex`, `thalamus`, `cerebellum`) |
| **Atlas** | The reference atlas the terms come from (e.g. Waxholm Space rat atlas v4, Allen CCF) |
| **Region ID** | A stable identifier of the finest term (an atlas structure ID or an `UBERON:` ID) |

Double-click a cluster button (when the info panel is visible) to edit any field via a dialog. The fields are saved with the clustering, in its own file beside the `.clu` ([`.cli.<method>.N`](../ndmanager-plugins/formats/cli.md)), and read back when it is opened. Klusters does not write the session YAML's `units` table: that is the record of finished curation, filled from these files in ndManager with **Actions ▸ Record Curated Units…**.

### Focus handling after batch operations

After any operation that auto-selects the next cluster in the palette (delete-to-noise, delete-to-artefact, recluster completion, realign completion), focus returns to the palette rather than to the 2-D scatter. This means arrow keys continue to step through clusters without requiring a Tab press to reach the palette.

The one exception is the cluster-editing slots whose natural follow-up is 2-D inspection (undo, redo, group-merge, nudge): these leave focus on the scatter view.

---

## Hierarchical clustering (fibers and atoms)

Klusters can open a **two-level** clustering: the units you curate
(**fibers**, the `.clu` layer) are assembled from a finer over-split layer
(**atoms**, the `.clc` layer), with a `.clp` sibling mapping each atom to
its fiber. Opening a `.clu.N` that has `.clc.N` + `.clp.N` siblings enables
**Displays → Hierarchical Session (.clc + .clp)**.

When it is on, the main palette lists the **fibers** and a second **Child
clusters (.clc)** dock lists the **atoms of the selected fiber**. `Tab`
cycles focus parent → child → toolbar. `G` merges atoms, `Ctrl+Up` forms a
new fiber from selected atoms, `Ctrl+Down` groups fibers, and
`Ctrl+Shift+Down` dissolves a fiber into its atoms; the atom layer has its
own undo/redo. Saving regenerates the `.clu`/`.clc`/`.clp` triple together.

See **[Hierarchical clustering](hierarchical-clustering.md)** for the full
model, the file formats, and every operation.

---

## Views

Multiple views can be open simultaneously as tabs. Each view type can be opened from the **Displays** menu:

| View type | Menu | Description |
|---|---|---|
| Cluster Display | Displays → New Cluster Display | 2-D scatter plot of feature projections |
| Waveform Display | Displays → New Waveform Display | Per-cluster mean ± SD waveforms |
| Correlation Display | Displays → New Correlation Display | Auto- and cross-correlograms |
| Overview Display | Displays → New Overview Display | Scatter + waveform side by side (default on open) |
| Grouping Assistant | Displays → New Grouping Assistant Display | Error (misclassification) matrix and template matrix |
| Trace Display | Displays → New Trace Display | Raw signal with spike overlays |
| Residual Matrix | Displays → New Residual Matrix Display | Adds a residual (separability) matrix to the active display |
| Drift Matrix | Displays → New Drift Matrix Display | Adds a drift-shifted waveform correlation matrix to the active display |
| Template Library | Displays → New Template Library Display | The marked template units' waveform series (`.wtf` / `.wti`) |

The error and template matrices are docks inside a display (by default in
the Overview Display, tabbed together on the right); `E` steps through the
open matrix tabs (error → template → residual → drift). **Displays → Rename
Active Display** (`Ctrl+R`) and **Close Active Display** (`Ctrl+W`) act on
the current tab. **Displays → Hierarchical Session (.clc + .clp)** is an
indicator, checked when the open session is
[hierarchical](#hierarchical-clustering-fibers-and-atoms).

All views are synchronized: selecting a cluster in any view highlights it in all others.

### Cluster Display (scatter plot)

Displays spike feature vectors as a 2-D scatter plot. The x and y axes are selected from drop-down menus in the **Parameters** toolbar.

- **Axis dimensions**: any pair of feature dimensions (0-indexed; the last dimension, which is the timestamp, is available as an axis and is useful for detecting drift).
- **Point size**: `=` / `Shift+=` to increase; `-` to decrease.
- **Lasso**: with a cutting tool on (New Cluster, Split Clusters, Delete Artifact / Noisy Spikes), left-click adds a vertex, right-click removes the last one, and middle-click closes the polygon.
- **Zoom and pan**: `Ctrl`+wheel zooms toward the cursor, `Ctrl`+drag pans, and a double-click shows everything again. `A` toggles auto-scale to the shown clusters.
- **Pick a time**: with no tool on, a click scrolls the Trace Display to the nearest spike.

Multiple Cluster Displays can be open simultaneously with different axis pairs, allowing quality assessment in several projection planes at once.

### Waveform Display

Shows spike waveforms for the selected clusters on all channels of the electrode group.

**Presentation modes** (Waveforms menu):

| Mode | Key | Description |
|---|---|---|
| Sample mode (default) | — | Shows the N most-recent spikes per cluster (N set in Parameters bar) |
| Time Frame mode | — | Shows all spikes from a time window set in the Parameters bar (start + duration in seconds) |
| Overlay | `O` | All clusters drawn in the same color space on top of each other |
| Mean ± SD | `M` | Shows the cluster mean waveform with ±1 SD envelope; hides individual spikes |

**Amplitude controls** (Waveforms menu):

| Action | Key |
|---|---|
| Increase amplitude (all channels) | `I` |
| Decrease amplitude (all channels) | `D` |

### Correlogram Display

Shows auto-correlograms (diagonal) and cross-correlograms (off-diagonal) for selected clusters.

**Parameters** (set in the Parameters toolbar):

| Parameter | Description |
|---|---|
| Bin size | Width of each correlogram bin in milliseconds |
| Half duration | Time span on each side of zero lag (total window = 2 × half duration) |

**Correlations menu:**

| Entry | Key | Description |
|---|---|---|
| Scale by Maximum | — | Normalize each correlogram to its own peak |
| Scale by Asymptote | `Shift+A` | Normalize to the flat baseline (shoulder) |
| Uniform Scale | `Shift+U` | No normalization — raw counts |
| Asymptote Line | `L` | Show the baseline level as a line |
| Increase / Decrease Amplitude | — | Scale the correlograms up or down |

Correlograms are computed on a background thread and update asynchronously. `Actions → Update Display` forces a recompute.

### Overview Display

The default view on open. Shows the Cluster Display (scatter) and Waveform Display side by side in a single tab. All controls from both views are available.

### Grouping Assistant Display

Computes a cluster × cluster misclassification probability matrix using a probabilistic classifier (`GroupingAssistant::computeMeanProbabilities()`).

Entry (r, c) is the mean posterior probability that a spike from cluster r was actually generated by cluster c. The diagonal should be close to 1.0; high off-diagonal values indicate:

- High (r, c) alone → spikes of cluster r are frequently misclassified as cluster c (possible contaminant).
- High (r, c) **and** (c, r) symmetrically → the two clusters are strongly overlapping and are candidates for merging.

**Actions → Update Error Matrix** (`U`) recomputes the matrix after editing. `U` also refreshes the template and residual matrices that are open.

### Template Matrix

Pairwise waveform similarity matrix for the currently-shown clusters. Each cell (r, c) shows the normalized cross-correlation between the mean waveform of cluster r and the mean waveform of cluster c, summed across all channels of the electrode group.

- **High off-diagonal xcorr** (dark cells close to the diagonal value) identifies clusters whose mean waveforms are almost identical — strong merge candidates.
- **Selection highlighting is asymmetric**: clicking cell (r, c) highlights only that one cell, not the mirror cell (c, r). This lets you trace the direction of a putative merge (for example, when you suspect cluster r is a sub-population of cluster c).
- Pairwise xcorr is computed **on demand** — when a cell is first clicked, the corresponding pair is computed and cached. This keeps matrix open for large (50+) cluster sets cheap.
- The matrix auto-recomputes after Apply and after any cluster-editing operation that invalidates waveforms. Parallel computation via OpenMP.
- The noise cluster (cluster 1) is included in the matrix — useful for identifying clusters that have drifted into MUA.

### Residual Matrix Display

Pairwise **separability** matrix over the shown clusters' waveforms. Each
off-diagonal cell (i, j) is a bounded separability fraction — the share of
the cross-template residual between clusters i and j that is *systematic*
(template gap) rather than *noise* (within-cluster variance):

```
M(i, j) = gap / (meanVar[i] + gap)      ∈ [0, 1)
```

- **Near 0** (red) — the two templates differ by less than the noise, so
  the clusters are hard to tell apart: a **merge candidate**.
- **Near 1** (blue) — distinct given the noise.
- The scale is absolute (`[0, 1]`), so distinct pairs saturate near 1
  instead of stretching the color map; the diagonal holds each cluster's
  own waveform variance (drawn separately, hover-only).
- **Click a cell to select clusters**: a plain click shows the pair
  (row cluster and column cluster) in the scatter/waveform views;
  Ctrl-click adds them to the current selection — the same entry points as
  the error matrix, so the residual matrix is usable for curation, not
  only inspection. Pan with Ctrl-drag, zoom with Ctrl+wheel (as in every matrix).
- Computed by a background thread (per-cluster mean/variance, then the
  pairwise metric, both parallelised); recomputes after edits.

### Drift Matrix Display

Diagnostic only. Cell (row A, column B) is the correlation between A's mean
waveform shifted along the probe's depth axis by the slider's µm value and
B's mean waveform (+Δ above the diagonal, −Δ below). A pair of clusters that
is one unit split by probe drift reads high at the Δ that realigns it. It
needs the probe geometry (the `probes:` section of the session YAML) and never
moves spikes.

### Template Library Display

Shows the template library of the open group: the units marked as templates
(**Actions → Mark/Unmark as Template**) and their linked waveform series
(`.wtf` / `.wti`). Pick a unit and a link (drift / adapt) and scrub its bins;
select two or more units to pin them as the [oblique basis](#oblique-projection-and-decollide).
See [Template library](#template-library).

### Trace Display

Shows raw or high-pass filtered signal (`session.dat` or `session.fil`). Spike timestamps from the current electrode group are overlaid as colored tick marks.

- Navigate with the start / duration fields in the Parameters toolbar.
- **Traces → Next Spike** (`Ctrl+Shift+Right`) and **Previous Spike** (`Ctrl+Shift+Left`) scroll the trace to the next or previous spike of the selected clusters.
- **Traces → Increase / Decrease Channel Amplitudes** (`Ctrl+Shift+I` / `Ctrl+Shift+D`) scale the selected channels.
- **Traces → Show Labels** (`Ctrl+L`): display channel names on the trace view.
- A click in the Cluster Display (no tool on) scrolls the trace to the nearest spike.

---

## Cluster editing operations

All operations push onto the undo stack. The full session history is preserved until the file is closed.

| Operation | Shortcut / Menu | Description |
|---|---|---|
| Group (merge) clusters | `G` | Merges all selected clusters into the lowest-numbered one |
| Auto-Merge similar clusters | `Shift+G` | Same template cross-correlation mechanism KKE uses for `WithinChunkTemplateMatch`; pairs scoring at or above the threshold are merged. See [Auto-Merge action](#auto-merge-action) below. |
| Split clusters | `Ctrl+2` (Tools → Split Clusters) | Draw a lasso in the scatter plot; closing it moves the enclosed spikes of each shown cluster into a new cluster of its own |
| Auto bimodal split (DipSplit) | `Shift+D` | Tests the active cluster for bimodality on its top PCs; if a clean valley is found, splits it into two new clusters |
| Watershed split | `Shift+W` | Runs a 2D density watershed on the selected clusters in the active scatter view's X/Y dimensions, with a live preview; each density basin becomes one new cluster, leftover spikes (outside any peak) form a residual cluster |
| New cluster from lasso | `Ctrl+1` (Tools → New Cluster) | Creates one new cluster from the lasso-enclosed spikes |
| Delete selected spikes → artefact (0) | `Ctrl+3` (Tools → Delete Artifact Spikes) | Moves lasso-enclosed spikes to cluster 0 (artefact) |
| Delete selected spikes → noise (1) | `Ctrl+4` (Tools → Delete Noisy Spikes) | Moves lasso-enclosed spikes to cluster 1 (noise/MUA) |
| Strip feature outliers (5σ) | menu only (Actions → Strip Feature Outliers) | Across **all** clusters, moves every spike lying more than 5σ from its cluster's per-dimension feature mean (in any single feature dimension) into the artefact cluster (0). A non-mutating scan runs first and asks for confirmation with the exact spike/cluster count; undoable. Per-dimension (not Mahalanobis) is deliberate — a raw 5σ Mahalanobis cut in ~24-D would flag roughly half of every cluster |
| Delete cluster(s) → artefact (0) | `Shift+Delete` | Moves all spikes of selected clusters to cluster 0 |
| Delete cluster(s) → noise (1) | `Delete` | Moves all spikes of selected clusters to cluster 1 |
| Renumber selected to end | `T` | Reassigns the selected cluster IDs to be greater than the current maximum, moving them to the tail of the palette |
| Undo | `Ctrl+Z` | Undoes the last editing operation |
| Redo | `Ctrl+Y` | Redoes the last undone operation |
| Renumber clusters | `R` | Reassigns cluster IDs to be contiguous starting from 2 (0 and 1 are always noise/artefact) |
| Renumber and Save | `Ctrl+Shift+S` | Renumbers then saves in one step |
| Update Display | Actions toolbar | Forces all views to recompute from current cluster assignments |

The cutting tools (`Ctrl+1` … `Ctrl+4`, and Multi-Lasso `Ctrl+5`) are
toggles: pressing the key, button or menu entry of the active tool again
returns to the normal cursor.

**Cluster 0** is the artefact pseudo-cluster (waveforms that are not spikes). **Cluster 1** is the noise/MUA pseudo-cluster. Both are always shown at the top of the palette. Spikes sent to these clusters are not deleted — they remain in the feature file and can be retrieved by undo.

Operations that change cluster **order** rather than membership — Reorder
by Similarity (`Shift+S`) and the standalone sorts (by spike count, time,
contamination, SNR, waveform, …) — live under **Actions → Sort Clusters**
and are documented separately in
**[Cluster sorting and reordering](cluster-sorting.md)**.

### Reserved chunk clusters

A fiber-kit chunk run (`FK_CHUNKS`) sorts only some chunks of a recording.
Every spike of a chunk it did not sort goes to that chunk's **reserved chunk
cluster**: id 16 777 116 + chunk, in the block 2^24 − 100 … 2^24 − 1
([reserved ids](../ndmanager-plugins/formats/clu.md#reserved-ids)). Klusters
protects these clusters. When an operation on the clusters you selected leaves
one out, the status bar says so. Operations over all clusters (Purge Small
Clusters, the feature-outlier strip, Auto-Merge, the sorts) leave them out
without a note.

- **Nothing is added to one.** An edit that would put a spike into a reserved
  chunk cluster is refused, and the clustering stays as it was. Decollide
  refuses a reserved basis cluster and skips a collision that names one as a
  unit.
- **Only reclustering and feature-space selections take spikes out of one.**
  These are the **Recluster** menu (the three Recluster entries, Split by KNN
  Voting, Strip by Template), DipSplit, Watershed, and the lasso tools: New
  Cluster, Split Clusters, Delete Artifact/Noisy Spikes, Multi-Lasso and
  Decollide (`D`), in the scatter, t-SNE and oblique views. Their results get
  new ids below the block, or go to 0 or 1. Strip by Template takes spikes from
  a reserved chunk cluster only when it is selected.
- **Every other edit leaves them out:** Group, Auto-Merge, Delete cluster(s),
  Purge Small Clusters, both outlier strips, Partition by Time, the template
  matrix's Apply, the decollide plugin's batch apply, and the hierarchy's
  parent merges, promotions, dissolves and drops. An atom of a reserved chunk
  cluster stays in it; atoms inside one can still be merged or re-cut among
  themselves.
- **Renumbering and sorting keep their ids:** Renumber, Renumber to End,
  Renumber and Save, the automatic renumber, Compact Cluster IDs and every
  sort. A reserved chunk cluster keeps its id and takes no place in the
  order.
- **The matrices leave them out:** the error, template, residual and drift
  matrices, the merge recommendations, Auto-Merge's scores and Split by KNN's
  reference clusters. A reserved chunk cluster is not a unit.

### DipSplit — automatic bimodal split

DipSplit (`Shift+D`) tests the active cluster for bimodality and splits it
when the test is clear. The pipeline:

1. Compute the cluster's top PCA directions in feature space.
2. Project members onto each top direction; run a kernel-density
   estimate on the 1D projection to detect a valley between two modes.
3. If a valley is found and is deeper than `DipSplit valley depth`
   (preference, default 0.20), refine the split via 2-means in the
   plane of the top two PCs.
4. Apply a **bloat gate**: re-fit a single Gaussian to the cluster and
   check that ≥10% of members fall outside the χ²(d, 0.9) ellipsoid.
   Bloated clusters split; tight ones do not. Disable the gate by
   setting `DipSplit bloat factor` to `0` (the default in Klusters).
5. Apply a BIC two-vs-one gate as a final sanity check.

#### Commit and confirm

DipSplit has no preview: when the tests accept the split, it is applied at
once. Both halves get **new IDs at the tail of the palette** (the left half
the next free ID, the right half the one after it), the source cluster's ID
disappears, and both new clusters are selected. A HUD on the scatter view
and the status bar report the result:

```
DipSplit: 5 → 32 + 33 (Esc to undo, Enter to keep)
```

The HUD also shows the spike counts, the best principal component, the
valley depth and ΔBIC. Press `Enter` to keep the split and close the HUD, or
`Esc` to undo it; any other curation action also closes the HUD and keeps
the split. The split is **one undo entry**: a single `Ctrl+Z` brings back
the source cluster with all its spikes.

If the tests reject the cluster (too small, already unimodal, no valley,
BIC worse, …) nothing changes and the status bar gives the reason. Every
decision is recorded in the curation log (see below).

Preferences live under **Settings → Preferences → Curation → Splitting**
(DipSplit group):

| Preference | Default | Effect |
|---|---|---|
| `DipSplit min size` | 50 | Minimum cluster size to consider |
| `DipSplit bloat factor` | 0 | χ²(d, 0.9) bloat gate; `0` skips it |
| `DipSplit valley depth` | 0.20 | KDE valley depth threshold (0–1) |
| `Autoscale margin` | 5% | Whitespace around per-cluster autoscale (`A` key) — set in **Preferences → Views → Display** |

DipSplit is intentionally conservative — it errs toward not splitting.
The manual lasso split (`Ctrl+2`) remains the primary tool for ambiguous cases.

### Auto-Merge action

`Shift+G` (Action → "Auto-Merge Similar Clusters...", or toolbar icon
next to the Group icon). Same template cross-correlation mechanism KKE
uses in `WithinChunkTemplateMatch` / `WithinChunkTemplateMatchMedianKnn`
— interactive klusters merges and offline KKE merges produce **consistent
decisions on the same data** by design.

Pipeline:

1. Filter candidates (skip 0 = artefact, 1 = noise, anything below the
   configured min cluster size).
2. Build per-cluster template — *mean* of all member waveforms, or
   *median* across up to K sampled waveforms (Median mode uses a fixed
   RNG seed so previews are reproducible on the same data).
3. Optional Hann taper on each template before scoring.
4. Pairwise normalized cross-correlation `score = max_lag |xcorr| /
   sqrt(|a|² · |b|²)` with bounded sample shift (0 = auto = nSamp/4
   matching KKE's `WithinChunkTemplateMatch`).
5. Union-find on score ≥ threshold pairs → connected components of size
   ≥ 2 are the merge groups.
6. If preview is enabled, modal dialog with a checkbox per group. OK
   applies the checked subset; Cancel applies none.
7. Apply each accepted group via the existing `groupClusters` machinery
   — integrates with klusters' undo/redo automatically.

#### Settings (Preferences → Curation → Merging)

All defaults match KKE flag defaults:

| Setting | Default | KKE equivalent |
|---|---|---|
| Algorithm | Median | matches `-MedianKnnTemplateMatchEnable 1` |
| Median K | 50 | matches `-MedianKnnTemplateMatchK` |
| Score threshold | 0.98 | matches `-TemplateMatchScore` |
| Max shift (samples) | 0 = auto (`nSamp/4`) | matches `WithinChunkTemplateMatch` internal |
| Hann taper samples | 0 = off | matches `-TemplateMatchTaperHannSamples` |
| Min cluster size | 25 | matches KKE clusters threshold |
| Target scope | Selected | (safer default) |
| Preview before apply | On | (safer default) |

The Median K row is only enabled when the Median algorithm is selected
(grayed out otherwise so the UI doesn't suggest the value applies in
Mean mode).

#### Two scope modes

- **Selected**: only the clusters currently selected in the palette are
  considered. Safest for targeted curation — needs at least 2 clusters
  selected or the action shows an informational dialog.
- **All active**: every non-special cluster (skips 0 and 1) is
  considered. Sweeps the whole document. Recommended with
  `Preview before apply = on` so you can inspect the proposed groups
  before committing.

#### Performance

<details>
<summary>Details</summary>

Currently synchronous on the GUI thread with a modal progress dialog;
`QApplication::processEvents()` between clusters keeps Cancel responsive.
For *Selected* mode this is instant; for *All active* on big sessions
expect seconds to tens of seconds for the waveform-read phase. A future
async `QThread` variant (mirroring `TemplateMatrixThread`) is on the
roadmap.

</details>

### Watershed split — density-based 2D segmentation

Watershed (`Shift+W` with one or more clusters selected in the palette)
treats the selected spikes' (X, Y) feature-space coordinates — using
the *active scatter view's currently displayed dimensions* — as a 2D
point cloud, builds a density grid, and segments it into basins. Each
basin becomes one new cluster.

The pipeline:

1. Project every spike of every selected cluster onto (`dim_x`,
   `dim_y`) — the X/Y dimensions of the active scatter view at the
   time `Shift+W` was pressed. Pressing it from a different view runs in a
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
are fully dissolved — the same renumber-to-tail behavior as a
recluster (KiloKlustaKwik) action.

`Shift+W` always opens a **live-preview overlay** first. The selected
clusters' (X, Y) feature points are extracted once, the kernel runs
against the active scatter view's current X/Y dimensions, and the
basin partition is rendered as a translucent colored overlay
directly on top of the scatter — one color per basin, transparent
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

## More curation tools

### t-SNE presentation

`F` (with a Cluster Display active) shows the selected clusters' spikes as a
t-SNE embedding instead of the feature scatter; `F` again returns. While it
shows, `Up` / `Down` raise or lower the perplexity and recompute. The
embedding settings are in **Preferences → Views → Cluster view**. Lassos work in the
embedding as in the scatter.

### Multi-Lasso

**Tools → Multi-Lasso** (`Ctrl+5`): a closed lasso is kept instead of cutting.
Draw several, then **Tools → Split by Lassos** (`Enter`): each overlap
pattern becomes a cluster, so two overlapping lassos give three.
`Backspace` removes the last kept lasso and `Esc` discards them all.

### Oblique projection and decollide

`Shift+O` projects the shown spikes onto axes given by the selected
clusters' templates (the oblique, template-axis view); `Shift+O` again
returns. **Actions → Set Oblique Basis…** pins chosen clusters as the axes,
so that a different cluster can then be selected and projected onto them.
In the oblique view a New Cluster lasso waits for confirmation: `Enter`
cuts the new cluster, `Esc` discards the lasso, and `D` instead
*decollides* the lassoed spikes against a pinned two-cluster basis.

### Overlays

- `Shift+H` shows a point-density heatmap over the scatter, the oblique view
  or the t-SNE embedding.
- `Shift+E` toggles the lineage overlay (edit mode), in which a left click
  marks lineage nodes. It needs Templates mode (**Preferences → Views → Display**).

### Matrices

- `V` toggles child-scoped matrices in a hierarchical session: the matrices
  then show the atoms of the selected fiber(s) instead of all clusters.
- `E` steps through the open matrix tabs.

### Template library

- **Actions → Mark/Unmark as Template** toggles the selected clusters as
  template units. On **Save** their linked template waveforms (`.wtf`) are
  written in every available method.
- **Actions → Generate Template for Selected Cluster(s)** runs fiber-template
  for the selected clusters now (save first: it reads the files on disk).
- **Displays → New Template Library Display** shows the library.
- **Actions → Recluster → Strip by Template…** (`Shift+J`) uses one selected
  cluster as a template and pulls matching spikes out of the other selected
  clusters.

### Probe drift

- **Actions → Generate Probe Drift…** runs `ndm_estimatedrift` on the group
  and writes `SESSION.drift` (the group must already have a `.clu`).
- **Actions → Apply Drift + Recluster Siblings…** (`Shift+F`) computes
  drift-adaptive chunk boundaries from `SESSION.drift` and can re-run
  KlustaKwik on the other groups of the same probe.
- **Displays → New Drift Matrix Display** shows which cluster pairs are one
  unit split by drift.

### Other Actions-menu entries

| Entry | Key | What it does |
|---|---|---|
| Partition Cluster by Time… | `Shift+T` | Cuts the selected cluster into consecutive blocks of the length you enter (minutes), counted from the start of the session |
| Purge Small Clusters… | — | Moves every cluster with fewer spikes than the number you enter to noise (1), after a confirmation |
| Strip Feature Outliers (5σ)… | — | Moves spikes more than 5σ from their cluster's mean in any feature dimension to artefact (0), after a confirmation |
| Strip Waveform Outliers… | `Shift+X` | Moves each selected cluster's worst-fitting spikes (residual to its median waveform) into a new cluster, after a confirmation |
| Time-Chunk Mode | — | Steps through the session one chunk of the length you enter at a time, with only that chunk's clusters active; **Previous Chunk** / **Next Chunk** move between chunks |
| Sync Feature Spaces, Next / Previous Feature Space, Sort Spikes by Time | — / `Ctrl+Tab` / `Ctrl+Shift+Tab` / — | See [Feature spaces](#feature-spaces--one-clu-through-another-method) |

### Plugins

The **Plugins** menu lists the plugins found in the plugin description
directories (one entry per plugin; **Reload Plugins** rescans them). A plugin
runs on the open clustering. See
[PLUGIN_API.md](../../src/klusters/docs/PLUGIN_API.md).

---

## Curation logging

Every editing operation in Klusters writes a JSON-line record to
`session.cur.<method>.<group>[.<stage>]`, named for the `.clu` it records:
the same method, group and stage, the stage being whatever follows the group
in the `.clu` name (`session.clu.stderiv.5.final` is logged to
`session.cur.stderiv.5.final`; see [file
naming](../ndmanager-plugins/formats/naming.md)). A session sorted under
`standard` gets no log. Logs written before 2026-10-21 are named
`session.curation_log.<group>.<method>`, without a stage; opening an unstaged
`.clu` renames such a log to the new name, and the session goes on appending
to it. The log
is the audit trail for both crash forensics and the empirical-prior
training pipeline (`kk_build_prior.py` in ndmanager-plugins; see
[that workflow](../workflows/empirical-priors.md)).

Each user-facing action emits, at minimum, a paired set of records:
one snapshot per affected cluster from `before` (pre-mutation state)
and one per affected cluster from `after` (post-mutation state),
both tagged with the same `action_idx`. Some actions additionally
emit `ACTION_DETAIL` records carrying algorithm-specific metadata
(see below). Every record carries a `status` field of either
`"good"` (the user kept the action) or `"bad"` (the user reverted
it via Ctrl+Z and never restored it before the action left the
in-memory ring buffer).

<details>
<summary>Log lifecycle, record schema and action types</summary>

### Deferred-flush lifecycle

The on-disk log only contains finalized records. While an action is
still undoable, its records live in an in-memory ring buffer of
capacity equal to the user's max-undo preference (default 50). The
ring's behavior:

- **Each user action** appends a tentative entry with
  `status = "good"`. If the buffer overflows, the oldest entry is
  flushed to disk with whatever status it currently has.
- **Ctrl+Z (undo)** flips the topmost still-`good` entry to `"bad"`.
- **Ctrl+Y (redo)** flips the topmost `bad` entry back to `"good"`.
- **Document close** (= save followed by quit) flushes every
  remaining entry with its current status, then writes the
  `SESSION_CLOSE` sentinel.

Consequences worth knowing:

- The log captures the user's *final* curatorial decisions, not their
  tentative explorations. An action explored and then reverted is
  written out as `"bad"`; one kept past the buffer boundary is
  `"good"`.
- If Klusters crashes before close, in-memory entries are lost. To
  guarantee persistence of recent decisions, save and quit cleanly.
- Undoing past the ring's bottom (i.e. undoing 51+ actions in a row
  with default capacity 50) reverts the data state but cannot
  retroactively re-status the corresponding on-disk records — they
  were finalized at overflow time.
- Manual J / K / X annotation shortcuts no longer exist; the
  `"good"` / `"bad"` status is inferred from undo behavior.

### Top-level record schema

| Field | Description |
|---|---|
| `event` | `SESSION_OPEN`, `SESSION_CLOSE`, `ACTION_DETAIL`, or empty for action snapshots |
| `action` | The operation kind — see action type table below |
| `status` | `good` (kept) or `bad` (reverted via Ctrl+Z) — present on every action / detail record |
| `phase` | `before`, `after`, or empty (for events / details) |
| `role` | `result`, `source`, or empty |
| `action_idx` | Monotonic index — `before`, `after`, and `ACTION_DETAIL` records of one action share an index |
| `cluster_id` | Cluster touched by this record |
| `n_spikes`, `n_clusters_in_group` | State after the action |
| `n_pca_dims`, `n_feat_dims` | Feature-space dimensionality |
| `feat_var_dims` | Per-PCA-dim within-cluster variance |
| `feat_var_frobenius`, `feat_var_top3_mean`, `feat_var_mean` | Aggregate variance stats |
| `l_ratio`, `isolation_dist` | Isolation metrics |
| `nearest_centroid_dist_norm` | Mahalanobis distance to nearest neighboring cluster |
| `waveform_snr`, `waveform_chan_spread`, `waveform_width_samp` | Waveform morphology |
| `isi_cv` | Inter-spike interval coefficient of variation |

### Action types

| `action` value | User-facing operation | Source clusters | Result clusters | Emits `ACTION_DETAIL`? |
|---|---|---|---|---|
| `GROUP` | Merge selected clusters (`G`) | N | 1 | No |
| `SPLIT` | Single new cluster from polygon, lasso, or DipSplit | 1 | 1 (new) + 1 (residual src) | DipSplit only |
| `SPLIT_N` | Multiple new clusters from polygon (one per source) | N | N (new) + N (residual srcs) | No |
| `RECLUSTER` | KiloKlustaKwik re-run on selected clusters (`Shift+R`) | M | K | No |
| `WATERSHED` | 2D density watershed (`Shift+W`) | M | K (basins) + 1 (residual) | Yes |
| `REALIGN` | Spike waveform realignment (`Shift+L`) | unchanged | unchanged | No |
| `NUDGE` | Timestamp ±1 sample shift (`Page Up` / `Page Down`) | 1 | 1 (same id) | No |
| `DELETE_NOISE` | Move whole cluster → cluster 1 (`Delete`) | 1 | — | No |
| `DELETE_ARTEFACT` | Move whole cluster → cluster 0 (`Shift+Delete`) | 1 | — | No |
| `DELETE_REGION_NOISE` | Lasso → cluster 1 (`Ctrl+4` tool) | 1+ | — | No |
| `DELETE_REGION_ARTEFACT` | Lasso → cluster 0 (`Ctrl+3` tool) | 1+ | — | No |
| `MOVE_SPIKES` | Manual subset reassignment | 1 | 1 | No |
| `RENUMBER_PARTIAL` | Renumber selected to end (`T`) | renamed | renamed | No |
| `SORT_ALL` | Any renumbering sort: **Actions → Sort Clusters**, `Shift+S` | — | — | Yes (the id map) |
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
 "min_size":50,"bloat_factor":0.0,"valley_thresh":0.2,
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
8 into neighbors" from "watershed only found 4 peaks total".

When the input has too few spikes (< 50) or the watershed finds only
one basin, the action is silently skipped — no records are written
for the no-op case.

**Sort All Clusters** (`SORT_ALL`) is one record per sort: which sort
ran, and the new id of every sorted cluster, keyed by its old id. A sort
moves no spikes, so it writes no `before` / `after` cluster records.

```json
{"event":"ACTION_DETAIL","action":"SORT_ALL","action_idx":12,"status":"good",
 "sort":"waveform nearest-neighbour","n_clusters":80,"n_renamed":77,
 "old_to_new":{"2":2,"3":12,"4":55,"5":22,"6":5, ...}}
```

### Logs are append-only

Logs are append-only within a session; reopening Klusters in the same
working copy continues the same log. Logs from many sessions on the
same shank can be combined into a single prior YAML via
`kk_build_prior.py`.

Klusters writes no separate quality-annotation event: the only verdict
in the log is each action's `good` / `bad` status, inferred from undo as
described above. The cluster quality you enter in the cluster information
panel is saved in the clustering's `.cli` file, not in the log.

</details>

---

## Saving

| Action | Shortcut | Description |
|---|---|---|
| Save | `Ctrl+S` | Replaces `session.clu.N` (and every other file the session changed) |
| Save As | — | Saves to a new `.clu.N` path |
| Renumber and Save | `Ctrl+Shift+S` | Renumbers cluster IDs, then saves |

The cluster user information (structure, type, quality, etc.) is saved to the clustering's [`.cli`](../ndmanager-plugins/formats/cli.md) file beside the `.clu`, not to the YAML parameter file (see [Cluster information panel](#cluster-information-panel)).

A save replaces all its files or none of them, skips files that already match,
and locks editing while it runs — see [Saving: all or nothing](#saving-all-or-nothing).

---

## Update modes

**Settings → Immediate Update** (default): all views redraw immediately after every editing operation. Appropriate for small datasets.

**Settings → Delayed Update**: views are not updated until **Actions → Update Display** is triggered manually. Useful for large datasets where recomputing all views after every lasso operation is slow.

---

## Automatic reclustering (KiloKlustaKwik)

Klusters can launch KiloKlustaKwik on the current electrode group:

1. Select the clusters to recluster (or select all).
2. **Actions → Recluster → Recluster** (`Shift+R`).
3. KiloKlustaKwik runs as a subprocess; its stdout streams into the embedded process widget.
4. When KiloKlustaKwik finishes, klusters reads the new clusters back and all views update.

The selected clusters' features go to a temporary
`<base>--<cluster ids>--<date>.fet.standard.N` in the session folder (with a
copy of the session YAML under the same base, so KiloKlustaKwik reads the
sampling rate and geometry), and KiloKlustaKwik writes
`….clu.standard.N` beside it: the names it uses for its default method,
`standard`, so the default arguments do not pass `-Method`. All of them are
deleted afterwards. A program configured under another name (the original
`KlustaKwik`, say) gets the untagged `….fet.N` and is expected to write
`….clu.N`.

The **Actions → Recluster** submenu also holds:

| Entry | Key | What it does |
|---|---|---|
| Recluster (median-waveform residual) | `Shift+M` | Reclusters the selected cluster(s) on the residuals of their pooled median waveform |
| Recluster (channel-variance features) | `Shift+C` | Reclusters using the features of the highest-variance channels |
| Split by KNN voting… | `Shift+K` | Splits the selected cluster by a K-nearest-neighbour vote against well-isolated reference clusters |
| Strip by Template… | `Shift+J` | Uses one selected cluster as a waveform template and pulls matching spikes out of the other selected clusters (current channel selection) |
| DipSplit Selected Cluster | `Shift+D` | See [DipSplit](#dipsplit--automatic-bimodal-split) |

KiloKlustaKwik parameters (executable path, UseFeatures, MaxClusters, etc.) are configured in **Settings → Preferences → Curation → Reclustering**. The program is
`KiloKlustaKwik_cpu` (KiloKlustaKwik's CPU build, installed by every build;
set `KiloKlustaKwik` for a GPU build) by default, with the arguments
`%fileBaseName %electrodeGroupID -MinClusters 2 -MaxClusters 12 -UseFeatures %features`.
Settings saved by earlier versions name `KlustaKwik`, which neurosuite-3 does
not install; when no `KlustaKwik` is on the PATH, Klusters uses
`KiloKlustaKwik_cpu` instead.

### Automatic feature selection

When **Auto-select features** is enabled in the Preferences (and the **N feat** spinbox appears in the Parameters toolbar), klusters computes per-feature variance across all spikes in the selected clusters and passes only the top-N high-variance features to KiloKlustaKwik via the `UseFeatures` parameter.

**Algorithm:**

1. Feature variances are pooled across all spikes from every selected cluster (clusters 0 and 1 are excluded even if selected).
2. Features are ranked by descending variance.
3. The **N feat** spinbox sets a ceiling on the number of features passed.
4. A variance drop-off threshold (5 % of the top feature's variance) trims features that have fallen to noise level — if only 4 of the requested 10 carry meaningful information, only those 4 are passed.
5. At least one feature is always selected.
6. The timestamp dimension (last column) is **off by default** — including the normalized timestamp as a clustering feature makes the reclusterer over-fit within-session drift (spikes get separated by *when* they fired rather than by waveform shape). The **time** checkbox next to the **N feat** spinbox lets you include it when you want it; it shares the spinbox's visibility and its state is persisted in the configuration.

When no clusters are selected or auto-select is disabled, klusters falls back to passing all PCA feature dimensions **with** the timestamp (the historical manual default). The **time** checkbox affects only the auto-select path, not this fallback.

**Channel-level variance selection** (Preferences → Curation → Reclustering) is a variant that ranks whole *channels* by aggregate feature variance and keeps every PCA column of the top **N feat** channels, so a high-variance channel's components are never split across the kept/dropped boundary.

---

## Spike realignment

**Actions → Realign Spikes…** (`Shift+L`) realigns the first selected
cluster (ID above 1) of the active display at once, with the settings in
**Settings → Preferences → Curation → Realignment**; there is no dialog. **Actions →
Align All Clusters** (`Shift+P`) runs the same alignment, with the same
settings (including the Parameters toolbar's top-channels box and the
post-alignment mode), over every cluster except 0 and 1. Before 2026-10-21 it
always added the PCA refine pass whatever the mode, so its results differed
from Realign Spikes'; choose the PCA refine as the post-alignment mode to get
that behaviour.

### How it works

For each spike in the cluster:

1. The cluster mean waveform is computed.
2. The spike's waveform is cross-correlated against the mean template at each lag within ±*Max shift* samples.
3. If the peak correlation exceeds the *Threshold*, the spike is shifted to the optimal lag: its timestamp is updated, the waveform snippet is re-extracted from `session.fil` or `session.dat` at the corrected sample offset, and PCA features are reprojected through the eigenvector basis matching the session method (`pca.stderiv.N` when the method is stderiv, else `pca.standard.N`). A spike whose snippet is all zeros (or a cluster whose mean is all zeros) has no defined correlation; it is never shifted, whatever the threshold is, and its score counts as 0.
4. Each spike is written back to its own row; rows are never reordered, so every method's `.spk`/`.fet` and the `.clu` stay aligned. A spike's new time may pass a neighbouring spike's.

### Pipeline variant awareness

Realignment and nudge respect the session variant resolved from the
[chain-of-custody policy](../ndmanager-plugins/formats/naming.md) (see the
Files section above):

- The `.spk` is **shared** (raw): the re-extracted waveform snippet is
  written back to it raw, regardless of the session method — there is no
  separate stderiv waveform file to keep in sync.
- If the session method is **stderiv**, the re-extracted waveform is
  transformed (spatial derivative + temporal first-difference, with
  `sdiff[-1] = 0` boundary) before the PCA projection, and the
  `pca.stderiv.N` basis is used to update `fet.stderiv.N`. If the method is
  **standard**, no transform is applied and `pca.standard.N` is used.

Because the stderiv domain lives only in the feature / PCA files (not in a
second `.spk`), realign never has to reconcile a raw-vs-stderiv waveform
file — it re-projects through whichever basis the resolved method selects.

### Transactional pending-file model

<details>
<summary>Details</summary>

All edits from realign and nudge go into `.pending` sidecar files
(`spk.<method>.N.pending`, `fet.<method>.N.pending`, etc.) rather than the live
session files. The session files are updated atomically on save:

- On **save**, every file the save changes — the `.clu` (and `.clc`/`.clp`),
  the `.spk`/`.res`/`.fet` a realign or nudge touched, other synced feature
  spaces, the `.cli` file — is first written in full
  next to its target (`<file>.saving`), then all are installed together or
  none is (see [Saving](#saving-all-or-nothing)). A file that already holds
  what the save would write is not rewritten.
- **Undo** (`Ctrl+Z`) of a realignment restores the working copies from
  the saved files — no on-disk changes.
- On crash, the autosave-recovery path, the commit journal (an interrupted
  save is completed on the next open) and the `.bak` files written by
  upstream scripts (reextract, subcluster) give independent recovery
  options.

A realignment is kept as soon as it finishes: the views switch to the
Overview Display with the realigned cluster selected, and the change is
written by the next **Save**. To discard it, use **Undo**, or close without
saving.

Realignment parameters are set in **Settings → Preferences → Curation →
Realignment**: *Threshold* (minimum score), *Iterations*,
*Max shift* (0 = automatic) and an optional post-alignment pass (PCA
projection refine, RMS circular recenter, per-spike centroid). The same page
can realign a cluster automatically after each merge.

</details>

---

## Timestamp nudge

`Page Up` shifts every spike of the selected cluster by +1 sample and
`Page Down` by −1 sample. Exactly one cluster must be shown; there is no
menu entry or toolbar button. Holding the key does not repeat the nudge.
Use to correct systematic clock offsets between a cluster and the
detection peak (for example, after a spike-sorter converted template
lag from milliseconds with slight rounding).

Nudge is a lighter-weight variant of realign: instead of doing
normalized cross-correlation, it applies a uniform known shift to every
spike in the cluster, re-extracts the shifted waveform from `.fil`,
reprojects through the PCA basis (using the same variant-aware logic as
realign), and writes the update into the `.pending` files. Nothing is
written to the session files until **Save**.

Every spike moves the full step and keeps its row, even where that carries
it past a spike of another cluster.

Focus returns to the cluster palette after nudge completes, so you can
immediately step to the next cluster with the arrow keys.

---

## Autosave and crash recovery

While the clustering has unsaved changes, Klusters writes a rescue copy of it
every few minutes (**Settings → Preferences → General → Session**): `.<clu file>.autosave`
next to the `.clu` and, in a hierarchical session, `.<clc file>.autosave` next
to the `.clc`. Both come from one snapshot, taken on the GUI thread, so the
two always belong together; no rescue copy is written while a save runs. A
successful save, or closing the session normally, removes them.

On opening a session, a rescue copy newer than the `.clu` means Klusters did
not close normally; it offers to use it (the old file is kept under a dated
name). In a hierarchical session the atom rescue copy is restored with it when
it fits — same spike count, every atom under one unit — otherwise the existing
`.clc` is kept and Repair Nesting re-cuts what straddles.

### Saving: all or nothing

A save is two phases. First every file it changes is written completely next
to its target as `<file>.saving` and flushed to disk; nothing is replaced yet,
and a failure here (a full disk, a permission) leaves every file as it was.
Then a journal `.<clu file>.commit` naming the files is written, and each file
is moved aside (`<file>.rollback`) and replaced by its new version. If one
replacement fails, the ones already made are undone. If Klusters dies in the
middle, the next open finds the journal and completes the save — every new
version was already complete — and says so.

What is already on disk is not rewritten: the `.clu`, `.clc`, `.clp` and the
`.cli` file are compared with what would be written, and each
`.spk`/`.res`/`.fet` working copy carries a stamp (size, times, inode) taken
when it was last known equal to its file, so an untouched 300 MB `.spk` is
recognised without being read.

While a save runs, the clustering cannot be edited: input to the window is
held, the cursor shows the wait state, and a "Saving…" box appears if it takes
more than a moment. Closing the window then shows "Waiting for the save to
finish before closing…", and afterwards asks to save only if the clustering
still differs from the files (an edit that was undone does not count).

---

## Preferences

**Settings → Preferences** (`P`) opens a tree of three groups; each setting is
described by its tooltip.

| Group | Page | What it sets |
|---|---|---|
| General | Appearance | Light, dark or system theme, shared by all NeuroSuite programs |
| | Session | Autosave interval, undo steps, re-extracting waveforms on save, the curation log |
| | Input | Keyboard and mouse bindings, keymap layouts |
| | Radial menu | The right-click wheel |
| Views | Display | Background, marker size, selection lines, autoscale margin, printing, Templates mode |
| | Cluster view | Time lines, t-SNE, template projection scope, density heatmap |
| | Waveform view | Default gain, channel positions |
| | Matrices | Showing and updating the matrix displays, error-matrix compute, template matrix metric and slider, drift-slider limit |
| Curation | Realignment | Threshold, iterations, maximum shift, post-alignment, realigning after a merge and from any feature space |
| | Splitting | DipSplit and the KNN voting split |
| | Merging | The recommended-merges panel and Auto-Merge |
| | Cluster order | Reorder by similarity, median or mean waveforms for the waveform sorts, renumbering after cluster edits |
| | Reclustering | KiloKlustaKwik executable and arguments, feature selection |

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
| N feat | All (when auto-select is on) | Maximum number of features passed to KiloKlustaKwik |
| time (checkbox) | Next to **N feat** (when auto-select is on) | Include the spike timestamp as a clustering feature (off by default; time over-fits within-session drift) |
| Space | Always (far right, document open) | Method space whose features and waveforms are loaded; pick another to view the same clusters in it — see [Feature spaces](#feature-spaces--one-clu-through-another-method) |

---

## Printing

**File → Print** (`Ctrl+P`) prints the currently active view to the system print dialog.

---

## GPU acceleration

The Grouping Assistant matrix computation and the spike realignment cross-correlation use the same GPU dispatch as KiloKlustaKwik: CUDA → HIP → SYCL → OpenMP CPU fallback. The CPU path is always compiled as a baseline. For realignment, the SYCL backend is used only when the environment variable `KLUSTERS_USE_SYCL=1` is set.

See the [GPU installation guide](../gpu/README.md) for CUDA, ROCm/HIP, and Intel oneAPI/SYCL setup.

---

## Installation

Klusters is built and installed with the rest of neurosuite-3; see
[Installing neurosuite-3](../INSTALL.md). The pages below are short notes
per platform that point into it.

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
