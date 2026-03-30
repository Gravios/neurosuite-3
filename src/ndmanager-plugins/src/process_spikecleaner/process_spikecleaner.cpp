/***************************************************************************
 * process_spikecleaner.cpp
 *
 * Remove pathological waveforms from .res / .spk / .fet / .clu files.
 *
 * Two conditions are detected and removed:
 *
 *   CUTOUT  — one or more channels are entirely zero across the full
 *             waveform window.  Indicates a hardware disconnection, ADC
 *             dropout, or amplifier saturation clamp.  All samples on the
 *             channel are literally 0x0000 — distinguishable from genuine
 *             low-amplitude data.
 *
 *   FLAT    — one or more channels have zero AC energy after DC removal.
 *             Catches railed amplifiers and DAC-stuck channels that output
 *             a non-zero constant instead of dropping to zero.
 *             Controlled by --remove-flat (default: on).
 *
 * Both conditions are evaluated on the .spk waveform (post-extraction);
 * matching spikes are dropped from .res, .spk, .fet, and .clu (when
 * present) simultaneously.
 *
 * Usage
 * -----
 *   process_spikecleaner  basename  group  nChannels  nSamples  resolution
 *                         [--remove-flat 0|1]  [--dry-run]
 *
 * Arguments
 * ---------
 *   basename     session file basename (without extension)
 *   group        electrode group number (1-based)
 *   nChannels    number of channels in this group
 *   nSamples     samples per waveform per channel
 *   resolution   ADC bits (must be 16; 32-bit recordings not yet supported)
 *
 *   --remove-flat 0|1   also remove flat (constant) channels (default 1)
 *   --dry-run           report counts without modifying files
 *
 * Files
 * -----
 *   Reads:   basename.res.group   basename.spk.group
 *            basename.fet.group   basename.clu.group  (optional)
 *
 *   Writes:  same files, in-place (originals renamed to .group.bak by the
 *            calling bash script ndm_spikecleaner before this binary runs)
 *
 * Exit codes
 * ----------
 *   0   success (even if 0 spikes removed)
 *   1   argument or file error
 *
 * Pipeline position
 * -----------------
 *   After ndm_extractspikes / ndm_extractspikes_sdiff.
 *   Before ndm_pca and ndm_klustakwik.
 *
 ***************************************************************************/

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// ---------------------------------------------------------------------------
// isCutout — true if every sample on any channel in this waveform is zero.
// Layout: sample-major int16  [sample * nChan + chan]
// ---------------------------------------------------------------------------
static bool isCutout(const int16_t *wav, int nChan, int nSamples)
{
    for (int c = 0; c < nChan; ++c) {
        bool allZero = true;
        for (int s = 0; s < nSamples && allZero; ++s)
            if (wav[s * nChan + c] != 0) allZero = false;
        if (allZero) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// isFlat — true if any channel has zero variance (constant value after DC).
// A channel stuck at a non-zero constant passes isCutout but fails here.
// Uses integer arithmetic: var = E[x²] - E[x]² scaled by nSamples².
// ---------------------------------------------------------------------------
static bool isFlat(const int16_t *wav, int nChan, int nSamples)
{
    for (int c = 0; c < nChan; ++c) {
        int64_t sum = 0, sumSq = 0;
        for (int s = 0; s < nSamples; ++s) {
            const int64_t v = wav[s * nChan + c];
            sum   += v;
            sumSq += v * v;
        }
        // variance * nSamples² = nSamples*sumSq - sum*sum
        // zero iff all samples are equal
        const int64_t varScaled = (int64_t)nSamples * sumSq - sum * sum;
        if (varScaled == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    // ── argument parsing ───────────────────────────────────────────────────
    if (argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        cout << "usage: process_spikecleaner "
                "basename group nChannels nSamples resolution "
                "[--remove-flat 0|1] [--dry-run]\n";
        return 0;
    }

    if (argc < 6) {
        cerr << "error: expected 5 positional arguments "
                "(basename group nChannels nSamples resolution)\n";
        return 1;
    }

    const string basename   = argv[1];
    const string group      = argv[2];
    const int    nChan      = atoi(argv[3]);
    const int    nSamples   = atoi(argv[4]);
    const int    resolution = atoi(argv[5]);

    bool removeFlat = true;
    bool dryRun     = false;

    for (int i = 6; i < argc; ++i) {
        if (!strcmp(argv[i], "--remove-flat") && i + 1 < argc)
            removeFlat = (atoi(argv[++i]) != 0);
        else if (!strcmp(argv[i], "--dry-run"))
            dryRun = true;
        else {
            cerr << "error: unknown argument '" << argv[i] << "'\n";
            return 1;
        }
    }

    if (nChan <= 0 || nSamples <= 0) {
        cerr << "error: nChannels and nSamples must be positive\n";
        return 1;
    }
    if (resolution != 16) {
        cerr << "error: only 16-bit resolution is supported (got "
             << resolution << ")\n";
        return 1;
    }

    // ── file names ─────────────────────────────────────────────────────────
    const string resPath = basename + ".res." + group;
    const string spkPath = basename + ".spk." + group;
    const string fetPath = basename + ".fet." + group;
    const string cluPath = basename + ".clu." + group;

    // ── open .spk (required) ───────────────────────────────────────────────
    FILE *spkIn = fopen(spkPath.c_str(), "rb");
    if (!spkIn) {
        cerr << "error: cannot open " << spkPath << "\n";
        return 1;
    }

    // ── open .res (required) ───────────────────────────────────────────────
    // Binary format: nSpikes × int64_t timestamps
    FILE *resIn = fopen(resPath.c_str(), "rb");
    if (!resIn) {
        cerr << "error: cannot open " << resPath << "\n";
        fclose(spkIn);
        return 1;
    }

    // ── determine spike count from .spk file size ──────────────────────────
    fseeko(spkIn, 0, SEEK_END);
    const off_t spkBytes  = ftello(spkIn);
    const int   wavBytes  = nChan * nSamples * 2;   // int16
    if (spkBytes % wavBytes != 0) {
        cerr << "error: " << spkPath
             << " size (" << spkBytes << " bytes) is not a multiple of "
             << wavBytes << " (nChan*nSamples*2)\n";
        fclose(spkIn); fclose(resIn);
        return 1;
    }
    const int nSpikes = (int)(spkBytes / wavBytes);
    fseeko(spkIn, 0, SEEK_SET);

    // ── optional files (.fet, .clu) ────────────────────────────────────────
    // .fet: text format — int32 nDims header, then nSpikes lines of nDims int64 values
    // .clu: text format — int32 nClusters header, then nSpikes int32 cluster IDs
    FILE *fetIn  = fopen(fetPath.c_str(), "r");
    FILE *cluIn  = fopen(cluPath.c_str(), "r");

    int nDims    = 0;
    int nClust   = 0;
    if (fetIn)  { if (fscanf(fetIn, "%d", &nDims) != 1)  { fclose(fetIn);  fetIn  = nullptr; } }
    if (cluIn)  { if (fscanf(cluIn, "%d", &nClust) != 1) { fclose(cluIn);  cluIn  = nullptr; } }

    // ── scan all spikes ────────────────────────────────────────────────────
    vector<int16_t>  wav(nChan * nSamples);
    vector<int64_t>  resKeep;
    vector<vector<int64_t>> fetKeep;
    vector<int32_t>  cluKeep;

    resKeep.reserve(nSpikes);
    if (fetIn) fetKeep.reserve(nSpikes);
    if (cluIn) cluKeep.reserve(nSpikes);

    int nCutoutRemoved = 0;
    int nFlatRemoved   = 0;

    for (int sp = 0; sp < nSpikes; ++sp) {
        // Read .spk waveform
        if ((int)fread(wav.data(), 2, nChan * nSamples, spkIn) != nChan * nSamples) {
            cerr << "error: short read from " << spkPath
                 << " at spike " << sp << "\n";
            break;
        }

        // Read .res timestamp
        int64_t ts = 0;
        if (fread(&ts, sizeof(int64_t), 1, resIn) != 1) {
            cerr << "error: short read from " << resPath
                 << " at spike " << sp << "\n";
            break;
        }

        // Read .fet row (optional)
        vector<int64_t> fetRow;
        if (fetIn) {
            fetRow.resize(nDims);
            for (int d = 0; d < nDims; ++d)
                if (fscanf(fetIn, "%lld", (long long *)&fetRow[d]) != 1) {
                    fclose(fetIn); fetIn = nullptr; break;
                }
        }

        // Read .clu cluster ID (optional)
        int32_t cluId = -1;
        if (cluIn)
            if (fscanf(cluIn, "%d", &cluId) != 1) {
                fclose(cluIn); cluIn = nullptr;
            }

        // ── classify ──────────────────────────────────────────────────────
        bool drop = false;

        if (isCutout(wav.data(), nChan, nSamples)) {
            ++nCutoutRemoved;
            drop = true;
        } else if (removeFlat && isFlat(wav.data(), nChan, nSamples)) {
            ++nFlatRemoved;
            drop = true;
        }

        if (!drop) {
            resKeep.push_back(ts);
            if (fetIn) fetKeep.push_back(std::move(fetRow));
            if (cluIn) cluKeep.push_back(cluId);
        }
    }

    fclose(spkIn);
    fclose(resIn);
    if (fetIn) fclose(fetIn);
    if (cluIn) fclose(cluIn);

    const int nRemoved = nCutoutRemoved + nFlatRemoved;
    const int nKept    = (int)resKeep.size();

    // ── report ─────────────────────────────────────────────────────────────
    cout << "process_spikecleaner: group " << group
         << "  total=" << nSpikes
         << "  removed=" << nRemoved
         << " (cutout=" << nCutoutRemoved
         << " flat=" << nFlatRemoved << ")"
         << "  kept=" << nKept << "\n";

    if (dryRun) {
        cout << "  dry-run: files not modified\n";
        return 0;
    }

    if (nRemoved == 0) {
        cout << "  nothing to remove\n";
        return 0;
    }

    // ── write filtered output ──────────────────────────────────────────────

    // .res — binary int64
    {
        FILE *f = fopen(resPath.c_str(), "wb");
        if (!f) { cerr << "error: cannot write " << resPath << "\n"; return 1; }
        if (!resKeep.empty())
            fwrite(resKeep.data(), sizeof(int64_t), resKeep.size(), f);
        fclose(f);
    }

    // .spk — need to re-read original (already renamed to .bak by caller)
    {
        const string bakPath = spkPath + ".bak";
        FILE *bakIn  = fopen(bakPath.c_str(), "rb");
        FILE *spkOut = fopen(spkPath.c_str(), "wb");
        if (!bakIn || !spkOut) {
            cerr << "error: cannot open " << bakPath << " or " << spkPath << "\n";
            if (bakIn)  fclose(bakIn);
            if (spkOut) fclose(spkOut);
            return 1;
        }
        // Rebuild keep-set as a sorted index list for sequential .bak reads
        // We need to know which spike indices survived — track in a second pass.
        // Simpler: re-scan bak, keep same indices as before.
        // We know resKeep[i] corresponds to the i-th kept spike; we need their
        // original waveforms in order.  Re-read bak sequentially, skip dropped.
        //
        // Build a boolean keep[] from the original scan:
        // Re-derive by re-reading bak and re-applying the same filter.
        vector<int16_t> wav2(nChan * nSamples);
        for (int sp = 0; sp < nSpikes; ++sp) {
            if ((int)fread(wav2.data(), 2, nChan * nSamples, bakIn) != nChan * nSamples)
                break;
            bool drop = isCutout(wav2.data(), nChan, nSamples)
                     || (removeFlat && isFlat(wav2.data(), nChan, nSamples));
            if (!drop)
                fwrite(wav2.data(), 2, nChan * nSamples, spkOut);
        }
        fclose(bakIn);
        fclose(spkOut);
    }

    // .fet — text format
    if (!fetKeep.empty()) {
        FILE *f = fopen(fetPath.c_str(), "w");
        if (f) {
            fprintf(f, "%d\n", nDims);
            for (const auto &row : fetKeep) {
                for (int d = 0; d < nDims; ++d)
                    fprintf(f, "%lld%c", (long long)row[d],
                            d == nDims - 1 ? '\n' : ' ');
            }
            fclose(f);
        }
    }

    // .clu — text format
    if (!cluKeep.empty()) {
        FILE *f = fopen(cluPath.c_str(), "w");
        if (f) {
            fprintf(f, "%d\n", nClust);
            for (int32_t id : cluKeep)
                fprintf(f, "%d\n", id);
            fclose(f);
        }
    }

    return 0;
}
