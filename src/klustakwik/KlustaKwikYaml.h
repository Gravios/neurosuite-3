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

struct KKYamlSpikeParams {
    int    nbChannels    = 0;     ///< 0 = not found in YAML
    int    nbSamples     = 0;     ///< 0 = not found in YAML
    double samplingRate  = 0.0;   ///< 0.0 = not found in YAML
    int    nBits         = 0;     ///< acquisitionSystem.nBits (diagnostic only)
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
