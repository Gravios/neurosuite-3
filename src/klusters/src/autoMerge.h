/***************************************************************************
                          autoMerge.h  -  description
                             -------------------
    Patch 0069 — Auto-Merge action algorithm + preview dialog.

    Same template-cross-correlation mechanism KKE uses in
    WithinChunkTemplateMatch / WithinChunkTemplateMatchMedianKnn:
      * build per-cluster mean OR median template from spike waveforms
      * pairwise normalised xcorr with bounded sample shift
      * union-find on pairs scoring at or above the threshold → groups
    Settings come from configuration() (see prefautomerge — patch 0068).
    The applied step uses doc->groupClusters() so the merge integrates
    with klusters' existing undo/redo.
 ***************************************************************************/

#ifndef AUTOMERGE_H
#define AUTOMERGE_H

#include <QList>
#include <QString>
#include <vector>

class KlustersDoc;
class KlustersView;
class Data;
class QWidget;

namespace AutoMerge {

struct Settings {
    int    algorithm           = 1;     ///< 0 = mean, 1 = median
    int    medianK             = 50;    ///< median: per-cluster subsample cap
    double scoreThreshold      = 0.98;
    int    maxShift            = 0;     ///< 0 = auto (nSamp/4)
    int    taperSamples        = 0;     ///< 0 = no Hann taper
    int    minClusterSize      = 25;
    int    scope               = 0;     ///< 0 = selected, 1 = all active
    bool   previewBeforeApply  = true;
};

struct MergeGroup {
    QList<int> clusters;      ///< sorted ascending
    double     maxPairScore = 0.0;
    int        totalSpikes  = 0;
};

/**
 * Compute merge proposals.  Returns groups of size >= 2 whose members
 * are connected via pairwise scores at or above the threshold.
 * Skips clusters 0 (artefact) and 1 (noise) unconditionally.
 *
 * The progress dialog is parented to @p parent; cancelling returns an
 * empty list.  This call is synchronous on the GUI thread and uses
 * QApplication::processEvents() to keep the UI responsive while reading
 * waveforms.
 */
QList<MergeGroup> computeProposals(
    KlustersDoc*       doc,
    Data&              data,
    const Settings&    settings,
    const QList<int>&  candidatesIn,
    QWidget*           parent);

/**
 * Modal preview dialog.  Shows proposed groups with a checkbox per
 * group; the user can uncheck groups to skip them.  Returns the subset
 * the user accepted (empty if Cancel).
 */
QList<MergeGroup> promptPreview(
    const QList<MergeGroup>& proposals,
    QWidget*                 parent);

}  // namespace AutoMerge

#endif
