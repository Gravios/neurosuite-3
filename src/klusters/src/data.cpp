#include "klusters.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <QThread>
/***************************************************************************
                          data.cpp  -  description
                             -------------------
    begin                : Wed Sep 17 2003
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
//Application include files
#include "data.h"
#include "binfile.h"
#include "minmaxthread.h"
#include "waveformview.h"
#include "autosavethread.h"
#include "klustersyamlreader.h"

//C include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64
#include <cstring>

//Qt include files
#include <QTextStream>
#include <QStringList>
#include <QString>
#include <QRegularExpression>

#include <QList>
#include <QDebug>

// (no KDE includes; Qt6-only build)


#include <iomanip> // Required for formated I/O.


#include "timer.h"
#include <neurosuite/core/neurofileio.h>
#include <queue>

extern int nbUndo;

Data::Data()
    :nbSpikes(0),
      traceViewVariablesAvailable(false),
      undoRedoInProcess(false),
      clusterZeroJustModified(false)
{

    minMaxThread = minMaxCalculator();
    spikesByCluster = new SortableTable();
    clusterInfoMap = new ClusterInfoMap();

}

Data::~Data(){
    //If the minMaxThread has not finish, wait until it is done
    minMaxThread->wait();
    delete minMaxThread;

    //delete the pointers to the tables and maps
    delete spikesByCluster;
    delete clusterInfoMap;

    qDeleteAll(clusterInfoMapUndoList);
    clusterInfoMapUndoList.clear();
    qDeleteAll(clusterInfoMapRedoList);
    clusterInfoMapRedoList.clear();
    qDeleteAll(spikesByClusterUndoList);
    spikesByClusterUndoList.clear();
    qDeleteAll(spikesByClusterRedoList);
    spikesByClusterRedoList.clear();

    qDeleteAll(waveformDict);
    waveformDict.clear();
    qDeleteAll(correlationDict);
    correlationDict.clear();

}

MinMaxThread* Data::minMaxCalculator(){
    return new MinMaxThread(*this);
}


bool Data::configure(QFile& parFile,int electrodeGroupID,QString& errorInformation){

    // Helper lambda to load fields from any reader that exposes the
    // KlustersYamlReader-compatible API (both KlustersYamlReader and SessionYamlReader).
    auto loadFromReader = [&](auto& reader) -> bool {
        nbBits = reader.getResolution();
        samplingRate = reader.getSamplingRate();
        QList<int> channels = reader.getNbChannelsByGroup(electrodeGroupID);

        QList<int>::iterator it;
        for(it = channels.begin(); it != channels.end(); ++it) currentChannels.append(*it);
        nbChannels = currentChannels.size();
        nbSamplesInWaveform = reader.getNbSamples(electrodeGroupID);
        peakPositionInWaveform = reader.getPeakSampleIndex(electrodeGroupID);
        nbFeaturesbyChannel = reader.getNbFeatures(electrodeGroupID);
        totalNbChannels = reader.getNbChannels();

        voltageRange = reader.getVoltageRange();
        amplification = reader.getAmplification();
        initialOffset = reader.getOffset();
        if(voltageRange != 0 && amplification != 0 && totalNbChannels != 0)
            traceViewVariablesAvailable = true;

        reader.getClusterUserInformation(electrodeGroupID, clusterUserInformationMap);
        reader.closeFile();
        return true;
    };

    const QString filePath = parFile.fileName();
    const bool isYaml = filePath.endsWith(QLatin1String(".yaml"), Qt::CaseInsensitive)
                     || filePath.endsWith(QLatin1String(".yml"),  Qt::CaseInsensitive);

    bool parsed = false;
    if (isYaml) {
        KlustersYamlReader reader;
        if (reader.parseFile(filePath))
            parsed = loadFromReader(reader);
    } else {
        // Legacy XML parameter files (.xml) are not supported in neurosuite-3.
        // Convert to YAML using the migration tool or open the .yaml file directly.
        errorInformation = QObject::tr(
            "Legacy XML parameter files are not supported. "
            "Please use the YAML parameter file (.yaml) instead.");
        return false;
    }

    if (!parsed) {
        errorInformation = QObject::tr("The parameter file could not be parsed.");
        return false;
    }

    // Validate that required fields were present
    if(nbBits == 0){
        errorInformation = QObject::tr("In the parameter file, the number of bits is missing.");
        return false;
    }
    if(samplingRate == 0){
        errorInformation = QObject::tr("In the parameter file, the sampling rate is missing.");
        return false;
    }
    if(currentChannels.isEmpty()){
        errorInformation = QObject::tr("There is no channels defined for this electrode group.");
        return false;
    }
    if(nbChannels == 0){
        errorInformation = QObject::tr("In the parameter file, the number of channels could not be determined.");
        return false;
    }
    if(nbSamplesInWaveform == 0){
        errorInformation = QObject::tr("In the parameter file, the number of samples per waveform is missing.");
        return false;
    }
    if(peakPositionInWaveform == 0){
        errorInformation = QObject::tr("In the parameter file, the position of the waveform peak is missing.");
        return false;
    }
    if(nbFeaturesbyChannel == 0){
        errorInformation = QObject::tr("In the parameter file, the number of features per channel is missing.");
        return false;
    }

    // Sampling rate is in Hz; sampling interval used in Klusters is in microseconds.
    samplingInterval = 1000000.0 / samplingRate;

    return true;
}

bool Data::configure(QFile& parXFile,QFile& parFile,QString& errorInformation){
    QTextStream parX(&parXFile);
    QTextStream par(&parFile);
    QList <QStringList> parXData;
    QList <QStringList> parData;

    int lineCounter = 0;
    QString line;
    for(line = parX.readLine(); !line.isNull();line = parX.readLine()){
        parXData.append(line.split(" ",Qt::SkipEmptyParts));
        lineCounter ++;
    }
    //The parX file must contain exactly 9 lines; fewer or more indicates a corrupt/unsupported format.
    if(lineCounter != 9){
        errorInformation = QObject::tr("In the general parameter file (base.par), the number of lines should be 9.");
        return false;
    }

    //get the parameters
    totalNbChannels = parXData[0][0].toInt();

    nbChannels = parXData[0][1].toInt();
    for (int i=0; i<nbChannels;++i){
        channelIds.append(parXData[1][i].toInt());
    }
    nbRefactorySample = parXData[2][0].toInt();
    RMSIntWindowLength = parXData[2][1].toInt();
    firingRate = parXData[3][0].toFloat();
    nbSamplesInWaveform = parXData[4][0].toInt();
    peakPositionInWaveform = parXData[4][1].toInt();
    windowLengthToRealign = parXData[5][0].toInt();
    peakPositionToRealign = parXData[5][1].toInt();
    nbSampleBeforePeak = parXData[6][0].toInt();
    nbSampleAfterPeak = parXData[6][1].toInt();
    nbFeaturesbyChannel = parXData[7][0].toInt();
    nbSamplesByPCA = parXData[7][1].toInt();
    HighPassFilterFreq = parXData[8][0].toFloat();

    lineCounter = 0;
    for(line = par.readLine(); !line.isNull();line = par.readLine()){
        parData.append(line.split(" ",Qt::SkipEmptyParts));
        lineCounter ++;
    }

    //The par file must contain at least 3 lines, otherwise there is a problem
    if(lineCounter < 3){
        errorInformation = QObject::tr("In the specific parameter file (base.par.n), there are less than 3 lines.");
        return false;
    }

    nbBits = parData[0][1].toInt();
    samplingInterval = (parData[1][0].toDouble());
    nbTotalElectrodes = parData[2][0].toInt();

    return true;
}

bool Data::loadClusters(QFile& clusterFile, long spkFileLength, QString& errorInformation)
{
    // Determine nbSpikes from the .spk file length
    int sampleSize;
    switch (nbBits) {
    case 12: case 14: case 16: sampleSize = 2; isTwoBytesRecording = true;  break;
    case 32:                   sampleSize = 4; isTwoBytesRecording = false; break;
    default:
        errorInformation = QObject::tr("The number of bits is not supported.");
        return false;
    }
    nbSpikes = spkFileLength /
               (static_cast<long>(nbChannels) * static_cast<long>(nbSamplesInWaveform) * static_cast<long>(sampleSize));

    // Binary .clu format (shared neurofileio::readCluBinary):
    //   int32_t  nClusters
    //   nSpikes x int32_t  cluster ids in timestamp order
    const QString path = clusterFile.fileName();
    clusterFile.close();

    const neurofileio::CluFile clu =
        neurofileio::readCluBinary(path.toStdString(), nbSpikes);
    if (!clu.ok) {
        errorInformation = QObject::tr(
            "Cannot open or fully read cluster file (expected %1 entries): %2")
            .arg(nbSpikes).arg(path);
        return false;
    }
    if (clu.nClusters < 0 || clu.nClusters > 65536) {
        errorInformation = QObject::tr(
            "Invalid or missing .clu header (nClusters=%1) in: %2")
            .arg(clu.nClusters).arg(path);
        return false;
    }

    spikesByCluster->setSize(nbSpikes);

    for (long k = 0; k < nbSpikes; ++k)
        (*spikesByCluster)(2, k + 1) =
            static_cast<dataType>(clu.ids[static_cast<size_t>(k)]);

    return true;
}


bool Data::loadFeatures(QFile& featureFile, QString& errorInformation)
{
    // Binary .fet format (shared neurofileio::readFetBinary):
    //   int32_t  nDimensions
    //   nSpikes x nDimensions x int64_t  (row-major, last column = timestamp)
    const QString path = featureFile.fileName();
    featureFile.close();

    const neurofileio::FetBinaryFile fet =
        neurofileio::readFetBinary(path.toStdString());
    if (!fet.ok || fet.nFeatures <= 0 || fet.nFeatures > 65536) {
        errorInformation = QObject::tr(
            "Invalid, missing, or unreadable feature file: %1").arg(path);
        return false;
    }
    nbDimensions = fet.nFeatures;

    if (fet.nSpikes != static_cast<int64_t>(nbSpikes)) {
        errorInformation = QObject::tr(
            "Spike count mismatch: .fet has %1 spikes, expected %2")
            .arg(fet.nSpikes).arg(nbSpikes);
        return false;
    }

    features.setSize(nbSpikes, nbDimensions);

    // Stage through int64_t -> dataType, correct whether or not dataType == int64_t.
    const int64_t total = static_cast<int64_t>(nbSpikes) * nbDimensions;
    for (int64_t i = 0; i < total; ++i)
        features[static_cast<size_t>(i)] =
            static_cast<dataType>(fet.values[static_cast<size_t>(i)]);
    return true;
}

// ---------------------------------------------------------------------------
QVector<double> Data::featureVariancesForCluster(int clusterId) const
{
    const int nDim  = nbDimensions;   // last column is timestamp
    const int nFeat = nDim - 1;       // feature columns only
    const int nSpk  = static_cast<int>(nbSpikes);

    // 1. Collect feature-file row indices for spikes in this cluster.
    // spikesByCluster is the sort map: row1 = feature-file row (1-based),
    // row2 = cluster id. The features array uses the feature-file row as its
    // row index, NOT the sorted position s.
    QVector<int> rows;   // 1-based rows into the features array
    rows.reserve(256);
    for (int s = 1; s <= nSpk; ++s){
        if (static_cast<int>((*spikesByCluster)(2, s)) == clusterId)
            rows.append(static_cast<int>((*spikesByCluster)(1, s)));
    }

    if (rows.size() < 2)
        return QVector<double>();

    const double n = static_cast<double>(rows.size());

    // 2. Per-feature mean
    QVector<double> mean(nFeat, 0.0);
    for (int row : rows)
        for (int f = 1; f <= nFeat; ++f)
            mean[f - 1] += static_cast<double>(features(row, f));
    for (int f = 0; f < nFeat; ++f)
        mean[f] /= n;

    // 3. Sample variance (n-1 denominator)
    QVector<double> var(nFeat, 0.0);
    for (int row : rows) {
        for (int f = 1; f <= nFeat; ++f) {
            double d = static_cast<double>(features(row, f)) - mean[f - 1];
            var[f - 1] += d * d;
        }
    }
    for (int f = 0; f < nFeat; ++f)
        var[f] /= (n - 1.0);

    return var;
}

QVector<double> Data::featureVariancesForClusters(const QList<int>& clusterIds) const
{
    if (clusterIds.isEmpty())
        return QVector<double>();

    const int nDim  = nbDimensions;
    const int nFeat = nDim - 1;
    const int nSpk  = static_cast<int>(nbSpikes);

    // Build a fast lookup set
    QSet<int> idSet(clusterIds.begin(), clusterIds.end());

    // Collect feature-file row indices for spikes in the listed clusters.
    // spikesByCluster row1 = feature-file row (1-based), row2 = cluster id.
    QVector<int> rows;
    rows.reserve(256);
    for (int s = 1; s <= nSpk; ++s)
        if (idSet.contains(static_cast<int>((*spikesByCluster)(2, s))))
            rows.append(static_cast<int>((*spikesByCluster)(1, s)));

    if (rows.size() < 2)
        return QVector<double>();

    const double n = static_cast<double>(rows.size());

    // Per-feature mean
    QVector<double> mean(nFeat, 0.0);
    for (int row : rows)
        for (int f = 1; f <= nFeat; ++f)
            mean[f - 1] += static_cast<double>(features(row, f));
    for (int f = 0; f < nFeat; ++f)
        mean[f] /= n;

    // Sample variance (n-1 denominator)
    QVector<double> var(nFeat, 0.0);
    for (int row : rows) {
        for (int f = 1; f <= nFeat; ++f) {
            double d = static_cast<double>(features(row, f)) - mean[f - 1];
            var[f - 1] += d * d;
        }
    }
    for (int f = 0; f < nFeat; ++f)
        var[f] /= (n - 1.0);

    return var;
}

// ---------------------------------------------------------------------------
// Chi-squared survival function — used for L-ratio computation
// Schmitzer-Torbert et al. (2005): L = Σ chi2_sf(d²_mahal, D) / n_cluster
//
// Implementation: regularized upper incomplete gamma Q(a, x) via the
// Lanczos lnGamma + Lentz continued fraction / series method.
// Accurate to ~1e-10 for all a > 0, x >= 0 within 200 iterations.
// ---------------------------------------------------------------------------
static double lnGamma_impl(double z)
{
    // Lanczos approximation (Numerical Recipes g=5, n=6)
    static const double c[6] = {
         76.18009172947146, -86.50532032941677,
         24.01409824083091,  -1.231739572450155,
          0.1208650973866179e-2, -0.5395239384953e-5
    };
    double y = z, x = z;
    double tmp = x + 5.5 - (x + 0.5) * std::log(x + 5.5);
    double ser = 1.000000000190015;
    for (int j = 0; j < 6; ++j) ser += c[j] / ++y;
    return -tmp + std::log(2.5066282746310005 * ser / x);
}

// Series representation of P(a, x)
static double gammaP_series(double a, double x)
{
    if (x <= 0.0) return 0.0;
    double ap = a, del = 1.0 / a, sum = del;
    for (int n = 1; n <= 300; ++n) {
        ap  += 1.0;
        del *= x / ap;
        sum += del;
        if (std::abs(del) < std::abs(sum) * 1e-10) break;
    }
    return sum * std::exp(-x + a * std::log(x) - lnGamma_impl(a));
}

// Continued-fraction representation of Q(a, x) via Lentz method
static double gammaQ_cf(double a, double x)
{
    constexpr double FPMIN = 1e-300;
    double b = x + 1.0 - a, c = 1.0 / FPMIN, d = 1.0 / b, h = d;
    for (int i = 1; i <= 300; ++i) {
        double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;  if (std::abs(d) < FPMIN) d = FPMIN;
        c = b + an / c;  if (std::abs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::abs(del - 1.0) < 1e-10) break;
    }
    return std::exp(-x + a * std::log(x) - lnGamma_impl(a)) * h;
}

/** chi2_sf(x, df) = P(chi²_df > x)  [survival / complementary CDF] */
static double chi2_sf(double x, int df)
{
    if (x <= 0.0) return 1.0;
    if (df <= 0)  return 0.0;
    const double a = 0.5 * df;
    const double y = 0.5 * x;
    return (y < a + 1.0) ? (1.0 - gammaP_series(a, y)) : gammaQ_cf(a, y);
}

// ---------------------------------------------------------------------------
// computeAllCentroids — one pass over all spikes, amortised across snapshots
// ---------------------------------------------------------------------------
QMap<int, QVector<double>> Data::computeAllCentroids() const
{
    const int nSpk  = static_cast<int>(nbSpikes);
    const int nFeat = nbDimensions - 1;     // timestamp is last column

    // Accumulate sum and count per cluster
    QMap<int, QVector<double>> sumMap;
    QMap<int, int>             cntMap;

    for (int s = 1; s <= nSpk; ++s) {
        const int cid = static_cast<int>((*spikesByCluster)(2, s));
        const int row = static_cast<int>((*spikesByCluster)(1, s));

        if (!sumMap.contains(cid)) {
            sumMap[cid] = QVector<double>(nFeat, 0.0);
            cntMap[cid] = 0;
        }
        QVector<double>& acc = sumMap[cid];
        for (int f = 1; f <= nFeat; ++f)
            acc[f - 1] += static_cast<double>(features(row, f));
        ++cntMap[cid];
    }

    QMap<int, QVector<double>> result;
    for (auto it = sumMap.begin(); it != sumMap.end(); ++it) {
        const int cid = it.key();
        const int cnt = cntMap[cid];
        if (cnt == 0) continue;
        QVector<double> centroid = it.value();
        for (double& v : centroid) v /= static_cast<double>(cnt);
        result[cid] = std::move(centroid);
    }
    return result;
}

// ---------------------------------------------------------------------------
// ClusterSnapshot — single-call summary for curation logging
// ---------------------------------------------------------------------------
ClusterSnapshot Data::computeSnapshot(int clusterId,
                                       double isiThreshMs,
                                       const QMap<int, QVector<double>>* allCentroids) const
{
    ClusterSnapshot snap;
    snap.clusterId      = clusterId;
    snap.isiThreshMs    = isiThreshMs;
    snap.samplingRateHz = samplingRate;
    snap.nChannels      = nbChannels;
    snap.nPcaDims       = totalNbOfPCAs();

    const int nSpkTotal = static_cast<int>(nbSpikes);
    if (nSpkTotal == 0)
        return snap;

    // ── F. Global context ─────────────────────────────────────────────────
    snap.nClustersInGroup = clusterInfoMap->count();
    const double totalSpikes = static_cast<double>(nSpkTotal);

    // ── Pass 1: collect (timestamp, row) pairs for this cluster ───────────
    // spikesByCluster row1 = feature-file row (1-based), row2 = cluster id.
    // We keep rows and timestamps paired so we can split by time for drift.
    struct SpikeRef { double ts; int row; };
    QVector<SpikeRef> spikes;
    spikes.reserve(256);

    const double invFs = (samplingRate > 0.0) ? (1.0 / samplingRate) : 0.0;

    for (int s = 1; s <= nSpkTotal; ++s) {
        if (static_cast<int>((*spikesByCluster)(2, s)) == clusterId) {
            const int row = static_cast<int>((*spikesByCluster)(1, s));
            const double ts = static_cast<double>(features(row, nbDimensions)) * invFs;
            spikes.append({ ts, row });
        }
    }

    const int n = spikes.size();
    snap.nSpikes      = static_cast<qint64>(n);
    snap.spikeFraction = (totalSpikes > 0.0) ? (static_cast<double>(n) / totalSpikes) : 0.0;

    if (n == 0)
        return snap;

    // Sort by timestamp (needed for ISI, temporal drift, rate CV)
    std::sort(spikes.begin(), spikes.end(),
              [](const SpikeRef& a, const SpikeRef& b){ return a.ts < b.ts; });

    // ── A. Firing rate ─────────────────────────────────────────────────────
    if (n >= 2) {
        const double span = spikes.last().ts - spikes.first().ts;
        snap.firingRateHz = (span > 0.0) ? (static_cast<double>(n) / span) : 0.0;
    }

    // ── B. ISI distribution ───────────────────────────────────────────────
    if (n >= 2) {
        const int nPairs = n - 1;
        QVector<double> isi(nPairs);            // ISI in ms
        for (int i = 0; i < nPairs; ++i)
            isi[i] = (spikes[i + 1].ts - spikes[i].ts) * 1000.0;

        int nViol3 = 0, nViol1 = 0, nViol2 = 0, nBurst = 0;
        double sumIsi = 0.0, sumSqIsi = 0.0;
        for (double v : isi) {
            if (v < 1.0)  ++nViol1;
            if (v < 2.0)  ++nViol2;
            if (v < isiThreshMs) ++nViol3;
            if (v < 10.0) ++nBurst;
            sumIsi   += v;
            sumSqIsi += v * v;
        }

        snap.isiViolPct    = 100.0 * nViol3 / nPairs;
        snap.isiViolPct1ms = 100.0 * nViol1 / nPairs;
        snap.isiViolPct2ms = 100.0 * nViol2 / nPairs;
        snap.isiBurstPct   = 100.0 * nBurst  / nPairs;

        const double meanIsi = sumIsi / static_cast<double>(nPairs);
        snap.isiMeanMs = meanIsi;

        // Median: partial sort on a copy
        QVector<double> isiSorted = isi;
        const int mid = nPairs / 2;
        std::nth_element(isiSorted.begin(), isiSorted.begin() + mid, isiSorted.end());
        snap.isiMedianMs = (nPairs % 2 == 1)
            ? isiSorted[mid]
            : 0.5 * (isiSorted[mid] + *std::min_element(isiSorted.begin() + mid + 1, isiSorted.end()));

        // CV = std / mean
        if (meanIsi > 0.0) {
            const double varIsi = sumSqIsi / nPairs - meanIsi * meanIsi;
            snap.isiCv = (varIsi > 0.0) ? (std::sqrt(varIsi) / meanIsi) : 0.0;
        }
    }

    // ── E-prep. Temporal rate CV (10 equal-time bins) ─────────────────────
    if (n >= 2) {
        const double t0   = spikes.first().ts;
        const double tEnd = spikes.last().ts;
        const double span = tEnd - t0;
        constexpr int kBins = 10;
        if (span > 0.0) {
            int counts[kBins] = {};
            for (const SpikeRef& sr : spikes) {
                int bin = static_cast<int>((sr.ts - t0) / span * kBins);
                if (bin >= kBins) bin = kBins - 1;
                ++counts[bin];
            }
            double sumC = 0.0, sumSqC = 0.0;
            for (int b = 0; b < kBins; ++b) {
                sumC   += counts[b];
                sumSqC += counts[b] * counts[b];
            }
            const double meanC = sumC / kBins;
            if (meanC > 0.0) {
                const double varC = sumSqC / kBins - meanC * meanC;
                snap.temporalRateCv = (varC > 0.0) ? (std::sqrt(varC) / meanC) : 0.0;
            }
        }
    }

    // ── Pass 2: per-feature statistics (mean, variance, skewness, kurtosis)
    //           and temporal drift ─────────────────────────────────────────
    const int nFeat = nbDimensions - 1;
    if (n >= 2 && nFeat > 0) {
        const double nd = static_cast<double>(n);

        // ── 2a. Feature means ─────────────────────────────────────────────
        QVector<double> mean(nFeat, 0.0);
        for (const SpikeRef& sr : spikes)
            for (int f = 1; f <= nFeat; ++f)
                mean[f - 1] += static_cast<double>(features(sr.row, f));
        for (int f = 0; f < nFeat; ++f)
            mean[f] /= nd;

        // ── 2b. Central moments: 2nd (variance), 3rd (skewness), 4th (kurtosis)
        QVector<double> m2(nFeat, 0.0), m3(nFeat, 0.0), m4(nFeat, 0.0);
        for (const SpikeRef& sr : spikes) {
            for (int f = 1; f <= nFeat; ++f) {
                const double d  = static_cast<double>(features(sr.row, f)) - mean[f - 1];
                const double d2 = d * d;
                m2[f - 1] += d2;
                m3[f - 1] += d2 * d;
                m4[f - 1] += d2 * d2;
            }
        }
        const double nMinus1 = static_cast<double>(n - 1);
        for (int f = 0; f < nFeat; ++f) {
            m2[f] /= nMinus1;   // sample variance
            m3[f] /= nd;        // population 3rd central moment
            m4[f] /= nd;        // population 4th central moment
        }

        // ── 2c. Aggregate variance metrics ────────────────────────────────
        double sumVar = 0.0, sumVarSq = 0.0;
        double vMax = m2[0], vMin = m2[0];
        for (double v : m2) {
            sumVar   += v;
            sumVarSq += v * v;
            if (v > vMax) vMax = v;
            if (v < vMin) vMin = v;
        }
        snap.featVarMean      = sumVar / static_cast<double>(nFeat);
        snap.featVarFrobenius = std::sqrt(sumVarSq);
        snap.driftRatio       = (vMin > 0.0) ? (vMax / vMin) : 0.0;

        QVector<double> sortedVar = m2;
        std::sort(sortedVar.begin(), sortedVar.end(), std::greater<double>());
        const int top = std::min(3, static_cast<int>(sortedVar.size()));
        double topSum = 0.0;
        for (int i = 0; i < top; ++i) topSum += sortedVar[i];
        snap.featVarTop3Mean = topSum / static_cast<double>(top);

        const int nLog = std::min(nFeat, ClusterSnapshot::kMaxLoggedDims);
        snap.featVarDims.resize(nLog);
        for (int f = 0; f < nLog; ++f) snap.featVarDims[f] = m2[f];

        // ── 2d. Skewness and kurtosis per dimension ────────────────────────
        double maxAbsSkew = 0.0, sumKurt = 0.0, maxKurt = -std::numeric_limits<double>::max();
        for (int f = 0; f < nFeat; ++f) {
            const double sigma2 = m2[f];
            if (sigma2 > 0.0) {
                const double sigma  = std::sqrt(sigma2);
                const double sigma3 = sigma2 * sigma;
                const double skew   = m3[f] / sigma3;
                const double kurt   = m4[f] / (sigma2 * sigma2) - 3.0; // excess
                if (std::abs(skew) > maxAbsSkew) maxAbsSkew = std::abs(skew);
                sumKurt += kurt;
                if (kurt > maxKurt) maxKurt = kurt;
            }
        }
        snap.featSkewnessMax  = maxAbsSkew;
        snap.featKurtosisMean = sumKurt / static_cast<double>(nFeat);
        snap.featKurtosisMax  = (maxKurt > -std::numeric_limits<double>::max()) ? maxKurt : 0.0;

        // ── 2e. Temporal drift: first-half vs second-half centroid distance
        if (n >= 4) {
            const int half = n / 2;
            // First-half centroid
            QVector<double> cEarly(nFeat, 0.0);
            for (int i = 0; i < half; ++i)
                for (int f = 1; f <= nFeat; ++f)
                    cEarly[f - 1] += static_cast<double>(features(spikes[i].row, f));
            for (double& v : cEarly) v /= static_cast<double>(half);

            // Second-half centroid
            QVector<double> cLate(nFeat, 0.0);
            const int nLate = n - half;
            for (int i = half; i < n; ++i)
                for (int f = 1; f <= nFeat; ++f)
                    cLate[f - 1] += static_cast<double>(features(spikes[i].row, f));
            for (double& v : cLate) v /= static_cast<double>(nLate);

            // L2 distance between centroids
            double distSq = 0.0;
            for (int f = 0; f < nFeat; ++f) {
                const double d = cLate[f] - cEarly[f];
                distSq += d * d;
            }
            const double dist = std::sqrt(distSq);
            snap.temporalDriftIndex = (snap.featVarFrobenius > 0.0)
                                      ? (dist / snap.featVarFrobenius)
                                      : 0.0;
        }
    }

    // ── G. Nearest-cluster isolation ──────────────────────────────────────
    if (allCentroids && allCentroids->size() > 1) {
        // Own centroid — recompute cheaply from mean (already have it above,
        // but it's local; recompute from allCentroids which is already O(1) lookup)
        const QVector<double>* ownCentroid = nullptr;
        auto it = allCentroids->constFind(clusterId);
        if (it != allCentroids->constEnd())
            ownCentroid = &it.value();

        if (ownCentroid && !ownCentroid->isEmpty()) {
            const int D = ownCentroid->size();
            double minDist = std::numeric_limits<double>::max();
            int    nearId  = -1;

            for (auto jt = allCentroids->constBegin();
                       jt != allCentroids->constEnd(); ++jt) {
                if (jt.key() == clusterId) continue;
                const QVector<double>& other = jt.value();
                if (other.size() != D) continue;
                double distSq = 0.0;
                for (int f = 0; f < D; ++f) {
                    const double d = (*ownCentroid)[f] - other[f];
                    distSq += d * d;
                }
                if (distSq < minDist) {
                    minDist = distSq;
                    nearId  = jt.key();
                }
            }
            if (nearId >= 0) {
                const double dist = std::sqrt(minDist);
                snap.nearestClusterId        = nearId;
                snap.nearestCentroidDist     = dist;
                snap.nearestCentroidDistNorm = (snap.featVarFrobenius > 0.0)
                                               ? (dist / snap.featVarFrobenius)
                                               : 0.0;
            }
        }
    }

    // ── H. Waveform morphology (conditional — only if mean is cached) ──────
    {
        const int     clusterIdInt    = clusterId;
        const QString clusterIdString = QString::number(clusterId);

        if (waveformStatusMap.contains(clusterIdInt) &&
            waveformStatusMap.value(clusterIdInt).sampleMeanStatus() == READY &&
            waveformDict.contains(clusterIdString))
        {
            const Waveforms* wf    = waveformDict.value(clusterIdString);
            const int        nSamp = nbSamplesInWaveform;
            const int        nChan = nbChannels;
            const int        peak0 = peakPositionInWaveform - 1;  // 0-based
            // Mean waveform layout: index = sample * nChan + channel
            // getSampleMean returns dataType (long int); values are in ADC units.

            // Per-channel peak-to-trough amplitudes
            QVector<double> chanAmp(nChan, 0.0);
            QVector<int>    chanPeakIdx(nChan, peak0);
            QVector<int>    chanTroughIdx(nChan, peak0);

            for (int ch = 0; ch < nChan; ++ch) {
                double chMax = static_cast<double>(wf->getSampleMean(0 * nChan + ch));
                double chMin = chMax;
                int    maxI  = 0, minI = 0;
                for (int s = 1; s < nSamp; ++s) {
                    const double v = static_cast<double>(wf->getSampleMean(s * nChan + ch));
                    if (v > chMax) { chMax = v; maxI = s; }
                    if (v < chMin) { chMin = v; minI = s; }
                }
                chanAmp[ch]       = chMax - chMin;
                chanPeakIdx[ch]   = maxI;
                chanTroughIdx[ch] = minI;
            }

            // Best channel = maximum peak-to-trough
            int    bestCh  = 0;
            double bestAmp = chanAmp[0];
            for (int ch = 1; ch < nChan; ++ch)
                if (chanAmp[ch] > bestAmp) { bestAmp = chanAmp[ch]; bestCh = ch; }

            snap.waveformAvailable = true;
            snap.waveformPeakAmp   = bestAmp;
            snap.waveformChanSpread = 0;
            if (bestAmp > 0.0)
                for (int ch = 0; ch < nChan; ++ch)
                    if (chanAmp[ch] >= 0.25 * bestAmp)
                        ++snap.waveformChanSpread;

            // Trough-to-repolarisation width on best channel
            snap.waveformWidthSamp = std::abs(chanPeakIdx[bestCh] - chanTroughIdx[bestCh]);

            // Asymmetry: (posPeak + negTrough) / (posPeak − negTrough)
            // Extract actual signed peak and trough for the best channel.
            double posVal = static_cast<double>(wf->getSampleMean(chanPeakIdx[bestCh]   * nChan + bestCh));
            double negVal = static_cast<double>(wf->getSampleMean(chanTroughIdx[bestCh] * nChan + bestCh));
            const double denom = posVal - negVal;
            if (std::abs(denom) > 1e-9)
                snap.waveformAsymmetry = (posVal + negVal) / denom;

            // SNR: peak-to-trough / (2 × baseline RMS)
            // Baseline = first 4 samples (pre-spike window, before any deflection)
            const int nBase = std::min(4, nSamp);
            double baseRms = 0.0;
            for (int s = 0; s < nBase; ++s) {
                const double v = static_cast<double>(wf->getSampleMean(s * nChan + bestCh));
                baseRms += v * v;
            }
            baseRms = std::sqrt(baseRms / static_cast<double>(nBase));
            if (baseRms > 0.0)
                snap.waveformSnr = bestAmp / (2.0 * baseRms);
        }
    }

    // ── J. L-ratio and isolation distance ────────────────────────────────
    // Diagonal Mahalanobis approximation — valid because PCA features are
    // by construction uncorrelated, so the off-diagonal covariance is zero.
    // Reference: Schmitzer-Torbert et al. (2005) Neuroscience 131(1).
    if (n >= 2 && nFeat > 0 && snap.featVarFrobenius > 0.0) {
        // Build inverse-variance vector (1/σ²_f) with epsilon guard.
        QVector<double> invVar(nFeat);
        bool anyNonzero = false;
        for (int f = 0; f < nFeat; ++f) {
            const double v = (f < snap.featVarDims.size()) ? snap.featVarDims[f] : snap.featVarMean;
            invVar[f] = (v > 1e-12) ? (1.0 / v) : 0.0;
            if (invVar[f] > 0.0) anyNonzero = true;
        }

        if (anyNonzero) {
            // Centroid of this cluster (reuse the mean we computed above —
            // retrieve it from allCentroids if available, else recompute).
            QVector<double> centroid(nFeat, 0.0);
            if (allCentroids && allCentroids->contains(clusterId)) {
                centroid = allCentroids->value(clusterId);
            } else {
                for (const SpikeRef& sr : spikes)
                    for (int f = 1; f <= nFeat; ++f)
                        centroid[f - 1] += static_cast<double>(features(sr.row, f));
                for (double& v : centroid) v /= static_cast<double>(n);
            }

            // Pass over ALL spikes: accumulate L-ratio from non-cluster spikes
            // and collect their Mahalanobis distances for isolation distance.
            QVector<double> nonClusterDists;
            nonClusterDists.reserve(std::max(0, nSpkTotal - n));

            double lSum = 0.0;
            for (int s = 1; s <= nSpkTotal; ++s) {
                if (static_cast<int>((*spikesByCluster)(2, s)) == clusterId)
                    continue;   // skip own spikes

                const int row = static_cast<int>((*spikesByCluster)(1, s));
                double d2 = 0.0;
                for (int f = 1; f <= nFeat; ++f) {
                    const double d = static_cast<double>(features(row, f)) - centroid[f - 1];
                    d2 += d * d * invVar[f - 1];
                }
                lSum += chi2_sf(d2, nFeat);
                nonClusterDists.append(d2);
            }

            snap.lRatio = (n > 0) ? (lSum / static_cast<double>(n)) : 0.0;

            // Isolation distance: Mahalanobis distance to the K-th nearest
            // non-cluster spike (K = n_cluster_spikes).
            if (static_cast<int>(nonClusterDists.size()) >= n) {
                std::nth_element(nonClusterDists.begin(),
                                 nonClusterDists.begin() + n - 1,
                                 nonClusterDists.end());
                snap.isolationDist = std::sqrt(nonClusterDists[n - 1]);
            }
        }
    }

    // ── K. Recording-relative temporal position ───────────────────────────
    if (n >= 1) {
        const double maxTs = static_cast<double>(maxDimension(nbDimensions));
        if (maxTs > 0.0) {
            // spikes is sorted by timestamp; first/last are already computed.
            snap.tFirstRel = (spikes.first().ts * samplingRate) / maxTs;
            snap.tLastRel  = (spikes.last().ts  * samplingRate) / maxTs;
        }
    }

    return snap;
}



