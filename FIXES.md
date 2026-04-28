# Klusters fix-bundle — summary

This drop addresses six issues raised in the last round.

## 1. Segfault on undo after dipsplit

**Root cause:** `KlustersDoc::commitClusterCreation` registered the new
cluster's colour BEFORE calling `prepareUndo`.  `prepareUndo` deep-copies
the current `clusterColorList` for the undo snapshot, so both the
snapshot AND the new "current" list contain the new cluster.  After the
user undoes the action, the colour list rolls back to a state that
*still* has the new cluster — but `Data::undo` rolls the spike table
back to a state where that cluster has no spikes.  The palette renders
an icon for a cluster that doesn't exist in `Data`; clicking it
dereferences nothing and crashes.

**Fix:** invert the order: call `prepareUndo` FIRST (so the snapshot is
captured before mutation), then `clusterColorList->append(newId, ...)`.
The matching `groupClusters` path already does it this way; this just
brings the createNewCluster / dipSplit path into line.

Affected code path: any "create one new cluster" operation —
**polygon-create new cluster, DipSplit**.  Both routed through
`commitClusterCreation`.

## 2. Group ('g') doesn't return focus to palette

**Fix:** `slotGroupClusters` now ends with
`clusterPalette->setFocusToList()` instead of
`view->focusClusterView()`.  Same pattern as nudge / recluster /
realign.

## 3. "T" — move selected cluster(s) to end of palette

New keyboard shortcut, palette-context only (so the existing
`Time Frame` waveform mode toggle on T still works when the waveform
view has focus).

**Implementation:**

- `ItemColors::moveItemToEnd(int itemId)` — `takeAt` + `append` on the
  internal `itemList`.  Preserves colour, label, isChanged.
- `KlustersDoc::moveClusterToEnd(int clusterId)` — calls
  `prepareUndo()` (no-arg → empty data lists, snapshots colour list)
  before invoking `moveItemToEnd`.  Refreshes the palette and preserves
  the active view's selection.  Logged to the curation log as
  `REORDER_PALETTE`.
- `KlustersApp::slotMoveSelectedClustersToEnd` — fans out over the
  palette's currently-selected clusters; reselects them and returns
  focus to the palette afterwards.
- T key intercepted in `eventFilter` at `ShortcutOverride` (so the
  global `Time Frame` QAction never fires) when the palette has focus.

Undoable via Ctrl+Z.

## 4. Trace view only updates when error matrix is open

**Root cause:** `KlustersDoc::nudgeClusterTimestamps` was missing the
`updateTraceView` call that every other cluster-mutating operation makes.
The trace would only refresh when something else happened to redraw — most
visibly when the error matrix's update path forced view repaints — making
nudge appear to "only work with the errormatrix open".

**Fix:** added the canonical `view->updateTraceView(electrodeGroupID,
clusterColorList, isActive)` call at the end of nudge, mirroring the
identical pattern used in `groupClusters` / `commitClusterCreation` /
`removeSpikesFromClusters`.

## 5. Help menu (`H`) refreshed

Now organized into 7 sections:

- Cluster palette (Arrow / S / T / PgUp/PgDn / H)
- Cluster operations (C, S, D, G, R, Shift+R, Shift+L, N/Del, A, Shift+Del, Z, W, U, F, Enter, Shift+P, Shift+F)
- File (Ctrl+O / Ctrl+S / Ctrl+Shift+S / Ctrl+I / Ctrl+P / Ctrl+Q)
- Edit / Selection (Ctrl+Z / Ctrl+Y / Ctrl+A / Ctrl+Shift+A)
- Waveform display (T (waveform-focus) / O / M / Ctrl+I/D / Ctrl+Shift+I/D / L / Shift+M/A/U)
- Correlograms (Shift+I/D / Ctrl+Shift+F/B)
- Curation log annotation (J / K / X)

Wider dialog with a `QScrollArea` so longer-than-screen content is reachable.

## 6. Spike-waveform corruption returns on a subset of spikes

**Most likely cause:** `process_alignspikes` was run with the unpatched
binary (or a stale binary somewhere on `$PATH` overrode the freshly-built
one), so `.res` was not updated for spikes that got non-zero shifts.

**Diagnostic added:** before nudge mutates anything, it now reads the
`.spk` for the first / middle / last spike of the cluster, computes the
sum-`|.spk|` peak position per spike, and compares to `peakSamp0`.  If
the median absolute offset across the three probes is more than 1
sample, the `.res ↔ .spk peak` invariant is broken.  Nudge **aborts**
with a status-bar message and a `qWarning()` that names the likely
cause:

```
[nudge] cluster N: pre-nudge invariant check FAILED — .spk peak is
M samples off peakSamp0=K (per-probe offsets: o1 o2 o3). The .res/.spk
peak invariant is broken; nudge will corrupt the .spk for this cluster.
Most likely cause: ndm_alignspikes was run with an old binary that does
not update .res. Rebuild ndmanager-plugins, then re-run:
ndm_extractspikes_stderiv → ndm_alignspikes → ndm_pca_stderiv.
```

**To verify which alignspikes binary actually ran:** check for
`session.res.G.prealign` archive files in the session directory.  The
patched alignspikes creates these on first run; the unpatched one does
not.  If they're absent, the patch never ran on this session — re-run
the pipeline with the rebuilt binary.

## Files in this drop

```
src/klusters/src/klusters.h           — declare slotMoveSelectedClustersToEnd
src/klusters/src/klusters.cpp         — group focus, T key wiring, slot, help
src/klusters/src/klustersdoc.h        — declare moveClusterToEnd
src/klusters/src/klustersdoc.cpp      — undo-order fix, moveClusterToEnd,
                                         trace-view update in nudge,
                                         pre-nudge invariant self-check
src/klusters/src/curationlogger.h     — REORDER_PALETTE action type
src/klusters/src/curationlogger.cpp   — REORDER_PALETTE → "REORDER_PALETTE"
src/libklustersshared/src/klustersshared/itemcolors.h  — moveItemToEnd decl
src/libklustersshared/src/klustersshared/itemcolors.cpp — moveItemToEnd impl
```

Apply the cumulative tarball over the working tree, rebuild, and the six
issues above are addressed.  The alignspikes `.res` fix from the prior
drop is a separate tarball; both are needed for end-to-end correctness.
