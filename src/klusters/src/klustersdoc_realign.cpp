// klustersdoc_realign.cpp — KlustersDoc spike realignment and nudge.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// in-GUI spike-realignment machinery of KlustersDoc — realignSpikes and its
// file-static helpers (readSpkWaveform/writeSpkWaveform/readTextFile/writeTextFile/
// swapSpkEntries/parseRealignArgs/realignRmsRecenter), the pending-file lifecycle
// (initPendingFiles/commitAndRenewPending/rejectLastRealign), the waveform/correlogram
// cache invalidators, the cluster-membership checks, and nudgeClusterTimestamps.
//
// Mechanical relocation, no logic change.  Declarations remain in klustersdoc.h.
// Carries the same preamble as klustersdoc.cpp, which (via klustersdoc.h) pulls in
// neurosuite/core/pca_projection.hpp and stderiv_transform.hpp, plus
// klustersdoc_internal.h and the three custody using-declarations, so loadPca /
// applyStderivTransform and featureMethod/resolveFeature/stripFeatureSuffix resolve
// identically.  The seven file-static helpers are realign-only and move with it.
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
#include "klustersdoc_internal.h"  // shared custody path helpers (split TUs)

extern int nbUndo;

// Chain-of-custody path helpers now live in klustersdoc_internal.h so the split
// klustersdoc_*.cpp translation units share one definition instead of each
// carrying a private anonymous-namespace copy.  Pull the three names into this TU.
using klustersdoc_internal::featureMethod;
using klustersdoc_internal::resolveFeature;
using klustersdoc_internal::stripFeatureSuffix;


// ===========================================================================
// KlustersDoc::realignSpikes  (normalised cross-correlation template method)
// ===========================================================================
//
// Algorithm overview
// ------------------
// 1. Load all waveforms for the cluster into a contiguous int16 buffer.
// 2. Compute the cluster template: int16 mean waveform across all spikes.
// 3. Dispatch XcorrDispatch::compute() — runs on CUDA / HIP / SYCL / OMP,
//    returns per-spike optimal lag and normalised xcorr score.
// 4. For each spike with |lag| > 0 and score ≥ minScore:
//      a. Re-extract the waveform at the shifted position (from .dat if
//         available, otherwise by rolling the existing buffer with zero-pad).
//      b. Write the new waveform to .spk.N.
//      c. Update the timestamp in .res.N and the feature row in .fet.N
//         (calling process_refeaturize for re-projection onto PCA basis).
//      d. If the new timestamp violates sort order, perform a sorted
//         insertion into .res / .spk / .clu / .fet and in memory.
//
// The xcorr approach uses the full multi-channel spatiotemporal waveform
// shape rather than the peak of a single channel, so it correctly handles
// spikes detected on different channels within the same cluster.

#include "realign_xcorr.h"   // XcorrDispatch lives here via the dispatch TU
#include "realign_center.h"  // realign_center::circularRecenterShift (shared)
#include "pca_refine_dispatch.h"

// Forward declaration — XcorrDispatch is defined in realign_xcorr_dispatch.cpp
namespace XcorrDispatch {
int compute(const int16_t*, const int16_t*,
            int, int, int, int, float, int*, float*);
const char* backendName();
}

// ---------------------------------------------------------------------------
// File I/O helpers  (unchanged from previous implementation)
// ---------------------------------------------------------------------------

static bool readSpkWaveform(const QString& spkPath, long spikeIdx0,
                             int nChannels, int nSamples,
                             std::vector<int16_t>& waveform)
{
    // spikeIdx0: 0-based
    QFile f(spkPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    qint64 bytesPerSpike = static_cast<qint64>(nChannels) * nSamples * 2;
    if (!f.seek(spikeIdx0 * bytesPerSpike)) { f.close(); return false; }
    waveform.resize(static_cast<size_t>(nChannels * nSamples));
    qint64 nRead = f.read(reinterpret_cast<char*>(waveform.data()),
                          static_cast<qint64>(waveform.size() * 2));
    f.close();
    return nRead == static_cast<qint64>(waveform.size() * 2);
}

static bool writeSpkWaveform(const QString& spkPath, long spikeIdx0,
                              int nChannels, int nSamples,
                              const std::vector<int16_t>& waveform)
{
    QFile f(spkPath);
    if (!f.open(QIODevice::ReadWrite)) return false;
    qint64 bytesPerSpike = static_cast<qint64>(nChannels) * nSamples * 2;
    if (!f.seek(spikeIdx0 * bytesPerSpike)) { f.close(); return false; }
    qint64 nWritten = f.write(reinterpret_cast<const char*>(waveform.data()),
                              static_cast<qint64>(waveform.size() * 2));
    f.close();
    return nWritten == static_cast<qint64>(waveform.size() * 2);
}

// Read all lines of a text file into a vector<QString>.
static bool readTextFile(const QString& path, std::vector<QString>& lines)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    while (!ts.atEnd())
        lines.push_back(ts.readLine());
    f.close();
    return true;
}

// Write vector<QString> back to a text file (overwrite).
static bool writeTextFile(const QString& path, const std::vector<QString>& lines)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream ts(&f);
    for (const auto& l : lines) ts << l << "\n";
    f.close();
    return true;
}

// Swap the waveform of two spikes in .spk.N.
static bool swapSpkEntries(const QString& spkPath, long idxA0, long idxB0,
                            int nChannels, int nSamples)
{
    std::vector<int16_t> a, b;
    if (!readSpkWaveform(spkPath, idxA0, nChannels, nSamples, a)) return false;
    if (!readSpkWaveform(spkPath, idxB0, nChannels, nSamples, b)) return false;
    if (!writeSpkWaveform(spkPath, idxA0, nChannels, nSamples, b)) return false;
    if (!writeSpkWaveform(spkPath, idxB0, nChannels, nSamples, a)) return false;
    return true;
}

// Parse the realign argument string, overriding the caller-supplied defaults.
// Phase-0 decomposition: lifted verbatim out of realignSpikes.  Pure -- it
// touches no member or document state, only the argv-style token list and the
// out-parameters, so it is a free function in this TU (no header change).
static void parseRealignArgs(const QString& args,
                             int& maxShift, float& minScore, int& nIter,
                             int& nTopChan, bool& pcaRefine, bool& rmsRecenter,
                             float& rMin)
{
    const QStringList tokens = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (qsizetype ti = 0; ti < tokens.size(); ++ti) {
        const QString& tok = tokens[ti];
        if ((tok == QStringLiteral("--threshold") || tok == QStringLiteral("-t"))
                && ti + 1 < tokens.size()) {
            bool ok2; float v = tokens[++ti].toFloat(&ok2);
            if (ok2 && v > 0.0f && v <= 1.0f) minScore = v;
        } else if ((tok == QStringLiteral("--iterations") || tok == QStringLiteral("-i"))
                && ti + 1 < tokens.size()) {
            bool ok2; int v = tokens[++ti].toInt(&ok2);
            if (ok2 && v >= 1 && v <= 20) nIter = v;
        } else if ((tok == QStringLiteral("--maxshift") || tok == QStringLiteral("-m"))
                && ti + 1 < tokens.size()) {
            bool ok2; int v = tokens[++ti].toInt(&ok2);
            if (ok2 && v >= 1) maxShift = v;
        } else if ((tok == QStringLiteral("--topchannels") || tok == QStringLiteral("-k"))
                && ti + 1 < tokens.size()) {
            bool ok2; int v = tokens[++ti].toInt(&ok2);
            if (ok2 && v >= 0) nTopChan = v;
        } else if (tok == QStringLiteral("--pca-refine") ||
                   tok == QStringLiteral("-p")) {
            // patch82 — no value, just a boolean flag.
            pcaRefine = true;
        } else if (tok == QStringLiteral("--recenter-rms")) {
            // RMS circular group-recenter — no value, boolean flag.
            rmsRecenter = true;
        } else if (tok == QStringLiteral("--rmin")
                && ti + 1 < tokens.size()) {
            bool ok2; float v = tokens[++ti].toFloat(&ok2);
            if (ok2 && v >= 0.0f && v <= 1.0f) rMin = v;
        }
    }
}

// Phase-0 decomposition of realignSpikes: optional post-alignment RMS circular
// group-recenter (--recenter-rms).  Lifted verbatim out of realignSpikes; it
// touches no member state -- it transforms the in-memory waveform buffer and
// shift bookkeeping in place and logs through the caller-supplied stream and
// flush -- so it is a free function in this TU (no header change).
static void realignRmsRecenter(std::vector<int16_t>& wavBuf,
                               std::vector<int>& cumShift, int& nShifted,
                               int64_t N, int nChan, int nSamp,
                               size_t spkElems, int peakSamp0, float rMin,
                               QTextStream& log,
                               const std::function<void()>& emitFlush)
{
    // Per-sample energy across the group, summed over all channels.
    std::vector<double> energy(static_cast<size_t>(nSamp), 0.0);
    for (int64_t i = 0; i < N; ++i) {
        const int16_t* w = wavBuf.data()
            + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
        for (int ch = 0; ch < nChan; ++ch) {
            const int16_t* wc = w + ch * nSamp;
            for (int t = 0; t < nSamp; ++t) {
                const double v = static_cast<double>(wc[t]);
                energy[static_cast<size_t>(t)] += v * v;
            }
        }
    }

    // Circular weighted mean of the energy profile (shared math).
    const realign_center::RecenterResult rc =
        realign_center::circularRecenterShift(energy.data(), nSamp,
                                              peakSamp0, rMin);

    if (!rc.applied) {
        log << "RMS recenter: R=" << QString::number(rc.R, 'f', 3)
            << " < rmin=" << QString::number(rMin, 'f', 2)
            << " (energy not single-lobed) — recenter skipped.\n";
        emitFlush();
    } else {
        const int dg = rc.shift;
        if (dg != 0) {
            std::vector<int16_t> tmp(spkElems);
            for (int64_t i = 0; i < N; ++i) {
                int16_t* w = wavBuf.data()
                    + static_cast<ptrdiff_t>(i)
                    * static_cast<ptrdiff_t>(spkElems);
                for (int t = 0; t < nSamp; ++t) {
                    const int src = (t + dg + nSamp) % nSamp;
                    for (int ch = 0; ch < nChan; ++ch)
                        tmp[static_cast<size_t>(ch * nSamp + t)] =
                            w[static_cast<size_t>(ch * nSamp + src)];
                }
                std::copy(tmp.begin(), tmp.end(), w);
                cumShift[static_cast<size_t>(i)] += dg;
            }
            // The uniform shift can move previously-unshifted spikes.
            nShifted = 0;
            for (int64_t i = 0; i < N; ++i)
                if (cumShift[static_cast<size_t>(i)] != 0) ++nShifted;
        }

        log << "RMS recenter: centroid="
            << QString::number(rc.centroid, 'f', 2)
            << "  R=" << QString::number(rc.R, 'f', 3)
            << "  group shift=" << dg << " sample(s)";
        if (dg != 0) log << "  (now " << nShifted << " shifted)";
        log << ".\n";
        emitFlush();
    }
}

