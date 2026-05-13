/***************************************************************************
 *  process_pca_multichannel.h
 *
 *  Multi-channel PCA for spike feature extraction, with probe-geometry
 *  awareness.  Companion to process_pca (per-channel PCA) and
 *  process_pca_stderiv (stderiv preprocessing).
 *
 *  Per-channel PCA treats each channel's nSamp-dimensional time-sample
 *  space independently — it ignores the fact that a real action potential
 *  appears as correlated signal across nearby channels.  Multi-channel
 *  PCA vectorises the whole spike (nChan × nSamp samples → one
 *  (nChan*nSamp)-dim vector) and learns a joint basis, capturing the
 *  spatial-temporal structure that per-channel PCA throws away.
 *
 *  Probe geometry enters in two ways:
 *
 *   1. The .pcaM.N output records the per-channel (x, y) positions used,
 *      so downstream tools (Klusters, custom viewers) can map feature
 *      directions back to physical space.
 *
 *   2. Optional spatial weighting (--spatial-weight RADIUS) down-weights
 *      covariance entries between channel pairs whose separation exceeds
 *      RADIUS.  For Neuropixels with large groups, this focuses PCA on
 *      spike-bearing channels rather than wasting components on far-noise
 *      correlations.
 *
 *  Built-in probe templates (selected via --probe-template NAME):
 *
 *    tetrode             4 channels in a square at 25 μm pitch
 *    octrode-linear      8 channels in a vertical line at 20 μm pitch
 *                          (Buzsaki silicon shank convention)
 *    octrode-2x4         8 channels in 2 cols × 4 rows, 20 μm horiz × 25 μm vert
 *    neuropixels1-32     32-channel slice of Neuropixels 1.0: 4 cols ×
 *                          8 rows, 16 μm horiz × 25 μm vert, checkerboard
 *    neuropixels1-NN     any nChan slice of NP1.0 (auto-derived from row count)
 *    neuropixels2-NN     any nChan slice of NP2.0: 2 cols, 15 μm vert × 32 μm horiz
 *    flat-linear-NNum    N channels in a vertical line at NN μm pitch
 *                          (fallback when no specific template matches)
 *
 *  Override: --channel-positions FILE (2-column "x y" in μm, one row per
 *  channel) takes precedence over any built-in template.
 *
 *  Auto-selection: when neither --probe-template nor --channel-positions
 *  is given, the tool picks a sensible default from --nChannels:
 *    nChan=4   → tetrode
 *    nChan=8   → octrode-linear
 *    nChan=16  → flat-linear-20um
 *    nChan=32  → neuropixels1-32
 *    nChan=64+ → neuropixels1-NN (NP1.0 slice)
 *
 *  Output files:
 *
 *    .fetM.N    int32 nFeatures, then nSpikes × nFeatures int64 features.
 *               Single-block layout (NOT per-channel) — Klusters needs to
 *               interpret as 1 virtual channel × nFeatures components.
 *
 *    .pcaM.N    binary, little-endian:
 *               int32 magic ("PCAM" = 0x4d414350)
 *               int32 version (1)
 *               int32 nChan, nSamp, nComp, centered, recShift
 *               nChan × double posX (μm)
 *               nChan × double posY (μm)
 *               (nChan*nSamp) × double  mean vector
 *               (nChan*nSamp) × nComp × double  eigenvectors (col-major)
 *               nComp × double  eigenvalues (top-K, descending)
 *
 *  Copyright (C) 2026 Gravios / NeuroSuite-3 contributors
 *  License: GPL v3+
 ***************************************************************************/
#ifndef __PROCESS_PCA_MULTICHANNEL_H
#define __PROCESS_PCA_MULTICHANNEL_H

#include <iostream>
#include <string>
#include <vector>

const unsigned long MAX_INPUT_SIZE = 2560000000;
const int RECORD_BYTE_SIZE = 2;

const int32_t PCAM_MAGIC   = 0x4d414350;   // "PCAM" little-endian
const int32_t PCAM_VERSION = 1;

// Sentinel for built-in templates so we don't sprinkle string-compare logic
// everywhere.  Strings sit on the CLI; this enum is the internal type after
// parsing + auto-selection.
enum class ProbeTemplate {
    USER_FILE,           // --channel-positions FILE was given
    TETRODE,             // 4-channel square
    OCTRODE_LINEAR,      // 8 in a line at 20 μm
    OCTRODE_2X4,         // 8 in 2×4 grid
    NEUROPIXELS1,        // NP1.0 (4-column staggered)
    NEUROPIXELS2,        // NP2.0 (2-column)
    FLAT_LINEAR,         // N in a line at user-specified pitch
    AUTO                 // pick from nChannels at run time
};

struct Arguments {
    char *inputFileName = nullptr;
    char *outputFileName = nullptr;
    long long inputSize = 0;
    int nChannels = 0;
    int beforeSpike = -1;
    int afterSpike = -1;
    int peakPosition = -1;
    int spikeLength = -1;
    int nComponents = 0;
    bool isCenteredData = false;
    int offset = 0;

    bool isInputFileProvided = false;
    bool isOutputFileProvided = false;
    bool isInputSizeProvided = false;
    bool isNChannelsProvided = false;
    bool isBeforeSpikeProvided = false;
    bool isAfterSpikeProvided = false;
    bool isPeakPositionProvided = false;
    bool isSpikeLengthProvided = false;
    bool isNComponentsProvided = false;
    bool isOffsetProvided = false;

    int electrodeGroup = -1;

    // ── Probe geometry ──
    ProbeTemplate probe = ProbeTemplate::AUTO;
    std::string   probeName;          // raw string from --probe-template, for diagnostics
    std::string   channelPositionsFile;
    double        flatLinearPitch = 20.0;  // μm, when probe is FLAT_LINEAR

    // ── Spatial covariance weighting ──
    // 0 = unweighted (treat all channel-pairs equally — pure multi-channel PCA).
    // > 0 = down-weight covariance entries between channels separated by more
    //       than 2×radius via a Gaussian: w_ij = exp(-d_ij² / (2 radius²)).
    //       Focuses PCA on spike-bearing channels for large groups.
    double spatialWeightRadius = 0.0;

    // ── Varimax rotation (mirrors process_pca's patch52 feature) ──
    bool   varimax = false;
    int    varimaxMaxIter = 30;
    double varimaxTol = 1e-6;
};

// CLI
void parseArgs(int argc, char **argv, Arguments &out);
bool checkInputs(const Arguments &args);

#endif
