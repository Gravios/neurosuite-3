// extractspikes_reextract.h — the -R pass of the three spike extractors:
// re-cut the waveforms at the times in <base>.res.<g> and write <base>.spk.<g>.
//
// The windows are cut by neurosuite::core::extractSpikeWindows
// (core/spike_extract.hpp), the same code Klusters re-extracts with, so the
// three tools and Klusters write the same bytes.  Each tool only says how a
// window is transformed (setSpec below): process_extractspikes copies it raw,
// process_extractspikes_sdiff takes the spatial derivative, and
// process_extractspikes_stderiv the spatial derivative and its temporal
// difference, seeded with the sample before the window.
//
// One .spk row is written per .res time, always.  A window that does not lie
// inside the .fil is written as zeros and counted; each tool used to skip it,
// which left the .spk shorter than the .res, so that every later waveform sat
// against the wrong spike (and .clu).
//
// With -N (NoiseOptions.windows > 0) each group also gets <base>.nst.<g>: the
// mean and covariance of random spike-free windows cut with the SAME spec as
// its .spk, outliers rejected (core/noise_stats.hpp) -- the noise model of
// exactly the waveforms just written, for the clusterers' whitener.
#ifndef EXTRACTSPIKES_REEXTRACT_H
#define EXTRACTSPIKES_REEXTRACT_H

#include "progressbar.h"                      // BlockProgress
#include <neurosuite/core/noise_stats.hpp>
#include <neurosuite/core/spike_extract.hpp>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace extractspikes {

// -N / -E / -G: the per-group noise statistics written beside the .spk.
struct NoiseOptions {
    long long windows = 0;     // -N: spike-free windows to draw per group; 0 = no .nst
    double    zMax    = 5.0;   // -E: rejection gate, robust SDs (amplitude and window energy)
    int       guard   = -1;    // -G: extra clearance around each spike window; -1 = nSamples
};

// Parse one of -N/-E/-G at argv[i] (advancing i past its value).  Returns false
// when the flag is not one of them; exits with a message on a missing or bad value.
inline bool parseNoiseFlag(int& i, int argc, char** argv, NoiseOptions& o)
{
    const char f = argv[i][1];
    if(f != 'N' && f != 'E' && f != 'G') return false;
    if(i + 1 >= argc) {
        std::fprintf(stderr, "error: -%c needs a value.\n", f);
        std::exit(1);
    }
    const char* v = argv[++i];
    char* end = nullptr;
    if(f == 'E') {
        o.zMax = std::strtod(v, &end);
        if(end == v || *end || !(o.zMax > 0.0)) {
            std::fprintf(stderr, "error: -E must be a number > 0 (got '%s').\n", v);
            std::exit(1);
        }
    } else {
        const long long n = std::strtoll(v, &end, 10);
        if(end == v || *end || n < (f == 'G' ? -1 : 0) || (f == 'G' && n > 1000000)) {
            std::fprintf(stderr, "error: -%c must be an integer %s (got '%s').\n", f,
                         f == 'G' ? ">= -1" : ">= 0", v);
            std::exit(1);
        }
        if(f == 'N') o.windows = n; else o.guard = static_cast<int>(n);
    }
    return true;
}

// Help lines for -N/-E/-G, in the engines' help-text style.
inline const char* noiseHelp()
{
    return
        "Noise statistics (with -R):\n"
        "  -N count        also write basename.nst.<grp>: mean + covariance of\n"
        "                  `count` random spike-free windows cut exactly like the\n"
        "                  .spk (same transform), outliers rejected.  0 = off\n"
        "                  (default).\n"
        "  -E zMax         rejection gate in robust SDs: a window goes when any\n"
        "                  sample, or its energy, is beyond zMax (default 5)\n"
        "  -G guard        clearance in samples added around each spike window\n"
        "                  (default: the waveform length)\n\n";
}

