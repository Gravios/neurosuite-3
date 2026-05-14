/***************************************************************************
 * spikerealign.cpp
 *
 * Spike waveform re-alignment engine.
 * See spikerealign.h for a detailed algorithm description.
 ***************************************************************************/

#include "spikerealign.h"
#include "realign_xcorr.h"
#include "data.h"
#include "sortabletable.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>

#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// SpikeRealign
// ---------------------------------------------------------------------------

SpikeRealign::SpikeRealign(Data* data, int clusterId,
                            const QString& basePath, int groupId,
                            int maxShift, QObject* parent)
    : QObject(parent)
    , m_data(data)
    , m_clusterId(clusterId)
    , m_basePath(basePath)
    , m_groupId(groupId)
    , m_maxShift(maxShift)
{}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

QString SpikeRealign::resPath()  const
{ return QString("%1.res.%2").arg(m_basePath).arg(m_groupId); }

QString SpikeRealign::spkPath()  const
{ return QString("%1.spk.%2").arg(m_basePath).arg(m_groupId); }

QString SpikeRealign::fetPath()  const
{ return QString("%1.fet.%2").arg(m_basePath).arg(m_groupId); }

QString SpikeRealign::cluPath()  const
{ return QString("%1.clu.%2").arg(m_basePath).arg(m_groupId); }

QString SpikeRealign::evecPath() const
{ return QString("%1.spk.%2.evec").arg(m_basePath).arg(m_groupId); }

QString SpikeRealign::filPath()  const
{
    // Prefer .fil (high-pass filtered) — that's what process_extractspikes
    // produces .spk from in the standard pipeline.  Fall back to .dat only
    // if .fil is missing; in that case the caller must verify that the
    // .spk content matches a sample re-extraction (see verifyRawSource()),
    // otherwise re-extracted waveforms will have completely different
    // frequency content (DC offset, slow drift, full bandwidth) and will
    // look "corrupted" compared to the rest of the cluster.
    QString fil = m_basePath + ".fil";
    if (QFile::exists(fil)) return fil;
    return m_basePath + ".dat";
}

