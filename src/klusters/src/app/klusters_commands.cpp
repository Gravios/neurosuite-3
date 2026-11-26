/***************************************************************************
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/
// The commands of the menus, each described once.
//
// A row gives a command its id, the category it has in the key bindings (empty: it is
// not listed there), its menu text, the slot it runs, and what else applies: its key,
// its tips, whether it is checkable and how it starts, and whether it EDITS -- changes
// the clustering, its files or the cluster ids, or starts a job that does.
//
//   - createMenus() (klusters.cpp) places a command: `mUndo = addCommand(editMenu, "edit.undo");`
//     The order of the menus, their separators and sub-menus are there, not here.
//   - registerInputBindings() mirrors every row that has a category into the binding registry.
//   - The edit lock (refreshEditLock) holds off every row marked editing() while a job runs.
//
// A new command is one row here and one line in createMenus().  A row without a slot
// (no ON(...)) is wired in createMenus(), where its lambda or its action group is.
// test/selftest/test_command_table.py compares the result with commands.golden.json.

#include "klusters.h"

#include <QMenu>
#include "commandspec.h"
#include "configuration.h"

// The slot of a command: connect(action, &QAction::<signal>, this, &KlustersApp::<slot>).
#define ON(signal, slot) [this](QAction* a) { connect(a, &QAction::signal, this, &KlustersApp::slot); }

QList<CommandSpec> KlustersApp::commandTable()
{
    QList<CommandSpec> t;

    // ── File ──────────────────────────────────────────────────────────────────────────────
    t << CommandSpec("file.open", "File", tr("&Open…"), ON(triggered, slotFileOpen))
            .shortcut(QKeySequence::Open);
    t << CommandSpec("file.import", "File", tr("&Import File"), ON(triggered, slotFileImport))
            .shortcut(Qt::CTRL | Qt::Key_I).editing();
    t << CommandSpec("file.save", "File", tr("Save…"), ON(triggered, slotFileSave))
            .shortcut(QKeySequence::Save).editing();
    t << CommandSpec("file.saveAs", "File", tr("&Save As…"), ON(triggered, slotFileSaveAs)).editing();
    t << CommandSpec("file.renumberAndSave", "File", tr("Re&number and Save"), ON(triggered, slotFileRenumberAndSave))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_S).editing();
    t << CommandSpec("file.print", "File", tr("Print"), ON(triggered, slotFilePrint))
            .shortcut(QKeySequence::Print);
    t << CommandSpec("file.close", "File", tr("Close"), ON(triggered, slotFileClose));
    t << CommandSpec("file.quit", "File", tr("Quit"), ON(triggered, slotFileQuit)).shortcut(QKeySequence::Quit);

    // ── Edit ──────────────────────────────────────────────────────────────────────────────
    t << CommandSpec("edit.undo", "Edit", tr("Undo"), ON(triggered, slotUndo))
            .shortcut(QKeySequence::Undo).editing();
    // explicit Ctrl+Y so the
    // Linux default Ctrl+Shift+Z
    // is free for the child layer
    t << CommandSpec("edit.redo", "Edit", tr("Redo"), ON(triggered, slotRedo))
            .shortcut(Qt::CTRL | Qt::Key_Y).editing();
    // The one command that works WHILE a job runs (the edit lock leaves it alone).
    t << CommandSpec("edit.cancelJob", "Edit", tr("&Cancel Running Job"), ON(triggered, cancelRunningJob))
            .shortcut(Qt::CTRL | Qt::Key_Period)
            .statusTip(tr("Stop the running recluster, drift or plugin job (its result is discarded), "
                     "or a realignment of several clusters after the cluster in progress")).disabled();
    t << CommandSpec("edit.selectAll", "Edit", tr("Select &All"), ON(triggered, slotSelectAll))
            .shortcut(QKeySequence::SelectAll);
    t << CommandSpec("edit.selectAllExcept", "Edit", tr("Select All Except 0 and 1"), ON(triggered, slotSelectAllWO01))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_A);

    // ── View ──────────────────────────────────────────────────────────────────────────────
    t << CommandSpec("view.showReservedClusters", "View", tr("Show &Reserved Chunk Clusters"), ON(toggled, slotShowReservedClusters))
            .toolTip(tr("List the reserved chunk clusters (R0 … R99: the chunks a fiber-kit\n"
                     "chunk run left unsorted) in the cluster palette.  Unchecked, they\n"
                     "leave the palette and the displays, and Select All skips them.")).checkable().checked();
    // a hierarchical session enables it
    t << CommandSpec("view.showChildPalette", "View", tr("Show &Child Cluster Palette"), ON(toggled, slotShowChildPalette))
            .toolTip(tr("Show the palette of the selected parent's child clusters (.clc).\n"
                     "Hidden, the selection, the displays and the matrices work on\n"
                     "the parent clusters.")).checkable().checked().disabled();
    t << CommandSpec("view.followSelectionInMatrices", "View", tr("&Follow Selection in Matrices"), ON(toggled, slotFollowSelectionInMatrices))
            .toolTip(tr("Centre the curation matrices on the cluster selected in the parent or\n"
                     "the child cluster palette: the view pans so its diagonal cell is in the\n"
                     "middle (the zoom is kept).  A matrix panned completely off screen is\n"
                     "brought back to its upper-left corner either way.")).checkable().checked(configuration().getMatrixFollowSelection());
    // Templates mode on
    t << CommandSpec("view.showTemplatePalette", "View", tr("Show &Template Palette"), ON(toggled, slotShowTemplatePalette))
            .toolTip(tr("Show the template-class palette (Templates mode).")).checkable().checked().enabledIf(configuration().getTemplatesMode());

    // ── Clusters, Split, Spikes, View (were Actions) ──────────────────────────────────
    t << CommandSpec("action.deleteArtifact", "Clusters", tr("Delete &Artifact Cluster(s)"))
            .shortcut(Qt::SHIFT | Qt::Key_Delete).editing();
    t << CommandSpec("action.deleteNoisy", "Clusters", tr("Delete &Noisy Cluster(s)"))
            .shortcut(Qt::Key_Delete).editing();
    t << CommandSpec("action.groupClusters", "Clusters", tr("&Group Clusters")).shortcut(Qt::Key_G).editing();
    t << CommandSpec("action.chunkMode", "View", tr("Time-&Chunk Mode"), ON(toggled, slotChunkModeToggled))
            .checkable().editing();
    t << CommandSpec("action.prevChunk", "View", tr("Previous Chunk"), ON(triggered, slotPrevChunk))
            .shortcut(Qt::Key_PageUp).disabled();
    t << CommandSpec("action.nextChunk", "View", tr("Next Chunk"), ON(triggered, slotNextChunk))
            .shortcut(Qt::Key_PageDown).disabled();
    // Patch 0069 — Auto-Merge action.
    // Uses the same template-cross-correlation mechanism as KKE; settings
    // come from the Auto-Merge preferences tab (patch 0068).  Shortcut Shift+G
    // by analogy with G for Group.
    t << CommandSpec("action.autoMerge", "Clusters", tr("&Auto-Merge Similar Clusters…"), ON(triggered, slotAutoMerge))
            .shortcut(Qt::SHIFT | Qt::Key_G).editing();
    // Purge: move every cluster smaller than a user-entered spike count into the
    // noise cluster (1).  Bulk analogue of "Delete Noisy Cluster(s)"; asks for N
    // then a Yes/No confirmation (default Yes) before moving.
    // Rows of the selected clusters that hold a second cluster's spike, found on the waveforms
    // and named in the stage's membership: the corrected collision rows for a sort that named
    // none.  Scan -> confirm -> mark; unbound, like the residual strip: bind it in the input
    // registry if it earns a chord.
    t << CommandSpec("action.markCollisions", "Clusters", tr("Mark &Collisions…"), ON(triggered, slotMarkCollisions))
            .toolTip(tr("Search the selected clusters (or the children selected in the child view) for rows\n"
                     "that hold a second cluster's spike, and name the partner in the membership (.eap):\n"
                     "the rows are then shown with the partner's template subtracted.  The clustering and\n"
                     "the .spk are not changed; the marks are written at Save.")).editing();
    t << CommandSpec("action.purgeSmallClusters", "Clusters", tr("&Purge Small Clusters…"), ON(triggered, slotPurgeSmallClusters))
            .editing();
    // Bulk artifact removal: move every spike more than 5 sigma from its cluster's
    // per-dimension feature mean (in any one dimension) into the artifact cluster.
    // Non-mutating scan first, then a Yes/No confirmation showing the exact spike
    // count (default No, since it is destructive); undoable.
    t << CommandSpec("action.stripOutliers", "Split", tr("Strip Feature &Outliers (5\u03c3)\u2026"), ON(triggered, slotStripFeatureOutliers))
            .editing();
    // Waveform counterpart of the feature strip, and the interactive twin of
    // fiber-kit's consolidate mode=shed: strip each SELECTED cluster's own
    // worst-fitting spikes (residual to its median waveform, robust MAD tail)
    // into ONE new cluster (not the artifact bin -- the strip is a hypothesis to
    // review, so the shed spikes stay inspectable rather than being discarded).
    // Selection-scoped (not bulk) because it is shortcut-driven -- Shift+X acts
    // on the cluster under inspection.  Same non-mutating scan -> confirm ->
    // undoable split as the feature strip's scan -> confirm -> move.
    t << CommandSpec("action.stripWaveformOutliers", "Split", tr("Strip &Waveform Outliers\u2026"), ON(triggered, slotStripWaveformOutliers))
            .shortcut(Qt::SHIFT | Qt::Key_X)
            .toolTip(tr("Split each selected cluster's waveform outliers (residual to its own "
                     "median waveform) off into a new cluster.  Undoable.")).editing();
    // The same strip with the per-spike GAIN divided out and the residual power
    // split by how much of the template each channel carries: an on-channel
    // term (wrong shape at the cell's own site) and an off-channel one (signal
    // where this cell has none), each cut at its own robust tail and unioned.
    // A sibling of Shift+X rather than a replacement -- that gesture is
    // calibrated to fiber-kit's shed knee, so the two stay comparable on the
    // same cluster.  Dialog-driven (five knobs, unit-dependent) and
    // deliberately unbound: bind it in the input registry if it earns a chord.
    t << CommandSpec("action.stripResidualOutliers", "Split", tr("Strip &Residual Outliers\u2026"), ON(triggered, slotStripResidualOutliers))
            .toolTip(tr("Split each selected cluster's gain-free residual outliers -- "
                     "on-channel (shape) and off-channel (position) terms, separately "
                     "thresholded -- off into new clusters.  Undoable.")).editing();
    // Two similarly shaped cells: the later, smaller spikes of one cell's bursts
    // are easily clustered with the other.  The waveform cannot tell (that is the
    // premise); the timing can.  Unbound: bind it in the input registry.
    t << CommandSpec("action.splitBurstFollowers", "Split", tr("Split &Burst Followers"), ON(triggered, slotSplitBurstFollowers))
            .toolTip(tr("With two similarly shaped clusters selected: every burst is given to the "
                     "cluster of its first spike, and the later spikes of the burst that are in "
                     "the other cluster are split off into a new cluster -- one per reference "
                     "cell, only when the count stands above chance (unrelated firing, and "
                     "cells firing together in no order).  With three selected: two references "
                     "and a cluster that holds spikes of both; its spikes behind each "
                     "reference's burst leaders are split off, one new cluster per reference.  "
                     "With children selected it works on the child layer: the new clusters are "
                     "new children beside the ones the spikes come from.  Undoable.")).editing();
    // One cluster: the spikes in the order that changes the cumulative mean
    // waveform least, cut where a channel's cumulative kurtosis starts to rise.
    // Its knobs are in Preferences > Splitting.
    t << CommandSpec("action.splitOffSpurious", "Split", tr("Split Off Sp&urious Spikes"), ON(triggered, slotSplitOffSpurious))
            .shortcut(Qt::CTRL | Qt::Key_Space)
            .toolTip(tr("For each selected cluster: its spikes, centred on the first half crossing "
                     "of their falling flank and seen through a tapered window around it on every "
                     "channel, are put in the order in which each next one changes the cumulative "
                     "mean waveform least.  The order is cut where a channel's cumulative kurtosis "
                     "starts to rise, and the spikes past the cut are split off into a new cluster; "
                     "the core stays.  It assumes one unit is more than half the cluster, and it "
                     "errs on the side of splitting.  Reads the waveforms of the space shown.  With "
                     "children selected the new cluster is a new child beside the one the spikes "
                     "come from.  Undoable.")).editing();
    // Pin the oblique (template-axis) projection's basis.  The oblique view
    // (Shift+O) normally uses the SELECTED clusters as its axes; this fixes a
    // chosen pair (or more) as the axes so a DIFFERENT cluster can be examined
    // against them -- select the third and press Shift+O.  Set via a dialog, as
    // it is a configuration rather than a per-view gesture.
    t << CommandSpec("action.setObliqueBasis", "Split", tr("Set Oblique &Basis…"), ON(triggered, slotSetObliqueBasis))
            .toolTip(tr("Pin cluster(s) as the fixed axes of the oblique (template-axis) view, "
                     "so a different cluster can be projected onto them with Shift+O."));
    // Mark/unmark the selected cluster(s) as template units.  Marked units are the
    // template library for this clustering; a save persists the set and (re)writes
    // their .wtf via the parent-template plugin.  A gesture on the selection, like
    // the move-to-noise/artifact actions, rather than a configuration dialog.
    t << CommandSpec("action.markAsTemplate", "Clusters", tr("Mark/Unmark as &Template"), ON(triggered, slotToggleTemplate))
            .toolTip(tr("Toggle the selected cluster(s) as template units.  On save their linked "
                     "template waveforms (.wtf) are written in every available method.")).editing();
    // Run parent-template on demand for the selected cluster(s) only — the
    // per-cluster "refine this unit now" step of the curation workflow, distinct
    // from the on-save run over the whole marked set.
    t << CommandSpec("action.generateTemplate", "Clusters", tr("&Generate Template for Selected Cluster(s)"), ON(triggered, slotGenerateTemplateForSelection))
            .toolTip(tr("Run fiber-template now for the selected cluster(s) only, writing their "
                     "linked template waveforms (.wti/.wtf).  Save first — the tool reads the "
                     "clustering files on disk.")).editing();
    t << CommandSpec("action.updateDisplay", "View", tr("&Update Display"));
    t << CommandSpec("action.renumberClusters", "Clusters", tr("&Renumber Clusters"))
            .shortcut(Qt::Key_R).editing();
    t << CommandSpec("action.updateErrorMatrix", "View", tr("&Update Error Matrix"), ON(triggered, slotUpdateErrorMatrix))
            .shortcut(Qt::Key_U);

    // ── Sort ──────────────────────────────────────────────────────────────────────────────
    // Disabled until at least one of the matrices is open.  Re-enabled
    // via slotStateChanged("errorMatrixViewState") and the equivalent
    // template-matrix wiring at the dock-creation site.
    t << CommandSpec("sort.bySimilarity", "Sort", tr("Re&order Clusters by Similarity"), ON(triggered, slotReorderClustersBySimilarity))
            .shortcut(Qt::SHIFT | Qt::Key_S)
            .toolTip(tr("Renumber clusters so that similar clusters get adjacent IDs, using\n"
                     "single-linkage agglomerative clustering on the active similarity matrix.\n"
                     "When both error and template matrices exist, uses whichever was most\n"
                     "recently created or clicked.  Clusters 0 (artifact) and 1 (noise) are\n"
                     "preserved at the start.  Undoable with Ctrl+Z.")).disabled().editing();
    // Sort by spike count: renumber clusters so IDs run by descending spike
    // count (largest cluster becomes 2).  Needs no matrix, just the cluster
    // sizes, so it follows the document/cluster-op enabled state (mirrors
    // mAutoMerge) rather than the matrix-availability gate above.  Undoable.
    t << CommandSpec("sort.bySpikeCount", "Sort", tr("Sort Clusters by Spike &Count"), ON(triggered, slotSortClustersBySpikeCount))
            .toolTip(tr("Renumber clusters so their IDs run from largest to smallest by spike\n"
                     "count (the biggest cluster becomes 2).  Clusters 0 (artifact) and 1\n"
                     "(noise) are preserved at the start.  Undoable with Ctrl+Z.")).editing();
    // Sort by starting-edge time: renumber clusters so IDs run by ascending time
    // of each cluster's earliest spike (the cluster that fires first becomes 2).
    // Needs no matrix, just spike timestamps, so it follows the same
    // document/cluster-op enabled state as the spike-count sort.  Undoable.
    t << CommandSpec("sort.byTime", "Sort", tr("Sort Clusters by &Time"), ON(triggered, slotSortClustersByTime))
            .toolTip(tr("Renumber clusters so their IDs run by ascending starting time — the\n"
                     "cluster whose earliest spike comes first becomes 2.  Clusters 0\n"
                     "(artifact) and 1 (noise) are preserved at the start.  Undoable with Ctrl+Z.")).editing();
    // Sort by contamination: renumber clusters by descending refractory ISI-
    // violation fraction (2 ms window), so the most-contaminated cluster becomes
    // 2 and lands at the top of the palette for review.  Needs no matrix, just
    // spike timestamps, so it follows the same enabled state.  Undoable.
    t << CommandSpec("sort.byContamination", "Sort", tr("Sort Clusters by C&ontamination"), ON(triggered, slotSortClustersByContamination))
            .toolTip(tr("Renumber clusters by descending refractory contamination — the\n"
                     "fraction of inter-spike intervals shorter than 2 ms.  The most\n"
                     "contaminated cluster becomes 2 (top of the palette).  Clusters 0\n"
                     "(artifact) and 1 (noise) are preserved at the start.  Undoable with Ctrl+Z.")).editing();
    // Sort by SNR: renumber clusters by descending mean-waveform SNR, so the
    // cleanest-waveform cluster becomes 2.  Reads the cached mean waveforms, so
    // it is only meaningful once waveforms are computed; it otherwise follows the
    // same enabled state as the other sorts.  Undoable.
    t << CommandSpec("sort.bySnr", "Sort", tr("Sort Clusters by &SNR"), ON(triggered, slotSortClustersBySnr))
            .toolTip(tr("Renumber clusters by descending mean-waveform SNR (peak-to-trough on\n"
                     "the best channel over baseline noise).  Best-SNR cluster becomes 2.\n"
                     "Requires computed mean waveforms — clusters without one sort last.\n"
                     "Clusters 0 (artifact) and 1 (noise) are preserved.  Undoable with Ctrl+Z.")).editing();
    // Sort by amplitude: renumber clusters by descending peak-to-trough of the
    // mean waveform, measured on whichever channel carries it largest, so the
    // biggest unit becomes 2.  Reads the same waveform cache as the SNR sort.
    t << CommandSpec("sort.byAmplitude", "Sort", tr("Sort Clusters by Largest &Amplitude"), ON(triggered, slotSortClustersByAmplitude))
            .toolTip(tr("Renumber clusters by descending peak-to-trough amplitude of the mean\n"
                     "waveform, taken on the channel where it is largest.  The biggest unit\n"
                     "becomes 2.  Requires computed mean waveforms — clusters without one\n"
                     "sort last.  Clusters 0 (artifact) and 1 (noise) are preserved.\n"
                     "Undoable with Ctrl+Z.")).editing();
    // Tiered version: block the clusters by which channel carries their largest
    // amplitude (channel order, so the blocks run down the probe), and order each
    // block by descending amplitude.  Groups units by site while keeping the
    // biggest first within a site.
    t << CommandSpec("sort.byAmplitudeByChannel", "", tr("Sort Clusters by Largest Amplitude by C&hannel"), ON(triggered, slotSortClustersByAmplitudeByChannel))
            .toolTip(tr("Renumber clusters in blocks: one block per channel, in channel order,\n"
                     "each holding the clusters whose largest peak-to-trough amplitude falls\n"
                     "on that channel, ordered biggest first.  Requires computed mean\n"
                     "waveforms — clusters without one sort last.  Clusters 0 (artifact) and\n"
                     "1 (noise) are preserved.  Undoable with Ctrl+Z."));
    // Sort by error-matrix p-value: renumber clusters by descending merge
    // affinity read from the active error matrix, so each cluster's strongest
    // merge candidate ends up adjacent and the top pairs land at low ids.
    // Reads a matrix view, so it reports if none is computed; it otherwise
    // follows the same enabled state as the other sorts.  Undoable.
    t << CommandSpec("sort.byErrorPval", "Sort", tr("Sort Clusters by Error &p-value"), ON(triggered, slotSortClustersByErrorPval))
            .toolTip(tr("Renumber clusters by descending error-matrix merge affinity — each\n"
                     "cluster's highest same-neuron probability against any other cluster.\n"
                     "The strongest merge candidate becomes 2.  Requires a computed,\n"
                     "up-to-date error matrix in the active display.  Undoable with Ctrl+Z.")).editing();
    // Sort by residual, gated by spike count.  Partitions clusters into a
    // high-count block (>= a prompted threshold) and a low-count block, places
    // the high block first (upper-left of the matrix / low ids) and the low
    // block last (lower-right), and seriates each block by residual-matrix
    // similarity.  Needs a computed residual matrix, so it follows the matrix-
    // availability gate (enabled when a ResidualMatrixView is created).  Undoable.
    t << CommandSpec("sort.byResidualGated", "Sort", tr("Sort by Residual (&Gated by Count)"), ON(triggered, slotSortByResidualGated))
            .toolTip(tr("Renumber clusters using the residual matrix, gated by spike count.\n"
                     "Clusters with >= the prompted threshold go to the upper-left (low\n"
                     "ids), the rest to the lower-right; each block is seriated by\n"
                     "residual similarity.  Requires a computed residual matrix.\n"
                     "Clusters 0/1 are preserved at the start.  Undoable with Ctrl+Z.")).disabled().editing();
    // Nearest-neighbour median-waveform sort: renumber clusters along a greedy
    // nearest-neighbour chain over their per-sample median waveforms, so that
    // waveform-similar clusters get adjacent IDs.  Reads the .spk file directly,
    // needs no matrix, so (like Sort by Spike Count) it is enabled by default and
    // follows the document/cluster-op state rather than the matrix gate.  Undoable.
    t << CommandSpec("sort.byWaveformNN", "Sort", tr("Sort by &Nearest-Neighbour Waveform"), ON(triggered, slotSortByWaveformNN))
            .toolTip(tr("Renumber clusters along a nearest-neighbour chain over their per-sample\n"
                     "MEDIAN waveforms (Euclidean distance): each cluster is placed next to\n"
                     "its most waveform-similar unused neighbour.  Reads the .spk file; needs\n"
                     "no matrix.  Preferences > Curation > Cluster order can use the mean\n"
                     "waveforms instead.  Clusters 0/1 preserved at the start.  Undoable with Ctrl+Z.")).editing();
    // Spectral (global) counterpart of the nearest-neighbour waveform sort: same
    // median-waveform distances, but ordered by the Fiedler vector of the
    // similarity Laplacian, so the layout respects global waveform structure and
    // not just local nearest neighbours.  Also matrix-free; enabled by default.
    t << CommandSpec("sort.byWaveformSpectral", "Sort", tr("Sort by Waveform (&Global / Spectral)"), ON(triggered, slotSortByWaveformSpectral))
            .toolTip(tr("Renumber clusters by spectral (Fiedler) seriation of their per-sample\n"
                     "MEDIAN waveforms: orders clusters so the whole layout respects the\n"
                     "GLOBAL waveform structure, not just each cluster's nearest neighbour.\n"
                     "Reads the .spk file; needs no matrix.  Preferences > Curation > Cluster\n"
                     "order can use the mean waveforms instead.  Clusters 0/1 preserved at\n"
                     "the start.  Undoable with Ctrl+Z.")).editing();

    // ── Recluster ─────────────────────────────────────────────────────────────────────────
    t << CommandSpec("recluster.recluster", "Recluster", tr("Re&cluster"), ON(triggered, slotRecluster))
            .shortcut(Qt::SHIFT  | Qt::Key_R).editing();
    t << CommandSpec("recluster.median", "Recluster", tr("Recluster (&median-waveform residual)"), ON(triggered, slotReclusterMedianResidual))
            .shortcut(Qt::SHIFT | Qt::Key_M)
            .toolTip(tr("Recluster the selected cluster(s) on the residuals of their pooled median waveform")).editing();
    t << CommandSpec("recluster.channelVar", "Recluster", tr("Recluster (&channel-variance features)"), ON(triggered, slotReclusterChannelVariance))
            .shortcut(Qt::SHIFT | Qt::Key_C)
            .toolTip(tr("Recluster using the highest-variance channels' features")).editing();
    t << CommandSpec("recluster.against", "Recluster", tr("Recluster &Against References…"), ON(triggered, slotReclusterAgainstReferences))
            .toolTip(tr("One selected cluster is a mixture of the others: sort them pooled, and replace "
                     "only the mixed cluster by its pieces (the references are left as they are)")).editing();
    t << CommandSpec("recluster.fiberkit", "Recluster", tr("Recluster with &fiber-kit (plan on the selection)"), ON(triggered, slotReclusterFiberKit))
            .shortcut(Qt::SHIFT | Qt::Key_Q)
            .toolTip(tr("Run the fiber-kit plan of this folder's fiber-kit.yaml on the selected "
                     "clusters' spikes (a sub-session) and replace them with its clusters")).editing();

    // ── ReclusterEngine ───────────────────────────────────────────────────────────────────
    t << CommandSpec("recluster.engine.klustakwik", "Recluster", tr("KlustaKwik"))
            .toolTip(tr("Recluster runs KiloKlustaKwik on the selection's features (Preferences > Reclustering)")).checkable();
    t << CommandSpec("recluster.engine.fiberkit", "Recluster", tr("fiber-kit"))
            .toolTip(tr("Recluster runs the fiber-kit plan on the selection's sub-session "
                     "(the fiber-kit-recluster plugin's defaults, else the preferences' command)")).checkable();
    // The REFINE mode: fiber-kit's manifold plugin.  A refine engine (descriptor <integration>refine)
    // runs on the children of the selected unit(s) and installs its children under the SAME parents --
    // nothing moves between units and no id changes -- where a recluster engine replaces the selection
    // with new clusters.  Built in so the wheel's Plugin ring carries it; enabled when the descriptor is
    // installed (fk install klusters-plugin).
    t << CommandSpec("recluster.engine.manifold", "Recluster", tr("Refine: fiber-kit manifold"))
            .toolTip(tr("Recluster REFINES the selected unit(s): fiber-manifold fits the field manifold of their "
                     "off-spike channels in sliding windows and installs the result as their children "
                     "(the parents keep their ids; the manifold becomes a feature space under Space)")).checkable();

    // ── Recluster ─────────────────────────────────────────────────────────────────────────
    t << CommandSpec("recluster.splitByKnn", "Split", tr("Split by &KNN voting…"), ON(triggered, slotSplitClusterByKnn))
            .shortcut(Qt::SHIFT | Qt::Key_K)
            .toolTip(tr("Partition the selected cluster into new sub-clusters using "
                     "a K-nearest-neighbour vote against existing well-isolated "
                     "clusters as references.")).disabled().editing();
    // Shift+J: J was unclaimed everywhere -- actions, the application key
    // filter, and the views' own handlers -- so nothing is shadowed either
    // way (Shift+T and Shift+S, the mnemonic choices, are taken by
    // partition-by-time and the similarity sort).  auditKeyBindings()
    // re-checks this at startup.
    t << CommandSpec("recluster.stripByTemplate", "Split", tr("Strip by &Template…"), ON(triggered, slotStripByTemplate))
            .shortcut(Qt::SHIFT | Qt::Key_J)
            .toolTip(tr("Designate one selected cluster as a waveform template and pull "
                     "matching spikes out of the other selected clusters — normalized "
                     "kernel-weighted median-waveform residual, restricted to the "
                     "current channel selection.  Works in both scopes.")).disabled().editing();

    // ── Clusters, Split, Spikes, View (were Actions) ──────────────────────────────────
    // Watershed is in the Split menu.
    // Its construction used to sit in the middle of the Tools menu block -- it was
    // added to reclusterMenu 121 lines after that submenu was built and after the
    // Tools menu had been created, so anyone adding a Tools item beside it would
    // have landed in the Recluster submenu instead.
    t << CommandSpec("action.watershed", "Split", tr("&Watershed Split"), ON(triggered, slotWatershedSplit))
            .shortcut(Qt::SHIFT | Qt::Key_W)
            .statusTip(tr("Split the selection into one new cluster per density basin in the "
                     "active scatter view — selected atoms (one parent) in children view, "
                     "selected parents otherwise.")).editing();
    t << CommandSpec("action.realignSpikes", "Spikes", tr("R&ealign Spikes…"), ON(triggered, slotRealignSpikes))
            .shortcut(Qt::SHIFT | Qt::Key_L)
            .toolTip(tr("Re-align spikes in the selected cluster to their true peak, "
                     "update .res/.spk/.fet files, and swap ordering if needed.")).editing();
    t << CommandSpec("action.partitionByTime", "Split", tr("Partition Cluster by &Time…"), ON(triggered, slotPartitionClusterByTime))
            .shortcut(Qt::SHIFT | Qt::Key_T)
            .toolTip(tr("Cut the selected cluster into consecutive blocks of recording time.\n"
                     "Blocks start at the origin of the session, so the boundaries are the "
                     "same for every cluster and the pieces line up in time.\n"
                     "Works on a parent or, in child view, on an atom.")).editing();
    t << CommandSpec("action.pcaAlignAllClusters", "Spikes", tr("&Align All Clusters"), ON(triggered, slotPcaAlignAllClusters))
            .shortcut(Qt::SHIFT | Qt::Key_P)
            .toolTip(tr("Run Realign Spikes on every cluster (skipping noise=0 and "
                     "artifact=1) with its settings: the Refinement preferences and the "
                     "Top-Channels spin box.  Each cluster's result is auto-accepted as a "
                     "pending change; the batch can be stopped with Edit > Cancel Running Job (Ctrl+.).")).editing();
    t << CommandSpec("action.syncFeatureSpaces", "", tr("S&ync Feature Spaces"), ON(triggered, slotSyncFeatureSpaces))
            .toolTip(tr("Bring every other feature space (method) of this group up to the current "
                     "spike times: re-extract its waveforms from the .fil and re-project its "
                     "features through its own PCA basis.  Use after realigning or nudging in "
                     "the .clu's own space.  The results are working copies, written by Save.")).editing();
    // Super-sampled waveform spaces (Preferences > Realignment > Super-sampling gates them).
    t << CommandSpec("space.supersample2", "Spikes", tr("Super-sample ×2…")).editing();
    t << CommandSpec("space.supersample4", "Spikes", tr("Super-sample ×4…")).editing();
    t << CommandSpec("space.exportBaseRate", "Spikes", tr("Export at the Recording's Sampling…"), ON(triggered, slotExportBaseRate))
            .toolTip(tr("From a super-sampled (_U) session: copy the saved .clu (and its .clc/.clp/.cli)\n"
                     "under the base-rate token, beside the base-rate .res/.spk/.fet -- the shared\n"
                     ".res and the synced base space -- without leaving this session.")).disabled().editing();
    // Step through the Space box without the mouse.  Ctrl+Tab, because plain
    // Tab / Shift+Tab move keyboard focus around the window.
    t << CommandSpec("action.nextFeatureSpace", "View", tr("Ne&xt Feature Space"))
            .shortcut(Qt::CTRL | Qt::Key_Tab)
            .toolTip(tr("Show the next feature space in the Space box "
                     "(spaces that cannot be loaded are skipped).")).editing();
    // Shift+Tab arrives as Backtab, so the chord is Ctrl+Shift+Backtab.
    t << CommandSpec("action.previousFeatureSpace", "View", tr("Previous Feature Spa&ce"))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab)
            .toolTip(tr("Show the previous feature space in the Space box.")).editing();
    t << CommandSpec("action.localPcaSpace", "View", tr("&Local PCA of the Selection"), ON(triggered, slotToggleLocalPca))
            .shortcut(Qt::SHIFT | Qt::Key_Y)
            .toolTip(tr("Show the selected clusters in a feature space of their own: a per-channel PCA\n"
                     "fitted on their spikes alone (parents, or the children selected in the child view)\n"
                     "and applied to every spike.  It replaces the features on screen until you press\n"
                     "the key again or pick another space; nothing is written, and realign and nudge\n"
                     "leave it first.")).editing();
    t << CommandSpec("action.sortSpikesByTime", "", tr("Sort Spikes by &Time"), ON(triggered, slotSortSpikesByTime))
            .toolTip(tr("Realign and nudge keep each spike in its own row, so the .res can drift "
                     "out of time order.  This sorts the saved .res by time and moves the same "
                     "rows in every method's .spk and .fet and in the .clu and .clc, then "
                     "reopens the document.  Save first; undo history is cleared.")).editing();

    // ── Recluster ─────────────────────────────────────────────────────────────────────────
    t << CommandSpec("recluster.dipSplit", "Split", tr("&DipSplit Selected Cluster"), ON(triggered, slotDipSplit))
            .shortcut(Qt::SHIFT | Qt::Key_D)
            .toolTip(tr("Test the selected cluster for hidden bimodality.  If a valley is\n"
                     "detected along one of its top-3 principal components AND the two-\n"
                     "cluster model beats the single-cluster BIC, split the cluster in two.")).editing();

    // ── Clusters, Split, Spikes, View (were Actions) ──────────────────────────────────
    // No keyboard shortcut: Shift+P is owned by "PCA-Center Align All Clusters"
    // (mPcaAlignAllClusters, above).  Both actions used to bind Shift+P, so Qt
    // reported an ambiguous shortcut overload and dispatched neither; probe
    // drift is now reachable from the Spikes menu only.
    t << CommandSpec("action.generateProbeDrift", "Spikes", tr("&Generate Probe Drift…"), ON(triggered, slotGenerateProbeDrift))
            .toolTip(tr("Run ndm_estimatedrift on the current electrode group to estimate probe "
                     "displacement over time.  Requires that this group is already curated "
                     "(a .clu.N file exists).  Produces SESSION.drift alongside the data files.")).editing();
    t << CommandSpec("action.applyDriftSiblings", "Spikes", tr("Apply Drift + &Recluster Siblings…"), ON(triggered, slotApplyDriftSiblings))
            .shortcut(Qt::SHIFT | Qt::Key_F)
            .toolTip(tr("Compute drift-adaptive chunk boundaries from SESSION.drift and optionally "
                     "re-run KlustaKwik on the other electrode groups that share this probe.  "
                     "Requires SESSION.drift (generate it first with 'Generate Probe Drift').")).editing();

    // ── Tools ─────────────────────────────────────────────────────────────────────────────
    // The four curation tools are checkable, mutually-exclusive toggles (Overview
    // redesign step 4): selecting one highlights its toolbar/menu button and clears
    // the others; clicking the active one (or "Normal Cursor") returns to the no-tool
    // default.  The checked state is driven from the active view's real mode by
    // updateToolModeActions(); the slots below toggle it.  Keyboard: Ctrl+1..Ctrl+4.
    // Shortcut removed; reachable via menu / toolbar / Ctrl+1 (slotSingleNew).
    t << CommandSpec("tools.newCluster", "", tr("New Cluster"), ON(triggered, slotSingleNew))
            .checkable().editing();
    t << CommandSpec("tools.splitClusters", "", tr("&Split Clusters"), ON(triggered, slotMultipleNew))
            .checkable().editing();
    // Multi-lasso: a lasso tool whose closed lassos are kept rather than cut, and the
    // button that splits by them.  (Their icons are drawn: actionicons.cpp.)
    t << CommandSpec("tools.multiLasso", "", tr("&Multi-Lasso"), ON(triggered, slotMultiLasso))
            .toolTip(tr("Multi-lasso (Ctrl+5): closing a lasso keeps it instead of cutting.\n"
                     "Draw several, then Split by Lassos (Enter): each overlap pattern\n"
                     "becomes a cluster -- two overlapping lassos give three.")).checkable().editing();
    t << CommandSpec("tools.applyMultiLasso", "", tr("Split by &Lassos"), ON(triggered, slotApplyMultiLasso))
            .toolTip(tr("Split the kept multi-lassos by their overlaps (Enter)")).editing();
    // Reachable via menu / toolbar / Ctrl+3.
    t << CommandSpec("tools.deleteArtifactSpikes", "", tr("Delete &Artifact Spikes"), ON(triggered, slotDeleteArtifact))
            .checkable().editing();
    // Reachable via menu / toolbar / Ctrl+4.
    t << CommandSpec("tools.deleteNoisySpikes", "", tr("Delete &Noisy Spikes"), ON(triggered, slotDeleteNoise))
            .checkable().editing();

    // ── WaveForms ─────────────────────────────────────────────────────────────────────────
    // Shortcut removed; reachable via menu only.  This frees plain "T"
    // entirely for the palette-focus "renumber-to-end" intercept (which
    // is handled in eventFilter rather than as a QAction).
    t << CommandSpec("waveforms.timeFrame", "Waveforms", tr("&Time Frame"), ON(triggered, slotTimeFrameMode))
            .checkable();
    t << CommandSpec("waveforms.overlay", "Waveforms", tr("&Overlay"), ON(triggered, setOverLayPresentation))
            .shortcut(Qt::Key_O).checkable();
    t << CommandSpec("waveforms.meanStd", "Waveforms", tr("&Mean and Standard Deviation"), ON(triggered, slotMeanPresentation))
            .shortcut(Qt::Key_M).checkable();
    t << CommandSpec("waveforms.increaseAmplitude", "Waveforms", tr("&Increase Amplitude"), ON(triggered, slotIncreaseAmplitude))
            .shortcut(Qt::Key_I);
    t << CommandSpec("waveforms.decreaseAmplitude", "Waveforms", tr("&Decrease Amplitude"), ON(triggered, slotDecreaseAmplitude))
            .shortcut(Qt::Key_D);

    // ── Correlations ──────────────────────────────────────────────────────────────────────
    // Shift+M is now the median-residual recluster shortcut; Scale-by-Maximum
    // stays available from View > Correlations (no accelerator).
    //Initialize the presentation mode to scale by maximum.
    t << CommandSpec("correlations.scaleByMax", "Correlations", tr("Scale by &Maximum"), ON(triggered, slotScaleByMax))
            .checkable().checked();
    t << CommandSpec("correlations.scaleByAsymptote", "Correlations", tr("Scale by &Asymptote"), ON(triggered, slotScaleByShouler))
            .shortcut(Qt::SHIFT | Qt::Key_A).checkable();
    t << CommandSpec("correlations.uniformScale", "Correlations", tr("&Uniform Scale"), ON(triggered, slotNoScale))
            .shortcut(Qt::SHIFT | Qt::Key_U).checkable();
    t << CommandSpec("correlations.asymptoteLine", "Correlations", tr("Asymptote &Line"), ON(triggered, slotShoulderLine))
            .shortcut(Qt::Key_L).checkable();
    // Shortcut removed (Shift+I freed for future use; reach via menu only).
    t << CommandSpec("correlations.increaseAmplitude", "Correlations", tr("&Increase Amplitude"), ON(triggered, slotIncreaseCorrelogramsAmplitude));
    // Shortcut removed (Shift+D was conflicting with DipSplit; reach via menu only).
    t << CommandSpec("correlations.decreaseAmplitude", "Correlations", tr("&Decrease Amplitude"), ON(triggered, slotDecreaseCorrelogramsAmplitude));

    // ── Trace ─────────────────────────────────────────────────────────────────────────────
    t << CommandSpec("trace.increaseChannelAmplitudes", "Traces", tr("&Increase Channel Amplitudes"), ON(triggered, slotIncreaseAllChannelsAmplitude))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_I);
    t << CommandSpec("trace.decreaseChannelAmplitudes", "Traces", tr("&Decrease Channel Amplitudes"), ON(triggered, slotDecreaseAllChannelsAmplitude))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_D);
    t << CommandSpec("trace.nextSpike", "Traces", tr("&Next Spike"), ON(triggered, slotShowNextCluster))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Right);
    t << CommandSpec("trace.previousSpike", "Traces", tr("&Previous Spike"), ON(triggered, slotShowPreviousCluster))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Left);
    t << CommandSpec("trace.showLabels", "Traces", tr("Show &Labels"), ON(triggered, slotShowLabels))
            .shortcut(Qt::CTRL | Qt::Key_L).checkable();

    // ── Display ───────────────────────────────────────────────────────────────────────────
    // committed at open from the file set; never a runtime toggle
    t << CommandSpec("display.hierarchicalSession", "Display", tr("Hierarchical Session (.clc + .clp)"))
            .toolTip(tr("Checked when the opened clustering has both a .clc child\n"
                     "and a .clp parent-map sibling — the session is then\n"
                     "hierarchical.  Determined by the files present at open, not\n"
                     "toggled at runtime; a flat .clu session is the alternative.")).checkable().disabled();

    // ── Hierarchy ─────────────────────────────────────────────────────────────────────────
    // The one entry for a FLAT session: turn it into a hierarchical one.
    t << CommandSpec("hierarchy.promoteToHierarchical", "Hierarchy", tr("Promote to &Hierarchical Session (.clc + .clp)…"), ON(triggered, slotPromoteToHierarchy))
            .toolTip(tr("Make this flat clustering hierarchical: write a .clc child layer and a\n"
                     ".clp parent map next to the .clu, then reopen the session with the child\n"
                     "palette.  Every unit starts as one atom with the same id; split a unit's\n"
                     "atoms in the child palette from there.  An existing .clc that is nested in\n"
                     "the .clu is kept.  Unsaved edits are saved first; the undo history is cleared.")).disabled().editing();
    t << CommandSpec("hierarchy.mergeParents", "Hierarchy", tr("&Merge Selected Parents"), ON(triggered, slotMergeParents))
            .disabled().editing();
    t << CommandSpec("hierarchy.promoteChild", "Hierarchy", tr("&Promote Child to New Parent"), ON(triggered, slotPromoteChildren))
            .disabled().editing();
    t << CommandSpec("hierarchy.groupChildren", "Hierarchy", tr("&Group Selected Children into New Parent"), ON(triggered, slotGroupChildrenIntoParent))
            .disabled().editing();
    t << CommandSpec("hierarchy.dissolveParent", "Hierarchy", tr("&Dissolve Parent into Children"), ON(triggered, slotDissolveParent))
            .disabled().editing();
    t << CommandSpec("hierarchy.breakUpParents", "Hierarchy", tr("Brea&k Up Parent by Template…"), ON(triggered, slotBreakUpParents))
            .toolTip(tr("Break the selected parent(s) up: each child moves, whole and keeping its id,\n"
                     "to the parent whose template (mean waveform) it matches best among the\n"
                     "parents with more than a given number of spikes (default 100).  The children\n"
                     "matching none become parents of their own, the largest keeping the parent's\n"
                     "id, as Dissolve Parent does.  Shows the plan first.")).disabled().editing();
    t << CommandSpec("hierarchy.dropChildNoise", "Hierarchy", tr("Drop Child to &Noise"), ON(triggered, slotDropChildToNoise))
            .disabled().editing();
    t << CommandSpec("hierarchy.compactIds", "Hierarchy", tr("Compact Cluster &IDs (parents and children)"), ON(triggered, slotCompactClusterIds))
            .toolTip(tr("Renumber both layers into the low integers with no gaps: parents become\n"
                     "2, 3, 4 … and atoms likewise, with the noise and artifact bins left in\n"
                     "place.  Occasional housekeeping after a long session of splitting.\n"
                     "The atom undo history is dropped, because it names ids that move.")).disabled().editing();
    t << CommandSpec("hierarchy.mergeOrphanChildren", "Hierarchy", tr("Merge &Orphan Children (by median waveform)…"), ON(triggered, slotMergeOrphanChildren))
            .shortcut(Qt::SHIFT | Qt::Key_B)
            .toolTip(tr("Gather the selected parent's tiny children — the single-spike debris that\n"
                     "accumulates after repeated splitting — judged against the median waveform\n"
                     "of the parent's other children, the ones not being merged: those whose\n"
                     "shape matches are merged into one child, the rest stay as they are.\n"
                     "Nothing is deleted; the merged child is an ordinary atom of the same parent.")).disabled().editing();
    t << CommandSpec("hierarchy.mergeOrphanParents", "Hierarchy", tr("Merge Orphan Paren&ts (by template)…"), ON(triggered, slotMergeOrphanParents))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_B)
            .toolTip(tr("Gather the tiny parents (below a spike count) by the parent whose template\n"
                     "(mean waveform) each matches best among the parents that are not being\n"
                     "merged: those matching the same parent are merged into one new parent\n"
                     "beside it, and those matching none stay as they are.  The parents they are\n"
                     "compared with are not changed.  Works on the selected parents, or on all\n"
                     "of them when fewer than two are selected; shows the plan first.")).disabled().editing();
    // Shift+N, not bare F.  QAction shortcuts here use Qt's default
    // WindowShortcut context -- they fire whenever the main window is active,
    // whatever has focus -- so a bare-F action silently outranked the feature
    // view's own F handler from every focus position.  One key, one owner:
    // F belongs to the feature view (see the F/A intercept in eventFilter),
    // and repair-nesting takes a free Shift combination.
    t << CommandSpec("hierarchy.repairNesting", "Hierarchy", tr("Re&repairNesting (re-cut atoms onto parents)"), ON(triggered, slotRepairNesting))
            .shortcut(Qt::SHIFT | Qt::Key_N)
            .toolTip(tr("Re-cut child atoms that now straddle more than one parent so each\n"
                     "atom belongs to a single parent again, and regenerate the .clp\n"
                     "parent map.  Run after reassigning / consolidating the parents.")).disabled().editing();
    t << CommandSpec("hierarchy.mergeChildren", "Hierarchy", tr("Merge Selected &Children (atom)"), ON(triggered, slotMergeChildren))
            .disabled().editing();
    t << CommandSpec("hierarchy.flatten", "Hierarchy", tr("&Flatten Hierarchy to Flat Clustering"), ON(triggered, slotFlattenHierarchyToClu))
            .toolTip(tr("Walk every parent and merge its child atoms into one self child\n"
                     "(numbered parent + 10000), so the atom layer becomes a copy of the\n"
                     "parent layer.  Discards all sub-mode structure -- undo with\n"
                     "Ctrl+Shift+Z.")).disabled().editing();
    t << CommandSpec("hierarchy.undoChildEdit", "Edit", tr("&Undo Child-Layer Edit"), ON(triggered, slotUndoChildEdit))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Z).disabled().editing();
    t << CommandSpec("hierarchy.redoChildEdit", "Edit", tr("&Redo Child-Layer Edit"), ON(triggered, slotRedoChildEdit))
            .shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Y).disabled().editing();

    // ── Display ───────────────────────────────────────────────────────────────────────────
    t << CommandSpec("display.newCluster", "Display", tr("New C&luster Display"), ON(triggered, slotWindowNewClusterDisplay));
    t << CommandSpec("display.newWaveform", "Display", tr("New &Waveform Display"), ON(triggered, slotWindowNewWaveformDisplay));
    t << CommandSpec("display.newCorrelation", "Display", tr("New C&orrelation Display"), ON(triggered, slotWindowNewCrosscorrelationDisplay));
    // ???????????????
    t << CommandSpec("display.newOverview", "Display", tr("New &Overview Display"), ON(triggered, slotWindowNewOverViewDisplay));
    t << CommandSpec("display.newGroupingAssistant", "Display", tr("New &Grouping Assistant Display"), ON(triggered, slotWindowNewGroupingAssistantDisplay));
    t << CommandSpec("display.newTrace", "Display", tr("New &Trace Display"), ON(triggered, slotNewTraceDisplay));
    t << CommandSpec("display.newResidualMatrix", "Display", tr("New &Residual Matrix Display"), ON(triggered, slotNewResidualMatrix))
            .toolTip(tr("Add a residual-matrix display to the active view.  Cell (row A,\n"
                     "col B) is the variance of cluster A's spikes taken about cluster\n"
                     "B's mean waveform; the diagonal is A's within-cluster variance.\n"
                     "Press U to (re)compute it along with the error/template matrices."));
    // Open a drift-shifted correlation matrix on the active display.  Purely
    // diagnostic: it never moves spikes and takes no part in the reorder /
    // residual-gated sort decisions.
    t << CommandSpec("display.newDriftMatrix", "Display", tr("New &Drift Matrix Display"), ON(triggered, slotNewDriftMatrix))
            .toolTip(tr("Add a drift-matrix display to the active view.  Cell (row A,\n"
                     "col B) is the correlation between A's mean waveform shifted along\n"
                     "the probe depth axis by the slider's µm value and B's mean: +Δ\n"
                     "above the diagonal, −Δ below.  A unit pair split by probe drift\n"
                     "reads high at the Δ that realigns it.  Needs probe geometry\n"
                     "(the session YAML probes: section)."));
    t << CommandSpec("display.newTemplateLibrary", "Display", tr("New Template &Library Display"), ON(triggered, slotWindowNewTemplateLibraryDisplay))
            .toolTip(tr("Open the template library for the open group: the marked units'\n"
                     "linked waveform series (.wtf/.wti).  Pick a unit and a link\n"
                     "(drift / adapt) and scrub its bins; select two or more units and\n"
                     "pin them as the oblique (template-axis) basis.  Mark units as\n"
                     "templates and save to build the library."));
    t << CommandSpec("display.renameActive", "Display", tr("&Rename Active Display"), ON(triggered, renameActiveDisplay))
            .shortcut(Qt::CTRL | Qt::Key_R);
    t << CommandSpec("display.closeActive", "Display", tr("&Close Active Display"), ON(triggered, slotDisplayClose))
            .shortcut(Qt::CTRL | Qt::Key_W);

    // ── Settings ──────────────────────────────────────────────────────────────────────────
    t << CommandSpec("view.showMainToolbar", "", tr("Show Main Toolbar"), ON(triggered, slotViewMainToolBar))
            .checkable().checked();
    t << CommandSpec("view.showActionsToolbar", "", tr("Show Actions"), ON(triggered, slotViewActionBar))
            .checkable().checked();
    t << CommandSpec("view.showToolsToolbar", "", tr("Show Tools"), ON(triggered, slotViewToolBar))
            .checkable().checked();
    t << CommandSpec("view.showParametersToolbar", "", tr("Show Parameters"), ON(triggered, slotViewParameterBar))
            .checkable().checked();
    t << CommandSpec("view.showClusterInfo", "", tr("Show Cluster Info"), ON(triggered, slotViewClusterInfo))
            .checkable();
    t << CommandSpec("view.showStatusBar", "", tr("Show StatusBar"), ON(triggered, slotViewStatusBar))
            .checkable().checked();
    t << CommandSpec("view.increasePointSize", "", tr("Increase Point Size"), ON(triggered, slotIncreasePointSize));
    t << CommandSpec("view.decreasePointSize", "", tr("Decrease Point Size"), ON(triggered, slotDecreasePointSize));
    //Initialize the update mode
    t << CommandSpec("view.immediateUpdate", "", tr("Immediate Update"), ON(triggered, slotImmediateSelection))
            .checkable().checked();
    t << CommandSpec("view.delayedUpdate", "", tr("Delayed Update"), ON(triggered, slotDelaySelection))
            .checkable();
    t << CommandSpec("edit.preferences", "", tr("&Preferences"), ON(triggered, executePreferencesDlg))
            .shortcut(Qt::Key_P);

    // ── Help ──────────────────────────────────────────────────────────────────────────────
    t << CommandSpec("help.shortcuts", "Help", tr("Keyboard Shortcuts…"), ON(triggered, slotShowShortcutHelp))
            .shortcut(Qt::Key_H);

    return t;
}

#undef ON

QAction* KlustersApp::addCommand(QMenu* menu, const char* id)
{
    const auto it = commandSpecs.constFind(QString::fromLatin1(id));
    Q_ASSERT_X(it != commandSpecs.constEnd(), "KlustersApp::addCommand", id);
    // Without a menu the command is created only: createMenus() places it in its layout block.
    auto make = [this, menu](const QString& text) { return menu ? menu->addAction(text) : new QAction(text, this); };
    if (it == commandSpecs.constEnd()) return make(QString::fromLatin1(id));   // visibly wrong, not a crash
    const CommandSpec& s = it.value();
    QAction* a = make(s.text);
    if (!s.key.isEmpty())    a->setShortcut(s.key);
    if (!s.status.isEmpty()) a->setStatusTip(s.status);
    if (!s.tip.isEmpty())    a->setToolTip(s.tip);
    if (s.isCheckable)       a->setCheckable(true);
    if (s.isChecked)         a->setChecked(true);
    if (!s.isEnabled)        a->setEnabled(false);
    if (s.wire)              s.wire(a);
    commandActions.insert(s.id, a);
    return a;
}