bool Data::initialize(QFile& featureFile,QFile& clusterFile,long spkFileLength,QString& errorInformation){
    if(!loadClusters(clusterFile,spkFileLength,errorInformation))
        return false;
    if(!loadFeatures(featureFile,errorInformation))
        return false;

    //Fill the first row of spikesByCluster with the row index of the spike,
    //knowing that for the moment the elements of the table are sorted by spike order.
    for(dataType i = 1; i <= nbSpikes; ++ i) (*spikesByCluster)(1,i) = i;

    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);
    QMap<dataType,dataType> clusters;
    dataType max = nbSpikes + 1;
    //Count the number of spikes for each cluster.
    for(dataType i = 1; i < max; ++i) {
        dataType clusterId = (*spikesByCluster)(2,i);
        clusters[clusterId]++;
    }

    //Initialize positions, for each clusterId the value is set to the position of the first spike.
    //Add the cluster user information.
    //The clusters will be sorted by clusterId in spikesByCluster. Initialize clusterInfoMap.
    QMap<dataType,dataType> positions;
    QMap<dataType,dataType>::Iterator iterator;
    int index = 1;
    for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator){
        dataType clusterId = iterator.key();
        positions[clusterId] =  index;
        ClusterUserInformation vClusterUserInformation = clusterUserInformationMap[static_cast<int>(clusterId)];

        clusterInfoMap->insert(clusterId,ClusterInfo(index,iterator.value(),vClusterUserInformation.getStructure(),vClusterUserInformation.getType(),vClusterUserInformation.getId(),vClusterUserInformation.getQuality(),vClusterUserInformation.getNotes()));

        index += iterator.value();
    }

    //Reset the clusterUserInformationMap, which is only needed to store the information before writing it to the YAML parameter file.
    clusterUserInformationMap.clear();

    //Fill tmp with the data sorted by cluster and by time (<=> position in the fet file)
    for(dataType i = 1; i < max; ++i){
        dataType clusterId = (*spikesByCluster)(2,i);
        dataType position = positions[clusterId];
        dataType positionInFet = (*spikesByCluster)(1,i);
        (*spikesByClusterTemp)(1,position) = positionInFet;
        (*spikesByClusterTemp)(2,position) = clusterId;
        positions[clusterId] ++;
    }

    //Delete spikesByCluster and assign to the pointer the value of spikesByClusterTemp;
    delete spikesByCluster;
    spikesByCluster = nullptr;
    spikesByCluster =  spikesByClusterTemp;


    //Calculate the minimum and maximum for each dimension and store them in
    //dimensionMinima and dimensionMaxima respectively
    QList<int> modifiedClusters;
    minMaxDimensionCalculation(modifiedClusters);
    return true;
}



bool Data::initialize(QFile& featureFile,QFile& clusterFile,long spkFileLength,const QString& spkFileName,QFile& parXFile,QFile& parFile,QString& errorInformation){
    this->spkFileName = spkFileName;
    if(!configure(parXFile, parFile,errorInformation))
        return false;

    if(!initialize(featureFile,clusterFile,spkFileLength,errorInformation))
        return false;

    return true;
}

bool Data::initialize(QFile& featureFile,QFile& clusterFile,long spkFileLength,const QString& spkFileName,QFile& parFile,int electrodeGroupID,QString& errorInformation){
    this->spkFileName = spkFileName;

    if(!configure(parFile,electrodeGroupID,errorInformation))
        return false;
    if(!initialize(featureFile,clusterFile,spkFileLength,errorInformation))
        return false;

    return true;
}

bool Data::initialize(QFile& featureFile,long spkFileLength,QString& errorInformation){

    //Determine the number of spikes using the length of the binary spike file
    int sampleSize;
    switch(nbBits){
    case 12:
        sampleSize = 2;
        isTwoBytesRecording = true;
        break;
    case 14:
        sampleSize = 2;
        isTwoBytesRecording = true;
        break;
    case 16:
        sampleSize = 2;
        isTwoBytesRecording = true;
        break;
    case 32:
        sampleSize = 4;
        isTwoBytesRecording = false;
        break;
    default:   //not implemented
        errorInformation = QObject::tr("The number of bits is not supported.");
        return false;
    }
    nbSpikes =  spkFileLength / static_cast<long>(static_cast<long>(nbChannels) * static_cast<long>(nbSamplesInWaveform) * static_cast<long>(sampleSize));
    spikesByCluster->setSize(nbSpikes);

    //As the cluster file does not exist assign all the spikes to cluster 1.
    for(dataType i = 1; i <= nbSpikes; ++ i)
        (*spikesByCluster)(2,i) = 1;

    if(!loadFeatures(featureFile,errorInformation))
        return false;

    //Fill the first row of spikesByCluster with the row index of the spike,
    //knowing that for the moment the elements of the table are sorted by spike order.
    for(dataType i = 1; i <= nbSpikes; ++ i)
        (*spikesByCluster)(1,i) = i;

    clusterInfoMap->insert(1, ClusterInfo(1,nbSpikes));

    //Calculate the minimum and maximum for each dimension and store them in
    //dimensionMinima and dimensionMaxima respectively
    QList<int> modifiedClusters;
    minMaxDimensionCalculation(modifiedClusters);
    return true;
}

bool Data::initialize(QFile& featureFile,long spkFileLength,const QString &spkFileName,QFile& parXFile,QFile& parFile,QString& errorInformation){
    this->spkFileName = spkFileName;
    if(!configure(parXFile, parFile,errorInformation))
        return false;
    if(!initialize(featureFile,spkFileLength,errorInformation)){
        return false;
    }

    return true;
}

bool Data::initialize(QFile& featureFile,long spkFileLength,const QString& spkFileName,QFile& parFile,int electrodeGroupID,QString& errorInformation){
    this->spkFileName = spkFileName;

    if(!configure(parFile,electrodeGroupID,errorInformation))
        return false;
    if(!initialize(featureFile,spkFileLength,errorInformation))
        return false;

    return true;
}

void Data::minMaxDimensionCalculation(const QList<int>& modifiedClusters){
    //If an undo or redo has started or the cluster 0 has been changed again, do not do any calculation, it will be done on the new data.
    if(undoRedoInProcess || clusterZeroJustModified) return;

    //The mutex protects spikesByCluster and clusterInfoMap so that only one thread can
    //access them at the time.  Both the spike table AND the cluster map are
    //snapshotted here, under the lock, and the long scan below reads ONLY the
    //snapshots.  This matters because the GUI thread can replace the live
    //spikesByCluster / clusterInfoMap pointers (prepareUndo on a cluster
    //reassignment, or undo / redo) while this scan is running.  The previous
    //code snapshotted the spike table into a block-scoped local that was
    //destroyed immediately, then read the live (*spikesByCluster) in the inner
    //loop using offsets taken from the clusterInfoMap snapshot — so after a
    //concurrent pointer swap the offsets and the data came from different
    //tables, giving a stale result or an out-of-bounds read (the swapped-in
    //table generally has a different size/layout).  The undoRedoInProcess /
    //clusterZeroJustModified early-returns only narrow that window; they are
    //never checked inside the per-spike loop, which is long for a high-rate
    //cluster.  Reading a private copy closes the race regardless of timing.
    ClusterInfoMap clusterInfoMapTemp;
    ClusterInfoMap::Iterator iterator;
    std::unique_ptr<SortableTable> spikesSnapshot;
    {
        QMutexLocker lk(&mutex);
        spikesSnapshot = std::make_unique<SortableTable>(*spikesByCluster);
        for(iterator = clusterInfoMap->begin(); iterator != clusterInfoMap->end(); ++iterator){
            clusterInfoMapTemp.insert(iterator.key(),iterator.value());
        }
    }
    const SortableTable& spikesByClusterTemp = *spikesSnapshot;

    Array<dataType> dimensionMaximaTemp(nbDimensions,1);
    Array<dataType> dimensionMinimaTemp(nbDimensions,1);
    dataType max,min;

    bool init = false;
    if(clustersGivingMinimum.isEmpty()){
        init = true;
        for(int i = 0; i<nbDimensions; ++i){
            clustersGivingMinimum.append(0);
            clustersGivingMaximum.append(0);
        }
    }

    //Calculate the minimum and maximum for each dimension and store them in
    //dimensionMinima and dimensionMaxima respectively. The cluster 0 is not taken into account.
    for(int dimension = 1; dimension<nbDimensions; ++dimension){
        //If an undo or redo has started or the cluster 0 has been changed again, stop the calculation.
        if(undoRedoInProcess || clusterZeroJustModified) return;

        max = min = features(1,dimension);

        dataType clusterIdMin = 0;
        dataType clusterIdMax = 0;

        if(!(modifiedClusters.contains(clustersGivingMinimum[dimension - 1]) || modifiedClusters.contains(clustersGivingMaximum[dimension - 1]))
                && !init && !modifiedClusters.isEmpty() && !modifiedClusters.contains(0)){
            dimensionMinimaTemp(dimension,1) = dimensionMinima(dimension,1);
            dimensionMaximaTemp(dimension,1) = dimensionMaxima(dimension,1);
            continue;
        }

        //NB: the iterator iterates on the items sorted by their key
        for(iterator = clusterInfoMapTemp.begin(); iterator != clusterInfoMapTemp.end(); ++iterator){
            dataType clusterId = iterator.key();

            if(clusterId == 0)
                continue;
            dataType firstSpikePosition = iterator.value().firstSpikePosition();
            dataType nbSpikesOfCluster = iterator.value().nbSpikes();
            dataType lastPosition =  firstSpikePosition + nbSpikesOfCluster;

            for(dataType i = firstSpikePosition; i < (lastPosition);++i){
                dataType spikePosition = spikesByClusterTemp(1,i);
                dataType currentSpike = features(spikePosition,dimension);

                if(currentSpike < min){
                    min = currentSpike;
                    clusterIdMin = clusterId;
                }
                if(currentSpike > max){
                    clusterIdMax = clusterId;
                    max = currentSpike;
                }

            }

            //If an undo or redo has started or the cluster 0 has been changed again, stop the calculation.
            if(undoRedoInProcess || clusterZeroJustModified) return;
        }
        dimensionMinimaTemp(dimension,1) = min;
        dimensionMaximaTemp(dimension,1) = max;
        clustersGivingMinimum[dimension - 1] = clusterIdMin;
        clustersGivingMaximum[dimension - 1] = clusterIdMax;
    }

    // Time dimension: scan all spikes for the true min and max timestamp.
    // The old shortcut used features(1,...) and features(nbSpikes,...) which
    // are the globally first/last spikes by feature-table index, not by
    // timestamp — causing stale world-window bounds after a nudge.
    {
        dataType tsMin = features(1, nbDimensions);
        dataType tsMax = tsMin;
        for (long k = 2; k <= nbSpikes; ++k) {
            const dataType ts = features(k, nbDimensions);
            if (ts < tsMin) tsMin = ts;
            if (ts > tsMax) tsMax = ts;
        }
        dimensionMinimaTemp(nbDimensions, 1) = tsMin;
        dimensionMaximaTemp(nbDimensions, 1) = tsMax;
    }

    //Update dimensionMinima and dimensionMaxima
    {
        QMutexLocker lk(&mutex);
    dimensionMaxima.setSize(nbDimensions,1);
    dimensionMinima.setSize(nbDimensions,1);
    for(int i = 1; i<=nbDimensions;++i){
        dimensionMinima(i,1) = dimensionMinimaTemp(i,1);
        dimensionMaxima(i,1) = dimensionMaximaTemp(i,1);
    }
    }


}


dataType Data::createNewCluster(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY, QList <int>& fromClusters,QList <int>& emptyClusters){
    //Set the new cluster number to the biggest existing number plus one
    dataType newClusterId = nextFreeClusterId();
    dataType nbSpikesInNewCluster = 0;

    //Create the variables to store the number of spikes and the position of the last spike
    //for each cluster contributing to the new cluster. This will be used to sort the new cluster.
    QList<dataType> lastPositions;
    QList<dataType> nbOfspikes;

    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    //Iteration on the clusters
    ClusterInfoMap::Iterator iterator;
    dataType upperInsertionIndex = 1;
    dataType lowerInsertionIndex = nbSpikes;

    //NB: the iterator iterates on the items sorted by their key
    for(iterator = clusterInfoMap->begin(); iterator != clusterInfoMap->end(); ++iterator){
        dataType firstSpikePosition = iterator.value().firstSpikePosition();
        dataType nbSpikesOfCluster = iterator.value().nbSpikes();
        dataType clusterId = iterator.key();

        //if clustersOfOrigin does not contains the current cluster, this cluster is let unchanged
        //and its information is simply copy as is from spikesByCluster to spikesByClusterTemp
        if(!clustersOfOrigin.contains(static_cast<int>(clusterId))){
            //copy the 2 rows of spikesByCluster for the given cluster
            memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            //Construct the new clusterInfoMap
            clusterInfoMapTemp->insert(clusterId,ClusterInfo(upperInsertionIndex,nbSpikesOfCluster,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes()));
            upperInsertionIndex += nbSpikesOfCluster;
        }
        //Now deal with the clusters which may contain spikes to add to the new cluster
        //<=> spike in the region.
        else{
            //Store the last spike position for the current cluster
            lastPositions.append(nbSpikesInNewCluster + 1);

            dataType updatedClusterPosition = 0;
            bool existUntouchSpike = true;
            dataType newNbSpikesOfCluster = nbSpikesOfCluster;
            dataType lastPosition =  firstSpikePosition + nbSpikesOfCluster;
            dataType lastPositionLessOne =  lastPosition -1;

            for(dataType i = firstSpikePosition; i < lastPosition;++i){
                dataType featuresRowIndex = static_cast<dataType>((*spikesByCluster)(1,i));
                if(region.contains(
                            QPoint(static_cast<dataType>(features(featuresRowIndex,dimensionX)),
                                   static_cast<dataType>(features(featuresRowIndex,dimensionY))))){
                    //Add the spike to the new cluster <=> add the row index at the end of spikesByCluster at the lowerInsertionIndex
                    (*spikesByClusterTemp)(1,lowerInsertionIndex) = featuresRowIndex;
                    --lowerInsertionIndex;
                    ++nbSpikesInNewCluster;
                    --newNbSpikesOfCluster;
                }
                else{
                    //Update the position of this cluster
                    if(existUntouchSpike){
                        existUntouchSpike = false;
                        updatedClusterPosition = upperInsertionIndex;
                    }
                    //Keep the spike in the current cluster <=> add the row index and the cluster number at the top of spikesByCluster at the upperInsertionIndex
                    (*spikesByClusterTemp)(1,upperInsertionIndex) = featuresRowIndex;
                    (*spikesByClusterTemp)(2,upperInsertionIndex) = (*spikesByCluster)(2,i);
                    ++upperInsertionIndex;
                }

                if(i == (lastPositionLessOne)){
                    if(newNbSpikesOfCluster < nbSpikesOfCluster){
                        //Store the number of spikes coming from the current cluster
                        nbOfspikes.append(nbSpikesOfCluster - newNbSpikesOfCluster);

                        //update fromClusters if at least one spike from that cluster was in the region
                        fromClusters.append(static_cast<int>(clusterId));
                    }
                    //No spike has been move to the new cluster, remove the last entry in lastPositions.
                    else lastPositions.pop_back();

                    //Construct the insertion of the current cluster in the new clusterInfoMap if
                    // the number of spikes is more than zero
                    if(newNbSpikesOfCluster >0)clusterInfoMapTemp->insert(clusterId,ClusterInfo(updatedClusterPosition,newNbSpikesOfCluster,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes()));
                    else emptyClusters.append(static_cast<int>(clusterId));
                }
            }
        }
    }

    //If some spikes have been taken from the cluster 0, the max and min
    // dimensions have to be recalculated. If minMaxThread is running, clusterZeroJustModified will
    //inform it that it has to stop (the computation will be done again on the new data).
    if(fromClusters.contains(0)) clusterZeroJustModified = true;

    //For the new cluster, only the row index has been inserted in spikesByClusterTemp,
    //now the cluster number is updated at once for all the spikes of the new cluster
    dataType startInsertion =  lowerInsertionIndex + 1;
    for(dataType i = 0; i<nbSpikesInNewCluster;++i) (*spikesByClusterTemp)(2,startInsertion + i) = newClusterId;

    if(nbSpikesInNewCluster > 0){
        //Construct the insertion of the new cluster in the new clusterInfoMap
        clusterInfoMapTemp->insert(newClusterId,ClusterInfo(lowerInsertionIndex + 1,nbSpikesInNewCluster));

        //Sort the spikes of the newly created cluster.
        sortCluster(clusterInfoMapTemp,spikesByClusterTemp,newClusterId,lastPositions,nbOfspikes,-1);

        //Get the list of clusters before applying the changes, this will be used in the clean
        //of the correlation.
        QList<dataType> currentClusterList = clusterIds();

        //Deal with the undo mechanism
        bool dimChanged = fromClusters.contains(0);
        prepareUndo(spikesByClusterTemp,clusterInfoMapTemp,dimChanged);

        //If some spikes have been taken from the cluster 0, the max and min
        // dimensions have to be recalculated. If minMaxThread is running, the call
        //will wait until it finishes before starting the thread again.
        if(dimChanged){
            //If the minMaxThread has not finish, wait until it is done
            minMaxThread->wait();
            //Reset the flag to false so the minMaxThread can do the computation
            clusterZeroJustModified = false;
            minMaxThread->setModifiedClusters(fromClusters);
            minMaxThread->start();
        }

        //Remove the waveform and correlation data for the clusters which gave the spikes for the new cluster.
        //if there is not a thread working with them,otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
        // and the thread will remove it.
        QList<int>::iterator iterator;
        for(iterator = fromClusters.begin(); iterator != fromClusters.end(); ++iterator){
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(*iterator)){
                if(!waveformStatusMap[*iterator].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*iterator));
                    waveformStatusMap.remove(*iterator);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[*iterator];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(*iterator,waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(static_cast<dataType>(*iterator))) cleanCorrelation(static_cast<dataType>(*iterator),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(*iterator),true);
                }
            }
        }

        return newClusterId;
    }
    //return 0 if no new cluster have been created
    //safe as cluster 0 (artifact) can never be created that way
    else return 0;
}

QMap<int,int> Data::createNewClusters(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY,QList <int>& emptyClusters){
    QMap<int,int> fromToClusterIds;
    QMap<int,int> fromToNewClusterIds;
    ClusterInfoMap clusterInfoMapTemp; //used in the first part of the function
    int nbNewClusters = 0;
    int nbMaxNewClusters = clustersOfOrigin.size();

    //Set the new cluster number to the biggest existing number plus nbMaxNewClusters.
    //The number will be decremented before being used, and the number will be corrected at the end once the
    //number of really created clusters will be known (the biggest clusterId is store at the bottom of spikesByClusterTemp).
    dataType newClusterId = highestClusterId() + nbMaxNewClusters;

    //Create the variables to store the number of spikes and the position of the first spike
    //for each cluster contributing to a new cluster. This will be used to sort the new clusters.
    QList< QList<dataType> > firstPositions;
    QList< QList<dataType> > nbOfspikes;

    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Iteration on the clusters
    ClusterInfoMap::Iterator iterator;
    dataType upperInsertionIndex = 1;
    dataType lowerInsertionIndex = nbSpikes;

    //NB: the iterator iterates on the items sorted by their key
    for(iterator = clusterInfoMap->begin(); iterator != clusterInfoMap->end(); ++iterator){
        dataType firstSpikePosition = iterator.value().firstSpikePosition();
        dataType nbSpikesOfCluster = iterator.value().nbSpikes();
        dataType clusterId = iterator.key();

        //if clustersOfOrigin does not contains the current cluster, this cluster is let unchanged
        //and its information is simply copy as is from spikesByCluster to spikesByClusterTemp
        if(!clustersOfOrigin.contains(static_cast<int>(clusterId))){
            //copy the 2 rows of spikesByCluster for the given cluster
            memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            //Construct the new clusterInfoMap
            clusterInfoMapTemp.insert(clusterId,ClusterInfo(upperInsertionIndex,nbSpikesOfCluster,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes()));
            upperInsertionIndex += nbSpikesOfCluster;
        }
        //Now deal with the clusters which may contain spikes to add to a new cluster <=> spike in the region.
        //If a cluster contain spikes in the region, a new cluster is created
        else{
            dataType nbSpikesInNewCluster = 0;
            dataType updatedClusterPosition = 0;
            bool existUntouchSpike = true;
            dataType newNbSpikesOfCluster = nbSpikesOfCluster;
            dataType lastPosition =  firstSpikePosition + nbSpikesOfCluster;
            dataType lastPositionLessOne =  lastPosition -1;

            //Create the variables to store the number of spikes and the position of the last spike
            //for the new cluster contributing to the new cluster. This will be used to sort the new cluster.
            QList<dataType> currentFirstPositions;
            QList<dataType> currentNbOfspikes;

            //Store the last spike position for the current cluster.
            currentFirstPositions.append(1);

            for(dataType i = firstSpikePosition; i < lastPosition;++i){
                dataType featuresRowIndex = (*spikesByCluster)(1,i);
                if(region.contains(
                            QPoint(features(featuresRowIndex,dimensionX),
                                   features(featuresRowIndex,dimensionY)))){
                    //Add the spike to the new cluster <=> add the row index at the end of spikesByCluster at the lowerInsertionIndex
                    (*spikesByClusterTemp)(1,lowerInsertionIndex) = featuresRowIndex;

                    --lowerInsertionIndex;
                    ++nbSpikesInNewCluster;
                    newNbSpikesOfCluster --;
                }
                else{
                    //Update the position of this cluster
                    if(existUntouchSpike){
                        existUntouchSpike = false;
                        updatedClusterPosition = upperInsertionIndex;
                    }
                    //Keep the spike in the current cluster <=> add the row index and the cluster number at the top of spikesByCluster at the upperInsertionIndex
                    (*spikesByClusterTemp)(1,upperInsertionIndex) = featuresRowIndex;
                    (*spikesByClusterTemp)(2,upperInsertionIndex) = (*spikesByCluster)(2,i);
                    ++upperInsertionIndex;
                }

                if(i == (lastPositionLessOne)){
                    //Construct the insertion of the current cluster in the new clusterInfoMap if
                    // the number of spikes is more than zero.
                    //Copy the spikes back to spikesByCluster.
                    if(newNbSpikesOfCluster > 0){
                        clusterInfoMapTemp.insert(clusterId,ClusterInfo(updatedClusterPosition,newNbSpikesOfCluster,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes()));
                    }
                    else emptyClusters.append(static_cast<int>(clusterId));

                    //If at least one spike from that cluster was in the region, a new cluster will be created
                    if(nbSpikesInNewCluster > 0){
                        //Store the number of spikes of the new cluster.
                        currentNbOfspikes.append(nbSpikesInNewCluster);
                        //Store the information to later sort the new cluster.
                        firstPositions.append(currentFirstPositions);
                        nbOfspikes.append(currentNbOfspikes);


                        ++nbNewClusters;
                        //update fromToClusterIds
                        fromToClusterIds.insert(static_cast<int>(clusterId),static_cast<int>(newClusterId));
                        //Construct the insertion of the newly created cluster with a temporarily clusterId
                        clusterInfoMapTemp.insert(newClusterId,ClusterInfo(lowerInsertionIndex + 1,nbSpikesInNewCluster));
                        //decrement the cluster id for the next cluster to be created
                        --newClusterId;
                    }
                }
            }
        }
    }

    //If some spikes have been taken from the cluster 0, the max and min
    // dimensions have to be recalculated. If minMaxThread is running, clusterZeroJustModified will
    //inform it that it has to stop (the computation will be done again on the new data).
    if(fromToClusterIds.contains(0)) clusterZeroJustModified = true;


    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp2 = new ClusterInfoMap(clusterInfoMapTemp);

    //Update the information on the new clusters,renumber them with the good number now that
    //we know how many clusters have been created.
    if(nbNewClusters > 0){
        int shift =  nbMaxNewClusters - nbNewClusters;

        QList<int> keys = fromToClusterIds.keys();

        //Iteration on the fromToClusterIds
        for(int i = keys.size() - 1; i >= 0; --i){
            int oldCluster = keys[i];
            int clusterToCreate = fromToClusterIds[oldCluster];

            int newClusterId = clusterToCreate - shift;
            fromToNewClusterIds.insert(oldCluster,newClusterId);
            ClusterInfo clusterInfo = clusterInfoMapTemp[clusterToCreate];
            dataType firstSpikePosition = clusterInfo.firstSpikePosition();
            dataType nbSpikesOfCluster = clusterInfo.nbSpikes();

            clusterInfoMapTemp2->remove(clusterToCreate);
            clusterInfoMapTemp2->insert(newClusterId,clusterInfo);

            //For the new cluster, only the row index has been inserted in spikesByClusterTemp,
            //now the cluster number is updated at once for all the spikes of the new cluster
            for(dataType i = 0; i<nbSpikesOfCluster;++i)
                (*spikesByClusterTemp)(2,firstSpikePosition + i) = newClusterId;
        }


        //Sort the spikes of the newly created clusters, the information to do so have been store in the 2 lists
        //firstPositions and nbOfspikes by increasing Id of the cluster of origin.
        //NB: the iterator iterates on the items sorted by their key, here from fromToNewClusterIds
        QMap<int,int>::Iterator iterator;
        int i = 0;
        for(iterator = fromToNewClusterIds.begin(); iterator != fromToNewClusterIds.end(); ++iterator){
            sortCluster(clusterInfoMapTemp2,spikesByClusterTemp,iterator.value(),firstPositions[i],nbOfspikes[i],-1);
            ++i;
        }

        //Get the list of clusters before applying the changes, this will be used in the clean
        //of the correlation.
        QList<dataType> currentClusterList = clusterIds();

        //Deal with the undo mechanism.
        bool dimChanged = fromToNewClusterIds.contains(0);
        prepareUndo(spikesByClusterTemp,clusterInfoMapTemp2,dimChanged);

        //If some spikes have been taken from the cluster 0, the max and min
        // dimensions have to be recalculated. If minMaxThread is running, the call
        //will wait until it finishes before starting the thread again.
        if(dimChanged){
            //If the minMaxThread has not finish, wait until it is done
            minMaxThread->wait();
            //Reset the flag to false so the minMaxThread can do the computation
            clusterZeroJustModified = false;
            // setModifiedClusters MUST be called before start() so the thread
            // sees the correct cluster list from the moment it begins running.
            minMaxThread->setModifiedClusters(fromToNewClusterIds.keys());
            minMaxThread->start();
        }

        //Remove the waveform and correlation data for the clusters which gave the spikes for the new cluster.
        //if there is not a thread working with them,otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
        // and the thread will remove it.
        QMap<int,int>::Iterator fromToNewClusterIdsIterator;
        for(fromToNewClusterIdsIterator = fromToNewClusterIds.begin(); fromToNewClusterIdsIterator != fromToNewClusterIds.end(); ++fromToNewClusterIdsIterator){
            int clusterId = fromToNewClusterIdsIterator.key();
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(clusterId)){
                if(!waveformStatusMap[clusterId].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(clusterId));
                    waveformStatusMap.remove(clusterId);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[clusterId];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(clusterId,waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(static_cast<dataType>(clusterId))) cleanCorrelation(static_cast<dataType>(clusterId),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(clusterId),true);
                }
            }
        }
    }
    return fromToNewClusterIds;
}


