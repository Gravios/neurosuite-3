// SpikeRealign.cpp — Standalone spike waveform realignment tool
//
// Reads binary .spk/.res/.fet/.clu/.pca files produced by ndmanager-plugins
// and klusters.  For each cluster (or a specified subset), aligns waveforms
// to the cluster mean template via normalised cross-correlation and writes
// the corrected data back in-place.
//
// Usage
// -----
//   SpikeRealign FileBase ElecNo [Options]
//
// Required
//   FileBase      Base name of the session (e.g. session for session.spk.1)
//   ElecNo        Electrode group number   (e.g. 1)
//
// Options
//   -Threshold F      Minimum normalised xcorr score to accept a shift
//                     (default: 0.70, range: 0.0–1.0)
//   -Iterations N     Number of iterative alignment passes (default: 2)
//   -MaxShift N       Maximum search radius in samples (default: peakSamp/2
//                     read from .xml, or 6 if no .xml found)
//   -Clusters A,B,C   Comma-separated list of cluster IDs to realign
//                     (default: all clusters except 0 and 1)
//   -Verbose          Print per-spike shift details
//
// File formats (all binary, same as klusters/KlustaKwik)
// -------------------------------------------------------
//   .spk.N   — raw waveforms: N_spikes * N_chan * N_samp * int16, no header
//   .res.N   — timestamps:    N_spikes * int64, no header
//   .clu.N   — cluster IDs:   int32 nClusters; N_spikes * int32
//   .fet.N   — features:      int32 nDims; N_spikes * nDims * int64
//   .pca.N   — PCA basis:     int32[5] header; per-channel means + eigenvectors
//              (written by process_pca, read to reproject features after shift)
//   .xml     — session parameters: nChannels, nSamples, peakSampleIndex
//              (parsed with simple string search, no XML library required)
//
// On success exits 0.  On error exits 1.

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "realign_xcorr.h"
#include "realign_xcorr_dispatch.h"

// XcorrDispatch and stub symbols are defined in realign_xcorr_dispatch.cpp

// ---------------------------------------------------------------------------
// Simple XML value extraction (no library dependency)
// ---------------------------------------------------------------------------
static bool xmlGetInt(const char* xml, const char* tag, int* out)
{
    // Matches <tag>value</tag> anywhere in the string
    std::string open  = std::string("<") + tag + ">";
    std::string close = std::string("</") + tag + ">";
    const char* p = strstr(xml, open.c_str());
    if (!p) return false;
    p += open.size();
    const char* q = strstr(p, close.c_str());
    if (!q) return false;
    char buf[64];
    size_t len = static_cast<size_t>(q - p);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, p, len);
    buf[len] = '\0';
    char* end;
    long v = strtol(buf, &end, 10);
    if (end == buf) return false;
    *out = static_cast<int>(v);
    return true;
}

// ---------------------------------------------------------------------------
// PCA basis
// ---------------------------------------------------------------------------
struct PcaBasis {
    int  nCh = 0, data2use = 0, nComp = 0, recShift = 0;
    bool centered = false;
    std::vector<std::vector<double>> means;  // [ch][data2use]
    std::vector<std::vector<double>> evec;   // [ch][data2use * nComp] col-major
    bool valid() const { return nCh > 0 && data2use > 0 && nComp > 0; }
};

