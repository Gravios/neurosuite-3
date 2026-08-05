// klustersdoc_dipsplit.cpp — KlustersDoc bimodality / KNN cluster splitting.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// hidden-bimodality and reference-vote split subsystem of KlustersDoc —
// dipSplitDecide (pure decision: bloat gate, top-3 PC valley test, k-means refine,
// BIC gate), dipSplitApply (commit a decision to new clusters), and
// splitClusterByKnnVsReferences (KNN vote against reference templates).  Declarations
// (including the DipSplitDecision result type) remain in klustersdoc.h; mechanical
// relocation, no logic change.  The block uses none of the custody helpers, core PCA
// headers, or realign file-statics, so it carries only the include preamble.
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
// KlustersDoc::dipSplitDecide
//
// Pure decision function: tests a cluster for hidden bimodality and returns
// a structured decision (accept/reject + metrics + per-spike labels).  No
// side effects on KlustersDoc state — safe to call repeatedly, safe to use
// from a "preview before commit" UI, safe to unit-test in isolation.
//
// Algorithm (ported from KiloKlustaKwik Phase 8):
//   1. Collect cluster members (nD-dim feature vectors, time dim excluded)
//   2. Gate A (bloat):   fit μ + Σ, Cholesky, compute Mahalanobis² per spike,
//                         reject if mahal²₉₀ < bloatFactor · χ²(d, 0.9)
//                         (skipped when bloatFactor == 0)
//   3. Top-3 PCs:        power iteration with deflation on the cluster
//   4. Gate B (valley):  project onto each PC, KDE valley test
//   5. Seed:             k=2 partition at valley location
//   6. Refine:           k-means on full nD features
//   7. BIC gate:         accept only if BIC(k=2) < BIC(k=1)
// ---------------------------------------------------------------------------
KlustersDoc::DipSplitDecision
KlustersDoc::dipSplitDecide(int   clusterId,
                             int   minSize,
                             float bloatFactor,
                             float valleyThresh)
{
    DipSplitDecision D;
    D.clusterId = clusterId;
    D.reason    = QStringLiteral("skip");

    // Validate cluster exists and fetch its spikes.
    SortableTable spkTable;
    if (!clusteringData->spikePositions(clusterId, spkTable)) {
        D.reason = QStringLiteral("cluster_not_found");
        return D;
    }
    const int M = static_cast<int>(spkTable.nbOfColumns());
    if (M < minSize * 2) {
        D.reason = QStringLiteral("too_small");
        return D;
    }

    // Feature dimensions, excluding the timestamp column.
    const Data& d = data();
    const int nDtot = d.nbOfDimensionsTotal();
    const int dPCA  = nDtot - 1;
    if (dPCA < 2) {
        D.reason = QStringLiteral("bad_features");
        return D;
    }

    // Build feature matrix X [M × dPCA], row-major.  featureValue() is
    // 1-based in both spike index and dimension index.  We also remember the
    // 1-based feature row for each member so the caller can map labels back
    // to spike file rows for the data move.
    std::vector<float>      X(static_cast<size_t>(M) * dPCA);
    QList<dataType>         rowsByMember;
    rowsByMember.reserve(M);
    for (int i = 0; i < M; ++i) {
        const dataType row1 = spkTable(1, static_cast<dataType>(i + 1));
        rowsByMember.append(row1);
        for (int j = 0; j < dPCA; ++j)
            X[static_cast<size_t>(i) * dPCA + j] =
                static_cast<float>(d.featureValue(row1, j + 1));
    }

    // -------------------------------------------------------------------
    // Gate A — bloat test
    // Fit single Gaussian (μ, Σ), compute Mahalanobis² for each member,
    // compare 90th percentile to χ²(d, 0.9) · bloatFactor.
    // -------------------------------------------------------------------
    std::vector<double> mu(dPCA, 0.0);
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < dPCA; ++j)
            mu[j] += X[static_cast<size_t>(i) * dPCA + j];
    for (int j = 0; j < dPCA; ++j) mu[j] /= M;

    // Covariance (full, row-major); small-sample ridge on diagonal.
    std::vector<double> cov(static_cast<size_t>(dPCA) * dPCA, 0.0);
    for (int i = 0; i < M; ++i) {
        const float* row = X.data() + static_cast<size_t>(i) * dPCA;
        for (int r = 0; r < dPCA; ++r) {
            const double dr = row[r] - mu[r];
            for (int c = r; c < dPCA; ++c) {
                const double dc = row[c] - mu[c];
                cov[static_cast<size_t>(r * dPCA + c)] += dr * dc;
            }
        }
    }
    const double invN = 1.0 / std::max(1, M - 1);
    double diagMean = 0.0;
    for (int r = 0; r < dPCA; ++r) {
        for (int c = r; c < dPCA; ++c)
            cov[static_cast<size_t>(r * dPCA + c)] *= invN;
        diagMean += cov[static_cast<size_t>(r * dPCA + r)];
    }
    diagMean /= dPCA;
    // Mirror upper→lower and add small ridge for numerical stability
    const double ridge = 1e-6 * diagMean;
    for (int r = 0; r < dPCA; ++r) {
        cov[static_cast<size_t>(r * dPCA + r)] += ridge;
        for (int c = r + 1; c < dPCA; ++c)
            cov[static_cast<size_t>(c * dPCA + r)] =
                cov[static_cast<size_t>(r * dPCA + c)];
    }

    // In-place Cholesky: L such that L·Lᵀ = Σ, stored in lower triangle.
    std::vector<double> L(static_cast<size_t>(dPCA) * dPCA, 0.0);
    bool cholOK = true;
    for (int r = 0; r < dPCA && cholOK; ++r) {
        for (int c = 0; c <= r && cholOK; ++c) {
            double s = cov[static_cast<size_t>(r * dPCA + c)];
            for (int k = 0; k < c; ++k)
                s -= L[static_cast<size_t>(r * dPCA + k)]
                   * L[static_cast<size_t>(c * dPCA + k)];
            if (r == c) {
                if (s <= 0.0) { cholOK = false; break; }
                L[static_cast<size_t>(r * dPCA + r)] = std::sqrt(s);
            } else {
                L[static_cast<size_t>(r * dPCA + c)] =
                    s / L[static_cast<size_t>(c * dPCA + c)];
            }
        }
    }
    if (!cholOK) {
        D.reason = QStringLiteral("bad_features");
        return D;
    }

    // Mahalanobis² per spike via forward substitution L·y = (x-μ), m² = |y|².
    std::vector<double> mahal2(static_cast<size_t>(M));
    std::vector<double> yvec(static_cast<size_t>(dPCA));
    for (int i = 0; i < M; ++i) {
        const float* row = X.data() + static_cast<size_t>(i) * dPCA;
        double m2 = 0.0;
        for (int r = 0; r < dPCA; ++r) {
            double s = row[r] - mu[r];
            for (int k = 0; k < r; ++k)
                s -= L[static_cast<size_t>(r * dPCA + k)] * yvec[k];
            const double diag = L[static_cast<size_t>(r * dPCA + r)];
            const double y = s / diag;
            yvec[r] = y;
            m2 += y * y;
        }
        mahal2[static_cast<size_t>(i)] = m2;
    }

    // 90th-percentile of Mahalanobis² distribution.
    {
        std::vector<double> sorted = mahal2;
        std::sort(sorted.begin(), sorted.end());
        const double idx = 0.90 * (sorted.size() - 1);
        const size_t lo  = static_cast<size_t>(std::floor(idx));
        const size_t hi  = std::min(sorted.size() - 1, lo + 1);
        const double frac = idx - lo;
        D.mahal2P90 = sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }
    // χ²(d, 0.9) via Wilson-Hilferty (z_0.9 = 1.2816).
    {
        const double dD = static_cast<double>(dPCA);
        const double a  = 1.0 - 2.0 / (9.0 * dD);
        const double b  = 1.2816 * std::sqrt(2.0 / (9.0 * dD));
        D.chi2_90 = dD * std::pow(a + b, 3.0);
    }
    // bloatFactor == 0 disables the gate.  This is the recommended setting
    // for interactive Klusters use: the user is already targeting a cluster
    // they suspect is bimodal, and a fresh-fit Gaussian (no EM context) makes
    // the gate unreliable for small or low-dimensional clusters.  Non-zero
    // values enforce the gate as in KiloKlustaKwik Phase 8.
    if (bloatFactor > 0.0f &&
        D.mahal2P90 < bloatFactor * D.chi2_90) {
        D.reason = QStringLiteral("not_bloated");
        return D;
    }

    // -------------------------------------------------------------------
    // Top-3 PCs via power iteration (uses cluster centroid already in mu).
    // -------------------------------------------------------------------
    constexpr int kPCA = 3;
    std::vector<double> pcs(static_cast<size_t>(kPCA) * dPCA, 0.0);
    dipsplit::top_pcs_power_iteration(X.data(), M, dPCA, kPCA, pcs.data());

    // Gate B — valley test on each PC; keep the deepest valley.
    int    bestPC         = -1;
    double bestDepth      = 0.0;
    double bestValleyLoc  = 0.0;
    std::vector<double> bestProj;
    for (int pc = 0; pc < kPCA; ++pc) {
        const double* u = pcs.data() + pc * dPCA;
        std::vector<double> proj(static_cast<size_t>(M));
        for (int i = 0; i < M; ++i) {
            const float* row = X.data() + static_cast<size_t>(i) * dPCA;
            double s = 0.0;
            for (int j = 0; j < dPCA; ++j)
                s += (row[j] - mu[j]) * u[j];
            proj[static_cast<size_t>(i)] = s;
        }
        const dipsplit::ValleyResult vr =
            dipsplit::valley_test(proj.data(), M, valleyThresh);
        if (vr.depth > bestDepth) {
            bestPC        = pc;
            bestDepth     = vr.depth;
            bestValleyLoc = vr.valley_loc;
            bestProj      = std::move(proj);
        }
    }
    D.bestPC    = bestPC;
    D.bestDepth = bestDepth;
    if (bestPC < 0 || bestDepth < valleyThresh) {
        D.reason = QStringLiteral("no_valley");
        return D;
    }

    // -------------------------------------------------------------------
    // Seed k=2 at the valley; compute initial centroids.
    // -------------------------------------------------------------------
    std::vector<int> labels(static_cast<size_t>(M), 0);
    for (int i = 0; i < M; ++i)
        labels[static_cast<size_t>(i)] =
            (bestProj[static_cast<size_t>(i)] >= bestValleyLoc) ? 1 : 0;

    std::vector<double> c0(dPCA, 0.0), c1(dPCA, 0.0);
    int n0 = 0, n1 = 0;
    for (int i = 0; i < M; ++i) {
        const float* row = X.data() + static_cast<size_t>(i) * dPCA;
        if (labels[static_cast<size_t>(i)] == 0) {
            for (int j = 0; j < dPCA; ++j) c0[j] += row[j];
            ++n0;
        } else {
            for (int j = 0; j < dPCA; ++j) c1[j] += row[j];
            ++n1;
        }
    }
    if (n0 < minSize || n1 < minSize) {
        D.n0 = n0; D.n1 = n1;
        D.reason = QStringLiteral("small_child");
        return D;
    }
    for (int j = 0; j < dPCA; ++j) { c0[j] /= n0; c1[j] /= n1; }

    // Refine with k-means.
    dipsplit::kmeans2_refine(X.data(), M, dPCA, c0.data(), c1.data(),
                             labels.data(), /*max_iters=*/20);

    n0 = n1 = 0;
    for (int i = 0; i < M; ++i)
        (labels[static_cast<size_t>(i)] == 0) ? ++n0 : ++n1;
    D.n0 = n0; D.n1 = n1;
    if (n0 < minSize || n1 < minSize) {
        D.reason = QStringLiteral("small_child");
        return D;
    }

    // Gate C — BIC: accept only if two-cluster model beats one-cluster.
    const dipsplit::BicPair bp = dipsplit::bic_two_vs_one(
        X.data(), M, dPCA, labels.data());
    D.deltaBIC = bp.bic_k1 - bp.bic_k2;
    if (!(bp.bic_k2 < bp.bic_k1)) {
        D.reason = QStringLiteral("bic_worse");
        return D;
    }

    // Accepted — return labels and row mapping for the commit.
    D.accepted     = true;
    D.reason       = QStringLiteral("split");
    D.labels       = std::move(labels);
    D.rowsByMember = std::move(rowsByMember);
    return D;
}