// ---------------------------------------------------------------------------
// Data::integrateBasinLabeling
//
// Refactor target: mirrors the recluster pipeline.  All spikes from
// `clustersToRecluster` are expected to have a basin label in
// `featureRowToBasin` (>= 1).  Internally we populate the same
// `reclusteringSpikesByCluster` table that createFeatureFile uses, then
// rewrite its second column with `basinLabel + highestClusterId` (the
// same offsetting `loadReclusteredClusters` does for KlustaKwik output),
// and finally hand off to `integrateReclusteredClusters` which rebuilds
// the spike table and pushes the undo entry.
//
// Side-effects: same as recluster — source clusters are dissolved
// (their old IDs disappear), new IDs land strictly after the previous
// highest cluster ID, single undo step covers the whole operation.
// ---------------------------------------------------------------------------
bool Data::integrateBasinLabeling(QList<int>& clustersToRecluster,
                                   const QHash<dataType,int>& featureRowToBasin,
                                   QList<int>& newClusterList)
{
 
    // 1. Mirror createFeatureFile's first half: bucket all spikes from
    //    clustersToRecluster into reclusteringSpikesByCluster.

    // Sanity check: every input cluster must be present in clusterInfoMap.
    // Without this, default-constructed ClusterInfo's nbSpikes=0 would
    // silently drop the cluster from basin labeling — the bucket loop
    // would skip its spikes (memcpy 0 bytes), the basin map would still
    // contain row indices for those skipped spikes, and the column-2
    // rewrite at step 2 would either find no entry for the skipped row
    // (triggering the "no basin label" qWarning + abort) or, worse,
    // assign labels to rows belonging to a still-existing cluster.
    // Catch the desync at the entry instead of letting it cascade.
    for (int cid : clustersToRecluster) {
        if (!clusterInfoMap->contains(static_cast<dataType>(cid))) {
            qWarning("integrateBasinLabeling: cluster %d is in the input "
                     "list but absent from clusterInfoMap "
                     "(spikesByCluster ↔ clusterInfoMap desync) — "
                     "aborting basin labeling.  Save the session and "
                     "re-open to resync.", cid);
            return false;
        }
    }

    dataType reclusteringNbSpikes = 0;
    for (int cid : clustersToRecluster) {
        ClusterInfo info = (*clusterInfoMap)[static_cast<dataType>(cid)];
        reclusteringNbSpikes += info.nbSpikes();
    }
    reclusteringSpikesByCluster.setSize(reclusteringNbSpikes);

    dataType upperInsertionIndex = 1;
    for (int cid : clustersToRecluster) {
        ClusterInfo info             = (*clusterInfoMap)[static_cast<dataType>(cid)];
        const dataType firstSpikePos = info.firstSpikePosition();
        const dataType nbSpikesOfCl  = info.nbSpikes();
        memcpy(&(reclusteringSpikesByCluster)(1, upperInsertionIndex),
               &(*spikesByCluster)(1, firstSpikePos),
               nbSpikesOfCl * sizeof(dataType));
        // Don't bother memcpy'ing column 2 — we're going to rewrite it
        // from the basin map below.
        upperInsertionIndex += nbSpikesOfCl;
    }

    // 2. Rewrite column 2 with `basin + highestId`, mirroring
    //    loadReclusteredClusters' offsetting of KlustaKwik output.
    const dataType highestId = highestClusterId();
    for (dataType i = 1; i <= reclusteringNbSpikes; ++i) {
        const dataType row = reclusteringSpikesByCluster(1, i);
        const auto it      = featureRowToBasin.find(row);
        if (it == featureRowToBasin.end()) {
            qWarning("integrateBasinLabeling: feature row %lld has no basin "
                     "label — caller must label every spike (use a residual "
                     "label for unassigned points)",
                     (long long)row);
            reclusteringSpikesByCluster.setSize(0, true);
            return false;
        }
        reclusteringSpikesByCluster(2, i) =
            static_cast<dataType>(it.value()) + highestId;
    }

    // 3. Hand off to the existing integrate path.  This reads
    //    reclusteringSpikesByCluster, rebuilds the spike table,
    //    populates `newClusterList` with the freshly-allocated IDs, and
    //    calls prepareUndo internally.  We pass an empty QFile because
    //    integrateReclusteredClusters' first action would have been to
    //    call loadReclusteredClusters — but we've already done that
    //    work in step 2.  Refactor TODO: split integrateReclusteredClusters
    //    into a "load" stage and a "commit" stage so this dummy QFile
    //    isn't needed.  For now, inline the post-load body.
    //
    // We essentially duplicate the post-loadReclusteredClusters portion
    // of integrateReclusteredClusters.  The duplication is regrettable
    // but bounded — if the original ever changes, this needs to track.

    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    // 3a. Copy untouched clusters.
    upperInsertionIndex = 1;
    for (auto it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it) {
        const dataType firstSpikePos = it.value().firstSpikePosition();
        const dataType nbSpikesOfCl  = it.value().nbSpikes();
        const dataType clusterId     = it.key();
        if (!clustersToRecluster.contains(static_cast<int>(clusterId))) {
            memcpy(&(*spikesByClusterTemp)(1, upperInsertionIndex),
                   &(*spikesByCluster)(1, firstSpikePos),
                   nbSpikesOfCl * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2, upperInsertionIndex),
                   &(*spikesByCluster)(2, firstSpikePos),
                   nbSpikesOfCl * sizeof(dataType));
            clusterInfoMapTemp->insert(clusterId,
                ClusterInfo(upperInsertionIndex, nbSpikesOfCl));
            upperInsertionIndex += nbSpikesOfCl;
        }
    }

    // 3b. Sort reclustering table by feature-row index for time order
    //     (matches integrateReclusteredClusters line 4717).
    reclusteringSpikesByCluster.sort(1);

    // 3c. Count new clusters; allocate positions; populate
    //     clusterInfoMapTemp with new entries.
    QMap<dataType, dataType> clusters;
    for (dataType i = 1; i <= reclusteringNbSpikes; ++i) {
        const dataType clusterId = reclusteringSpikesByCluster(2, i);
        clusters[clusterId]++;
    }

    QMap<dataType, dataType> positions;
    int index = upperInsertionIndex;
    for (auto cIt = clusters.begin(); cIt != clusters.end(); ++cIt) {
        const dataType clusterId = cIt.key();
        newClusterList.append(static_cast<int>(clusterId));
        positions[clusterId] = index;
        clusterInfoMapTemp->insert(clusterId,
            ClusterInfo(index, cIt.value()));
        index += cIt.value();
    }

    // 3d. Fill the new clusters' spikes.
    for (dataType i = 1; i <= reclusteringNbSpikes; ++i) {
        const dataType clusterId    = reclusteringSpikesByCluster(2, i);
        const dataType position     = positions[clusterId];
        const dataType positionInFet = reclusteringSpikesByCluster(1, i);
        (*spikesByClusterTemp)(1, position) = positionInFet;
        (*spikesByClusterTemp)(2, position) = clusterId;
        positions[clusterId]++;
    }

    reclusteringSpikesByCluster.setSize(0, true);

    const bool dimChanged = clustersToRecluster.contains(0);
    prepareUndo(spikesByClusterTemp, clusterInfoMapTemp, dimChanged);

    if (dimChanged) {
        minMaxThread->wait();
        clusterZeroJustModified = false;
        minMaxThread->start();
    }

    return true;
}


// ---------------------------------------------------------------------------
// Data::splitClusterByKnnVsReferences
// ---------------------------------------------------------------------------
// KNN-classifier-driven N-way split.  For each spike in sourceCluster,
// find its K nearest neighbours in feature space — restricted to the
// reference pool (well-isolated existing clusters, nbSpikes >= minRef).
// Majority-vote the neighbours' cluster IDs.  Spikes sharing the same
// majority-vote reference become one new cluster.  Spikes that fail the
// majority threshold form the "residual" new cluster.
//
// Brute-force KNN (Euclidean over dims 1..nbDimensions-1, OMP-parallel
// over source spikes) — fast enough for typical sessions (source size
// up to ~5k spikes, reference pool ~10k-50k spikes, 20-30 dims).  For
// very large sessions a kd-tree would help; not implemented here.
//
// Source cluster is REMOVED if all its spikes get reassigned to new
// clusters; otherwise it keeps whatever spikes ended up below the
// per-label minimum size AND below the residual minimum size.
// ---------------------------------------------------------------------------
bool Data::splitClusterByKnnVsReferences(int sourceCluster,
                                          int K,
                                          double majorityThreshold,
                                          int minNewClusterSize,
                                          int minRefClusterSize,
                                          QList<int>& newClusters,
                                          QList<int>& matchedReferences,
                                          QList<int>& emptiedClusters,
                                          QString& errorMessage)
{
    newClusters.clear();
    matchedReferences.clear();
    emptiedClusters.clear();
    errorMessage.clear();

    // ── Validation ──────────────────────────────────────────────────────
    if (!clusterInfoMap->contains(static_cast<dataType>(sourceCluster))) {
        errorMessage = QStringLiteral("source cluster %1 is not registered "
            "in clusterInfoMap").arg(sourceCluster);
        return false;
    }
    if (K < 2) {
        errorMessage = QStringLiteral("K must be >= 2 (got %1)").arg(K);
        return false;
    }
    if (majorityThreshold < 0.0 || majorityThreshold > 1.0) {
        errorMessage = QStringLiteral("majorityThreshold must be in [0, 1] "
            "(got %1)").arg(majorityThreshold);
        return false;
    }
    if (nbDimensions < 2) {
        errorMessage = QStringLiteral("feature table has < 2 dimensions");
        return false;
    }

    const dataType srcFirst = (*clusterInfoMap)[sourceCluster].firstSpikePosition();
    const dataType srcN     = (*clusterInfoMap)[sourceCluster].nbSpikes();
    if (srcN < static_cast<dataType>(K + 1)) {
        errorMessage = QStringLiteral("source cluster has %1 spikes, need "
            "at least K+1 = %2").arg(srcN).arg(K + 1);
        return false;
    }

    // ── Build reference pool: well-isolated clusters only ───────────────
    QVector<dataType> refRows;
    QVector<int>      refRowCluster;
    QList<int>        refClusterIds;
    for (auto it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it) {
        const int cid = static_cast<int>(it.key());
        if (cid == sourceCluster) continue;
        if (cid <= 1) continue;                       // skip artifact + MUA
        const dataType nSpk = it.value().nbSpikes();
        if (nSpk < minRefClusterSize) continue;
        refClusterIds.append(cid);
        const dataType fp = it.value().firstSpikePosition();
        for (dataType i = fp; i < fp + nSpk; ++i) {
            const dataType r = (*spikesByCluster)(1, i);
            if (r >= 1 && r <= nbSpikes) {
                refRows.append(r);
                refRowCluster.append(cid);
            }
        }
    }
    if (refClusterIds.isEmpty()) {
        errorMessage = QStringLiteral("no reference clusters meet "
            "minRefClusterSize = %1 (excluding source, artifact, MUA). "
            "Lower the threshold or label more clusters first.")
            .arg(minRefClusterSize);
        return false;
    }
    if (refRows.size() < K) {
        errorMessage = QStringLiteral("reference pool has %1 spikes "
            "across %2 clusters, fewer than K = %3")
            .arg(refRows.size()).arg(refClusterIds.size()).arg(K);
        return false;
    }

    // ── Extract feature vectors into a flat float buffer ────────────────
    // Cache-friendly layout: row r's features at &featBuf[(r-1)*nDims].
    const int nDimsForKnn = nbDimensions - 1;          // exclude timestamp
    QVector<float> featBuf(static_cast<qsizetype>(nbSpikes) * nDimsForKnn);
    for (dataType r = 1; r <= nbSpikes; ++r) {
        float* dst = &featBuf[(r - 1) * nDimsForKnn];
        for (int d = 0; d < nDimsForKnn; ++d)
            dst[d] = static_cast<float>(features(r, d + 1));
    }

    // ── Collect source spike feature-row indices ────────────────────────
    QVector<dataType> srcRows;
    srcRows.reserve(srcN);
    for (dataType i = srcFirst; i < srcFirst + srcN; ++i)
        srcRows.append((*spikesByCluster)(1, i));

    // ── KNN search per source spike (OMP-parallel over source) ──────────
    QVector<int> spikeLabel(srcRows.size(), -1);       // -1 = ambiguous
    const int    voteMinCount = static_cast<int>(
        std::ceil(majorityThreshold * static_cast<double>(K)));

    #pragma omp parallel for schedule(dynamic, 16)
    for (qsizetype si = 0; si < srcRows.size(); ++si) {
        const dataType targetRow = srcRows[si];
        const float*   tFeat     = &featBuf[(targetRow - 1) * nDimsForKnn];

        typedef std::pair<float, int> HeapEntry;
        std::priority_queue<HeapEntry> heap;

        const qsizetype nRef = refRows.size();
        for (qsizetype ri = 0; ri < nRef; ++ri) {
            const dataType refRow = refRows[ri];
            const float*   rFeat  = &featBuf[(refRow - 1) * nDimsForKnn];
            float d = 0.0f;
            for (int j = 0; j < nDimsForKnn; ++j) {
                const float diff = tFeat[j] - rFeat[j];
                d += diff * diff;
            }
            if (static_cast<int>(heap.size()) < K) {
                heap.push({d, static_cast<int>(ri)});
            } else if (d < heap.top().first) {
                heap.pop();
                heap.push({d, static_cast<int>(ri)});
            }
        }

        QHash<int, int> tally;
        while (!heap.empty()) {
            tally[refRowCluster[heap.top().second]]++;
            heap.pop();
        }

        int bestId = -1, bestCount = 0;
        for (auto it = tally.begin(); it != tally.end(); ++it) {
            if (it.value() > bestCount) {
                bestCount = it.value();
                bestId    = it.key();
            }
        }
        spikeLabel[si] = (bestCount >= voteMinCount) ? bestId : -1;
    }

    // ── Partition source spikes by label ────────────────────────────────
    // QMap (not QHash) — deterministic ascending iteration order so the
    // resulting new clusters get matched to references in refId order.
    QMap<int, QList<dataType>> partitions;
    for (qsizetype si = 0; si < srcRows.size(); ++si)
        partitions[spikeLabel[si]].append(srcRows[si]);

    // Per-label minimum-size: small per-label partitions fold into the
    // ambiguous residual.  If the residual itself doesn't meet the
    // threshold, its spikes stay in the source cluster.
    QList<dataType> ambiguousFloor;
    if (partitions.contains(-1)) {
        ambiguousFloor = partitions.take(-1);
    }
    for (auto it = partitions.begin(); it != partitions.end(); ) {
        if (it.value().size() < minNewClusterSize) {
            ambiguousFloor.append(it.value());
            it = partitions.erase(it);
        } else {
            ++it;
        }
    }
    QSet<dataType> keepInSource;
    if (ambiguousFloor.size() >= minNewClusterSize) {
        partitions[-1] = ambiguousFloor;
    } else {
        for (dataType r : ambiguousFloor)
            keepInSource.insert(r);
    }

    if (partitions.isEmpty()) {
        errorMessage = QStringLiteral("no new clusters meet "
            "minNewClusterSize = %1 — try a smaller K, lower "
            "majorityThreshold, or lower minNewClusterSize")
            .arg(minNewClusterSize);
        return false;
    }

    // ── Allocate new cluster IDs ────────────────────────────────────────
    int nextId = static_cast<int>(highestClusterId()) + 1;
    // Real-reference labels first (ascending refId), residual (-1) last
    QList<int> labelsInOrder;
    for (auto it = partitions.constBegin(); it != partitions.constEnd(); ++it)
        if (it.key() != -1) labelsInOrder.append(it.key());
    std::sort(labelsInOrder.begin(), labelsInOrder.end());
    if (partitions.contains(-1)) labelsInOrder.append(-1);

    QHash<int, int> labelToNewId;
    for (int lbl : labelsInOrder) {
        const int nid = nextId++;
        labelToNewId.insert(lbl, nid);
        newClusters.append(nid);
        matchedReferences.append(lbl);                 // -1 for residual
    }

    // ── Build new spikesByCluster + clusterInfoMap ──────────────────────
    SortableTable*  newSpk  = new SortableTable();
    ClusterInfoMap* newInfo = new ClusterInfoMap();
    newSpk->setSize(nbSpikes);

    dataType pos = 1;

    for (auto it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it) {
        const dataType cid      = it.key();
        const dataType firstPos = it.value().firstSpikePosition();
        const dataType nSpk     = it.value().nbSpikes();

        if (cid == static_cast<dataType>(sourceCluster)) {
            const dataType clStart = pos;
            dataType kept = 0;
            for (dataType i = firstPos; i < firstPos + nSpk; ++i) {
                const dataType row1 = (*spikesByCluster)(1, i);
                if (keepInSource.contains(row1)) {
                    (*newSpk)(1, pos) = row1;
                    (*newSpk)(2, pos) = cid;
                    ++pos;
                    ++kept;
                }
            }
            if (kept > 0) {
                newInfo->insert(cid,
                    ClusterInfo(clStart, kept,
                                it.value().getStructure(),
                                it.value().getType(),
                                it.value().getId(),
                                it.value().getQuality(),
                                it.value().getNotes()));
            } else {
                emptiedClusters.append(sourceCluster);
            }
        } else {
            // Other clusters: copy unchanged.
            const dataType clStart = pos;
            for (dataType i = firstPos; i < firstPos + nSpk; ++i) {
                (*newSpk)(1, pos) = (*spikesByCluster)(1, i);
                (*newSpk)(2, pos) = cid;
                ++pos;
            }
            if (nSpk > 0) {
                newInfo->insert(cid,
                    ClusterInfo(clStart, nSpk,
                                it.value().getStructure(),
                                it.value().getType(),
                                it.value().getId(),
                                it.value().getQuality(),
                                it.value().getNotes()));
            }
        }
    }

    // Append new clusters at the tail, in label-order.  Within each
    // cluster, sort the spike feature-row indices ascending — that
    // matches the time-ordered convention used everywhere else
    // (features are loaded time-sorted; row 1 of spikesByCluster
    // therefore stores .fet row indices in time order).
    for (int lbl : labelsInOrder) {
        const int             newId = labelToNewId[lbl];
        const QList<dataType>& rows = partitions[lbl];
        const dataType clStart = pos;
        std::vector<dataType> rowsSorted(rows.begin(), rows.end());
        std::sort(rowsSorted.begin(), rowsSorted.end());
        for (dataType row : rowsSorted) {
            (*newSpk)(1, pos) = row;
            (*newSpk)(2, pos) = static_cast<dataType>(newId);
            ++pos;
        }
        newInfo->insert(static_cast<dataType>(newId),
                        ClusterInfo(clStart,
                                    static_cast<dataType>(rowsSorted.size())));
    }

    // ── Commit via prepareUndo ──────────────────────────────────────────
    // dimChanged is false: KNN-split never touches cluster 0 (it is
    // explicitly excluded from both source and reference pool).
    prepareUndo(newSpk, newInfo, false);

    return true;
}





/*
  The deletion of spikes from a cluster means moving those spikes from a given cluster
  to either the cluster 0 or the cluster one.
  The main algorithm is the following:
  Create a temporarily spikesByClusterTemp and clusterInfoMapTemp where the new configuration will be store
  If possible copy the cluster 0 and the destination cluster into the temporarily variables.
  Loop on all the clusters id in decreasing order, except the cluster 0 and the destination cluster (if it is the cluster 1).
  If the cluster is not concern (not currently in the view) copy it as it is. Otherwise loop on each of its spike
  to test if it is in the region, if so copy it a the top of spikesByClusterTemp (where the destination cluster is beeing build)
  otherwise, copy it at the bottom. Create the entry of the current cluster into clusterInfoMapTemp
  Create the entry of the new cluster destination into clusterInfoMapTemp
  There are special cases which have to be taken into account:
  Cluster 0 or cluster 1 does not exist.
  Cluster one is the destination and cluster 0 can contain spikes to be deleted.
 */
void Data::deleteSpikesFromClusters(QRegion& region, const QList <int>& clustersOfOrigin, int destinationCluster, int dimensionX, int dimensionY, QList <int>& fromClusters,QList <int>& emptyClusters){
    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Create the variables to store the number of spikes and the position of the last spike
    //for each cluster contributing to the new cluster and the position of the first spike and the number
    //of spikes for the current cluster destination (cluster 0 or cluster 1).
    //This will be used to sort the new cluster.
    QList<dataType> positions;
    QList<dataType> nbOfspikes;
    dataType firstPosition = 0;
    dataType number = 0;

    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    dataType upperInsertionIndex = 1;
    dataType lowerInsertionIndex = nbSpikes + 1;
    dataType nbSpikesInNewCluster = 0;
    dataType nbNewSpikesInNewCluster = 0;
    dataType lastSpikePositionForCurrentClusterPlus1 = 1;
    dataType firstSpikePositionForNewCluster = 1;

    //First process the case of cluster zero and one
    //Zero has to be take care of

    if(destinationCluster == 0){
        //Copy cluster 0 as it is if exists
        if(clusterInfoMap->contains(0)){
            ClusterInfo currentClusterInfo = (*clusterInfoMap)[0];
            dataType nbSpikesOfCluster = currentClusterInfo.nbSpikes();
            dataType firstSpikePosition = currentClusterInfo.firstSpikePosition();

            memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));

            //Store the first spike position for the cluster 0
            firstPosition = 1;
            //Store the number of spikes coming from the cluster 0
            number = nbSpikesOfCluster;

            upperInsertionIndex += nbSpikesOfCluster;

            //Initialize the number of spikes for the new cluster one with the information coming
            //from the current one
            firstSpikePositionForNewCluster = firstSpikePosition;
            nbSpikesInNewCluster = nbSpikesOfCluster;
            lastSpikePositionForCurrentClusterPlus1 = upperInsertionIndex;
        }
    }
    if(destinationCluster == 1){
        if(clusterInfoMap->contains(0)){
            ClusterInfo currentClusterInfo = (*clusterInfoMap)[0];
            dataType nbSpikesOfCluster = currentClusterInfo.nbSpikes();
            dataType firstSpikePosition = currentClusterInfo.firstSpikePosition();

            //Copy the cluster 0 as it is
            if(!clustersOfOrigin.contains(0)){
                memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                       &(*spikesByCluster)(1,firstSpikePosition),
                       nbSpikesOfCluster * sizeof(dataType));
                memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                       &(*spikesByCluster)(2,firstSpikePosition),
                       nbSpikesOfCluster * sizeof(dataType));

                //Construct the new clusterInfoMap
                clusterInfoMapTemp->insert(0,ClusterInfo(upperInsertionIndex,nbSpikesOfCluster));
                upperInsertionIndex += nbSpikesOfCluster;
                //assign the position of the first spike of cluster 1
                firstSpikePositionForNewCluster = nbSpikesOfCluster + 1;
            }
            else{
                //copy the points which are not in the region at the top and
                //copy the points which are in the region at a lower index starting at the number of spikes of the cluster 0
                dataType newNbSpikesOfCluster = nbSpikesOfCluster;
                dataType zeroLowerInsertionIndex =  nbSpikesOfCluster;
                dataType lastPosition =  firstSpikePosition + nbSpikesOfCluster;
                dataType lastPositionLessOne =  lastPosition -1;

                for(dataType i = firstSpikePosition; i < lastPosition;++i){
                    dataType featuresRowIndex = (*spikesByCluster)(1,i);
                    if(region.contains(
                                QPoint(features(featuresRowIndex,dimensionX),
                                       features(featuresRowIndex,dimensionY)))){
                        //Add the spike to the new cluster <=> add the row index at the end of spikesByCluster at the lowerInsertionIndex
                        (*spikesByClusterTemp)(1,zeroLowerInsertionIndex) = featuresRowIndex;
                        --zeroLowerInsertionIndex;
                        ++nbSpikesInNewCluster;
                        ++nbNewSpikesInNewCluster;
                        --newNbSpikesOfCluster;
                    }
                    else{
                        //Keep the spike in the current cluster <=> add the row index and the cluster number at the bottom of spikesByCluster at the lowerInsertionIndex
                        (*spikesByClusterTemp)(1,upperInsertionIndex) = featuresRowIndex;
                        (*spikesByClusterTemp)(2,upperInsertionIndex) = (*spikesByCluster)(2,i);
                        ++upperInsertionIndex;
                    }

                    if(i == (lastPositionLessOne)){
                        if(newNbSpikesOfCluster < nbSpikesOfCluster){
                            //Store the last spike position for the cluster 0
                            positions.append(nbSpikesOfCluster - newNbSpikesOfCluster);
                            //Store the number of spikes coming from the cluster 0
                            nbOfspikes.append(nbSpikesOfCluster - newNbSpikesOfCluster);

                            //update fromClusters if at least one spike from cluster 0 was in the region
                            fromClusters.append(0);
                        }
                        //Construct the insertion of the current cluster in the new clusterInfoMap if
                        // the number of spikes is more than zero
                        if(newNbSpikesOfCluster >0)clusterInfoMapTemp->insert(0,ClusterInfo(1,newNbSpikesOfCluster));
                        else emptyClusters.append(0);
                    }
                }
                //For the new cluster, only the row index has been inserted in spikesByClusterTemp,
                //now the cluster number is updated at once for all the spikes of the new cluster coming from cluster 0
                for(dataType i = 0; i<nbNewSpikesInNewCluster;++i) (*spikesByClusterTemp)(2,upperInsertionIndex + i) = destinationCluster;
                firstSpikePositionForNewCluster =  upperInsertionIndex;//when the last spike has been tested upperInsertionIndex =  zeroLowerInsertionIndex+1
                lastSpikePositionForCurrentClusterPlus1 = upperInsertionIndex = firstSpikePositionForNewCluster + nbSpikesInNewCluster;
                //reset nbNewSpikesInNewCluster
                nbNewSpikesInNewCluster = 0;
            }//else
        } //end of exists cluster 0

        if(clusterInfoMap->contains(1)){
            ClusterInfo currentClusterInfo = (*clusterInfoMap)[1];
            dataType nbSpikesOfCluster = currentClusterInfo.nbSpikes();
            dataType firstSpikePosition = currentClusterInfo.firstSpikePosition();

            memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));

            //Store the first spike position for the cluster 1
            firstPosition = nbSpikesInNewCluster + 1;
            //Store the number of spikes coming from the cluster 1
            number = nbSpikesOfCluster;

            upperInsertionIndex += nbSpikesOfCluster;

            //Initialize the number of spikes and the starting position for the new cluster one with the information coming
            //from the current one.
            nbSpikesInNewCluster += nbSpikesOfCluster;
            lastSpikePositionForCurrentClusterPlus1 = upperInsertionIndex;
        }
    }

    //process all the other clusters

    //Iteration on the clusters in decreasing order
    QList<dataType> clusters = clusterInfoMap->keys();
    std::sort(clusters.begin(), clusters.end());
    int nbClusters = clusters.size();

    for(int i = nbClusters - 1; i >=0 ; --i){
        dataType clusterId = clusters[i];

        if(clusterId == destinationCluster || clusterId == 0) continue;

        dataType firstSpikePosition = (*clusterInfoMap)[clusterId].firstSpikePosition();
        dataType nbSpikesOfCluster = (*clusterInfoMap)[clusterId].nbSpikes();
        //The user information of the different clusters.
        QString structure = (*clusterInfoMap)[clusterId].getStructure();
        QString	type = (*clusterInfoMap)[clusterId].getType();
        QString	iD = (*clusterInfoMap)[clusterId].getId();
        QString	quality = (*clusterInfoMap)[clusterId].getQuality();
        QString	notes = (*clusterInfoMap)[clusterId].getNotes();


        //if clustersOfOrigin does not contains the current cluster, this cluster is let unchanged
        //and its information is simply copy as is from spikesByCluster to spikesByClusterTemp
        if(!clustersOfOrigin.contains(static_cast<int>(clusterId))){
            lowerInsertionIndex -= nbSpikesOfCluster;
            //copy the 2 rows of spikesByCluster for the given cluster
            memcpy(&(*spikesByClusterTemp)(1,lowerInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,lowerInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            //Construct the new clusterInfoMap
            clusterInfoMapTemp->insert(clusterId,ClusterInfo(lowerInsertionIndex,nbSpikesOfCluster,structure,type,iD,quality,notes));
        }
        //Now deal with the clusters which may contain spikes to add to the new cluster
        //<=> spike in the region. Look up the spikes starting from the last one.
        else{
            dataType newNbSpikesOfCluster = nbSpikesOfCluster;
            dataType lastPosition =  firstSpikePosition - 1;

            for(dataType i = firstSpikePosition + nbSpikesOfCluster - 1; i > lastPosition;--i){
                dataType featuresRowIndex = (*spikesByCluster)(1,i);
                if(region.contains(
                            QPoint(features(featuresRowIndex,dimensionX),
                                   features(featuresRowIndex,dimensionY)))){
                    //Add the spike to the new cluster <=> add the row index at the end of spikesByCluster at the lowerInsertionIndex
                    (*spikesByClusterTemp)(1,upperInsertionIndex) = featuresRowIndex;
                    ++upperInsertionIndex;
                    ++nbSpikesInNewCluster;
                    ++nbNewSpikesInNewCluster;
                    --newNbSpikesOfCluster;
                }
                else{
                    //Keep the spike in the current cluster <=> add the row index and the cluster number at the bottom of spikesByCluster at the lowerInsertionIndex
                    --lowerInsertionIndex;
                    (*spikesByClusterTemp)(1,lowerInsertionIndex) = featuresRowIndex;
                    (*spikesByClusterTemp)(2,lowerInsertionIndex) = (*spikesByCluster)(2,i);
                }

                if(i == (firstSpikePosition)){
                    if(newNbSpikesOfCluster < nbSpikesOfCluster){
                        //Store the last spike position for the current cluster
                        positions.append(nbSpikesInNewCluster);
                        //Store the number of spikes coming from the current cluster
                        nbOfspikes.append(nbSpikesOfCluster - newNbSpikesOfCluster);

                        //If the destination cluster is cluster 0,the max and min dimensions have to
                        //be recalculated. If minMaxThread is running, clusterZeroJustModified will
                        //inform it that it has to stop (the computation will be done again on the new data).
                        if(destinationCluster == 0) clusterZeroJustModified = true;

                        //update fromClusters if at least one spike from that cluster was in the region
                        fromClusters.append(static_cast<int>(clusterId));
                    }
                    //Construct the insertion of the current cluster in the new clusterInfoMap if
                    // the number of spikes is more than zero
                    if(newNbSpikesOfCluster >0)clusterInfoMapTemp->insert(clusterId,ClusterInfo(lowerInsertionIndex,newNbSpikesOfCluster,structure,type,iD,quality,notes));
                    else emptyClusters.append(static_cast<int>(clusterId));
                }
            }
        }
    }

    //For the new cluster, only the row index has been inserted in spikesByClusterTemp,
    //now the cluster number is updated at once for all the spikes of the new cluster
    for(dataType i = 0; i<nbNewSpikesInNewCluster;++i) (*spikesByClusterTemp)(2,lastSpikePositionForCurrentClusterPlus1 + i) = destinationCluster;

    if(nbSpikesInNewCluster > 0){
        if(clustersOfOrigin.contains(destinationCluster)){
            //update fromClusters for the cluster destination
            fromClusters.append(destinationCluster);
        }
        //Construct the insertion of the new cluster in the new clusterInfoMap
        clusterInfoMapTemp->insert(destinationCluster,ClusterInfo(firstSpikePositionForNewCluster,nbSpikesInNewCluster));

        //Sort the spikes of the newly created cluster.
        sortCluster(clusterInfoMapTemp,spikesByClusterTemp,destinationCluster,positions,nbOfspikes,firstPosition,number);

        //Get the list of clusters before applying the changes, this will be used in the clean
        //of the correlation.
        QList<dataType> currentClusterList = clusterIds();

        //Deal with the undo mechanism
        bool dimChanged = (destinationCluster == 0);
        prepareUndo(spikesByClusterTemp,clusterInfoMapTemp,dimChanged);

        //If the spikes have been sent to the cluster 0, the max and min
        // dimensions have to be recalculated. If minMaxThread is running, the call
        //will wait until it finishes before starting the thread again.
        if(dimChanged){
            //If the minMaxThread has not finish, wait until it is done
            minMaxThread->wait();
            //Reset the flag to false so the minMaxThread can do the computation
            clusterZeroJustModified = false;
            minMaxThread->setModifiedClusters(fromClusters);
            minMaxThread->start();
        }

        //Remove the waveform and correlation data for the clusters which gave the spikes for the new cluster.
        //if there is not a thread working with them, otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
        // and the thread will remove it.
        QList<int>::iterator iterator;
        for(iterator = fromClusters.begin(); iterator != fromClusters.end(); ++iterator){
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(*iterator)){
                if(!waveformStatusMap[*iterator].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*iterator));
                    waveformStatusMap.remove(*iterator);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[*iterator];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(*iterator,waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(static_cast<dataType>(*iterator))) cleanCorrelation(static_cast<dataType>(*iterator),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(*iterator),true);
                }
            }
        }
    }
}

