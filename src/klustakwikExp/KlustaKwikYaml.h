/***************************************************************************
 * KlustaKwikYaml.h
 *
 * Lightweight yaml-cpp reader for KlustaKwik — no Qt dependency.
 *
 * Reads the three spike-group parameters that KlustaKwik needs for
 * Phase 1.5 waveform realignment directly from the neurosuite YAML
 * parameter file (<FileBase>.yaml or <FileBase>.yml):
 *
 *   NbChannels       ← spikeDetection.channelGroups[ElecNo-1].channels.channel[].size()
 *                       (ndmanager YAML schema stores channel IDs under
 *                        channels.channel as a sequence; a flat channels: []
 *                        list is also accepted for hand-authored files)
 *   NbSamplesPerSpike← spikeDetection.channelGroups[ElecNo-1].nSamples
 *   SamplingRate     ← acquisitionSystem.samplingRate
 *
 * Data-type note
 * --------------
 * NbBytesPerSample is NOT read from the YAML because it is invariant:
 * the entire ndmanager pipeline (process_medianfilter → ndm_hipass →
 * process_extractspikes / process_extractspikes_sdiff) always reads and
 * writes int16 (sizeof(short) = 2).  The YAML field
 * acquisitionSystem.nBits captures only the ADC precision (12/14/16
 * meaningful bits); the storage container is always 2 bytes per sample.
 * We read nBits solely to emit a diagnostic when the value is unusual.
 *
 * Copyright (C) 2026  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once
#include <array>
#include <string>
#include <vector>

struct KKYamlSpikeParams {
    int    nbChannels      = 0;   ///< spike group channel count (matches .spk layout)
    int    nbSamples       = 0;   ///< samples per spike window
    int    peakSampleIndex = 0;   ///< 0-based index of the spike peak within the window
    int    nTotalChannels  = 0;   ///< acquisitionSystem.nChannels (total in .fil file)
    double samplingRate    = 0.0; ///< 0.0 = not found in YAML
    int    nBits           = 0;   ///< acquisitionSystem.nBits (diagnostic only)
    std::vector<int> channelIds;  ///< 0-based channel indices for this group in the .fil file
    // Probe geometry — used by inline drift estimation
    int    probeId       = -1;    ///< -1 = not present in YAML
    int    shankIndex    = 0;     ///< 0-based shank index on the probe
    std::string probeFile;        ///< probeFile path from probes: list (empty = not set)
    std::string probeLibraryPath; ///< optional override from YAML probeLibraryPath
    // Inline electrode site positions from sitePositions_um.
    // Each entry is {x_um, y_um}.  Empty when not present in YAML.
    std::vector<std::array<float,2>> sitePositions; ///< [x_um, y_um] per site
    bool   valid         = false; ///< true if YAML was parsed successfully
};

/**
 * @brief Parse <fileBase>.yaml (or .yml) and return spike-group parameters
 *        for electrode group @p elecNo (1-based).
 *
 * Returns a struct with valid=false and all zeros on any error (file not
 * found, parse failure, or group index out of range).  The caller can
 * still proceed with command-line defaults in that case.
 */
KKYamlSpikeParams kkReadYamlSpikeParams(const char* fileBase, int elecNo);