bool KlustersDoc::realignSpikes(int clusterId, QString& logOut, int& nShifted, int& nSwapped,
                                std::function<void(const QString&,bool)> liveLog,
                                const QString& args,
                                QVector<float>* meanBefore,
                                QVector<float>* meanAfter,
                                QString* backupBase)
{
    nShifted = 0;
    nSwapped = 0;
    logOut.clear();

    // Snapshot the cluster before any waveform data is modified.  During an
    // Align-All batch the centroid pass this triggers is served from a
    // batch-scoped cache (see beginRealignBatchLog), so per-cluster logging
    // stays cheap and the log streams cluster-by-cluster.
    logBefore(CurationLogger::ActionType::REALIGN, QList<int>{ clusterId });

    // Helper: emit live if callback provided, otherwise buffer in logOut for later.
    auto emitLine = [&](const QString& line, bool isError = false) {
        if (liveLog) {
            // Split on newlines so each physical line is a separate widget row.
            const QStringList parts = line.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& p : parts)
                liveLog(p, isError);
        } else {
            logOut += line;
        }
    };

    // Convenience: write a formatted line (same interface as QTextStream).
    // We build a local QString and flush via emitLine.
    // Use a lambda that returns a helper object supporting operator<<.
    // Simpler: just write directly with emitLine.
    // We keep a local QTextStream on a buffer and flush after each statement.
    QString _lineBuf;
    QTextStream log(&_lineBuf);
    // After each log << ... << "\n"; call emitFlush() to push it live.
    auto emitFlush = [&]() {
        if (!_lineBuf.isEmpty()) {
            emitLine(_lineBuf);
            _lineBuf.clear();
        }
    };

    // Per-phase wall-clock timing, opt-in via NS3_REALIGN_TIMING=1.  Emits one
    // line per cluster (setup / compute / writeback / total) so a batch can be
    // checked for which phase grows with cluster index.  No-op (one cheap
    // elapsed() pair) when the env var is unset.
    const bool _timing = qEnvironmentVariableIsSet("NS3_REALIGN_TIMING");
    QElapsedTimer _rtmr; _rtmr.start();
    qint64 _rtSetupMs = 0, _rtComputeMs = 0;
    // Writeback sub-phase markers (elapsed ms): boundary after sort-prep, after
    // file-open+verify, and after the write loop+close.  Pinpoints which part of
    // the (flat ~130ms) writeback is the fixed per-cluster cost.
    qint64 _rtWprepMs = 0, _rtWopenMs = 0, _rtWwriteMs = 0;
    // Gap since the previous cluster's realign finished — captures the
    // inter-cluster overhead (the batch worker's per-iteration bookkeeping and
    // clusterDone emit) that is invisible to the in-function timers.
    const long long _nowStartMs = (long long)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    const long long _gapMs =
        (realignPrevEndMs >= 0) ? (_nowStartMs - realignPrevEndMs) : -1;

    const Data& d         = data();
    const int   nChan     = d.nbOfChannels();
    const int   nSamp     = d.nbSamplesPerWaveform();
    const int   peakSamp  = d.peakSampleIndex(); // 1-based index from .par.N / parameter file
    const int   peakSamp0 = peakSamp - 1;        // 0-based waveform index
    const int   timeDim   = d.timeDimension();  // = nDimensions from .fet header
    const int   nFeatCols = timeDim - 1;        // feature columns, last col is ts

    // -----------------------------------------------------------------------
    // Parse configurable parameters from args string.
    // Supported: --threshold F  --iterations N  --maxshift N  --topchannels N
    //            --pca-refine  --recenter-rms  --rmin F
    // Defaults:  threshold=0.70  iterations=2  maxshift=peakSamp/2  rmin=0.40
    // -----------------------------------------------------------------------
    int   maxShift  = std::max(1, peakSamp0 / 2);
    float minScore  = 0.70f;
    int   nIter     = 2;
    int   nTopChan  = 0;   // 0 = use all channels (legacy behaviour)
    bool  pcaRefine = false;   // patch82: second-pass PCA-energy-max alignment
    bool  rmsRecenter = false; // post-alignment RMS circular group-recenter
    float rMin        = 0.4f;  // min mean-resultant-length to trust the centroid
    parseRealignArgs(args, maxShift, minScore, nIter, nTopChan,
                     pcaRefine, rmsRecenter, rMin);

    const QString dir   = documentDirectory();
    const QString base  = documentBaseName();
    const QString grpId = currentElectrodeGroupID();

    // Read from the pending file when it exists (i.e. after at least one
    // unsaved realignment pass), otherwise fall back to the original.
    // Writing always targets the pending files (see spkW/resW/fetW below).
    // If we always read from the originals, a second realignment pass would
    // load pre-aligned waveforms, xcorr them against the updated template,
    // find the same non-zero lags all over again, and write them back —
    // pushing late spikes further forward and early spikes further backward.
    auto pendingOrOrig = [](const QString& orig, const QString& pending) {
        return QFileInfo::exists(pending) ? pending : orig;
    };
    const QString spkPath = pendingOrOrig(origSpkPath, pendingSpkPath);
    const QString resPath = pendingOrOrig(origResPath, pendingResPath);
    const QString fetPath = pendingOrOrig(origFetPath, pendingFetPath);
    const QString cluPath = resolveFeature(dir + "/" + base, "clu", grpId,
                                           featureMethod(origFetPath));
    // Pipeline detection — decouple .spk storage format from .fet feature
    // space.  These are orthogonal signals in Pipeline C (raw .spk + stderiv
    // .fetD/.pcaD) and only coincide in Pipeline A (both raw) and Pipeline D
    // (both stderiv).  Conflating them corrupts .fetD under Pipeline C by
    // selecting the raw-space .pca basis and skipping the stderiv transform
    // during feature reprojection.
    //
    //   spkIsTransformed — .spk stores stderiv waveforms (Pipeline D).
    //                      When writing back, apply applyStderivTransform
    //                      to the raw extraction so .spk stays in stderiv
    //                      space.  For raw .spk (Pipeline A, C) write the
    //                      untransformed waveform.
    //   fetIsStderiv    — .fet features are in stderiv space, built on the
    //                      stderiv .pca basis.  Select .pca.stderiv.N and apply
    //                      the stderiv transform to the raw waveform before
    //                      projecting onto eigenvectors.
    // Chain-of-custody, but the two flags are INDEPENDENT (Pipeline-C): they are
    // read off the artifacts actually resolved, not from one shared method.
    //   spkIsTransformed — true only if the resolved .spk is itself a stderiv
    //     file; with a shared raw .spk it is false even for a stderiv sort.
    //   fetIsStderiv     — true if the resolved .fet is stderiv-space (the
    //     feature space the sort was done in), which selects the stderiv .pca
    //     basis and transforms the raw waveform before projecting.
    const QString fetMethod = featureMethod(origFetPath);
    const bool spkIsTransformed = (featureMethod(origSpkPath) == QLatin1String("stderiv"));
    const bool fetIsStderiv     = (fetMethod == QLatin1String("stderiv"));
    // Kept as alias for existing legacy-named uses in this function that
    // really want the feature-space flag, not the .spk storage flag.
    const bool isStderivRealign = fetIsStderiv;
    // Basis follows the feature space (.pca at fetMethod, with shared fallback).
    const QString pcaPath = resolveFeature(
        dir + "/" + base, "pca", grpId, fetMethod);
    const QString pcaDPath_ra = pcaPath;  // retained name for logging below

    for (const QString& p : {spkPath, resPath, fetPath}) {
        if (!QFileInfo::exists(p)) {
            log << "ERROR: missing file: " << p << "\n";
            return false;
        }
    }

    log << "Cluster " << clusterId
        << "  nChan=" << nChan << "  nSamp=" << nSamp
        << "  nFeatCols=" << nFeatCols
        << "  timeDim=" << timeDim
        << "  maxShift=+-" << maxShift
        << "  topChan=" << (nTopChan > 0 ? QString::number(nTopChan)
                                         : QStringLiteral("all"))
        << "  backend=" << XcorrDispatch::backendName() << "\n";
    emitFlush();

    // -----------------------------------------------------------------------
    // Cluster spike indices
    // -----------------------------------------------------------------------
    // Resolve the selected cluster from the ACTIVE layer: the parent fiber normally, or a
    // child atom when a child is shown (childScopeActive).  data() == clusteringData unless a
    // child is active, so the parent / Align-All paths are byte-for-byte unchanged; this lets
    // realign target a single microfiber whose spikes scatter relative to its parent's mean.
    SortableTable spkTable;
    if (!data().spikePositions(clusterId, spkTable)) {
        log << "ERROR: cluster " << clusterId << " not found.\n";
        return false;
    }
    const int64_t N = static_cast<int64_t>(spkTable.nbOfColumns());
    if (N == 0) { log << "Cluster is empty.\n"; emitFlush(); return true; }
    log << N << " spikes in cluster.\n";
    emitFlush();

    // gidx[i] = 0-based global file index of cluster spike i
    std::vector<int64_t> gidx(static_cast<size_t>(N));
    for (int64_t i = 0; i < N; ++i)
        gidx[static_cast<size_t>(i)] =
            static_cast<int64_t>(spkTable(1, static_cast<dataType>(i + 1))) - 1;

    // -----------------------------------------------------------------------
    // No backup needed: writes are deferred until saveDocument() is called.
    // -----------------------------------------------------------------------
    if (backupBase) *backupBase = QString();  // kept for API compatibility

    // -----------------------------------------------------------------------
    // Load PCA eigenvectors (per-channel basis)
    // -----------------------------------------------------------------------
    PcaBasis pca;   // type defined in klustersdoc.h (member-cached below)

    const QFileInfo _pcaFi(pcaPath);
    const bool      _pcaExists = _pcaFi.exists();
    const qint64    _pcaMtime  = _pcaExists
        ? _pcaFi.lastModified().toMSecsSinceEpoch() : -1;

    if (isStderivRealign && !QFileInfo::exists(pcaDPath_ra))
        log << "WARNING: stderiv PCA basis (.pca.stderiv/.pcaD) for group "
            << grpId << " not found — run ndm_pca_stderiv to generate it.\n";

    if (_pcaExists && realignPcaCache.valid()
        && realignPcaCachePath == pcaPath
        && realignPcaCacheMtime == _pcaMtime) {
        // Cache hit — reuse the basis loaded for an earlier cluster in the batch.
        pca = realignPcaCache;
        log << "PCA file: " << pcaPath << " [cached]\n";
        emitFlush();
    } else {
        log << "PCA file: " << pcaPath
            << (_pcaExists ? " [found]" : " [NOT FOUND]") << "\n";
        emitFlush();
        if (_pcaExists) {
            // PCAE loader (libneurosuite-core): validates magic/version, reads the
            // block-wise body, and populates pca.method/nInputChannels.  core
            // already rejects bad magic / version / short reads and guarantees
            // nCh,data2use,nComp > 0; klusters keeps its upper-bound sanity checks
            // (and the original log lines) against an absurd-but-valid header.
            if (!neurosuite::core::loadPca(pcaPath.toStdString(), pca)) {
                pca = PcaBasis{};
            } else if (pca.nCh > 64 || pca.data2use > 4096 || pca.nComp > 64 ||
                       pca.recShift < 0 || pca.recShift + pca.data2use > nSamp) {
                log << "WARNING: .pca header out of range (nCh="
                    << pca.nCh << " data2use=" << pca.data2use
                    << " nComp=" << pca.nComp << " recShift="
                    << pca.recShift << ") — ignoring .pca file\n";
                pca = PcaBasis{};
            }
        }
        if (pca.valid()) {
            // Loaded fresh from disk — cache for the rest of the batch so the
            // next cluster reuses it instead of re-reading the basis file.
            realignPcaCache      = pca;
            realignPcaCachePath  = pcaPath;
            realignPcaCacheMtime = _pcaMtime;
        }
    }

    const int nPcaFeats   = pca.valid() ? (pca.nCh * pca.nComp) : 0;
    const int nExtraFeats = (pca.valid() && nPcaFeats < nFeatCols)
                            ? (nFeatCols - nPcaFeats) : 0;
    if (!pca.valid())
        log << "WARNING: .pca unavailable — features will not be recomputed.\n";
    else
        log << "PCA: " << pca.nCh << "ch x " << pca.nComp
            << "comp  recShift=" << pca.recShift
            << (pca.centered ? " centered" : "")
            << "  extraFeats=" << nExtraFeats << "\n";
    emitFlush();

    // Spatial-derivative order for the stderiv reprojection below, taken from the
    // basis (PCAE Method) rather than hardcoded.  The three transform sites
    // (candidate refine, raw-source verify, disk-write) all used a fixed all-pairs
    // formula; the basis is the authority now (cf. the nudge flatten).  Fall back
    // to all-pairs for a non-temporal-diff / invalid basis, which keeps every
    // existing stderiv session byte-identical and never reaches the transform with
    // a raw basis anyway.
    const neurosuite::core::SdiffOrder stderivOrder =
        neurosuite::core::hasTemporalDiff(pca.method)
            ? neurosuite::core::spatialOrder(pca.method)
            : neurosuite::core::SdiffOrder::AllPairs;

    // -----------------------------------------------------------------------
    // Load cluster waveforms from binary .spk (int16, no header)
    // Layout: spike 0 samples, spike 1 samples, ...
    // Each spike: nChan * nSamp int16 values
    // -----------------------------------------------------------------------
    const size_t  spkElems      = static_cast<size_t>(nChan)
                                * static_cast<size_t>(nSamp);
    const int64_t bytesPerSpike = static_cast<int64_t>(spkElems) * 2;

    log << "Loading " << N << " waveforms ("
        << (N * bytesPerSpike / (1024*1024)) << " MB)...\n";
    emitFlush();

    std::vector<int16_t> wavBuf(static_cast<size_t>(N) * spkElems);
    {
        FILE* sf = fopen(spkPath.toLocal8Bit().constData(), "rb");
        if (!sf) {
            log << "ERROR: cannot open " << spkPath << "\n";
            return false;
        }
        for (int64_t i = 0; i < N; ++i) {
            const off_t off = static_cast<off_t>((gidx[static_cast<size_t>(i)] * bytesPerSpike));
            if (fseeko(sf, off, SEEK_SET) != 0) {
                fclose(sf);
                log << "ERROR: .spk seek failed at spike "
                    << gidx[static_cast<size_t>(i)] << "\n";
                return false;
            }
            int16_t* dst = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            if (fread(dst, 2, spkElems, sf) != spkElems) {
                fclose(sf);
                log << "ERROR: .spk short read at spike "
                    << gidx[static_cast<size_t>(i)] << "\n";
                return false;
            }
        }
        fclose(sf);
    }
    log << "Waveforms loaded.\n";
    emitFlush();

    // -----------------------------------------------------------------------
    // Transpose wavBuf from sample-major (.spk on-disk: [s * nChan + ch])
    // to channel-major (algorithm + XcorrDispatch: [ch * nSamp + s]).
    // -----------------------------------------------------------------------
    {
        std::vector<int16_t> cm(static_cast<size_t>(N) * spkElems);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* src = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            int16_t* dst = cm.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (int s = 0; s < nSamp; ++s)
                for (int ch = 0; ch < nChan; ++ch)
                    dst[ch * nSamp + s] = src[s * nChan + ch];
        }
        wavBuf = std::move(cm);
    }

    // Capture before-mean in channel-major order for the review dialog.
    if (meanBefore) {
        meanBefore->resize(static_cast<int>(spkElems));
        for (size_t e = 0; e < spkElems; ++e) {
            double acc = 0.0;
            for (int64_t i = 0; i < N; ++i)
                acc += wavBuf[static_cast<size_t>(i) * spkElems + e];
            (*meanBefore)[static_cast<int>(e)] = static_cast<float>(acc / N);
        }
    }

    // -----------------------------------------------------------------------
    // Read cluster timestamps and extra feature columns from binary files.
    //
    // Binary .res: N_total * int64_t, no header.
    //   spike p (0-based) → byte offset p * 8
    //
    // Binary .fet: int32_t nDimensions; then N_total * nDimensions * int64_t
    //   spike p (0-based), column c (0-based) →
    //     byte offset sizeof(int32_t) + (p * nDimensions + c) * 8
    //   columns 0..nFeatCols-1 = features; column nFeatCols = timestamp
    // -----------------------------------------------------------------------
    std::vector<int64_t> clusterTs(static_cast<size_t>(N));
    // Extra non-PCA feature columns per cluster spike [spike][col]
    std::vector<std::vector<int64_t>> extraFeats;
    if (nExtraFeats > 0)
        extraFeats.assign(static_cast<size_t>(N),
                          std::vector<int64_t>(static_cast<size_t>(nExtraFeats), 0));

    {
        FILE* rf = fopen(resPath.toLocal8Bit().constData(), "rb");
        if (!rf) {
            log << "ERROR: cannot open " << resPath << "\n";
            return false;
        }
        FILE* ff = fopen(fetPath.toLocal8Bit().constData(), "rb");
        if (!ff) {
            fclose(rf);
            log << "ERROR: cannot open " << fetPath << "\n";
            return false;
        }

        // Validate .fet nDimensions header
        int32_t fetNDim = 0;
        if (fread(&fetNDim, sizeof(int32_t), 1, ff) != 1) {
            fclose(rf); fclose(ff);
            log << "ERROR: cannot read .fet header\n";
            return false;
        }
        if (fetNDim != static_cast<int32_t>(timeDim)) {
            log << "WARNING: .fet nDimensions=" << fetNDim
                << " but timeDim=" << timeDim
                << " — proceeding with file value\n";
        }
        const int32_t fileDim = (fetNDim > 0) ? fetNDim : static_cast<int32_t>(timeDim);

        for (int64_t i = 0; i < N; ++i) {
            const int64_t p = gidx[static_cast<size_t>(i)];

            // Read timestamp from .res at byte offset p*8
            if (fseeko(rf, static_cast<off_t>((p * static_cast<int64_t>(sizeof(int64_t)))), SEEK_SET) != 0 ||
                fread(&clusterTs[static_cast<size_t>(i)],
                      sizeof(int64_t), 1, rf) != 1) {
                fclose(rf); fclose(ff);
                log << "ERROR: cannot read .res at spike " << p << "\n";
                return false;
            }

            // Read extra feature columns from .fet row p
            for (int k = 0; k < nExtraFeats; ++k) {
                const int col = nPcaFeats + k;
                const off_t off = static_cast<off_t>(sizeof(int32_t))
                                + static_cast<off_t>((p * static_cast<int64_t>(fileDim) + col))
                                * static_cast<off_t>(sizeof(int64_t));
                if (fseeko(ff, off, SEEK_SET) != 0 ||
                    fread(&extraFeats[static_cast<size_t>(i)][static_cast<size_t>(k)],
                          sizeof(int64_t), 1, ff) != 1) {
                    log << "WARNING: cannot read extra feat col=" << col
                        << " spike=" << p << "\n";
                }
            }
        }
        fclose(rf);
        fclose(ff);
    }

    // -----------------------------------------------------------------------
    // Iterative xcorr alignment on waveform buffer
    // -----------------------------------------------------------------------
    std::vector<int>   cumShift(static_cast<size_t>(N), 0);
    std::vector<float> bestScore(static_cast<size_t>(N), 0.0f);

    for (int iter = 0; iter < nIter; ++iter) {
        // Build mean template from current wavBuf
        std::vector<int64_t> acc(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e)
                acc[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> tmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e)
            tmpl[e] = static_cast<int16_t>(acc[e] / N);

        // Pre-align template: shift so its peak lands at peakSamp0.
        // Without this the xcorr moves every spike away from the true peak.
        //
        // Top-K channel selection: when nTopChan > 0 and < nChan, select the
        // nTopChan channels with the largest absolute amplitude in the
        // template.  Energy for peak detection is computed only on those.
        // The same mask is then applied to the template and all waveform
        // buffers (non-top channels zeroed) so the xcorr ignores them too.
        // This excludes collision / residual activity on non-primary
        // channels from influencing alignment.
        std::vector<uint8_t> chanUse(static_cast<size_t>(nChan), 1);
        if (nTopChan > 0 && nTopChan < nChan) {
            std::vector<std::pair<int64_t,int>> chanAmp(static_cast<size_t>(nChan));
            for (int ch = 0; ch < nChan; ++ch) {
                int64_t pk = 0;
                for (int t = 0; t < nSamp; ++t) {
                    const int64_t v = std::abs(static_cast<int64_t>(
                        tmpl[static_cast<size_t>(ch * nSamp + t)]));
                    if (v > pk) pk = v;
                }
                chanAmp[static_cast<size_t>(ch)] = {pk, ch};
            }
            std::partial_sort(chanAmp.begin(),
                              chanAmp.begin() + nTopChan,
                              chanAmp.end(),
                              [](const auto& a, const auto& b){
                                  return a.first > b.first;
                              });
            std::fill(chanUse.begin(), chanUse.end(), 0);
            for (int k = 0; k < nTopChan; ++k)
                chanUse[static_cast<size_t>(chanAmp[static_cast<size_t>(k)].second)] = 1;
        }

        {
            int    tmplPeak = 0;
            double bestAmp  = -1.0;
            for (int t = 0; t < nSamp; ++t) {
                double amp = 0.0;
                for (int ch = 0; ch < nChan; ++ch)
                    if (chanUse[static_cast<size_t>(ch)])
                        amp += std::abs(static_cast<double>(
                                   tmpl[static_cast<size_t>(ch * nSamp + t)]));
                if (amp > bestAmp) { bestAmp = amp; tmplPeak = t; }
            }
            const int tShift = peakSamp0 - tmplPeak;
            if (tShift != 0) {
                // To move the peak from tmplPeak to peakSamp0: src = (t - tShift + N) % N
                std::vector<int16_t> shifted(spkElems);
                for (int ch = 0; ch < nChan; ++ch)
                    for (int t = 0; t < nSamp; ++t) {
                        const int src = (t - tShift + nSamp) % nSamp;
                        shifted[static_cast<size_t>(ch * nSamp + t)] =
                            tmpl[static_cast<size_t>(ch * nSamp + src)];
                    }
                tmpl = std::move(shifted);
            }
        }

        // Apply top-K mask to a SCRATCH copy for the xcorr call only.
        // Mutating wavBuf would destroy data needed for the next iteration's
        // template build, the final xcorr pass, and the post-realign mean.
        // Same reasoning for tmpl — keep the original, mask a copy.
        //
        // Layout is channel-major [ch * nSamp + s]; zeroing ranges of
        // contiguous samples for masked-out channels gives the xcorr
        // dispatcher zeros on both sides of the correlation for those
        // channels, so they contribute nothing at any lag.
        const int16_t* xcorrWav = wavBuf.data();
        const int16_t* xcorrTmpl = tmpl.data();
        std::vector<int16_t> wavMasked;
        std::vector<int16_t> tmplMasked;
        if (nTopChan > 0 && nTopChan < nChan) {
            tmplMasked = tmpl;   // copy
            wavMasked  = wavBuf; // copy (N × spkElems int16 — cheap vs. xcorr)
            for (int ch = 0; ch < nChan; ++ch) {
                if (chanUse[static_cast<size_t>(ch)]) continue;
                std::fill(tmplMasked.begin() + ch * nSamp,
                          tmplMasked.begin() + (ch + 1) * nSamp,
                          int16_t(0));
                for (int64_t i = 0; i < N; ++i) {
                    int16_t* row = wavMasked.data()
                                 + static_cast<ptrdiff_t>(i)
                                 * static_cast<ptrdiff_t>(spkElems);
                    std::fill(row + ch * nSamp,
                              row + (ch + 1) * nSamp,
                              int16_t(0));
                }
            }
            xcorrWav  = wavMasked.data();
            xcorrTmpl = tmplMasked.data();
        }

        std::vector<int>   sh(static_cast<size_t>(N), 0);
        std::vector<float> sc(static_cast<size_t>(N), 0.0f);
        int rc = XcorrDispatch::compute(
            xcorrWav, xcorrTmpl,
            static_cast<int>(N), nChan, nSamp,
            maxShift, minScore, sh.data(), sc.data());
        if (rc != 0) {
            log << "ERROR: XcorrDispatch rc=" << rc << "\n";
            return false;
        }

        int changed = 0;
        for (int64_t i = 0; i < N; ++i) {
            const int s = sh[static_cast<size_t>(i)];
            if (s == 0) continue;
            int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            // Circular shift: newSpike[t] = oldSpike[(t + shift) % N].
            // Derivation: score(lag) = Σ tmpl[s]·spike[(s+lag)%N] peaks at
            // lag = bestLag when spike[(s+bestLag)%N] ≈ tmpl[s], meaning the
            // spike peak is late by bestLag.  Rolling forward by bestLag
            // brings the peak back to the template position.  The timestamp
            // must move in the opposite direction (see newTs below).
            std::vector<int16_t> tmp(spkElems);
            for (int t = 0; t < nSamp; ++t) {
                const int src = (t + s + nSamp) % nSamp;
                for (int ch = 0; ch < nChan; ++ch)
                    tmp[static_cast<size_t>(ch * nSamp + t)] =
                        w[static_cast<size_t>(ch * nSamp + src)];
            }
            std::copy(tmp.begin(), tmp.end(), w);
            cumShift[static_cast<size_t>(i)] += s;
            bestScore[static_cast<size_t>(i)] = sc[static_cast<size_t>(i)];
            ++changed;
        }
        log << "  iter " << (iter+1) << ": " << changed << " shifted\n";
        if (changed == 0) break;
    }

    for (int64_t i = 0; i < N; ++i)
        if (cumShift[static_cast<size_t>(i)] != 0) ++nShifted;
    log << nShifted << " spike(s) shifted.\n";
    emitFlush();

    // -----------------------------------------------------------------------
    // patch82 — PCA-projection-maximizing refine pass (opt-in via --pca-refine)
    //
    // After the mean-template xcorr alignment converges, refine each
    // spike's position by trying candidate shifts s in [-maxShift, +maxShift]
    // and picking the one that MAXIMIZES the spike's PCA-projection energy:
    //
    //     energy(s) = sum_ch sum_k <basis_ch_k, spike(s) - mean_ch>²
    //
    // Intuition: a well-aligned spike concentrates its variance in the
    // directions of greatest cluster variance (the kept PCA components);
    // a misaligned spike spreads variance across all directions,
    // suppressing this sum.  energy(s) peaks where the spike most
    // strongly looks like a member of the cluster as measured by the
    // canonical PCA basis.
    //
    // Reads each candidate window FRESH from .fil (no circular wrap
    // artifacts that would skew the PCA scores near the window edges).
    // Uses a local FILE* so the existing .fil open at line ~4060 stays
    // untouched.
    //
    // Update propagation: when bestS != 0,
    //   - cumShift[i] += bestS (so the existing sort + disk-write code
    //     downstream uses the refined position)
    //   - wavBuf[i] is overwritten with the freshly-extracted window
    //     at the refined position, so the score statistics block below
    //     (computing finalTmpl + scores) sees post-refine waveforms
    //     instead of stale circular-shifted ones.
    //
    // Falls back silently to no-op when:
    //   - .pca eigenvector basis didn't load (pca.valid() == false)
    //   - .fil can't be opened
    //   - the cluster's spikes' windows would run off the end of .fil
    //     at any candidate shift (handled per-spike)
    if (pcaRefine && pca.valid()) {
        log << "PCA-projection refine pass: search ±" << maxShift
            << " samples, "
            << pca.nCh << " channels × " << pca.nComp << " components\n";
        emitFlush();

        const int totalNbChanP82 = clusteringData->getTotalNbChannels();
        const QList<int>& groupChannelsP82 = clusteringData->getCurrentChannels();
        QString filPathP82;
        {
            const QString baseSpk = spkPath.left(spkPath.lastIndexOf(QLatin1Char('.')));
            const QString noExt = baseSpk.left(baseSpk.lastIndexOf(QLatin1Char('.')));
            if (QFileInfo::exists(noExt + QStringLiteral(".fil")))
                filPathP82 = noExt + QStringLiteral(".fil");
            else
                filPathP82 = noExt + QStringLiteral(".dat");
        }
        FILE* filFp82 = fopen(filPathP82.toLocal8Bit().constData(), "rb");
        if (!filFp82) {
            log << "  WARNING: cannot open " << filPathP82
                << " for PCA-refine — skipping.\n";
            emitFlush();
        } else {
            const int64_t totalSamplesP82 =
                static_cast<int64_t>(QFileInfo(filPathP82).size())
              / (static_cast<int64_t>(sizeof(short)) * totalNbChanP82);

            // Scratch buffers reused across spikes.
            std::vector<int16_t> rawFrame(
                static_cast<size_t>(nSamp * totalNbChanP82));
            std::vector<int16_t> candCM(static_cast<size_t>(spkElems));

            // Use the SMALLER of pca.nCh and nChan in case they disagree
            // (stderiv pipelines drop one channel; pca.nCh = nChan - 1).
            const int chForPca = std::min(pca.nCh, nChan);
            const int kComp    = pca.nComp;
            const int d2u      = pca.data2use;
            const int rShift   = pca.recShift;
            const bool centered = pca.centered;
            const bool useStder = isStderivRealign;

            int nRefined = 0;
            int nClamped = 0;   // spikes where the search bumped the .fil edge

            // ── GPU PCA-refine fast path ─────────────────────────────────
            // For large clusters this replaces the per-spike per-candidate
            // CPU loop below with: one wide .fil read per spike (covers all
            // candidates) + one GPU kernel that evaluates every (spike,
            // candidate) PCA energy and returns the argmax shift per spike.
            //
            // Returns true when the GPU path completed; otherwise we fall
            // through to the CPU loop unchanged.
            _rtSetupMs = _rtmr.elapsed();
            bool gpuPathRan = false;
            do {
                if (!PcaRefineGpu::hasGpu()) break;
                if (N < PcaRefineGpu::gpuThreshold()) break;
                if (nChan > 256) break;        // safety — kernel sums across nChan in one block
                const int M       = 2 * maxShift + 1;
                // Each candidate c in [0,M) feeds the kernel rShift+d2u raw
                // samples at channel-row offset [c, c+rShift+d2u); the widest
                // candidate (c = 2*maxShift) therefore needs
                // 2*maxShift + rShift + d2u samples per channel row.  The old
                // nSamp+2*maxShift only covers that when nSamp >= rShift+d2u.
                // When the stderiv pre-roll plus the PCA window exceed nSamp
                // (e.g. d2u == nSamp with rShift >= 1) the top candidates read
                // past their channel row — into the next channel for interior
                // spikes, and past the d_raw allocation for the last spike,
                // which raises an illegal memory access that poisons the CUDA
                // context for the rest of the session (every later GPU call,
                // including the grouping-assistant malloc, then fails sticky).
                // Size the buffer for the widest read so the absolute samples
                // each candidate uses are unchanged but never truncated.
                const int wideLen = 2 * maxShift + std::max(nSamp, rShift + d2u);

                // Bound the working buffer at ~512 MB to avoid surprising
                // the GPU on very large clusters.  Above this the CPU loop
                // is still fast enough that splitting the batch isn't
                // worth the code.
                const long long rawBytes =
                    static_cast<long long>(N) * nChan * wideLen * sizeof(int16_t);
                if (rawBytes > static_cast<long long>(512) * 1024 * 1024) break;

                std::vector<int16_t> rawWindows(
                    static_cast<size_t>(N) * nChan * wideLen, 0);
                std::vector<int> validRow(static_cast<size_t>(N), 1);

                // ── Phase A — read one wide window per spike ────────────
                // Saves ~M× the .fil syscalls vs the CPU per-candidate loop.
                // A spike whose widest candidate runs off the .fil edge
                // gets validRow[i]=0 — those drop into the CPU path so
                // edge-clamp accounting (nClamped) matches the legacy
                // behaviour exactly.
                int prepared = 0;
                for (int64_t i = 0; i < N; ++i) {
                    const int   curr = cumShift[static_cast<size_t>(i)];
                    const int64_t baseTs = clusterTs[static_cast<size_t>(i)] + curr;
                    const int64_t leftStart = baseTs - maxShift
                                            - static_cast<int64_t>(peakSamp0);
                    if (leftStart < 0 ||
                        leftStart + wideLen > totalSamplesP82) {
                        validRow[static_cast<size_t>(i)] = 0;
                        continue;
                    }
                    const off_t rawOff = static_cast<off_t>(leftStart)
                                       * static_cast<off_t>(totalNbChanP82)
                                       * static_cast<off_t>(sizeof(short));
                    if (fseeko(filFp82, rawOff, SEEK_SET) != 0) {
                        validRow[static_cast<size_t>(i)] = 0;
                        continue;
                    }
                    // Read sample-major into rawFrame (reused), then
                    // gather to channel-major into rawWindows.
                    std::vector<int16_t> wide(
                        static_cast<size_t>(wideLen) * totalNbChanP82);
                    if (fread(wide.data(), sizeof(short),
                              wide.size(), filFp82) != wide.size()) {
                        validRow[static_cast<size_t>(i)] = 0;
                        continue;
                    }
                    int16_t* spkBase = rawWindows.data()
                                     + static_cast<size_t>(i) * nChan * wideLen;
                    for (int t = 0; t < wideLen; ++t)
                        for (int ci = 0; ci < nChan; ++ci)
                            spkBase[static_cast<size_t>(ci) * wideLen + t] =
                                wide[static_cast<size_t>(t) * totalNbChanP82
                                   + groupChannelsP82[ci]];
                    ++prepared;
                }

                if (prepared < PcaRefineGpu::gpuThreshold()) break;

                // ── Phase B — pack PCA basis into flat float arrays ─────
                std::vector<float> evecFlat(
                    static_cast<size_t>(chForPca) * kComp * d2u);
                std::vector<float> meansFlat(
                    centered ? static_cast<size_t>(chForPca) * d2u : 0);
                for (int ch = 0; ch < chForPca; ++ch) {
                    const auto& ev = pca.evec[static_cast<size_t>(ch)];
                    for (int k = 0; k < kComp; ++k)
                        for (int u = 0; u < d2u; ++u)
                            evecFlat[static_cast<size_t>(ch) * kComp * d2u
                                   + static_cast<size_t>(k) * d2u + u] =
                                static_cast<float>(ev[static_cast<size_t>(k) * d2u + u]);
                    if (centered) {
                        const auto& mu = pca.means[static_cast<size_t>(ch)];
                        for (int u = 0; u < d2u; ++u)
                            meansFlat[static_cast<size_t>(ch) * d2u + u] =
                                static_cast<float>(mu[static_cast<size_t>(u)]);
                    }
                }

                // ── Phase C — kernel launch + best-shift collection ─────
                std::vector<int> bestShiftGpu(static_cast<size_t>(N), 0);
                const int rc = PcaRefineGpu::refine(
                    static_cast<int>(N), M, wideLen, nSamp, nChan, chForPca,
                    kComp, d2u, rShift, maxShift,
                    centered ? 1 : 0, useStder ? 1 : 0,
                    rawWindows.data(),
                    evecFlat.data(),
                    centered ? meansFlat.data() : nullptr,
                    bestShiftGpu.data());
                if (rc != 0) {
                    log << "  PCA-refine GPU dispatch returned "
                        << rc << " — falling back to CPU.\n";
                    emitFlush();
                    break;
                }

                // ── Phase D — apply shifts + refresh wavBuf per spike ────
                // For spikes where validRow[i] == 0 (edge-clamped), we
                // mark them as clamped and leave cumShift untouched —
                // matches the CPU path's "every candidate ran off the
                // .fil edge" accounting.
                std::vector<int16_t> rawFrame(
                    static_cast<size_t>(nSamp) * totalNbChanP82);
                for (int64_t i = 0; i < N; ++i) {
                    if (!validRow[static_cast<size_t>(i)]) { ++nClamped; continue; }
                    const int bestS = bestShiftGpu[static_cast<size_t>(i)];
                    if (bestS == 0) continue;

                    cumShift[static_cast<size_t>(i)] = cumShift[static_cast<size_t>(i)] + bestS;
                    ++nRefined;

                    // Refresh wavBuf[i] from .fil at the chosen position
                    // (same logic as the CPU branch below).
                    const int64_t bestStart =
                        clusterTs[static_cast<size_t>(i)] + cumShift[static_cast<size_t>(i)]
                      - static_cast<int64_t>(peakSamp0);
                    const off_t bestOff = static_cast<off_t>(bestStart)
                                        * static_cast<off_t>(totalNbChanP82)
                                        * static_cast<off_t>(sizeof(short));
                    if (fseeko(filFp82, bestOff, SEEK_SET) != 0) continue;
                    if (fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filFp82)
                            != rawFrame.size()) continue;
                    int16_t* wTgt = wavBuf.data()
                        + (ptrdiff_t)i * (ptrdiff_t)spkElems;
                    for (int t = 0; t < nSamp; ++t)
                        for (int ci = 0; ci < nChan; ++ci)
                            wTgt[static_cast<size_t>(ci) * nSamp + t] =
                                rawFrame[static_cast<size_t>(t) * totalNbChanP82
                                       + groupChannelsP82[ci]];
                    if (useStder)
                        neurosuite::core::applyStderivTransform(
                            stderivOrder, wTgt, nChan, nSamp, wTgt);
                }
                gpuPathRan = true;
            } while (false);

            // ── CPU per-spike refine (GPU path not taken) ────────────────────
            // Each spike's best-shift search is independent: it reads from
            // .fil and writes only its own cumShift[i] and wavBuf[i] slot.  The
            // only shared mutable state is the .fil handle (its file position)
            // and the rawFrame/candCM scratch buffers, so we parallelise across
            // spikes — each thread gets its own handle + buffers.  Falls back to
            // a serial loop when OpenMP is unavailable or per-thread handles
            // cannot be opened, giving identical results either way.
            if (!gpuPathRan) {
                enum class RefineResult { None, Refined, Clamped };
                auto refineSpike =
                    [&](int64_t i, FILE* filFp82,
                        std::vector<int16_t>& rawFrame,
                        std::vector<int16_t>& candCM) -> RefineResult {
                const int   curr = cumShift[static_cast<size_t>(i)];
                const int64_t baseTs = clusterTs[static_cast<size_t>(i)] + curr;
                int bestS = 0;
                double bestEnergy = -1.0;
                int validCandidates = 0;
                for (int s = -maxShift; s <= maxShift; ++s) {
                    const int64_t candStart = baseTs + s
                                            - static_cast<int64_t>(peakSamp0);
                    if (candStart < 0 ||
                        candStart + nSamp > totalSamplesP82) continue;

                    const off_t rawOff = static_cast<off_t>(candStart)
                                       * static_cast<off_t>(totalNbChanP82)
                                       * static_cast<off_t>(sizeof(short));
                    if (fseeko(filFp82, rawOff, SEEK_SET) != 0) continue;
                    if (fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filFp82)
                            != rawFrame.size()) continue;

                    // Subset to group channels (sample-major in rawFrame
                    // → channel-major for PCA projection).
                    for (int t = 0; t < nSamp; ++t)
                        for (int ci = 0; ci < nChan; ++ci)
                            candCM[static_cast<size_t>(ci * nSamp + t)] =
                                rawFrame[static_cast<size_t>(
                                    t * totalNbChanP82 + groupChannelsP82[ci])];

                    // Apply stderiv transform in-place if pipeline needs it.
                    // Spatial order from the basis; matches the disk-write block.
                    if (useStder)
                        neurosuite::core::applyStderivTransform(
                            stderivOrder, candCM.data(), nChan, nSamp, candCM.data());

                    // PCA projection energy — shared primitive in
                    // libneurosuite-core (same math the GPU kernel mirrors and
                    // the plugin's Stage 2 uses).  candCM is channel-major
                    // [ch*nSamp+t]; core derives chForPca/kComp/d2u/rShift/
                    // centered from pca.  The core fn additionally bounds-guards
                    // sIdx, a no-op here because the loader rejects any basis with
                    // recShift + data2use > nSamp.
                    const double energy = neurosuite::core::pcaProjectionEnergy(
                        candCM.data(), nChan, nSamp, pca);
                    ++validCandidates;

                    if (energy > bestEnergy) {
                        bestEnergy = energy;
                        bestS = s;
                    }
                }

                if (validCandidates == 0) {
                    // Every candidate ran off the .fil edge — nothing we
                    // can do, leave cumShift unchanged.
                    return RefineResult::Clamped;
                }
                if (bestS == 0) return RefineResult::None;   // already optimal

                cumShift[static_cast<size_t>(i)] = curr + bestS;

                // Refresh wavBuf[i] from the .fil at the chosen position
                // so the downstream score-stats block uses post-refine
                // content.  We need to re-extract once more (we don't
                // cache the per-candidate candCM, only the winning bestS).
                {
                    const int64_t bestStart =
                        clusterTs[static_cast<size_t>(i)]
                      + cumShift[static_cast<size_t>(i)]
                      - static_cast<int64_t>(peakSamp0);
                    const off_t bestOff = static_cast<off_t>(bestStart)
                                        * static_cast<off_t>(totalNbChanP82)
                                        * static_cast<off_t>(sizeof(short));
                    if (fseeko(filFp82, bestOff, SEEK_SET) == 0 &&
                        fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filFp82) == rawFrame.size()) {
                        int16_t* wTgt = wavBuf.data()
                            + static_cast<ptrdiff_t>(i)
                            * static_cast<ptrdiff_t>(spkElems);
                        for (int t = 0; t < nSamp; ++t)
                            for (int ci = 0; ci < nChan; ++ci)
                                wTgt[static_cast<size_t>(ci * nSamp + t)] =
                                    rawFrame[static_cast<size_t>(
                                        t * totalNbChanP82
                                      + groupChannelsP82[ci])];
                        if (useStder)
                            neurosuite::core::applyStderivTransform(
                                stderivOrder, wTgt, nChan, nSamp, wTgt);
                    }
                }
                return RefineResult::Refined;
                };  // refineSpike

                // Dispatch: parallel across spikes when OpenMP is available and
                // the cluster is large enough to amortise thread setup; each
                // thread opens its own .fil handle and owns its scratch buffers.
                bool ranParallel = false;