// patch62 — sanity-check that the file we're about to re-extract from
// actually produces waveforms compatible with the existing .spk content.
// Compares ONE existing .spk waveform (sh=0, no extraction needed) to a
// fresh read from filPath() at the same timestamp.  If the RMS difference
// exceeds a calibration threshold, the source files are mismatched — most
// likely .fil missing and we fell back to .dat while .spk was extracted
// from a different .fil.  Aborts the realign with a clear error message.
bool SpikeRealign::verifyRawSource(const QVector<long long>& globalIndices,
                                    const QVector<QVector<short>>& waveforms,
                                    FILE* rawFile,
                                    int peakPos,
                                    QString& outError)
{
    if (globalIndices.isEmpty() || waveforms.isEmpty()) return true;

    const int nChan    = m_data->nbOfchannels();
    const int nSamples = m_data->nbOfSampleInWaveform();
    const int totalNbChan = m_data->getTotalNbChannels();
    const QList<int> channelIds = m_data->getChannelIds();

    // Read the first cluster spike's original timestamp.
    QVector<long long> allRes;
    if (!readAllRes(allRes)) return true;   // can't verify, but don't block
    const long long gidx = globalIndices[0];
    if (gidx - 1 >= (long long)allRes.size()) return true;
    const long long oldTs = allRes[(int)(gidx - 1)];
    const long long startSample = oldTs - peakPos;

    const long long totalSamples =
        QFileInfo(filPath()).size() / ((long long)sizeof(short) * totalNbChan);
    if (startSample < 0 || startSample + nSamples > totalSamples) return true;

    off_t rawOff = (off_t)startSample * totalNbChan * sizeof(short);
    if (fseeko(rawFile, rawOff, SEEK_SET) != 0) return true;

    QVector<short> rawFrame((size_t)nSamples * totalNbChan);
    if ((int)fread(rawFrame.data(), sizeof(short),
                   (size_t)nSamples * totalNbChan, rawFile)
        != nSamples * totalNbChan)
        return true;

    // Compare: RMS difference between fresh-read and .spk-stored waveform.
    const QVector<short>& spkWv = waveforms[0];
    double sumSq = 0.0, sumRefSq = 0.0;
    for (int s = 0; s < nSamples; ++s) {
        for (int ci = 0; ci < nChan; ++ci) {
            short fresh = rawFrame[s * totalNbChan + channelIds[ci]];
            short spk   = spkWv[s * nChan + ci];
            double d = (double)fresh - (double)spk;
            sumSq    += d * d;
            sumRefSq += (double)spk * (double)spk;
        }
    }
    const double rmsDiff = std::sqrt(sumSq / (nSamples * nChan));
    const double rmsRef  = std::sqrt(sumRefSq / (nSamples * nChan));

    // Quantisation can produce small differences (rounding in the original
    // extraction).  A factor-of-2 disagreement signals a fundamentally
    // different source (e.g. unfiltered vs filtered).  RMS-relative > 0.5
    // is the cutoff — generous enough to allow integer rounding, tight
    // enough to catch source mismatch.
    if (rmsRef > 1.0 && rmsDiff > 0.5 * rmsRef) {
        outError = QString(
            "Re-extracted waveform does not match .spk content "
            "(RMS difference = %1, reference RMS = %2).\n\n"
            "This usually means the original .spk was extracted from .fil "
            "(filtered) but spikerealign is reading from .dat (unfiltered), "
            "because .fil is missing.\n\n"
            "Source file in use: %3\n\n"
            "Generate the .fil file (e.g. via process_filter / ndm_filter) "
            "and retry, or accept that re-extracted waveforms will not "
            "match the rest of the cluster.")
            .arg(rmsDiff, 0, 'f', 1)
            .arg(rmsRef,  0, 'f', 1)
            .arg(filPath());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// loadEigenvectors (static)
// ---------------------------------------------------------------------------

PcaEigenvectors SpikeRealign::loadEigenvectors(const QString& evecPath)
{
    PcaEigenvectors pca;
    FILE* f = fopen(evecPath.toLocal8Bit().constData(), "rb");
    if (!f) {
        qWarning() << "SpikeRealign: cannot open evec file" << evecPath;
        return pca;
    }

    int32_t hdr[3];
    if (fread(hdr, sizeof(int32_t), 3, f) != 3) { fclose(f); return pca; }
    pca.nChannels   = hdr[0];
    pca.data2use    = hdr[1];
    pca.nComponents = hdr[2];

    int32_t centered = 0;
    if (fread(&centered, sizeof(int32_t), 1, f) != 1) { fclose(f); return pca; }
    pca.isCentered = (centered != 0);

    // recShift: added after isCentered (older files without this field default to 0,
    // which is correct for whole-waveform PCA).  Peek at the remaining byte count:
    // if there are enough bytes for recShift + all means + all evecs, read it;
    // otherwise leave recShift = 0 for backward compatibility.
    {
        long curPos = (long)ftell(f);
        fseek(f, 0, SEEK_END);
        long endPos = (long)ftell(f);
        fseek(f, curPos, SEEK_SET);
        long remaining   = endPos - curPos;
        long withShift   = sizeof(int32_t)
                         + (long)pca.nChannels * pca.data2use * sizeof(double)
                         + (long)pca.nChannels * pca.data2use * pca.nComponents * sizeof(double);
        long withoutShift = withShift - (long)sizeof(int32_t);
        if (remaining == withShift) {
            int32_t shift32 = 0;
            if (fread(&shift32, sizeof(int32_t), 1, f) != 1) { fclose(f); return pca; }
            pca.recShift = (int)shift32;
        } else if (remaining == withoutShift) {
            pca.recShift = 0;   // old file without recShift field
        } else {
            fclose(f);
            return pca;         // unexpected size — return invalid
        }
    }

    pca.means.resize(pca.nChannels);
    for (int ch = 0; ch < pca.nChannels; ++ch) {
        pca.means[ch].resize(pca.data2use);
        if ((int)fread(pca.means[ch].data(), sizeof(double), pca.data2use, f)
                != pca.data2use) {
            fclose(f); pca = PcaEigenvectors(); return pca;
        }
    }

    pca.evec.resize(pca.nChannels);
    const int evecSize = pca.data2use * pca.nComponents;
    for (int ch = 0; ch < pca.nChannels; ++ch) {
        pca.evec[ch].resize(evecSize);
        if ((int)fread(pca.evec[ch].data(), sizeof(double), evecSize, f) != evecSize) {
            fclose(f); pca = PcaEigenvectors(); return pca;
        }
    }

    fclose(f);
    return pca;
}

// ---------------------------------------------------------------------------
// readAllRes / writeAllRes
// ---------------------------------------------------------------------------

bool SpikeRealign::readAllRes(QVector<long long>& timestamps)
{
    QFile f(resPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    timestamps.clear();
    QString line;
    while (!(line = ts.readLine()).isNull())
        timestamps.append(line.trimmed().toLongLong());
    return true;
}

bool SpikeRealign::writeAllRes(const QVector<long long>& timestamps)
{
    QFile f(resPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream ts(&f);
    for (long long t : timestamps)
        ts << t << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// readAllClu / writeAllClu
// ---------------------------------------------------------------------------

bool SpikeRealign::readAllClu(QVector<int>& clusterIds, int& nClustersHeader)
{
    QFile f(cluPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    QString line = ts.readLine().trimmed();
    nClustersHeader = line.toInt();
    clusterIds.clear();
    while (!(line = ts.readLine()).isNull())
        clusterIds.append(line.trimmed().toInt());
    return true;
}

bool SpikeRealign::writeAllClu(const QVector<int>& clusterIds, int nClustersHeader)
{
    QFile f(cluPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream ts(&f);
    ts << nClustersHeader << "\n";
    for (int c : clusterIds)
        ts << c << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// readSpkWaveforms — read a set of spike waveforms indexed by 1-based global index
// ---------------------------------------------------------------------------

bool SpikeRealign::readSpkWaveforms(const QVector<long long>& spikeGlobalIndices,
                                     QVector<QVector<short>>&  waveforms)
{
    const int nChan    = m_data->nbOfchannels();
    const int nSamples = m_data->nbOfSampleInWaveform();
    const int nPts     = nChan * nSamples;

    FILE* f = fopen(spkPath().toLocal8Bit().constData(), "rb");
    if (!f) return false;

    waveforms.resize(spikeGlobalIndices.size());
    for (qsizetype k = 0; k < spikeGlobalIndices.size(); ++k) {
        long long idx = spikeGlobalIndices[k];  // 1-based chronological index
        off_t offset  = (off_t)(idx - 1) * nPts * sizeof(short);
        if (fseeko(f, offset, SEEK_SET) != 0) { fclose(f); return false; }
        waveforms[k].resize(nPts);
        if ((int)fread(waveforms[k].data(), sizeof(short), nPts, f) != nPts) {
            fclose(f); return false;
        }
    }
    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// rewriteSpkSlot — write a new waveform into the .spk file at a given slot
// ---------------------------------------------------------------------------

bool SpikeRealign::rewriteSpkSlot(long long globalIdx1based,
                                   const QVector<short>& newWaveform)
{
    const int nPts   = m_data->nbOfchannels() * m_data->nbOfSampleInWaveform();
    FILE* f = fopen(spkPath().toLocal8Bit().constData(), "r+b");
    if (!f) return false;
    off_t offset = (off_t)(globalIdx1based - 1) * nPts * sizeof(short);
    if (fseeko(f, offset, SEEK_SET) != 0) { fclose(f); return false; }
    bool ok = ((int)fwrite(newWaveform.constData(), sizeof(short), nPts, f) == nPts);
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------
// projectWaveform — apply PCA eigenvectors to produce feature values
// ---------------------------------------------------------------------------

bool SpikeRealign::projectWaveform(const QVector<short>& waveform,
                                    const PcaEigenvectors& pca,
                                    QVector<double>& feats)
{
    if (!pca.valid()) return false;

    const int nChan       = m_data->nbOfchannels();
    const int nSamples    = m_data->nbOfSampleInWaveform();
    const int nComp       = pca.nComponents;
    const int data2use    = pca.data2use;
    const int recShift    = pca.recShift;  // first sample index used by PCA

    feats.resize(nChan * nComp);

    // Waveform layout in .spk: sample-major, channel-minor
    // i.e. waveform[s * nChan + ch] = sample s, channel ch
    for (int ch = 0; ch < nChan; ++ch) {
        // Build centred input vector of length data2use
        QVector<double> x(data2use);
        for (int j = 0; j < data2use; ++j) {
            int s = j + recShift;
            double raw = (s >= 0 && s < nSamples)
                         ? (double)waveform[s * nChan + ch]
                         : 0.0;
            x[j] = pca.isCentered
                   ? (raw - pca.means[ch][j])
                   : raw;
        }

        // Project: feat = E^T * x  where E is (data2use × nComp) col-major
        const double* E = pca.evec[ch].constData();
        for (int c = 0; c < nComp; ++c) {
            double dot = 0.0;
            for (int j = 0; j < data2use; ++j)
                dot += E[j + c * data2use] * x[j];   // E column-major
            feats[ch * nComp + c] = dot;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// swapSpikes — swap two spikes completely: on-disk + in-memory
// ---------------------------------------------------------------------------

bool SpikeRealign::swapSpikes(long long idxA, long long idxB,
                               QVector<long long>& allRes,
                               QVector<int>& allClu,
                               int /*nClustersHeader*/)
{
    // 0-based positions for vector indexing
    int posA = (int)(idxA - 1);
    int posB = (int)(idxB - 1);

    // Swap res
    std::swap(allRes[posA], allRes[posB]);

    // Swap clu
    std::swap(allClu[posA], allClu[posB]);

    // Swap spk slots in the file
    const int nPts = m_data->nbOfchannels() * m_data->nbOfSampleInWaveform();
    FILE* spkf = fopen(spkPath().toLocal8Bit().constData(), "r+b");
    if (!spkf) return false;

    QVector<short> wvA(nPts), wvB(nPts);
    off_t offA = (off_t)posA * nPts * sizeof(short);
    off_t offB = (off_t)posB * nPts * sizeof(short);

    fseeko(spkf, offA, SEEK_SET);
    if ((int)fread(wvA.data(), sizeof(short), nPts, spkf) != nPts) { fclose(spkf); return false; }
    fseeko(spkf, offB, SEEK_SET);
    if ((int)fread(wvB.data(), sizeof(short), nPts, spkf) != nPts) { fclose(spkf); return false; }
    fseeko(spkf, offA, SEEK_SET);
    fwrite(wvB.constData(), sizeof(short), nPts, spkf);
    fseeko(spkf, offB, SEEK_SET);
    fwrite(wvA.constData(), sizeof(short), nPts, spkf);
    fclose(spkf);

    // Swap .fet file rows (the non-timestamp features + timestamp)
    // We'll do a full text rewrite at the end, so just mark the need here by
    // swapping the features array rows in memory via Data::swapFeatureRows.
    // But Data doesn't expose that — instead we swap the in-memory features
    // directly. We use the fact that features(row, dim) gives access.
    // Since we can't call private members directly, we defer the swap to the
    // caller which holds the full allFetLines and handles it there.
    // (Return true — caller handles fet swap.)

    // Swap in-memory spikesByCluster: find positions by featuresRowIndex
    // spikesByCluster(1,i) = featuresRowIndex (= 1-based chronological index)
    // spikesByCluster(2,i) = cluster id
    // We need to find which column i has (1,i)==idxA and swap with (1,j)==idxB.
    // Data exposes features() and a way to swap...
    // Since Data doesn't expose a direct swap, we update the features in memory
    // by swapping features rows idxA and idxB (both 1-based).
    // The Data::features array is features(spikeGlobalIdx, dim) — but it's
    // private. We access it by using the public features() accessor.
    // We'll perform the swap via the friend-style approach: the caller,
    // SpikeRealign::run(), holds the spikesByCluster position table and does
    // the update directly using Data's swapFetRows() which we'll add.
    // For now return true; run() handles the memory side.

    return true;
}

// ---------------------------------------------------------------------------
// run — main realignment loop
// ---------------------------------------------------------------------------

RealignResult SpikeRealign::run()
{
    RealignResult result;

    const int nChan    = m_data->nbOfchannels();
    const int nSamples = m_data->nbOfSampleInWaveform();
    const int nPts     = nChan * nSamples;
    const int peakPos  = m_data->positionOfPeakInWaveform();  // 0-based

    // ------------------------------------------------------------------
    // 1. Load eigenvectors
    // ------------------------------------------------------------------
    PcaEigenvectors pca = loadEigenvectors(evecPath());
    if (!pca.valid()) {
        result.success = false;
        result.errorMessage = QString("Cannot load eigenvector file:\n%1").arg(evecPath());
        return result;
    }

    // ------------------------------------------------------------------
    // 2. Collect 1-based global spike indices for the selected cluster
    //    using spikePositions() (thread-safe, mutex-protected in Data)
    // ------------------------------------------------------------------
    SortableTable positions;
    if (!m_data->spikePositions(m_clusterId, positions)) {
        result.success = false;
        result.errorMessage = "Failed to get spike positions (cluster not found or modified).";
        return result;
    }
    const int nSpikesInCluster = (int)positions.nbOfColumns();
    if (nSpikesInCluster == 0) {
        result.errorMessage = "Cluster is empty.";
        return result;
    }

    QVector<long long> globalIndices(nSpikesInCluster);
    for (int i = 0; i < nSpikesInCluster; ++i)
        globalIndices[i] = (long long)positions(1, i + 1);  // 1-based

    // ------------------------------------------------------------------
    // 3. Read waveforms for this cluster
    // ------------------------------------------------------------------
    QVector<QVector<short>> waveforms;
    if (!readSpkWaveforms(globalIndices, waveforms)) {
        result.success = false;
        result.errorMessage = QString("Cannot read .spk file:\n%1").arg(spkPath());
        return result;
    }

    emit progress(10);

    // ------------------------------------------------------------------
    // 4. Compute mean waveform across all spikes in the cluster
    // ------------------------------------------------------------------
    QVector<double> meanWvD(nPts, 0.0);
    for (const auto& wv : waveforms)
        for (int p = 0; p < nPts; ++p)
            meanWvD[p] += wv[p];
    for (double& v : meanWvD) v /= nSpikesInCluster;

    QVector<short> meanWv(nPts);
    for (int p = 0; p < nPts; ++p)
        meanWv[p] = (short)std::round(meanWvD[p]);

    // Find the sample where the mean waveform actually peaks (summed |amplitude|
    // across all channels), then shift meanWv so that sample lands at peakPos.
    // Without this step the template is misaligned and xcorr shifts every spike
    // away from the true peak rather than toward it.
    {
        int    meanPeakSamp = peakPos;
        double bestAmp      = -1.0;
        for (int s = 0; s < nSamples; ++s) {
            double amp = 0.0;
            for (int ch = 0; ch < nChan; ++ch)
                amp += std::abs(meanWvD[s * nChan + ch]);
            if (amp > bestAmp) { bestAmp = amp; meanPeakSamp = s; }
        }
        const int tmplShift = peakPos - meanPeakSamp;
        if (tmplShift != 0) {
            QVector<short> shifted(nPts, 0);
            for (int s = 0; s < nSamples; ++s) {
                int src = s - tmplShift;
                if (src < 0 || src >= nSamples) continue;
                for (int ch = 0; ch < nChan; ++ch)
                    shifted[s * nChan + ch] = meanWv[src * nChan + ch];
            }
            meanWv = shifted;
        }
    }

    emit progress(15);

    // ------------------------------------------------------------------
    // 5. Pack waveforms and template into channel-major buffers and run
    //    the xcorr dispatcher (CUDA -> HIP -> SYCL -> OpenMP).
    //
    //    .spk layout:  sample-major  [s * nChan + ch]
    //    dispatcher:   channel-major [ch * nSamples + s]
    //    Transpose both buffers before the call; shifts[] maps 1-to-1
    //    back to waveforms[] / globalIndices[].
    // ------------------------------------------------------------------
    // Template - channel-major
    std::vector<int16_t> tmplBuf(static_cast<size_t>(nChan) * nSamples);
    for (int ch = 0; ch < nChan; ++ch)
        for (int s = 0; s < nSamples; ++s)
            tmplBuf[static_cast<size_t>(ch) * nSamples + s] =
                meanWv[s * nChan + ch];

    // Waveforms - [nSpikes x nChan x nSamples], channel-major per spike
    std::vector<int16_t> waveBuf(
        static_cast<size_t>(nSpikesInCluster) * nChan * nSamples);
    for (int si = 0; si < nSpikesInCluster; ++si) {
        const QVector<short>& wv = waveforms[si];
        int16_t* dst = waveBuf.data()
                     + static_cast<ptrdiff_t>(si) * nChan * nSamples;
        for (int ch = 0; ch < nChan; ++ch)
            for (int s = 0; s < nSamples; ++s)
                dst[ch * nSamples + s] = wv[s * nChan + ch];
    }

    std::vector<int>   shifts(static_cast<size_t>(nSpikesInCluster), 0);
    std::vector<float> xcorrScores(static_cast<size_t>(nSpikesInCluster), 0.0f);

    // minScore = 0: accept any non-zero shift - the pre-aligned template
    // ensures lag=0 wins naturally when the spike is already well aligned.
    XcorrDispatch::compute(
        waveBuf.data(), tmplBuf.data(),
        nSpikesInCluster, nChan, nSamples,
        m_maxShift, /*minScore=*/0.0f,
        shifts.data(), xcorrScores.data());

    emit progress(20);

    // ------------------------------------------------------------------
    // 6. Read all .res and .clu into memory (we will rewrite them once at end)
    // ------------------------------------------------------------------
    QVector<long long> allRes;
    if (!readAllRes(allRes)) {
        result.success = false;
        result.errorMessage = QString("Cannot read .res file:\n%1").arg(resPath());
        return result;
    }
    const long long totalSpikes = (long long)allRes.size();

    QVector<int> allClu;
    int nClustersHeader = 0;
    if (!readAllClu(allClu, nClustersHeader)) {
        result.success = false;
        result.errorMessage = QString("Cannot read .clu file:\n%1").arg(cluPath());
        return result;
    }

    // ------------------------------------------------------------------
    // 7. Read the .fet file fully into memory so we can patch individual rows
    // ------------------------------------------------------------------
    QFile fetFile(fetPath());
    if (!fetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.success = false;
        result.errorMessage = QString("Cannot read .fet file:\n%1").arg(fetPath());
        return result;
    }
    QTextStream fetIn(&fetFile);
    QStringList fetLines;
    QString line;
    while (!(line = fetIn.readLine()).isNull())
        fetLines.append(line);
    fetFile.close();
    // fetLines[0] = nDimensions header
    // fetLines[k] = spike k-1 (1-based spike k) features

    const int nbDimensions = m_data->totalNbOfPCAs() + 1;  // PCAs + timestamp
    const int nComp        = m_data->nbFeaturesbyChannel();

    // Open the raw signal file for re-extraction
    const int totalNbChan = m_data->getTotalNbChannels();
    FILE* rawFile = fopen(filPath().toLocal8Bit().constData(), "rb");
    if (!rawFile) {
        result.success = false;
        result.errorMessage = QString("Cannot open raw signal file (tried .fil and .dat):\n%1")
                              .arg(filPath());
        return result;
    }

    emit progress(20);

    // ------------------------------------------------------------------
    // 8. Main loop over spikes in cluster
    // ------------------------------------------------------------------
    // patch62 — totalSamples was previously int, overflows at ~18h × 32kHz
    // × 64 channels.  Use long long throughout for future-proofing.
    const long long totalSamples = QFileInfo(filPath()).size()
                                 / ((long long)sizeof(short) * totalNbChan);

    // patch62 — verify the raw file we'll re-extract from actually
    // produces waveforms compatible with the existing .spk content.
    // Catches the .fil-missing → .dat-fallback case where re-extracted
    // waveforms have wrong frequency content and look "corrupted".
    {
        QString verifyError;
        if (!verifyRawSource(globalIndices, waveforms, rawFile, peakPos,
                             verifyError)) {
            fclose(rawFile);
            result.success = false;
            result.errorMessage = verifyError;
            return result;
        }
        // Rewind so the main loop starts from a known seek offset.
        fseeko(rawFile, 0, SEEK_SET);
    }

    // Collect channels for this electrode group from data
    // (stored in Data but not publicly accessible; we use the features layout
    //  and the spk byte offset trick — we already know the channel list
    //  implicitly from how the .spk was written by process_extractspikes:
    //  each spike row = nChan consecutive channel values per sample).
    // We need the global channel IDs to re-extract from .fil.
    // Obtained via Data::getChannelIds(), which exposes the private
    // `channelList` field set during initialization.
    QList<int> channelIds = m_data->getChannelIds();

    int processedSinceLastProgress = 0;

    for (int si = 0; si < nSpikesInCluster; ++si) {
        const long long gidx = globalIndices[si];  // 1-based
        const int vecIdx = (int)(gidx - 1);        // 0-based index into allRes/allClu

        // Shift from dispatcher (sign convention: add to timestamp to realign)
        int sh = shifts[static_cast<size_t>(si)];

        if (sh != 0) {
            // -- a. Compute new timestamp --
            long long oldTs = allRes[vecIdx];
            long long newTs = oldTs + sh;
            if (newTs < 0) newTs = 0;
            if (newTs >= totalSamples) newTs = totalSamples - 1;

            // -- b. Re-extract waveform from .fil at new timestamp --
            long long startSample = newTs - peakPos;
            QVector<short> newWv(nPts, 0);
            bool canExtract = (startSample >= 0 &&
                               startSample + nSamples <= totalSamples);
            if (canExtract) {
                off_t rawOff = (off_t)startSample * totalNbChan * sizeof(short);
                fseeko(rawFile, rawOff, SEEK_SET);
                QVector<short> rawFrame((size_t)nSamples * totalNbChan);
                if ((int)fread(rawFrame.data(), sizeof(short),
                               (size_t)nSamples * totalNbChan, rawFile)
                    == nSamples * totalNbChan) {
                    // Extract only our group's channels
                    for (int s = 0; s < nSamples; ++s)
                        for (int ci = 0; ci < nChan; ++ci)
                            newWv[s * nChan + ci] =
                                rawFrame[s * totalNbChan + channelIds[ci]];
                } else {
                    // patch62 — fread failure (disk error / truncated raw
                    // file).  Previously only `newWv` and `newTs` were
                    // reverted; `sh` stayed non-zero so the outer block
                    // entered write-back with no-op data (rewriting the
                    // .spk slot with the same bytes and incrementing
                    // nRealigned spuriously).  Reset sh=0 like the
                    // canExtract=false branch does.
                    newWv = waveforms[si];
                    newTs = oldTs;
                    sh    = 0;
                }
            } else {
                newWv = waveforms[si];
                newTs = oldTs;
                sh    = 0;
            }

            if (sh != 0) {
                // -- c. Update .spk slot --
                rewriteSpkSlot(gidx, newWv);

                // -- d. Re-project features --
                QVector<double> newFeats;
                if (projectWaveform(newWv, pca, newFeats)) {
                    // Build new .fet line: features then timestamp
                    QStringList parts;
                    for (double fv : newFeats)
                        parts << QString::number((int)std::round(fv));
                    parts << QString::number(newTs);
                    if (vecIdx + 1 < fetLines.size())
                        fetLines[vecIdx + 1] = parts.join(" ");
                }

                // -- e. Update in-memory features (timestamp column only via
                //       the public features() accessor — which is const.
                //       We use Data::updateFeatureTimestamp() — added below) --
                m_data->updateFeatureTimestamp(gidx, newTs);

                // -- f. Update allRes --
                allRes[vecIdx] = newTs;

                result.nRealigned++;

                // -- g. Bubble-sort the shifted spike into its correct position.
                //
                // patch62 — fix: we MUST update vecIdx/gidx after each swap to
                // continue tracking OUR moved spike, not the displaced neighbour.
                // The previous implementation kept vecIdx fixed and ended up
                // looking at the neighbour's timestamp after a swap, terminating
                // the sort early.  For 1-sample shifts in dense spike trains
                // one swap usually sufficed; for ±2/±3 crossings the .res file
                // could be left with multi-sample out-of-order regions.
                //
                // After a swap with idxB, our spike now occupies idxB's slot.
                // Update curVec/curGidx accordingly and loop until our spike
                // is sandwiched between predecessors with smaller timestamps
                // and successors with larger ones.
                int       curVec  = vecIdx;
                long long curGidx = gidx;
                bool swapped = true;
                while (swapped) {
                    swapped = false;
                    // Check with previous spike
                    if (curVec > 0 && allRes[curVec] < allRes[curVec - 1]) {
                        long long idxB = curGidx - 1;
                        swapSpikes(curGidx, idxB, allRes, allClu, nClustersHeader);
                        if (curVec - 1 >= 0 && curVec < fetLines.size())
                            fetLines.swapItemsAt(curVec, curVec + 1 - 1);
                        m_data->swapSpikesByClusterRows(curGidx, idxB);
                        result.nSwapped++;
                        // OUR spike now lives at curVec-1.  Track it there.
                        curVec  -= 1;
                        curGidx -= 1;
                        swapped = true;
                        continue;
                    }
                    // Check with next spike
                    if (curVec < totalSpikes - 1 && allRes[curVec] > allRes[curVec + 1]) {
                        long long idxB = curGidx + 1;
                        swapSpikes(curGidx, idxB, allRes, allClu, nClustersHeader);
                        if (curVec + 2 < fetLines.size())
                            fetLines.swapItemsAt(curVec + 1, curVec + 2);
                        m_data->swapSpikesByClusterRows(curGidx, idxB);
                        result.nSwapped++;
                        // OUR spike now lives at curVec+1.  Track it there.
                        curVec  += 1;
                        curGidx += 1;
                        swapped = true;
                        continue;
                    }
                }
            }
        }

        ++processedSinceLastProgress;
        if (processedSinceLastProgress >= std::max(1, nSpikesInCluster / 70)) {
            processedSinceLastProgress = 0;
            int pct = 20 + (int)(70.0 * si / nSpikesInCluster);
            emit progress(pct);
        }
    }

    fclose(rawFile);
    emit progress(90);

    // ------------------------------------------------------------------
    // 9. Write back .res, .clu, .fet files
    // ------------------------------------------------------------------
    if (!writeAllRes(allRes)) {
        result.success = false;
        result.errorMessage = "Failed to write .res file.";
        return result;
    }
    if (!writeAllClu(allClu, nClustersHeader)) {
        result.success = false;
        result.errorMessage = "Failed to write .clu file.";
        return result;
    }

    // Write .fet
    QFile fetOut(fetPath());
    if (!fetOut.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        result.success = false;
        result.errorMessage = "Failed to write .fet file.";
        return result;
    }
    {
        QTextStream ts(&fetOut);
        for (const QString& l : fetLines)
            ts << l << "\n";
    }
    fetOut.close();

    emit progress(100);
    return result;
}