void Data::moveClustersToArtefact(QList <int>& clustersToDelete){
    //If clustersToDelete is not empty, the cluster 0 will be modified and the max and min dimensions
    //have to be recalculated. If minMaxThread is running, clusterZeroJustModified will
    //inform it that it has to stop (the computation will be done again on the new data).
    if(clustersToDelete.size() != 0) clusterZeroJustModified = true;

    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    //Create the variables to store the number of spikes and the position of the first spike
    //for each cluster contributing to the new cluster. This will be used to sort the new cluster.
    QList<dataType> positions;
    QList<dataType> nbOfspikes;

    dataType upperInsertionIndex = 1;

    //If the cluster 0 exits copy the the 2 rows of spikesByCluster
    dataType nbSpikesInNewClusterZero;
    dataType nbSpikesInCurrentClusterZero;
    dataType lastSpikePositionForCurrentClusterZeroPlus1;

    if(clusterInfoMap->contains(0)){
        ClusterInfo currentClusterInfo = (*clusterInfoMap)[0];
        dataType nbSpikesOfCluster = currentClusterInfo.nbSpikes();
        dataType firstSpikePosition = currentClusterInfo.firstSpikePosition();

        memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
               &(*spikesByCluster)(1,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));
        memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
               &(*spikesByCluster)(2,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));

        upperInsertionIndex += nbSpikesOfCluster;

        //Initialize the number of spikes for the new cluster 0 with the information coming
        //from the current one if it exists
        nbSpikesInNewClusterZero = nbSpikesInCurrentClusterZero = nbSpikesOfCluster;
        lastSpikePositionForCurrentClusterZeroPlus1 = upperInsertionIndex;

        //Store the first spike position for the original cluster 0
        positions.append(1);
        //Store the number of spikes coming from the original cluster 0v
        nbOfspikes.append(nbSpikesOfCluster);
    }
    //cluster 0 does not exist
    else{
        nbSpikesInNewClusterZero = nbSpikesInCurrentClusterZero = 0;
        lastSpikePositionForCurrentClusterZeroPlus1 = upperInsertionIndex;
    }

    //Move the clusters contain in clustersToDelete to cluster 0 and leave the others as they are
    moveClusters(clustersToDelete,spikesByClusterTemp,clusterInfoMapTemp,upperInsertionIndex,nbSpikesInNewClusterZero,0,positions,nbOfspikes);

    //For the new cluster 0, only the row index has been inserted in spikesByClusterTemp,
    //now the cluster number is updated at once for all the new spikes of the new cluster 0
    dataType nbNewSpikesInClusterZero = nbSpikesInNewClusterZero - nbSpikesInCurrentClusterZero;
    for(dataType i = 0; i<nbNewSpikesInClusterZero;++i) (*spikesByClusterTemp)(2,lastSpikePositionForCurrentClusterZeroPlus1 + i) = 0;

    //Construct the entry for cluster 0 in clusterInfoMap
    clusterInfoMapTemp->insert(0,ClusterInfo(1,nbSpikesInNewClusterZero));

    //Sort the spikes of the newly created cluster.
    sortCluster(clusterInfoMapTemp,spikesByClusterTemp,0,positions,nbOfspikes,1,true);

    //Get the list of clusters before applying the changes, this will be used in the clean
    //of the correlation.
    QList<dataType> currentClusterList = clusterIds();

    //Deal with the undo mechanism (dimension always changes when deleting to cluster 0)
    prepareUndo(spikesByClusterTemp,clusterInfoMapTemp,true);

    //The max and min dimensions have to be recalculated.
    //If the minMaxThread has not finish, wait until it is done
    minMaxThread->wait();
    //Reset the flag to false so the minMaxThread can do the computation
    clusterZeroJustModified = false;
    minMaxThread->setModifiedClusters(clustersToDelete);
    minMaxThread->start();

    //Remove the waveform and correlation data for the clusters which gave the spikes for the new cluster 0.
    //if there is not a thread working with them, otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
    // and the thread will remove it.
    QList<int>::iterator iterator;
    for(iterator = clustersToDelete.begin(); iterator != clustersToDelete.end(); ++iterator){
        {
            QMutexLocker lk(&mutex);
        if(waveformStatusMap.contains(*iterator)){
            if(!waveformStatusMap[*iterator].isInProcess()){
                delete waveformDict.take(QString::fromLatin1("%1").arg(*iterator));
                waveformStatusMap.remove(*iterator);
            }
            else{
                WaveformStatus waveformStatus = waveformStatusMap[*iterator];
                WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                waveformStatusCopy.setClusterModified(true);
                waveformStatusMap.insert(*iterator,waveformStatusCopy);
            }
        }
        }
        if(!correlationsInProcess.contains(static_cast<dataType>(*iterator))) cleanCorrelation(static_cast<dataType>(*iterator),currentClusterList);
        else{
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.setClusterModified(static_cast<dataType>(*iterator),true);
            }
        }
    }

    //remove the waveform and correlation data for the cluster 0 if clustersToDelete is not empty <=> cluster 0 will change
    //and if there is not a thread working with it, otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
    // and the thread will remove it.
    if(!clustersToDelete.empty()){
        {
            QMutexLocker lk(&mutex);
        if(!waveformStatusMap[0].isInProcess()){
            delete waveformDict.take("0");
            waveformStatusMap.remove(0);
        }
        else{
            WaveformStatus waveformStatus = waveformStatusMap[0];
            WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
            waveformStatusCopy.setClusterModified(true);
            waveformStatusMap.insert(0,waveformStatusCopy);
        }
        }
        if(!correlationsInProcess.contains(0)) cleanCorrelation(0,currentClusterList);
        else{
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.setClusterModified(0,true);
            }
        }
    }
}


void Data::moveClustersToNoise(QList<int>& clustersToDelete){
    //If clustersToDelete contains the cluster 0, the max and min dimensions
    //have to be recalculated. If minMaxThread is running, clusterZeroJustModified will
    //inform it that it has to stop (the computation will be done again on the new data).
    if(clustersToDelete.contains(0)) clusterZeroJustModified = true;

    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    //Create the variables to store the number of spikes and the position of the first spike
    //for each cluster contributing to the new cluster. This will be used to sort the new cluster.
    QList<dataType> positions;
    QList<dataType> nbOfspikes;

    dataType upperInsertionIndex = 1;

    //If the cluster 0 exists and is not a cluster to delete, copy the 2 rows of spikesByCluster.
    // If cluster 1 exists copy the the 2 rows of spikesByCluster
    int i, max;
    if(clusterInfoMap->contains(0) && !clustersToDelete.contains(0)) i = 0;
    else i = 1;
    if(clusterInfoMap->contains(1)) max = 2;
    else max = 1;
    dataType nbSpikesOfCluster = 0;
    dataType firstSpikePosition;
    for(;i<max;++i){
        ClusterInfo currentClusterInfo = (*clusterInfoMap)[i];
        nbSpikesOfCluster = currentClusterInfo.nbSpikes();
        firstSpikePosition = currentClusterInfo.firstSpikePosition();

        memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
               &(*spikesByCluster)(1,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));
        memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
               &(*spikesByCluster)(2,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));

        //Construct the new clusterInfoMap, the entry for cluster 1 will be updated later
        clusterInfoMapTemp->insert(i,ClusterInfo(upperInsertionIndex,nbSpikesOfCluster));

        upperInsertionIndex += nbSpikesOfCluster;

        //Store the information on the spikes of the original cluster 1
        if(i == 1){
            //Store the first spike position for the the original cluster 1
            positions.append(1);
            //Store the number of spikes coming from the original cluster 1
            nbOfspikes.append(nbSpikesOfCluster);
        }
    }

    //Initialize the number of spikes for the new cluster one with the information coming
    //from the current one if it exists
    dataType nbSpikesInNewClusterOne;
    dataType nbSpikesInCurrentClusterOne;
    dataType lastSpikePositionForCurrentClusterOnePlus1;
    if(max == 2){
        nbSpikesInNewClusterOne = nbSpikesInCurrentClusterOne = nbSpikesOfCluster; //nbSpikesOfCluster currently contents the info for cluster 1
        lastSpikePositionForCurrentClusterOnePlus1 = upperInsertionIndex; //upperInsertionIndex currently contents the info for cluster 1
    }
    //max == 1 <=> cluster 1 does not exist
    else{
        nbSpikesInNewClusterOne = nbSpikesInCurrentClusterOne = 0;
        lastSpikePositionForCurrentClusterOnePlus1 = upperInsertionIndex;

        //Construct the new clusterInfoMap, the entry for cluster 1 will be updated later
        clusterInfoMapTemp->insert(1,ClusterInfo(upperInsertionIndex,nbSpikesInNewClusterOne));
    }

    //Move the clusters contain in clustersToDelete to cluster 1 and leave the others as they are
    moveClusters(clustersToDelete,spikesByClusterTemp,clusterInfoMapTemp,upperInsertionIndex,nbSpikesInNewClusterOne,1,positions,nbOfspikes);

    //For the new cluster 1, only the row index has been inserted in spikesByClusterTemp,
    //now the cluster number is updated at once for all the new spikes of the new cluster 1
    dataType nbNewSpikesInClusterOne = nbSpikesInNewClusterOne - nbSpikesInCurrentClusterOne;
    for(dataType i = 0; i<nbNewSpikesInClusterOne;++i) (*spikesByClusterTemp)(2,lastSpikePositionForCurrentClusterOnePlus1 + i) = 1;

    //Update the new clusterInfoMap for the cluster 1
    ClusterInfo clusterOneInfo = (*clusterInfoMapTemp)[1];
    clusterOneInfo.setNbSpikes(nbSpikesInNewClusterOne);
    clusterInfoMapTemp->insert(1,clusterOneInfo);

    //Sort the spikes of the newly created cluster.
    sortCluster(clusterInfoMapTemp,spikesByClusterTemp,1,positions,nbOfspikes,1,true);

    //Get the list of clusters before applying the changes, this will be used in the clean
    //of the correlation.
    QList<dataType> currentClusterList = clusterIds();

    //Deal with the undo mechanism
    bool dimChanged = clustersToDelete.contains(0);
    prepareUndo(spikesByClusterTemp,clusterInfoMapTemp,dimChanged);

    //The max and min dimensions have to be recalculated.
    //If the minMaxThread has not finish, wait until it is done
    if(dimChanged){
        minMaxThread->wait();
        //Reset the flag to false so the minMaxThread can do the computation
        clusterZeroJustModified = false;
        QList<int> modifiedClusters;
        minMaxThread->setModifiedClusters(modifiedClusters);
        minMaxThread->start();
    }


    //Remove the waveform and correlation data for the clusters which gave the spikes for the new cluster 1.
    //if there is not a thread working with them, otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
    // and the thread will remove it.
    QList<int>::iterator iterator;
    for(iterator = clustersToDelete.begin(); iterator != clustersToDelete.end(); ++iterator){
        {
            QMutexLocker lk(&mutex);
        if(waveformStatusMap.contains(*iterator)){
            if(!waveformStatusMap[*iterator].isInProcess()){
                delete waveformDict.take(QString::fromLatin1("%1").arg(*iterator));
                waveformStatusMap.remove(*iterator);
            }
            else{
                WaveformStatus waveformStatus = waveformStatusMap[*iterator];
                WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                waveformStatusCopy.setClusterModified(true);
                waveformStatusMap.insert(*iterator,waveformStatusCopy);
            }
        }
        }
        if(!correlationsInProcess.contains(static_cast<dataType>(*iterator))) cleanCorrelation(static_cast<dataType>(*iterator),currentClusterList);
        else{
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.setClusterModified(static_cast<dataType>(*iterator),true);
            }
        }
    }

    //remove the waveform and correlation data for the cluster 1 if clustersToDelete is not empty <=> cluster 1 will change
    //and if there is not a thread working with it, otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
    // and the thread will remove it.
    if(!clustersToDelete.empty()){
        {
            QMutexLocker lk(&mutex);
        if(!waveformStatusMap[1].isInProcess()){
            delete waveformDict.take("1");
            waveformStatusMap.remove(1);
        }
        else{
            WaveformStatus waveformStatus = waveformStatusMap[1];
            WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
            waveformStatusCopy.setClusterModified(true);
            waveformStatusMap.insert(1,waveformStatusCopy);
        }
        }
        if(!correlationsInProcess.contains(1)) cleanCorrelation(1,currentClusterList);
        else{
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.setClusterModified(1,true);
            }
        }
    }
}

dataType Data::groupClusters(QList<int>& clustersToGroup){
    //If the clusters to group contain the cluster 0, the max and min
    // dimensions have to be recalculated. If minMaxThread is running, clusterZeroJustModified will
    //inform it that it has to stop (the computation will be done again on the new data).
    if(clustersToGroup.contains(0)) clusterZeroJustModified = true;

    //Set the new cluster number to the biggest existing number plus one
    dataType newClusterId = nextFreeClusterId();
    dataType nbSpikesInNewCluster = 0;

    //Create the variables to store the number of spikes and the position of the first spike
    //for each cluster contributing to the new cluster. This will be used to sort the new cluster.
    QList<dataType> positions;
    QList<dataType> nbOfspikes;

    //The user information of the different clusters to be grouped will be concatenated.
    QString newStructure;
    QString	newType;
    QString	newID;
    QString	newQuality;
    QString	newNotes;

    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    dataType upperInsertionIndex = 1;
    dataType lowerInsertionIndex = nbSpikes + 1;

    //Iteration on the clusters
    ClusterInfoMap::Iterator iterator;

    //Variable used to determined
    bool first = true;

    //NB: the iterator iterates on the items sorted by their key
    for(iterator = clusterInfoMap->begin(); iterator != clusterInfoMap->end(); ++iterator) {
        dataType firstSpikePosition = iterator.value().firstSpikePosition();
        dataType nbSpikesOfCluster = iterator.value().nbSpikes();
        dataType clusterId = iterator.key();

        //if clustersToGroup does not contains the current cluster, this cluster is let unchanged
        //and its information is simply copy as is from spikesByCluster to spikesByClusterTemp
        if(!clustersToGroup.contains(static_cast<int>(clusterId))){
            //copy the 2 rows of spikesByCluster for the given cluster
            memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));

            //Construct the new clusterInfoMap
            clusterInfoMapTemp->insert(clusterId,ClusterInfo(upperInsertionIndex,nbSpikesOfCluster,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes()));
            upperInsertionIndex += nbSpikesOfCluster;
        }
        //Now deal with the clusters which are to be grouped and need to be added to the new cluster
        else{
            memcpy(&(*spikesByClusterTemp)(1,lowerInsertionIndex - nbSpikesOfCluster),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));

            nbSpikesInNewCluster += nbSpikesOfCluster;
            lowerInsertionIndex -= nbSpikesOfCluster;

            //Store the first spike position for the current cluster
            positions.append(nbSpikesInNewCluster);
            //Store the number of spikes coming from the current cluster
            nbOfspikes.append(nbSpikesOfCluster);

            //Take care of the user information about the current cluster
            if(first){
                newStructure += iterator.value().getStructure();
                newType += iterator.value().getType();
                newID += iterator.value().getId();
                newQuality += iterator.value().getQuality();
                newNotes += iterator.value().getNotes();

                first = false;
            }
            else{
                newStructure += "--" + iterator.value().getStructure();
                newType += "--" + iterator.value().getType();
                newID += "--" + iterator.value().getId();
                newQuality += "--" + iterator.value().getQuality();
                newNotes += "--" + iterator.value().getNotes();
            }
        }
    }

    //For the new cluster, only the row index has been inserted in spikesByClusterTemp,
    //now the cluster number is updated at once for all the spikes of the new cluster
    for(dataType i = 0; i<nbSpikesInNewCluster;++i) (*spikesByClusterTemp)(2,lowerInsertionIndex + i) = newClusterId;

    //Construct the insertion of the new cluster in the new clusterInfoMap
    clusterInfoMapTemp->insert(newClusterId,ClusterInfo(lowerInsertionIndex,nbSpikesInNewCluster,newStructure,newType,newID,newQuality,newNotes));

    //Sort the spikes of the newly created cluster.
    sortCluster(clusterInfoMapTemp,spikesByClusterTemp,newClusterId,positions,nbOfspikes,1);

    //Get the list of clusters before applying the grouping, this will be used in the clean
    //of the correlation.
    QList<dataType> currentClusterList = clusterIds();

    //Deal with the undo mechanism
    bool dimChanged = clustersToGroup.contains(0);
    prepareUndo(spikesByClusterTemp,clusterInfoMapTemp,dimChanged);

    //If the clusters to group contain the cluster 0, the max and min
    // dimensions have to be recalculated.
    if(dimChanged){
        //If the minMaxThread has not finish, wait until it is done
        minMaxThread->wait();
        //Reset the flag to false so the minMaxThread can do the computation
        clusterZeroJustModified = false;
        minMaxThread->setModifiedClusters(clustersToGroup);
        minMaxThread->start();
    }

    //Remove the waveform and correlation data for the clusters which gave the spikes for the new cluster.
    //if there is not a thread working with them, otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
    // and the thread will remove it.
    QList<int>::iterator clustersToGroupIterator;
    for(clustersToGroupIterator = clustersToGroup.begin(); clustersToGroupIterator != clustersToGroup.end(); ++clustersToGroupIterator){

        {
            QMutexLocker lk(&mutex);
        if(waveformStatusMap.contains(*clustersToGroupIterator)){
            if(!waveformStatusMap[*clustersToGroupIterator].isInProcess()){
                delete waveformDict.take(QString::fromLatin1("%1").arg(*clustersToGroupIterator));
                waveformStatusMap.remove(*clustersToGroupIterator);
            }
            else{
                WaveformStatus waveformStatus = waveformStatusMap[*clustersToGroupIterator];
                WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                waveformStatusCopy.setClusterModified(true);
                waveformStatusMap.insert(*clustersToGroupIterator,waveformStatusCopy);
            }
        }
        }

        if(!correlationsInProcess.contains(static_cast<dataType>(*clustersToGroupIterator))) cleanCorrelation(static_cast<dataType>(*clustersToGroupIterator),currentClusterList);
        else{
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.setClusterModified(static_cast<dataType>(*clustersToGroupIterator),true);
            }
        }
    }

    return newClusterId;
}


void Data::moveSpikeSubset(int fromCluster, const QSet<dataType>& featureRowSet,
                            int toCluster,
                            QList<int>& fromClusters, QList<int>& emptiedClusters)
{
    if (featureRowSet.isEmpty()) return;
    if (!clusterInfoMap->contains(static_cast<dataType>(fromCluster))) return;

    // If toCluster does not yet exist insert an empty placeholder so the
    // rebuild loop hits the toCluster branch and creates it.
    const bool toClusterIsNew = !clusterInfoMap->contains(static_cast<dataType>(toCluster));
    if (toClusterIsNew)
        clusterInfoMap->insert(static_cast<dataType>(toCluster), ClusterInfo(0, 0));

    // Collect feature-row indices that are actually in fromCluster.
    QList<dataType> movedRows;
    {
        const ClusterInfo& fi = (*clusterInfoMap)[static_cast<dataType>(fromCluster)];
        for (dataType i = fi.firstSpikePosition();
             i < fi.firstSpikePosition() + fi.nbSpikes(); ++i) {
            dataType row1 = (*spikesByCluster)(1, i);
            if (featureRowSet.contains(row1))
                movedRows.append(row1);
        }
    }
    if (movedRows.isEmpty()) {
        if (toClusterIsNew)
            clusterInfoMap->remove(static_cast<dataType>(toCluster));
        return;
    }

    // Count total spikes retained to size the new table.
    SortableTable* newSpk  = new SortableTable();
    ClusterInfoMap* newInfo = new ClusterInfoMap();
    newSpk->setSize(nbSpikes);

    dataType pos = 1;  // 1-based insertion cursor

    for (auto it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it) {
        const dataType cid      = it.key();
        const dataType firstPos = it.value().firstSpikePosition();
        const dataType nSpk     = it.value().nbSpikes();

        if (cid == static_cast<dataType>(fromCluster)) {
            const dataType clStart = pos;
            dataType kept = 0;
            for (dataType i = firstPos; i < firstPos + nSpk; ++i) {
                const dataType row1 = (*spikesByCluster)(1, i);
                if (!featureRowSet.contains(row1)) {
                    (*newSpk)(1, pos) = row1;
                    (*newSpk)(2, pos) = static_cast<dataType>(fromCluster);
                    ++pos; ++kept;
                }
            }
            if (kept > 0) {
                newInfo->insert(cid, ClusterInfo(clStart, kept));
                fromClusters.append(fromCluster);
            } else {
                emptiedClusters.append(fromCluster);
                fromClusters.append(fromCluster);
            }

        } else if (cid == static_cast<dataType>(toCluster)) {
            const dataType clStart = pos;
            const dataType total   = nSpk + static_cast<dataType>(movedRows.size());

            // Copy existing toCluster spikes
            for (dataType i = firstPos; i < firstPos + nSpk; ++i) {
                (*newSpk)(1, pos) = (*spikesByCluster)(1, i);
                (*newSpk)(2, pos) = static_cast<dataType>(toCluster);
                ++pos;
            }
            // Append moved spikes
            for (dataType row1 : movedRows) {
                (*newSpk)(1, pos) = row1;
                (*newSpk)(2, pos) = static_cast<dataType>(toCluster);
                ++pos;
            }
            // Sort this block by time (row1)
            std::vector<dataType> tmp(static_cast<size_t>(total));
            for (dataType j = 0; j < total; ++j)
                tmp[static_cast<size_t>(j)] = (*newSpk)(1, clStart + j);
            std::sort(tmp.begin(), tmp.end());
            for (dataType j = 0; j < total; ++j)
                (*newSpk)(1, clStart + j) = tmp[static_cast<size_t>(j)];

            newInfo->insert(cid, ClusterInfo(clStart, total));

        } else {
            // Copy cluster unchanged
            const dataType clStart = pos;
            for (dataType i = firstPos; i < firstPos + nSpk; ++i) {
                (*newSpk)(1, pos) = (*spikesByCluster)(1, i);
                (*newSpk)(2, pos) = cid;
                ++pos;
            }
            if (nSpk > 0)
                newInfo->insert(cid, ClusterInfo(clStart, nSpk));
        }
    }

    // Determine whether the noise/artefact cluster (id 0) was a source —
    // if so, the dimension min/max may have changed.  Mirrors the same
    // criterion used in createNewCluster / deleteSpikesFromClusters.
    const bool dimChanged = fromClusters.contains(0);

    // Hand off the new tables to Data::prepareUndo, which:
    //   1. pushes the OLD spikesByCluster + clusterInfoMap onto the undo
    //      stack (essential — the previous version delete'd them outright,
    //      so Ctrl+Z after a moveSpikeSubset replayed an older snapshot
    //      instead of the pre-move state);
    //   2. swaps in the new tables under the data mutex;
    //   3. trims the undo list and clears the redo lists.
    prepareUndo(newSpk, newInfo, dimChanged);

    // Remove emptied clusters from the (now-current) clusterInfoMap.
    for (int cid : emptiedClusters)
        clusterInfoMap->remove(static_cast<dataType>(cid));
}