#ifdef _OPENMP
                const int nThr = omp_get_max_threads();
                if (nThr > 1 && N >= 64) {
                    std::vector<FILE*> fh(static_cast<size_t>(nThr), nullptr);
                    bool allOpen = true;
                    for (int t = 0; t < nThr; ++t) {
                        fh[static_cast<size_t>(t)] =
                            fopen(filPathP82.toLocal8Bit().constData(), "rb");
                        if (!fh[static_cast<size_t>(t)]) { allOpen = false; break; }
                    }
                    if (allOpen) {
                        #pragma omp parallel reduction(+:nRefined, nClamped)
                        {
                            const int tid = omp_get_thread_num();
                            FILE* filT = fh[static_cast<size_t>(tid)];
                            std::vector<int16_t> rawT(rawFrame.size());
                            std::vector<int16_t> candT(candCM.size());
                            #pragma omp for schedule(dynamic, 16)
                            for (int64_t i = 0; i < N; ++i) {
                                const RefineResult r =
                                    refineSpike(i, filT, rawT, candT);
                                if (r == RefineResult::Refined)      ++nRefined;
                                else if (r == RefineResult::Clamped) ++nClamped;
                            }
                        }
                        ranParallel = true;
                    }
                    for (FILE* h : fh) if (h) fclose(h);
                }
