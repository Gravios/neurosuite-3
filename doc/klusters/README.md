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
  run **in the space shown** (Preferences → Curation → Realignment → *Realign
  and nudge in the shown feature space; every other space follows*, on by
  default): they align on the shown space's waveforms and basis — sub-sample
  lags in a super-sampled space, whichever space the `.clu` lives in — and
  rewrite that space's working copies and the shared `.res`. Every other
  space, the `.clu`'s own included, then follows the spikes that moved: each
  moved spike is re-extracted from the `.fil` into every other waveform file,
  in that file's own domain and geometry (a super-sampled file at the phases
  the realign found), and re-projected into every space's features. Only the
  moved spikes are touched; a space that was already behind gets a full sync
  instead. The results are working copies, written by **Save**. This costs a
  `.fil` read of the moved spikes and a rewrite of them in each space per
  realign or nudge; with the preference off, these operations are off while
  another space is shown and the other spaces keep their old spike times
  until synced. Not in a lag (`_D`) space — show its waveform space. A Save
  As into another space refuses while that space holds an unsaved realign.
- **Save As**, **decollide** and **template generation** still need the
  `.clu`'s own space. **Recluster** works, using the shown space's features.
- **Ctrl+Tab** shows the next space and **Ctrl+Shift+Tab** the previous one
  (View → Next / Previous Feature Space; rebindable in Preferences →
  Input). Spaces that cannot be loaded are skipped. Plain Tab / Shift+Tab
  still move keyboard focus around the window.
- **Shift+Y** (View → Local PCA of the Selection) shows the selected
  clusters in a space of their own: a per-channel PCA fitted on their spikes
  alone — the parents shown, or the children selected in the child view —
  and applied to every spike. The axes are then the directions in which
  those clusters differ, channel by channel, not the directions of the whole
  group, so sub-groups that the session's features overlay can come apart.
  It takes the place of the features on screen, with the same number of
  dimensions (the window and the components per channel are the shown
  space's; the time column stays), and appears in the *Space* box as
  *local PCA*. **Shift+Y** again, or any other space, leaves it. Nothing is
  written. Cutting, grouping and **Recluster** work in it; **Realign** and
  **nudge** leave it first and run where they would have. A new selection
  needs a new fit: leave and press the key again.
- The box is locked while saving, realigning or reclustering.
- Every base-rate space is held in memory: its `.fet` table and its `.spk`
  waveforms are loaded when the document opens (and refreshed after a switch,
  a sync or a Save, reading only files that changed), so switching, syncing
  and waveform display never wait on the disk. The status bar says how much is
  held. This costs memory: roughly the size of each space's `.fet` plus each
  distinct `.spk` (a `_D` space shares its waveform token's).
- A **super-sampled** (`_U`) space is the exception: its `.spk` is 2× or 4×
  the waveforms, so it is read when you first switch to it, not at open. That
  first switch waits for the read (the status bar says so, and the *Space*
  tooltip says which spaces are not in memory yet); from then on the space is
  held like any other until the document closes. A session whose `.clu` is
  itself saved under a `_U` token still opens in that space — it is the
  `.clu`'s own.

This works because every file of a group is indexed by the same spike row,
and Klusters keeps it that way: realign and nudge rewrite each spike in its own
row and never move rows. A spike's new time may pass a neighbouring spike's;
that does not matter, because every `.spk` and `.fet` follows the one `.res`.

**Putting rows back in time order.** **Spikes → Sort Spikes by Time** sorts
the saved `.res` by time and moves the same rows in every file of the group that
is indexed by spike: the group's `.res`, each waveform file (`.spk`), each
method's `.fet`, every `.clu` and `.clc` (stage-tagged copies too), every
`.eap` template-class table of the group, and every sparse `.spkc` (its index
is renamed to the rows' new numbers). A file
shared by several spaces — the one `.res`, or the `.spk.stderiv_C5` that
`stderiv_C5_D34` also reads — is one file and is permuted once. It works on the saved files, so save
first; it then reopens the document, which clears undo history. A file without
the group's spike count is left as is, and a `.col` decollide sidecar is
listed as not reordered (regenerate it). The sort
closes the document, rewrites the files, and opens it again; if the document
cannot be closed yet, nothing is sorted. All files are written first and
swapped in together; if anything fails, every file is left as it was.

**Keeping spaces in sync.** Realign and nudge rewrite the shown space's
`.spk` and `.fet` (working copies) and the shared `.res`; the other spaces
follow the moved spikes (above). A `_D` space has no waveform file of its
own — it reads its waveform token's (`stderiv_C5_D34` reads
`.spk.stderiv_C5`) — so with `stderiv_C5` shown the lag space's waveforms are
already current and only its `.fet` is followed; a space over a different
waveform file (`standard`, say) gets both. A space counts as current when
its feature times are the session's and, super-sampled, its phases are the
group's (one set per factor, in the tick `.res`); one that is behind gets a
full sync instead of a follow. **Spikes → Sync Feature Spaces** brings every other space up
to date: it re-extracts each waveform file the `.clu` space does not use from
the `.fil` at the current spike times (once, however many spaces read it) and
re-projects every other space's `.fet` through its own `.pca` basis (lag `_D`
spaces included). To do this automatically, turn on **Preferences →
Curation → Realignment → Sync a feature space to the current spike times when switching to
it**; each switch then costs one `.fil` pass over the group. Synced files are
working copies: **Save** writes them and closing without saving discards them.
A space that cannot
be synced — no `.pca` basis, a basis that does not fit the waveforms, or one
trained in the other domain (standard vs stderiv) — is reported and keeps its
existing files. Spaces that read the same `.spk` are synced together, and if
any one of them cannot be, none of them is. The follow after a realign or
nudge (*Realign and nudge from any feature space*) works per waveform file
in that file's own geometry: a super-sampled `_U` space is re-cut at
`factor` ticks per sample at each spike's own phase (a move made in another
space is a whole-sample move for it), so it follows like any other space
instead of waiting for a full sync. (Exercised headlessly by
`src/klusters/test/selftest/test_follow_supersampled.py` on the built binary —
see `src/klusters/test/selftest/README.md`.)

The *Space* box rescans the session directory each time it is opened, so a
space made beside an open session (`ndm_extractspikes --supersample`, `ndm_pca`,
fiber-kit) is listed without a reopen.

### Detached feature spaces

A `.fet.<token>.N` with no basis (`.pca.<token>.N`) is a **detached** space: a
per-spike feature table that is not a projection of the waveforms — fiber-manifold's
`.fet.manifold.N` (snr, lag, position, …) is one. It is listed and can be shown and
cut like any space (its waveforms are the `.spk` custody resolves for the token,
usually `standard`), but it is never re-projected: when spikes move, its feature
values stay and only its time (and phase) column follows, and it never blocks the
sync or the follow of the spaces it shares waveforms with. Realign and nudge do not
run in it ("not a projection of waveforms"); show a waveform space first. The
*Space* tooltip says when a space is detached.

### Super-sample mode — a space at 2× or 4× the sampling

For machines with the memory and cores to spare, a feature space can be built
over **super-sampled** waveforms: each acquisition sample interpolated into 2
or 4 ticks — a **smoothing spline** by default (a cubic spline through
Whittaker-smoothed samples, a low-pass of about 0.6 Nyquist: the spike is
untouched, the sample-to-sample noise an exact interpolant reproduces between
the samples is not), or the exact natural cubic spline, or a windowed sinc. A
super-sampled **stderiv** space is derived from the interpolated raw signal: its
temporal part is the interpolant's derivative per raw sample (on the base-rate
amplitude scale, tick `4k` standing where sample `k`'s derivative stands), not
the difference of two rounded ticks. The same cut is available outside Klusters
as `ndm_extractspikes --supersample 4 [--interp smooth|spline|lanczos3]`
(`--method stderiv_C5 --supersample 4`, or `--method stderiv_C5_U4`), which
writes `.spk.<token>_U4.N` and the group's tick `.res`; Klusters then fits the
basis and the `.fet` on those waveforms instead of cutting its own. A space is
re-cut (realign, nudge, sync, follow) with the interpolator it was built with,
whatever the preference says now: Klusters detects it from the file's rows
(a sample re-cut with each interpolator; the one that reproduces the rows wins,
and the log says when it differs from the preference). The preference decides
new spaces only.
**Preferences → Curation → Realignment → Super-sampling** sets
the **mode** (off by default, ×2, ×4): in a mode, every session that opens
gets the super-sampled space over its `.clu`'s space built, if its files are
not there yet. When the waveforms (`.spk.<token>_U4.N`) are not there Klusters
asks first, naming the file and its size: **Yes** builds the `.spk`, its PCA
basis and `.fet`; **No** builds nothing, turns the mode off in the preferences
and carries on at the recording's sampling (with the `.spk` there and only the
`.fet` missing, the `.fet` is fitted without a question). The session still opens in the `.clu`'s own space; the
super-sampled one is read when you switch to it (the *Space* box, Ctrl+Tab).
With *Also save the clustering under the
super-sampled token* the `.clu` is saved as `<base>.clu.<token>_U4.N` and
reopened there, so realign and nudge run at sub-sample resolution without a
further step. A super-sampled `.clu` opened while its `.spk.<token>_U4.N` is
missing (copied without the big file) is rebuilt the same way through the base
space, after the same question; with **No** the session stays in the base
clustering. **Spikes → Super-sample ×2 / ×4…** does it by hand; it builds, over the
`.clu`'s space, `<base>.spk.<token>_U4.N` (every spike
re-cut from the `.fil` with a margin of raw samples on either side so the
edges are exact; from the stored windows when there is no recording), a PCA
basis fitted on it (`.pca.<token>_U4.N`: the shown basis's window and
component count, scaled), `.fet.<token>_U4.N` — the projections, a
**phase** column and time — and, if it is the first space at ×4, the
group's tick `.res`: **one per factor for all spaces**, named after the
shared `.res` (`.res.stderiv_S3.6` → `.res.stderiv_S3_U4.6`), the spike
times in **ticks** (`sample × 4 + phase`). A second ×4 space built later
(`stderiv_C5_U4` after `standard_U4`, say) takes its phases from it, so
every ×4 space carries the same sub-sample times; a realign in one of them
hands its phases to the others (the follow), and every save rewrites the
tick `.res` from a ×4 space's columns. The shared base-rate `.res` is a
different file and never a `_U` one. The space is then listed under *Space* like any
other; its waveforms show 4× the points, every matrix and the realign see a
4× longer window. The `_U<factor>` token belongs to the waveform token
(`standard_U4`, `stderiv_C5_U4`; a lag space over it keeps it:
`stderiv_C5_U4_D34`); the group's `nSamples` and `peakSampleIndex` in the
session file stay those of the recording, Klusters scales them per space.