static PcaBasis loadPca(const char* path, int nSamp)
{
    PcaBasis pca;
    FILE* fp = fopen(path, "rb");
    if (!fp) return pca;

    int32_t hdr[5] = {};
    if (fread(hdr, sizeof(int32_t), 5, fp) != 5) { fclose(fp); return pca; }

    pca.nCh      = (int)hdr[0];
    pca.data2use = (int)hdr[1];
    pca.nComp    = (int)hdr[2];
    pca.centered = (hdr[3] != 0);
    pca.recShift = (int)hdr[4];

    if (pca.nCh <= 0 || pca.nCh > 64 ||
        pca.data2use <= 0 || pca.data2use > 4096 ||
        pca.nComp <= 0 || pca.nComp > 64 ||
        pca.recShift < 0 || pca.recShift + pca.data2use > nSamp) {
        fprintf(stderr, "WARNING: .pca header out of range — ignoring\n");
        fclose(fp); return PcaBasis{};
    }

    pca.means.resize(static_cast<size_t>(pca.nCh));
    pca.evec.resize(static_cast<size_t>(pca.nCh));
    bool ok = true;

    for (int ch = 0; ch < pca.nCh && ok; ++ch) {
        pca.means[static_cast<size_t>(ch)].resize(static_cast<size_t>(pca.data2use));
        ok = (fread(pca.means[static_cast<size_t>(ch)].data(),
                    sizeof(double), static_cast<size_t>(pca.data2use), fp)
              == static_cast<size_t>(pca.data2use));
    }

    const size_t evSz = static_cast<size_t>(pca.data2use) *
                        static_cast<size_t>(pca.nComp);
    for (int ch = 0; ch < pca.nCh && ok; ++ch) {
        pca.evec[static_cast<size_t>(ch)].resize(evSz);
        ok = (fread(pca.evec[static_cast<size_t>(ch)].data(),
                    sizeof(double), evSz, fp) == evSz);
    }

    fclose(fp);
    if (!ok) { fprintf(stderr, "WARNING: .pca read error — ignoring\n"); return PcaBasis{}; }
    return pca;
}