#endif
                if (!ranParallel) {
                    for (int64_t i = 0; i < N; ++i) {
                        const RefineResult r =
                            refineSpike(i, filFp82, rawFrame, candCM);
                        if (r == RefineResult::Refined)      ++nRefined;
                        else if (r == RefineResult::Clamped) ++nClamped;
                    }
                }
            }  // if (!gpuPathRan)
            fclose(filFp82);
            _rtComputeMs = _rtmr.elapsed();

            log << "  PCA-refine: " << nRefined
                << " spike(s) refined";
            if (nClamped > 0)
                log << " (" << nClamped << " skipped — window off edge)";
            log << ".\n";

            // Recompute nShifted from the now-refined cumShift.
            nShifted = 0;
            for (int64_t i = 0; i < N; ++i)
                if (cumShift[static_cast<size_t>(i)] != 0) ++nShifted;
            log << "  total shifted after refine: " << nShifted << "\n";
            emitFlush();
        }
    } else if (pcaRefine) {
        log << "PCA-projection refine requested but PCA basis not loaded "
               "— skipping.\n";
        emitFlush();
    }

    // -----------------------------------------------------------------------
    // RMS circular group-recenter (opt-in via --recenter-rms)
    //
    // After per-spike alignment, shift the WHOLE cluster by a single offset so
    // its energy envelope sits at peakSamp0.  The anchor is the circular
    // weighted mean of the per-sample RMS² profile (energy across the group,
    // summed over all channels).  The sample axis of the .spk window is
    // periodic, so a circular mean is the correct centroid; it is also exactly
    // invariant to the RMS noise floor — Σ exp(i·2πs/N) over a full period is
    // the sum of the Nth roots of unity, i.e. zero — so a uniform pedestal in
    // the weights cancels with no baseline subtraction.  Weighting by energy
    // (RMS², not RMS) concentrates the mass on the peak.
    //
    // The mean resultant length R (∈[0,1]) guards the degenerate case: when the
    // energy splits into two lobes ~N/2 apart the resultant collapses and the
    // centroid is meaningless.  A clean (bi/tri)phasic spike is a single lobe,
    // so R stays high; if R < rMin we skip the recenter and keep the per-spike
    // alignment rather than throw the cluster to an arbitrary offset.
    //
    // The shift uses the same convention as the per-spike roll
    // (newSpike[t] = oldSpike[(t+δ)%N]; cumShift += δ), so the score stats,
    // mean-after and fresh .fil re-extraction below all pick it up.  Note the
    // circular roll of wavBuf only drives the diagnostics — the .spk content is
    // re-extracted linearly from .fil at clusterTs+cumShift, so the recenter is
    // measured circularly but realised as a clean unwrapped window.
    if (rmsRecenter)
        realignRmsRecenter(wavBuf, cumShift, nShifted, N, nChan, nSamp,
                           spkElems, peakSamp0, rMin, log, emitFlush);

    // -----------------------------------------------------------------------
    // Score / shift statistics
    // -----------------------------------------------------------------------
    {
        // Scores: bestScore[i] > 0 only for shifted spikes.
        // Collect all per-spike final scores (run one more xcorr pass
        // on the final template to get scores for unshifted spikes too).
        std::vector<int64_t> acc2(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e)
                acc2[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> finalTmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e)
            finalTmpl[e] = static_cast<int16_t>(acc2[e] / N);

        std::vector<int>   dummySh(static_cast<size_t>(N), 0);
        std::vector<float> allScores(static_cast<size_t>(N), 0.0f);

        // Apply top-K mask to scratch copies so the final score reflects the
        // same channel subset that drove the alignment (consistent with the
        // iteration-loop xcorr).  Non-destructive — wavBuf and finalTmpl are
        // still needed below for the post-realign mean accumulation.
        const int16_t* scoreWav  = wavBuf.data();
        const int16_t* scoreTmpl = finalTmpl.data();
        std::vector<int16_t> finalWavMasked;
        std::vector<int16_t> finalTmplMasked;
        if (nTopChan > 0 && nTopChan < nChan) {
            // Recompute chanUse from the final template (cluster mean may
            // have shifted channel dominance across iterations).
            std::vector<std::pair<int64_t,int>> chanAmp(
                static_cast<size_t>(nChan));
            for (int ch = 0; ch < nChan; ++ch) {
                int64_t pk = 0;
                for (int t = 0; t < nSamp; ++t) {
                    const int64_t v = std::abs(static_cast<int64_t>(
                        finalTmpl[static_cast<size_t>(ch * nSamp + t)]));
                    if (v > pk) pk = v;
                }
                chanAmp[static_cast<size_t>(ch)] = {pk, ch};
            }
            std::partial_sort(chanAmp.begin(),
                              chanAmp.begin() + nTopChan,
                              chanAmp.end(),
                              [](const auto& a, const auto& b){
                                  return a.first > b.first;
                              });
            std::vector<uint8_t> chanUseFinal(
                static_cast<size_t>(nChan), 0);
            for (int k = 0; k < nTopChan; ++k)
                chanUseFinal[static_cast<size_t>(
                    chanAmp[static_cast<size_t>(k)].second)] = 1;

            finalTmplMasked = finalTmpl;
            finalWavMasked  = wavBuf;
            for (int ch = 0; ch < nChan; ++ch) {
                if (chanUseFinal[static_cast<size_t>(ch)]) continue;
                std::fill(finalTmplMasked.begin() + ch * nSamp,
                          finalTmplMasked.begin() + (ch + 1) * nSamp,
                          int16_t(0));
                for (int64_t i = 0; i < N; ++i) {
                    int16_t* row = finalWavMasked.data()
                                 + static_cast<ptrdiff_t>(i)
                                 * static_cast<ptrdiff_t>(spkElems);
                    std::fill(row + ch * nSamp,
                              row + (ch + 1) * nSamp,
                              int16_t(0));
                }
            }
            scoreWav  = finalWavMasked.data();
            scoreTmpl = finalTmplMasked.data();
        }

        // Use maxShift=0 so no shifting occurs — we just want the scores.
        XcorrDispatch::compute(
            scoreWav, scoreTmpl,
            static_cast<int>(N), nChan, nSamp,
            0, 0.005f, dummySh.data(), allScores.data());

        float scoreMin =  2.0f, scoreMax = -2.0f, scoreSum = 0.0f;
        int   nBelow   = 0;
        int   shiftMin = INT_MAX, shiftMax = INT_MIN;
        float shiftAbsSum = 0.0f;

        for (int64_t i = 0; i < N; ++i) {
            const float s  = allScores[static_cast<size_t>(i)];
            const int   sh = cumShift[static_cast<size_t>(i)];
            if (s < scoreMin) scoreMin = s;
            if (s > scoreMax) scoreMax = s;
            scoreSum += s;
            if (s < minScore) ++nBelow;
            if (sh < shiftMin) shiftMin = sh;
            if (sh > shiftMax) shiftMax = sh;
            shiftAbsSum += static_cast<float>(sh < 0 ? -sh : sh);
        }
        const float scoreMean = scoreSum / static_cast<float>(N);
        const float shiftMean = shiftAbsSum / static_cast<float>(N);

        log << "\n--- Alignment statistics (" << N << " spikes) ---\n";
        log << "  xcorr score  min=" << QString::number(static_cast<double>(scoreMin), 'f', 3)
            << "  max="              << QString::number(static_cast<double>(scoreMax), 'f', 3)
            << "  mean="             << QString::number(static_cast<double>(scoreMean), 'f', 3)
            << "  (threshold="       << QString::number(static_cast<double>(minScore),  'f', 2) << ")\n";
        log << "  below threshold: " << nBelow << " / " << N
            << " (" << QString::number(100.0 * nBelow / N, 'f', 1) << "%) — left unshifted\n";
        if (nShifted > 0) {
            log << "  shift (samples) min=" << shiftMin
                << "  max=" << shiftMax
                << "  mean-abs=" << QString::number(static_cast<double>(shiftMean), 'f', 2) << "\n";
        }
        log << "\n";
        emitFlush();
    }

    // -----------------------------------------------------------------------
    // Compute new timestamps and sort cluster spikes by new timestamp
    // -----------------------------------------------------------------------
    // Two timestamp values are computed per spike:
    //
    //   extTs   — the realigned window position: extTs = oldTs + cumShift.
    //             The .spk waveform is re-extracted from .fil/.dat at this
    //             position (window [extTs - peakSamp0 ...]), which captures the
    //             content that lands at peakSamp0 after the shift and avoids the
    //             wrap-around artifact a circular shift produces at the edges.
    //             extTs is ALSO what gets written to .res, to the .fet timestamp
    //             column, and to the in-memory timestamp — so all four stay in
    //             lock-step and the pipeline-wide invariant
    //                 .spk[i] peak  ≡  .fil at .res[i]
    //             holds.  nudge (and any other tool that re-reads .fil at the
    //             .res offset) depends on this.  Writing the pre-shift detection
    //             sample to .res instead — as an earlier revision did — left
    //             .res pointing cumShift samples away from where the .spk window
    //             actually sits, so the next nudge jumped by cumShift rather
    //             than by the requested single sample.
    //
    //   resTs   — the spike's CURRENT .res value (i.e. where its .spk window
    //             sits going into this pass).  Used only to seed extTs and for
    //             the raw-source .spk-match verification below; it is NOT a
    //             separate value persisted to .res.
    //
    // Sorting is done by extTs so spikes stay in temporal order after any shift
    // of their window position.
    std::vector<int64_t> newTs(static_cast<size_t>(N));   // = extTs (for sort + re-extract)
    std::vector<int64_t> resTs(static_cast<size_t>(N));   // current .res value (seeds extTs + verification)
    for (int64_t i = 0; i < N; ++i) {
        resTs[static_cast<size_t>(i)]  = clusterTs[static_cast<size_t>(i)];
        newTs[static_cast<size_t>(i)]  = clusterTs[static_cast<size_t>(i)]
            + static_cast<int64_t>(cumShift[static_cast<size_t>(i)]);
    }

    // sortedOrder[j] = which cluster spike goes to sorted position j
    std::vector<int64_t> sortedOrder(static_cast<size_t>(N));
    std::iota(sortedOrder.begin(), sortedOrder.end(), int64_t{0});
    std::stable_sort(sortedOrder.begin(), sortedOrder.end(),
        [&](int64_t a, int64_t b) {
            return newTs[static_cast<size_t>(a)]
                 < newTs[static_cast<size_t>(b)];
        });

    // The cluster's file positions, sorted ascending, are the destination slots
    std::vector<int64_t> targetPos(gidx.begin(), gidx.end());
    std::sort(targetPos.begin(), targetPos.end());

    for (int64_t j = 0; j < N; ++j) {
        if (gidx[static_cast<size_t>(sortedOrder[static_cast<size_t>(j)])]
                != targetPos[static_cast<size_t>(j)])
            ++nSwapped;
    }
    if (nSwapped > 0)
        log << nSwapped << " spike(s) reordered.\n";

    // -----------------------------------------------------------------------
    // Project waveform onto per-channel PCA basis → full .fet row (timeDim values)
    // Row layout: [pca_ch0_pc0, pca_ch0_pc1, ..., pca_chN_pcK, extras..., timestamp]
    // -----------------------------------------------------------------------
    auto makeFetRow = [&](int64_t csIdx, const int16_t* wav,
                           int64_t ts) -> std::vector<int64_t>
    {
        std::vector<int64_t> row(static_cast<size_t>(timeDim), int64_t{0});

        // wavBuf is loaded from .spkD which already stores the stderiv-
        // transformed waveform in channel-major layout [ch*nSamp+s].
        // For stderiv sessions pca.nCh = nChan-1 (last channel is linearly
        // dependent and excluded); for raw sessions pca.nCh = nChan.
        // Either way, project wav[ch * nSamp + recShift + j2] directly —
        // no second stderiv transform needed.
        const bool canProject = pca.valid() &&
            (isStderivRealign ? (pca.nCh == nChan - 1) : (pca.nCh == nChan));

        if (canProject) {
            int outCol = 0;
            for (int ch = 0; ch < pca.nCh; ++ch) {
                const double* E    = pca.evec[static_cast<size_t>(ch)].data();
                const double* mean = pca.means[static_cast<size_t>(ch)].data();
                for (int c = 0; c < pca.nComp; ++c) {
                    double dot = 0.0;
                    for (int j2 = 0; j2 < pca.data2use; ++j2) {
                        double x = static_cast<double>(
                            wav[static_cast<size_t>(
                                ch * nSamp + pca.recShift + j2)]);
                        if (pca.centered) x -= mean[j2];
                        dot += E[j2 + c * pca.data2use] * x;
                    }
                    row[static_cast<size_t>(outCol++)] =
                        static_cast<int64_t>(std::llround(dot));
                }
            }
            // Extra (non-PCA) feature columns copied verbatim
            for (int k = 0; k < nExtraFeats; ++k)
                row[static_cast<size_t>(nPcaFeats + k)] =
                    extraFeats[static_cast<size_t>(csIdx)]
                               [static_cast<size_t>(k)];
        } else {
            // PCA unavailable: copy existing feature values from in-memory Data
            const dataType spikeRow =
                static_cast<dataType>(
                    gidx[static_cast<size_t>(csIdx)] + 1);  // 1-based
            Q_ASSERT_X(clusteringData->isValidSpikeIndex(spikeRow),
                       "featureValue", "spikeRow out of range");
            for (int col = 0; col < nFeatCols; ++col)
                row[static_cast<size_t>(col)] =
                    static_cast<int64_t>(
                        clusteringData->featureValue(spikeRow, col + 1));
        }

        // Last column is always the timestamp
        row[static_cast<size_t>(nFeatCols)] = ts;
        return row;
    };

    // -----------------------------------------------------------------------
    // Open raw signal file for re-extraction from .fil/.dat
    // (still needed to fill wavBuf with real signal at corrected timestamps)
    // -----------------------------------------------------------------------
    const int totalNbChan = clusteringData->getTotalNbChannels();
    const QList<int>& groupChannels = clusteringData->getCurrentChannels();

    // Resolve .fil / .dat path
    QString filPath;
    {
        const QString base = spkPath.left(spkPath.lastIndexOf(QLatin1Char('.')));
        const QString noExt = base.left(base.lastIndexOf(QLatin1Char('.')));
        if (QFileInfo::exists(noExt + QStringLiteral(".fil")))
            filPath = noExt + QStringLiteral(".fil");
        else
            filPath = noExt + QStringLiteral(".dat");
    }
    _rtWprepMs = _rtmr.elapsed();
    FILE* filF = fopen(filPath.toLocal8Bit().constData(), "rb");
    if (!filF)
        log << "WARNING: cannot open raw file " << filPath
            << " — waveforms will not be re-extracted\n";

    const int64_t totalSamples = filF
        ? (static_cast<int64_t>(QFileInfo(filPath).size())
           / (static_cast<int64_t>(sizeof(short)) * totalNbChan))
        : 0LL;

    // -----------------------------------------------------------------------
    // Write realignment results directly into the persistent .pending files.
    // These were seeded from the originals on document open (and re-seeded
    // after every save/reject), so they are always complete files ready for
    // random-access writes.  The originals remain untouched until save.
    // -----------------------------------------------------------------------
    FILE* spkW = fopen(pendingSpkPath.toLocal8Bit().constData(), "r+b");
    FILE* resW = fopen(pendingResPath.toLocal8Bit().constData(), "r+b");
    FILE* fetW = fopen(pendingFetPath.toLocal8Bit().constData(), "r+b");
    if (!spkW || !resW || !fetW) {
        if (spkW) fclose(spkW);
        if (resW) fclose(resW);
        if (fetW) fclose(fetW);
        if (filF) fclose(filF);
        log << "ERROR: cannot open pending files for writing\n";
        return false;
    }

    PendingRealign pending;
    pending.bytesPerSpike  = bytesPerSpike;
    pending.spkElems       = spkElems;
    pending.timeDim        = timeDim;
    pending.nFeatCols      = nFeatCols;
    pending.records.reserve(static_cast<size_t>(N));

    // -----------------------------------------------------------------------
    // patch69 — verify that the chosen raw source (.fil or .dat) actually
    // matches what's stored in .spk.  Without this check, if .fil got
    // deleted but the .spk was originally extracted FROM .fil (filtered),
    // the code at lines 4013-4016 silently falls back to .dat (unfiltered)
    // and re-extracts unfiltered waveforms into .spk slots that were
    // filtered, corrupting the cluster a little more on every nudge.
    //
    // Algorithm: pick the first cluster spike whose extraction window fits
    // entirely within the raw file, read it, apply the same stderiv
    // transform we'd apply during normal extraction, and compare to that
    // spike's existing .spk slot (the channel-major copy currently in
    // wavBuf).  If RMS difference exceeds 50% of the .spk content's RMS,
    // the chosen raw source does not match — abort with a clear error
    // rather than silently corrupting more waveforms.
    //
    // Skipped only when filF is null (no raw source — log already warned).
    // -----------------------------------------------------------------------
    if (filF) {
        bool verifyDone = false;
        int  verifySkipped = 0;
        for (int64_t csV = 0; csV < N && !verifyDone; ++csV) {
            const int64_t tsV          = resTs[static_cast<size_t>(csV)];
            const int64_t startSampleV = tsV - static_cast<int64_t>(peakSamp0);
            if (startSampleV < 0 || startSampleV + nSamp > totalSamples) {
                ++verifySkipped;
                continue;
            }
            const off_t rawOffV = static_cast<off_t>(startSampleV)
                                * static_cast<off_t>(totalNbChan)
                                * static_cast<off_t>(sizeof(short));
            if (fseeko(filF, rawOffV, SEEK_SET) != 0) { ++verifySkipped; continue; }
            std::vector<int16_t> rawFrameV(static_cast<size_t>(nSamp * totalNbChan));
            if (fread(rawFrameV.data(), sizeof(short),
                      rawFrameV.size(), filF) != rawFrameV.size()) {
                ++verifySkipped; continue;
            }
            // Channel-major raw waveform for the group's channels
            std::vector<int16_t> verifyWav(static_cast<size_t>(nChan * nSamp));
            for (int s = 0; s < nSamp; ++s)
                for (int ci = 0; ci < nChan; ++ci)
                    verifyWav[static_cast<size_t>(ci * nSamp + s)] =
                        rawFrameV[static_cast<size_t>(s * totalNbChan + groupChannels[ci])];

            // Mirror the stderiv transform used in the per-spike loop, so
            // verifyWav lives in the same space as wavBuf for raw OR stderiv
            // .spk formats.
            if (spkIsTransformed)
                neurosuite::core::applyStderivTransform(
                    stderivOrder, verifyWav.data(), nChan, nSamp, verifyWav.data());

            // Compare to wavBuf[csV * spkElems ..] which holds the .spk
            // contents in channel-major layout (same as verifyWav above).
            const int16_t* refSpk = wavBuf.data()
                + static_cast<ptrdiff_t>(csV) * static_cast<ptrdiff_t>(spkElems);
            double sumDiff2 = 0.0, sumRef2 = 0.0;
            for (int e = 0; e < spkElems; ++e) {
                const double dv = static_cast<double>(verifyWav[static_cast<size_t>(e)])
                                - static_cast<double>(refSpk[static_cast<size_t>(e)]);
                sumDiff2 += dv * dv;
                sumRef2  += static_cast<double>(refSpk[static_cast<size_t>(e)])
                          * static_cast<double>(refSpk[static_cast<size_t>(e)]);
            }
            const double rmsRatio = (sumRef2 > 0.0)
                ? std::sqrt(sumDiff2 / sumRef2)
                : 0.0;

            if (rmsRatio > 0.5) {
                log << "ERROR: raw source verification FAILED\n"
                    << "  raw file:           " << filPath << "\n"
                    << "  verify spike index: " << csV
                    << " (ts=" << tsV << ")\n"
                    << "  RMS difference:     "
                    << QString::number(rmsRatio * 100.0, 'f', 1)
                    << "% of .spk content (threshold 50%)\n"
                    << "  Likely cause: .spk was extracted from a different raw\n"
                    << "  source than the one currently on disk (e.g. .fil was\n"
                    << "  deleted and the code fell back to .dat, or the raw file\n"
                    << "  was overwritten by a later pipeline stage).\n"
                    << "  Aborting realignment to avoid further .spk corruption.\n"
                    << "  Re-extract waveforms with process_extractspikes before\n"
                    << "  retrying the nudge.\n";
                emitFlush();
                if (spkW) fclose(spkW);
                if (resW) fclose(resW);
                if (fetW) fclose(fetW);
                fclose(filF);
                return false;
            }
            log << "Raw source verified: " << filPath
                << " matches .spk (verify spike " << csV
                << ", RMS diff "
                << QString::number(rmsRatio * 100.0, 'f', 1)
                << "%, skipped " << verifySkipped
                << " spikes near file edges)\n";
            emitFlush();
            verifyDone = true;
        }
        if (!verifyDone) {
            log << "WARNING: could not verify raw source — no spike's extraction\n"
                << "  window fit within the raw file (all near boundaries).\n"
                << "  Proceeding without verification.\n";
            emitFlush();
        }
    }

    _rtWopenMs = _rtmr.elapsed();
    for (int64_t j = 0; j < N; ++j) {
        const int64_t csIdx = sortedOrder[static_cast<size_t>(j)];
        const int64_t dest  = targetPos[static_cast<size_t>(j)];
        const int64_t ts = newTs[static_cast<size_t>(csIdx)]; // extTs: re-extraction window AND .res/.fet/in-memory timestamp (kept consistent)

        int16_t* w = wavBuf.data()
            + static_cast<ptrdiff_t>(csIdx) * static_cast<ptrdiff_t>(spkElems);

        // Capture original in-memory values before any update (needed for reject)
        PendingSpkRecord rec;
        rec.destPos = dest;
        rec.ts      = ts;
        rec.origTs  = static_cast<int64_t>(
            clusteringData->featureValue(
                static_cast<dataType>(dest + 1),
                static_cast<int>(timeDim)));  // timestamp is last dim
        rec.origFet.reserve(nFeatCols);
        for (int col = 0; col < nFeatCols; ++col)
            rec.origFet.append(
                clusteringData->featureValue(
                    static_cast<dataType>(dest + 1), col + 1));

        // Re-extract waveform from .fil/.dat at the new timestamp.
        std::vector<int16_t> spkRow(static_cast<size_t>(spkElems));
        if (filF && cumShift[static_cast<size_t>(csIdx)] != 0) {
            const int64_t startSample = ts - static_cast<int64_t>(peakSamp0);
            if (startSample >= 0 && startSample + nSamp <= totalSamples) {
                const off_t rawOff = static_cast<off_t>(startSample)
                                   * static_cast<off_t>(totalNbChan)
                                   * static_cast<off_t>(sizeof(short));
                if (fseeko(filF, rawOff, SEEK_SET) == 0) {
                    std::vector<int16_t> rawFrame(static_cast<size_t>(nSamp * totalNbChan));
                    if (fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filF) == rawFrame.size()) {
                        // Build channel-major raw waveform for w[]
                        std::vector<int16_t> rawCM(static_cast<size_t>(nChan * nSamp));
                        for (int s = 0; s < nSamp; ++s)
                            for (int ci = 0; ci < nChan; ++ci)
                                rawCM[static_cast<size_t>(ci * nSamp + s)] =
                                    rawFrame[static_cast<size_t>(
                                        s * totalNbChan + groupChannels[ci])];
                        // .spk write path: transform only when the .spk file
                        // actually stores stderiv waveforms (Pipeline D).  The
                        // wavBuf `w` parallel update must match the .spk format,
                        // so xcorr against the on-disk template stays consistent.
                        if (spkIsTransformed) {
                            // stderiv into w (channel-major), then mirror into
                            // spkRow (sample-major, what .spkD stores) so wavBuf
                            // stays in stderiv space for xcorr and the two agree.
                            neurosuite::core::applyStderivTransform(
                                stderivOrder, rawCM.data(), nChan, nSamp, w);
                            for (int s = 0; s < nSamp; ++s)
                                for (int ci = 0; ci < nChan; ++ci)
                                    spkRow[static_cast<size_t>(s * nChan + ci)] =
                                        w[static_cast<size_t>(ci * nSamp + s)];
                        } else {
                            // Raw pipeline: sample-major for .spk, channel-major for w
                            for (int s = 0; s < nSamp; ++s)
                                for (int ci = 0; ci < nChan; ++ci) {
                                    spkRow[static_cast<size_t>(s * nChan + ci)] =
                                        rawCM[static_cast<size_t>(ci * nSamp + s)];
                                    w[static_cast<size_t>(ci * nSamp + s)] =
                                        rawCM[static_cast<size_t>(ci * nSamp + s)];
                                }
                        }
                    } else {
                        for (int s = 0; s < nSamp; ++s)
                            for (int ch = 0; ch < nChan; ++ch)
                                spkRow[static_cast<size_t>(s * nChan + ch)] =
                                    w[static_cast<size_t>(ch * nSamp + s)];
                    }
                } else {
                    for (int s = 0; s < nSamp; ++s)
                        for (int ch = 0; ch < nChan; ++ch)
                            spkRow[static_cast<size_t>(s * nChan + ch)] =
                                w[static_cast<size_t>(ch * nSamp + s)];
                }
            } else {
                for (int s = 0; s < nSamp; ++s)
                    for (int ch = 0; ch < nChan; ++ch)
                        spkRow[static_cast<size_t>(s * nChan + ch)] =
                            w[static_cast<size_t>(ch * nSamp + s)];
            }
        } else {
            for (int s = 0; s < nSamp; ++s)
                for (int ch = 0; ch < nChan; ++ch)
                    spkRow[static_cast<size_t>(s * nChan + ch)] =
                        w[static_cast<size_t>(ch * nSamp + s)];
        }

        rec.fetRow = makeFetRow(csIdx, w, ts);

        // Write into the pending files (random-access, same layout as originals)
        fseeko(spkW, static_cast<off_t>(dest) * static_cast<off_t>(bytesPerSpike), SEEK_SET);
        fwrite(spkRow.data(), sizeof(int16_t), static_cast<size_t>(spkElems), spkW);

        fseeko(resW, static_cast<off_t>(dest) * static_cast<off_t>(sizeof(int64_t)), SEEK_SET);
        fwrite(&ts, sizeof(int64_t), 1, resW);  // extTs — keep .res in lock-step with the re-extracted .spk window so .spk[i] peak == .fil at .res[i] (the invariant nudge depends on)

        const off_t fetOff = static_cast<off_t>(sizeof(int32_t))
            + static_cast<off_t>(dest) * static_cast<off_t>(timeDim)
              * static_cast<off_t>(sizeof(int64_t));
        fseeko(fetW, fetOff, SEEK_SET);
        fwrite(rec.fetRow.data(), sizeof(int64_t), static_cast<size_t>(timeDim), fetW);

        // Update in-memory feature table and timestamp so scatter/feature
        // views reflect the new alignment without requiring a save first.
        {
            QList<dataType> vals;
            vals.reserve(nFeatCols);
            for (int col = 0; col < nFeatCols; ++col)
                vals.append(static_cast<dataType>(rec.fetRow[static_cast<size_t>(col)]));
            clusteringData->updateFeatureRow(
                static_cast<dataType>(dest + 1), vals);
            clusteringData->updateTimestamp(
                static_cast<dataType>(dest + 1),
                static_cast<dataType>(ts));
            // Mirror into the child atom layer: childData is a second Data over the SAME
            // spikes, so without this its scatter/feature views keep the pre-realign row
            // (parent re-aligned, children shifted/scattered).  Hierarchical mode only.
            if (childData) {
                childData->updateFeatureRow(
                    static_cast<dataType>(dest + 1), vals);
                childData->updateTimestamp(
                    static_cast<dataType>(dest + 1),
                    static_cast<dataType>(ts));
            }
        }

        rec.spkRow = std::move(spkRow);  // keep copy for flush-to-original
        pending.records.push_back(std::move(rec));
    }

    fclose(spkW);
    fclose(resW);
    fclose(fetW);
    if (filF) fclose(filF);
    _rtWwriteMs = _rtmr.elapsed();

    // spkFileName already points to pendingSpkPath (set on open and kept
    // permanently) — no redirect needed here.
    pendingRealign.push_back(std::move(pending));

    log << "Done. " << nShifted << " shifted, " << nSwapped
        << " reordered. (pending save)\n";

    // Capture after-mean for the review dialog.
    if (meanAfter) {
        meanAfter->resize(static_cast<int>(spkElems));
        for (size_t e = 0; e < spkElems; ++e) {
            double acc = 0.0;
            for (int64_t i = 0; i < N; ++i)
                acc += wavBuf[static_cast<size_t>(i) * spkElems + e];
            (*meanAfter)[static_cast<int>(e)] = static_cast<float>(acc / N);
        }
    }

    // Log the cluster after realignment — features and timestamps have been
    // updated.  Served from the batch centroid cache during Align-All.
    logAfter(QList<int>{ clusterId });

    if (_timing) {
        const qint64 _tot = _rtmr.elapsed();
        log << "[timing] cluster " << clusterId
            << ": nspk=" << N
            << " gap=" << _gapMs << "ms"
            << " setup=" << _rtSetupMs << "ms"
            << " compute=" << (_rtComputeMs - _rtSetupMs) << "ms"
            << " wsort=" << (_rtWprepMs - _rtComputeMs) << "ms"
            << " wopen=" << (_rtWopenMs - _rtWprepMs) << "ms"
            << " wwrite=" << (_rtWwriteMs - _rtWopenMs) << "ms"
            << " wlog=" << (_tot - _rtWwriteMs) << "ms"
            << " total=" << _tot << "ms";
        emitFlush();
        // Mirror to stderr so the line lands in the launching terminal too —
        // far easier to redirect/capture than the in-app realign output panel
        // (which is where the live log above goes).
        fprintf(stderr,
                "[timing] cluster %d: nspk=%lld gap=%lldms setup=%lldms "
                "compute=%lldms wsort=%lldms wopen=%lldms wwrite=%lldms "
                "wlog=%lldms total=%lldms\n",
                clusterId, static_cast<long long>(N), _gapMs, static_cast<long long>(_rtSetupMs),
                static_cast<long long>((_rtComputeMs - _rtSetupMs)),
                static_cast<long long>((_rtWprepMs - _rtComputeMs)),
                static_cast<long long>((_rtWopenMs - _rtWprepMs)),
                static_cast<long long>((_rtWwriteMs - _rtWopenMs)),
                static_cast<long long>((_tot - _rtWwriteMs)), static_cast<long long>(_tot));
        fflush(stderr);
    }
    // Record this call's end so the next cluster can report its gap.  Updated
    // unconditionally (cheap) so it is correct regardless of the timing flag.
    realignPrevEndMs = (long long)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

    return true;
}

