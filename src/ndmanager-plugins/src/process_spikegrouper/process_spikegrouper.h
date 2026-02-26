/***************************************************************************
    process_spikegrouper.h
    ----------------------
    Shared types, constants, and declarations for process_spikegrouper.

    copyright  (C) 2025 neurosuite-3 contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/

#pragma once

#include <vector>
#include <string>
#include <set>
#include <cstdint>

// ---------------------------------------------------------------------------
// Runtime parameters (parsed from CLI)
// ---------------------------------------------------------------------------
struct SpikeGrouperArgs {
    std::string filPath;
    std::string yamlPath;

    int         nChannels       = 0;
    int         nBits           = 16;
    double      samplingRate    = 32000.0;

    double      thresholdFactor = 3.0;    // multiplier on sigma_n
    double      refractoryMs    = 1.0;    // minimum inter-event interval (ms)
    double      coincidenceMs   = 0.4;    // half-width of coincidence window (ms)
    double      windowSec       = 60.0;   // seconds of .fil to analyse
    int         maxSubGroups    = 16;     // maximum k for clustering (silhouette sweep upper bound)
    int         minChannels     = 4;      // minimum channels per sub-group (post-merge)
    int         maxMergedSize   = 12;    // maximum channels in a merged group
    int         channelOverlap  = 0;     // channels borrowed from each neighbour group
    std::set<int> excludeChannels;        // channels excluded from all groups (user-supplied)

    // Values written into new spikeDetection groups
    int         nSamples        = 52;
    int         peakSampleIndex = 26;
    int         nFeatures       = 3;

    bool        verbose         = false;
    bool        forceCPU        = false;
};

// ---------------------------------------------------------------------------
// One existing spikeDetection group as read from the YAML
// ---------------------------------------------------------------------------
struct ChannelGroup {
    std::vector<int> channels;
    int              nSamples        = 52;
    int              peakSampleIndex = 26;
    int              nFeatures       = 3;
};

// ---------------------------------------------------------------------------
// CUDA path: compute per-channel sigma_n and coincidence matrix
// for one group.
//
// sigmas[i]  = median(|data[:, channels[i]]|) / 0.6745
// coinc[i*n + j] = (# events on ch-i co-incident with ch-j) / (# events on ch-i + 1)
// (symmetrised by caller)
// ---------------------------------------------------------------------------
#ifdef USE_CUDA
// Returns the number of CUDA devices (0 if none). Defined in the .cu file so
// the .cpp translation unit never needs to include cuda_runtime.h directly.
extern "C" int  cudaHasDevice();
extern "C" void runCudaSpikeGrouper(
    const short*        hostData,          // (nSamples, nChannels) interleaved
    long int            nSamplesTotal,     // nSamples * nChannels
    long int            nSamplesPerCh,     // nSamples
    int                 nChannelsTotal,    // total channels in .fil
    const int*          groupChannels,     // channel indices for this group
    int                 groupSize,         // number of channels in group
    double              thresholdFactor,
    int                 refractorySamples,
    int                 coincidenceSamples,
    double*             sigmasOut,         // [groupSize] output
    double*             coincOut,          // [groupSize * groupSize] output
    bool                verbose
);
#endif
