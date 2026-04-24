/***************************************************************************
 * process_alignspikes.cpp
 *
 * Align spike waveform snippets to their true peak before PCA feature
 * extraction, eliminating shift-induced bimodality in feature space.
 *
 * ALGORITHM
 * ─────────
 * For each spike snippet in .spk.N (or .spkD.N):
 *
 *   1. Compute cross-channel energy E(t) = Σ_c |W[t, c]| in a window
 *      [peakSampleIndex − maxShift … peakSampleIndex + maxShift].
 *
 *   2. Find foundSample = argmax E(t).
 *      Sub-sample refinement via parabolic interpolation reduces residual
 *      error from ±0.5 to ~±0.1 samples.
 *
 *   3. shift = foundSample − peakSampleIndex
 *
 *   4. If shift ≠ 0: re-read the spike window from the raw .fil at
 *      extTs = originalTs + shift, extract the group's channels, and
 *      (in --stderiv mode) apply SDIFF_ALLPAIRS on the fly.
 *      Array-rolling is intentionally avoided — re-extraction is the
 *      only artifact-free method.
 *
 *   5. Write the updated .spk.N sorted by extTs.  .res.N is not modified
 *      (detection times are physical spike times, not extraction times).
 *
 * USAGE
 * ─────
 *   process_alignspikes [options] basename electrodeGroup
 *
 *   -n  nTotalChannels    Total channels in .fil
 *   -c  channelList       Comma-separated channel IDs for this group
 *   -w  nSamples          Samples per spike waveform
 *   -p  peakSampleIndex   Target peak position (1-based, converted to 0-based)
 *   -m  maxShift          Max shift in samples [default: 3]
 *   --stderiv             Input/output is .spkD.N with SDIFF_ALLPAIRS re-extraction
 *   --min-score  f        Minimum relative energy score [0,1] to accept a shift;
 *                         spikes below threshold are left unshifted [default: 0.0]
 *   -v                    Verbose
 *   -h                    Help
 *
 * OUTPUT FILES
 * ────────────
 *   basename.spk.G   overwritten in-place (aligned snippets, reordered by extTs)
 *   basename.res.G   UNCHANGED
 *   basename.fet.G   caller must delete and re-run process_pca
 *   basename.pca.G   caller must delete and re-run process_pca
 *
 * copyright   (C) 2026 Gravios / NeuroSuite-3 contributors
 * GNU GPL v3 or later.
 ***************************************************************************/

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <climits>
#include <stdexcept>

// ── SDIFF_ALLPAIRS kernel (same as process_extractspikes_stderiv) ─────────

static double sdiff_allpairs(const short* frame,
                              const int*   chanList,
                              int          idx,
                              int          nChanGrp)
{
    if (nChanGrp <= 1) return 0.0;
    double sum = 0.0;
    for (int j = 0; j < nChanGrp; ++j) sum += frame[chanList[j]];
    return (double)nChanGrp * frame[chanList[idx]] - sum;
}

// ── Helpers ───────────────────────────────────────────────────────────────

static void die(const char* msg)
{
    fprintf(stderr, "process_alignspikes: %s\n", msg);
    exit(1);
}

static FILE* xfopen(const char* path, const char* mode)
{
    FILE* f = fopen(path, mode);
    if (!f) {
        fprintf(stderr, "process_alignspikes: cannot open '%s'\n", path);
        exit(1);
    }
    return f;
}

// Parse comma-separated integer list into vector.
static std::vector<int> parseIntList(const char* s)
{
    std::vector<int> v;
    char* buf = strdup(s);
    char* p   = buf;
    char* tok;
    while ((tok = strsep(&p, ",")) != nullptr) {
        if (*tok) v.push_back(atoi(tok));
    }
    free(buf);
    return v;
}

// ── Shift estimation ──────────────────────────────────────────────────────

/** Compute optimal alignment shift for one spike snippet.
 *
 *  @param wav      Snippet data: layout [sample * nChan + chan], nSamp * nChan shorts.
 *  @param nChan    Channels in snippet (may be derivative channels for spkD).
 *  @param nSamp    Samples per snippet.
 *  @param target   Target peak sample index (0-based).
 *  @param maxShift Maximum allowed shift in either direction.
 *  @param score    Output: quality metric ∈ [0,1].  1 = energy all at target.
 *  @return         Integer shift δ (foundPeak − target).  0 if no improvement.
 */