void KlustersDoc::invalidateWaveformCache(int clusterId)
{
    clusteringData->invalidateWaveformCache(clusterId);
    // Keep the child atom layer in step so child views re-read the realigned .spk instead
    // of serving stale cached waveforms.  A parent-fiber realign touches that fiber's atoms;
    // a child-atom realign (child scope) touches the atom itself and its parent fiber.
    if (childData) {
        childData->invalidateWaveformCache(clusterId);
        for (int kid : childrenOf(QList<int>{ clusterId }))
            childData->invalidateWaveformCache(kid);
        const auto it = childToParent.constFind(clusterId);
        if (it != childToParent.constEnd())
            clusteringData->invalidateWaveformCache(it.value());
    }
}

void KlustersDoc::invalidateCorrelogramCache(int clusterId)
{
    clusteringData->invalidateCorrelogramCache(clusterId);
}

// ---------------------------------------------------------------------------
// Persistent pending files: init on open, commit+renew on save, reseed on reject
// ---------------------------------------------------------------------------

bool KlustersDoc::initPendingFiles()
{
    // Build the four pending paths from the current originals.
    pendingSpkPath = origSpkPath + QStringLiteral(".pending");
    pendingResPath = origResPath + QStringLiteral(".pending");
    pendingFetPath = origFetPath + QStringLiteral(".pending");
    pendingCluPath = docUrl       + QStringLiteral(".pending");

    // Helper: overwrite dst with a fresh copy of src.
    auto seedFile = [](const QString& src, const QString& dst) -> bool {
        QFile::remove(dst);
        if (!QFile::copy(src, dst)) {
            qWarning() << "[initPendingFiles] copy failed:" << src << "->" << dst;
            return false;
        }
        return true;
    };

    const bool ok = seedFile(origSpkPath, pendingSpkPath)
                 && seedFile(origResPath, pendingResPath)
                 && seedFile(origFetPath, pendingFetPath)
                 && seedFile(docUrl,        pendingCluPath);

    if (ok) {
        // Redirect the waveform reader and clu writer to the pending files.
        // They will remain here for the entire document session.
        clusteringData->setSpkFileName(pendingSpkPath);
        // The child atom layer reads the SAME spikes; point its waveform reader at the same
        // pending .spk so child views show the realigned waveforms.  Without this the parent
        // reads pending (aligned) while children keep reading the original (shifted/scattered).
        if (childData) childData->setSpkFileName(pendingSpkPath);
        tmpCluFile = pendingCluPath;
    }
    return ok;
}

bool KlustersDoc::commitAndRenewPending(QString* outError)
{
    // patch63 — Step 1: copy each pending file over the original.
    // QFile::copy refuses to overwrite, so remove the target first.
    // Each step now reports failure via outError + return value instead
    // of silently swallowing the error.  Without this, a failed
    // QFile::copy here meant the user saw "save successful" but the
    // on-disk file was unchanged.
    QString firstError;
    auto copyOver = [&firstError](const QString& src, const QString& dst,
                                  const char* label) -> bool {
        if (!QFile::exists(src)) {
            if (firstError.isEmpty()) {
                firstError = QString(
                    "Cannot commit %1 — pending file missing:\n%2")
                    .arg(QString::fromLatin1(label)).arg(src);
            }
            qWarning().noquote() << "[commitAndRenewPending]" << label
                                 << "pending missing:" << src;
            return false;
        }
        QFile::remove(dst);    // OK if dst doesn't exist
        if (!QFile::copy(src, dst)) {
            // QFile doesn't expose errno reliably — best we can do is
            // report both paths and check permissions/existence.
            QFileInfo dstInfo(dst);
            QFileInfo dirInfo(dstInfo.absolutePath());
            const QString hint =
                !dirInfo.isWritable() ? "parent directory not writable by current user"
              : QFileInfo::exists(dst) ? "destination still present after remove() — likely owned by another user, or immutable (chattr +i)"
              : "QFile::copy refused — check file ownership and parent-directory write permission";
            if (firstError.isEmpty()) {
                firstError = QString(
                    "Cannot commit %1:\n  %2\n→ %3\n\nHint: %4")
                    .arg(QString::fromLatin1(label))
                    .arg(src).arg(dst).arg(hint);
            }
            qWarning().noquote() << "[commitAndRenewPending]" << label
                                 << "copy failed:" << src << "->" << dst
                                 << "hint:" << hint;
            return false;
        }
        return true;
    };
    bool allOk = true;
    allOk &= copyOver(pendingSpkPath, origSpkPath, "spk");
    allOk &= copyOver(pendingResPath, origResPath, "res");
    allOk &= copyOver(pendingFetPath, origFetPath, "fet");
    allOk &= copyOver(pendingCluPath, docUrl,      "clu");

    // Clear the in-memory queue — all realignment batches have been
    // applied to disk (even if a later step failed).  Don't replay them.
    pendingRealign.clear();

    // Step 2 — renew: re-seed the pending files from the fresh originals so
    // the next realignment (or another save cycle) starts from a clean slate.
    initPendingFiles();

    // Localisation probe (Data::checkSpikeFeatureInvariant): realignSpikes updated the
    // in-memory feature rows in place (updateFeatureRow) before this commit.  If it
    // left a feature-row/spike-count mismatch, this names the realign at its boundary
    // instead of letting it surface later in a curation-log snapshot.
    if (clusteringData) clusteringData->checkSpikeFeatureInvariant("realign-commit/parent");
    if (childData)      childData->checkSpikeFeatureInvariant("realign-commit/child");

    if (!allOk && outError) *outError = firstError;
    return allOk;
}

void KlustersDoc::rejectLastRealign()
{
    if (pendingRealign.empty()) return;

    const PendingRealign& p = pendingRealign.back();

    // Restore in-memory feature/timestamp data.
    for (const PendingSpkRecord& rec : p.records) {
        clusteringData->updateFeatureRow(
            static_cast<dataType>(rec.destPos + 1),
            rec.origFet);
        clusteringData->updateTimestamp(
            static_cast<dataType>(rec.destPos + 1),
            static_cast<dataType>(rec.origTs));
    }

    pendingRealign.pop_back();

    // Re-seed pending files from the untouched originals so the waveform
    // viewer immediately reflects the restored state.
    initPendingFiles();
}