// ---------------------------------------------------------------------------
// Data::splitClusterTwoWays
//
// Atomic two-way split: takes one source cluster and partitions ALL its
// spikes into TWO new clusters according to two disjoint row-sets, in a
// single rebuild of spikesByCluster + clusterInfoMap.  The source is
// emptied and dropped from the cluster list; both new IDs land at the
// tail of the palette (assuming the caller chose them as
// nextFreeClusterId() and nextFreeClusterId()+1).
//
// dipSplit's commit primitive.  Pushes a SINGLE Data-side undo entry
// covering the whole operation.
//
// Preconditions:
//   - leftRows ∪ rightRows must cover every spike of @p sourceCluster
//     exactly once.  No validation here; dipSplit guarantees it by
//     construction (every member spike has exactly one label, 0 or 1).
//   - leftId and rightId must not already exist.
//   - leftId != rightId.
//   - sourceCluster must exist.
// ---------------------------------------------------------------------------
void Data::splitClusterTwoWays(int sourceCluster,
                                const QSet<dataType>& leftRows,
                                int leftId,
                                const QSet<dataType>& rightRows,
                                int rightId,
                                QList<int>& fromClusters,
                                QList<int>& emptiedClusters,
                                QList<int>& newClusters)
{
    if (!clusterInfoMap->contains(static_cast<dataType>(sourceCluster))) return;
    if (leftId == rightId) return;
    if (clusterInfoMap->contains(static_cast<dataType>(leftId)))  return;
    if (clusterInfoMap->contains(static_cast<dataType>(rightId))) return;

    // First pass: walk the source's spikes (in temporal order via the
    // existing spikesByCluster) and partition by label set.  Reads from
    // current state, writes to local lists — does NOT mutate
    // clusterInfoMap.  Watershed's integrateBasinLabeling uses this same
    // discipline; pre-mutating the current map pollutes the undo
    // snapshot when prepareUndo prepends the (now-corrupted) pointer.
    QList<dataType> leftMoved;
    QList<dataType> rightMoved;
    {
        const ClusterInfo& fi = (*clusterInfoMap)[static_cast<dataType>(sourceCluster)];
        for (dataType i = fi.firstSpikePosition();
             i < fi.firstSpikePosition() + fi.nbSpikes(); ++i) {
            const dataType row1 = (*spikesByCluster)(1, i);
            if (leftRows.contains(row1))
                leftMoved.append(row1);
            else if (rightRows.contains(row1))
                rightMoved.append(row1);
            // else: row not labelled — should not happen given dipSplit's
            // construction.  Drop silently.
        }
    }
    if (leftMoved.isEmpty() || rightMoved.isEmpty()) {
        // Degenerate split — one side empty.  Bail without touching
        // anything.  Caller treats this as small_child.
        return;
    }

    // Second pass: build temp tables.  Three-stage construction matching
    // integrateBasinLabeling:
    //   2a. Copy untouched clusters (every cluster except the source).
    //       Source is NOT copied — it dissolves.
    //   2b. Append the leftId block.
    //   2c. Append the rightId block.
    SortableTable*  newSpk  = new SortableTable();
    ClusterInfoMap* newInfo = new ClusterInfoMap();
    newSpk->setSize(nbSpikes);

    dataType pos = 1;

    // 2a. Untouched clusters.
    for (auto it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it) {
        const dataType cid      = it.key();
        const dataType firstPos = it.value().firstSpikePosition();
        const dataType nSpk     = it.value().nbSpikes();

        if (cid == static_cast<dataType>(sourceCluster)) continue;

        const dataType clStart = pos;
        for (dataType i = firstPos; i < firstPos + nSpk; ++i) {
            (*newSpk)(1, pos) = (*spikesByCluster)(1, i);
            (*newSpk)(2, pos) = cid;
            ++pos;
        }
        if (nSpk > 0)
            newInfo->insert(cid, ClusterInfo(clStart, nSpk));
    }

    // 2b. leftId block at the tail.  leftMoved is already in temporal
    // order from the first-pass scan (we walked the source in ascending
    // spikesByCluster position).
    {
        const dataType clStart = pos;
        for (dataType row1 : leftMoved) {
            (*newSpk)(1, pos) = row1;
            (*newSpk)(2, pos) = static_cast<dataType>(leftId);
            ++pos;
        }
        newInfo->insert(static_cast<dataType>(leftId),
            ClusterInfo(clStart, static_cast<dataType>(leftMoved.size())));
    }

    // 2c. rightId block — appended after leftId so rightId > leftId in
    // the spikesByCluster layout, matching the ascending-key order
    // QMap will iterate over newInfo.
    {
        const dataType clStart = pos;
        for (dataType row1 : rightMoved) {
            (*newSpk)(1, pos) = row1;
            (*newSpk)(2, pos) = static_cast<dataType>(rightId);
            ++pos;
        }
        newInfo->insert(static_cast<dataType>(rightId),
            ClusterInfo(clStart, static_cast<dataType>(rightMoved.size())));
    }

    // Source as cluster 0 (noise) would change dim min/max; not a normal
    // dipsplit target, but mirror moveSpikeSubset's criterion for safety.
    const bool dimChanged = (sourceCluster == 0);

    // Single Data-side undo entry covering the whole operation.  The
    // pre-mutation clusterInfoMap is what gets pushed onto the undo
    // stack as the "before" state — it's intact (we never modified it).
    prepareUndo(newSpk, newInfo, dimChanged);

    // Output bookkeeping for the caller.
    fromClusters.append(sourceCluster);
    emptiedClusters.append(sourceCluster);
    newClusters.append(leftId);
    newClusters.append(rightId);
}

void Data::prepareUndo(SortableTable* spikesByClusterTemp,ClusterInfoMap* clusterInfoMapTemp, bool dimensionChanged){
    //Store the current spikesByCluster in the undo list and make the temporary becomes the current one.
    spikesByClusterUndoList.prepend(spikesByCluster);
    //Store the current map in the undo list and make the temporary become the current one.
    clusterInfoMapUndoList.prepend(clusterInfoMap);
    //Record whether this operation changed the min/max dimensions.
    //Must be prepended HERE (not by the caller after returning) so the trim below
    //keeps it in sync with the two data lists.
    dimensionChangedUndo.prepend(dimensionChanged);

    {
        QMutexLocker lk(&mutex);
    clusterInfoMap = clusterInfoMapTemp;
    spikesByCluster = spikesByClusterTemp;
    }

    //if the number of undo has been reached, remove the oldest element from all three lists
    int currentNbUndo = spikesByClusterUndoList.count();
    if(currentNbUndo > nbUndo){
        delete spikesByClusterUndoList.takeAt(currentNbUndo - 1);
        delete clusterInfoMapUndoList.takeAt(currentNbUndo - 1);
        dimensionChangedUndo.removeAt(currentNbUndo - 1);
    }

    //Clear the redoLists (including dimensionChangedRedo which must stay in sync)
    qDeleteAll(spikesByClusterRedoList);
    spikesByClusterRedoList.clear();
    qDeleteAll(clusterInfoMapRedoList);
    clusterInfoMapRedoList.clear();
    dimensionChangedRedo.clear();
}

void Data::nbUndoChangedCleaning(int newNbUndo){
    //if the new number of possible undo is smaller than the current one,
    // clean the undo/redo related variables.
    if(newNbUndo < nbUndo){
        int currentNbUndo = spikesByClusterUndoList.count();
        //if the current number of undo is bigger than the new number of undo,
        // remove the last elements in the undo lists (first ones inserted).
        if(currentNbUndo > newNbUndo){
            while(currentNbUndo > newNbUndo){
                delete spikesByClusterUndoList.takeAt(currentNbUndo - 1);
                delete clusterInfoMapUndoList.takeAt(currentNbUndo - 1);
                if(!dimensionChangedUndo.isEmpty()) dimensionChangedUndo.removeLast();
                currentNbUndo = spikesByClusterUndoList.count();
            }
            //Clear the redoLists
            qDeleteAll(spikesByClusterRedoList);
            spikesByClusterRedoList.clear();
            qDeleteAll(clusterInfoMapRedoList);
            clusterInfoMapRedoList.clear();
            dimensionChangedRedo.clear();
        }
        //currentNbUndo < newNbUndo, check the redo list.
        else{
            //number of undo and redo must be <= new number of undo. Remove redo elements if need it.
            int currentNbRedo = spikesByClusterRedoList.count();
            if((currentNbRedo + currentNbUndo) > newNbUndo){
                while((currentNbRedo + currentNbUndo) > newNbUndo){
                    delete clusterInfoMapRedoList.takeAt(currentNbRedo - 1);
                    delete spikesByClusterRedoList.takeAt(currentNbRedo - 1);
                    if(!dimensionChangedRedo.isEmpty()) dimensionChangedRedo.removeLast();
                    currentNbRedo = spikesByClusterRedoList.count();
                }
            }
        }
    }
}

void Data::moveClusters(QList<int>& clustersToDelete,SortableTable* spikesByClusterTemp,ClusterInfoMap* clusterInfoMapTemp,long upperInsertionIndex,long& nbSpikesInNewCluster,int destinationId,QList<long>& positions,QList<long>& nbOfspikes){

    //For all the clusters to delete, copy the first row of spikesByCluster into spikesByClusterTemp
    //right after the data coming from the current cluster destination (0 or 1)
    QList<int>::iterator clustersToDeleteIterator;
    for(clustersToDeleteIterator = clustersToDelete.begin(); clustersToDeleteIterator != clustersToDelete.end(); ++clustersToDeleteIterator ){
        dataType clusterId = static_cast<dataType>(*clustersToDeleteIterator);
        if(clusterId == destinationId) continue;
        // Desync guard: a missing key here would default-construct a
        // ClusterInfo (firstPos=0, nbSpikes=0), making the memcpy below
        // a 0-byte no-op AND still appending a (bogus) entry to
        // positions/nbOfspikes — the caller (moveClustersToArtefact /
        // moveClustersToNoise) would then operate on a list with extra
        // zero-spike entries, producing wrong totals and an
        // inconsistent destination cluster.  Skip with a warning so
        // the user can see something went wrong, and the
        // positions/nbOfspikes lists stay self-consistent.
        if (!clusterInfoMap->contains(clusterId)) {
            qWarning("moveClusters: cluster %lld is in the delete list "
                     "but absent from clusterInfoMap (spikesByCluster ↔ "
                     "clusterInfoMap desync) — skipping.  The "
                     "destination bin (cluster %d) will be missing this "
                     "cluster's spikes until the session is saved and "
                     "re-opened.",
                     static_cast<long long>(clusterId), destinationId);
            continue;
        }
        ClusterInfo currentClusterInfo = (*clusterInfoMap)[clusterId];
        dataType firstSpikePosition = currentClusterInfo.firstSpikePosition();
        dataType nbSpikesOfCluster = currentClusterInfo.nbSpikes();

        memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
               &(*spikesByCluster)(1,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));

        //Store the first spike position for the current cluster
        positions.append(nbSpikesInNewCluster + 1);
        //Store the number of spikes coming from the current cluster
        nbOfspikes.append(nbSpikesOfCluster);

        upperInsertionIndex += nbSpikesOfCluster;
        nbSpikesInNewCluster += nbSpikesOfCluster;
    }

    //Copy the 2 rows for all the other clusters
    //Iteration on the clusters starting with the cluster following cluster 1
    ClusterInfoMap::Iterator iterator;

    //NB: the iterator iterates on the items sorted by their key
    for(iterator = clusterInfoMap->begin(); iterator != clusterInfoMap->end(); ++iterator){
        dataType clusterId = iterator.key();
        if(clusterId <= destinationId) continue;
        if(!clustersToDelete.contains(static_cast<int>(clusterId))){
            dataType firstSpikePosition = iterator.value().firstSpikePosition();
            dataType nbSpikesOfCluster = iterator.value().nbSpikes();

            memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));

            //Construct the new clusterInfoMap
            clusterInfoMapTemp->insert(clusterId,ClusterInfo(upperInsertionIndex,nbSpikesOfCluster,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes()));
            upperInsertionIndex += nbSpikesOfCluster;
        }
    }
}

void Data::undo(QList<int>& addedClusters,QList<int>& updatedClusters){
    //Inform that an undo is in process
    undoRedoInProcess = true;


    //Get the list of clusters before applying the changes, this will be used in the clean
    //of the correlation.
    QList<dataType> currentClusterList = clusterIds();

    //If addedClusters or updatedClusters contain any cluster, remove the corresponding entry in waveformDict and correlationDict
    //(the data will have to be uploaded again) if there is not a thread working with it,
    //otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
    // and the thread will remove it.
    if(!addedClusters.isEmpty() ){
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = addedClusters.begin(); clustersToRemoveIterator != addedClusters.end(); ++clustersToRemoveIterator){

            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(*clustersToRemoveIterator)){
                if(!waveformStatusMap[*clustersToRemoveIterator].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*clustersToRemoveIterator));
                    waveformStatusMap.remove(*clustersToRemoveIterator);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[*clustersToRemoveIterator];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(*clustersToRemoveIterator,waveformStatusCopy);
                }
            }
            }



            if(!correlationsInProcess.contains(static_cast<dataType>(*clustersToRemoveIterator))) cleanCorrelation(static_cast<dataType>(*clustersToRemoveIterator),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(*clustersToRemoveIterator),true);
                }
            }
        }
    }
    if(!updatedClusters.isEmpty()){
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = updatedClusters.begin(); clustersToRemoveIterator != updatedClusters.end(); ++clustersToRemoveIterator){


            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(*clustersToRemoveIterator)){
                if(!waveformStatusMap[*clustersToRemoveIterator].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*clustersToRemoveIterator));
                    waveformStatusMap.remove(*clustersToRemoveIterator);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[*clustersToRemoveIterator];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(*clustersToRemoveIterator,waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(static_cast<dataType>(*clustersToRemoveIterator))) cleanCorrelation(static_cast<dataType>(*clustersToRemoveIterator),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(*clustersToRemoveIterator),true);
                }
            }
        }
    }

    //if addedClusters and updatedClusters are both empty, the undo concern the renumbering
    //Can not do much, all the data will have to be reloaded (it should not happen very often)
    if(addedClusters.isEmpty() && updatedClusters.isEmpty()){
        //Gets all the clustersId currently available
        QList<dataType> clusters = clusterIds();

        //Loop on all the clusters and delete the linked information if possible (if a thread is not
        //working with it), otherwise modify the status so the thread will delete the information.
        QList<dataType>::iterator iterator;
        for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator){



            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(static_cast<int>(*iterator))){
                if(!waveformStatusMap[static_cast<int>(*iterator)].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*iterator));
                    waveformStatusMap.remove(static_cast<int>(*iterator));
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[static_cast<int>(*iterator)];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(static_cast<int>(*iterator),waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(*iterator)) cleanCorrelation(*iterator,clusters);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(*iterator,true);
                }
            }
        }
    }

    //If clusterInfoMapUndoList is not empty, make the current clusterInfoMap become the first element
    //of the clusterInfoMapRedoList and the first element of the clusterInfoMapUndoList become the current clusterInfoMap.
    //Do the same with the spikesByCluster
    if(!clusterInfoMapUndoList.isEmpty()){

        clusterInfoMapRedoList.prepend(clusterInfoMap);
        ClusterInfoMap* clusterInfoMapTemp = clusterInfoMapUndoList.takeAt(0);
        spikesByClusterRedoList.prepend(spikesByCluster);
        SortableTable* spikesByClusterTemp = spikesByClusterUndoList.takeAt(0);

        {
            QMutexLocker lk(&mutex);
        clusterInfoMap =  clusterInfoMapTemp;


        spikesByCluster =  spikesByClusterTemp;

        }


        //If the last action implied a changed of the dimension, change the dimension again
        bool dimChanged = !dimensionChangedUndo.isEmpty() && dimensionChangedUndo.takeFirst();
        if(dimChanged){


            //If the minMaxThread has not finish, wait until it is done
            minMaxThread->wait();

            //Reset the flag to false so the minMaxThread can do the computation
            undoRedoInProcess = false;
            QList<int> modifiedClusters;
            minMaxThread->setModifiedClusters(modifiedClusters);
            minMaxThread->start();
            dimensionChangedRedo.prepend(true);
        }
        else{
            // No dimension change: undo/redo is complete; allow minMaxThread to run again.
            undoRedoInProcess = false;
            dimensionChangedRedo.prepend(false);
        }
    }
}


void Data::redo(QList<int>& addedClusters,QList<int>& updatedClusters,QList<int>& deletedClusters){
    //Inform that a redo is in process
    undoRedoInProcess = true;

    //Get the list of clusters before applying the changes, this will be used in the clean
    //of the correlation.
    QList<dataType> currentClusterList = clusterIds();

    //If addedClusters or updatedClusters contain any cluster, remove the corresponding entry in waveformDict and correlationDict
    //(the data will have to be uploaded again).
    if(!addedClusters.isEmpty() ){
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = addedClusters.begin(); clustersToRemoveIterator != addedClusters.end(); ++clustersToRemoveIterator){
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(*clustersToRemoveIterator)){
                if(!waveformStatusMap[*clustersToRemoveIterator].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*clustersToRemoveIterator));
                    waveformStatusMap.remove(*clustersToRemoveIterator);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[*clustersToRemoveIterator];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(*clustersToRemoveIterator,waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(static_cast<dataType>(*clustersToRemoveIterator))) cleanCorrelation(static_cast<dataType>(*clustersToRemoveIterator),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(*clustersToRemoveIterator),true);
                }
            }
        }
    }

    if(updatedClusters.size() > 0){
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = updatedClusters.begin(); clustersToRemoveIterator != updatedClusters.end(); ++clustersToRemoveIterator){
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(*clustersToRemoveIterator)){
                if(!waveformStatusMap[*clustersToRemoveIterator].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*clustersToRemoveIterator));
                    waveformStatusMap.remove(*clustersToRemoveIterator);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[*clustersToRemoveIterator];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(*clustersToRemoveIterator,waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(static_cast<dataType>(*clustersToRemoveIterator))) cleanCorrelation(static_cast<dataType>(*clustersToRemoveIterator),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(*clustersToRemoveIterator),true);
                }
            }
        }
    }

    if(!deletedClusters.isEmpty()){
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = deletedClusters.begin(); clustersToRemoveIterator != deletedClusters.end(); ++clustersToRemoveIterator){
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(*clustersToRemoveIterator)){
                if(!waveformStatusMap[*clustersToRemoveIterator].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*clustersToRemoveIterator));
                    waveformStatusMap.remove(*clustersToRemoveIterator);
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[*clustersToRemoveIterator];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(*clustersToRemoveIterator,waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(static_cast<dataType>(*clustersToRemoveIterator))) cleanCorrelation(static_cast<dataType>(*clustersToRemoveIterator),currentClusterList);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(static_cast<dataType>(*clustersToRemoveIterator),true);
                }
            }
        }
    }


    //if addedClusters and updatedClusters are both empty, the undo concern the renumbering
    //Can not do much, all the data will have to be reloaded (it should not happen very often)
    if(addedClusters.isEmpty() && updatedClusters.isEmpty()){
        //Gets all the clustersId currently available
        QList<dataType> clusters = clusterIds();

        //Loop on all the clusters and delete the linked information if possible (if a thread is not
        //working with it), otherwise modify the status so the thread will delete the information.
        QList<dataType>::iterator iterator;
        for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator){
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(static_cast<int>(*iterator))){
                if(!waveformStatusMap[static_cast<int>(*iterator)].isInProcess()){
                    delete waveformDict.take(QString::fromLatin1("%1").arg(*iterator));
                    waveformStatusMap.remove(static_cast<int>(*iterator));
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[static_cast<int>(*iterator)];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(static_cast<int>(*iterator),waveformStatusCopy);
                }
            }
            }
            if(!correlationsInProcess.contains(*iterator)) cleanCorrelation(*iterator,clusters);
            else{
                {
                    QMutexLocker lk(&mutex);
                correlationsInProcess.setClusterModified(*iterator,true);
                }
            }
        }
    }

    //If clusterInfoMapRedoList is not empty, make the current clusterInfoMap become the first element
    //of the clusterInfoMapUndoList and the first element of the clusterInfoMapRedoList become the current clusterInfoMap.
    //Do the same with the spikesByCluster
    if(!clusterInfoMapRedoList.isEmpty()){
        clusterInfoMapUndoList.prepend(clusterInfoMap);
        ClusterInfoMap* clusterInfoMapTemp = clusterInfoMapRedoList.takeAt(0);
        spikesByClusterUndoList.prepend(spikesByCluster);
        SortableTable* spikesByClusterTemp = spikesByClusterRedoList.takeAt(0);

        {
            QMutexLocker lk(&mutex);
        clusterInfoMap =  clusterInfoMapTemp;
        spikesByCluster =  spikesByClusterTemp;
        }

        //If the last redo implied a changed of the dimension, change the dimension again
        bool dimChanged = !dimensionChangedRedo.isEmpty() && dimensionChangedRedo.takeFirst();
        if(dimChanged){
            //If the minMaxThread has not finish, wait until it is done
            minMaxThread->wait();

            //Reset the flag to false so the minMaxThread can do the computation
            undoRedoInProcess = false;
            QList<int> modifiedClusters;
            minMaxThread->setModifiedClusters(modifiedClusters);
            minMaxThread->start();
            dimensionChangedUndo.prepend(true);
        }
        else{
            // No dimension change: undo/redo is complete; allow minMaxThread to run again.
            undoRedoInProcess = false;
            dimensionChangedUndo.prepend(false);
        }
    }
}

void Data::renumber(QMap<int,int>& clusterIdsOldNew,QMap<int,int>& clusterIdsNewOld){
    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    //process the clusters 0 and 1 separately as, if they exist, they are never renumber.
    for(int i = 0; i < 2; ++i){
        if(clusterInfoMap->contains(i)){
            ClusterInfo currentClusterInfo = (*clusterInfoMap)[i];
            dataType nbSpikesOfCluster = currentClusterInfo.nbSpikes();
            dataType firstSpikePosition = currentClusterInfo.firstSpikePosition();

            //copy the 2 rows of spikesByCluster for the given cluster
            memcpy(&(*spikesByClusterTemp)(1,firstSpikePosition),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,firstSpikePosition),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));

            //Construct the new clusterInfoMap
            clusterInfoMapTemp->insert(i,ClusterInfo(firstSpikePosition,nbSpikesOfCluster));
            clusterIdsOldNew.insert(i,i);
            clusterIdsNewOld.insert(i,i);
        }
    }

    //Iteration on the clusters
    ClusterInfoMap::Iterator iterator;
    int clusterNumber = 2;

    //NB: the iterator iterates on the items sorted by their key
    for(iterator = clusterInfoMap->begin(); iterator != clusterInfoMap->end(); ++iterator) {
        dataType firstSpikePosition = iterator.value().firstSpikePosition();
        dataType nbSpikesOfCluster = iterator.value().nbSpikes();
        dataType clusterId = iterator.key();

        //The clusters 0 and 1 have been processed separately before
        if(clusterId == 0 || clusterId == 1) continue;

        //Insert into spikesByClusterTemp and clusterInfoMapTemp with the new number

        //copy the first row of spikesByCluster for the given cluster
        memcpy(&(*spikesByClusterTemp)(1,firstSpikePosition),
               &(*spikesByCluster)(1,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));

        if(clusterId == clusterNumber){
            //copy the second row as it is
            memcpy(&(*spikesByClusterTemp)(2,firstSpikePosition),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
        }
        //renumber
        else{
            //Insert the new cluster id in the second row.
            for(long i = 0; i<nbSpikesOfCluster;++i) (*spikesByClusterTemp)(2,firstSpikePosition + i) = clusterNumber;
            //If waveformDict or correlationDict contain that cluster, change the key for it.
            {
                QMutexLocker lk(&mutex);
            if(waveformStatusMap.contains(static_cast<int>(clusterId))){
                if(!waveformStatusMap[static_cast<int>(clusterId)].isInProcess()){
                    Waveforms* waveforms = waveformDict.take(QString::fromLatin1("%1").arg(clusterId));
                    waveformDict.insert(QString::fromLatin1("%1").arg(clusterNumber),waveforms);
                    WaveformStatus waveformStatus = waveformStatusMap[static_cast<int>(clusterId)];
                    waveformStatusMap.insert(clusterNumber,waveformStatus);
                    waveformStatusMap.remove(static_cast<int>(clusterId));
                }
                else{
                    WaveformStatus waveformStatus = waveformStatusMap[static_cast<int>(clusterId)];
                    WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                    waveformStatusCopy.setClusterModified(true);
                    waveformStatusMap.insert(static_cast<int>(clusterId),waveformStatusCopy);
                }
            }
            }
        }
        //Construct the new clusterInfoMap
        clusterInfoMapTemp->insert(clusterNumber,ClusterInfo(firstSpikePosition,nbSpikesOfCluster,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes()));
        clusterIdsOldNew.insert(static_cast<int>(clusterId),clusterNumber);
        clusterIdsNewOld.insert(clusterNumber,static_cast<int>(clusterId));

        ++clusterNumber;
    }

    //Renumber the correlations, this is not done in the loop because the complet mapping has
    //to be known in order to do it.
    renumberCorrelation(clusterIdsOldNew);

    //Deal with the undo mechanism (renumber does not affect cluster 0 dimensions)
    prepareUndo(spikesByClusterTemp,clusterInfoMapTemp,false);
}

// ---------------------------------------------------------------------------
// Data::renumberPartial
//
// Targeted version of renumber: only the clusters listed in `oldToNew` are
// renamed.  All others retain their current cluster ID and palette position.
// Used by the palette T shortcut.
//
// Implementation notes:
//   - We build fresh spikesByCluster + clusterInfoMap tables (same pattern
//     as the existing full renumber) so the undo snapshot via prepareUndo
//     captures the pre-rename state cleanly.
//   - Spike-table layout (column 1 = original .fet row index, column 2 =
//     cluster ID) is preserved exactly; only column 2 entries belonging to
//     renamed clusters are rewritten.
//   - The mutex guard around dict / cache moves matches Data::renumber.
//   - Caller (KlustersDoc) is responsible for matching prepareClusterColorUndo
//     and view-side renumberClusters() calls.
// ---------------------------------------------------------------------------
void Data::renumberPartial(const QMap<int,int>& oldToNew)
{
    if (oldToNew.isEmpty()) return;

    // Sanity: no entry may remap 0 or 1.
    for (auto it = oldToNew.constBegin(); it != oldToNew.constEnd(); ++it) {
        if (it.key() == 0 || it.key() == 1) return;        // would corrupt artefact/noise
        if (it.value() == 0 || it.value() == 1) return;
    }

    SortableTable*  spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);
    ClusterInfoMap* clusterInfoMapTemp  = new ClusterInfoMap();

    // ── Pass 1: build the new-ID → (oldId, oldFirstSpikePosition,
    // nbSpikes, ClusterInfo-payload) table.
    //
    // Each old cluster gets a (possibly identity) new ID via the
    // oldToNew map.  We bucket by NEW id so the next pass can write
    // spikes into the temp table in ascending-new-id order — that's
    // what keeps highestClusterId() (which reads row 2 at the last
    // physical position) and the canonical spike-table-is-sorted-by-id
    // invariant honest.  Without this re-sort, a single partial
    // rename leaves the table physically unsorted, and every
    // subsequent nextFreeClusterId() (= last position's id + 1) lies
    // — every later T renames to the same "new" id, producing
    // duplicate IDs and a corrupt clusterInfoMap (last-writer-wins on
    // QMap::insert with the same key).
    struct Bucket {
        dataType oldFirstPos;
        dataType nbSpikesOfCluster;
        dataType oldId;
        // ClusterInfo-payload fields, copied so we can pass them to
        // the new-id-keyed clusterInfoMap entry without re-iterating.
        ClusterInfo info;
    };
    QMap<int, Bucket> bucketsByNewId;          // sorted by new-id (QMap iterates ascending)
    for (ClusterInfoMap::Iterator it = clusterInfoMap->begin();
         it != clusterInfoMap->end(); ++it)
    {
        const int oldId = static_cast<int>(it.key());
        const int newId = oldToNew.contains(oldId)
                            ? oldToNew.value(oldId)
                            : oldId;
        Bucket b{ it.value().firstSpikePosition(),
                  it.value().nbSpikes(),
                  static_cast<dataType>(oldId),
                  it.value() };
        bucketsByNewId.insert(newId, b);
    }

    // ── Pass 2: write spikes into the temp table in ascending-new-id
    // order, packing them contiguously starting at position 1.  Build
    // clusterInfoMapTemp with the NEW firstSpikePosition for each
    // cluster.  Move waveform-cache entries for renamed clusters.
    dataType writePos = 1;
    for (auto it = bucketsByNewId.constBegin();
         it != bucketsByNewId.constEnd(); ++it)
    {
        const int        newId      = it.key();
        const Bucket&    b          = it.value();
        const int        oldId      = static_cast<int>(b.oldId);
        const dataType   nbSp       = b.nbSpikesOfCluster;
        const dataType   oldFirst   = b.oldFirstPos;

        // Row 1 (feature-file row indices) copies verbatim from the
        // old block; quicksort isn't stable so we do this manually
        // instead of using SortableTable::sort.  Within-cluster spike
        // order (== feature-file row order) is preserved.
        memcpy(&(*spikesByClusterTemp)(1, writePos),
               &(*spikesByCluster)(1, oldFirst),
               nbSp * sizeof(dataType));

        if (newId == oldId) {
            // Identity rename — row 2 copies verbatim too.
            memcpy(&(*spikesByClusterTemp)(2, writePos),
                   &(*spikesByCluster)(2, oldFirst),
                   nbSp * sizeof(dataType));
        } else {
            // Rewrite row 2 with the new id.
            for (long i = 0; i < nbSp; ++i)
                (*spikesByClusterTemp)(2, writePos + i) = newId;

            // Move waveform cache entry from old key to new key.  Mirrors
            // the corresponding block in Data::renumber.
            {
                QMutexLocker lk(&mutex);
                if (waveformStatusMap.contains(oldId)) {
                    if (!waveformStatusMap[oldId].isInProcess()) {
                        Waveforms* waveforms = waveformDict.take(QString::fromLatin1("%1").arg(oldId));
                        waveformDict.insert(QString::fromLatin1("%1").arg(newId), waveforms);
                        WaveformStatus waveformStatus = waveformStatusMap[oldId];
                        waveformStatusMap.insert(newId, waveformStatus);
                        waveformStatusMap.remove(oldId);
                    } else {
                        // A WaveformThread is in flight for oldId; flag
                        // it as modified so it'll re-key on completion.
                        WaveformStatus waveformStatus     = waveformStatusMap[oldId];
                        WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                        waveformStatusCopy.setClusterModified(true);
                        waveformStatusMap.insert(oldId, waveformStatusCopy);
                    }
                }
            }
        }

        // Insert into clusterInfoMapTemp under the NEW id, with the
        // NEW firstSpikePosition (writePos) — not the old one.
        clusterInfoMapTemp->insert(newId,
            ClusterInfo(writePos, nbSp,
                        b.info.getStructure(), b.info.getType(),
                        b.info.getId(), b.info.getQuality(),
                        b.info.getNotes()));

        writePos += nbSp;
    }

    // Renumber correlation cache; needs only the partial map (renumber()
    // and renumberCorrelation() both treat unchanged clusters as identity).
    {
        QMap<int,int> partial = oldToNew;
        renumberCorrelation(partial);
    }

    // Push the previous tables onto the undo stack and swap in the new ones.
    // renumberPartial does not affect cluster 0, so dimensionChanged = false.
    prepareUndo(spikesByClusterTemp, clusterInfoMapTemp, false);
}

bool Data::saveClusters(FILE* clusterFile)
{
    RestartTimer();

    // Binary .clu format:
    //   int32_t  nClusters
    //   nSpikes x int32_t  cluster ids in timestamp order

    SortableTable spikesByClusterTemp;
    ClusterInfoMap clusterInfoMapTemp;
    {
        QMutexLocker lk(&mutex);
        spikesByClusterTemp = *spikesByCluster;
        for (auto it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it)
            clusterInfoMapTemp.insert(it.key(), it.value());
    }

    int32_t nClusters = (int32_t)clusterInfoMapTemp.count();
    if (fwrite(&nClusters, sizeof(int32_t), 1, clusterFile) != 1) return false;

    // Sort by spike order (row 1 = feature index = timestamp order)
    spikesByClusterTemp.sort(1);

    for (long i = 1; i <= nbSpikes; ++i) {
        int32_t id = (int32_t)(spikesByClusterTemp)(2, i);
        if (fwrite(&id, sizeof(int32_t), 1, clusterFile) != 1) return false;
    }

    return true;
}


bool Data::spikePositions(int clusterId,SortableTable& subsetTable){

    //Lock the mutex to protect the changes as a whole, including the initial contains check.
    QMutexLocker lk(&mutex);

    if(!clusterInfoMap->contains(static_cast<dataType>(clusterId))){
        return false;
    }

    ClusterInfo clusterInfo  = (*clusterInfoMap)[clusterId];
    dataType firstSpikePosition = clusterInfo.firstSpikePosition();
    dataType nbSpikesOfCluster = clusterInfo.nbSpikes();

    spikesByCluster->subset(subsetTable,1,firstSpikePosition,firstSpikePosition + nbSpikesOfCluster - 1);

    return true;
}

