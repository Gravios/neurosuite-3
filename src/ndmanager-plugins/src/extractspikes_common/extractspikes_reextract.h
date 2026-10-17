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
#ifndef EXTRACTSPIKES_REEXTRACT_H
#define EXTRACTSPIKES_REEXTRACT_H

#include "progressbar.h"                      // BlockProgress
#include <neurosuite/core/spike_extract.hpp>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace extractspikes {

// Re-extract every group.  Args is the tool's `arguments` (inputFileName,
// outputBaseFileName, totalChannelNumber, spikeLength, timeBeforeSpike are
// read).  setSpec(group, spec) fills the transform fields of the group's
// SpikeWindowSpec; its geometry is already set.  Returns the number of
// errors (0 = success).
template <class Args, class SetSpec>
int reextractFromRes(const Args& args, int** channelList, const int* channelNb_group,
                     int nbGroups, bool verbose, SetSpec setSpec)
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