**Realign and nudge at sub-sample resolution.** They run in the space
shown, so showing the super-sampled space is enough; the `.clu` may stay
where it is (its own space follows) or move there with *Save As*
`<base>.clu.<token>_U4.N` (offered when the space is built, or automatic in
the mode). A Save As into a method whose space already exists **adopts** that
space: the `.clu` moves, the space's files stay (the origin's pending copies
are not written over them; an unsaved realign in either space must be saved
first) and the session reopens there. A realignment in the super-sampled
space finds each spike's lag in **ticks**; the `.res`
keeps whole acquisition samples (a lag of −5 ticks at ×4 moves the `.res`
by −2 samples) and the rest is the spike's **phase**, the space's last feature
column (0 … factor−1): tick `k` of the waveform stands at raw time
`res + (k − peak + phase) / factor`. The window is re-cut from the recording at
exactly that fractional time, the stderiv transform applied in the
super-sampled domain, the features re-projected and the phase column updated;
the shift cap in the realign dialog is in ticks. A nudge of ±1 sample keeps
the phase. **Sync Feature Spaces** re-cuts a super-sampled space at its
spikes' phases. The phase lives in the `.fet`, so every row
operation (sort by time, sub-sessions, recluster plugins) carries it for free;
a `.fet` of such a space has `nCh × nComp + 2` columns.