bool Data::spikePositionsNotModified(int clusterId,SortableTable& subsetTable){
    // Like spikePositions() but atomically also checks isClusterModified().
    // Returns false if the cluster is gone OR has been flagged as modified.
    QMutexLocker lk(&mutex);

    if(!clusterInfoMap->contains(static_cast<dataType>(clusterId))){
        return false;
    }
    if(waveformStatusMap.contains(clusterId) && waveformStatusMap[clusterId].isClusterModified()){
        return false;
    }

    ClusterInfo clusterInfo  = (*clusterInfoMap)[clusterId];
    dataType firstSpikePosition = clusterInfo.firstSpikePosition();
    dataType nbSpikesOfCluster = clusterInfo.nbSpikes();

    spikesByCluster->subset(subsetTable,1,firstSpikePosition,firstSpikePosition + nbSpikesOfCluster - 1);

    return true;
}



Data::Status Data::getSampleWaveformPoints(int clusterId,dataType nbSpkToDisplay){
    //If the cluster has been suppress after the thread calling this function has been launched
    //return this information that the data are not available.
    bool clusterExists = false;
    {
        QMutexLocker lk(&mutex);
        clusterExists = clusterInfoMap->contains(static_cast<dataType>(clusterId));
    }
    if(!clusterExists) return NOT_AVAILABLE;

    //Take a sample of the spikes (displayNbSpikes) evenly distributed on all the recording.

    QString clusterIdString = QString::fromLatin1("%1").arg(clusterId);
    SortableTable positionOfSpikes = SortableTable();
    Waveforms* waveforms;
    dataType nbSpikesOfCluster = 0;

    //Does this cluster has already been processed?
    // Hold the mutex across the status check AND waveformDict lookup together.
    // Without this, the main thread can delete waveformDict[key] between our status
    // check and the pointer dereference, causing a use-after-free crash.
    bool alreadyProcessed = false;
    Status statusLocked = NOT_AVAILABLE;
    {
        QMutexLocker lk(&mutex);
        alreadyProcessed = waveformStatusMap.contains(clusterId);
        if (alreadyProcessed) {
            statusLocked = waveformStatusMap[clusterId].sampleStatus();
            if (statusLocked != IN_PROCESS)
                waveforms = waveformDict[clusterIdString];
        } else {
            // Claim the slot atomically so concurrent threads see IN_PROCESS
            // immediately, preventing the check-then-act race that allows
            // multiple threads to each allocate a new WaveformData object.
            waveformStatusMap.insert(clusterId, WaveformStatus(IN_PROCESS));
        }
    }

    if(alreadyProcessed){
        Status status = statusLocked;
        if(status == IN_PROCESS)return IN_PROCESS;
        //status == READY with the same number of spikes to present
        if((waveforms->nbOfSpikesAsked() == nbSpkToDisplay) && (status == READY))return READY;
        //status == READY with a different number of spikes to present, recollect the data
        {
            QMutexLocker lk(&mutex);
        waveformStatusMap[clusterId].setSampleStatus(IN_PROCESS);
        }
        //Check if there is not a mean calculation in process; if so, wait until it finishes.
        //Use a mutex-protected check to avoid racing with main-thread removal of the entry.
        {
            // Spin until the concurrent mean calculation finishes.
            // Cap at 5000 yields so a stuck mean thread can't block close() forever;
            // returning NOT_AVAILABLE lets the outer WaveformThread loop recheck
            // haveToStopProcessing and exit cleanly.
            bool stillInProcess = true;
            for(int _spinCount = 0; stillInProcess && _spinCount < 5000; ++_spinCount){
                {
                    QMutexLocker lk(&mutex);
                stillInProcess = waveformStatusMap.contains(clusterId) &&
                                 (waveformStatusMap[clusterId].sampleMeanStatus() == IN_PROCESS);
                }
                if(stillInProcess) QThread::yieldCurrentThread();
            }
            if(stillInProcess) return NOT_AVAILABLE;  // timed out — let caller recheck stop flag
        }
        //check if the cluster has not been removed while the mean function was running
        //if so the entry in waveformStatusMap for that cluster will have been removed  in the mean function
        if(!waveformStatusMap.contains(clusterId)) return NOT_AVAILABLE;
        {
            QMutexLocker lk(&mutex);
        waveformStatusMap[clusterId].setSampleMeanStatus(NOT_AVAILABLE);
        }

        //Check again that the cluster has not been removed or modified and get the spikes positions in a one row SortableTable.
        if(!spikePositionsNotModified(clusterId,positionOfSpikes)){
            {
                QMutexLocker lk(&mutex);
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString); //not already done by the function which modified the data as the thread is running.
            waveformStatusMap.remove(clusterId);
            }
            return NOT_AVAILABLE;
        }
        waveforms->setNbOfSpikesAsked(nbSpkToDisplay);
        //Get the spikes information
        nbSpikesOfCluster = positionOfSpikes.nbOfColumns();
        waveforms->setSize(nbSpikesOfCluster,SAMPLE);
    }
    else{
        // IN_PROCESS was already inserted atomically in the initial lock above.
        if(isTwoBytesRecording) waveforms = new WaveformData<short>(*this);
        else waveforms = new WaveformData<long>(*this);

        //Check that the cluster has not been removed or modified and get the spikes positions in a one row SortableTable.
        if(!spikePositionsNotModified(clusterId,positionOfSpikes)){
            {
                QMutexLocker lk(&mutex);
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString); //not already done by the function which modified the data as the thread is running.
            waveformStatusMap.remove(clusterId);
            }
            return NOT_AVAILABLE;
        }

        waveforms->setNbOfSpikesAsked(nbSpkToDisplay);
        //Get the spikes information
        nbSpikesOfCluster = positionOfSpikes.nbOfColumns();

        waveforms->setSize(nbSpikesOfCluster,SAMPLE);
        {
            QMutexLocker lk(&mutex);
        waveformDict.insert(clusterIdString,waveforms);
        }
    }

    FILE* spikeFile = fopen(qPrintable(spkFileName),"rb");
    if(spikeFile == nullptr){
        qCritical() << "getSampleWaveformPoints: cannot open spike file:" << spkFileName;
        return NOT_AVAILABLE;
    }

    //read and store the data
    waveforms->read(positionOfSpikes,nbSpikesOfCluster,spikeFile,nbSpkToDisplay);
    fclose(spikeFile);

    //If the cluster has been suppress or modified after the thread calling this function has been launched
    //return this information that the data are not available and remove the collected data.
    QMutexLocker lk(&mutex);
    bool clusterGone = !clusterInfoMap->contains(static_cast<dataType>(clusterId));
    bool clusterMod  = !clusterGone && waveformStatusMap.contains(clusterId) && waveformStatusMap[clusterId].isClusterModified();
    if(clusterGone || clusterMod){
        if(waveformStatusMap.contains(clusterId)){
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString);  //not already done by the function which modified the data as the thread is running.
            waveformStatusMap.remove(clusterId);
        }
        return NOT_AVAILABLE;
    }
    else{
        //Store the information in waveformStatusMap
        if(waveformStatusMap.contains(clusterId))
            waveformStatusMap[clusterId].setSampleStatus(READY);
        return READY;
    }
}

Data::Status Data::getTimeFrameWaveformPoints(int clusterId,dataType start,dataType end){
    //If the cluster has been suppress after the thread calling this function has been launched
    //return this information that the data are not available.
    bool clusterExists = false;
    {
        QMutexLocker lk(&mutex);
        clusterExists = clusterInfoMap->contains(static_cast<dataType>(clusterId));
    }
    if(!clusterExists) return NOT_AVAILABLE;

    //Take all the spikes in a given time frame
    QString clusterIdString = QString::fromLatin1("%1").arg(clusterId);
    SortableTable positionOfSpikes = SortableTable();
    dataType nbSpikesOfCluster = 0;
    dataType startInRecordingUnits = start * static_cast<dataType>(1000000.0 / samplingInterval);
    dataType endInRecordingUnits =  end * static_cast<dataType>(1000000.0 / samplingInterval);
    dataType currentSpikeIndex  = 0;
    Waveforms* waveforms;

    //Does this cluster has already been processed?
    if(waveformStatusMap.contains(clusterId)){
        // Hold the mutex across status check AND waveformDict lookup to prevent
        // main thread deleting the Waveforms* between our check and our dereference.
        Status status = NOT_AVAILABLE;
        {
            QMutexLocker lk(&mutex);
            status = waveformStatusMap[clusterId].timeFrameStatus();
            if(status != IN_PROCESS)
                waveforms = waveformDict[clusterIdString];
        }
        if(status == IN_PROCESS)return IN_PROCESS;
        dataType timeEndIndex = waveforms->indexOfTimeEnd();
        dataType timeStart = waveforms->startTime();
        dataType timeEnd = waveforms->endTime();

        //status == READY with the time frame
        if(timeStart == start && timeEnd == end && status == READY) return READY;
        {
            QMutexLocker lk(&mutex);
        waveformStatusMap[clusterId].setTimeFrameStatus(IN_PROCESS);
        }
        //Check if there is not a mean calculation in process; wait under mutex to avoid racing with main-thread removal.
        {
            // Spin until the concurrent mean calculation finishes (capped, see sample variant).
            bool stillInProcess = true;
            for(int _spinCount = 0; stillInProcess && _spinCount < 5000; ++_spinCount){
                {
                    QMutexLocker lk(&mutex);
                stillInProcess = waveformStatusMap.contains(clusterId) &&
                                 (waveformStatusMap[clusterId].timeFrameMeanStatus() == IN_PROCESS);
                }
                if(stillInProcess) QThread::yieldCurrentThread();
            }
            if(stillInProcess) return NOT_AVAILABLE;  // timed out — let caller recheck stop flag
        }
        //check if the cluster has not been removed while the mean function was running
        //if so the entry in waveformStatusMap for that cluster will have been removed  in the mean function
        if(!waveformStatusMap.contains(clusterId)) return NOT_AVAILABLE;
        {
            QMutexLocker lk(&mutex);
        waveformStatusMap[clusterId].setTimeFrameMeanStatus(NOT_AVAILABLE);
        }

        //Check again that the cluster has not been removed or modifed and get the spikes positions in a one row SortableTable.
        if(!spikePositionsNotModified(clusterId,positionOfSpikes)){
            {
                QMutexLocker lk(&mutex);
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString); //not already done by the function which modified the data as the thread is running.
            waveformStatusMap.remove(clusterId);
            }
            return NOT_AVAILABLE;
        }

        //Get the spikes information
        nbSpikesOfCluster = positionOfSpikes.nbOfColumns();
        waveforms->setSize(nbSpikesOfCluster,TIME_FRAME);
        //status == READY with a different time frame, recollect the data
        //Start from the last position where the spikes have been taken
        //(timeEndIndex is one step after the last spike taken).
        if(start == timeEnd) currentSpikeIndex =  timeEndIndex;
    }
    else{
        {
            QMutexLocker lk(&mutex);
        waveformStatusMap.insert(clusterId,WaveformStatus(NOT_AVAILABLE,IN_PROCESS));
        }
        if(isTwoBytesRecording) waveforms = new WaveformData<short>(*this);
        else waveforms = new WaveformData<long>(*this);

        //Check that the cluster has not been removed or modified and get the spikes positions in a one row SortableTable.
        if(!spikePositionsNotModified(clusterId,positionOfSpikes)){
            {
                QMutexLocker lk(&mutex);
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString); //not already done by the function which modified the data as the thread is running.
            waveformStatusMap.remove(clusterId);
            }
            return NOT_AVAILABLE;
        }
        //Get the spikes information
        nbSpikesOfCluster = positionOfSpikes.nbOfColumns();

        waveforms->setSize(nbSpikesOfCluster,TIME_FRAME);
        {
            QMutexLocker lk(&mutex);
        waveformDict.insert(clusterIdString,waveforms);
        }
    }

    //Look for the starting position if not already known
    if(currentSpikeIndex == 0){
        dataType max = nbSpikesOfCluster + 1;
        dataType currentPositionInFeatures = 0;
        dataType currentTime = 0;
        for(currentSpikeIndex = 1; currentSpikeIndex < max; ++currentSpikeIndex){
            currentPositionInFeatures = positionOfSpikes(1,currentSpikeIndex);
            currentTime = features(currentPositionInFeatures,nbDimensions);
            if(currentTime >= startInRecordingUnits) break;
        }
    }

    FILE* spikeFile = fopen(qPrintable(spkFileName),"rb");
    if(spikeFile == nullptr){
        qCritical() << "getTimeFrameWaveformPoints: cannot open spike file:" << spkFileName;
        return NOT_AVAILABLE;
    }

    //read and store the data
    waveforms->read(positionOfSpikes,nbSpikesOfCluster,spikeFile,currentSpikeIndex,endInRecordingUnits);

    fclose(spikeFile);

    // Store timing info before taking the mutex (pure local work on the Waveforms object).
    waveforms->setStartTime(start);
    waveforms->setEndTime(end);
    waveforms->setIndexOfTimeEnd(currentSpikeIndex);

    //If the cluster has been suppress or modified after the thread calling this function has been launched
    //return this information that the data are not available and remove the collected data.
    {
        QMutexLocker lk(&mutex);
    bool tfClusterGone = !clusterInfoMap->contains(static_cast<dataType>(clusterId));
    bool tfClusterMod  = !tfClusterGone && waveformStatusMap.contains(clusterId) && waveformStatusMap[clusterId].isClusterModified();
    if(tfClusterGone || tfClusterMod){
        if(waveformStatusMap.contains(clusterId)){
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString); //if not already done by the function which modified the data
            waveformStatusMap.remove(clusterId);
        }
        return NOT_AVAILABLE;
    }
    else{
        //Store the information in waveformStatusMap
        waveformStatusMap[clusterId].setTimeFrameStatus(READY);
        return READY;
    }
    }
}

template <class T>
void Data::WaveformData<T>::setSize(dataType size,WaveformMode waveformMode){
    mode = waveformMode;
    if(mode == SAMPLE){
        sampleSpikesTable.assign(static_cast<size_t>(size) * nbPtsBySpike, T{});
        sampleMeanTable.clear();
        sampleStDeviationTable.clear();
        nbSampleSpikes = 0;
    }
    else{
        timeFrameSpikesTable.assign(static_cast<size_t>(size) * nbPtsBySpike, T{});
        timeFrameMeanTable.clear();
        timeFrameStDeviationTable.clear();
        nbTimeFrameSpikes = 0;
    }
}


template <class T>
void Data::WaveformData<T>::read(SortableTable& positionOfSpikes,dataType nbSpikesOfCluster,FILE* spikeFile,dataType nbSpkToDisplay){
    // Capacity of sampleSpikesTable in elements (set by setSize() before this call).
    const dataType bufCap = static_cast<dataType>(sampleSpikesTable.size());

    //Show nbSpkToDisplay spikes or all the spikes if nbSpikesOfCluster < nbSpkToDisplay
    if(nbSpikesOfCluster < nbSpkToDisplay){
        dataType max = nbSpikesOfCluster +1;
        dataType position = 0;
        for(dataType i = 1; i < max; ++i){
            //go to the spike position
            dataType currentSpikePosition = (positionOfSpikes(1,i) - 1) * nbPtsBySpike ;
            // Guard: skip corrupt entries that would write past the buffer.
            if (position + nbPtsBySpike > bufCap) {
                qWarning("WaveformData::read: buffer overrun guard triggered at spike %d"
                         " (position=%d bufCap=%d) — cluster data may be corrupt",
                         static_cast<int>(i), static_cast<int>(position),
                         static_cast<int>(bufCap));
                break;
            }
            fseeko64(spikeFile,currentSpikePosition * sizeof(T),SEEK_SET);
            // copy the spikes into spikePoints.
            if (            fread(&(sampleSpikesTable[position]),sizeof(T),nbPtsBySpike,spikeFile) != static_cast<std::size_t>(nbPtsBySpike))
                qWarning("WaveformData::read: short fread — spike data may be truncated");

            position += nbPtsBySpike;
            ++nbSampleSpikes;
        }
    }
    //If there is only one spike to show, take the first one
    else if(nbSpkToDisplay == 1){
        //go to the spike position
        dataType currentSpikePosition = (positionOfSpikes(1,1) - 1) * nbPtsBySpike ;
        fseeko64(spikeFile,currentSpikePosition * sizeof(T),SEEK_SET);
        // copy the spikes into spikePoints.
        if (        fread(&(sampleSpikesTable[0]),sizeof(T),nbPtsBySpike,spikeFile) != static_cast<std::size_t>(nbPtsBySpike))
            qWarning("WaveformData::read: short fread — spike data may be truncated");

        nbSampleSpikes = 1;
    }
    else{
        float factor = static_cast<float>(static_cast<float>(nbSpikesOfCluster - 1) / static_cast<float>(nbSpkToDisplay - 1));
        dataType position = 0;
        dataType max = nbSpkToDisplay +1;
        float floatSpkIndice = 1;
        dataType spkIndice;
        for(float i = 1; i < max; ++i){
            spkIndice = static_cast<dataType>(floatSpkIndice + 0.5);
            //go to the spike position
            dataType currentSpikePosition = (positionOfSpikes(1,spkIndice) - 1) * nbPtsBySpike ;
            if (position + nbPtsBySpike > bufCap) {
                qWarning("WaveformData::read: buffer overrun guard (sampled path) at i=%d",
                         static_cast<int>(i));
                break;
            }
            fseeko64(spikeFile,currentSpikePosition * sizeof(T),SEEK_SET);
            // copy the spikes into spikePoints.
            if (            fread(&(sampleSpikesTable[position]),sizeof(T),nbPtsBySpike,spikeFile) != static_cast<std::size_t>(nbPtsBySpike))
                qWarning("WaveformData::read: short fread — spike data may be truncated");

            position += nbPtsBySpike;
            ++nbSampleSpikes;
            floatSpkIndice += factor;
        }
    }
}

template <class T>
void Data::WaveformData<T>::read(SortableTable& positionOfSpikes,dataType nbSpikesOfCluster,FILE* spikeFile,dataType& currentSpikeIndex,dataType end){
    dataType max = nbSpikesOfCluster +1;
    dataType position = 0;
    dataType startPositionInSpk;

    for(; currentSpikeIndex < max; ++currentSpikeIndex){
        dataType currentPositionInFeatures = positionOfSpikes(1,currentSpikeIndex);
        dataType currentTime = data.features(currentPositionInFeatures,data.nbDimensions);

        if(currentTime >= end) break;
        //positionOfSpikes and features take indices starting at 1, so currentPositionInFeatures
        //is already correct regarding the presence of an additional first line (nb of features) in the fet file.
        startPositionInSpk = (currentPositionInFeatures - 1) * nbPtsBySpike * sizeof(T);
        //go to the spike position
        fseeko64(spikeFile,startPositionInSpk,SEEK_SET);
        // copy the spikes into timeFrameSpikesTable.
        if (        fread(&(timeFrameSpikesTable[position]),sizeof(T),nbPtsBySpike,spikeFile) != static_cast<std::size_t>(nbPtsBySpike))
            qWarning("WaveformData::read: short fread — spike data may be truncated");

        position += nbPtsBySpike;
        ++nbTimeFrameSpikes;
    }
}

template <class T>
void Data::WaveformData<T>::calculateMean(WaveformMode waveformMode){
    if(waveformMode == SAMPLE){
        sampleMeanTable.assign(static_cast<size_t>(data.nbSamplesInWaveform) * data.nbChannels, T{});
        sampleStDeviationTable.assign(static_cast<size_t>(data.nbSamplesInWaveform) * data.nbChannels, T{});
        for(int i = 0; i < data.nbSamplesInWaveform; ++i){
            for(int j = 0; j < data.nbChannels; ++j){
                dataType sum = 0;
                dataType sumOfSquares = 0;
                for(dataType k = 0; k < nbSampleSpikes; ++k){
                    dataType value = sampleSpikesTable[k * (data.nbSamplesInWaveform * data.nbChannels) + (i * data.nbChannels) + j];
                    sum += value;
                    sumOfSquares += (value * value);
                }
                //The data are store as follow:
                //sample after sample and for each of them the value of channel after channel.
                dataType mean = sum / nbSampleSpikes;
                sampleMeanTable[(i * data.nbChannels) + j] = mean;
                //variance(X) = mean(X^2) - mean(X)^2
                dataType variance =  (sumOfSquares / nbSampleSpikes) - (mean * mean);
                //standard deviation = square root of the variance
                sampleStDeviationTable[(i * data.nbChannels) + j] = static_cast<dataType>(sqrt(static_cast<double>(variance)));
            }
        }
    }
    else{
        timeFrameMeanTable.assign(static_cast<size_t>(data.nbSamplesInWaveform) * data.nbChannels, T{});
        timeFrameStDeviationTable.assign(static_cast<size_t>(data.nbSamplesInWaveform) * data.nbChannels, T{});
        for(int i = 0; i < data.nbSamplesInWaveform; ++i){
            for(int j = 0; j < data.nbChannels; ++j){
                dataType sum = 0;
                dataType sumOfSquares = 0;
                for(dataType k = 0; k < nbTimeFrameSpikes; ++k){
                    dataType value = timeFrameSpikesTable[k * (data.nbSamplesInWaveform * data.nbChannels) + (i * data.nbChannels) + j];
                    sum += value;
                    sumOfSquares += (value * value);
                }
                //The data are store as follow:
                //sample after sample and for each of them the value of channel after channel.
                dataType mean = sum / nbTimeFrameSpikes;
                timeFrameMeanTable[(i * data.nbChannels) + j] = mean;
                //variance(X) = mean(X^2) - mean(X)^2
                dataType variance =  (sumOfSquares / nbTimeFrameSpikes) - (mean * mean);
                //standard deviation = square root of the variance
                timeFrameStDeviationTable[(i * data.nbChannels) + j] = static_cast<dataType>(sqrt(static_cast<double>(variance)));
            }
        }
    }
}

Data::Status Data::calculateSampleMean(int clusterId,dataType nbSpkToDisplay){
    //Calculate the mean and the standard deviation for
    //a sample of the spikes (displayNbSpikes) evenly distributed on all the recording.
    QString clusterIdString = QString::fromLatin1("%1").arg(clusterId);
    Waveforms* waveforms;

    //Does this cluster already processed?
    // Use mutex around both contains check and waveformDict lookup.
    {
        QMutexLocker lk(&mutex);
        bool sampleExists = waveformStatusMap.contains(clusterId);
        if(sampleExists){
            Status status = waveformStatusMap[clusterId].sampleMeanStatus();
            waveforms = waveformDict[clusterIdString];
            if(status == IN_PROCESS)return IN_PROCESS;
            else if(waveforms->nbOfSpikesAsked() != nbSpkToDisplay) return NOT_AVAILABLE;
            //status == READY with the same number of spikes to present
            else if((waveforms->nbOfSpikesAsked() == nbSpkToDisplay) && (status == READY))return READY;
            else{
                if(waveformStatusMap[clusterId].sampleStatus() != READY) return NOT_AVAILABLE;
                if(waveforms->nbOfSpikes(SAMPLE) == 0){
                    waveformStatusMap[clusterId].setSampleMeanStatus(NOT_AVAILABLE);
                    return READY;
                }
                waveformStatusMap[clusterId].setSampleMeanStatus(IN_PROCESS);
            }
        }
        else{
            return NOT_AVAILABLE;
        }
    } // mutex released before calculateMean

    //calculate the mean and the standard deviation and store the data
    waveforms->calculateMean(SAMPLE);
    //If the cluster has been suppress or modified after the thread calling this function has been launched
    //return this information that the data are not available.
    QMutexLocker lk(&mutex);
    bool smGone = !clusterInfoMap->contains(static_cast<dataType>(clusterId));
    bool smMod  = !smGone && waveformStatusMap.contains(clusterId) && waveformStatusMap[clusterId].isClusterModified();
    if(smGone || smMod){
        if(waveformStatusMap.contains(clusterId)){
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString);  //if not already done by the function which modified the data
            waveformStatusMap.remove(clusterId);
        }
        return NOT_AVAILABLE;
    }
    else{
        //Store the information in waveformStatusMap
        if(waveformStatusMap.contains(clusterId))
            waveformStatusMap[clusterId].setSampleMeanStatus(READY);
        return READY;
    }
}


Data::Status Data::calculateTimeFrameMean(int clusterId,dataType start,dataType end){
    //Calculate the mean and the standard deviation for
    //a sample of the spikes (displayNbSpikes) evenly distributed on all the recording.

    QString clusterIdString = QString::fromLatin1("%1").arg(clusterId);
    Waveforms* waveforms;

    //Does this cluster already processed?
    // Use mutex around both contains check and waveformDict lookup.
    {
        QMutexLocker lk(&mutex);
        bool frameExists = waveformStatusMap.contains(clusterId);
        if(frameExists){
            Status status = waveformStatusMap[clusterId].timeFrameMeanStatus();
            waveforms = waveformDict[clusterIdString];
            dataType timeStart = waveforms->startTime();
            dataType timeEnd = waveforms->endTime();

            if(status == IN_PROCESS)return IN_PROCESS;
            else if(timeStart == start && timeEnd == end && status == READY) return READY;
            else{
                if(waveformStatusMap[clusterId].timeFrameStatus() != READY) return NOT_AVAILABLE;
                if(waveforms->nbOfSpikes(TIME_FRAME) == 0){
                    waveformStatusMap[clusterId].setTimeFrameMeanStatus(NOT_AVAILABLE);
                    return READY;
                }
                waveformStatusMap[clusterId].setTimeFrameMeanStatus(IN_PROCESS);
            }
        }
        else{
            return NOT_AVAILABLE;
        }
    } // mutex released before calculateMean

    //calculate the mean and the standard deviation and store the data
    waveforms->calculateMean(TIME_FRAME);

    //If the cluster has been suppress or modifed after the thread calling this function has been launched
    //return this information that the data are not available.
    QMutexLocker lk(&mutex);
    bool tfmGone = !clusterInfoMap->contains(static_cast<dataType>(clusterId));
    bool tfmMod  = !tfmGone && waveformStatusMap.contains(clusterId) && waveformStatusMap[clusterId].isClusterModified();
    if(tfmGone || tfmMod){
        if(waveformStatusMap.contains(clusterId)){
            waveformStatusMap[clusterId].setClusterModified(false);
            delete waveformDict.take(clusterIdString);  //if not already done by the function which modified the data
            waveformStatusMap.remove(clusterId);
        }
        return NOT_AVAILABLE;
    }
    else{
        //Store the information in waveformStatusMap
        if(waveformStatusMap.contains(clusterId))
            waveformStatusMap[clusterId].setTimeFrameMeanStatus(READY);
        return READY;
    }
}


void Data::sortCluster(ClusterInfoMap* clusterInfoMapTemp,SortableTable* spikesByClusterTemp, dataType clusterId,QList<dataType> positions,
                       QList<dataType> nbOfspikes,int step,bool fromTop){
    uint nbClusters = static_cast<uint>(positions.size());
    uint indice = 0;

    ClusterInfo clusterInfo  = (*clusterInfoMapTemp)[clusterId];
    dataType nbSpikesOfCluster = clusterInfo.nbSpikes();
    dataType firstSpikePosition = clusterInfo.firstSpikePosition();

    //Update positions to set the last spikes position correctly if need it.
    //The positions were counted from the last spike of the new cluster, now
    //they will be counted from the first spike of the new cluster.
    if(!fromTop){
        for(dataType i = 0; i < static_cast<dataType>(nbClusters); ++i){
            positions[i] = nbSpikesOfCluster - positions[i] + 1;
        }
    }

    SortableTable data = SortableTable();
    //Get the spike positions in a one row SortableTable.
    spikesByClusterTemp->subset(data,1,firstSpikePosition,firstSpikePosition + nbSpikesOfCluster - 1);

    SortableTable final = SortableTable();
    final.setSize(nbSpikesOfCluster,false);
    dataType position = 1;
    dataType value = data(1,positions[0]);

    //If step == -1, the spikes for each cluster are sorted from the first one to the last one
    //by decreasing order. They will be read from last one to the first one.
    //If step == 1, the spikes for each cluster are sorted from the first one to the last one
    //by increasing order. They will be read from first one to the last one.
    while(true){
        for(uint i = 1; i < nbClusters; ++i){
            dataType current = data(1,positions[i]);
            if(current < value){
                value = current;
                indice = i;
            }
        }
        final(1,position) = value;
        position ++;
        positions[indice] += step;
        nbOfspikes[indice] --;
        if(nbOfspikes[indice] == 0){
            nbClusters --;
            positions.removeOne(positions.at(indice));
            nbOfspikes.removeOne(nbOfspikes.at(indice));
        }
        if(nbClusters == 0) break;
        indice = 0;
        value = data(1,positions[0]);
    }

    //Copy the sorted data to spikesByClusterTemp
    memcpy(&(*spikesByClusterTemp)(1,firstSpikePosition),
           &final(1,1),
           nbSpikesOfCluster * sizeof(dataType));
}