// ---------------------------------------------------------------------------
// Realign one cluster
// ---------------------------------------------------------------------------
static bool realignCluster(
    int clusterId,
    const std::vector<int64_t>& gidx,  // 0-based global spike positions
    int nChan, int nSamp, int maxShift, float minScore, int nIter,
    bool verbose,
    const PcaBasis& pca,
    int nFeatCols, int timeDim,
    FILE* spkF, FILE* resF, FILE* fetF, FILE* cluF)
{
    const int64_t N = static_cast<int64_t>(gidx.size());
    if (N == 0) return true;

    const size_t  spkElems      = static_cast<size_t>(nChan) *
                                  static_cast<size_t>(nSamp);
    const int64_t bytesPerSpike = static_cast<int64_t>(spkElems) * 2;

    const int nPcaFeats   = pca.valid() ? (pca.nCh * pca.nComp) : 0;
    const int nExtraFeats = (pca.valid() && nPcaFeats < nFeatCols)
                            ? (nFeatCols - nPcaFeats) : 0;

    // --- Load waveforms ---
    std::vector<int16_t> wavBuf(static_cast<size_t>(N) * spkElems);
    for (int64_t i = 0; i < N; ++i) {
        const off_t off = static_cast<off_t>(gidx[static_cast<size_t>(i)] * bytesPerSpike);
        if (fseeko(spkF, off, SEEK_SET) != 0) {
            fprintf(stderr, "ERROR: .spk seek failed for spike %lld\n",
                    (long long)gidx[static_cast<size_t>(i)]);
            return false;
        }
        int16_t* dst = wavBuf.data() +
                       static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
        if (fread(dst, 2, spkElems, spkF) != spkElems) {
            fprintf(stderr, "ERROR: .spk short read at spike %lld\n",
                    (long long)gidx[static_cast<size_t>(i)]);
            return false;
        }
    }

    // --- Load timestamps and extra features ---
    std::vector<int64_t> clusterTs(static_cast<size_t>(N));
    std::vector<std::vector<int64_t>> extraFeats;
    if (nExtraFeats > 0)
        extraFeats.assign(static_cast<size_t>(N),
                          std::vector<int64_t>(static_cast<size_t>(nExtraFeats), 0));

    // Read .fet header to get actual nDims
    int32_t fetNDim = timeDim;
    fseeko(fetF, 0, SEEK_SET);
    { size_t _r = fread(&fetNDim, sizeof(int32_t), 1, fetF); (void)_r; }
    if (fetNDim <= 0) fetNDim = timeDim;

    for (int64_t i = 0; i < N; ++i) {
        const int64_t p = gidx[static_cast<size_t>(i)];

        // Timestamp from .res
        if (fseeko(resF, static_cast<off_t>(p * (int64_t)sizeof(int64_t)), SEEK_SET) != 0 ||
            fread(&clusterTs[static_cast<size_t>(i)], sizeof(int64_t), 1, resF) != 1) {
            fprintf(stderr, "ERROR: cannot read .res at spike %lld\n", (long long)p);
            return false;
        }

        // Extra features from .fet
        for (int k = 0; k < nExtraFeats; ++k) {
            const int col = nPcaFeats + k;
            const off_t off = static_cast<off_t>(sizeof(int32_t)) +
                              static_cast<off_t>(p * (int64_t)fetNDim + col) *
                              static_cast<off_t>(sizeof(int64_t));
            if (fseeko(fetF, off, SEEK_SET) != 0 ||
                fread(&extraFeats[static_cast<size_t>(i)][static_cast<size_t>(k)],
                      sizeof(int64_t), 1, fetF) != 1) {
                fprintf(stderr, "WARNING: cannot read extra feat col=%d spike=%lld\n",
                        col, (long long)p);
            }
        }
    }

    // --- Iterative xcorr alignment ---
    std::vector<int>   cumShift(static_cast<size_t>(N), 0);
    std::vector<float> bestScore(static_cast<size_t>(N), 0.0f);

    for (int iter = 0; iter < nIter; ++iter) {
        // Build mean template
        std::vector<int64_t> acc(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data() +
                               static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e)
                acc[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> tmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e)
            tmpl[e] = static_cast<int16_t>(acc[e] / N);

        std::vector<int>   sh(static_cast<size_t>(N), 0);
        std::vector<float> sc(static_cast<size_t>(N), 0.0f);
        int rc = XcorrDispatch::compute(
            wavBuf.data(), tmpl.data(),
            static_cast<int>(N), nChan, nSamp,
            maxShift, minScore, sh.data(), sc.data());
        if (rc != 0) {
            fprintf(stderr, "ERROR: XcorrDispatch failed (rc=%d)\n", rc);
            return false;
        }

        int changed = 0;
        for (int64_t i = 0; i < N; ++i) {
            const int s = sh[static_cast<size_t>(i)];
            if (s == 0) continue;
            int16_t* w = wavBuf.data() +
                         static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            std::vector<int16_t> tmp(spkElems, 0);
            for (int t = 0; t < nSamp; ++t) {
                const int src = t - s;
                if (src < 0 || src >= nSamp) continue;
                for (int ch = 0; ch < nChan; ++ch)
                    tmp[static_cast<size_t>(ch * nSamp + t)] =
                        w[static_cast<size_t>(ch * nSamp + src)];
            }
            std::copy(tmp.begin(), tmp.end(), w);
            cumShift[static_cast<size_t>(i)] += s;
            bestScore[static_cast<size_t>(i)] = sc[static_cast<size_t>(i)];
            ++changed;
        }
        printf("  Cluster %d  iter %d: %d shifted\n", clusterId, iter + 1, changed);
        if (changed == 0) break;
    }

    // --- Statistics ---
    {
        // Final score pass (maxShift=0, score only)
        std::vector<int64_t> acc2(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data() +
                               static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e)
                acc2[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> finalTmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e)
            finalTmpl[e] = static_cast<int16_t>(acc2[e] / N);

        std::vector<int>   dummySh(static_cast<size_t>(N), 0);
        std::vector<float> allScores(static_cast<size_t>(N), 0.0f);
        XcorrDispatch::compute(wavBuf.data(), finalTmpl.data(),
                               static_cast<int>(N), nChan, nSamp,
                               0, 0.0f, dummySh.data(), allScores.data());

        float scoreMin = 2.0f, scoreMax = -2.0f, scoreSum = 0.0f;
        int   nShifted = 0, nBelow = 0;
        int   shiftMin = INT_MAX, shiftMax = INT_MIN;
        float shiftAbsSum = 0.0f;

        for (int64_t i = 0; i < N; ++i) {
            const float s  = allScores[static_cast<size_t>(i)];
            const int   sh = cumShift[static_cast<size_t>(i)];
            if (s < scoreMin) scoreMin = s;
            if (s > scoreMax) scoreMax = s;
            scoreSum += s;
            if (s < minScore) ++nBelow;
            if (sh != 0) ++nShifted;
            if (sh < shiftMin) shiftMin = sh;
            if (sh > shiftMax) shiftMax = sh;
            shiftAbsSum += static_cast<float>(sh < 0 ? -sh : sh);
        }
        printf("  Cluster %d  %lld spikes  shifted=%d  score min=%.3f max=%.3f"
               " mean=%.3f  below-threshold=%d\n",
               clusterId, (long long)N, nShifted,
               scoreMin, scoreMax, scoreSum / static_cast<float>(N), nBelow);
        if (nShifted > 0)
            printf("  Cluster %d  shift range [%d, %d]  mean-abs=%.2f\n",
                   clusterId, shiftMin, shiftMax,
                   shiftAbsSum / static_cast<float>(N));

        if (verbose) {
            for (int64_t i = 0; i < N; ++i) {
                if (cumShift[static_cast<size_t>(i)] != 0)
                    printf("    spike %lld  shift=%d  score=%.3f\n",
                           (long long)gidx[static_cast<size_t>(i)],
                           cumShift[static_cast<size_t>(i)],
                           bestScore[static_cast<size_t>(i)]);
            }
        }
    }

    // --- Compute new timestamps and sort order ---
    std::vector<int64_t> newTs(static_cast<size_t>(N));
    for (int64_t i = 0; i < N; ++i)
        newTs[static_cast<size_t>(i)] =
            clusterTs[static_cast<size_t>(i)] +
            static_cast<int64_t>(cumShift[static_cast<size_t>(i)]);

    std::vector<int64_t> sortedOrder(static_cast<size_t>(N));
    std::iota(sortedOrder.begin(), sortedOrder.end(), int64_t{0});
    std::stable_sort(sortedOrder.begin(), sortedOrder.end(),
        [&](int64_t a, int64_t b) {
            return newTs[static_cast<size_t>(a)] < newTs[static_cast<size_t>(b)];
        });

    std::vector<int64_t> targetPos(gidx.begin(), gidx.end());
    std::sort(targetPos.begin(), targetPos.end());

    // --- makeFetRow lambda ---
    auto makeFetRow = [&](int64_t csIdx, const int16_t* wav,
                          int64_t ts) -> std::vector<int64_t>
    {
        std::vector<int64_t> row(static_cast<size_t>(timeDim), int64_t{0});

        if (pca.valid() && pca.nCh == nChan) {
            int outCol = 0;
            for (int ch = 0; ch < pca.nCh; ++ch) {
                const double* E    = pca.evec[static_cast<size_t>(ch)].data();
                const double* mean = pca.means[static_cast<size_t>(ch)].data();
                for (int c = 0; c < pca.nComp; ++c) {
                    double dot = 0.0;
                    for (int j = 0; j < pca.data2use; ++j) {
                        double raw = static_cast<double>(
                            wav[static_cast<size_t>(ch * nSamp + pca.recShift + j)]);
                        const double x = pca.centered ? (raw - mean[j]) : raw;
                        dot += E[j + c * pca.data2use] * x;
                    }
                    row[static_cast<size_t>(outCol++)] =
                        static_cast<int64_t>(llround(dot));
                }
            }
            for (int k = 0; k < nExtraFeats; ++k)
                row[static_cast<size_t>(nPcaFeats + k)] =
                    extraFeats[static_cast<size_t>(csIdx)][static_cast<size_t>(k)];
        }
        // (No PCA: feature columns are left as zero — .fet not rewritten
        //  for this cluster; only .spk, .res, .clu are updated)

        row[static_cast<size_t>(nFeatCols)] = ts;
        return row;
    };

    // --- Read original cluster IDs before any overwrites ---
    std::vector<int32_t> origCluIds(static_cast<size_t>(N), 0);
    if (cluF) {
        for (int64_t i = 0; i < N; ++i) {
            const off_t off = static_cast<off_t>(sizeof(int32_t)) +
                              static_cast<off_t>(gidx[static_cast<size_t>(i)] *
                              (int64_t)sizeof(int32_t));
            fseeko(cluF, off, SEEK_SET);
            { size_t _r = fread(&origCluIds[static_cast<size_t>(i)], sizeof(int32_t), 1, cluF); (void)_r; }
        }
    }

    // --- Write back ---
    for (int64_t j = 0; j < N; ++j) {
        const int64_t csIdx = sortedOrder[static_cast<size_t>(j)];
        const int64_t dest  = targetPos[static_cast<size_t>(j)];
        const int64_t ts    = newTs[static_cast<size_t>(csIdx)];

        const int16_t* w = wavBuf.data() +
                           static_cast<ptrdiff_t>(csIdx) * static_cast<ptrdiff_t>(spkElems);

        // .spk
        fseeko(spkF, static_cast<off_t>(dest * bytesPerSpike), SEEK_SET);
        fwrite(w, 2, spkElems, spkF);

        // .res
        fseeko(resF, static_cast<off_t>(dest * (int64_t)sizeof(int64_t)), SEEK_SET);
        fwrite(&ts, sizeof(int64_t), 1, resF);

        // .fet (only if PCA available for proper reprojection)
        if (pca.valid()) {
            std::vector<int64_t> row = makeFetRow(csIdx, w, ts);
            const off_t fetOff = static_cast<off_t>(sizeof(int32_t)) +
                                 static_cast<off_t>(dest * (int64_t)fetNDim) *
                                 static_cast<off_t>(sizeof(int64_t));
            fseeko(fetF, fetOff, SEEK_SET);
            fwrite(row.data(), sizeof(int64_t), static_cast<size_t>(timeDim), fetF);
        }

        // .clu
        if (cluF) {
            const int32_t id = origCluIds[static_cast<size_t>(csIdx)];
            const off_t cluOff = static_cast<off_t>(sizeof(int32_t)) +
                                 static_cast<off_t>(dest * (int64_t)sizeof(int32_t));
            fseeko(cluF, cluOff, SEEK_SET);
            fwrite(&id, sizeof(int32_t), 1, cluF);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static void usage(const char* prog)
{
    fprintf(stderr,
        "Usage: %s FileBase ElecNo [Options]\n\n"
        "Options:\n"
        "  -Threshold F      Minimum xcorr score to accept a shift (default: 0.70)\n"
        "  -Iterations N     Number of alignment passes            (default: 2)\n"
        "  -MaxShift N       Search radius in samples              (default: peakSamp/2)\n"
        "  -Clusters A,B,C   Cluster IDs to realign                (default: all)\n"
        "  -Verbose          Print per-spike shift details\n\n"
        "Files read/written: FileBase.{spk,res,clu,fet,pca}.ElecNo\n"
        "Parameters read from: FileBase.xml (nChannels, nSamples, peakSampleIndex)\n",
        prog);
}

int main(int argc, char** argv)
{
    if (argc < 3) { usage(argv[0]); return 1; }

    const char* fileBase = argv[1];
    const int   elecNo   = atoi(argv[2]);

    // Defaults
    float minScore  = 0.70f;
    int   nIter     = 2;
    int   maxShift  = 0;   // 0 = derive from .xml
    bool  verbose   = false;
    std::set<int> clusterFilter;  // empty = all

    // Parse options
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "-Threshold") == 0 && i + 1 < argc) {
            minScore = static_cast<float>(atof(argv[++i]));
        } else if (strcmp(argv[i], "-Iterations") == 0 && i + 1 < argc) {
            nIter = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-MaxShift") == 0 && i + 1 < argc) {
            maxShift = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-Clusters") == 0 && i + 1 < argc) {
            char* p = argv[++i];
            while (*p) {
                clusterFilter.insert(atoi(p));
                while (*p && *p != ',') ++p;
                if (*p == ',') ++p;
            }
        } else if (strcmp(argv[i], "-Verbose") == 0) {
            verbose = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    // --- Build file paths ---
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s.xml", fileBase);
    const std::string xmlPath = buf;
    snprintf(buf, sizeof(buf), "%s.spk.%d", fileBase, elecNo);
    const std::string spkPath = buf;
    snprintf(buf, sizeof(buf), "%s.res.%d", fileBase, elecNo);
    const std::string resPath = buf;
    snprintf(buf, sizeof(buf), "%s.clu.%d", fileBase, elecNo);
    const std::string cluPath = buf;
    snprintf(buf, sizeof(buf), "%s.fet.%d", fileBase, elecNo);
    const std::string fetPath = buf;
    snprintf(buf, sizeof(buf), "%s.pca.%d", fileBase, elecNo);
    const std::string pcaPath = buf;

    // --- Read .xml for nChannels, nSamples, peakSampleIndex ---
    int nChan    = 0;
    int nSamp    = 0;
    int peakSamp = 0;

    {
        FILE* xf = fopen(xmlPath.c_str(), "r");
        if (!xf) {
            fprintf(stderr, "WARNING: cannot open %s — nChannels, nSamples"
                    " must be inferable from file sizes\n", xmlPath.c_str());
        } else {
            fseek(xf, 0, SEEK_END);
            long xsz = ftell(xf);
            rewind(xf);
            std::vector<char> xbuf(static_cast<size_t>(xsz) + 1, '\0');
            { size_t _r = fread(xbuf.data(), 1, static_cast<size_t>(xsz), xf); (void)_r; }
            fclose(xf);

            // Try electrode-group-specific first, then global
            // nSamples is per waveform across all channels
            xmlGetInt(xbuf.data(), "nSamples",        &nSamp);
            xmlGetInt(xbuf.data(), "peakSampleIndex",  &peakSamp);

            // nChannels for this group — look inside the Nth channelGroup
            // Simple approach: count channels in the target group
            // For now use nChannels globally (works for single-group sessions)
            xmlGetInt(xbuf.data(), "nChannels", &nChan);

            // Also try spikeDetection nChannels for the group
            // More robust: count <channel> tags in the target group
            // This is good enough for the common case
        }
    }

    if (nSamp <= 0 || nChan <= 0) {
        // Fallback: derive from file sizes if we have .clu to know nSpikes
        FILE* cf = fopen(cluPath.c_str(), "rb");
        if (!cf) {
            fprintf(stderr, "ERROR: cannot determine nChannels/nSamples"
                    " — provide a valid %s\n", xmlPath.c_str());
            return 1;
        }
        int32_t nClu32 = 0;
        { size_t _r = fread(&nClu32, sizeof(int32_t), 1, cf); (void)_r; }
        fseek(cf, 0, SEEK_END);
        long cluSz = ftell(cf);
        fclose(cf);
        int64_t nSpikes = (cluSz - (long)sizeof(int32_t)) / (long)sizeof(int32_t);

        if (nSpikes <= 0) {
            fprintf(stderr, "ERROR: .clu file appears empty\n");
            return 1;
        }

        // .spk size = nSpikes * nChan * nSamp * 2
        FILE* sf = fopen(spkPath.c_str(), "rb");
        if (!sf) { fprintf(stderr, "ERROR: cannot open %s\n", spkPath.c_str()); return 1; }
        fseek(sf, 0, SEEK_END);
        long spkSz = ftell(sf);
        fclose(sf);

        long samplesPerSpike = spkSz / (nSpikes * 2);
        if (nChan <= 0) nChan = 4; // last-resort guess
        nSamp = static_cast<int>(samplesPerSpike / nChan);
        fprintf(stderr, "WARNING: guessed nChan=%d nSamp=%d from file sizes\n",
                nChan, nSamp);
    }

    if (maxShift == 0)
        maxShift = std::max(1, peakSamp > 0 ? peakSamp / 2 : nSamp / 4);

    printf("SpikeRealign  %s.{spk,res,clu,fet}.%d\n", fileBase, elecNo);
    printf("  nChan=%d  nSamp=%d  peakSamp=%d\n", nChan, nSamp, peakSamp);
    printf("  Threshold=%.2f  Iterations=%d  MaxShift=+-%d\n",
           minScore, nIter, maxShift);
    printf("  Backend: %s\n", XcorrDispatch::backendName());

    // --- Load PCA ---
    PcaBasis pca = loadPca(pcaPath.c_str(), nSamp);
    if (pca.valid())
        printf("  PCA: %dch x %dcomp  recShift=%d%s\n",
               pca.nCh, pca.nComp, pca.recShift, pca.centered ? " centered" : "");
    else
        printf("  PCA: not available — features will not be recomputed\n");

    // --- Load .clu — map cluster ID → list of 0-based spike positions ---
    FILE* cf = fopen(cluPath.c_str(), "rb");
    if (!cf) { fprintf(stderr, "ERROR: cannot open %s\n", cluPath.c_str()); return 1; }

    int32_t nClusters32 = 0;
    { size_t _r = fread(&nClusters32, sizeof(int32_t), 1, cf); (void)_r; }

    std::vector<int32_t> allCluIds;
    {
        int32_t id;
        while (fread(&id, sizeof(int32_t), 1, cf) == 1)
            allCluIds.push_back(id);
    }
    fclose(cf);

    const int64_t nTotalSpikes = static_cast<int64_t>(allCluIds.size());
    printf("  Total spikes: %lld  Clusters: %d\n",
           (long long)nTotalSpikes, (int)nClusters32);

    // Build cluster → spike index map
    std::set<int> uniqueClusters;
    for (auto id : allCluIds) uniqueClusters.insert(static_cast<int>(id));

    // Determine which clusters to process
    std::set<int> toProcess;
    if (clusterFilter.empty()) {
        for (int id : uniqueClusters)
            if (id > 1) toProcess.insert(id);  // skip noise (0) and artefact (1)
    } else {
        toProcess = clusterFilter;
    }

    printf("  Processing %zu cluster(s): ", toProcess.size());
    for (int id : toProcess) printf("%d ", id);
    printf("\n\n");

    // --- Read .fet header for nDims ---
    int32_t fetNDim = 0;
    {
        FILE* ff = fopen(fetPath.c_str(), "rb");
        if (!ff) { fprintf(stderr, "ERROR: cannot open %s\n", fetPath.c_str()); return 1; }
        { size_t _r = fread(&fetNDim, sizeof(int32_t), 1, ff); (void)_r; }
        fclose(ff);
    }
    const int timeDim   = fetNDim > 0 ? fetNDim : (pca.nCh * pca.nComp + 1);
    const int nFeatCols = timeDim - 1;

    // --- Open files for random-access read+write ---
    FILE* spkF = fopen(spkPath.c_str(), "r+b");
    FILE* resF = fopen(resPath.c_str(), "r+b");
    FILE* fetF = fopen(fetPath.c_str(), "r+b");
    FILE* cluF = fopen(cluPath.c_str(), "r+b");

    if (!spkF || !resF || !fetF) {
        if (spkF) fclose(spkF);
        if (resF) fclose(resF);
        if (fetF) fclose(fetF);
        if (cluF) fclose(cluF);
        fprintf(stderr, "ERROR: cannot open files for read/write\n");
        return 1;
    }

    // --- Process each cluster ---
    int nProcessed = 0, nFailed = 0;
    for (int cid : toProcess) {
        std::vector<int64_t> gidx;
        gidx.reserve(256);
        for (int64_t i = 0; i < nTotalSpikes; ++i)
            if (static_cast<int>(allCluIds[static_cast<size_t>(i)]) == cid)
                gidx.push_back(i);

        if (gidx.empty()) {
            printf("Cluster %d: not found in .clu, skipping\n", cid);
            continue;
        }

        printf("=== Cluster %d (%lld spikes) ===\n",
               cid, (long long)gidx.size());

        bool ok = realignCluster(
            cid, gidx,
            nChan, nSamp, maxShift, minScore, nIter, verbose,
            pca, nFeatCols, timeDim,
            spkF, resF, fetF, cluF);

        if (ok) ++nProcessed;
        else    ++nFailed;
    }

    fclose(spkF);
    fclose(resF);
    fclose(fetF);
    if (cluF) fclose(cluF);

    printf("\nDone: %d cluster(s) realigned, %d failed.\n", nProcessed, nFailed);
    return nFailed > 0 ? 1 : 0;
}