**Export at the recording's sampling** (*Spikes*, from a saved `_U`
session): copies the saved `.clu` — with its `.clc`/`.clp`/`.cli` — under
the base-rate token (`<base>.clu.<token>.N`), beside the base-rate files it
reads: the shared `.res` (whole samples, moved by every realign or nudge in
the `_U` space; the phase stays in this space's column and tick `.res`) and
the base space's `.spk`/`.fet`, which *Realign from any space* re-cuts at the
moved times (run *Sync Feature Spaces* and *Save* first if that was off).
The session stays open where it is; the dialog lists what was written and
the base-rate files as they stand. (A *Save As* under the base token does the
same move and reopens there.)

Costs: the `.spk` and every waveform read are ×factor, the PCA basis works on
a ×factor window (fitted on up to 20 000 spikes), realign's cross-correlation
on ×factor ticks. fiber-kit reads such a space like any other (`FK_MANIFOLD_SPK
standard_U4`, say) once it knows the token (fiber-kit 0.66.5), and the
`fiber-kit-manifold` plugin follows the shown space by itself: its
`FK_MANIFOLD_SPK` defaults to the `%spk` context token (fiber-kit 0.66.6).

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
- **Top — Toolbars** (each can be hidden from **View → Toolbars and Panels**):
  - **Main**: Open, Save, Print, Undo, Redo.
  - **Actions**: Delete Artifact Cluster(s), Delete Noisy Cluster(s), Update Display, Update Error Matrix, Reorder Clusters by Similarity, Group Clusters, Auto-Merge.
  - **Tools**: New Cluster, Split Clusters, Multi-Lasso, Split by Lassos, Delete Artifact Spikes, Delete Noisy Spikes.
  - **Cluster bar**: Previous Spike, Next Spike (trace view).
  - **Parameters**: feature axes, waveform and correlogram settings, the feature space box — see [Parameters toolbar reference](#parameters-toolbar-reference).

- **Menu bar**: the commands by what they do.

  | Menu | What is in it |
  |---|---|
  | **File** | open, import, save, print, close |
  | **Edit** | undo and redo (of both layers), Cancel Running Job, select all, **Preferences** |
  | **View** | what the left column shows, the time chunks, the feature space shown, Update Display and Update Error Matrix; the settings of the waveform, correlogram and trace views (**Waveforms**, **Correlations**, **Traces**); the toolbars, the point size and the update mode |
  | **Clusters** | whole clusters: group, delete, renumber, purge, auto-merge, templates, **Sort Clusters** |
  | **Split** | cutting a cluster: watershed, DipSplit, KNN, by time, burst followers, spurious spikes, the strip commands, the oblique basis |
  | **Recluster** | the recluster commands and the **Engine** they run |
  | **Spikes** | spike times and features: realign, sort by time, feature spaces, super-sampling, drift |
  | **Hierarchy** | parents and children |
  | **Tools** | the mouse tools of the cluster view |
  | **Displays** | open, rename and close displays |
  | **Plugins**, **Help** | the installed plugins; the shortcuts, the handbook |

  Until 2026-11-24 the Clusters, Split, Recluster and Spikes commands were one **Actions**
  menu, Waveforms, Correlations and Traces were on the bar, and a **Settings** menu held
  Preferences and the toolbar toggles. Key bindings and radial-menu layouts made before
  are unaffected.

The **View** menu shows or hides parts of the left column. Each entry starts
checked and holds until Klusters closes.

- **Show Reserved Chunk Clusters**: unchecked, the reserved chunk clusters
  leave the cluster palette and the displays
  ([Reserved chunk clusters](#reserved-chunk-clusters)).
- **Show Child Cluster Palette** (hierarchical session): unchecked, the child
  palette is hidden. The selection, the displays and the matrices then work on
  the parent clusters, the Hierarchy menu's edits are disabled, and `V` waits
  for the palette.
- **Show Template Palette** (Templates mode): shows or hides the template
  palette.
- **Follow Selection in Matrices** (on by default): a cluster picked in the
  parent palette, or an atom picked in the child palette, centres every open
  curation matrix on its diagonal cell -- the view pans, the zoom is kept.
  With several selected, the palette's current item (the one clicked or
  reached with the keyboard) is centred. A matrix still computing centres
  when its result lands; a cluster that is not in the matrix shown (a fiber
  picked while the atom matrix is up) is centred by the next result that
  holds it (the parent matrix coming back) and forgotten otherwise, and a
  selection made by clicking a matrix cell leaves the matrix where it is. Whatever the setting,
  a matrix panned completely off screen is brought back to its upper-left
  corner at the current zoom (the cross-connected matrices follow).

### Radial context menu (experimental)

**Right-click** in a view (cluster, waveform, correlogram, trace, the curation
matrices) opens a ring menu centred on the cursor. Two right-clicks keep their
own meaning: while a lasso is being drawn, Right still removes the last point,
and with the lineage overlay engaged it opens the overlay's menu. By default the root ring holds Tools,
Clusters, Recluster, Undo, Redo, Hierarchy, Sort, the two feature axes
**X: n** and **Y: n**, and **N: n**; hovering a group opens its actions as the next ring out. An open ring is not closed by a cursor that is still moving: its ring is wider than the group's own wedge, so the straight way to most of its entries crosses a neighbouring group, and that crossing is ignored. Another group takes over once the cursor **stops** on it (stays within 4 px for 100 ms); the same holds for going back to an inner ring or to the hub. A click, a release or a wheel step always acts on the wedge under the cursor at once. Sort holds the Clusters ▸ Sort Clusters entries
under short labels (Count, Time, SNR, Spectral, …); the status bar shows each
one's full name. Recluster holds the Recluster action and a **Plugin** wedge
whose ring chooses the **active engine** -- **KlustaKwik** (the built-in
recluster) or **fiber-kit** (its plan on the selection) -- the active one
marked with the checked dot; then the other split tools. **Recluster
(`Shift+R`) runs whichever engine is active**, as does the Recluster wedge, so
switching engine is one flick and the shortcuts follow. The same choice is
*Recluster ▸ Engine*, where every installed `recluster` plugin is an
entry too (placeable on the wheel from the catalogue in Preferences ▸ Radial
menu). `Shift+M` / `Shift+C` stay KlustaKwik on other features, `Shift+Q` is
fiber-kit explicitly. A layout saved before this ring existed keeps its own
shape; *Default* on that page restores the shipped wheel.

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
including the ones you have changed. Bindings are edited in **Edit →
Preferences → Input**, which also saves and loads named keymap layouts
(*Keymap layout*: Apply, Save As…, Rename, Delete, Reset all to defaults).
Each row's **Reset** restores that command's shipped binding. On a row that is
already at its default the button reads **Unbind**: a press clears the binding
and the command keeps none (mouse rows show *(none)*) — so pressing twice goes
default, then unbound; a third press brings the default back. An unbound
command is saved as such, in the settings and in a keymap layout.
The defaults for the most-used actions:

| Key | Action |
|---|---|
| `Ctrl+O` / `Ctrl+S` | Open / Save |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+A` / `Ctrl+Shift+A` | Select all clusters / all except 0 and 1 |
| `S`, `Esc` `Esc` | Toggle a cluster in the palette selection; clear the selection |
| `G` / `Shift+G` | Group the selected clusters / Auto-Merge |
| `Delete` / `Shift+Delete` | Move the selected clusters to noise (1) / artifact (0) |
| `Ctrl+1` / `Ctrl+2` | New Cluster / Split Clusters lasso tool |
| `Ctrl+3` / `Ctrl+4` / `Ctrl+5` | Delete Artifact Spikes / Delete Noisy Spikes / Multi-Lasso tool |
| `R` / `T` | Renumber clusters / move the selected clusters to the end |
| `U` | Update the error, template and residual matrices |
| `E` | Next matrix tab |
| `Shift+D` / `Shift+W` | DipSplit / Watershed split |
| `Shift+R` | Recluster with the active engine (Recluster ▸ Engine: KlustaKwik, fiber-kit, a plugin) |
| `Shift+Q` | Recluster with fiber-kit (the plan of `fiber-kit.yaml` on the selection) |
| `Shift+L` / `Page Up` / `Page Down` | Realign / nudge the selected cluster +1 / −1 sample |
| `A` / `F` | Auto-scale / t-SNE presentation in the Cluster Display |
| `O` / `M` / `I` / `D` | Waveforms: overlay / mean and SD / amplitude up / down |
| `=` / `-` | Point size up / down |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous feature space |
| `Shift+Y` | Local PCA of the selection, in place of the shown features (again to leave) |
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

**View → Toolbars and Panels → Show Cluster Info** expands each cluster button to show the user-editable metadata fields (the non-empty ones, in this order):

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

After any operation that auto-selects the next cluster in the palette (delete-to-noise, delete-to-artifact, recluster completion, realign completion), focus returns to the palette rather than to the 2-D scatter. This means arrow keys continue to step through clusters without requiring a Tab press to reach the palette.

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

**Presentation modes** (View → Waveforms):

| Mode | Key | Description |
|---|---|---|
| Sample mode (default) | — | Shows the N most-recent spikes per cluster (N set in Parameters bar) |
| Time Frame mode | — | Shows all spikes from a time window set in the Parameters bar (start + duration in seconds) |
| Overlay | `O` | All clusters drawn in the same color space on top of each other |
| Mean ± SD | `M` | Shows the cluster mean waveform with ±1 SD envelope; hides individual spikes |

**Amplitude controls** (View → Waveforms):

| Action | Key |
|---|---|
| Increase amplitude (all channels) | `I` |
| Decrease amplitude (all channels) | `D` |

**Quality dot.**  A dot in the upper right corner of each cluster's waveforms (side by side: of its
column; overlaid: the dots line up at the view's upper right corner, in cluster order).  A dot is
the quality colour alone, with no perimeter.  It is there for parents and for children alike, and is computed on the
spikes the view holds for the cluster (the N of sample mode, or the time frame).

Its colour is the **normalised residual variance** of those spikes about their median waveform: the
mean squared difference from the per-point median, less the background (the mean square at the
first and last tenth of the window, per channel), summed over the rest of the window and all
channels, and divided by the median waveform's energy over the same points.  0 is a cluster whose
spikes differ from their median by the background only; 0.25 one whose spikes carry a quarter of
the waveform's energy in differences from it.  Hover the dot for the number.

| Colour | Value (defaults, Preferences ▸ Waveform view ▸ Cluster quality dot) |
|---|---|
| green | below 0.12 |
| yellow | 0.12 – 0.27 |
| orange | 0.27 – 0.52 |
| red | 0.52 and above |
| empty white circle | fewer than 30 spikes held: no value |

The defaults are the quantiles measured in a `stderiv_C5` space; raw (`standard`) waveforms read
lower (about 0.00 / 0.05 / 0.20 for the same split).  The dot is a hint, not a verdict.  A second
cell raises the value, and so do bursts (amplitude), drift (shape) and misalignment; two cells of
similar shape and amplitude do not.  On `sirotaA-jg-000005-20120312` group 8, over 594 pipeline
parents and children scored against the curated file, clusters under 90 % pure were told from
clusters at 98 % or more with AUC 0.71–0.74: of the fifth with the lowest values 10–15 % were
impure, of the fifth with the highest 55–60 %.  Clusters that span a long session read higher than
that (a cell's waveform changing over hours counts as residual): of the curated parents of that
session 42 % are red at the defaults.  Time frame mode measures a stretch of the session instead;
raise the thresholds if most of a curated session reads red.

### Correlogram Display

Shows auto-correlograms (diagonal) and cross-correlograms (off-diagonal) for selected clusters.

**Parameters** (set in the Parameters toolbar):

| Parameter | Description |
|---|---|
| Bin size | Width of each correlogram bin in milliseconds |
| Half duration | Time span on each side of zero lag (total window = 2 × half duration) |

**View → Correlations:**

| Entry | Key | Description |
|---|---|---|
| Scale by Maximum | — | Normalize each correlogram to its own peak |
| Scale by Asymptote | `Shift+A` | Normalize to the flat baseline (shoulder) |
| Uniform Scale | `Shift+U` | No normalization — raw counts |
| Asymptote Line | `L` | Show the baseline level as a line |
| Increase / Decrease Amplitude | — | Scale the correlograms up or down |

Correlograms are computed on a background thread and update asynchronously. `View → Update Display` forces a recompute.

### Overview Display

The default view on open. Shows the Cluster Display (scatter) and Waveform Display side by side in a single tab. All controls from both views are available.

### Grouping Assistant Display

Computes a cluster × cluster misclassification probability matrix using a probabilistic classifier (`GroupingAssistant::computeMeanProbabilities()`).

Entry (r, c) is the mean posterior probability that a spike from cluster r was actually generated by cluster c. The diagonal should be close to 1.0; high off-diagonal values indicate:

- High (r, c) alone → spikes of cluster r are frequently misclassified as cluster c (possible contaminant).
- High (r, c) **and** (c, r) symmetrically → the two clusters are strongly overlapping and are candidates for merging.

**View → Update Error Matrix** (`U`) recomputes the matrix after editing. `U` also refreshes the template and residual matrices that are open.

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
(**Clusters → Mark/Unmark as Template**) and their linked waveform series
(`.wtf` / `.wti`). Pick a unit and a link (drift / adapt) and scrub its bins;
select two or more units to pin them as the [oblique basis](#oblique-projection-and-decollide).
See [Template library](#template-library).

### Trace Display

Shows raw or high-pass filtered signal (`session.dat` or `session.fil`). Spike timestamps from the current electrode group are overlaid as colored tick marks.

- Navigate with the start / duration fields in the Parameters toolbar.
- **View → Traces → Next Spike** (`Ctrl+Shift+Right`) and **Previous Spike** (`Ctrl+Shift+Left`) scroll the trace to the next or previous spike of the selected clusters.
- **View → Traces → Increase / Decrease Channel Amplitudes** (`Ctrl+Shift+I` / `Ctrl+Shift+D`) scale the selected channels.
- **View → Traces → Show Labels** (`Ctrl+L`): display channel names on the trace view.
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
| Delete selected spikes → artifact (0) | `Ctrl+3` (Tools → Delete Artifact Spikes) | Moves lasso-enclosed spikes to cluster 0 (artifact) |
| Delete selected spikes → noise (1) | `Ctrl+4` (Tools → Delete Noisy Spikes) | Moves lasso-enclosed spikes to cluster 1 (noise/MUA) |
| Strip feature outliers (5σ) | menu only (Split → Strip Feature Outliers) | Across **all** clusters, moves every spike lying more than 5σ from its cluster's per-dimension feature mean (in any single feature dimension) into the artifact cluster (0). A non-mutating scan runs first and asks for confirmation with the exact spike/cluster count; undoable. Per-dimension (not Mahalanobis) is deliberate — a raw 5σ Mahalanobis cut in ~24-D would flag roughly half of every cluster |
| Delete cluster(s) → artifact (0) | `Shift+Delete` | Moves all spikes of selected clusters to cluster 0 |
| Delete cluster(s) → noise (1) | `Delete` | Moves all spikes of selected clusters to cluster 1 |
| Renumber selected to end | `T` | Reassigns the selected cluster IDs to be greater than the current maximum, moving them to the tail of the palette |
| Undo | `Ctrl+Z` | Undoes the last editing operation |
| Redo | `Ctrl+Y` | Redoes the last undone operation |
| Renumber clusters | `R` | Reassigns cluster IDs to be contiguous starting from 2 (0 and 1 are always noise/artifact) |
| Renumber and Save | `Ctrl+Shift+S` | Renumbers then saves in one step |
| Update Display | Actions toolbar | Forces all views to recompute from current cluster assignments |

The cutting tools (`Ctrl+1` … `Ctrl+4`, and Multi-Lasso `Ctrl+5`) are
toggles: pressing the key, button or menu entry of the active tool again
returns to the normal cursor.

**Cluster 0** is the artifact pseudo-cluster (waveforms that are not spikes). **Cluster 1** is the noise/MUA pseudo-cluster. Both are always shown at the top of the palette. Spikes sent to these clusters are not deleted — they remain in the feature file and can be retrieved by undo.

Operations that change cluster **order** rather than membership — Reorder
by Similarity (`Shift+S`) and the standalone sorts (by spike count, time,
contamination, SNR, waveform, …) — live under **Clusters → Sort Clusters**
and are documented separately in
**[Cluster sorting and reordering](cluster-sorting.md)**.

### Reserved chunk clusters

A fiber-kit chunk run (`FK_CHUNKS`) sorts only some chunks of a recording.
Every spike of a chunk it did not sort goes to that chunk's **reserved chunk
cluster**: id 16 777 116 + chunk, in the block 2^24 − 100 … 2^24 − 1
([reserved ids](../ndmanager-plugins/formats/clu.md#reserved-ids)). Klusters
protects these clusters. When an operation on the clusters you selected leaves
one out, the status bar says so. Operations over all clusters (Purge Small
Clusters, the feature-outlier strip, Auto-Merge, Align All, the sorts) leave
them out without a note.

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
- **Align All and the automatic realign after an edit skip them.** Realign on
  a selected cluster still runs.

Klusters shows a reserved chunk cluster as **R** and its chunk: R0 … R99, the
chunk as fiber-kit numbers it (`FK_CHUNKS` counts from 0). Chunk 14's cluster,
16 777 130, is R14. The palettes, views, matrices, messages and dialogs use the
label, and hovering over R14 in the palette shows its id. The files Klusters
writes, the curation log and plugin arguments keep the number. Set Oblique
Basis accepts R14 as well as 16777130.

**View ▸ Show Reserved Chunk Clusters** hides them: unchecked, they leave the
cluster palette and every display that shows one, and Select All skips them.
The clustering does not change, nor do the operations over all clusters or the
child palette (an atom is not a reserved cluster). Checked again, they are
listed again but not selected.

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

Preferences live under **Edit → Preferences → Curation → Splitting**
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

1. Filter candidates (skip 0 = artifact, 1 = noise, anything below the
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
embedding as in the scatter, and so does Watershed (`Shift+W`): the basins
are found in the embedding's own coordinates and the split applies exactly
the basins previewed, on fibers or on the atoms selected in the child
palette; the embedding stays up after the split.

### Multi-Lasso

**Tools → Multi-Lasso** (`Ctrl+5`): a closed lasso is kept instead of cutting.
Draw several, then **Tools → Split by Lassos** (`Enter`): each overlap
pattern becomes a cluster, so two overlapping lassos give three. What the
lassos leave behind in a source cluster is a new cluster as well, and the
original id goes away with it, so every piece of the split is a new cluster
and the selection lands on all of them (the noise and artifact bins and the
reserved chunk clusters keep their ids). `Backspace` removes the last kept
lasso and `Esc` discards them all.

### Oblique projection and decollide

`Shift+O` projects the shown spikes onto axes given by the selected
clusters' templates (the oblique, template-axis view); `Shift+O` again
returns. **Split → Set Oblique Basis…** pins chosen clusters as the axes,
so that a different cluster can then be selected and projected onto them.
In the oblique view a New Cluster lasso waits for confirmation: `Enter`
cuts the new cluster, `Esc` discards the lasso, and `D` instead
*decollides* the lassoed spikes against a pinned two-cluster basis.

### Overlays

- `Shift+H` shows a point-density heatmap over the scatter, the oblique view
  or the t-SNE embedding.
- Moving the cursor over the points tracks the nearest spike (within 32
  pixels): a ring marks it, the status bar names it, and the waveform view
  draws that spike over its cluster's waveforms in a colour that contrasts
  with the cluster's. It works in the scatter and in the oblique / t-SNE
  embedding, pauses while a button is held, and `Shift+V` turns it off and on in the
  view it is pressed in. **Preferences → Cluster view → Spike under the cursor** turns
  it off or on for every view and sets the ring's radius (the point size plus a margin)
  and line width and the width of the trace drawn over the waveform view.
- `Shift+E` toggles the lineage overlay (edit mode), in which a left click
  marks lineage nodes. It needs Templates mode (**Preferences → Views → Display**).

### Matrices

- `V` toggles child-scoped matrices in a hierarchical session: the matrices
  then show the atoms of the selected fiber(s) instead of all clusters.
- `E` steps through the open matrix tabs.

### Template library

- **Clusters → Mark/Unmark as Template** toggles the selected clusters as
  template units. On **Save** their linked template waveforms (`.wtf`) are
  written in every available method.
- **Clusters → Generate Template for Selected Cluster(s)** runs fiber-template
  for the selected clusters now (save first: it reads the files on disk).
- **Displays → New Template Library Display** shows the library.
- **Split → Strip by Template…** (`Shift+J`) uses one selected
  cluster as a template and pulls matching spikes out of the other selected
  clusters.

### Probe drift

- **Spikes → Generate Probe Drift…** runs `ndm_estimatedrift` on the group
  and writes `SESSION.drift` (the group must already have a `.clu`).
- **Spikes → Apply Drift + Recluster Siblings…** (`Shift+F`) computes
  drift-adaptive chunk boundaries from `SESSION.drift` and can re-run
  KlustaKwik on the other groups of the same probe.
- Neither starts while a recluster, another drift job or a plugin is
  running.
- **Displays → New Drift Matrix Display** shows which cluster pairs are one
  unit split by drift.

### Other entries of the Clusters, Split, Spikes and View menus

| Entry | Key | What it does |
|---|---|---|
| Split ▸ Partition Cluster by Time… | `Shift+T` | Cuts the selected cluster into consecutive blocks of the length you enter (minutes), counted from the start of the session |
| Clusters ▸ Mark Collisions… | — | Searches the selected clusters (in the child view: the selected children) for rows that hold a second cluster's spike and, after a confirmation, names the partner in the `.eap`: the rows are then shown with the partner subtracted. See [Mark Collisions](#mark-collisions--the-collisions-no-sorter-named) |
| Clusters ▸ Purge Small Clusters… | — | Moves every cluster with fewer spikes than the number you enter to noise (1), after a confirmation In the **child view** it purges children instead: the children, below that number, of the parents being curated (the ones the child palette lists) are dropped to noise as **Drop to Noise** does, in one undo step; other parents' children are not touched. |
| Split ▸ Strip Feature Outliers (5σ)… | — | Moves spikes more than 5σ from their cluster's mean in any feature dimension to artifact (0), after a confirmation |
| Split ▸ Strip Waveform Outliers… | `Shift+X` | Moves each selected cluster's worst-fitting spikes (residual to its median waveform) into a new cluster, after a confirmation |
| Split ▸ Split Burst Followers | — | With two similarly shaped clusters selected, splits the spikes of each that fall in bursts led by the other into a new cluster; with three, two references sort the third; see [below](#split-burst-followers--two-similar-cells-told-apart-by-their-bursts) |
| Split ▸ Split Off Spurious Spikes | Ctrl+Space | For each selected cluster, keeps its core and splits the spikes that are not it into a new cluster; see [below](#split-off-spurious-spikes--a-clusters-core-and-the-rest) |
| View ▸ Time-Chunk Mode | — | Steps through the session one chunk of the length you enter at a time, with only that chunk's clusters active; **Previous Chunk** / **Next Chunk** move between chunks |
| Spikes ▸ Sync Feature Spaces, View ▸ Next / Previous Feature Space, Spikes ▸ Sort Spikes by Time | — / `Ctrl+Tab` / `Ctrl+Shift+Tab` / — | See [Feature spaces](#feature-spaces--one-clu-through-another-method) |
| View ▸ Local PCA of the Selection | `Shift+Y` | A per-channel PCA fitted on the selected clusters alone, shown in place of the features; see [Feature spaces](#feature-spaces--one-clu-through-another-method) |

### Split Burst Followers — two similar cells, told apart by their bursts

A bursting cell's later spikes are smaller than its first, and when a second cell has a
similar waveform they are easily clustered with it. The waveform cannot settle it; the
timing can, because two different cells seldom fire one behind the other inside a burst
interval. Select the two clusters and choose **Split → Split Burst Followers** (no
key; bind `action.splitBurstFollowers` in Preferences → Input).

- **Bursts.** The two clusters' spikes are pooled in time. A burst is a run whose
  consecutive intervals are at most 10 ms. Its first spike is the leader, and the
  leader's cluster is the burst's **reference cell**. Every later spike of the burst
  that is in the other cluster is a **follower**: a spike that should be with the
  reference cell.
- **Two new clusters at most**, one per reference cell: the followers of the first
  cluster's bursts found in the second, and the reverse. They are not merged into
  the reference cluster: each is parked as a cluster of its own, shown beside the two
  you selected, to look at and merge by hand. Each is one undo step. There is no
  confirmation box.
- **Left alone.** A burst in which the two clusters fall closer than 2 ms is two cells
  firing at once, not one cell bursting.
- **Two references and a mixed cluster.** Select three clusters: the action asks which
  one holds spikes of the other two. The mixed cluster is not a cell, so only its spikes
  *behind* a reference's burst leader are sorted: one new cluster for those behind the
  first reference's leaders, one for those behind the second's, each tested as above.
  A spike behind leaders of both references stays in the mixed cluster, as does
  everything that follows no reference. The references are not changed.
- **Children.** With the child clustering active (children selected), the same runs on
  the child layer, for two children or for two references and a mixed child; the
  children may belong to different parents. A split makes a **new child beside the
  child the spikes came from**, under that child's parent. The parent clustering is
  not changed, so nothing moves to the reference's parent: that step is yours.
- **Chance.** A direction is split only when its number of bursts stands above two
  counts, by a factor of two and at p < 0.001 each: the count with one train shifted
  against the other by 40 to 70 ms (unrelated firing), and the count with the train
  run backwards (cells that fire together within a few ms, as in a ripple, do so in
  no order; a cell's own burst has one). The status bar and the log give all three
  numbers for both directions, also when nothing is split.
- **Limits.** Timing only: no waveform is read, so it works in any feature space, and
  it will also split a pair whose order comes from something other than a burst (one
  cell driving the other). If each cluster holds the other's followers in equal
  numbers the order test sees no asymmetry and nothing is split. The 2 ms and 10 ms
  are fixed.

### Split Off Spurious Spikes — a cluster's core, and the rest

A cluster that is mostly one cell with other spikes in it: select it (or several, or
children in the child palette) and press **Ctrl+Space**, or choose **Split → Split Off
Spurious Spikes** (rebind `action.splitOffSpurious` in Preferences → Input). The spikes that are not
the core become one new cluster per selected cluster, shown beside it; in child scope,
a new child beside the one they came from. The core stays where it is. There is no
dialog and no confirmation box, and each split is one undo step.

- **How the core is found.** Every spike is centred on the point where its falling
  flank crosses half its trough depth, and looked at through a short window around
  that point on every channel. The spikes are then put in the order in which each
  next one changes the mean waveform of those already taken least. The cell's
  ordinary spikes come first.
- **Where it is cut.** While the spikes taken are of one kind, their kurtosis on each
  channel stays flat; when different ones start to come in it rises. The cut is the
  first place, past half the order, where that rise sets in on any channel.
- **Nothing split off** means no such place was found: the status bar says so.
- **It assumes** that one cell is more than half the cluster, and a downward spike.
  A cluster of two cells of similar size is not what it is for.
- **It errs on the side of splitting.** On a unit a curator had left whole it took
  45% of the spikes. What is split off is a cluster to look at, not noise: merge it
  back when it is the same cell.
- **It reads the waveforms of the space shown.** It was worked out on the standard
  space.
- **Knobs** (Preferences → Curation → Splitting). *Cut sensitivity* is the one to
  reach for: lower it when foreign spikes are left in, raise it when too much of the
  cell is taken. The window's four edges, the largest centring shift, the number of
  spikes ordered, the smallest cluster and the GPU switch are there too.
- **Speed.** The ordering is quadratic: about 3 s for 8,000 spikes and 7 s for 12,000
  on two cores. Up to 40,000 spikes are ordered; above that an evenly spaced 40,000
  are, and the others follow them. Over 400,000 spikes it refuses. In a CUDA build with
  an NVIDIA device, clusters of 4,000 spikes or more are ordered on the GPU and the
  status bar says so.

### Collision rows — a row that holds two units, shown as its own unit's spike

A spike row whose [`.eap`](../ndmanager-plugins/formats/eap.md) row names two or more template
classes holds two units' spikes: its own unit's (the cluster it is in) and a partner's.  With
*Preferences > Cluster view > Subtract the partner from collision rows* (on by default) every read
of such a row — the waveform view, the mean and median waveforms, the tools that work on waveforms
— gets the record with the partner's fitted template subtracted.

- The `.spk` is never changed.  It stays the raw cut of the recording, so realign, nudge and every
  other tool that re-cuts it work as before, and a row cannot be subtracted twice.
- What the `.eap` holds is which partner and about where.  The amplitude and the exact shift are
  fitted from the row and the templates as they now are, within 4 samples of the stored offset,
  jointly with the row's own template.  A partner is subtracted only when its fitted amplitude is
  plausible (0.3–2.5 of its template) and it removes at least 5 % of what the own template leaves.
- The templates are the mean of up to 300 rows of the unit that are *not* collisions, nearest in
  time to the row.  A unit with fewer than 30 such rows has no template; its partners are left in.
- Which cluster a class is: the cluster the `.tcl` says it was made from, while that cluster still
  holds at least 30 rows naming only that class; else the cluster holding most of those rows.  So a
  renumbering needs no bookkeeping, and a sort that leaves many of a unit's spikes in a mixed
  cluster (fiber-unmix's one-unit spikes) does not make that mixture the unit.
- A row whose own unit has no template is left as it is: fitted alone, a look-alike partner would
  take the row's own spike for itself.
- The corrected rows are rebuilt when a session opens, after a realign, a nudge or a rejected
  realign (the rows were re-cut and the templates moved), after a switch of feature space, after a
  save, when the preference changes, and -- once the application is idle -- after any edit of the
  clustering (a move, a merge, a split, an undo: which unit a row is may have changed).  At Save they are written as a sparse
  [`.spkc`](../ndmanager-plugins/formats/spkc.md) for other tools.

- The `.eap`'s offsets are relative to the spike times it was written for, and realign and nudge
  move times long before a Save.  So a rebuild starts each partner's search from the stored offset
  less the row's time change plus the partner unit's (the median time change of its rows that are
  not collisions), and Save writes the offsets the fit found -- relative to the saved times -- to
  the stage's own `.eap`.  A row can be moved any distance, and so can its partner's unit.
- **Realign.**  A realign measures a collision row on its corrected record: with the partner in
  the record, the lag that fits the unit's mean best is pulled toward the partner.  Only the
  measuring copy holds the corrected record; what is written for the row is a fresh cut of the
  recording at its new time (or its stored record if it does not move), and the corrected rows
  are rebuilt at the new times afterwards.  The optional PCA refinement, which compares raw cuts,
  leaves these rows at the lag their corrected record gave.  Not done while the corrected rows
  wait for a rebuild after an edit, and not with the preference off.
- **Save As.**  The `.eap` the session read goes with it: it is copied to the new name's own
  (`<base>.eap.<group>[.<stage>]`) unless the new name reads that same file, so a new stage name
  does not fall back to the base `.eap`.

- **Features.**  A corrected row's features are re-projected from its corrected record onto the
  space's PCA basis (the `.pca` of the open space), so the row sits in the cluster views where its
  own unit's spike belongs, not where the sum of two spikes fell.  Like the records, this is in
  memory only: the `.fet` on disk stays the projection of the raw `.spk`, and Save writes the raw
  projection for these rows.  With the preference off, or when a row stops being corrected, its
  file values come back exactly.  Only the PCA columns are replaced; a space without a `.pca`, or
  whose basis does not fit the records, keeps the raw features (reported as skipped).

Not done yet: a super-sampled space shows the raw records.

#### Mark Collisions — the collisions no sorter named

The corrected rows are made from what the `.eap` names. A sort that did not look for collisions
names none, and its clusters then hold the rows with a second unit's spike in them as they were
cut. **Clusters → Mark Collisions…** finds those rows in the selected clusters — or, in the child
view, in the children selected in the child palette — and names their partners. It has no key of
its own; bind one in **Preferences → Input**.

- **The search.** For each row its own cluster's template is taken out (best whole-sample shift
  within 2, gain 0.6–1.5). In what is left, the template of every other cluster with at least 200
  rows is tried at every lag that keeps its peak 6 samples inside the window, at 0.5–1.6 of its
  size. A row of a child is searched as a row of its parent cluster.
- **The control and the threshold.** The same search is made with the partners' templates
  *negated*. No spike is the negative of a unit's, so what that finds is what noise and misfit
  give. The threshold is the residual energy the control reaches in 1 % of the rows searched; a
  row is a candidate when its best partner removes more than that, and more than its own control.
- **The confirmation.** Each candidate is fitted jointly with the row's own template — the fit
  the corrected rows are made with — and kept when the partner comes out at 0.5–1.6 of its
  template and removes at least a fifth of what the own template leaves.
- **Templates** follow the session: a cluster's rows are cut into stretches and a template is
  the mean of up to 300 rows spread over one. A stretch is at most about 3000 rows and at most
  ten minutes of the cluster's life, but never fewer than 100 rows, and a cluster has at most 64.
  A row is searched with its own cluster's stretch and each partner's stretch at the same place
  in time.
- **Presence.** A cluster is tried as a partner of a row only where it has spikes of its own
  within five minutes of the row. A unit that is silent, or not sorted, around that time is not
  there to collide with; its template from another hour would only be fitted to noise.
- **Not tried as partners:** a cluster whose template is too like the row's own (cosine 0.85 or
  more at some shift within 3 samples, between any two of their templates along the session) — it
  would take the row's own spike for itself; the
  artefact and noise clusters; reserved chunk clusters. Rows that already name a partner are left
  out of the search.
- **The dialog** shows, for each selected cluster or child, the rows found and the rows the
  control puts over the threshold, and the partners by count. Nothing is changed before *Yes*.
- **What a mark is.** The row's `.eap` row gets its own cluster's class at offset 0 (when it names
  none) and the partner's class at the fitted offset. A cluster without a class is given one,
  named by every row of the cluster that names no class, with the cluster as its provenance in
  the `.tcl`. A session without an `.eap` gets one. The clustering, the `.spk` and the `.fet` are
  not changed, and a marked row is from then on a collision row like any other: shown, averaged
  and realigned with the partner subtracted, its offset following realign and nudge.
- **When it is written.** The marks are held in memory and applied to every read of the `.eap`;
  Save writes the `.eap` and the `.tcl`, with the preference on or off. Closing without saving
  drops them. Undo does not take a mark back.
- **A row that names another cluster's class.** A sorter names every spike's class, and nothing
  renames it when the spike is moved to another cluster. The `.clu` says whose spike a row is, so
  a marked row that names one class of another cluster has that class taken out and gets its own
  cluster's. Rows that are not marked keep what they name.
- **Left as it is:** a hit one of whose clusters can be given no class (fewer than 30 rows naming
  none).
- With the preference off the rows are marked and written but shown as they are.

Limits. A partner is found only where the partner's cluster has a template and spikes of its
own nearby in time: a unit the sort missed, one with fewer than 200 rows, or one whose spikes of
that part of the session are in another cluster, is not found. A partner whose peak lies in the outer 6
samples of the window, or outside it, is not searched for. Two spikes of look-alike clusters are
not told apart. The marks add nothing to the partner's cluster: its own spike in that row is
still missing from its train.

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
- **Save** (`Ctrl+S`) writes a *checkpoint*: every entry still in the
  ring is written with its status at that moment and `"final": false`,
  then a `CHECKPOINT` event, and the ring is kept, so a later undo still
  flips the entry.
- **Document close** (= save followed by quit) flushes every
  remaining entry with its current status and `"final": true`, then
  writes the `SESSION_CLOSE` sentinel.  An entry that leaves the ring by
  overflow is written the same way.

So an action saved and later undone appears twice: a checkpoint copy
(`good`, `final: false`) and the final copy (`bad`, `final: true`).  A
reader keeps the **last line per (`action_idx`, `phase`, `cluster`)**, or
filters on `final` when only closed sessions matter:

```python
df = pd.read_json("session.cur.stderiv.6.final", lines=True)
snaps = df[df.phase.notna()].drop_duplicates(["session_id", "action_idx", "phase", "cluster"], keep="last")
```

Consequences worth knowing:

- The log captures the user's *final* curatorial decisions, not their
  tentative explorations. An action explored and then reverted is
  written out as `"bad"`; one kept past the buffer boundary is
  `"good"`.
- If Klusters crashes, the actions since the last Save are lost (before
  2026-11-07 nothing was written until close, and a crash lost the whole
  session's log).
- Undoing past the ring's bottom (i.e. undoing 51+ actions in a row
  with default capacity 50) reverts the data state but cannot
  retroactively re-status the corresponding on-disk records — they
  were finalized at overflow time.
- Manual J / K / X annotation shortcuts no longer exist; the
  `"good"` / `"bad"` status is inferred from undo behavior.

### Top-level record schema

| Field | Description |
|---|---|
| `event` | `SESSION_OPEN`, `SESSION_CONTEXT`, `CHECKPOINT`, `SESSION_CLOSE`, `ACTION_DETAIL`, or empty for action snapshots |
| `trigger` | What started the action, on every line of it: `manual` (the curator asked for it), `after_merge` (the realign Klusters runs on a merged cluster), `after_edit` (the realign of the parents another edit changed), `align_all` (the batch). An automatic realign is not a decision of the curator's; filter on this before counting actions |
| `final` | `false` on a checkpoint copy (the action was still undoable), `true` once it left the ring |
| `layer` | `parent` or `child`: which labelling the snapshot's `cluster` id belongs to |
| `gesture_idx` | `action_idx` of the hierarchy command this primitive ran inside; -1 outside one |
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
| `waveform_snr`, `waveform_chan_spread`, `waveform_width_samp` | Waveform morphology (from the cached mean; `waveform_available` says whether it was) |
| `waveform_dom_chan`, `waveform_width_ms`, `waveform_chan_amp` | Dominant channel; trough-to-peak width in ms on it, polarity-aware (the cell-type axis: ~0.28 ms narrow vs ~0.6 ms pyramidal); the per-channel peak-to-trough profile |
| `isi_cv` | Inter-spike interval coefficient of variation |
| `refrac_ratio`, `refrac_expected` | Spike pairs 0.6–1.5 ms apart over the number expected from a Poisson train at the cluster's rate (~0 one cell, 1 − 1/k for k cells, ~2f(1−f) for a foreign share f); `null` when fewer than 3 pairs are expected. A violation percent cannot say this |
| `burst_index` | Autocorrelogram at 3–10 ms over the 40–50 ms baseline, per ms (complex-spike bursting ≫ 1) |
| `occupied_hz` | Rate over the time bins the cluster occupies (≥ 2% of its spikes), where `firing_hz` spans first to last spike |

`SESSION_OPEN` also records `method`, `stage`, `clu_file`, `n_spikes`,
`n_clusters`, `hierarchical` and `klusters_version`, so a log can be tied to
the pipeline output it curated; and `auto_realign_after_merge`,
`auto_renumber_after_merge` (what Klusters does by itself after an edit) and
`considered_min_dwell_s`.  `hierarchical` says whether a `.clc` is there to
load: the line is written before the child layer exists (until 2026-11-24 it
therefore said `false` for every hierarchical session).

`SESSION_CONTEXT` holds what is only known after the open -- once the child
layer is loaded: `hierarchical: true`, `n_parents`, `n_children`,
`child_ids_upgraded`.  Take the last value of a key per `session_id`.

`SESSION_CLOSE` gives `n_actions`, `duration_s` and `n_by_action` (begun
actions per kind, undone ones included; gestures as `GESTURE:<name>`).

### Action types

| `action` value | User-facing operation | Source clusters | Result clusters | Emits `ACTION_DETAIL`? |
|---|---|---|---|---|
| `GROUP` | Merge selected clusters (`G`) | N | 1 | Yes: one `pair` record per source pair |
| `CONSIDERED` | Two to four parents -- or children (`layer: child`) -- shown together and then left **without** a merge | — | — | Yes: one `pair` record per pair, no snapshots |
| `SPLIT` | Single new cluster from polygon, lasso, or DipSplit | 1 | 1 (new) + 1 (residual src) | DipSplit only |
| `SPLIT_N` | Multiple new clusters from polygon (one per source) | N | N (new) + N (residual srcs) | No |
| `RECLUSTER` | KiloKlustaKwik re-run on selected clusters (`Shift+R`) | M | K | Yes: mode, sources, the clusters returned and their sizes |
| `WATERSHED` | 2D density watershed (`Shift+W`) | M | K (basins) + 1 (residual) | Yes |
| `REALIGN` | Spike waveform realignment (`Shift+L`), also run by Klusters itself (see `trigger`) | unchanged | unchanged | Yes: outcome, spikes shifted, how far |
| `NUDGE` | Timestamp ±1 sample shift (`Page Up` / `Page Down`) | 1 | 1 (same id) | No |
| `DELETE_NOISE` | Move whole cluster → cluster 1 (`Delete`) | 1 | — | No |
| `DELETE_ARTEFACT` | Move whole cluster → cluster 0 (`Shift+Delete`) | 1 | — | No |
| `DELETE_REGION_NOISE` | Lasso → cluster 1 (`Ctrl+4` tool) | 1+ | — | No |
| `DELETE_REGION_ARTEFACT` | Lasso → cluster 0 (`Ctrl+3` tool) | 1+ | — | No |
| `MOVE_SPIKES` | Manual subset reassignment | 1 | 1 | No |
| `RENUMBER_PARTIAL` | Renumber selected to end (`T`) | renamed | renamed | No |
| `SORT_ALL` | Any renumbering sort: **Clusters → Sort Clusters**, `Shift+S` | — | — | Yes (the id map) |
| `GESTURE` | A hierarchy command (see Layers, gestures) | — | — | Yes (plan, outcome) |
| `HIERARCHY_EDIT` | `mergeChildren` (atom layer) | N atoms | 1 atom | Yes: the edit, and one `pair` record per pair of children merged |
| `UNDO`, `REDO` | Reverts/replays a previous action | — | — | No |

### Layers, gestures and the hierarchy commands

Every snapshot line carries `layer`: `parent` (the `.clu` units) or `child`
(the `.clc` atoms).  Child ids collide with parent numerals, so readers key on
(`layer`, `cluster`).  A child-scope edit -- a split, watershed, delete or
recluster of atoms, `mergeChildren` -- writes its snapshots from the atom
layer with `layer: child`.  Their `status` follows the undo history like a
parent action's: `bad` once undone, `good` again once redone (since
2026-11-24; before, a child edit was always `good`).  The undo or redo is
also logged as its own `UNDO` / `REDO` detail (`layer: child`, the `added` /
`modified` / `deleted` atom ids, and `reverts_action_idx`: the entry of the
edit it reverts or replays).

A hierarchy **command** is logged as a `GESTURE` entry -- one per command,
no snapshots, not undo-paired -- whose first `ACTION_DETAIL` holds the plan
and whose last (`gesture_end: true`) the outcome.  Every primitive the
command ran (`MOVE_SPIKES`, `GROUP`, a nested gesture) carries the command's
`action_idx` in `gesture_idx` (-1 outside any gesture), so the per-atom
entries fold back into the command:

| `gesture` | plan details | outcome |
|---|---|---|
| `promoteChildren` | `children`, `new_parent` | `produced` |
| `dropChildToNoise` | `child`, `parent`, `n_spikes` | |
| `dissolveParent` | `parent`, `children`; one nested `promoteChildren` per child | `produced` |
| `breakUpParents` | `parents`, `moves` (per child: `parent`, `spikes`, `best`, `r`, `target`, `keeps_parent_id`) | `landed` |
| `mergeOrphanParents` | `groups` (per template parent: `members` with `orphan`, `best`, `r`); one `GROUP` per group | `made` |
| `mergeOrphanChildren` | `parent`, `min_spikes`, `min_correlation`, `established`, `orphans` (with `r`), `matched`, `stray` | `merged` |
| `collapseToSelfChildren`, `flattenHierarchyToClu`, `compactAllClusterIds`, `promoteToHierarchy` | counts / id maps | |

Gestures nest (Break Up Parent runs `promoteChildren`): the inner gesture's
own lines carry the outer's index.

```python
g = df[(df.action == "GESTURE") & df.gesture.notna()].set_index("action_idx")
moves = df[(df.action == "MOVE_SPIKES") & (df.gesture_idx >= 0)]
moves.join(g[["gesture"]], on="gesture_idx")      # each atom move with the command it served
```

### What is not logged

Realign and nudge entries carry no waveform section unless the mean-waveform
cache was warm (`waveform_available`).  A full time sort (`applyTimeSort`)
permutes every row-indexed file and is not logged; it runs on saved files and
the document is reopened.

### `ACTION_DETAIL` records

These carry algorithm-internal state for actions that benefit from
per-decision metadata. Every `ACTION_DETAIL` record shares an
`action_idx` with the parent action's `before` / `after` records, so
joining is trivial in pandas.

**GROUP** logs what a merge decision rests on, which two single-cluster
snapshots cannot give back: one `{"detail": "pair"}` record per pair of
sources, measured before the merge, with `cross_refrac_ratio` /
`cross_refrac_expected` (pairs of one spike from each 0.6–1.5 ms apart over
chance: ~0 one cell, ~1 two cells; `null` without power), `template_cos`
(cosine of the cached mean waveforms, max over ±4 samples; `null` when a mean
was not cached) and `time_overlap` (of the two spans, over the shorter).
Since 2026-11-24 the record also holds:

| Field | |
|---|---|
| `template_lag`, `template_cos_lag0` | the lag (samples) at which `template_cos` was found, and the cosine without the search: a lag other than 0 says the two were misaligned |
| `profile_cos`, `amp_ratio` | cosine of the per-channel peak-to-peak vectors (where on the shank), and smaller / larger largest-channel amplitude: `template_cos` mixes these with shape |
| `coactive_overlap` | share of the one-minute bins of the cluster occupying fewer of them in which the other fires too. `time_overlap` compares first-to-last extents, so two cells that alternate read 1 there and near 0 here |
| `cross_refrac_observed` | the coincidence count the ratio is over, so pairs can be pooled and a withheld (`null`) ratio judged |
| `dwell_s` | seconds the selection had been on screen when the merge was made |
| `selection_matches` | the merged set was exactly the selection shown |
| `recommended` | the Recommended-merges panel had selected this set |

**CONSIDERED** is the other half of the same decision: a comparison the
curator made and did **not** act on.  When two to four parents (not the bins)
have been shown together for at least `considered_min_dwell_s` (2 s) and the
selection then moves on without a merge, one entry is logged with the same
`pair` record per pair (`dwell_s`, `n_shown`, `recommended`), no snapshots,
not undo-paired.  A merge is logged as the `GROUP` it is and not also as a
`CONSIDERED`; a merge that is undone keeps `status: bad`.  Not logged: a
selection that includes cluster 0 or 1, and one that changes while another
action is running.  A pair that was never selected
together leaves no record, so the absence of a `CONSIDERED` is not a
rejection.

```python
d = df[(df.event == "ACTION_DETAIL") & (df.detail == "pair")]
merged   = d[(d.action == "GROUP") & (d.status == "good")]
rejected = d[d.action == "CONSIDERED"]            # looked at, left apart
```

**The child layer** gets the same records, each with `layer: child` and the
child ids in `cluster_a` / `cluster_b`:

| Entry | `pair` records |
|---|---|
| `CONSIDERED` | children selected in the child palette, shown for the dwell and left: as for parents, plus `parent_a`, `parent_b`, `same_parent` |
| `HIERARCHY_EDIT` (`mergeChildren`) | the children merged, measured before the merge: every pair up to six children, above that each against the largest (`pairing`: `all` / `vs_largest`); `parent`, `n_merged`, `dwell_s`, `selection_matches` |
| `GESTURE` `promoteChildren`, `dropChildrenToBin` | each child taken out against the largest sibling that **stays** in its parent (`role: leaving_vs_largest_staying`, `parent`, `n_leaving`): what it was separated from. At most 12 per command; none when the whole parent leaves, and none for the promotions a Dissolve or Break Up runs inside itself |

Children merged or taken out are not also logged as `CONSIDERED`.
`template_cos` and the fields derived from the mean waveforms are `null`
for a child whose mean was not cached (it had not been drawn).

```python
kids = d[d.layer == "child"]
merged_kids  = kids[kids.action == "HIERARCHY_EDIT"]
split_off    = kids[kids.role == "leaving_vs_largest_staying"]
left_as_is   = kids[kids.action == "CONSIDERED"]
```

**REALIGN** logs `{"edit": "realign"}` with `outcome` (`applied`, or
`not_applied` for a realign that returned without changing anything -- a
refusal, a failed raw-source verification; such an entry has no `after`
line), `n_spikes`, `n_shifted`, `shifted_share`, `shift_abs_median`,
`shift_abs_max`, `shift_mean` (signed, over the shifted spikes) and
`not_recut`.

**RECLUSTER** logs `{"edit": "recluster"}` with `mode` (`plain` or
`against_references`), `layer`, `source_clusters`, `result_clusters`,
`result_sizes`, `largest_share` (of the returned spikes in the largest
piece) and `unchanged` (no more clusters came back than went in).

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

## While a job runs

A recluster (KlustaKwik, fiber-kit, a plugin, a drift job), a realignment (the
one you start, the automatic one after a merge or an edit, Align All), a nudge
and a save each own the clustering until they end.  While one runs, every
command that edits it is disabled: the tools, Group and Delete, the Hierarchy
menu, the split, strip and sort commands, Recluster and Realign, the
feature-space switch, Undo and Redo, Save.  Looking stays possible: selecting
clusters, the displays, the views, the trace navigation.

- **Edit → Cancel Running Job** (`Ctrl+.`, and the **Cancel job** button that
  appears in the status bar) is the one command that works meanwhile:
  - a recluster, drift or plugin program is stopped; its result is discarded
    and the clustering is as it was;
  - a realignment of several clusters (after an edit, Align All) stops after
    the cluster in progress; the clusters already realigned stay realigned;
  - the realignment of one cluster, a nudge and a save cannot be interrupted
    (a realign writes its rows in place): they end by themselves.
- The commands come back when the job ends, as they were before it.
- A command reached while one runs says so in the status bar ("Undo / Redo:
  wait for the realignment to finish.").
- Close, Open and Quit wait for a save, ask about a running recluster and are
  refused while a realignment runs: its worker holds the session.
- The automatic realignment after a merge therefore holds the next edit until
  it is done.  It can be turned off in **Preferences → Clustering**.

Before 2026-11-24 each kind of job disabled a list of its own, and the lists
had gaps: the Hierarchy menu and Save stayed enabled during a recluster, the
split and strip commands during a realignment, and Undo came back on with the
next change of the undo history.

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

**View → Immediate Update** (default): all views redraw immediately after every editing operation. Appropriate for small datasets.

**View → Delayed Update**: views are not updated until **View → Update Display** is triggered manually. Useful for large datasets where recomputing all views after every lasso operation is slow.

---

## Automatic reclustering (KiloKlustaKwik)

Klusters can launch KiloKlustaKwik on the current electrode group:

1. Select the clusters to recluster (or select all).
2. **Recluster → Recluster** (`Shift+R`).
3. KiloKlustaKwik runs as a subprocess. By default no tab opens for it: the
   status bar says when it starts and how many clusters it made. If the
   recluster fails, a **Recluster output** tab opens with KiloKlustaKwik's
   output. Uncheck **Suppress the Recluster output tab** in **Edit →
   Preferences → Curation → Reclustering** to open that tab with every
   recluster. With the tab suppressed, closing the last display stops a
   running recluster, as **File → Close** does.
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

The **Recluster** submenu also holds:

| Entry | Key | What it does |
|---|---|---|
| Recluster (median-waveform residual) | `Shift+M` | Reclusters the selected cluster(s) on the residuals of their pooled median waveform |
| Recluster (channel-variance features) | `Shift+C` | Reclusters using the features of the highest-variance channels |
| Recluster Against References… | — | One selected cluster is a mixture of the others: sorts them pooled and replaces only the mixed cluster by its pieces; see [below](#recluster-against-references--a-mixed-cluster-and-the-cells-it-is-a-mixture-of) |
| Recluster with fiber-kit (plan on the selection) | `Shift+Q` | Runs the fiber-kit plan of the session folder's `fiber-kit.yaml` on the selected clusters' (or atoms') spikes and replaces them with its clusters; see [below](#recluster-with-fiber-kit) |
| Split by KNN voting… | `Shift+K` | Splits the selected cluster by a K-nearest-neighbour vote against well-isolated reference clusters |
| Strip by Template… | `Shift+J` | Uses one selected cluster as a waveform template and pulls matching spikes out of the other selected clusters (current channel selection) |
| DipSplit Selected Cluster | `Shift+D` | See [DipSplit](#dipsplit--automatic-bimodal-split) |

### Recluster Against References — a mixed cluster and the cells it is a mixture of

A cluster that holds spikes of two known cells is hard to split by itself: the sorter sees
one blob. Sorted together with the two clean clusters, each cell's mode has its weight.

Select the mixed cluster **and** the clusters it is a mixture of, then **Recluster →
Recluster Against References…** (no key; bind `recluster.against` in Preferences → Input). A
dialog lists the selected clusters with their spike counts and asks which one is the mixed
one; the others are the references.

- **The pool.** All spikes of the mixed cluster and the spikes of each reference go to the
  sorter (KiloKlustaKwik with the Reclustering preferences, in the shown space's features --
  collision rows with their corrected ones). A reference larger than three times the mixed
  cluster (and than 1000 spikes) is cut to that many: its spikes inside the mixed cluster's
  time span, thinned evenly, or the nearest outside it when the span holds fewer.
- **What changes.** Only the mixed cluster. Each of the sorter's clusters that holds spikes of
  it becomes a new cluster of just those spikes; sorter clusters without any are ignored. The
  references keep every spike and their ids. Nothing is merged into a reference.
- **The summary**, in the Recluster output tab under the sorter's log and in the status bar:
  each new cluster with its spikes and the reference it was sorted with ("new cluster 31: 140
  spikes, sorted with 98 % of reference 12"), how many spikes of each reference were pooled,
  and a warning when the sorter split a reference itself (its largest part under 90 %), which
  says the pool was not separable. The ids are the ones after the automatic renumbering.
- **Undo.** As after any recluster: the automatic renumber is one step, the recluster the next.
- **Pool size.** The sorter needs enough spikes for its number of dimensions: on the test
  session a pool of 600 spikes in 13 dimensions came back as one cluster, 780 as the two
  units. A mixed cluster that returns in one piece is reported as such.

It runs KiloKlustaKwik whatever engine Recluster → Engine has checked. Compare
**Split by KNN voting…**, which assigns a cluster's spikes to reference clusters by vote
and chooses the references itself.

### Recluster with fiber-kit

**Recluster → Recluster with fiber-kit** (`Shift+Q`) runs a whole fiber-kit
pipeline on the selected clusters only -- in the child palette, on the selected
atoms -- and replaces them with the clusters it finds, as a recluster does (one
undo step, the result in the parent or the atom layer the selection came from).

Klusters writes the selection as a **sub-session** in the session folder, under
the same temporary base the recluster uses (`<base>--<ids>--<date>`): the
selected spikes' rows of every `.res`, `.spk` and `.fet` of the group (every
waveform and feature variant, in time order), a seed `.clu` with their current
ids, copies of the session's `.yaml`/`.xml`, and symlinks to the `.fil`, `.dat`
and the group's `.nst`/`.pca`/`.eap`/`.tcl`, so the stages that read the
recording (realign `--reextract`, deconvolution, the consensus envelope) read
the real one.  It then runs the fiber-kit command (settings key
`fiberKitReclusterCommand`; the default is
`fk run %electrodeGroupID --session %fileBaseName --final-clu %fileBaseName.clu.klusters.%electrodeGroupID`),
so, without the plugin, **the plan and knobs are the session folder's
`fiber-kit.yaml`**.  With fiber-kit's plugin installed (`fk install
klusters-plugin`) the recluster runs the `fiber-kit-recluster` plugin instead,
and its configuration is **`fiber-kit-klusters.yaml` beside the descriptor** in
Klusters' plugin directory: the tuned profile made session-agnostic for a
sub-session (every chunk, no resume, the open clustering's variant, no QC
report), passed at run time by the plugin's `config` parameter whatever
session is open -- edit that file to change what a recluster runs.  The
plugin's parameter defaults (`profile`, `config`, `plan`, `method`) are in
**Preferences → Plugins**; the Plugins menu runs it with a dialog.  Any other
sorter plugs in the same way (a `recluster` descriptor;
`src/klusters/docs/PLUGIN_API.md` §3b).  The final `.clu` is read back by spike order; every
temporary file, including what the stages wrote, is removed.  The engine reads
the spikes on disk, so the spike set must be the saved one (any clustering edit
is fine: the selection and the seed are taken from the live document).  In a
hierarchical session a plan that ends on a hierarchy (`.clu`/`.clc`/`.clp`)
installs its atoms too: a parent-scope recluster takes the `.clu` as the new
fibers and the `.clc` as their atoms (renumbered above the atom layer's
highest id); an atom-scope recluster takes the `.clu`'s units as the new atoms.
The sub-session's realigned spike times are not written back -- the new
clusters are queued for Klusters' own post-recluster realign, as any recluster's
are.  See fiber-kit's `docs/pipeline.md`, "Reclustering a selection".

**Preferences → Curation → Reclustering** is built around the engine: the
choice at the top (KlustaKwik, fiber-kit, or any installed `recluster` plugin
-- the same choice as *Recluster → Engine* and the wheel's Plugin
ring) with a summary of what the chosen engine runs and *Parameters…*, which
jumps to its page under *Preferences → Plugins*; below it the KlustaKwik
engine's own settings (executable path, arguments, UseFeatures and the
feature-selection options), marked inactive when another engine is active
(`Shift+M`/`Shift+C` still use them); and, when fiber-kit's descriptor is not
installed, its fallback command. The program is
`KiloKlustaKwik_cpu` (KiloKlustaKwik's CPU build, installed by every build;
set `KiloKlustaKwik` for a GPU build) by default, with the arguments
`%fileBaseName %electrodeGroupID -MinClusters 2 -MaxClusters 12 -UseFeatures %features`.
Klusters adds `-fSaveModel 0` to a KlustaKwik recluster unless the Arguments name the flag,
and removes a model file left under the temporary name: a recluster leaves no
`*.model.*` file behind.
Settings saved by earlier versions name `KlustaKwik`, which neurosuite-3 does
not install; when no `KlustaKwik` is on the PATH, Klusters uses
`KiloKlustaKwik_cpu` instead.

**Time chunks.** KiloKlustaKwik sorts in time chunks and merges across them,
and `-MaxClusters` applies to each chunk. *Time chunks* sets how a recluster
is cut:

| Setting | Passed to the sorter | What runs |
|---|---|---|
| As the sorter decides (default) | nothing | 7-minute chunks: a cluster spanning a long session is sorted as many short chunks |
| One chunk: the whole selection | `-ChunkMinutes 1000000` | the chunked sorter on the selection as a whole |
| Chunks of *N* min | `-ChunkMinutes N` | chunks of that length |
| Off: classic two-phase pass | `-ChunkMinutes 0` | the original KlustaKwik-style pass, no chunks |

The flag is added only when the executable is `KiloKlustaKwik*` (the classic
KlustaKwik has no such option) and the Arguments name neither `-ChunkMinutes`
nor `-ChunkFile`; a flag written there wins.

The feature file's last column, the spike time, is always the sorter's time
axis. The toolbar's **time** box and the `Shift+M` / `Shift+C` modes may leave
its bit off in `-UseFeatures`; KiloKlustaKwik selects it regardless and
clusters on the feature columns chosen.

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

**Spikes → Realign Spikes…** (`Shift+L`) realigns the first selected
cluster (ID above 1) of the active display at once, with the settings in
**Edit → Preferences → Curation → Realignment**; there is no dialog. **Spikes →
Align All Clusters** (`Shift+P`) runs the same alignment, with the same
settings (including the Parameters toolbar's top-channels box and the
post-alignment mode), over every cluster except 0 and 1. Before 2026-10-21 it
always added the PCA refine pass whatever the mode, so its results differed
from Realign Spikes'; choose the PCA refine as the post-alignment mode to get
that behaviour.

### How it works

For each spike in the cluster:

1. The cluster mean waveform is computed.
2. The spike's waveform is cross-correlated against the mean template at each lag within ±*Max shift* samples — over the whole window by default, or under a **correlation window** (**Edit → Preferences → Curation → Realignment → Correlation window**): a Gaussian or Hann bell of the given half-width centred on the peak sample, or the template's own energy profile. Under a window the score is the normalised correlation of the template with the part of the spike the window admits, normalised by that part's energy, so a second spike in the tail or the lead-in of the window — a collision — neither feeds the score nor its normalisation and cannot pull the lag toward itself. *None* is the classic kernel (every GPU back end); a window runs on the CPU kernel. Argument form: `--kernel none|gauss|hann|template`, `--kernel-width N` (samples each side of the peak; 0 = half the peak position, like the automatic max shift; in a super-sampled space the width applies in ticks). The post-alignment pass runs under the same window. The **RMS circular recenter** takes the centroid of the windowed energy; a Gaussian or Hann bell is followed from the peak sample onto the energy lobe it finds there (measured once under a bell that sits on the target, the centroid is pulled toward the bell's centre and the shift comes out short), the template-energy window is used as it stands. The log gives the whole-window centroid beside it. The **PCA projection refine** projects through the basis under the window, Σ w·ev·(x − mean). It is not normalised by the windowed energy as the correlation is, so the window narrows what votes on a spike's position but does not make the refine a collision guard. The per-spike centroid mode is template-free and does not use the window.
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
- A realignment is an **undo step**: `Ctrl+Z` writes back every row it rewrote —
  the spike time, the waveform and the features, exactly as they were — and
  `Ctrl+Y` writes the realigned rows again. The other feature spaces follow, as
  they do after the realign.
  - **Align All** is one step. The automatic realign after a merge belongs to
    the merge's step: undoing the merge also puts the spikes back.
  - It works across a **Save**: undo after saving and save again to have the
    old rows on disk.
  - A realign done in another feature space is undone **in that space**: with a
    different one shown, Undo shows the realign's space for the step and comes
    back to the one you were looking at, which follows the moved spikes.
  - The rows are kept in memory: for each spike of the cluster the old and the
    new waveform. Realign steps count against the undo depth. What they may
    take together is **Preferences → General → Session → Undo → Memory for the
    undo of realigns** (4 GB unless changed). Beyond it the oldest realign
    steps are dropped (the edits of the same step stay undoable), and a single
    realign or Align All larger than it cannot be undone: the status bar says
    so. With 0, no realign is kept.
- A **nudge** is **not undoable**, and it ends the undo of the realigns before
  it: their kept rows no longer describe the files. `Ctrl+Z` then goes to the
  edit before. The way back from a nudge is the opposite nudge, or **File →
  Close** without saving.
- On crash, the autosave-recovery path, the commit journal (an interrupted
  save is completed on the next open) and the `.bak` files written by
  upstream scripts (reextract, subcluster) give independent recovery
  options.

A realignment is kept as soon as it finishes: the views switch to the
Overview Display with the realigned cluster selected, and the change is
written by the next **Save**. To discard it, use **Undo**, or close without
saving.

Realignment parameters are set in **Edit → Preferences → Curation →
Realignment**: *Threshold* (minimum score, default 0.10: a spike whose best
correlation is lower is not moved in that pass — at a high threshold the
low-scoring spikes of a cluster stay behind while the rest move), *Iterations*,
*Max shift* (0 = automatic), the *Correlation window* (None / Gaussian / Hann /
Template energy, with its half-width; the collision guard described above) and
an optional post-alignment pass (PCA projection refine, RMS circular recenter,
per-spike centroid). The same page can realign a cluster automatically after
each merge.

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
every few minutes (**Edit → Preferences → General → Session**): `.<clu file>.autosave`
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

**Edit → Preferences** (`P`) opens a tree of three groups; each setting is
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
| | Reclustering | KiloKlustaKwik executable and arguments, feature selection, the Recluster output tab |

The dialog can be worked from the keyboard, in two levels:

| Where | Key | What it does |
|---|---|---|
| the page list | `Up` / `Down` | another page |
| | `Enter` | into the page shown; on a group, opens it on its first page |
| | `Tab` | the list, then the dialog's buttons, then the list again |
| | `Esc` | closes the dialog (Cancel) |
| a page | `Tab` / `Shift+Tab` | the next / previous setting, round and round within the page |
| | `Space` | toggles a check box |
| | `Enter` | toggles a check box, presses a button, opens a list of choices, commits a number — it does not press OK |
| | `Esc` | back to the page list |
| anywhere | `Ctrl+Enter` | OK |

A key editor on the Input page records `Enter` and `Esc` as keys; leave it with
`Tab` first.

## Parameters toolbar reference

The **Parameters** toolbar (View → Toolbars and Panels → Show Parameters) shows context-sensitive controls depending on the active view:

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