void Data::sortCluster(ClusterInfoMap* clusterInfoMapTemp,SortableTable* spikesByClusterTemp,dataType clusterId,QList<dataType> lastPositions,QList<dataType> nbOfspikes,dataType firstPosition,dataType number){
    //Initialize the variables concerning the previous data of clusterId (which is 0 or 1).
    dataType originalFirstPosition = firstPosition;
    dataType originalNb = number;
    uint nbClusters = static_cast<uint>(lastPositions.size());
    uint indice = 0;

    ClusterInfo clusterInfo  = (*clusterInfoMapTemp)[clusterId];
    dataType nbSpikesOfCluster = clusterInfo.nbSpikes();
    dataType firstSpikePosition = clusterInfo.firstSpikePosition();

    SortableTable data = SortableTable();
    //Get the spikes positions in a one row SortableTable.
    spikesByClusterTemp->subset(data,1,firstSpikePosition,firstSpikePosition + nbSpikesOfCluster - 1);

    SortableTable final = SortableTable();
    final.setSize(nbSpikesOfCluster,false);
    dataType position = 1;
    dataType value = data(1,lastPositions[0]);

    //The spikes for each cluster are sorted from the last one to the first one
    //by increasing order.
    if(originalNb > 0){
        while(true){
            for(uint i = 1; i < nbClusters; ++i){
                dataType current = data(1,lastPositions[i]);
                if(current < value){
                    value = current;
                    indice = i;
                }
            }
            //work on the original spikes of the new cluster (which is either cluster 0 or cluster 1).
            dataType current = data(1,originalFirstPosition);
            if(current < value){
                final(1,position) = current;
                position ++;
                originalNb --;
                if(originalNb == 0)break;
                originalFirstPosition ++;
            }
            else{
                final(1,position) = value;
                position ++;
                lastPositions[indice]--;
                nbOfspikes[indice] --;
                if(nbOfspikes[indice] == 0){
                    nbClusters --;
                    lastPositions.removeOne(lastPositions.at(indice));
                    nbOfspikes.removeOne(nbOfspikes.at(indice));
                }
                //Copy all the reminding original spikes of the new cluster.
                if(nbClusters == 0){
                    memcpy(&(final(1,position)),
                           &(data(1,originalFirstPosition)),
                           originalNb * sizeof(dataType));
                    break;
                }
                indice = 0;
                value = data(1,lastPositions[0]);
            }
        }
    }
    //All the original spikes have been sort, now deal with the reminding ones coming
    //from the other clusters.
    if(originalNb == 0 && nbClusters != 0){
        indice = 0;
        value = data(1,lastPositions[0]);
        while(true){
            for(uint i = 1; i < nbClusters; ++i){
                dataType current = data(1,lastPositions[i]);
                if(current < value){
                    value = current;
                    indice = i;
                }
            }
            final(1,position) = value;
            position ++;
            lastPositions[indice]--;
            nbOfspikes[indice] --;
            if(nbOfspikes[indice] == 0){
                nbClusters --;
                lastPositions.removeOne(lastPositions.at(indice));
                nbOfspikes.removeOne(nbOfspikes.at(indice));
            }
            if(nbClusters == 0) break;
            indice = 0;
            value = data(1,lastPositions[0]);
        }
    }

    //Copy the sorted data to spikesByClusterTemp
    memcpy(&(*spikesByClusterTemp)(1,firstSpikePosition),
           &final(1,1),
           nbSpikesOfCluster * sizeof(dataType));
}

Data::Status Data::getCorrelograms(Pair& pair,int binSize,int timeWindow,double binSizeInRU,float timeWindowInRU,int halfBins){
    int cluster1 = pair.first;
    int cluster2 = pair.second;
    Pair parameters{binSize,timeWindow};
    QHash<QString, Correlation*>* dict = nullptr;

    //Test first if the clusters still exist
    {
        QMutexLocker lk(&mutex);
        bool cluster1Removed = !clusterInfoMap->contains(static_cast<dataType>(cluster1));
        bool cluster2Removed = !clusterInfoMap->contains(static_cast<dataType>(cluster2));
        if(cluster1Removed || cluster2Removed)return NOT_AVAILABLE;
    }

    //Test if the correlogram is in process or already available.
    Status status = NOT_AVAILABLE;
    {
        QMutexLocker lk(&mutex);
    if(correlationDict[pairKey(pair)] != 0){
        dict = correlationDict[pairKey(pair)];
        if((*dict)[pairKey(parameters)] != 0){
            if(((*dict)[pairKey(parameters)])->getStatus(binSize,timeWindow) == IN_PROCESS) status = IN_PROCESS;
            else if(((*dict)[pairKey(parameters)])->getStatus(binSize,timeWindow) == READY) status = READY;
        }
    }
    }

    if(status != NOT_AVAILABLE) return status;

    //Test if the correlogram, for the given parametres, is not available, if so compute it.
    //If the pair does not exist or the binSize and/or the timeFrame are different, the correlogram will have to be computed.
    bool computeCorrelogram = false;
    {
        QMutexLocker lk(&mutex);
    dict = correlationDict[pairKey(pair)];
    if(correlationDict[pairKey(pair)] == 0 || ((*dict)[pairKey(parameters)] == 0))
        computeCorrelogram = true;
    }

    if(computeCorrelogram){
        //Advice that a correlation is in process on the cluster1 and cluster2.
        {
            QMutexLocker lk(&mutex);
        correlationsInProcess.addProcess(static_cast<dataType>(cluster1));
        correlationsInProcess.addProcess(static_cast<dataType>(cluster2));
        }

        //Create the correlation object or retrieve it if it already exists.
        //In case several threads, working on the same pair with the same parameters, get to this point, make sure that only one will
        //performs the computation.
        bool correlationAlreadyInProcess = false;
        Correlation* correlation = nullptr;
        {
            QMutexLocker lk(&mutex);
        dict = correlationDict[pairKey(pair)];
        if(dict == 0){
            dict = new QHash<QString, Correlation*>();
            correlation = new Correlation(*this,binSize,timeWindow);
            correlation->setStatus(IN_PROCESS);
            dict->insert(pairKey(parameters),correlation);
            correlationDict.insert(pairKey(pair),dict);
        }
        else if((*dict)[pairKey(parameters)] == 0){
            correlation = new Correlation(*this,binSize,timeWindow);
            correlation->setStatus(IN_PROCESS);
            dict->insert(pairKey(parameters),correlation);
        }
        else if(((*dict)[pairKey(parameters)] != 0) && (*dict)[pairKey(parameters)]->getStatus() != IN_PROCESS){
            correlation = (*dict)[pairKey(parameters)];
            correlation->setStatus(IN_PROCESS);
            correlation->reset();
            correlation->setBinSize(binSize);
            correlation->setTimeWindow(timeWindow);
        }
        else correlationAlreadyInProcess = true;
        }

        if(correlationAlreadyInProcess){
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster1));
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster2));
            }
            return IN_PROCESS;
        }

        //If cluster1 or cluster2 have been suppress after the thread calling this function has been launched
        //skip this pair.
        bool clusterNotAvailable = false;
        bool autoCorrelogram = false;
        SortableTable spikesOfCluster1 = SortableTable();
        SortableTable spikesOfCluster2 =  SortableTable();

        if(cluster1 == cluster2) autoCorrelogram = true;

        //Get the spikes positions for the cluster1 in a one row SortableTable.
        if(!spikePositions(cluster1,spikesOfCluster1)){
            cleanCorrelation(static_cast<dataType>(cluster1),clusterIds(),true);
            clusterNotAvailable = true;
        }
        //Get the spikes positions for the cluster2 in a one row SortableTable.
        if(!autoCorrelogram && (!spikePositions(cluster2,spikesOfCluster2))){
            cleanCorrelation(static_cast<dataType>(cluster2),clusterIds(),true);
            clusterNotAvailable = true;
        }
        if(clusterNotAvailable){
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster1));
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster2));
            delete correlationDict.take(pairKey(pair)); //if the clusters do not exist anymore they would not have been
            //removed in cleanCorrelation
            }
            return NOT_AVAILABLE;
        }

        //Check if either the cluster1 or the cluster2 have been modified since the thread has been launched
        {
            QMutexLocker lk(&mutex);
        if(correlationsInProcess.isClusterModified(static_cast<dataType>(cluster1))){
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster1));
            delete correlationDict.take(pairKey(pair));
            clusterNotAvailable = true;
        }
        if(correlationsInProcess.isClusterModified(static_cast<dataType>(cluster2))){
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster2));
            delete correlationDict.take(pairKey(pair));
            clusterNotAvailable = true;
        }
        }
        if(clusterNotAvailable) return NOT_AVAILABLE;


        //Compute the correlogram.
        if(!autoCorrelogram) correlation->calculateCorrelation(spikesOfCluster1,spikesOfCluster2,binSizeInRU,timeWindowInRU,halfBins,autoCorrelogram);
        else correlation->calculateCorrelation(spikesOfCluster1,spikesOfCluster1,binSizeInRU,timeWindowInRU,halfBins,autoCorrelogram);

        //If cluster1 or cluster2 have been suppress or modifed after the thread calling this function has been launched
        //skip this pair.
        if(!clusterInfoMap->contains(static_cast<dataType>(cluster1))){
            cleanCorrelation(static_cast<dataType>(cluster1),clusterIds(),true);
            clusterNotAvailable = true;
        }
        if(!autoCorrelogram && (!clusterInfoMap->contains(static_cast<dataType>(cluster2)))){
            cleanCorrelation(static_cast<dataType>(cluster2),clusterIds(),true);
            clusterNotAvailable = true;
        }
        if(clusterNotAvailable){
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster1));
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster2));
            delete correlationDict.take(pairKey(pair)); //if the clusters do not exist anymore they would not have been
            //removed in cleanCorrelation
            }
            return NOT_AVAILABLE;
        }

        {
            QMutexLocker lk(&mutex);
        if(correlationsInProcess.isClusterModified(static_cast<dataType>(cluster1))){
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster1));
            delete correlationDict.take(pairKey(pair));
            clusterNotAvailable = true;
        }
        if(correlationsInProcess.isClusterModified(static_cast<dataType>(cluster2))){
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster2));
            delete correlationDict.take(pairKey(pair));
            clusterNotAvailable = true;
        }
        if(!clusterNotAvailable){
            //Update the status
            correlation->setStatus(READY);

            //Update the correlation status of the cluster1 and cluster2.
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster1));
            correlationsInProcess.removeProcess(static_cast<dataType>(cluster2));
        }
        }
        if(clusterNotAvailable) return NOT_AVAILABLE;
    }
    return READY;
}

void Data::Correlation::calculateCorrelation(SortableTable& spikesOfCluster1,SortableTable& spikesOfCluster2,double binSizeInRU,double timeWindowInRU,int halfBins,bool autoCorrelogram){
    dataType cluster1NbSpikesPlusOne = spikesOfCluster1.nbOfColumns() + 1;
    dataType cluster2NbSpikes = spikesOfCluster2.nbOfColumns();
    dataType cluster2NbSpikesPlusOne = cluster2NbSpikes + 1;
    dataType spikeOfCluster2 = 1;
    double timeOfCluster1;
    double lowerBound;
    double upperBound;

    int totalNbBins = (2 * halfBins) + 1;
    setNbBins(totalNbBins);
    //Initialize the array which will contain the correlogram data.
    values.assign(totalNbBins, 0u);
    //One additional bin is used for the upper boundary (and his content is later added to the last bin)
    std::vector<uint> tmpValues(totalNbBins + 1, 0u);

    //Cluster 1 will be the cluster of reference.
    for(dataType spikeOfCluster1 = 1; spikeOfCluster1 < cluster1NbSpikesPlusOne; ++spikeOfCluster1){

        timeOfCluster1 = data.spikeTime(spikesOfCluster1,spikeOfCluster1);

        lowerBound = timeOfCluster1 - timeWindowInRU/2;
        upperBound = timeOfCluster1 + timeWindowInRU/2;

        //If the last spike of cluster2 is before the lower limit the computation is over.
        if(data.spikeTime(spikesOfCluster2,cluster2NbSpikes) < lowerBound) break;

        //Move along cluster2's spikes while the time of the spike is lower than lowerBound.
        for(;spikeOfCluster2 < cluster2NbSpikesPlusOne && data.spikeTime(spikesOfCluster2,spikeOfCluster2) < lowerBound; ++spikeOfCluster2){}

        //Loop backwards along cluster2's spikes and store the bin value as long as we still in the time frame (>= lowerBound)
        if(spikeOfCluster2 > 1){
            double backwardsTime;
            for(dataType backwardsSpike = spikeOfCluster2 - 1;backwardsSpike >= 1 && (backwardsTime = data.spikeTime(spikesOfCluster2,backwardsSpike)) >= lowerBound; --backwardsSpike){

                //calculate the bin.
                int bin = halfBins + static_cast<int>(floor(0.5 + (backwardsTime - timeOfCluster1)/ static_cast<double>(binSizeInRU)));
                if ( bin < 0 ) bin = 0;
                tmpValues[bin]++;
            }
        }

        //Loop forwards along cluster2's spikes and store the bin value as long as we still in the time frame (<= upperBound)
        double forwardsTime;
        //All the spikes at the limit of 2 bins are included in the left one if the time is not round.
        //The same thing is done for the last bin, so a spike of cluster2 having a time difference of timeWindowInRu
        //with a spike of cluster1 will not be computed.
        for(;spikeOfCluster2 < cluster2NbSpikesPlusOne && (forwardsTime = data.spikeTime(spikesOfCluster2,spikeOfCluster2)) < upperBound; ++spikeOfCluster2){
            //calculate the bin.
            int bin = halfBins + static_cast<int>(floor(0.5 + (forwardsTime - timeOfCluster1)/ static_cast<double>(binSizeInRU)));
            if ( bin < 0 ) bin = 0;
            tmpValues[bin]++;
        }

    }

    //If it is an autocorrelogram, remove the center bin.
    if(autoCorrelogram) tmpValues[halfBins] = 0;

    //Update last bin (see comment above)
    tmpValues[2 * halfBins] += tmpValues[totalNbBins];
    //Store values
    memcpy(values.data(), tmpValues.data(),totalNbBins * sizeof(uint));


    //Calculate the maximum and the shoulder
    for(int i = 0; i < totalNbBins; ++i)
        if(values[i] > max) max = values[i];

    //Computation of the asymptote: N1*N2*(binSize/Time)
    //with Time, time between the first common spike and the last common spike.
    double time;
    if(autoCorrelogram){
        //Computation also of the firing rate: nbSpikes / Time converted in seconds.
        time = data.spikeTime(spikesOfCluster2,cluster2NbSpikes) - data.spikeTime(spikesOfCluster2,1);
        if(time == 0){
            asymptote = 0;
            firingRate = 0;
        }
        else{
            asymptote = static_cast<float>(
                        static_cast<double>(cluster2NbSpikes) * static_cast<double>(cluster2NbSpikes) *
                        (static_cast<double>(binSizeInRU) / static_cast<double>(time))
                        );

            double timeInS = static_cast<double>(time *data.samplingInterval) / 1000000.0;
            firingRate = static_cast<float>(
                        static_cast<double>(cluster2NbSpikes) / (static_cast<long>(timeInS) + 1 )
                        );
        }
    }
    else{
        double clu1T1 = data.spikeTime(spikesOfCluster1,1);
        double clu2T1 = data.spikeTime(spikesOfCluster2,1);
        double clu1T2 = data.spikeTime(spikesOfCluster1,cluster1NbSpikesPlusOne - 1);
        double clu2T2 = data.spikeTime(spikesOfCluster2,cluster2NbSpikes);
        double T1;
        double T2;
        dataType clu1Spk1;
        dataType clu1Spk2;
        dataType clu2Spk1;
        dataType clu2Spk2;

        if((clu1T2 < clu2T1) || (clu2T2 < clu1T1)) asymptote = 0;
        else{
            if(clu1T1 < clu2T1){
                T1 = clu2T1;
                if(clu1T2 < clu2T2){
                    T2 = clu1T2;
                    //Search the number of spikes of the 2 clusters within "time"
                    clu1Spk1 = data.findSpikePosition(T1,spikesOfCluster1);
                    clu1Spk2 = cluster1NbSpikesPlusOne - 1;
                    clu2Spk1 = 1;
                    clu2Spk2 = data.findSpikePosition(T2,spikesOfCluster2);
                }
                else{
                    T2 = clu2T2;
                    //Search the number of spikes of the 2 clusters within "time"
                    clu1Spk1 = data.findSpikePosition(T1,spikesOfCluster1);
                    clu1Spk2 = data.findSpikePosition(T2,spikesOfCluster1);
                    clu2Spk1 = 1;
                    clu2Spk2 = cluster2NbSpikes;
                }
            }
            else{
                T1 = clu1T1;
                if(clu1T2 < clu2T2){
                    T2 = clu1T2;
                    //Search the number of spikes of the 2 clusters within "time"
                    clu1Spk1 = 1;
                    clu1Spk2 = cluster1NbSpikesPlusOne - 1;
                    clu2Spk1 = data.findSpikePosition(T1,spikesOfCluster2);
                    clu2Spk2 = data.findSpikePosition(T2,spikesOfCluster2);
                }
                else{
                    T2 = clu2T2;
                    //Search the number of spikes of the 2 clusters within "time"
                    clu1Spk1 = 1;
                    clu1Spk2 = data.findSpikePosition(T2,spikesOfCluster1);
                    clu2Spk1 = data.findSpikePosition(T1,spikesOfCluster2);
                    clu2Spk2 = cluster2NbSpikes;
                }
            }
            time = T2 - T1;
            if(time == 0) asymptote = 0;
            else asymptote = static_cast<float>(
                        static_cast<double>(clu1Spk2 - clu1Spk1 + 1) * static_cast<double>(clu2Spk2 - clu2Spk1 + 1) *
                        static_cast<double>(static_cast<double>(binSizeInRU) / static_cast<double>(time))
                        );
        }
    }
}

void Data::cleanCorrelation(dataType clusterId,const QList<dataType>& currentClusterList,bool cleanProcess){
    {
        QMutexLocker lk(&mutex);
    if(cleanProcess) correlationsInProcess.removeCluster(clusterId);

    //Remove the autocorrelogram separatly as the clusterID has already been removed from
    //the list of clusters.
    delete correlationDict.take(pairKey(static_cast<int>(clusterId),static_cast<int>(clusterId)));

    //Gets all the clustersId currently available

    //Remove all the correlations link to clusterId
    QList<dataType>::const_iterator iterator;
    QList<dataType>::const_iterator end(currentClusterList.end());
    for(iterator = currentClusterList.begin(); iterator != end; ++iterator){
        //Search pairs as (clusterId,*iterator) where clusterId > *iterator
        //and (*iterator,clusterId) where *iterator > clusterId
        if(*iterator <= clusterId) delete correlationDict.take(pairKey(static_cast<int>(*iterator),static_cast<int>(clusterId)));
        else delete correlationDict.take(pairKey(static_cast<int>(clusterId),static_cast<int>(*iterator)));
    }
    }
}

void Data::renumberCorrelation(QMap<int,int>& clusterIdsOldNew){
    //Get all the old cluster ids
    QList<int> oldClusterIds = clusterIdsOldNew.keys();

    QList<int>::iterator iterator;
    {
        QMutexLocker lk(&mutex);
    int i = 0;
    for(iterator = oldClusterIds.begin(); iterator != oldClusterIds.end(); ++iterator){
        if(correlationsInProcess.contains(*iterator)){
            correlationsInProcess.setClusterModified(*iterator,true);
            continue;
        }
        for(int j = i; j<static_cast<int>(oldClusterIds.count());j++) {
            int val = oldClusterIds.at(i);
            int val2 = oldClusterIds.at(j);
            if(val2 <= val){
                QHash<QString, Correlation*>* dict = correlationDict.take(pairKey(val2,val));
                if(dict != 0)
                    correlationDict.insert(pairKey(clusterIdsOldNew[val2],clusterIdsOldNew[val]),dict);
            }
            else{
                QHash<QString, Correlation*>* dict = correlationDict.take(pairKey(val,val2));
                if(dict != 0)
                    correlationDict.insert(pairKey(clusterIdsOldNew[val],clusterIdsOldNew[val2]),dict);
            }

        }
        ++i;
    }
    }
}

long Data::findSpikePosition(double time,SortableTable& spikesOfCluster){
    dataType clusterNbSpikes = spikesOfCluster.nbOfColumns();
    double currentTime = spikeTime(spikesOfCluster,clusterNbSpikes);
    if(currentTime == time) return clusterNbSpikes;


    int largeStep = 400;
    int smallStep = 20;
    int step = largeStep;
    long i = step + 1;

    for(; i <= clusterNbSpikes; i += step){
        double currentTime = spikeTime(spikesOfCluster,i);
        if(currentTime < time) continue;
        else{
            if(step != 1){
                i -= step;
                if(step == largeStep) step = smallStep;
                else step = 1;
            }
            else{
                return i;
            }
        }
    }

    //Process the last spikes separately
    if(step == largeStep){
        i -= step;
        step = smallStep;
        for(; i <= clusterNbSpikes; i += step){
            double currentTime = spikeTime(spikesOfCluster,i);
            if(currentTime < time) continue;
            else{
                if(step != 1){
                    i -= step;
                    step = 1;
                }
                else{
                    return i;
                }
            }
        }
    }
    //loop with a step of 1
    i -= step;
    for(;i <= clusterNbSpikes; ++i){
        double currentTime = spikeTime(spikesOfCluster,i);
        if(currentTime < time) continue;
        else{
            return i;
        }
    }

    return i;
}

void Data::duplicate(SortableTable* & spikesOfClusterTemp,ClusterInfoMap* & clusterInfoMapTemp){
    //The mutex protect spikesByCluster and clusterInfoMap so that only one thread can
    //access them at the time.
    {
        QMutexLocker lk(&mutex);
    spikesOfClusterTemp = new SortableTable(*spikesByCluster);
    clusterInfoMapTemp = new ClusterInfoMap(*clusterInfoMap);
    }
}

// ────────────────────────────────────────────────────────────────────────
// patch76 — Mean-subtracted sub-dimensional feature file for a single
// cluster.  Algorithm (works on feature columns 1..nbDimensions-1; the
// last column is the timestamp and gets passed through unchanged):
//
//   1. Pool the cluster's spike feature rows into a centered matrix R,
//      where R[i,j] = features(spikeRow_i, j) − mean[j].
//   2. Build the D×D symmetric covariance matrix C = R^T R / nSpikes.
//   3. Diagonalise C with cyclic Jacobi rotations (D small, ≤ ~50 for
//      typical electrode-group sizes; converges in 10-20 sweeps).
//   4. Sort eigenpairs by descending eigenvalue, keep top K.
//   5. Project each spike's residual onto those K eigenvectors:
//        residPCA[i, k] = sum_j R[i, j] * eigvec[j, k]
//   6. Quantise to int64 and write the .fet file with K+1 dimensions
//      (K residual-PCA dims + 1 original timestamp dim).
//
// Quantisation: feature values in the existing .fet files are int64.
// Residual-PCA outputs are doubles that can have a wide dynamic range.
// We rescale each output column independently so its range matches
// the dynamic range of the original features in that cluster (the
// 99.5 percentile spread).  This keeps the K residual columns
// numerically commensurate with each other and with the timestamp
// column, so KKExp's variance-based feature scaling treats them on
// equal footing.
//
// Cost: O(nSpikes × D²) for the covariance build, O(D³) per Jacobi
// sweep, O(nSpikes × D × K) for the projection.  For typical D ≈ 24
// and nSpikes ≈ 10000, the whole thing runs in a fraction of a
// second.  No external linear-algebra dependency required.
// ────────────────────────────────────────────────────────────────────────
int Data::createMeanSubtractedSubdimFeatureFile(int clusterId, int K,
                                                QFile& fetFile,
                                                QVector<double>* eigvalsOut)
{
    if (!clusterInfoMap || !spikesByCluster)
        return 0;
    const dataType cid = static_cast<dataType>(clusterId);
    if (!clusterInfoMap->contains(cid)) return 0;
    const ClusterInfo info = (*clusterInfoMap)[cid];
    const dataType firstPos = info.firstSpikePosition();
    const dataType nSp      = info.nbSpikes();
    if (nSp < 3) return 0;        // need at least 3 spikes for a meaningful covariance
    if (nbDimensions < 2) return 0;

    // D = feature columns excluding the trailing timestamp column.
    const int D = nbDimensions - 1;
    K = std::max(1, std::min(K, D));

    // patch76-fix1 — populate reclusteringSpikesByCluster.
    //
    // integrateReclusteredClusters → loadReclusteredClusters reads
    // reclusteringSpikesByCluster.nbOfColumns() to know how many
    // spike labels to expect from the .clu file, and uses row 1 to
    // map back to global spike row indices.  Without this setup the
    // integration aborts with INCORRECT_CONTENT and the user sees
    // "The temporary file containing the new clusters contains
    // incorrect data." — the canonical createFeatureFile does this
    // setup; we missed it on the subdim path.
    //
    // Mirror the createFeatureFile setup for the single cluster:
    //   row 1 of reclusteringSpikesByCluster gets the global spike
    //                row indices into the features table
    //   row 2 is overwritten by loadReclusteredClusters with the
    //                offset-by-highestClusterId new labels
    reclusteringSpikesByCluster.setSize(nSp);
    memcpy(&(reclusteringSpikesByCluster)(1, 1),
           &(*spikesByCluster)(1, firstPos),
           nSp * sizeof(dataType));
    memcpy(&(reclusteringSpikesByCluster)(2, 1),
           &(*spikesByCluster)(2, firstPos),
           nSp * sizeof(dataType));

    // ── (1) Compute the cluster's mean over the D non-time columns. ──
    std::vector<double> mu(static_cast<size_t>(D), 0.0);
    for (dataType s = 0; s < nSp; ++s) {
        const dataType row = (*spikesByCluster)(1, firstPos + s);
        for (int j = 0; j < D; ++j) {
            // features is 1-indexed in both row and column.
            mu[static_cast<size_t>(j)] +=
                static_cast<double>(features(row, j + 1));
        }
    }
    const double invN = 1.0 / static_cast<double>(nSp);
    for (int j = 0; j < D; ++j) mu[static_cast<size_t>(j)] *= invN;

    // ── (2) Build symmetric covariance C = R^T R / nSp. ──
    std::vector<double> C(static_cast<size_t>(D) * D, 0.0);
    std::vector<double> resid(static_cast<size_t>(D));
    for (dataType s = 0; s < nSp; ++s) {
        const dataType row = (*spikesByCluster)(1, firstPos + s);
        for (int j = 0; j < D; ++j)
            resid[static_cast<size_t>(j)] =
                static_cast<double>(features(row, j + 1))
              - mu[static_cast<size_t>(j)];
        for (int j = 0; j < D; ++j) {
            const double rj = resid[static_cast<size_t>(j)];
            for (int k = j; k < D; ++k)
                C[static_cast<size_t>(j) * D + k] +=
                    rj * resid[static_cast<size_t>(k)];
        }
    }
    for (int j = 0; j < D; ++j) {
        for (int k = j; k < D; ++k) {
            C[static_cast<size_t>(j) * D + k] *= invN;
            C[static_cast<size_t>(k) * D + j] =
                C[static_cast<size_t>(j) * D + k];
        }
    }

    // ── (3) Cyclic Jacobi eigendecomposition. ──
    //   Maintains an eigenvector matrix V (initially identity).
    //   Each sweep zeroes the largest off-diagonal then rotates rows
    //   and columns of both C and V by the corresponding 2×2 Givens.
    std::vector<double> V(static_cast<size_t>(D) * D, 0.0);
    for (int j = 0; j < D; ++j) V[static_cast<size_t>(j) * D + j] = 1.0;
    const int    maxSweeps = 100;
    const double tol       = 1e-12;
    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        double offSum = 0.0;
        for (int p = 0; p < D - 1; ++p)
            for (int q = p + 1; q < D; ++q) {
                const double cpq = C[static_cast<size_t>(p) * D + q];
                offSum += cpq * cpq;
            }
        if (offSum < tol) break;
        for (int p = 0; p < D - 1; ++p) {
            for (int q = p + 1; q < D; ++q) {
                const double apq = C[static_cast<size_t>(p) * D + q];
                if (std::abs(apq) < 1e-18) continue;
                const double app = C[static_cast<size_t>(p) * D + p];
                const double aqq = C[static_cast<size_t>(q) * D + q];
                const double theta = (aqq - app) / (2.0 * apq);
                const double t = (theta >= 0.0)
                    ?  1.0 / (theta + std::sqrt(1.0 + theta * theta))
                    :  1.0 / (theta - std::sqrt(1.0 + theta * theta));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = t * c;
                C[static_cast<size_t>(p) * D + p] = app - t * apq;
                C[static_cast<size_t>(q) * D + q] = aqq + t * apq;
                C[static_cast<size_t>(p) * D + q] = 0.0;
                C[static_cast<size_t>(q) * D + p] = 0.0;
                for (int r = 0; r < D; ++r) {
                    if (r != p && r != q) {
                        const double arp = C[static_cast<size_t>(r) * D + p];
                        const double arq = C[static_cast<size_t>(r) * D + q];
                        C[static_cast<size_t>(r) * D + p] = c * arp - s * arq;
                        C[static_cast<size_t>(p) * D + r] =
                            C[static_cast<size_t>(r) * D + p];
                        C[static_cast<size_t>(r) * D + q] = s * arp + c * arq;
                        C[static_cast<size_t>(q) * D + r] =
                            C[static_cast<size_t>(r) * D + q];
                    }
                    const double vrp = V[static_cast<size_t>(r) * D + p];
                    const double vrq = V[static_cast<size_t>(r) * D + q];
                    V[static_cast<size_t>(r) * D + p] = c * vrp - s * vrq;
                    V[static_cast<size_t>(r) * D + q] = s * vrp + c * vrq;
                }
            }
        }
    }

    // ── (4) Sort eigenpairs by descending eigenvalue, take top K. ──
    std::vector<std::pair<double, int>> sortedEv(D);
    for (int j = 0; j < D; ++j)
        sortedEv[static_cast<size_t>(j)] =
            std::make_pair(C[static_cast<size_t>(j) * D + j], j);
    std::sort(sortedEv.begin(), sortedEv.end(),
        [](const std::pair<double,int>& a, const std::pair<double,int>& b) {
            return a.first > b.first;
        });
    if (eigvalsOut) {
        eigvalsOut->clear();
        eigvalsOut->reserve(K);
        for (int k = 0; k < K; ++k)
            eigvalsOut->append(sortedEv[static_cast<size_t>(k)].first);
    }

    // Pack top-K eigenvectors into evK[D × K] (column-major: evK[j*K+k]).
    std::vector<double> evK(static_cast<size_t>(D) * K, 0.0);
    for (int k = 0; k < K; ++k) {
        const int origCol = sortedEv[static_cast<size_t>(k)].second;
        for (int r = 0; r < D; ++r)
            evK[static_cast<size_t>(r) * K + k] =
                V[static_cast<size_t>(r) * D + origCol];
    }

    // ── (5) Project residuals onto top-K eigenvectors.  Also tally
    //       per-column min/max so we can rescale to a sensible int64
    //       quantisation range. ──
    std::vector<double> proj(static_cast<size_t>(nSp) * K, 0.0);
    std::vector<double> minVal(static_cast<size_t>(K),
                               std::numeric_limits<double>::infinity());
    std::vector<double> maxVal(static_cast<size_t>(K),
                               -std::numeric_limits<double>::infinity());
    for (dataType s = 0; s < nSp; ++s) {
        const dataType row = (*spikesByCluster)(1, firstPos + s);
        for (int j = 0; j < D; ++j)
            resid[static_cast<size_t>(j)] =
                static_cast<double>(features(row, j + 1))
              - mu[static_cast<size_t>(j)];
        for (int k = 0; k < K; ++k) {
            double val = 0.0;
            for (int j = 0; j < D; ++j)
                val += resid[static_cast<size_t>(j)]
                     * evK[static_cast<size_t>(j) * K + k];
            proj[static_cast<size_t>(s) * K + k] = val;
            if (val < minVal[static_cast<size_t>(k)])
                minVal[static_cast<size_t>(k)] = val;
            if (val > maxVal[static_cast<size_t>(k)])
                maxVal[static_cast<size_t>(k)] = val;
        }
    }

    // Rescale each projected column to ±2^28 range (well clear of
    // int64 overflow, comparable to the dynamic range of normalised
    // features in the original .fet).
    const double targetMax = static_cast<double>(1 << 28);
    std::vector<double> scale(static_cast<size_t>(K), 1.0);
    for (int k = 0; k < K; ++k) {
        const double span = std::max(std::abs(minVal[static_cast<size_t>(k)]),
                                     std::abs(maxVal[static_cast<size_t>(k)]));
        scale[static_cast<size_t>(k)] = (span > 1e-12)
            ? targetMax / span
            : 1.0;
    }

    // ── (6) Write .fet: int32 nDim header, then per-spike int64 rows
    //       of (K residual-PCA + 1 timestamp).  Match the existing
    //       binary format used by createFeatureFile above. ──
    fetFile.close();
    FILE* ff = fopen(fetFile.fileName().toLocal8Bit().constData(), "wb");
    if (!ff) return 0;
    const int32_t nDim32 = static_cast<int32_t>(K + 1);
    fwrite(&nDim32, sizeof(int32_t), 1, ff);
    for (dataType s = 0; s < nSp; ++s) {
        const dataType row = (*spikesByCluster)(1, firstPos + s);
        for (int k = 0; k < K; ++k) {
            const double v = proj[static_cast<size_t>(s) * K + k]
                           * scale[static_cast<size_t>(k)];
            const int64_t iv = static_cast<int64_t>(v);
            fwrite(&iv, sizeof(int64_t), 1, ff);
        }
        // Pass timestamp through unchanged so KKExp's chunk-by-time
        // machinery still works.
        const int64_t ts = static_cast<int64_t>(features(row, nbDimensions));
        fwrite(&ts, sizeof(int64_t), 1, ff);
    }
    fclose(ff);
    return K + 1;
}