// ---------------------------------------------------------------------------
// KlustersDoc::clusterHasMembers
// ---------------------------------------------------------------------------
// Returns true iff `clusterId` is present in Data::clusterInfoMap with
// nbSpikes() > 0.  This is the same map createFeatureFile() reads when
// building the recluster temp .fet, so a false return here will produce
// an empty .fet → KK aborts with the cryptic "Array::SetSize: n < 1
// (n=0, tag=Data (nPoints*nDims))" once it tries to allocate.
//
// The cluster palette reads its membership counts from spikesByCluster
// (a row → cluster table) which can stay populated even when
// clusterInfoMap loses the corresponding key — typically after a
// curation-log replay, an undone merge/split, or an aborted reorder
// where the row-table commit landed but the clusterInfoMap rebuild
// didn't fire.  (Note: nudge itself does NOT desync the map — it only
// mutates feature rows and timestamps.  If recluster fails right after
// a nudge, the desync was already present BEFORE the nudge.)  Catching
// the desync here turns a downstream KK exception into a useful UI
// message and lets the user save / reopen to resync.
// ---------------------------------------------------------------------------
bool KlustersDoc::clusterHasMembers(int clusterId) const
{
    if (!clusteringData) return false;
    return clusteringData->clusterHasMembers(clusterId);
}

bool KlustersDoc::activeClusterHasMembers(int clusterId) const
{
    // data() == childData in hierarchical mode, clusteringData otherwise, so a
    // child id is validated against the clustering that actually owns it.
    return data().clusterHasMembers(clusterId);
}