// ---------------------------------------------------------------------------
// KlustersDoc::dipSplitApply
//
// Commits a pre-computed DipSplitDecision (model A: two-cluster split).
//
// Architecture:
//   1. Allocate two new IDs at the tail: leftId = nextFreeClusterId(),
//      rightId = leftId + 1.
//   2. Build leftRows (label==0) and rightRows (label==1).
//   3. Call Data::splitClusterTwoWays — partitions the source's spikes
//      into leftId + rightId in a single rebuild.  Source becomes empty
//      and is removed.  ONE Data-side undo entry.
//   4. Call commitTwoClusterCreation — palette colours, view
//      notifications, doc-side undo (one entry).  View-side undo
//      routes through addNewClustersToView (recluster variant) for
//      one view-side entry.
//
// One Ctrl+Z fully reverts.  One Ctrl+Y fully replays.  No rename, no
// asymmetric undo.
//
// Palette result for source=22 with previous max=31:
//   pre:   {1, 2, ..., 21, 22, 23, ..., 31}
//   post:  {1, 2, ..., 21,     23, ..., 31, 32=left, 33=right}
//
// minSize/bloatFactor/valleyThresh are recorded in the curation log
// alongside the decision metrics; they don't influence behaviour here.
// ---------------------------------------------------------------------------
KlustersDoc::DipSplitResult
KlustersDoc::dipSplitApply(const DipSplitDecision& D,
                            int   minSize,
                            float bloatFactor,
                            float valleyThresh)
{
    auto resultFromDecision = [](const DipSplitDecision& D) {
        DipSplitResult R;
        R.accepted   = D.accepted;
        R.sourceId   = D.clusterId;
        R.n0         = D.n0;
        R.n1         = D.n1;
        R.bestPC     = D.bestPC;
        R.bestDepth  = D.bestDepth;
        R.mahal2P90  = D.mahal2P90;
        R.chi2_90    = D.chi2_90;
        R.deltaBIC   = D.deltaBIC;
        R.reason     = D.reason;
        return R;
    };

    auto buildLogDetails = [&](const DipSplitDecision& D, int leftId, int rightId) {
        QMap<QString, QVariant> m;
        m.insert(QStringLiteral("algorithm"),     QStringLiteral("dipsplit"));
        m.insert(QStringLiteral("source_cluster"), D.clusterId);
        m.insert(QStringLiteral("left_cluster"),   leftId);
        m.insert(QStringLiteral("right_cluster"),  rightId);
        m.insert(QStringLiteral("reason"),         D.reason);
        m.insert(QStringLiteral("min_size"),       minSize);
        m.insert(QStringLiteral("bloat_factor"),   static_cast<double>(bloatFactor));
        m.insert(QStringLiteral("valley_thresh"),  static_cast<double>(valleyThresh));
        m.insert(QStringLiteral("n_left"),         D.n0);
        m.insert(QStringLiteral("n_right"),        D.n1);
        m.insert(QStringLiteral("best_pc"),        D.bestPC);
        m.insert(QStringLiteral("best_depth"),     D.bestDepth);
        m.insert(QStringLiteral("mahal2_p90"),     D.mahal2P90);
        m.insert(QStringLiteral("chi2_90"),        D.chi2_90);
        m.insert(QStringLiteral("delta_bic"),      D.deltaBIC);
        return m;
    };

    if (!D.accepted)
        return resultFromDecision(D);

    const int sourceClusterId = D.clusterId;

    // ── Allocate two free cluster IDs at the tail ────────────────────────
    const int leftId  = static_cast<int>(clusteringData->nextFreeClusterId());
    const int rightId = leftId + 1;
    if (leftId == 0) {
        DipSplitResult R = resultFromDecision(D);
        R.accepted = false;
        R.reason   = QStringLiteral("no_free_id");
        return R;
    }

    // ── Curation-log: open the action ─────────────────────────────────────
    logBefore(CurationLogger::ActionType::SPLIT, QList<int>{ sourceClusterId });

    // ── Build the row-sets for label==0 (left) and label==1 (right) ──────
    QSet<dataType> leftRows;
    QSet<dataType> rightRows;
    leftRows.reserve(D.n0);
    rightRows.reserve(D.n1);
    const int M = static_cast<int>(D.labels.size());
    for (int i = 0; i < M; ++i) {
        const dataType row = D.rowsByMember.at(i);
        if (D.labels[static_cast<size_t>(i)] == 0)
            leftRows.insert(row);
        else
            rightRows.insert(row);
    }

    // ── Mutate (single Data-side undo entry) ─────────────────────────────
    KlustersView* activeView = app()->activeView();

    QList<int> fromClusters;
    QList<int> emptiedClusters;
    QList<int> newClusters;
    clusteringData->splitClusterTwoWays(sourceClusterId,
                                         leftRows,  leftId,
                                         rightRows, rightId,
                                         fromClusters, emptiedClusters,
                                         newClusters);
    if (newClusters.size() != 2) {
        // Degenerate split — one side empty after row resolution.
        if (activeView) activeView->showAllWidgets();
        if (curationLogger && curationLogger->isOpen()) {
            curationLogger->recordActionDetails(buildLogDetails(D, 0, 0));
        }
        logAfter(QList<int>{ sourceClusterId });
        DipSplitResult R = resultFromDecision(D);
        R.accepted = false;
        R.reason   = QStringLiteral("small_child");
        return R;
    }

    // ── UI plumbing (single doc-side undo entry, single view-side entry) ─
    commitTwoClusterCreation(leftId, rightId, fromClusters, emptiedClusters,
                             activeView);

    // Hierarchical mode: this split partitions the source's spikes by an algorithmic
    // criterion that knows nothing about the .clc atom layer, so every atom the cut
    // crosses is left spanning the resulting fibers.
    //
    // Re-cut HERE, immediately after the parent mutation and before anything is told
    // about it.  Everything below is an observer -- view notification, the matrix
    // signal bus, showAllWidgets, the palette rebuild -- and Qt delivers direct
    // connections synchronously, so repairing afterwards means each of them runs
    // against a layer whose atoms straddle.  It also means the repair's own childData
    // mutation invalidates the caches those observers just rebuilt, so the refresh is
    // done twice and the first one is wrong.
    // childData-guarded, so flat sessions are untouched.
    if (childData) refiberize();

    // ── Curation-log: details + after-snapshot ───────────────────────────
    if (curationLogger && curationLogger->isOpen()) {
        QMap<QString, QVariant> details = buildLogDetails(D, leftId, rightId);
        curationLogger->recordActionDetails(details);
    }
    logAfter(QList<int>{ leftId, rightId });

    // ── Build result ─────────────────────────────────────────────────────
    DipSplitResult R = resultFromDecision(D);
    R.leftId  = leftId;
    R.rightId = rightId;
    return R;
}