static int computeShift(const short* wav,
                         int nChan, int nSamp, int target, int maxShift,
                         double* score)
{
    int lo = std::max(0, target - maxShift);
    int hi = std::min(nSamp - 1, target + maxShift);
    int wSz = hi - lo + 1;

    // Energy per sample in window
    std::vector<int64_t> energy(wSz, 0);
    for (int t = lo; t <= hi; ++t) {
        int64_t e = 0;
        for (int c = 0; c < nChan; ++c)
            e += std::abs((int)wav[t * nChan + c]);
        energy[t - lo] = e;
    }

    // Find peak
    int best = 0;
    for (int k = 1; k < wSz; ++k)
        if (energy[k] > energy[best]) best = k;

    // Parabolic sub-sample refinement
    double subPeak = lo + best;
    if (best > 0 && best < wSz - 1) {
        double a0 = (double)energy[best - 1];
        double a1 = (double)energy[best];
        double a2 = (double)energy[best + 1];
        double denom = 2.0 * (2.0*a1 - a0 - a2);
        if (denom > 1e-6) {
            double refined = (lo + best) - (a2 - a0) / denom;
            // Only accept if within bounds
            if (refined >= lo && refined <= hi)
                subPeak = refined;
        }
    }

    int intShift = (int)std::round(subPeak) - target;

    // Clamp to maxShift (parabolic refinement can only widen by rounding)
    if (intShift >  maxShift) intShift =  maxShift;
    if (intShift < -maxShift) intShift = -maxShift;

    // Quality score: fraction of total window energy at the found peak
    // normalised by the maximum possible (all energy at one sample).
    int64_t totalEnergy = 0;
    for (auto e : energy) totalEnergy += e;
    if (score) {
        if (totalEnergy > 0)
            *score = (double)energy[best] / (double)totalEnergy;
        else
            *score = 0.0;
    }

    return intShift;
}

// ── Main ──────────────────────────────────────────────────────────────────

static void usage(const char* prog)
{
    fprintf(stderr,
        "Usage: %s [options] basename electrodeGroup\n"
        "  -n nTotalChannels   Total channels in .fil\n"
        "  -c channelList      Comma-separated channel IDs (e.g. 0,1,2,3)\n"
        "  -w nSamples         Samples per spike waveform\n"
        "  -p peakSampleIndex  Target peak position (1-based)\n"
        "  -m maxShift         Maximum shift in samples [default: 3]\n"
        "  --stderiv           Process .spkD.N with SDIFF_ALLPAIRS re-extraction\n"
        "  --min-score f       Minimum energy score to accept shift [default: 0.0]\n"
        "  -v                  Verbose\n"
        "  -h                  This help\n",
        prog);
}