// ---------------------------------------------------------------------------
// KlustersDoc::nudgeClusterTimestamps
// ---------------------------------------------------------------------------
// Shift every spike in @p clusterId by @p deltaSamples raw samples.
// Re-extracts waveforms from .fil at the new timestamp and reprojects onto
// the .pca.N eigenvectors, applying the spatial+temporal derivative if the
// PCA model has nCh == nChan - 1 (stderiv pipeline detection).
// ---------------------------------------------------------------------------
bool KlustersDoc::nudgeClusterTimestamps(int clusterId, int deltaSamples)
{
    if (!clusteringData) return false;
    if (deltaSamples == 0) return true;

    logBefore(CurationLogger::ActionType::NUDGE, QList<int>{ clusterId });

    // ── Stop all in-flight WaveformThreads BEFORE any file writes ─────────
    // A WaveformThread reads from pendingSpkPath (= spkFileName) without
    // holding any lock around the fread call.  If we write to that file
    // while the thread is mid-read we get a torn read → garbage waveforms
    // or, when that data drives an array index, a segfault.  Stop all
    // threads first so the file is idle before we touch it.
    for (int i = 0; i < viewList->count(); ++i)
        viewList->at(i)->stopAllViewThreads();

    if (pendingResPath.isEmpty()) {
        if (!initPendingFiles()) return false;
    }

    Data& d              = *clusteringData;
    const int   nChan    = d.nbOfChannels();
    const int   nSamp    = d.nbSamplesPerWaveform();
    const int   peakSamp0 = d.peakSampleIndex() - 1;  // 0-based
    const int   timeDim  = d.timeDimension();
    const int   nFeatCols = timeDim - 1;
    const int   totalNbChan = d.getTotalNbChannels();
    const QList<int>& groupChannels = d.getCurrentChannels();

    // ── Load .pca.N / .pcaD.N model ──────────────────────────────────────
    // .pcaD.N is written by process_pca when its output is .fetD.N.
    // Its existence definitively identifies a stderiv session — no need
    // for the nCh==nChan-1 heuristic.  Loaded below through the PCAE loader
    // (neurosuite::core::loadPca), which validates the header and carries the
    // transform Method the reprojection reads.  PcaBasis is the core type
    // (KlustersDoc alias in klustersdoc.h); a local redefinition here used to
    // shadow it, leaving pca with no `method` field and the wrong type for
    // loadPca's reference parameter.
    PcaBasis pca;

    // Derive session base: strip ".<type>[.<variant>].N" (handles .spk.N,
    // .spkD.N and dotted .spk.stderiv.N).
    const QString sessionBase = stripFeatureSuffix(origSpkPath, "spk");

    // Pipeline detection — two independent signals.  Decoupling them fixes
    // Pipeline C (raw .spk + stderiv .fetD/.pcaD) which was previously
    // misclassified as a raw session because only the .spk variant was
    // checked: PCA basis selection would fall back to .pca.N and feature
    // reprojection would skip the stderiv transform, silently corrupting
    // .fetD rows written by nudge.
    //
    //   spkIsTransformed — .spk stores stderiv waveforms (Pipeline D).
    //                      Apply applyStderivTransform when writing the
    //                      re-extracted raw waveform back to .spk; for
    //                      raw .spk (Pipeline A, C) write it unchanged.
    //                      Do NOT infer this from .pcaD existence — if
    //                      the session was opened with raw .spk.N and
    //                      ndm_pca_stderiv ran later producing .pcaD.N,
    //                      applying the transform to already-raw waveforms
    //                      would double-transform them.
    //   fetIsStderiv    — .fet features are stderiv-space, built on the
    //                      stderiv .pca basis.  Select .pca.stderiv.N and apply
    //                      the stderiv transform before projecting the raw
    //                      waveform onto the eigenvectors.
    // Independent flags (Pipeline-C), read off the resolved artifacts:
    const QString nudgeMethod = featureMethod(origFetPath);
    const bool spkIsTransformed = (featureMethod(origSpkPath) == QLatin1String("stderiv"));
    const bool fetIsStderiv     = (nudgeMethod == QLatin1String("stderiv"));
    // Legacy name retained for any downstream use that really means the
    // feature-space flag; nothing in nudge uses this directly after the
    // refactor below, but keep it for grep compatibility during review.
    const bool isStderivSession = fetIsStderiv;
    (void)isStderivSession;

    // PCA basis follows the session method: .pca.stderiv.N stores eigenvectors
    // on the (nChan-1) stderiv-space channels, paired with .fet.stderiv.N.
    const QString chosenPca = resolveFeature(
        sessionBase, "pca", electrodeGroupID, nudgeMethod);

    if (QFileInfo::exists(chosenPca)) {
        // PCAE loader (libneurosuite-core): validates magic/version and reads the
        // block-wise body, and — crucially for the reprojection below — carries the
        // transform Method, so the spatial-derivative order is taken from the basis
        // itself rather than a separate YAML field.  Returns false (→ invalid, and
        // the loud refusal below fires) on bad magic / version / short read.
        if (!neurosuite::core::loadPca(chosenPca.toStdString(), pca))
            pca = PcaBasis{};
    }

    // ── Refuse to nudge when the PCA basis failed to load ────────────────
    // makeFetRow returns a fixed-size zero-filled row when pca.valid() is
    // false (it cannot reproject without eigenvectors).  The disk write at
    // line ~4767 was previously gated on `!fetRowWritten.empty()`, which
    // ALWAYS passes — the row has the right size, just zero contents — so
    // a missing or unreadable .pca[D].N file resulted in every spike's .fet
    // row being silently overwritten with zeros.  The in-memory update is
    // correctly guarded on pca.valid() (line ~4802), so on-disk and
    // in-memory diverge: features go to zero on disk while the cluster
    // looks normal in the GUI until the session is reopened.
    //
    // Loud failure is the right behaviour here.  Nudging without
    // reprojection is not actually well-defined (you'd shift timestamps
    // and waveforms but leave features pointing at the old position),
    // so refuse rather than producing an inconsistent state.
    if (!pca.valid()) {
        QString msg = QString(
            "[nudge] cluster %1: refusing to nudge — PCA basis (%2) "
            "could not be loaded.  Run process_pca / process_pca_stderiv "
            "to regenerate, or check file permissions.")
            .arg(clusterId).arg(chosenPca);
        qWarning().noquote() << msg;
        if (auto* sb = app() ? app()->statusBar() : nullptr)
            sb->showMessage(QString("Nudge refused: PCA basis (%1) "
                                    "not available.").arg(chosenPca), 8000);
        return false;
    }

    // Feature-reprojection path: requires a valid .pcaD basis AND a .fetD
    // feature file.  The PCA basis is only meaningful if we know whether it
    // was computed in stderiv space — fetIsStderiv encodes exactly that.
    const bool isStderivFet = fetIsStderiv && pca.valid();
    // .spk write path: transform only if the file on disk is actually in
    // stderiv space (Pipeline D).  Independent of whether .fet is stderiv.
    const bool isStderivSpk = spkIsTransformed;
    // Alias kept for comments / logs / future code that references
    // "isStderiv".  Do not use it inside the transform branches below —
    // use the specific flag that matches the branch's output target.
    const bool isStderiv = isStderivFet || isStderivSpk;
    (void)isStderiv;

    // ── Open .fil/.dat for waveform re-extraction ─────────────────────────
    // sessionBase is already the bare session path (no .spk/.spkD/.N suffix).
    QString filPath;
    if (QFileInfo::exists(sessionBase + QStringLiteral(".fil")))
        filPath = sessionBase + QStringLiteral(".fil");
    else
        filPath = sessionBase + QStringLiteral(".dat");
    FILE* filF = fopen(filPath.toLocal8Bit().constData(), "rb");
    const int64_t totalSamples = filF
        ? (static_cast<int64_t>(QFileInfo(filPath).size())
           / (static_cast<int64_t>(sizeof(short)) * totalNbChan))
        : 0LL;

    // Upper bound for timestamps: last sample at which a full waveform window
    // fits — i.e. the peak can sit at peakSamp0 and the window doesn't
    // run past the end of the recording.
    const int64_t maxValidTs = totalSamples > 0
        ? (totalSamples - static_cast<int64_t>(nSamp)
           + static_cast<int64_t>(peakSamp0))
        : static_cast<int64_t>(d.maxDimension(d.timeDimension()));

    // ── Open pending files for writing ────────────────────────────────────
    FILE* spkW = fopen(pendingSpkPath.toLocal8Bit().constData(), "r+b");
    FILE* resW = fopen(pendingResPath.toLocal8Bit().constData(), "r+b");
    FILE* fetW = fopen(pendingFetPath.toLocal8Bit().constData(), "r+b");
    if (!resW || !fetW || !spkW) {
        if (spkW) fclose(spkW);
        if (resW) fclose(resW);
        if (fetW) fclose(fetW);
        if (filF) fclose(filF);
        return false;
    }

    // ── Per-spike waveform re-extraction helper ───────────────────────────
    // Reads raw waveform from .fil into channel-major layout [ch*nSamp+s].
    //
    // patch89: also reads ONE EXTRA raw sample at (startSample - 1) into
    // `prevSample` (channel-major, group channels only, nChan int16s).
    // applyStderivTransform uses this to compute the correct prev_sdiff
    // for the sample-0 temporal first-difference, matching the canonical
    // extractor's g_prev_sdiff behaviour across chunk boundaries in
    // fill_sdiff_buffer (process_extractspikes_stderiv.cpp:181-186).
    //
    // Pre-patch89 this lambda always returned `prevSample` empty and
    // applyStderivTransform fell back to sd[-1]=0 per spike — that did
    // NOT match the canonical extractor (which uses the prior sample's
    // spatial derivative as prev_sdiff for sample 0), so every nudged
    // spike differed from a canonically-extracted spike by exactly
    // sdiff[ws-1] at sample 0.  Visible as cluster mean dispersion
    // accumulating across nudges.  Edge case: when startSample==0 (spike
    // at the very start of the recording), no prev sample exists;
    // prevSample stays empty and the transform falls back to prev=0 to
    // match the canonical extractor's first-chunk startup behaviour.
    const int64_t bytesPerSpike = static_cast<int64_t>(nChan) * nSamp * 2;

    auto extractWaveform = [&](int64_t ts,
                                std::vector<int16_t>& wav,
                                std::vector<int16_t>& prevSample) -> bool {
        wav.assign(static_cast<size_t>(nChan * nSamp), 0);
        prevSample.clear();
        if (!filF) return false;
        const int64_t startSample = ts - static_cast<int64_t>(peakSamp0);
        if (startSample < 0 || startSample + nSamp > totalSamples) return false;

        // patch89: read (nSamp + 1) raw samples starting at (startSample - 1)
        // when we have a sample to spare; the leading sample feeds prev_sdiff.
        const bool hasPrev = (startSample >= 1);
        const int64_t readStart = hasPrev ? (startSample - 1) : startSample;
        const int nReadSamples  = nSamp + (hasPrev ? 1 : 0);

        const off_t rawOff = static_cast<off_t>(readStart)
                           * static_cast<off_t>(totalNbChan) * 2;
        if (fseeko(filF, rawOff, SEEK_SET) != 0) return false;
        std::vector<int16_t> rawFrame(
            static_cast<size_t>(nReadSamples) * static_cast<size_t>(totalNbChan));
        if (fread(rawFrame.data(), 2, rawFrame.size(), filF) != rawFrame.size())
            return false;

        // Split rawFrame: when hasPrev, sample 0 of rawFrame is the prev
        // sample; samples [1..nSamp] are the spike window.  When !hasPrev,
        // all nSamp samples are the spike window.
        if (hasPrev) {
            prevSample.resize(static_cast<size_t>(nChan));
            for (int ci = 0; ci < nChan; ++ci)
                prevSample[static_cast<size_t>(ci)] =
                    rawFrame[static_cast<size_t>(groupChannels[ci])];
        }
        const int wavOffset = hasPrev ? 1 : 0;
        for (int s = 0; s < nSamp; ++s)
            for (int ci = 0; ci < nChan; ++ci)
                wav[static_cast<size_t>(ci * nSamp + s)] =
                    rawFrame[static_cast<size_t>((s + wavOffset) * totalNbChan
                             + groupChannels[ci])];
        return true;
    };

    // ── Stderiv transform: matches ndm_extractspikes_stderiv exactly ──────
    // Returns transformed waveform in sample-major layout [s*nChan+ch]
    // (ALL nChan channels, including the linearly-dependent last one).
    //
    // prevRaw: kept for ABI compatibility with existing call sites; ignored.
    // The canonical pipeline applies the temporal first-difference per
    // spike with sdiff[-1] = 0, so the actual preceding raw sample is
    // not used.  See the comment on `prev` inside the body for the full
    // rationale.
    // Saturation counters — populated by applyStderivTransform via lambda
    // capture-by-reference.  Reported once after the main loop.  The int16
    // .spk format inherently clips spatial derivatives that exceed ±32768,
    // and temporal first-differences between two identically-saturated
    // samples are exactly 0 (sd[s] - sd[s-1] = -32768 - -32768 = 0).  This
    // produces visible "zero plateau" segments on the largest-amplitude
    // spikes in a cluster — a property of the canonical pipeline, not a
    // nudge bug, but worth surfacing so it can be distinguished from real
    // corruption.
    int64_t spatialClampCount  = 0;
    int64_t temporalClampCount = 0;
    int64_t temporalZeroCount  = 0;  // # of (sd - prev = 0) events

    // ── Spatial-derivative order: taken from the basis, not the YAML ─────
    // The canonical pipeline (process_extractspikes_stderiv) supports four
    // spatial-derivative modes:
    //   0 = SDIFF_NONE       (pass-through, no spatial transform)
    //   1 = SDIFF_FIRST      (val − val[ci+1], or val − val[ci-1] at the edge)
    //   2 = SDIFF_LAPLACIAN  (val − ½(val[ci-1] + val[ci+1]) at interior)
    //   3 = SDIFF_ALLPAIRS   (nChan·val − Σ val)
    // Reprojection MUST apply the SAME order the .pcaD basis was built with, or
    // the new waveform lands off-axis in the eigenvector subspace.  Earlier
    // builds read this from two session-YAML copies (extraction + ndm_pca) that
    // "should agree, else refuse"; the PCAE basis is now self-describing — its
    // Method encodes the order — so it is the single authority and the YAML
    // lookup is gone (spatialOrder()'s enum values 0..3 are exactly the modes
    // above).  pca is valid here (we refused above otherwise); the ALLPAIRS
    // fallback only covers a non-temporal-diff method, which should not reach
    // this transform.
    const int kSdiffOrder = neurosuite::core::hasTemporalDiff(pca.method)
        ? static_cast<int>(neurosuite::core::spatialOrder(pca.method))
        : 3;  // SDIFF_ALLPAIRS
    NS3_DIAG() << "[nudge] sdiffOrder=" << kSdiffOrder
               << " (from basis method=" << static_cast<int>(pca.method)
               << "; 0=none 1=first 2=laplacian 3=allpairs)";

    auto applyStderivTransform = [&](const std::vector<int16_t>& wavCM,
                                     const std::vector<int16_t>& prevRaw,
                                     std::vector<int16_t>& out) {
        // wavCM: channel-major [ch*nSamp+s]
        // out:   sample-major  [s*nChan+ch]
        out.resize(static_cast<size_t>(nSamp * nChan));

        // Step 1: spatial derivative (matches process_extractspikes_stderiv
        // computeSDiff for each mode).  Result lands in `out` in the
        // sample-major layout that matches the canonical Pass 2 writer.
        for (int s = 0; s < nSamp; ++s) {
            // Cache the channel values at this time-sample for cheap
            // intra-channel access in modes 1 (FIRST) and 2 (LAPLACIAN).
            // We read channel-major wavCM[ci * nSamp + s].
            for (int ci = 0; ci < nChan; ++ci) {
                const int val = wavCM[static_cast<size_t>(ci * nSamp + s)];
                int sd;
                switch (kSdiffOrder) {
                case 0:  // SDIFF_NONE — pass-through
                    sd = val;
                    break;
                case 1: {  // SDIFF_FIRST — val − next-channel (or prev at edge)
                    const int other = (ci < nChan - 1)
                        ? wavCM[static_cast<size_t>((ci+1) * nSamp + s)]
                        : (nChan > 1
                            ? wavCM[static_cast<size_t>((ci-1) * nSamp + s)]
                            : 0);
                    sd = val - other;
                    break; }
                case 2: {  // SDIFF_LAPLACIAN
                    if (nChan == 1) { sd = val; break; }
                    if (ci == 0)
                        sd = val - wavCM[static_cast<size_t>(1 * nSamp + s)];
                    else if (ci == nChan - 1)
                        sd = val - wavCM[static_cast<size_t>((nChan-2) * nSamp + s)];
                    else {
                        // ½(prev+next) implemented in integer arithmetic with
                        // the same rounding behaviour as the canonical:
                        //   sd = round(val − 0.5*(prev + next))
                        //      = (2*val − prev − next + sign·1) / 2
                        // canonical uses double + std::round → tie-to-nearest-even
                        // is unreachable for int16 inputs; the (a+1)/2 trick
                        // matches the floor(.5+) behaviour for all values
                        // representable as int16.
                        const int prev = wavCM[static_cast<size_t>((ci-1) * nSamp + s)];
                        const int next = wavCM[static_cast<size_t>((ci+1) * nSamp + s)];
                        const int twoVal = 2 * val - prev - next;
                        sd = (twoVal >= 0)
                            ? (twoVal + 1) / 2
                            : -((-twoVal + 1) / 2);
                    }
                    break; }
                case 3:  // SDIFF_ALLPAIRS — fall through (default)
                default: {
                    int sum = 0;
                    for (int cj = 0; cj < nChan; ++cj)
                        sum += wavCM[static_cast<size_t>(cj * nSamp + s)];
                    sd = nChan * val - sum;
                    break; }
                }
                if (sd >  32767) { sd =  32767; ++spatialClampCount; }
                else if (sd < -32768) { sd = -32768; ++spatialClampCount; }
                out[static_cast<size_t>(s * nChan + ci)] = static_cast<int16_t>(sd);
            }
        }

        // patch89: initial prev[] for the temporal first-difference.
        //
        // The canonical extractor (fill_sdiff_buffer in
        // process_extractspikes_stderiv.cpp:181-194) computes the temporal
        // first-difference as:
        //
        //     diff[t] = sd[t] - sd[t-1]                      if t > 0
        //     diff[0] = sd[0] - g_prev_sdiff[ch]             if g_prev_sdiff non-empty
        //     diff[0] = sd[0] - 0                            otherwise (first chunk only)
        //
        // where g_prev_sdiff carries the previous chunk's last sample's
        // spatial derivative.  Across all but the very first chunk, sample 0
        // sees a non-zero prev.  For typical mid-recording spikes the prev
        // sample is baseline noise so sd[ws-1] is small but non-zero — and
        // critically, the .spkD content on disk was written using this
        // non-zero prev.  Sample-0 of every spike in the on-disk .spkD
        // therefore reflects (sd[ws] - sd[ws-1]), NOT just sd[ws].
        //
        // Pre-patch89 nudge code used prev=0 unconditionally, producing
        // sample-0 = sd[ws] for nudged spikes.  This differed from the
        // canonical content by exactly sd[ws-1] at sample 0.  The .pcaD
        // basis was trained on canonical content, so the offset projected
        // as a constant feature-space displacement, dispersing nudged
        // cluster spikes.  Visible symptom: cluster mean dispersion that
        // accumulates with every nudge.
        //
        // Fix: when prevRaw is provided (size == nChan), compute its
        // spatial derivative using the same kSdiffOrder logic as above and
        // use it as the initial prev[].  When prevRaw is empty (only at
        // startSample == 0, i.e. spike at the very start of the recording),
        // fall back to prev[]=0 to match the canonical extractor's
        // first-chunk startup behaviour.
        std::vector<int16_t> prev(static_cast<size_t>(nChan), 0);
        if (!prevRaw.empty() && static_cast<int>(prevRaw.size()) == nChan) {
            for (int ci = 0; ci < nChan; ++ci) {
                const int val = prevRaw[static_cast<size_t>(ci)];
                int sd;
                switch (kSdiffOrder) {
                case 0:  // SDIFF_NONE — pass-through
                    sd = val;
                    break;
                case 1: {  // SDIFF_FIRST
                    const int other = (ci < nChan - 1)
                        ? prevRaw[static_cast<size_t>(ci + 1)]
                        : (nChan > 1
                            ? prevRaw[static_cast<size_t>(ci - 1)]
                            : 0);
                    sd = val - other;
                    break; }
                case 2: {  // SDIFF_LAPLACIAN
                    if (nChan == 1) { sd = val; break; }
                    if (ci == 0)
                        sd = val - prevRaw[1];
                    else if (ci == nChan - 1)
                        sd = val - prevRaw[static_cast<size_t>(nChan - 2)];
                    else {
                        const int p = prevRaw[static_cast<size_t>(ci - 1)];
                        const int n = prevRaw[static_cast<size_t>(ci + 1)];
                        const int twoVal = 2 * val - p - n;
                        sd = (twoVal >= 0)
                            ? (twoVal + 1) / 2
                            : -((-twoVal + 1) / 2);
                    }
                    break; }
                case 3:  // SDIFF_ALLPAIRS (default)
                default: {
                    int sum = 0;
                    for (int cj = 0; cj < nChan; ++cj)
                        sum += prevRaw[static_cast<size_t>(cj)];
                    sd = nChan * val - sum;
                    break; }
                }
                if (sd >  32767) sd =  32767;
                else if (sd < -32768) sd = -32768;
                prev[static_cast<size_t>(ci)] = static_cast<int16_t>(sd);
            }
        }

        // Step 2: temporal first-difference in-place, using prev as the
        // baseline for output sample 0.
        for (int s = 0; s < nSamp; ++s) {
            int16_t* row = out.data() + s * nChan;
            for (int ci = 0; ci < nChan; ++ci) {
                const int16_t sd  = row[ci];
                int diff = static_cast<int>(sd)
                         - static_cast<int>(prev[static_cast<size_t>(ci)]);
                if (diff >  32767) { diff =  32767; ++temporalClampCount; }
                else if (diff < -32768) { diff = -32768; ++temporalClampCount; }
                if (diff == 0 && s > 0) ++temporalZeroCount;
                prev[static_cast<size_t>(ci)] = sd;  // save SD, not diff
                row[ci] = static_cast<int16_t>(diff);
            }
        }
    };

    // ── Feature projection helper ─────────────────────────────────────────
    // wav: channel-major [ch*nSamp+s] — raw OR stderiv depending on isStderiv.
    // prevRaw: kept on the signature for ABI compatibility; ignored.  The
    //          stderiv transform now uses sdiff[-1]=0 per spike, matching
    //          the canonical pipeline that built .pcaD.
    // Returns full feature row (nFeatCols int64_t + timestamp placeholder).
    auto makeFetRow = [&](int64_t ts,
                          const std::vector<int16_t>& wavRaw,
                          const std::vector<int16_t>& prevRaw,
                          dataType spikeRow) -> std::vector<int64_t>
    {
        std::vector<int64_t> row(static_cast<size_t>(timeDim), 0LL);
        if (!pca.valid()) return row;

        // For stderiv mode: apply spatial+temporal derivative to raw waveform.
        // The derivative is applied in sample-major [s*nChan+ci] space then
        // projected using channel-major indexing into the .pca.N eigenvectors.
        // We build a compact derivative buffer in sample-major layout.
        std::vector<double> xform;  // [s * pca.nCh + ch] after derivative
        if (isStderivFet) {
            // Apply the exact same transform as ndm_extractspikes_stderiv
            // (integer arithmetic, all nChan channels).
            // Then use first pca.nCh = nChan-1 channels for projection
            // (last channel is linearly dependent and excluded from PCA).
            std::vector<int16_t> sdWav;
            applyStderivTransform(wavRaw, prevRaw, sdWav);  // sample-major [s*nChan+ch]
            const int kCh = pca.nCh;  // = nChan - 1
            xform.resize(static_cast<size_t>(nSamp * kCh));
            for (int s = 0; s < nSamp; ++s)
                for (int ci = 0; ci < kCh; ++ci)
                    xform[static_cast<size_t>(s * kCh + ci)] =
                        static_cast<double>(sdWav[static_cast<size_t>(s*nChan+ci)]);
        }

        int outCol = 0;
        for (int ch = 0; ch < pca.nCh; ++ch) {
            const double* E    = pca.evec[static_cast<size_t>(ch)].data();
            const double* mean = pca.means[static_cast<size_t>(ch)].data();
            for (int c = 0; c < pca.nComp; ++c) {
                double dot = 0.0;
                for (int j2 = 0; j2 < pca.data2use; ++j2) {
                    double x;
                    if (isStderivFet) {
                        x = xform[static_cast<size_t>(
                            (pca.recShift + j2) * pca.nCh + ch)];
                    } else {
                        x = static_cast<double>(
                            wavRaw[static_cast<size_t>(
                                ch * nSamp + pca.recShift + j2)]);
                    }
                    if (pca.centered) x -= mean[j2];
                    dot += E[j2 + c * pca.data2use] * x;
                }
                row[static_cast<size_t>(outCol++)] = std::llround(dot);
            }
        }

        // Extra feature columns (non-PCA, e.g. peak amplitude): preserve
        // the existing in-memory values rather than writing zeros.
        const int nPcaFeats = pca.nCh * pca.nComp;
        for (int col = nPcaFeats; col < nFeatCols; ++col)
            row[static_cast<size_t>(col)] =
                static_cast<int64_t>(clusteringData->featureValue(
                    spikeRow, static_cast<int>(col + 1)));

        row[static_cast<size_t>(nFeatCols)] = ts;
        return row;
    };

    // ── Main loop ─────────────────────────────────────────────────────────
    SortableTable spkTable;
    if (!clusteringData->spikePositions(clusterId, spkTable)) {
        fclose(spkW); fclose(resW); fclose(fetW);
        if (filF) fclose(filF);
        return false;
    }
    const int64_t N = static_cast<int64_t>(spkTable.nbOfColumns());

    // ── Pre-nudge invariant self-check ───────────────────────────────────
    // Klusters' nudge — and any other tool that re-reads .fil at .res
    // offsets — assumes:
    //
    //     .spk[i] peak  ≡  .fil at file-sample .res[i]
    //
    // This holds after canonical extractspikes (writes refined-peak
    // position to .res) and after the patched alignspikes (updates .res
    // to extTs = resTs + shifts).  An unpatched alignspikes leaves .res
    // pointing at the original detection-threshold position while
    // re-extracting .spk centred on the refined peak — every nudge then
    // lands at a window mispositioned by `shifts[i]` samples relative
    // to where the peak actually is in .fil.
    //
    // Detect this by reading the on-disk .spk for a small sample of
    // spikes (first, middle, last in the cluster), finding the
    // sum-|stderiv| peak position within the spike window, and comparing
    // to peakSamp0.  A consistent offset across the sample indicates the
    // invariant is broken; warn loudly so the user can re-run the
    // patched pipeline rather than producing more corrupted data.
    {
        const QString readPath = QFileInfo(pendingSpkPath).exists()
            ? pendingSpkPath : origSpkPath;
        FILE* fr = fopen(readPath.toLocal8Bit().constData(), "rb");
        if (fr && N > 0) {
            const std::vector<int64_t> probeIdx = {
                0,
                N / 2,
                N - 1,
            };
            std::vector<int> argmaxOffsets;
            argmaxOffsets.reserve(probeIdx.size());
            std::vector<int16_t> buf(static_cast<size_t>(nChan * nSamp));
            for (int64_t pIdx : probeIdx) {
                if (pIdx < 0 || pIdx >= N) continue;
                const dataType row = static_cast<dataType>(
                    spkTable(1, static_cast<dataType>(pIdx + 1)));
                const int64_t pos0 = static_cast<int64_t>(row) - 1;
                if (fseeko(fr, pos0 * static_cast<off_t>(bytesPerSpike),
                           SEEK_SET) != 0) continue;
                if (fread(buf.data(), 2, buf.size(), fr) != buf.size())
                    continue;
                // Sum |.spk| across channels per sample; in stderiv mode
                // this peaks at the refined-peak position; in raw mode it
                // peaks at the same place modulo sign (we use abs so sign
                // doesn't matter).  Layout depends on isStderivSpk: stderiv
                // is sample-major [s*nChan+c], raw is channel-major.
                int bestS = 0;
                int64_t bestE = -1;
                for (int s = 0; s < nSamp; ++s) {
                    int64_t e = 0;
                    for (int ci = 0; ci < nChan; ++ci) {
                        const size_t k = isStderivSpk
                            ? static_cast<size_t>(s * nChan + ci)
                            : static_cast<size_t>(ci * nSamp + s);
                        e += std::abs(static_cast<int>(buf[k]));
                    }
                    if (e > bestE) { bestE = e; bestS = s; }
                }
                argmaxOffsets.push_back(bestS - peakSamp0);
            }
            fclose(fr);
            // Median absolute offset across the probes.  If the median is
            // > 1 sample, the .spk peak is consistently NOT at peakSamp0
            // — strongly suggests the .res/.spk invariant is broken.
            if (argmaxOffsets.size() >= 2) {
                std::vector<int> abs_off;
                abs_off.reserve(argmaxOffsets.size());
                for (int o : argmaxOffsets) abs_off.push_back(std::abs(o));
                std::sort(abs_off.begin(), abs_off.end());
                const int median = abs_off[abs_off.size() / 2];
                if (median > 1) {
                    // The cluster's .spk peaks don't sit at peakSamp0.
                    // This is NOT something nudge introduces — it's a
                    // pre-existing condition (most often a stale .par.N
                    // peakSampleIndex, or .spk written by an old
                    // ndm_alignspikes that didn't update .res).
                    //
                    // Nudge is a rigid time translation: it shifts every
                    // .res[i] by delta and re-reads the .fil window at
                    // (newRes - peakSamp0).  Whatever within-window
                    // alignment existed before the nudge is preserved
                    // afterward — the median offset will still be
                    // `median` samples after this operation.
                    //
                    // We used to refuse here, but that conflates two
                    // independent concerns: window-internal peak position
                    // (what `median` measures) and timestamp accuracy
                    // (what nudge corrects).  The user's nudge intent
                    // doesn't depend on the former, so we proceed and
                    // just inform them.
                    QString offTxt;
                    for (int o : argmaxOffsets)
                        offTxt += QString::number(o) + ' ';
                    qInfo().noquote() << QString(
                        "[nudge] cluster %1: existing alignment off by %2 "
                        "samples (peakSamp0=%3, per-probe offsets: %4); "
                        "nudge will preserve the offset.")
                        .arg(clusterId).arg(median).arg(peakSamp0)
                        .arg(offTxt.trimmed());
                    if (auto* sb = app() ? app()->statusBar() : nullptr)
                        sb->showMessage(QString(
                            "Cluster %1: existing alignment off by %2 "
                            "samples; nudge will preserve the offset.")
                            .arg(clusterId).arg(median), 5000);
                    // Fall through — proceed with the nudge.
                }
            }
        } else if (fr) {
            fclose(fr);
        }
    }

    // Defensive accounting — surface I/O failures rather than silently
    // writing at stale positions.  These should normally remain zero.
    int64_t spkSeekFail   = 0;
    int64_t resSeekFail   = 0;
    int64_t fetSeekFail   = 0;
    int64_t shortSpkWrite = 0;
    int64_t shortResWrite = 0;
    int64_t shortFetWrite = 0;
    int64_t boundarySkip  = 0;  // extractWaveform returned false
    int64_t fetReadback   = 0;  // # spikes whose .fet readback differed from intended
    int64_t spkReadback   = 0;  // # spikes whose .spk readback differed from intended

    // Per-spike trace mode: when NS3_VERBOSE is set, dump detailed numbers for
    // the first nudged spike so the user can verify that:
    //   (a) extractWaveform is reading exactly nSamp consecutive samples from
    //       the right offset (no half-window mismatch),
    //   (b) applyStderivTransform produces values that match what
    //       process_extractspikes_stderiv would on the same raw window,
    //   (c) the on-disk .fet bytes after our write match the values we asked
    //       to write (ruling out concurrent thread interference),
    //   (d) the feature delta is in line with what process_refeaturize_stderiv
    //       would compute for a +1-sample shift of the same spike.
    // The reference computation can be reproduced offline by running:
    //   process_refeaturize_stderiv -p session.pcaD.G -w nSamp -n (nChan-1) \
    //                                -i one-spike-index session.spkD.G
    // on the .spkD slice corresponding to the nudged spike.
    const bool traceFirst = qEnvironmentVariableIsSet("NS3_VERBOSE");

    // ── Mean-waveform dump (gated by NUDGE_DUMP_MEAN env var) ─────────────
    // When enabled, walk every spike in the cluster twice — once before the
    // main rewrite loop and once after — accumulating the mean waveform
    // straight from the bytes on disk in .spk.pending.  Both means are
    // written to a single text file in the session directory so the user
    // can upload it and we can plot before/after side by side.  This is
    // the most direct test for "nudge moves the post-peak portion of the
    // waveform by more than the pre-peak portion": if true, the after-mean
    // will show a non-uniform horizontal shift relative to the before-mean.
    // Reads use a separate read-only FILE* so we don't perturb the spkW
    // cursor that the main loop relies on.
    const bool dumpMean = qEnvironmentVariableIsSet("NUDGE_DUMP_MEAN");
    auto computeMean = [&](std::vector<double>& meanCM /* [ch*nSamp+s] */)
                          -> int64_t {
        meanCM.assign(static_cast<size_t>(nChan * nSamp), 0.0);
        FILE* fr = fopen(pendingSpkPath.toLocal8Bit().constData(), "rb");
        if (!fr) return 0;
        std::vector<int16_t> buf(static_cast<size_t>(nChan * nSamp));
        int64_t nRead = 0;
        for (int64_t i = 0; i < N; ++i) {
            const dataType row = static_cast<dataType>(
                spkTable(1, static_cast<dataType>(i + 1)));
            const int64_t pos0 = static_cast<int64_t>(row) - 1;
            if (fseeko(fr, pos0 * static_cast<off_t>(bytesPerSpike),
                       SEEK_SET) != 0) continue;
            if (fread(buf.data(), 2, buf.size(), fr) != buf.size()) continue;
            // .spk layout is sample-major [s*nChan+ci]; accumulate to
            // channel-major mean buffer [ci*nSamp+s] so the file dump
            // reads naturally one channel at a time.
            for (int s = 0; s < nSamp; ++s)
                for (int ci = 0; ci < nChan; ++ci)
                    meanCM[static_cast<size_t>(ci * nSamp + s)] +=
                        buf[static_cast<size_t>(s * nChan + ci)];
            ++nRead;
        }
        fclose(fr);
        if (nRead > 0)
            for (auto& v : meanCM)
                v /= static_cast<double>(nRead);
        return nRead;
    };

    std::vector<double> meanBefore;
    int64_t nReadBefore = 0;
    if (dumpMean) nReadBefore = computeMean(meanBefore);

    for (int64_t i = 0; i < N; ++i) {
        const dataType row  = static_cast<dataType>(
            spkTable(1, static_cast<dataType>(i + 1)));
        const int64_t  pos0 = static_cast<int64_t>(row) - 1;

        // Read the authoritative old timestamp from .res.pending rather than
        // the .fetD copy in the features array.  The .fetD timestamp column may
        // have been zeroed by an earlier buggy build (gotWav=false wrote zeros);
        // .res.pending is always written atomically per spike and is safe to use.
        int64_t oldTs64 = 0;
        if (fseeko(resW, static_cast<off_t>(pos0) * static_cast<off_t>(sizeof(int64_t)),
                   SEEK_SET) != 0) {
            ++resSeekFail;
            oldTs64 = static_cast<int64_t>(clusteringData->featureValue(row, timeDim));
        } else if (fread(&oldTs64, sizeof(int64_t), 1, resW) != 1) {
            oldTs64 = static_cast<int64_t>(clusteringData->featureValue(row, timeDim));
        }
        const dataType oldTs = static_cast<dataType>(oldTs64);
        dataType newTs = oldTs + static_cast<dataType>(deltaSamples);
        if (newTs < 0) newTs = 0;
        if (maxValidTs > 0 && newTs > static_cast<dataType>(maxValidTs))
            newTs = static_cast<dataType>(maxValidTs);

        const int64_t ts64 = static_cast<int64_t>(newTs);

        // Capture the in-memory feature row BEFORE the write, so we can show
        // the user the feature delta and compare against an offline reference.
        std::vector<int64_t> oldFetRow;
        if (traceFirst && i == 0 && pca.valid()) {
            oldFetRow.reserve(static_cast<size_t>(nFeatCols));
            for (int col = 0; col < nFeatCols; ++col)
                oldFetRow.push_back(static_cast<int64_t>(
                    clusteringData->featureValue(row, col + 1)));
        }

        // Re-extract waveform at new timestamp.  patch89: prevSample is
        // populated with the raw sample at (startSample - 1) when one is
        // available, so applyStderivTransform's sample-0 temporal-diff uses
        // the correct prev_sdiff matching the canonical extractor's
        // g_prev_sdiff behaviour.  prevSample is empty only when the spike
        // sits at the very start of the recording (startSample == 0).
        std::vector<int16_t> wav;
        std::vector<int16_t> prevSample;
        const bool gotWav = extractWaveform(ts64, wav, prevSample);
        if (!gotWav) ++boundarySkip;

        // Write .spk at new position.  Use spkIsTransformed (via isStderivSpk),
        // not the feature-space flag: .spk format is independent of .fet format.
        std::vector<int16_t> spkRowWritten;  // kept for trace + readback
        if (gotWav) {
            if (fseeko(spkW, static_cast<off_t>(pos0) * static_cast<off_t>(bytesPerSpike),
                       SEEK_SET) != 0) {
                ++spkSeekFail;
            } else if (isStderivSpk) {
                // stderiv pipeline: .spk stores transformed waveform
                // (all nChan channels, sample-major, matching ndm_extractspikes_stderiv)
                applyStderivTransform(wav, prevSample, spkRowWritten);
                const size_t want = static_cast<size_t>(nChan * nSamp);
                if (fwrite(spkRowWritten.data(), 2, want, spkW) != want) ++shortSpkWrite;
            } else {
                // raw pipeline: convert channel-major → sample-major
                spkRowWritten.assign(static_cast<size_t>(nChan * nSamp), 0);
                for (int s = 0; s < nSamp; ++s)
                    for (int ch = 0; ch < nChan; ++ch)
                        spkRowWritten[static_cast<size_t>(s * nChan + ch)] =
                            wav[static_cast<size_t>(ch * nSamp + s)];
                const size_t want = static_cast<size_t>(nChan * nSamp);
                if (fwrite(spkRowWritten.data(), 2, want, spkW) != want) ++shortSpkWrite;
            }
        }

        // Write .res — but ONLY when the new waveform was successfully
        // extracted from .fil.  When the new window crosses a recording
        // boundary, extractWaveform returns false and .spk / .fet writes
        // are skipped below, leaving stale waveform/feature content on
        // disk.  If we still updated .res in that case, the spike's .res
        // timestamp would no longer match the .spk window position — the
        // .res/.spk invariant the entire pipeline depends on (.spk[i] peak
        // ≡ .fil at .res[i]) breaks for the affected spikes.  Visible
        // symptom: a small fraction of cluster spikes show waveforms
        // shifted by deltaSamples relative to where their .res says.
        // Boundary spikes therefore stay at their original timestamp —
        // counted via boundarySkip and reported in the summary so the
        // user can see how many spikes were left behind.
        if (gotWav) {
            if (fseeko(resW, static_cast<off_t>(pos0) * static_cast<off_t>(sizeof(int64_t)),
                       SEEK_SET) != 0) {
                ++resSeekFail;
            } else if (fwrite(&ts64, sizeof(int64_t), 1, resW) != 1) {
                ++shortResWrite;
            }
        }

        // Reproject and write .fet — only when waveform was successfully read.
        // When gotWav=false (spike at recording boundary or .fil unreadable),
        // preserve the existing feature values rather than zeroing them out.
        std::vector<int64_t> fetRowWritten;
        if (gotWav) {
            fetRowWritten = makeFetRow(ts64, wav, prevSample, row);
            if (!fetRowWritten.empty()) {
                // Update on-disk .fetD
                const off_t fetOff = static_cast<off_t>(sizeof(int32_t))
                    + static_cast<off_t>(pos0) * static_cast<off_t>(timeDim)
                      * static_cast<off_t>(sizeof(int64_t));
                if (fseeko(fetW, fetOff, SEEK_SET) != 0) {
                    ++fetSeekFail;
                } else {
                    const size_t want = static_cast<size_t>(timeDim);
                    if (fwrite(fetRowWritten.data(), sizeof(int64_t), want, fetW) != want)
                        ++shortFetWrite;
                    fflush(fetW);
                }

                // Read-back verification: confirm the bytes on disk now match
                // what we asked to write.  A mismatch would indicate either a
                // concurrent writer (no other code path should be touching
                // .fetD.pending while nudge holds spkW/resW/fetW open) or a
                // filesystem-level reorder; either is a bug we want to catch.
                if (fseeko(fetW, fetOff, SEEK_SET) == 0) {
                    std::vector<int64_t> rb(static_cast<size_t>(timeDim), 0LL);
                    const size_t got = fread(rb.data(), sizeof(int64_t),
                                             static_cast<size_t>(timeDim), fetW);
                    if (got == static_cast<size_t>(timeDim)) {
                        for (int col = 0; col < timeDim; ++col) {
                            if (rb[static_cast<size_t>(col)]
                                != fetRowWritten[static_cast<size_t>(col)]) {
                                ++fetReadback;
                                break;
                            }
                        }
                    }
                }

                // Update in-memory feature table (PCA dims only).
                // updateFeatureRow explicitly excludes the timestamp
                // column (see Data::updateFeatureRow), so we have to
                // call updateTimestamp separately — otherwise the
                // in-memory `.fet` time column stays at oldTs while the
                // on-disk one has been advanced to newTs.  Anything that
                // reads the in-memory feature table (autocorrelogram,
                // refractory-violation panel, error matrix) would
                // continue to show the cluster at its pre-nudge
                // timestamps until the session is closed and reopened.
                if (pca.valid()) {
                    QList<dataType> vals;
                    vals.reserve(nFeatCols);
                    for (int col = 0; col < nFeatCols; ++col)
                        vals.append(static_cast<dataType>(
                            fetRowWritten[static_cast<size_t>(col)]));
                    clusteringData->updateFeatureRow(row, vals);
                    clusteringData->updateTimestamp(row, newTs);
                }
            }
        }

        // .spk read-back verification, mirroring the .fet check above.
        if (gotWav && !spkRowWritten.empty()) {
            if (fseeko(spkW, static_cast<off_t>(pos0)
                       * static_cast<off_t>(bytesPerSpike), SEEK_SET) == 0) {
                fflush(spkW);
                std::vector<int16_t> rbSpk(spkRowWritten.size(), 0);
                const size_t got = fread(rbSpk.data(), 2,
                                         spkRowWritten.size(), spkW);
                if (got == spkRowWritten.size()) {
                    for (size_t k = 0; k < spkRowWritten.size(); ++k) {
                        if (rbSpk[k] != spkRowWritten[k]) {
                            ++spkReadback;
                            break;
                        }
                    }
                }
            }
        }

        // Per-spike trace for the first nudged spike.
        if (traceFirst && i == 0 && gotWav) {
            // Find detection channel (largest peak-to-peak in the raw window).
            int detCh = 0;
            int bestPp = -1;
            for (int ci = 0; ci < nChan; ++ci) {
                int16_t mn = INT16_MAX, mx = INT16_MIN;
                for (int s = 0; s < nSamp; ++s) {
                    const int16_t v = wav[static_cast<size_t>(ci * nSamp + s)];
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
                const int pp = static_cast<int>(mx) - static_cast<int>(mn);
                if (pp > bestPp) { bestPp = pp; detCh = ci; }
            }

            QString rawTxt;
            for (int s = 0; s < std::min(nSamp, 12); ++s)
                rawTxt += QString::number(
                    wav[static_cast<size_t>(detCh * nSamp + s)]) + ' ';
            qDebug().noquote() << QString(
                "[nudge-trace] cluster=%1 spike=%2 (file pos %3) "
                "oldTs=%4 newTs=%5 delta=%6 peakSamp0=%7 startSample=%8 detCh=%9")
                .arg(clusterId).arg(i).arg(pos0)
                .arg(static_cast<long long>(oldTs))
                .arg(static_cast<long long>(newTs))
                .arg(deltaSamples)
                .arg(peakSamp0)
                .arg(static_cast<long long>(ts64 - peakSamp0))
                .arg(detCh);
            qDebug().noquote() << QString(
                "[nudge-trace]  raw[detCh,0..11]: %1").arg(rawTxt);

            if (isStderivSpk && spkRowWritten.size()
                >= static_cast<size_t>(nChan * std::min(nSamp, 12))) {
                QString sdTxt;
                for (int s = 0; s < std::min(nSamp, 12); ++s)
                    sdTxt += QString::number(
                        spkRowWritten[static_cast<size_t>(s * nChan + detCh)]) + ' ';
                qDebug().noquote() << QString(
                    "[nudge-trace]  sdWritten[detCh,0..11]: %1").arg(sdTxt);
            }

            if (!fetRowWritten.empty()) {
                const int nShow = std::min(nFeatCols, 8);
                QString oldF, newF, dF;
                for (int c = 0; c < nShow; ++c) {
                    const int64_t o = (c < static_cast<int>(oldFetRow.size()))
                        ? oldFetRow[static_cast<size_t>(c)] : 0;
                    const int64_t n = fetRowWritten[static_cast<size_t>(c)];
                    oldF += QString::number(static_cast<long long>(o)) + ' ';
                    newF += QString::number(static_cast<long long>(n)) + ' ';
                    dF   += QString::number(static_cast<long long>(n - o)) + ' ';
                }
                qDebug().noquote() << QString(
                    "[nudge-trace]  oldFet[0..%1]: %2").arg(nShow-1).arg(oldF);
                qDebug().noquote() << QString(
                    "[nudge-trace]  newFet[0..%1]: %2").arg(nShow-1).arg(newF);
                qDebug().noquote() << QString(
                    "[nudge-trace]  deltaFet[0..%1]: %2").arg(nShow-1).arg(dF);
            }
        }
    }

    fclose(spkW); fclose(resW); fclose(fetW);
    if (filF) fclose(filF);

    // ── Mean-waveform dump: AFTER pass + write file ────────────────────────
    if (dumpMean) {
        std::vector<double> meanAfter;
        const int64_t nReadAfter = computeMean(meanAfter);

        // Write next to the .spk.pending file with a unique timestamp so
        // repeated nudges produce distinct dump files.
        QFileInfo spkInfo(pendingSpkPath);
        const QString outName = QString("nudge-meanwave-c%1-d%2-%3.txt")
            .arg(clusterId).arg(deltaSamples)
            .arg(QDateTime::currentSecsSinceEpoch());
        const QString outPath = spkInfo.dir().absoluteFilePath(outName);

        FILE* fw = fopen(outPath.toLocal8Bit().constData(), "w");
        if (fw) {
            fprintf(fw, "# nudge mean-waveform dump\n");
            fprintf(fw, "# clusterId=%d delta=%d nChan=%d nSamp=%d "
                        "peakSamp0=%d isStderivSpk=%d isStderivFet=%d "
                        "nSpikes=%lld nReadBefore=%lld nReadAfter=%lld\n",
                    clusterId, deltaSamples, nChan, nSamp,
                    peakSamp0,
                    isStderivSpk ? 1 : 0,
                    isStderivFet ? 1 : 0,
                    static_cast<long long>(N),
                    static_cast<long long>(nReadBefore),
                    static_cast<long long>(nReadAfter));
            // Column layout chosen to be trivial to load with numpy:
            //   data = np.loadtxt(path, skiprows=3)   # 4 cols: ch, s, b, a
            fprintf(fw, "ch s before after\n");
            const size_t nb = meanBefore.size();
            const size_t na = meanAfter.size();
            for (int ci = 0; ci < nChan; ++ci) {
                for (int s = 0; s < nSamp; ++s) {
                    const size_t k = static_cast<size_t>(ci * nSamp + s);
                    const double b = (k < nb) ? meanBefore[k] : 0.0;
                    const double a = (k < na) ? meanAfter[k]  : 0.0;
                    fprintf(fw, "%d %d %.6f %.6f\n", ci, s, b, a);
                }
            }
            fclose(fw);
            qDebug().noquote() << QString("[nudge-meanwave] wrote %1 "
                "(N=%2 nReadBefore=%3 nReadAfter=%4)")
                .arg(outPath)
                .arg(N).arg(nReadBefore).arg(nReadAfter);
        } else {
            qWarning().noquote() << QString(
                "[nudge-meanwave] cannot open %1 for writing").arg(outPath);
        }
    }
    // Diagnostic summary.  All "fail" / "shortWrite" / "Readback" counters
    // should be zero under normal operation; non-zero indicates filesystem
    // trouble or a concurrent writer.  The saturation counters reflect the
    // int16 .spk format limit and are expected to be non-zero on clusters
    // containing very-large-amplitude spikes — this is a property of the
    // canonical pipeline (see the boundary-condition comment above), not
    // nudge corruption.
    NS3_DIAG() << "[nudge] cluster=" << clusterId
               << " delta=" << deltaSamples
               << " N=" << N
               << " boundarySkip=" << boundarySkip
               << " spatialClamp=" << spatialClampCount
               << " temporalClamp=" << temporalClampCount
               << " temporalZero=" << temporalZeroCount
               << " (of " << (N * static_cast<int64_t>(nSamp - 1) * nChan)
               << " interior temporal-diff samples)"
               << " fetReadback=" << fetReadback
               << " spkReadback=" << spkReadback;
    if (spkSeekFail || resSeekFail || fetSeekFail
        || shortSpkWrite || shortResWrite || shortFetWrite
        || fetReadback || spkReadback) {
        qWarning() << "[nudge] I/O anomalies cluster=" << clusterId
                   << " spkSeekFail=" << spkSeekFail
                   << " resSeekFail=" << resSeekFail
                   << " fetSeekFail=" << fetSeekFail
                   << " shortSpkWrite=" << shortSpkWrite
                   << " shortResWrite=" << shortResWrite
                   << " shortFetWrite=" << shortFetWrite
                   << " fetReadback=" << fetReadback
                   << " spkReadback=" << spkReadback;
    }

    setModified(true);

    // Surface boundary-skip count to the user.  After the bug-B fix,
    // spikes whose new window crosses a recording boundary stay at their
    // original timestamp rather than being silently desynchronised.  If
    // any spikes were left behind, tell the user via the status bar so
    // they know the cluster wasn't fully translated.
    if (boundarySkip > 0) {
        QString msg = QString(
            "Nudge: cluster %1 — %2 of %3 spike(s) near recording boundary "
            "kept at original timestamp (new window outside .fil range).")
            .arg(clusterId)
            .arg(static_cast<long long>(boundarySkip))
            .arg(static_cast<long long>(N));
        qInfo().noquote() << "[nudge]" << msg;
        if (auto* sb = app() ? app()->statusBar() : nullptr)
            sb->showMessage(msg, 8000);
    }

    // Recompute per-dimension min/max so the scatter view world-window
    // axes are correct after feature values have been updated.
    clusteringData->minMaxDimensionCalculation(QList<int>{clusterId});

    // Invalidate caches so the next thread launch re-reads from disk/memory.
    invalidateWaveformCache(clusterId);
    invalidateCorrelogramCache(clusterId);

    // Force every ClusterView to recalculate its world window.
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        v->updateDimensions(v->abscissaDimension(), v->ordinateDimension());
    }

    // For each view showing this cluster: set REDRAW mode on the scatter
    // (ClusterView) to erase ghost points, then relaunch the WaveformThread
    // and CorrelogramThread against the updated data.
    for (int i = 0; i < viewList->count(); ++i)
        viewList->at(i)->invalidateClusterDisplay(clusterId);

    // Refresh the trace view (if any) so its cluster timestamp markers
    // shift to the new positions on .fil playback.  Without this, the
    // trace would only repaint when something else triggered a redraw
    // — e.g. an open errormatrix's update — making nudge appear to
    // "only work when errormatrix is available".
    KlustersView* activeView =
        app()->activeView();
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        v->updateTraceView(electrodeGroupID, clusterColorList,
                           v == activeView);
    }

    logAfter(QList<int>{ clusterId });

    return true;
}