// ---------------------------------------------------------------------------
// KlustersDoc::splitClusterByKnnVsReferences
//
// Doc-level orchestration for the K-nearest-neighbour N-way split.  The
// shape mirrors dipSplitApply:
//
//   1. Validate the source via clusterHasMembers (patch96 desync guard).
//   2. Open the curation log for this action.
//   3. Call Data::splitClusterByKnnVsReferences — pure algorithm; on
//      success, prepareUndo has been called (single Data-side undo).
//   4. Update the colour palette: append a colour entry per new cluster
//      (HSV-cycled by id, same scheme as commitTwoClusterCreation).
//   5. Record one doc-side undo via prepareReclusteringUndo (treating
//      the operation as N-source → M-new — the source is in
//      emptiedClusters iff fully consumed).
//   6. Update every KlustersView with addNewClustersToView (single
//      view-side entry each — the recluster variant).
//   7. Record curation-log details + after-snapshot.
//
// One Ctrl+Z fully reverts.  No rename, no asymmetric undo.
// ---------------------------------------------------------------------------
KlustersDoc::KnnSplitResult
KlustersDoc::splitClusterByKnnVsReferences(int    sourceCluster,
                                            int    K,
                                            double majorityThreshold,
                                            int    minNewClusterSize,
                                            int    minRefClusterSize)
{
    KnnSplitResult R;
    R.sourceId = sourceCluster;

    // ── Desync guard (same predicate as slotRecluster) ────────────────────
    if (!clusterHasMembers(sourceCluster)) {
        R.reason = tr("Cluster %1 is not registered in clusterInfoMap "
                      "(spikesByCluster ↔ clusterInfoMap desync). "
                      "Save the session and re-open before retrying.")
                      .arg(sourceCluster);
        return R;
    }

    // ── Open curation log ─────────────────────────────────────────────────
    logBefore(CurationLogger::ActionType::SPLIT, QList<int>{ sourceCluster });

    // ── Run the algorithm (single Data-side undo entry on success) ────────
    QList<int> newClusters;
    QList<int> matchedReferences;
    QList<int> emptiedClusters;
    QString    err;
    const bool ok = clusteringData->splitClusterByKnnVsReferences(
        sourceCluster, K, majorityThreshold,
        minNewClusterSize, minRefClusterSize,
        newClusters, matchedReferences, emptiedClusters, err);
    if (!ok) {
        // Nothing was mutated.  Close the log entry as "no-op" and bail.
        if (curationLogger && curationLogger->isOpen()) {
            QMap<QString, QVariant> details;
            details.insert(QStringLiteral("algorithm"),       QStringLiteral("knn_split_vs_references"));
            details.insert(QStringLiteral("source_cluster"),  sourceCluster);
            details.insert(QStringLiteral("k"),               K);
            details.insert(QStringLiteral("majority_thresh"), majorityThreshold);
            details.insert(QStringLiteral("min_new_size"),    minNewClusterSize);
            details.insert(QStringLiteral("min_ref_size"),    minRefClusterSize);
            details.insert(QStringLiteral("status"),          QStringLiteral("rejected"));
            details.insert(QStringLiteral("reason"),          err);
            curationLogger->recordActionDetails(details);
        }
        logAfter(QList<int>{ sourceCluster });
        R.reason = err;
        return R;
    }

    R.accepted          = true;
    R.newClusters       = newClusters;
    R.matchedReferences = matchedReferences;
    R.emptiedClusters   = emptiedClusters;
    R.nResidual         = 0;
    for (int i = 0; i < newClusters.size(); ++i)
        if (matchedReferences.value(i, 0) == -1)
            R.nResidual = clusteringData->nbOfSpikes(
                static_cast<dataType>(newClusters[i]));

    // ── Doc-side undo entry (one for the whole N-way operation) ───────────
    prepareReclusteringUndo(newClusters, emptiedClusters);

    // ── UI plumbing: palette colours + view notifications ─────────────────
    // Mirrors commitTwoClusterCreation exactly, generalised to N new
    // clusters.  Builds clustersToShow as: current view contents minus
    // emptied sources, plus all new clusters.
    KlustersView* activeView = app()->activeView();
    QList<int> clustersToShow;
    if (activeView) {
        const QList<int> currentlyShown = activeView->clusters();
        for (int c : currentlyShown)
            if (!emptiedClusters.contains(c)) clustersToShow.append(c);
    }
    // Hierarchical mode: this split partitions the source's spikes by an algorithmic
    // criterion that knows nothing about the .clc atom layer, so every atom the cut
    // crosses is left spanning the resulting fibers.
    //
    // Re-cut HERE, immediately after the parent mutation and before anything is told
    // about it.  Everything below is an observer -- view notification, the matrix
    // signal bus, showAllWidgets, the palette rebuild -- and Qt delivers direct
    // connections synchronously, so repairing afterwards means each of them runs
    // against a layer whose atoms straddle.  It also means the repair's own childData
    // mutation invalidates the caches those observers just rebuilt, so the refresh is
    // done twice and the first one is wrong.
    // childData-guarded, so flat sessions are untouched.
    if (childData) refiberize();

    QColor color;
    for (int newId : newClusters) {
        // Same HSV scheme used by commitTwoClusterCreation — keeps the
        // palette stable and visually consistent with other split paths.
        color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10,
                     200, 255);
        clusterColorList->append(newId, color);
        clustersToShow.append(newId);
    }
    for (int oldId : emptiedClusters)
        clusterColorList->remove(oldId);

    // Per-view notification: recluster-variant primitive — one view-side
    // undo entry per view.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == activeView);
        v->addNewClustersToView(emptiedClusters, newClusters, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }
    // Matrix-view / signal-bus notification — recluster shape (dissolved
    // sources only, new clusters appear at the tail).
    emit newClustersAdded(emptiedClusters);

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    if (activeView) activeView->showAllWidgets();

    refreshActivePalette(clustersToShow);

    // The source cluster (if not fully consumed) has stale waveform /
    // correlogram caches — invalidate them.  emptied clusters' caches
    // vanish with the cluster itself, so skip those.
    QList<int> fromClusters;
    fromClusters.append(sourceCluster);
    for (int cid : fromClusters) {
        if (emptiedClusters.contains(cid)) continue;
        invalidateWaveformCache(cid);
        invalidateCorrelogramCache(cid);
    }
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        for (int cid : fromClusters) {
            if (emptiedClusters.contains(cid)) continue;
            v->invalidateClusterDisplay(cid);
        }
    }

    setModified(true);

    // ── Curation-log details ──────────────────────────────────────────────
    if (curationLogger && curationLogger->isOpen()) {
        QMap<QString, QVariant> details;
        details.insert(QStringLiteral("algorithm"),       QStringLiteral("knn_split_vs_references"));
        details.insert(QStringLiteral("source_cluster"),  sourceCluster);
        details.insert(QStringLiteral("k"),               K);
        details.insert(QStringLiteral("majority_thresh"), majorityThreshold);
        details.insert(QStringLiteral("min_new_size"),    minNewClusterSize);
        details.insert(QStringLiteral("min_ref_size"),    minRefClusterSize);
        details.insert(QStringLiteral("n_new_clusters"),  static_cast<int>(newClusters.size()));
        QList<QVariant> newIdsV;     for (int i : newClusters)       newIdsV.append(i);
        QList<QVariant> refIdsV;     for (int i : matchedReferences) refIdsV.append(i);
        QList<QVariant> emptiedV;    for (int i : emptiedClusters)   emptiedV.append(i);
        details.insert(QStringLiteral("new_clusters"),       newIdsV);
        details.insert(QStringLiteral("matched_references"), refIdsV);
        details.insert(QStringLiteral("emptied"),            emptiedV);
        details.insert(QStringLiteral("status"),             QStringLiteral("accepted"));
        curationLogger->recordActionDetails(details);
    }
    logAfter(newClusters);

    R.reason = tr("Split %1 into %2 new cluster(s)%3.")
                .arg(sourceCluster)
                .arg(newClusters.size())
                .arg(emptiedClusters.contains(sourceCluster)
                     ? tr(" (source consumed)") : tr(""));
    return R;
}
