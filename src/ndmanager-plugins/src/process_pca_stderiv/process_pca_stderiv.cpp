/***************************************************************************
 *  process_pca_stderiv.cpp
 *
 *  Transforms spike waveforms by applying spatial derivative + temporal
 *  first-difference before computing PCA features, writing the transformed
 *  waveforms to stdout for piping directly into process_pca.
 *
 *  Transformation per spike (nChan × wLen samples):
 *
 *    step 1:  sdiff[t, ch]   = neurosuite::core::spatialDeriv, order -d:
 *                              0 = none, 1 = first-diff, 2 = Laplacian,
 *                              3 = all-pairwise (default).  -d 4 is pass-through
 *                              (the input is already transformed), not core's
 *                              order 4.
 *
 *    step 2:  stderiv[t, ch] = sdiff[t, ch] - sdiff[t-1, ch]
 *                              boundary: sdiff[-1, ch] = 0 per spike
 *                              (waveforms begin at baseline)
 *
 *  Usage:
 *    process_pca_stderiv -n nChannels -w waveformSamples [-d order] [input.spk]
 *
 *  Reads from stdin if no input file is given.
 *  Writes transformed waveforms (same binary layout as .spk) to stdout.
 *  Pipe to process_pca:
 *    process_pca_stderiv -n N -w W session.spk.1 | process_pca [opts] -
 *
 *  Copyright (C) 2025 Gravios / NeuroSuite-3 contributors
 *  License: GPL v3+
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>

#include <neurosuite/core/custody.hpp>             // sdiffDropsLastChannel
#include <neurosuite/core/stderiv_transform.hpp>   // the one spatial derivative

static const char *VERSION = "process_pca_stderiv 1.0";

// ── Spatial derivative ───────────────────────────────────────────────────────
// -d 0..3 apply neurosuite::core::spatialDeriv (stderiv_transform.hpp), the one
// definition every tool shares, across the nChan channels of each time sample.
// -d 4 is NOT core's order 4 (a custom partner map): it means "pass" -- no
// transform, just read nChan and write nOutChan, dropping the dependent channel
// when the order applied AT EXTRACTION left one.  Use it when the input already
// holds the transformed waveform (ndm_pca, ndm_refeaturize).  That order is not
// recoverable from the data, so pass it with -D.
static const int kPassArg = 4;

// Whether the order leaves a dependent channel to drop: custody's rule
// (sdiffDropsLastChannel), shared with ndm_custody and ndm_resolve_io.
static inline bool dropsLastChannel(int o) { return neurosuite::custody::sdiffDropsLastChannel(o); }

static void usage(const char *name)
{
    std::cerr << VERSION << "\n"
              << "usage: " << name
              << " -n nChannels -w waveformSamples [-d order] [-D origOrder] [input.spk]\n"
              << "  -n   channels per spike (mandatory)\n"
              << "  -w   time-samples per channel per spike (mandatory)\n"
              << "  -d   spatial derivative order: 0=none 1=first-diff\n"
              << "       2=Laplacian 3=all-pairwise 4=pass-through (default 3)\n"
              << "  -k   keep the dependent channel instead of dropping it (the drop\n"
              << "       is free for orders 1 and 3, a convention for 4 and 5)\n"
              << "  -D   with -d 4, the order applied at extraction: 1=first-diff\n"
              << "       2=Laplacian 3=all-pairwise 4=custom pattern\n"
              << "       5=custom reference-set 6=custom expressions (default 3).\n"
              << "       Decides whether the\n"
              << "       linearly dependent channel is dropped.\n"
              << "  Reads from stdin if no input file given.\n"
              << "  Writes transformed waveforms to stdout.\n";
    std::exit(1);
}

int main(int argc, char *argv[])
{
    int nChan      = 0;
    int wLen       = 0;
    int orderArg   = 3;
    int origOrder  = -1;   // -D: extraction order, only meaningful with -d 4
    bool keepLast  = false; // -k: keep the dependent channel instead of dropping it
    std::string inFile;

    for(int i = 1; i < argc; i++) {
        if     (!strcmp(argv[i],"-n") && i+1<argc) nChan    = atoi(argv[++i]);
        else if(!strcmp(argv[i],"-w") && i+1<argc) wLen     = atoi(argv[++i]);
        else if(!strcmp(argv[i],"-d") && i+1<argc) orderArg = atoi(argv[++i]);
        else if(!strcmp(argv[i],"-D") && i+1<argc) origOrder = atoi(argv[++i]);
        else if(!strcmp(argv[i],"-k"))             keepLast = true;
        else if(argv[i][0] != '-') inFile = argv[i];
        else { std::cerr << "unknown option: " << argv[i] << "\n"; usage(argv[0]); }
    }
    if(nChan <= 0 || wLen <= 0) {
        std::cerr << "error: -n and -w are mandatory\n";
        usage(argv[0]);
    }

    // -d 4: input is already transformed; just do channel-reduction (-D says
    // whether the extraction order left a dependent channel).  Otherwise one of
    // core's plain orders 0..3; anything else falls back to all-pairs, as before.
    const bool passThrough = (orderArg == kPassArg);
    const neurosuite::core::SdiffOrder order = static_cast<neurosuite::core::SdiffOrder>(
        (orderArg >= 0 && orderArg <= 3) ? orderArg : 3);

    FILE *in = inFile.empty() ? stdin : fopen(inFile.c_str(), "rb");
    if(!in) {
        std::cerr << "error: cannot open '" << inFile << "'\n";
        return 1;
    }

    // ── .spk layout: [spike][t * nChan + ch] ──────────────────────────────
    // Same as process_pca rawData layout.
    const int spkPts = wLen * nChan;
    std::vector<short> rawSpike(static_cast<size_t>(spkPts));
    std::vector<short> sdSpike (static_cast<size_t>(spkPts));
    // Orders 1 and 3 produce a rank-deficient spatial derivative (one linear
    // dependency across channels). Drop the last channel before writing so
    // process_pca sees only the independent dimensions.
    //   order 1 (first-diff):   s[n-1] = -s[n-2]
    //   order 3 (all-pairwise): sum(s) = 0  →  s[n-1] = -sum(s[0..n-2])
    // Input already transformed (-d 4): the decision belongs to the order
    // used at EXTRACTION, supplied by -D.  Without it, assume all-pairs — the
    // historical default, so existing callers keep their behaviour.
    // -k suppresses the drop.  For orders 1 and 3 the last channel is a genuine
    // linear combination of the others, so keeping it adds a redundant column; for
    // orders 4 and 5 the pattern may well be full rank and the drop is a convention,
    // so -k is how a caller keeps that information.  The basis records the real
    // width, so downstream consumers follow whichever was used.
    const bool dropLast = keepLast ? false
        : passThrough
        ? dropsLastChannel(origOrder >= 0 ? origOrder : 3)
        : dropsLastChannel(static_cast<int>(order));
    const int  nOutChan = dropLast ? nChan - 1 : nChan;
    if(nOutChan < 1) {
        std::cerr << VERSION << " error: need >= 2 channels for spatial derivative\n";
        return 1;
    }

    std::vector<short> outSpike(static_cast<size_t>(spkPts));

    long nSpikes = 0;

    while(true) {
        size_t nr = fread(rawSpike.data(), sizeof(short),
                          static_cast<size_t>(spkPts), in);
        if(nr == 0) break;
        if((int)nr != spkPts) {
            std::cerr << VERSION << " warning: truncated spike (" << nr
                      << " of " << spkPts << " shorts)\n";
            break;
        }

        // -d 4: no transform, just channel-reduction.
        if(passThrough) {
            if(dropLast) {
                for(int t = 0; t < wLen; t++)
                    fwrite(rawSpike.data() + t * nChan, sizeof(short),
                           static_cast<size_t>(nOutChan), stdout);
            } else {
                fwrite(rawSpike.data(), sizeof(short),
                       static_cast<size_t>(spkPts), stdout);
            }
            ++nSpikes;
            continue;
        }

        // Step 1: spatial derivative — each time-sample t, across channels.
        for(int t = 0; t < wLen; t++) {
            const short *rawT = rawSpike.data() + t * nChan;
                  short *sdT  = sdSpike.data()  + t * nChan;
            for(int ci = 0; ci < nChan; ci++)
                sdT[ci] = neurosuite::core::clampToInt16(neurosuite::core::spatialDerivBy(
                    order, [rawT](int j) { return rawT[j]; }, ci, nChan));
        }

        // Step 2: temporal first-difference across samples per channel.
        // Boundary: sdiff[-1, ch] = 0 (pre-spike baseline).
        for(int t = 0; t < wLen; t++) {
            const short *sdCur  = sdSpike.data() + t * nChan;
            const short *sdPrev = (t > 0) ? sdSpike.data() + (t-1) * nChan : nullptr;
                  short *outT   = outSpike.data() + t * nChan;
            for(int ci = 0; ci < nChan; ci++) {
                int diff = (int)sdCur[ci] - (sdPrev ? (int)sdPrev[ci] : 0);
                if(diff >  32767) diff =  32767;
                if(diff < -32768) diff = -32768;
                outT[ci] = (short)diff;
            }
        }

        // Write nOutChan channels per time sample; drop last if it's redundant.
        if(dropLast) {
            for(int t = 0; t < wLen; t++)
                fwrite(outSpike.data() + t * nChan, sizeof(short),
                       static_cast<size_t>(nOutChan), stdout);
        } else {
            fwrite(outSpike.data(), sizeof(short),
                   static_cast<size_t>(spkPts), stdout);
        }
        ++nSpikes;
    }

    if(!inFile.empty()) fclose(in);

    // Summary line removed: it spammed the terminal mid-bar in the
    // ndm_pca_stderiv parallel-group flow (per-group log dumps cat'd
    // back from the wrapper script, racing the live progress bars on
    // /dev/tty).  The progress bars themselves are sufficient signal.
    // Re-enable for direct-CLI debugging only:
    //   std::cerr << VERSION << ": transformed " << nSpikes << " spikes"
    //             << " (" << nOutChan << "/" << nChan
    //             << " channels; sdiff order " << orderArg << " + temporal dt)\n";
    return 0;
}
