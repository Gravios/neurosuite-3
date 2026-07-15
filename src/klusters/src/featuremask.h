/***************************************************************************
 * featuremask.h — map .fet feature dimensions to probe channels.
 *
 * The waveform-based matrices restrict themselves to a channel subset by
 * slicing channel-major waveforms (channelmask.h).  The feature-based
 * consumers — the error matrix and the clustering/splitting feature selection,
 * both of which read Data::features() — need the equivalent for .fet columns,
 * which means knowing which channel each column came from.
 *
 * Layout, as written by process_pca (ndmanager-plugins):
 *
 *     for ch in 0 .. pcaChans-1:          // PCA block, channel-major
 *         for j in 0 .. featPerChan-1:
 *             col++ = PCA[ch][j]
 *     if extra:                           // optional peak block, one per channel
 *         for ch in 0 .. pcaChans-1:
 *             col++ = peak[ch]
 *
 * followed by a timestamp column that process_mergefeatures appends, which is
 * Data::timeDimension() and belongs to no channel.
 *
 * pcaChans is NOT always Data::nbOfchannels(): on a stderiv session
 * process_pca_stderiv drops the last (linearly dependent) channel before the
 * PCA, so the .fet carries features for nChan-1 channels only and the last
 * channel has no feature columns at all.
 *
 * Qt-free and header-only so the mapping can be unit-tested in isolation.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef FEATUREMASK_H
#define FEATUREMASK_H

#include <algorithm>
#include <vector>

/**Resolved .fet column layout.  `valid` is false when the numbers do not match
 * any layout process_pca can produce, in which case callers must NOT mask —
 * guessing a stride here would silently corrupt the clustering.*/
struct FeatureLayout {
    int  pcaChans   = 0;      ///< channels the .fet has PCA features for
    int  featPerChan = 0;     ///< PCA components per channel
    bool hasExtra   = false;  ///< trailing one-peak-per-channel block present
    bool valid      = false;
};

/**
 * Work out the .fet column layout.
 *
 * @param nFeatureDims  Data::nbOfDimensionsTotal() - 1 (timestamp excluded).
 * @param nChan         Data::nbOfchannels().
 * @param featPerChan   Data::nbOfFeaturesByChannel() (the YAML's per-group
 *                      nFeatures), i.e. PCA components per channel.
 * @param stderivSession KlustersDoc::isStderivSession().
 *
 * The stderiv flag decides pcaChans; the observed dimension count then has to
 * agree with one of the two layouts process_pca emits for that channel count.
 * Inferring pcaChans from the dimension count alone is not safe — the layouts
 * collide whenever featPerChan == nChan-1, where nChan*featPerChan equals
 * (nChan-1)*(featPerChan+1) exactly.  If nothing matches, `valid` stays false.
 */
inline FeatureLayout fmResolveLayout(int nFeatureDims, int nChan,
                                     int featPerChan, bool stderivSession)
{
    FeatureLayout layout;
    if (nFeatureDims <= 0 || nChan <= 0 || featPerChan <= 0) return layout;

    const int pcaChans = stderivSession ? (nChan - 1) : nChan;
    if (pcaChans <= 0) return layout;

    if (nFeatureDims == pcaChans * featPerChan) {
        layout = FeatureLayout{pcaChans, featPerChan, false, true};
    } else if (nFeatureDims == pcaChans * featPerChan + pcaChans) {
        layout = FeatureLayout{pcaChans, featPerChan, true, true};
    }
    return layout;   // unrecognised -> valid stays false -> caller must not mask
}

/**0-based feature dimension -> group-local channel, or -1 when the column
 * belongs to no channel (timestamp, or anything past the known blocks).*/
inline int fmChannelOfDim(int dim, const FeatureLayout& layout)
{
    if (!layout.valid || dim < 0) return -1;
    const int pcaCols = layout.pcaChans * layout.featPerChan;
    if (dim < pcaCols) return dim / layout.featPerChan;
    if (layout.hasExtra && dim < pcaCols + layout.pcaChans) return dim - pcaCols;
    return -1;
}

/**
 * The 0-based feature dimensions belonging to @p selection, ascending.
 *
 * Returns EMPTY to mean "no restriction — use every dimension".  That covers
 * the no-selection case, an unresolved layout, a selection naming every channel
 * that has features, and — deliberately — a selection that maps to no feature
 * columns at all.  The last one is reachable: on a stderiv session the last
 * channel has no PCA columns, so selecting only it yields nothing, and
 * clustering on zero dimensions is worse than clustering on all of them.
 * Callers that want to tell the user should compare against the input.
 */
inline std::vector<int> fmSelectedDims(const std::vector<int>& selection,
                                       const FeatureLayout& layout,
                                       int nFeatureDims)
{
    std::vector<int> dims;
    if (!layout.valid || selection.empty() || nFeatureDims <= 0) return dims;

    for (int d = 0; d < nFeatureDims; ++d) {
        const int ch = fmChannelOfDim(d, layout);
        if (ch >= 0 && std::find(selection.begin(), selection.end(), ch) != selection.end())
            dims.push_back(d);
    }
    if (static_cast<int>(dims.size()) == nFeatureDims) dims.clear();  // all == none
    return dims;
}

#endif // FEATUREMASK_H
