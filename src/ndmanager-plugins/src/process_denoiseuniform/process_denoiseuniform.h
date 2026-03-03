/***************************************************************************
    process_denoiseuniform.h
    ------------------------
    Shared types and declarations for process_denoiseuniform.

    copyright  (C) 2025 neurosuite-3 contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/

#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Runtime parameters (parsed from CLI)
// ---------------------------------------------------------------------------
struct DenoiseArgs {
    const char *basename        = nullptr;
    const char *group           = nullptr;
    int         nChannels       = 0;
    int         nSamples        = 0;
    int         resolution      = 16;

    double      uniformityThreshold = 0.30;
    double      minAmplitude        = 0.0;
    bool        dryRun              = false;
    bool        verbose             = false;
};

// ---------------------------------------------------------------------------
// Per-spike classification result
// ---------------------------------------------------------------------------
struct SpikeScore {
    double peak;    // max |sample| across all channels and samples (pre-DC)
    double score;   // spatial uniformity score in [0,1], or -1 if unscored
    bool   keep;    // true = genuine spike; false = noise event to remove
};

// ---------------------------------------------------------------------------
// Compute the spatial-uniformity score for a single spike waveform.
//
//   waveform  — int16 array of length nSamples*nChannels,
//               sample-major layout: element[s*nChannels + c]
//   peakOut   — set to max(|W|) across all elements (before DC removal)
//
// Returns the spatial-uniformity score in [0, 1].
// Returns -1.0 if the waveform is too flat to score (total RMS ≈ 0).
//
// Thread-safe: reads only from waveform, writes only to peakOut.
// ---------------------------------------------------------------------------
double uniformityScore(const int16_t *waveform,
                       int nSamples, int nChannels,
                       double &peakOut);

// ---------------------------------------------------------------------------
// Classify a batch of nSpikes waveforms.
//
//   waveforms  — flat buffer: spike i starts at waveforms[i*nSamples*nChannels]
//   results    — caller-allocated array of nSpikes SpikeScore structs
//
// OpenMP-parallelised across spikes when built with -fopenmp.
// ---------------------------------------------------------------------------
void classifySpikes(const int16_t  *waveforms,
                    int64_t         nSpikes,
                    int             nSamples,
                    int             nChannels,
                    double          uniformityThreshold,
                    double          minAmplitude,
                    SpikeScore     *results);