// Raw-waveform median-residual variant of the sub-dimensional path.
// Centres each spike on the cluster's per-(channel,sample) MEDIAN waveform
// (robust to drift and outliers), then PCA-projects the residual waveforms and
// writes the top-K components as the recluster .fet.  Mirrors the .fet format
// and reclusteringSpikesByCluster setup of createMeanSubtractedSubdimFeatureFile,
// but D' = nbPtsBySpike (raw waveform points) and the samples come from .spk.
//
// Memory: an exact per-point median needs one in-memory copy of the cluster's
// spikes; that copy is held in the native acquisition width (T = int16 for
// two-byte recordings, int32 otherwise) rather than as double, roughly halving
// the footprint of the earlier float store.  The residual covariance is then
// accumulated in a single streaming pass over that store (only a Dp-length
// residual scratch is allocated; no nSp x Dp residual matrix), and the
// projection streams the same way.
template <class T>
int Data::medianWaveformResidualImpl(const QList<int>& clusterIds, int K,
                                     QFile& fetFile, QVector<double>* eigvalsOut)
{
    if (!clusterInfoMap || !spikesByCluster) return 0;
    if (clusterIds.isEmpty()) return 0;

    // Total spike count across the pooled clusters; validate each exists.
    dataType nSp = 0;
    for (int c : clusterIds) {
        const dataType cc = static_cast<dataType>(c);
        if (!clusterInfoMap->contains(cc)) return 0;
        nSp += (*clusterInfoMap)[cc].nbSpikes();
    }
    if (nSp < 3) return 0;
    if (nbDimensions < 2) return 0;

    const int Dp = nbChannels * nbSamplesInWaveform;   // raw waveform points
    if (Dp < 2) return 0;
    K = std::max(1, std::min(K, Dp));

    // Pool the clusters into reclusteringSpikesByCluster, mirroring
    // createFeatureFile: row 1 = global spike indices into the feature table,
    // row 2 = the (later overwritten) labels.  Required or integration aborts.
    reclusteringSpikesByCluster.setSize(nSp);
    {
        dataType ins = 1;
        for (int c : clusterIds) {
            const ClusterInfo info = (*clusterInfoMap)[static_cast<dataType>(c)];
            const dataType fp = info.firstSpikePosition();
            const dataType n  = info.nbSpikes();
            memcpy(&(reclusteringSpikesByCluster)(1, ins),
                   &(*spikesByCluster)(1, fp), n * sizeof(dataType));
            memcpy(&(reclusteringSpikesByCluster)(2, ins),
                   &(*spikesByCluster)(2, fp), n * sizeof(dataType));
            ins += n;
        }
    }

    // ---- (1) Read the pooled raw waveforms into a native-width store. ----
    // .spk is sample-major; each spike is Dp samples at offset
    // (globalIdx-1)*Dp.  T matches the acquisition width (sizeof(T)==sampleSize).
    // Global spike indices come from the pooled reclusteringSpikesByCluster.
    FILE* spk = fopen(qPrintable(spkFileName), "rb");
    if (!spk) {
        qCritical() << "createMedianWaveformResidualFeatureFile: cannot open"
                    << spkFileName;
        return 0;
    }
    std::vector<T> W;
    try {
        W.resize(static_cast<size_t>(nSp) * Dp);
    } catch (const std::bad_alloc&) {
        qCritical() << "createMedianWaveformResidualFeatureFile: out of memory for"
                    << static_cast<long long>(nSp) << "spikes x" << Dp << "points";
        fclose(spk);
        return 0;
    }
    for (dataType s = 0; s < nSp; ++s) {
        const dataType gid = reclusteringSpikesByCluster(1, s + 1);   // 1-based
        const long long off = static_cast<long long>(gid - 1) * Dp
                            * static_cast<long long>(sizeof(T));
        T* wrow = &W[static_cast<size_t>(s) * Dp];
        if (fseeko64(spk, off, SEEK_SET) != 0 ||
            fread(wrow, sizeof(T), Dp, spk) != static_cast<size_t>(Dp)) {
            qCritical() << "createMedianWaveformResidualFeatureFile: short read"
                           " at spike" << static_cast<long long>(gid);
            fclose(spk);
            return 0;
        }
    }
    fclose(spk);

    // ---- (2) Per-point median template. ----
    std::vector<double> med(static_cast<size_t>(Dp), 0.0);
    {
        std::vector<T> col(static_cast<size_t>(nSp));
        const size_t half = static_cast<size_t>(nSp) / 2;
        for (int j = 0; j < Dp; ++j) {
            for (dataType s = 0; s < nSp; ++s)
                col[static_cast<size_t>(s)] = W[static_cast<size_t>(s) * Dp + j];
            std::nth_element(col.begin(), col.begin() + half, col.end());
            double m = static_cast<double>(col[half]);
            if ((nSp & 1) == 0) {    // even: average the two central order stats
                T lo = std::numeric_limits<T>::min();
                for (size_t i = 0; i < half; ++i) if (col[i] > lo) lo = col[i];
                m = 0.5 * (static_cast<double>(lo) + static_cast<double>(col[half]));
            }
            med[static_cast<size_t>(j)] = m;
        }
    }

    // ---- (3) Streaming residual covariance C = R^T R / nSp. ----
    const double invN = 1.0 / static_cast<double>(nSp);
    std::vector<double> C(static_cast<size_t>(Dp) * Dp, 0.0);
    std::vector<double> resid(static_cast<size_t>(Dp));
    for (dataType s = 0; s < nSp; ++s) {
        const T* wrow = &W[static_cast<size_t>(s) * Dp];
        for (int j = 0; j < Dp; ++j)
            resid[static_cast<size_t>(j)] =
                static_cast<double>(wrow[j]) - med[static_cast<size_t>(j)];
        for (int j = 0; j < Dp; ++j) {
            const double rj = resid[static_cast<size_t>(j)];
            if (rj == 0.0) continue;
            double* Crow = &C[static_cast<size_t>(j) * Dp];
            for (int k = j; k < Dp; ++k)
                Crow[k] += rj * resid[static_cast<size_t>(k)];
        }
    }
    for (int j = 0; j < Dp; ++j)
        for (int k = j; k < Dp; ++k) {
            C[static_cast<size_t>(j) * Dp + k] *= invN;
            C[static_cast<size_t>(k) * Dp + j] = C[static_cast<size_t>(j) * Dp + k];
        }

    // ---- (4) Cyclic Jacobi eigendecomposition. ----
    std::vector<double> V(static_cast<size_t>(Dp) * Dp, 0.0);
    for (int j = 0; j < Dp; ++j) V[static_cast<size_t>(j) * Dp + j] = 1.0;
    const int    maxSweeps = 100;
    const double tol       = 1e-12;
    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        double offSum = 0.0;
        for (int p = 0; p < Dp - 1; ++p)
            for (int q = p + 1; q < Dp; ++q) {
                const double cpq = C[static_cast<size_t>(p) * Dp + q];
                offSum += cpq * cpq;
            }
        if (offSum < tol) break;
        for (int p = 0; p < Dp - 1; ++p)
            for (int q = p + 1; q < Dp; ++q) {
                const double apq = C[static_cast<size_t>(p) * Dp + q];
                if (std::abs(apq) < 1e-18) continue;
                const double app = C[static_cast<size_t>(p) * Dp + p];
                const double aqq = C[static_cast<size_t>(q) * Dp + q];
                const double theta = (aqq - app) / (2.0 * apq);
                const double t = (theta >= 0.0)
                    ?  1.0 / (theta + std::sqrt(1.0 + theta * theta))
                    :  1.0 / (theta - std::sqrt(1.0 + theta * theta));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double sn = t * c;
                C[static_cast<size_t>(p) * Dp + p] = app - t * apq;
                C[static_cast<size_t>(q) * Dp + q] = aqq + t * apq;
                C[static_cast<size_t>(p) * Dp + q] = 0.0;
                C[static_cast<size_t>(q) * Dp + p] = 0.0;
                for (int r = 0; r < Dp; ++r) {
                    if (r != p && r != q) {
                        const double arp = C[static_cast<size_t>(r) * Dp + p];
                        const double arq = C[static_cast<size_t>(r) * Dp + q];
                        C[static_cast<size_t>(r) * Dp + p] = c * arp - sn * arq;
                        C[static_cast<size_t>(p) * Dp + r] =
                            C[static_cast<size_t>(r) * Dp + p];
                        C[static_cast<size_t>(r) * Dp + q] = sn * arp + c * arq;
                        C[static_cast<size_t>(q) * Dp + r] =
                            C[static_cast<size_t>(r) * Dp + q];
                    }
                    const double vrp = V[static_cast<size_t>(r) * Dp + p];
                    const double vrq = V[static_cast<size_t>(r) * Dp + q];
                    V[static_cast<size_t>(r) * Dp + p] = c * vrp - sn * vrq;
                    V[static_cast<size_t>(r) * Dp + q] = sn * vrp + c * vrq;
                }
            }
    }

    // ---- (5) Top-K eigenpairs by descending eigenvalue. ----
    std::vector<std::pair<double, int>> sortedEv(Dp);
    for (int j = 0; j < Dp; ++j)
        sortedEv[static_cast<size_t>(j)] =
            std::make_pair(C[static_cast<size_t>(j) * Dp + j], j);
    std::sort(sortedEv.begin(), sortedEv.end(),
        [](const std::pair<double,int>& a, const std::pair<double,int>& b) {
            return a.first > b.first;
        });
    if (eigvalsOut) {
        eigvalsOut->clear();
        eigvalsOut->reserve(K);
        for (int k = 0; k < K; ++k)
            eigvalsOut->append(sortedEv[static_cast<size_t>(k)].first);
    }
    std::vector<double> evK(static_cast<size_t>(Dp) * K, 0.0);
    for (int k = 0; k < K; ++k) {
        const int origCol = sortedEv[static_cast<size_t>(k)].second;
        for (int r = 0; r < Dp; ++r)
            evK[static_cast<size_t>(r) * K + k] =
                V[static_cast<size_t>(r) * Dp + origCol];
    }

    // ---- (6) Streaming projection onto the top-K eigenvectors; tally range. ----
    std::vector<double> proj(static_cast<size_t>(nSp) * K, 0.0);
    std::vector<double> minVal(static_cast<size_t>(K),
                               std::numeric_limits<double>::infinity());
    std::vector<double> maxVal(static_cast<size_t>(K),
                               -std::numeric_limits<double>::infinity());
    for (dataType s = 0; s < nSp; ++s) {
        const T* wrow = &W[static_cast<size_t>(s) * Dp];
        for (int j = 0; j < Dp; ++j)
            resid[static_cast<size_t>(j)] =
                static_cast<double>(wrow[j]) - med[static_cast<size_t>(j)];
        for (int k = 0; k < K; ++k) {
            double val = 0.0;
            for (int j = 0; j < Dp; ++j)
                val += resid[static_cast<size_t>(j)]
                     * evK[static_cast<size_t>(j) * K + k];
            proj[static_cast<size_t>(s) * K + k] = val;
            if (val < minVal[static_cast<size_t>(k)]) minVal[static_cast<size_t>(k)] = val;
            if (val > maxVal[static_cast<size_t>(k)]) maxVal[static_cast<size_t>(k)] = val;
        }
    }
    const double targetMax = static_cast<double>(1 << 28);
    std::vector<double> scale(static_cast<size_t>(K), 1.0);
    for (int k = 0; k < K; ++k) {
        const double span = std::max(std::abs(minVal[static_cast<size_t>(k)]),
                                     std::abs(maxVal[static_cast<size_t>(k)]));
        scale[static_cast<size_t>(k)] = (span > 1e-12) ? targetMax / span : 1.0;
    }

    // ---- (7) Write .fet: int32 nDim header, per-spike int64 (K + timestamp). ----
    fetFile.close();
    FILE* ff = fopen(fetFile.fileName().toLocal8Bit().constData(), "wb");
    if (!ff) return 0;
    const int32_t nDim32 = static_cast<int32_t>(K + 1);
    fwrite(&nDim32, sizeof(int32_t), 1, ff);
    for (dataType s = 0; s < nSp; ++s) {
        const dataType row = reclusteringSpikesByCluster(1, s + 1);
        for (int k = 0; k < K; ++k) {
            const int64_t iv = static_cast<int64_t>(
                proj[static_cast<size_t>(s) * K + k] * scale[static_cast<size_t>(k)]);
            fwrite(&iv, sizeof(int64_t), 1, ff);
        }
        const int64_t ts = static_cast<int64_t>(features(row, nbDimensions));
        fwrite(&ts, sizeof(int64_t), 1, ff);
    }
    fclose(ff);
    return K + 1;
}

int Data::createMedianWaveformResidualFeatureFile(const QList<int>& clusterIds,
                                                  int K, QFile& fetFile,
                                                  QVector<double>* eigvalsOut)
{
    // Dispatch on acquisition width so the in-memory waveform store uses the
    // native sample type (int16 for two-byte recordings, int32 otherwise).
    if (isTwoBytesRecording)
        return medianWaveformResidualImpl<int16_t>(clusterIds, K, fetFile, eigvalsOut);
    return medianWaveformResidualImpl<int32_t>(clusterIds, K, fetFile, eigvalsOut);
}

void Data::createFeatureFile(QList<int>& clustersToRecluster,QFile& fetFile){
    // Desync guard: KlustersApp::slotRecluster also pre-validates this
    // via Data::clusterHasMembers before reaching us, so a desync
    // shouldn't make it this far.  Repeat the check here for
    // defence-in-depth — if a future code path calls createFeatureFile
    // outside slotRecluster, the same silent-empty-.fet bug must not
    // resurface.  Same handling as integrateBasinLabeling: abort early
    // (the temp file is left as-is; the QFile destructor closes it; the
    // caller checks the file exists / has the expected size).
    for (int cid : clustersToRecluster) {
        if (!clusterInfoMap->contains(static_cast<dataType>(cid))) {
            qWarning("createFeatureFile: cluster %d is in the recluster "
                     "list but absent from clusterInfoMap "
                     "(spikesByCluster ↔ clusterInfoMap desync) — "
                     "aborting before writing the temp .fet.  "
                     "KlustaKwik would otherwise see an empty .fet and "
                     "abort with the cryptic 'Array::SetSize: n < 1' "
                     "exception.", cid);
            return;
        }
    }
    dataType reclusteringNbSpikes = 0;
    //Loop on the selected clusters to calculate the total number of spikes
    QList<int>::iterator iterator;
    for(iterator = clustersToRecluster.begin(); iterator != clustersToRecluster.end(); ++iterator ){
        ClusterInfo clusterInfo = (*clusterInfoMap)[static_cast<dataType>(*iterator)];
        reclusteringNbSpikes += clusterInfo.nbSpikes();
    }
    reclusteringSpikesByCluster.setSize(reclusteringNbSpikes);//erase any previous data.

    //Loop on the selected clusters
    dataType upperInsertionIndex = 1;
    for(iterator = clustersToRecluster.begin(); iterator != clustersToRecluster.end(); ++iterator ){
        ClusterInfo clusterInfo = (*clusterInfoMap)[static_cast<dataType>(*iterator)];
        dataType firstSpikePosition = clusterInfo.firstSpikePosition();
        dataType nbSpikesOfCluster = clusterInfo.nbSpikes();
        //copy the 2 rows of spikesByCluster for the given cluster
        memcpy(&(reclusteringSpikesByCluster)(1,upperInsertionIndex),
               &(*spikesByCluster)(1,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));
        memcpy(&(reclusteringSpikesByCluster)(2,upperInsertionIndex),
               &(*spikesByCluster)(2,firstSpikePosition),
               nbSpikesOfCluster * sizeof(dataType));
        upperInsertionIndex += nbSpikesOfCluster;
    }

    // Write all features to file in binary .fet format:
    //   int32_t nDimensions; then nSpikes x nDimensions x int64_t row-major
    fetFile.close();
    FILE* ff = fopen(fetFile.fileName().toLocal8Bit().constData(), "wb");
    if (ff) {
        int32_t nDim32 = (int32_t)nbDimensions;
        fwrite(&nDim32, sizeof(int32_t), 1, ff);
        for (dataType i = 1; i <= reclusteringNbSpikes; ++i) {
            dataType featuresRowIndex = reclusteringSpikesByCluster(1, i);
            for (int j = 1; j <= nbDimensions; ++j) {
                int64_t v = static_cast<int64_t>(features(featuresRowIndex, j));
                fwrite(&v, sizeof(int64_t), 1, ff);
            }
        }
        fclose(ff);
    }
}

bool Data::integrateReclusteredClusters(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList, QFile& clusterFile){
    //Replace the cluster ids in reclusteringSpikesByCluster by the new ones.
    if(!loadReclusteredClusters(clusterFile)) return 0;

    //The new information about the cluster will be inserted in the table pointed by spikesByClusterTemp
    SortableTable* spikesByClusterTemp = new SortableTable();
    spikesByClusterTemp->setSize(nbSpikes);

    //Create a new map which will contain the new information about the position of the clusters
    ClusterInfoMap* clusterInfoMapTemp = new ClusterInfoMap();

    //Iteration on the clusters to copy the unchanged clusters.
    ClusterInfoMap::Iterator infoMapIterator;
    dataType upperInsertionIndex = 1;

    //NB: the iterator iterates on the items sorted by their key
    for(infoMapIterator = clusterInfoMap->begin(); infoMapIterator != clusterInfoMap->end(); ++infoMapIterator){
        dataType firstSpikePosition = infoMapIterator.value().firstSpikePosition();
        dataType nbSpikesOfCluster = infoMapIterator.value().nbSpikes();
        dataType clusterId = infoMapIterator.key();

        //if clustersToRecluster does not contains the current cluster, this cluster is let unchanged
        //and its information is simply copy as is from spikesByCluster to spikesByClusterTemp
        if(!clustersToRecluster.contains(static_cast<int>(clusterId))){
            //copy the 2 rows of spikesByCluster for the given cluster
            memcpy(&(*spikesByClusterTemp)(1,upperInsertionIndex),
                   &(*spikesByCluster)(1,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            memcpy(&(*spikesByClusterTemp)(2,upperInsertionIndex),
                   &(*spikesByCluster)(2,firstSpikePosition),
                   nbSpikesOfCluster * sizeof(dataType));
            //Construct the new clusterInfoMap
            clusterInfoMapTemp->insert(clusterId,ClusterInfo(upperInsertionIndex,nbSpikesOfCluster));
            upperInsertionIndex += nbSpikesOfCluster;
        }
    }

    //Sort by time the spikes of the reclustered clusters.
    reclusteringSpikesByCluster.sort(1);

    dataType reclusteringNbSpikes = reclusteringSpikesByCluster.nbOfColumns();

    QMap<dataType,dataType> clusters;
    dataType max = reclusteringNbSpikes + 1;
    //Count the number of spikes for each cluster.
    for(dataType i = 1; i < max; ++i){
        dataType clusterId = reclusteringSpikesByCluster(2,i);
        clusters[clusterId]++;
    }

    //Initialize positions, for each clusterId the value is set to the position of the first spike.
    //The clusters will sorted by clusterId in spikesByClusterTemp. Initialize clusterInfoMapTemp.
    QMap<dataType,dataType> positions;
    QMap<dataType,dataType>::Iterator clusterIterator;
    int index = upperInsertionIndex;//The first new cluster will start after all the clusters which were not reclustered.
    for(clusterIterator = clusters.begin(); clusterIterator != clusters.end(); ++clusterIterator){
        dataType clusterId = clusterIterator.key();
        reclusteredClusterList.append(static_cast<int>(clusterId));
        positions[clusterId] =  index;
        clusterInfoMapTemp->insert(clusterId,ClusterInfo(index,clusterIterator.value()));
        index += clusterIterator.value();
    }

    //Fill spikesByClusterTemp with the data of the reclustered clusters sorted by cluster and by time (<=> position in the fet file)
    for(dataType i = 1; i < max; ++i){
        dataType clusterId = reclusteringSpikesByCluster(2,i);
        dataType position = positions[clusterId];
        dataType positionInFet = reclusteringSpikesByCluster(1,i);
        (*spikesByClusterTemp)(1,position) = positionInFet;
        (*spikesByClusterTemp)(2,position) = clusterId;
        positions[clusterId] ++;
    }

    //clear reclusteringSpikesByCluster
    reclusteringSpikesByCluster.setSize(0,true);

    //Get the list of clusters before applying the changes, this will be used in the clean
    //of the correlation.
    QList<dataType> currentClusterList = clusterIds();

    //Deal with the undo mechanism
    bool dimChanged = clustersToRecluster.contains(0);
    prepareUndo(spikesByClusterTemp,clusterInfoMapTemp,dimChanged);

    //If the cluster 0 have been recluster (very unlikely), the max and min
    // dimensions have to be recalculated. If minMaxThread is running, the call
    //will wait until it finishes before starting the thread again.
    if(dimChanged){
        //If the minMaxThread has not finish, wait until it is done
        minMaxThread->wait();
        //Reset the flag to false so the minMaxThread can do the computation
        clusterZeroJustModified = false;
        minMaxThread->setModifiedClusters(clustersToRecluster);
        minMaxThread->start();
    }

    //Remove the waveform and correlation data for the reclustered clusters.
    //If there is not a thread working with them,otherwise advice the thread of the change,by updating waveformStatus and correlationsInProcess
    // and the thread will remove it.
    QList<int>::iterator iterator;
    for(iterator = clustersToRecluster.begin(); iterator != clustersToRecluster.end(); ++iterator){
        {
            QMutexLocker lk(&mutex);
        if(waveformStatusMap.contains(*iterator)){
            if(!waveformStatusMap[*iterator].isInProcess()){
                delete waveformDict.take(QString::fromLatin1("%1").arg(*iterator));
                waveformStatusMap.remove(*iterator);
            }
            else{
                WaveformStatus waveformStatus = waveformStatusMap[*iterator];
                WaveformStatus waveformStatusCopy = WaveformStatus(waveformStatus);
                waveformStatusCopy.setClusterModified(true);
                waveformStatusMap.insert(*iterator,waveformStatusCopy);
            }
        }
        }
        if(!correlationsInProcess.contains(static_cast<dataType>(*iterator))) cleanCorrelation(static_cast<dataType>(*iterator),currentClusterList);
        else{
            {
                QMutexLocker lk(&mutex);
            correlationsInProcess.setClusterModified(static_cast<dataType>(*iterator),true);
            }
        }
    }

    return 1;
}


bool Data::loadReclusteredClusters(QFile &clusterFile){
    // Binary .clu format written by KlustaKwik:
    //   int32_t  nClusters  (number of distinct cluster labels)
    //   nSpikes * int32_t   cluster IDs, 1-based
    //
    // We offset each ID by highestClusterId so new clusters don't collide
    // with existing ones.

    dataType highestId = highestClusterId();
    const dataType maxK = reclusteringSpikesByCluster.nbOfColumns();

    NS3_DIAG() << "loadReclusteredClusters: expecting" << maxK
             << "spike labels, highestClusterId=" << highestId;

    const QString path = clusterFile.fileName();
    clusterFile.close();

    FILE* fp = fopen(path.toLocal8Bit().constData(), "rb");
    if (!fp) {
        qWarning() << "loadReclusteredClusters: cannot open" << path;
        return 0;
    }

    // Read nClusters header
    int32_t nClusters32 = 0;
    if (fread(&nClusters32, sizeof(int32_t), 1, fp) != 1) {
        qWarning() << "loadReclusteredClusters: cannot read header";
        fclose(fp); return 0;
    }
    NS3_DIAG() << "loadReclusteredClusters: header nClusters=" << nClusters32;

    // Read spike labels
    dataType k = 1;
    int32_t id32 = 0;
    while (fread(&id32, sizeof(int32_t), 1, fp) == 1) {
        if (k > maxK) {
            qWarning() << "loadReclusteredClusters: too many labels (k="
                     << k << "> maxK=" << maxK << ") — aborting";
            fclose(fp); return 0;
        }
        reclusteringSpikesByCluster(2, k++) =
            static_cast<dataType>(id32) + highestId;
    }
    fclose(fp);

    NS3_DIAG() << "loadReclusteredClusters: read" << (k - 1)
             << "labels, expected" << maxK;

    if (k != (maxK + 1))
        return 0;
    else
        return 1;
}

void Data::getClusterUserInformation (int pGroup,QMap<int,ClusterUserInformation>& clusterUserInformationMap)const{
    // This method is called from SaveThread (off the main thread) while the main
    // thread may concurrently modify clusterInfoMap via undo/redo/clustering.
    // Take a snapshot of the map under the mutex to avoid a use-after-free.
    // Take a snapshot under the mutex; iterate after releasing it.
    ClusterInfoMap localSnapshot;
    {
        QMutexLocker lk(&mutex);
        localSnapshot = *clusterInfoMap;
    }

    //Iteration on the clusters
    ClusterInfoMap::Iterator iterator;

    //NB: the iterator iterates on the items sorted by their key
    for(iterator = localSnapshot.begin(); iterator != localSnapshot.end(); ++iterator) {
        int clusterId = static_cast<int>(iterator.key());

        if(clusterId == 0 || clusterId == 1) continue;

        ClusterUserInformation currentClusterUserInformation = ClusterUserInformation(pGroup,clusterId,iterator.value().getStructure(),iterator.value().getType(),iterator.value().getId(),iterator.value().getQuality(),iterator.value().getNotes());

        clusterUserInformationMap.insert(clusterId,currentClusterUserInformation);
    }
}


// ---------------------------------------------------------------------------
// Spike re-alignment helpers
// ---------------------------------------------------------------------------

bool Data::updateFeatureRow(dataType spikeIndex, const QList<dataType>& newValues)
{
    if (spikeIndex < 1 || spikeIndex > nbSpikes) return false;
    int nFeat = nbDimensions - 1; // exclude timestamp column
    for (qsizetype d = 0; d < (qsizetype)nFeat && d < newValues.size(); ++d)
        features(spikeIndex, d + 1) = newValues[d];
    return true;
}

bool Data::updateTimestamp(dataType spikeIndex, dataType newTimestamp)
{
    if (spikeIndex < 1 || spikeIndex > nbSpikes) return false;
    features(spikeIndex, nbDimensions) = newTimestamp;
    return true;
}

void Data::swapSpikes(dataType idxA, dataType idxB)
{
    if (idxA == idxB) return;
    // Swap all feature columns (including timestamp)
    for (int d = 1; d <= nbDimensions; ++d) {
        dataType tmp        = features(idxA, d);
        features(idxA, d)  = features(idxB, d);
        features(idxB, d)  = tmp;
    }
    // Swap the two entries in spikesByCluster that point to idxA / idxB.
    // Row 1 = feature row index; row 2 = cluster id.
    // The table is sorted by cluster, so we need to search for both entries.
    for (dataType k = 1; k <= nbSpikes; ++k) {
        dataType ref = (*spikesByCluster)(1, k);
        if (ref == idxA)       (*spikesByCluster)(1, k) = idxB;
        else if (ref == idxB)  (*spikesByCluster)(1, k) = idxA;
    }
}

void Data::invalidateWaveformCache(int clusterId)
{
    // Mirror the pattern used throughout data.cpp for cache invalidation:
    // - If no thread is currently loading waveforms for this cluster, delete
    //   the cached data immediately and remove the status entry so the next
    //   WaveformThread request re-reads from the .spk file.
    // - If a thread is in-flight, set the clusterModified flag instead.
    //   The thread checks this flag after loading and discards its results,
    //   then removes the status entry itself, forcing a fresh load next time.
    {
        QMutexLocker lk(&mutex);
    if (waveformStatusMap.contains(clusterId)) {
        if (!waveformStatusMap[clusterId].isInProcess()) {
            delete waveformDict.take(QString::fromLatin1("%1").arg(clusterId));
            waveformStatusMap.remove(clusterId);
        } else {
            WaveformStatus updated = waveformStatusMap[clusterId];
            updated.setClusterModified(true);
            waveformStatusMap.insert(clusterId, updated);
        }
    }
    }
}

void Data::invalidateCorrelogramCache(int clusterId)
{
    // Remove all cached correlogram entries that involve this cluster so the
    // next CorrelationThread recomputes them from the updated in-memory
    // timestamps.  Use the existing cleanCorrelation helper which handles
    // the mutex and iterates all pairs that reference clusterId.
    cleanCorrelation(static_cast<dataType>(clusterId), clusterIds(), false);
}