int main(int argc, char* argv[])
{
    // ── Parse arguments ───────────────────────────────────────────────────
    int         nTotalChannels = 0;
    std::string channelListStr;
    int         nSamples       = 0;
    int         peakSampleIdx  = -1;  // 0-based after conversion
    int         maxShift       = 3;
    bool        stderiv        = false;
    double      minScore       = 0.0;
    bool        verbose        = false;
    std::string basename;
    int         electrodeGroup = 0;

    for (int i = 1; i < argc; ) {
        const char* a = argv[i];
        if (strcmp(a, "-h") == 0) { usage(argv[0]); return 0; }
        else if (strcmp(a, "-n") == 0 && i+1 < argc)
            nTotalChannels = atoi(argv[++i]);
        else if (strcmp(a, "-c") == 0 && i+1 < argc)
            channelListStr = argv[++i];
        else if (strcmp(a, "-w") == 0 && i+1 < argc)
            nSamples = atoi(argv[++i]);
        else if (strcmp(a, "-p") == 0 && i+1 < argc)
            peakSampleIdx = atoi(argv[++i]) - 1; // convert 1-based to 0-based
        else if (strcmp(a, "-m") == 0 && i+1 < argc)
            maxShift = atoi(argv[++i]);
        else if (strcmp(a, "--stderiv") == 0)
            stderiv = true;
        else if (strcmp(a, "--min-score") == 0 && i+1 < argc)
            minScore = atof(argv[++i]);
        else if (strcmp(a, "-v") == 0)
            verbose = true;
        else if (a[0] != '-') {
            if (basename.empty()) basename = a;
            else electrodeGroup = atoi(a);
        }
        ++i;
    }

    // ── Validate ──────────────────────────────────────────────────────────
    if (basename.empty())       die("missing session basename");
    if (electrodeGroup <= 0)    die("missing or invalid electrode group number");
    if (nTotalChannels <= 0)    die("missing or invalid -n (total channel count)");
    if (channelListStr.empty()) die("missing -c (channel list)");
    if (nSamples <= 0)          die("missing or invalid -w (samples per waveform)");
    if (peakSampleIdx < 0)      die("missing or invalid -p (peak sample index)");
    if (peakSampleIdx >= nSamples) die("-p peak index >= waveform length");

    std::vector<int> chanList = parseIntList(channelListStr.c_str());
    const int nChanGrp = (int)chanList.size();
    if (nChanGrp == 0) die("empty channel list");
    for (int c : chanList)
        if (c < 0 || c >= nTotalChannels) die("channel ID out of range");

    if (maxShift <= 0) die("-m maxShift must be > 0");
    if (maxShift >= nSamples) die("-m maxShift >= nSamples; waveform too short");

    // File paths
    const std::string grpStr    = std::to_string(electrodeGroup);
    const std::string spkExt    = stderiv ? ".spkD." : ".spk.";
    const std::string resPath   = basename + ".res."  + grpStr;
    const std::string spkPath   = basename + spkExt   + grpStr;
    const std::string filPath   = basename + ".fil";

    if (verbose) {
        fprintf(stderr, "process_alignspikes:\n");
        fprintf(stderr, "  session       = %s\n", basename.c_str());
        fprintf(stderr, "  group         = %d\n", electrodeGroup);
        fprintf(stderr, "  stderiv       = %s\n", stderiv ? "yes" : "no");
        fprintf(stderr, "  nTotalChan    = %d\n", nTotalChannels);
        fprintf(stderr, "  nChanGrp      = %d\n", nChanGrp);
        fprintf(stderr, "  nSamples      = %d\n", nSamples);
        fprintf(stderr, "  peakSampleIdx = %d (0-based)\n", peakSampleIdx);
        fprintf(stderr, "  maxShift      = %d samples\n", maxShift);
        fprintf(stderr, "  minScore      = %.2f\n", minScore);
        fprintf(stderr, "  spk file      = %s\n", spkPath.c_str());
        fprintf(stderr, "  res file      = %s\n", resPath.c_str());
        fprintf(stderr, "  fil file      = %s\n", filPath.c_str());
    }

    // ── Read .res.N ───────────────────────────────────────────────────────
    FILE* resF = xfopen(resPath.c_str(), "rb");
    fseeko(resF, 0, SEEK_END);
    const off_t resSize  = ftello(resF);
    const int   nSpikes  = (int)(resSize / sizeof(int64_t));
    fseeko(resF, 0, SEEK_SET);

    std::vector<int64_t> resTs(nSpikes);
    if (nSpikes > 0 && fread(resTs.data(), sizeof(int64_t), nSpikes, resF) != (size_t)nSpikes)
        die("short read on .res file");
    fclose(resF);

    if (nSpikes == 0) {
        fprintf(stderr, "process_alignspikes: group %d has 0 spikes — nothing to do\n",
                electrodeGroup);
        return 0;
    }
    if (verbose)
        fprintf(stderr, "  nSpikes       = %d\n", nSpikes);

    // ── Read .spk.N into memory ───────────────────────────────────────────
    const int   elemsPerSpike = nSamples * nChanGrp;
    const off_t spkExpected   = (off_t)nSpikes * elemsPerSpike * sizeof(short);

    FILE* spkF = xfopen(spkPath.c_str(), "rb");
    fseeko(spkF, 0, SEEK_END);
    const off_t spkSize = ftello(spkF);
    if (spkSize != spkExpected) {
        fprintf(stderr,
                "process_alignspikes: %s size %lld does not match expected %lld "
                "(%d spikes × %d samp × %d chan × 2 bytes)\n",
                spkPath.c_str(), (long long)spkSize, (long long)spkExpected,
                nSpikes, nSamples, nChanGrp);
        fclose(spkF);
        exit(1);
    }
    fseeko(spkF, 0, SEEK_SET);

    std::vector<short> spkBuf((size_t)nSpikes * elemsPerSpike);
    if (fread(spkBuf.data(), sizeof(short), spkBuf.size(), spkF) != spkBuf.size())
        die("short read on .spk file");
    fclose(spkF);

    // ── Compute shifts ────────────────────────────────────────────────────
    std::vector<int>    shifts(nSpikes, 0);
    std::vector<double> scores(nSpikes, 0.0);
    int nToRextract = 0;

    for (int i = 0; i < nSpikes; ++i) {
        const short* wav = spkBuf.data() + (size_t)i * elemsPerSpike;
        double sc = 0.0;
        int  sh = computeShift(wav, nChanGrp, nSamples, peakSampleIdx, maxShift, &sc);
        scores[i] = sc;
        if (sc >= minScore) {
            shifts[i] = sh;
            if (sh != 0) ++nToRextract;
        }
    }

    if (verbose)
        fprintf(stderr, "  spikes to re-extract = %d / %d (%.1f%%)\n",
                nToRextract, nSpikes, 100.0 * nToRextract / nSpikes);

    // ── Re-extract shifted spikes from .fil ───────────────────────────────
    if (nToRextract > 0) {
        // Build event list sorted by file offset for near-sequential .fil access
        struct ReEvent { off_t off; int idx; };
        std::vector<ReEvent> relist;
        relist.reserve(nToRextract);

        for (int i = 0; i < nSpikes; ++i) {
            if (shifts[i] == 0) continue;
            const int64_t extTs   = resTs[i] + shifts[i];
            const int64_t winStart = extTs - peakSampleIdx;
            if (winStart < 0) {
                // Window extends before start of recording — skip
                if (verbose)
                    fprintf(stderr,
                            "  warning: spike %d at ts=%lld shift=%d would "
                            "require reading before file start; left unshifted\n",
                            i, (long long)resTs[i], shifts[i]);
                shifts[i] = 0;
                continue;
            }
            const off_t fileOff = winStart * (off_t)nTotalChannels * (off_t)sizeof(short);
            relist.push_back({fileOff, i});
        }
        std::sort(relist.begin(), relist.end(),
                  [](const ReEvent& a, const ReEvent& b){ return a.off < b.off; });

        FILE* filF = xfopen(filPath.c_str(), "rb");
        fseeko(filF, 0, SEEK_END);
        const off_t filSize = ftello(filF);
        fseeko(filF, 0, SEEK_SET);

        const int rawElems     = nSamples * nTotalChannels;
        const off_t rawBytes   = (off_t)rawElems * sizeof(short);
        std::vector<short> rawFrame(rawElems);

        // Scratch buffer for per-spike derivative output (stderiv mode)
        std::vector<short> derivFrame(elemsPerSpike);

        off_t filePos = 0;

        for (const ReEvent& ev : relist) {
            // Clamp at end of file
            if (ev.off + rawBytes > filSize) {
                if (verbose)
                    fprintf(stderr,
                            "  warning: spike %d re-extraction window extends past "
                            "end of .fil; left unshifted\n", ev.idx);
                shifts[ev.idx] = 0;
                continue;
            }

            // Seek to window start (use fseeko for sparse jumps between shifted spikes)
            if (ev.off != filePos) {
                if (fseeko(filF, ev.off, SEEK_SET) != 0)
                    die("fseeko failed on .fil");
                filePos = ev.off;
            }

            if (fread(rawFrame.data(), sizeof(short), rawElems, filF) != (size_t)rawElems)
                die("short read from .fil during re-extraction");
            filePos += rawBytes;

            short* dst = spkBuf.data() + (size_t)ev.idx * elemsPerSpike;

            if (!stderiv) {
                // Raw pipeline: extract group channels directly
                // Layout: [sample * nChanGrp + chan]
                for (int s = 0; s < nSamples; ++s) {
                    const short* frame = rawFrame.data() + s * nTotalChannels;
                    for (int c = 0; c < nChanGrp; ++c)
                        dst[s * nChanGrp + c] = frame[chanList[c]];
                }
            } else {
                // Stderiv pipeline: apply SDIFF_ALLPAIRS on each sample frame
                // then write all nChanGrp derivative channels
                // (process_pca_stderiv reads only the first nChanGrp-1 channels,
                // but the file stores all nChanGrp; match the convention from
                // process_extractspikes_stderiv)
                for (int s = 0; s < nSamples; ++s) {
                    const short* frame = rawFrame.data() + s * nTotalChannels;
                    for (int ci = 0; ci < nChanGrp; ++ci) {
                        double sd = sdiff_allpairs(frame, chanList.data(), ci, nChanGrp);
                        // Clamp to int16 range
                        if      (sd >  32767.0) sd =  32767.0;
                        else if (sd < -32768.0) sd = -32768.0;
                        dst[s * nChanGrp + ci] = (short)sd;
                    }
                }
            }
        }
        fclose(filF);
    }

    // ── Sort output order by extTs = resTs[i] + shifts[i] ─────────────────
    // The .spk.N file must be in ascending temporal order matching .res.N.
    // If any shift is non-zero the order may differ from the original.
    std::vector<int> sortOrder(nSpikes);
    std::iota(sortOrder.begin(), sortOrder.end(), 0);
    std::stable_sort(sortOrder.begin(), sortOrder.end(),
        [&](int a, int b) {
            return (resTs[a] + shifts[a]) < (resTs[b] + shifts[b]);
        });

    // Count reordered spikes
    int nReordered = 0;
    for (int j = 0; j < nSpikes; ++j)
        if (sortOrder[j] != j) ++nReordered;

    // ── Write updated .spk.N atomically (write to .tmp, rename) ───────────
    const std::string spkTmp = spkPath + ".aligntmp";
    FILE* outF = xfopen(spkTmp.c_str(), "wb");
    for (int j = 0; j < nSpikes; ++j) {
        const int i = sortOrder[j];
        if (fwrite(spkBuf.data() + (size_t)i * elemsPerSpike,
                   sizeof(short), elemsPerSpike, outF) != (size_t)elemsPerSpike) {
            fclose(outF);
            remove(spkTmp.c_str());
            die("write error on .spk output");
        }
    }
    fclose(outF);

    if (rename(spkTmp.c_str(), spkPath.c_str()) != 0) {
        remove(spkTmp.c_str());
        die("rename of .spk.aligntmp failed");
    }

    // ── Statistics ────────────────────────────────────────────────────────
    int nShifted = 0;
    int64_t sumAbsShift = 0;
    int minSh = INT_MAX, maxSh = INT_MIN;

    for (int i = 0; i < nSpikes; ++i) {
        if (shifts[i] != 0) {
            ++nShifted;
            sumAbsShift += std::abs(shifts[i]);
            if (shifts[i] < minSh) minSh = shifts[i];
            if (shifts[i] > maxSh) maxSh = shifts[i];
        }
    }

    fprintf(stderr,
            "process_alignspikes group %d: %d/%d spikes shifted "
            "(%.1f%%)  mean-abs=%.2f samp",
            electrodeGroup, nShifted, nSpikes,
            nSpikes > 0 ? 100.0 * nShifted / nSpikes : 0.0,
            nShifted > 0 ? (double)sumAbsShift / nShifted : 0.0);
    if (nShifted > 0)
        fprintf(stderr, "  range=[%+d,%+d]", minSh, maxSh);
    if (nReordered > 0)
        fprintf(stderr, "  reordered=%d", nReordered);
    fprintf(stderr, "\n");
    fprintf(stderr,
            "  NOTE: delete %s.fet%s%d and %s.pca%s%d then re-run "
            "process_pca%s\n",
            basename.c_str(), stderiv ? "D." : ".", electrodeGroup,
            basename.c_str(), stderiv ? "D." : ".", electrodeGroup,
            stderiv ? "_stderiv" : "");

    return 0;
}