// Re-extract every group.  Args is the tool's `arguments` (inputFileName,
// outputBaseFileName, totalChannelNumber, spikeLength, timeBeforeSpike are
// read).  setSpec(group, spec) fills the transform fields of the group's
// SpikeWindowSpec; its geometry is already set.  With noise.windows > 0 the
// group's .nst is written too.  Returns the number of errors (0 = success).
template <class Args, class SetSpec>
int reextractFromRes(const Args& args, int** channelList, const int* channelNb_group,
                     int nbGroups, bool verbose, SetSpec setSpec,
                     const NoiseOptions& noise = NoiseOptions{})
{
    std::FILE* fil = std::fopen(args.inputFileName, "rb");
    if(!fil) {
        std::fprintf(stderr, "[-R] cannot open '%s' for reading: %s\n",
                     args.inputFileName, std::strerror(errno));
        return 1;
    }
    if(verbose) {
        std::cout << "[-R] re-extract mode active" << std::endl;
        std::cout << "[-R]   .fil          = " << args.inputFileName
                  << " (" << args.totalChannelNumber << " ch)" << std::endl;
        std::cout << "[-R]   output stem   = " << args.outputBaseFileName << std::endl;
        std::cout << "[-R]   spikeLength   = " << args.spikeLength
                  << ", peakSampleIndex (timeBefore+1) = " << (args.timeBeforeSpike + 1)
                  << std::endl;
    }

    int errors = 0;
    int64_t totalSpikes = 0, totalZero = 0;
    BlockProgress prog(args.outputBaseFileName);

    for(int grp = 0; grp < nbGroups; grp++) {
        const int nCG = channelNb_group[grp];
        if(nCG <= 0) continue;

        std::ostringstream resPath, spkPath;
        resPath << args.outputBaseFileName << ".res." << (grp + 1);
        spkPath << args.outputBaseFileName << ".spk." << (grp + 1);

        std::FILE* resFp = std::fopen(resPath.str().c_str(), "rb");
        if(!resFp) {
            std::fprintf(stderr, "[-R] cannot open '%s' for reading: %s\n",
                         resPath.str().c_str(), std::strerror(errno));
            errors++;
            continue;
        }
        std::vector<int64_t> resTs;
        {
            int64_t t;
            while(std::fread(&t, sizeof t, 1, resFp) == 1) resTs.push_back(t);
        }
        std::fclose(resFp);

        std::FILE* spkFp = std::fopen(spkPath.str().c_str(), "wb");
        if(!spkFp) {
            std::fprintf(stderr, "[-R] cannot open '%s' for writing: %s\n",
                         spkPath.str().c_str(), std::strerror(errno));
            errors++;
            continue;
        }

        neurosuite::core::SpikeWindowSpec spec;
        spec.nSamp           = args.spikeLength;
        spec.peakSample      = args.timeBeforeSpike;
        spec.totalNbChannels = args.totalChannelNumber;
        spec.groupChannels   = channelList[grp];
        spec.nChan           = nCG;
        setSpec(grp, spec);

        const std::size_t rowElems = (std::size_t)args.spikeLength * (std::size_t)nCG;
        bool writeFailed = false;
        int64_t zeroRows = 0;
        prog.beginStage("G" + std::to_string(grp + 1), (long long)resTs.size());
        const int64_t n = neurosuite::core::extractSpikeWindows(
            fil, (int64_t)resTs.size(), spec,
            [&](int64_t i) { return resTs[(std::size_t)i]; },
            [&](int64_t i, const int16_t* row) {
                prog.setPosition((long long)i);
                if(!writeFailed && std::fwrite(row, sizeof(int16_t), rowElems, spkFp) != rowElems)
                    writeFailed = true;
            },
            &zeroRows);
        prog.endStage();
        if(std::fclose(spkFp) != 0) writeFailed = true;

        if(n < 0) {
            std::fprintf(stderr, "[-R] cannot read '%s'\n", args.inputFileName);
            errors++;
            continue;
        }
        if(writeFailed) {
            std::fprintf(stderr, "[-R] short write on '%s'\n", spkPath.str().c_str());
            errors++;
        }
        if(zeroRows > 0)
            std::fprintf(stderr, "[-R] warning: group %d: %lld of %lld windows are not inside "
                         "the .fil; written as zeros\n",
                         grp + 1, (long long)zeroRows, (long long)resTs.size());
        totalSpikes += n;
        totalZero   += zeroRows;
        if(verbose)
            std::cout << "[-R] group " << (grp + 1) << ": " << resTs.size()
                      << " timestamps -> wrote " << n << " spikes to " << spkPath.str()
                      << " (" << zeroRows << " outside the .fil, as zeros)" << std::endl;

        if(noise.windows > 0) {
            std::ostringstream nstPath;
            nstPath << args.outputBaseFileName << ".nst." << (grp + 1);
            neurosuite::core::NoiseStatsParams prm;
            prm.nWindows = noise.windows;
            prm.zMax     = noise.zMax;
            prm.guard    = noise.guard;
            prm.seed     = static_cast<std::uint64_t>(grp + 1);
            neurosuite::core::NoiseStats st;
            if(!neurosuite::core::estimateNoiseStats(fil, spec, resTs, prm, st)) {
                std::fprintf(stderr, "[-R] group %d: no noise statistics (%lld windows drawn, "
                             "%lld kept)\n", grp + 1, (long long)st.nDrawn, (long long)st.nKept);
                errors++;
            } else if(!neurosuite::core::writeNoiseStats(nstPath.str(), st)) {
                std::fprintf(stderr, "[-R] cannot write '%s'\n", nstPath.str().c_str());
                errors++;
            } else {
                if(st.nDrawn < noise.windows)
                    std::fprintf(stderr, "[-R] warning: group %d: only %lld of %lld noise windows "
                                 "clear of spikes\n", grp + 1, (long long)st.nDrawn,
                                 (long long)noise.windows);
                if(verbose)
                    std::cout << "[-R] group " << (grp + 1) << ": noise " << st.nDrawn
                              << " windows, " << st.nKept << " kept (" << st.nRejectedFlat
                              << " flat, " << st.nRejectedOutlier << " outliers) -> "
                              << nstPath.str() << std::endl;
            }
        }
    }

    prog.finish();
    std::fclose(fil);
    if(verbose)
        std::cout << "[-R] DONE. Total: " << totalSpikes << " spikes written, "
                  << totalZero << " as zeros, " << errors << " errors." << std::endl;
    return errors;
}

} // namespace extractspikes

#endif // EXTRACTSPIKES_REEXTRACT_H
