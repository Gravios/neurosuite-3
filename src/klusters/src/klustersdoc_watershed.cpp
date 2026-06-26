// klustersdoc_watershed.cpp — KlustersDoc 2D density-watershed cluster split.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements
// watershedSelectedClusters — runs a 2D density watershed (Watershed2D) on the
// selected clusters using the active scatter view's X/Y feature dimensions, turning
// each basin into a new cluster and dissolving the sources.  Declarations remain in
// klustersdoc.h (which also includes watershed2d.h for the Config/Result types in the
// signature); mechanical relocation, no logic change.  The realign-style include for
// watershed2d.h travels with this TU via the shared preamble; the block uses no
// custody helpers, core PCA headers, or nbUndo.
//
#include <algorithm>
#include <functional>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cerrno>          // patch63 — saveDocument errno diagnostics
#include <cstring>         // patch63 — strerror
#include <vector>
#include <stdint.h>
#ifdef _OPENMP
#include <omp.h>            // CPU-fallback realign parallelisation
#endif
#include <QElapsedTimer>    // opt-in per-phase realign timing
#include <chrono>           // inter-cluster gap timestamp (steady_clock)
/***************************************************************************
                          klustersdoc.cpp  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// include files for Qt
#include <QDir>
#include <QFile>
#include <QWidget>
#include <QStringList>
#include <QString>
#include <QTimer>
#include <QDateTime>
#include <QApplication>

#include <QList>

#include <QEvent>
#include <QMessageBox>
#include <QDebug>
#include <QAction>
#include <QUrl>
#include <QRegularExpression>
#include <QTextStream>

// application specific includes
#include "processwidget.h"
#include "klusters.h"
#include "klustersdoc.h"
#include "configuration.h"
#include "klustersview.h"
#include "watershed2d.h"
#include "clusterview.h"
#include "klustersdoc.h"
#include "clusterPalette.h"
#include "types.h"
#include "autosavethread.h"
#include "parameteryamlmodifier.h"
#include "dipsplit.h"
#include "parameteryamlreader.h"
#include "clusteruserinformation.h"

//C, C++ include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <math.h>
#include <climits>

#include "timer.h"

#include <neurosuite/core/neurofileio.h>  // variant-aware input resolution
#include <neurosuite/core/custody.hpp>     // shared chain-of-custody policy

// ---------------------------------------------------------------------------
// KlustersDoc::watershedSelectedClusters
//
// Run a 2D density watershed on the *selected* clusters in the palette,
// using the active scatter view's X/Y feature dimensions.  Each watershed
// basin becomes a new cluster; the source clusters are dissolved (their
// spikes redistributed across new clusters or, if some spikes fell on
// watershed lines / below density threshold, into a small residual that
// stays in the source).
//
// Returns the number of new clusters created (0 on failure / no basins).
// ---------------------------------------------------------------------------
int KlustersDoc::watershedSelectedClusters(const QList<int>& selectedClusters,
                                           const Watershed2D::Config& cfg)
{
    KlustersView* activeView = app()->activeView();
    if (!activeView) return 0;

    const int dimX = activeView->abscissaDimension();
    const int dimY = activeView->ordinateDimension();
    QList<int>  inputs = selectedClusters;
    if (inputs.isEmpty()) return 0;

    // Drop 0 / 1 from input — we never reassign artefact / noise spikes.
    inputs.removeAll(0);
    inputs.removeAll(1);
    if (inputs.isEmpty()) return 0;

    // ── Collect the (x, y) coordinates and matching feature-row indices
    // ── for every spike in the input clusters.
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<dataType> rowIdxs;
    {
        size_t totalEst = 0;
        for (int cid : inputs) totalEst += clusteringData->nbOfSpikes(cid);
        xs.reserve(totalEst);
        ys.reserve(totalEst);
        rowIdxs.reserve(totalEst);
    }
    for (int cid : inputs) {
        SortableTable subset;
        if (!clusteringData->spikePositions(cid, subset)) continue;
        const dataType n = subset.nbOfColumns();
        for (dataType i = 1; i <= n; ++i) {
            const dataType row = subset(1, i);
            xs.push_back(static_cast<double>(clusteringData->featureValue(row, dimX)));
            ys.push_back(static_cast<double>(clusteringData->featureValue(row, dimY)));
            rowIdxs.push_back(row);
        }
    }
    if (xs.size() < 50) return 0;     // not enough points to cluster

    // ── Run watershed.  Caller-supplied config is taken as-is, but if
    // ── the caller left minPeakHeight / minBasinSize at sentinel 0
    // ── values we auto-tune them from the data size.
    Watershed2D::Config c = cfg;
    if (c.minPeakHeight <= 0)
        c.minPeakHeight = std::max(3, static_cast<int>(xs.size() / 2000));
    if (c.minBasinSize <= 0)
        c.minBasinSize  = std::max(20, static_cast<int>(xs.size() / 500));

    Watershed2D::Result res = Watershed2D::run(xs, ys, c);
    if (!res.ok || res.numBasins == 0) return 0;
    if (res.numBasins == 1) {
        // Watershed returns just one basin: pointless to "split" — bail.
        return 0;
    }

    // ── Build feature-row -> basin map.  Unlabeled spikes (label 0)
    // ── must still get a label so integrateBasinLabeling's "every
    // ── source spike has a basin" contract holds — assign them all
    // ── to basin (numBasins+1), which becomes the residual cluster
    // ── after the renumber.  This way the residual is its own
    // ── tail-positioned cluster rather than being left in the source.
    QHash<dataType, int> rowToBasin;
    rowToBasin.reserve(static_cast<int>(rowIdxs.size()));
    const int residualBasin = res.numBasins + 1;
    bool sawResidual = false;
    int residualCount = 0;
    // Per-basin spike counts (basin label -> count); residualBasin is
    // included if any unlabeled spikes were rerouted.
    QMap<int,int> basinCounts;
    for (size_t i = 0; i < rowIdxs.size(); ++i) {
        int lab = res.pointLabels[i];
        if (lab <= 0) {
            lab = residualBasin;
            sawResidual = true;
            ++residualCount;
        }
        rowToBasin.insert(rowIdxs[i], lab);
        basinCounts[lab]++;
    }

    // Per-source spike counts (cid -> count) for the "before" snapshot.
    QMap<int,int> sourceCounts;
    for (int cid : inputs)
        sourceCounts[cid] = static_cast<int>(clusteringData->nbOfSpikes(cid));

    // ── Curation log (before).
    logBefore(CurationLogger::ActionType::WATERSHED, inputs);

    // ── Hand off to the recluster-style integrate pipeline.  This
    // ── dissolves source clusters and emits new ones at IDs strictly
    // ── greater than the previous max — exactly the renumber-after-
    // ── last-cluster behaviour the user expects.
    QList<int> newClusterList;
    if (!clusteringData->integrateBasinLabeling(inputs, rowToBasin,
                                                 newClusterList)) {
        return 0;
    }
    if (newClusterList.isEmpty()) return 0;

    // ── Doc-level undo: same shape as recluster.
    prepareReclusteringUndo(newClusterList, inputs);

    // ── Update colour palette: add new, remove dissolved.  Mirrors
    // ── reclusteringUpdate's main branch.
    KlustersView* mainActiveView = app()->activeView();
    QList<int> clustersToShow;
    {
        const QList<int> currentlyShown = mainActiveView->clusters();
        for (int c : currentlyShown)
            if (!inputs.contains(c)) clustersToShow.append(c);
    }
    QColor color;
    for (int newId : newClusterList) {
        color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10,
                     200, 255);
        clusterColorList->append(newId, color);
        clustersToShow.append(newId);
    }
    for (int oldId : inputs)
        clusterColorList->remove(oldId);

    // ── View notification.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == mainActiveView);
        v->addNewClustersToView(inputs, newClusterList, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }
    emit newClustersAdded(inputs);

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    mainActiveView->showAllWidgets();

    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);

    // ── Curation-log details ─────────────────────────────────────────────
    // One ACTION_DETAIL record covering: source clusters and their sizes,
    // resolved watershed config (after auto-tune), kernel diagnostics, and
    // per-basin spike counts.  Keys mirror the dipsplit pattern so a
    // downstream reader can treat the WATERSHED records uniformly.
    if (curationLogger && curationLogger->isOpen()) {
        QMap<QString, QVariant> details;
        details.insert(QStringLiteral("algorithm"),
                       QStringLiteral("watershed_2d"));

        // Source side: cluster IDs (in input order) + per-source counts +
        // total spike count fed into the watershed.
        {
            QStringList src;
            for (int cid : inputs) src.append(QString::number(cid));
            details.insert(QStringLiteral("source_clusters"),
                           src.join(QStringLiteral(",")));
            QStringList srcCounts;
            for (int cid : inputs)
                srcCounts.append(QString("%1:%2").arg(cid)
                                 .arg(sourceCounts.value(cid, 0)));
            details.insert(QStringLiteral("source_counts"),
                           srcCounts.join(QStringLiteral(",")));
            details.insert(QStringLiteral("total_input_spikes"),
                           static_cast<qulonglong>(xs.size()));
        }

        // Feature-space the watershed ran in.
        details.insert(QStringLiteral("dim_x"), dimX);
        details.insert(QStringLiteral("dim_y"), dimY);

        // Config: BOTH requested values and resolved (post-auto-tune) values.
        details.insert(QStringLiteral("grid_size"),         c.gridSize);
        details.insert(QStringLiteral("smooth_sigma_req"),  cfg.smoothSigma);
        details.insert(QStringLiteral("smooth_sigma_eff"),  res.effSigma);
        details.insert(QStringLiteral("min_peak_height_req"),
                       cfg.minPeakHeight);
        details.insert(QStringLiteral("min_peak_height_eff"),
                       res.effPeakHeight);
        details.insert(QStringLiteral("min_basin_size_req"),
                       cfg.minBasinSize);
        details.insert(QStringLiteral("min_basin_size_eff"),
                       res.effMinBasinSize);
        details.insert(QStringLiteral("use_local_maxima"),
                       c.useLocalMaxima);

        // Kernel diagnostics.
        details.insert(QStringLiteral("num_peaks"),   res.numPeaks);
        details.insert(QStringLiteral("num_basins"),  res.numBasins);

        // Result side: new cluster IDs and their sizes.  Residual basin
        // (catch-all for spikes that fell outside any peak) is logged
        // separately so downstream tooling can distinguish "watershed
        // basin proper" from "residual leftover".
        {
            QStringList newIds;
            for (int nid : newClusterList) newIds.append(QString::number(nid));
            details.insert(QStringLiteral("new_clusters"),
                           newIds.join(QStringLiteral(",")));

            // Per-basin counts in the order the new IDs were assigned.
            // integrateBasinLabeling sorts new IDs by basin label
            // ascending, so basin 1 → newClusterList[0], basin 2 →
            // newClusterList[1], ..., residualBasin → newClusterList[k].
            QStringList basinPairs;
            const auto basinKeys = basinCounts.keys();   // ascending
            for (int i = 0; i < basinKeys.size() && i < newClusterList.size();
                 ++i) {
                const int basinLabel = basinKeys[i];
                const int newId      = newClusterList[i];
                const int count      = basinCounts.value(basinLabel, 0);
                basinPairs.append(QString("%1:%2").arg(newId).arg(count));
            }
            details.insert(QStringLiteral("new_cluster_counts"),
                           basinPairs.join(QStringLiteral(",")));
        }

        details.insert(QStringLiteral("residual_present"),  sawResidual);
        details.insert(QStringLiteral("residual_count"),    residualCount);

        curationLogger->recordActionDetails(details);
    }

    logAfter(newClusterList);

    return newClusterList.size();
}
