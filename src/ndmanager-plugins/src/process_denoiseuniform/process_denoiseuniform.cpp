/***************************************************************************
                     process_denoiseuniform.cpp
                     --------------------------
    copyright            : (C) 2025 neurosuite-3 contributors

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/**
 * @file process_denoiseuniform.cpp
 * @brief Remove electrically uniform noise events from .res and .spk files.
 *
 * Background
 * ----------
 * Electrical noise artifacts (60/50 Hz pickup, movement transients, cable
 * shorts, ground spikes) often manifest as waveforms that look nearly
 * identical on every channel within a tetrode or shank group.  Genuine
 * single-unit spikes always show a spatially varying pattern because the
 * neuron is located asymmetrically with respect to the recording sites.
 *
 * Detection strategy
 * ------------------
 * For each spike waveform W[s,c]  (s = sample index, c = channel index)
 * we compute a *spatial uniformity score*:
 *
 *   1. Remove DC per channel:  W_dc[s,c] = W[s,c] - mean_s(W[·,c])
 *   2. For every sample time s compute the std across channels:
 *          spatial_std[s] = std_c( W_dc[s,·] )
 *   3. mean_spatial_std  = mean_s( spatial_std[s] )
 *   4. total_rms         = sqrt( mean_{s,c}( W_dc[s,c]² ) )
 *   5. uniformity_score  = mean_spatial_std / total_rms
 *
 * Score near 0: channels carry the same waveform → noise event.
 * Score near 1: channels are maximally diverse → genuine spike.
 * Spikes with score < uniformity_threshold are removed.
 * Spikes with peak amplitude < min_amplitude are retained unscored.
 *
 * Parallelism
 * -----------
 * Classification is embarrassingly parallel across spikes — each spike is
 * independent.  When built with OpenMP (-fopenmp), the classify pass uses
 * omp parallel for, dispatching spikes across all available cores.  The
 * bottleneck is file I/O (reading the .spk file); parallelising the score
 * computation eliminates any CPU overhead on top of that.
 *
 * On a typical 4-core laptop with 100K spikes × 4 channels × 32 samples,
 * the classify pass takes < 50 ms regardless — the bottleneck is always
 * disk bandwidth.  OpenMP matters most for large silicon-probe sessions
 * (512 channels, 1M+ spikes) where the score computation can otherwise
 * reach several seconds.
 *
 * GPU note
 * --------
 * A CUDA kernel is not provided and would not be beneficial here.
 * The arithmetic intensity of the score computation is ≈1.5 FLOP/byte —
 * far below the ≈20 FLOP/byte threshold needed to be compute-bound on
 * any GPU.  The workload is memory-bandwidth limited, and PCIe transfer
 * of the full .spk buffer to device costs more than the CPU computation
 * it would replace.  OpenMP across host cores is the correct accelerator
 * for this pattern.
 *
 * File formats (binary, little-endian, no header)
 * ------------------------------------------------
 *   .res.N — N_spikes × int64_t timestamps (sample index)
 *   .spk.N — N_spikes × (nSamples × nChannels) × int16_t waveforms;
 *             layout: spike i, sample s, channel c →
 *             byte offset (i*nSamples*nChannels + s*nChannels + c) × 2
 *   .clu.N — optional text: first line = nClusters, then one int per spike
 *   .fet.N — optional binary: int32_t nDimensions header, then
 *             N_spikes × nDimensions × int64_t features (row-major)
 *
 * Usage
 * -----
 *   process_denoiseuniform  basename  group  nChannels  nSamples  resolution
 *       [--uniformity-threshold T]   (default 0.30)
 *       [--min-amplitude A]          (default 0 — disabled)
 *       [--dry-run]
 *       [--verbose]
 *       [--cpu]                      (no-op; reserved for future GPU path)
 */

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include "process_denoiseuniform.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

#ifdef _OPENMP
#  include <omp.h>
#endif

using namespace std;

// ---------------------------------------------------------------------------
// Uniformity score for a single waveform — thread-safe, no heap allocation
// ---------------------------------------------------------------------------
double uniformityScore(const int16_t *waveform,
                       int nSamples, int nChannels,
                       double &peakOut)
{
    // --- peak amplitude (raw, before DC removal) ---
    double peak = 0.0;
    for (int i = 0; i < nSamples * nChannels; ++i) {
        double v = std::abs((double)waveform[i]);
        if (v > peak) peak = v;
    }
    peakOut = peak;

    // --- per-channel DC (mean over samples) ---
    // Use a small stack array; nChannels is at most a few dozen.
    double dc[256] = {};           // zero-initialised
    for (int s = 0; s < nSamples; ++s)
        for (int c = 0; c < nChannels; ++c)
            dc[c] += (double)waveform[s * nChannels + c];
    for (int c = 0; c < nChannels; ++c)
        dc[c] /= nSamples;

    // --- total RMS (all channels, all samples, DC-removed) ---
    double sum_sq = 0.0;
    for (int s = 0; s < nSamples; ++s)
        for (int c = 0; c < nChannels; ++c) {
            double v = (double)waveform[s * nChannels + c] - dc[c];
            sum_sq += v * v;
        }
    double total_rms = sqrt(sum_sq / (nSamples * nChannels));

    if (total_rms < 0.5)          // essentially zero signal — can't score
        return -1.0;

    // --- mean spatial std across channels, averaged over sample times ---
    double mean_spatial_std = 0.0;
    for (int s = 0; s < nSamples; ++s) {
        // mean across channels for this sample (DC already removed)
        double mean_ch = 0.0;
        for (int c = 0; c < nChannels; ++c)
            mean_ch += (double)waveform[s * nChannels + c] - dc[c];
        mean_ch /= nChannels;

        // std across channels
        double ssq = 0.0;
        for (int c = 0; c < nChannels; ++c) {
            double v = ((double)waveform[s * nChannels + c] - dc[c]) - mean_ch;
            ssq += v * v;
        }
        mean_spatial_std += sqrt(ssq / nChannels);
    }
    mean_spatial_std /= nSamples;

    return mean_spatial_std / total_rms;
}

// ---------------------------------------------------------------------------
// Classify a batch of spikes — OpenMP-parallelised across spikes
// ---------------------------------------------------------------------------
void classifySpikes(const int16_t  *waveforms,
                    int64_t         nSpikes,
                    int             nSamples,
                    int             nChannels,
                    double          uniformityThreshold,
                    double          minAmplitude,
                    SpikeScore     *results)
{
    const int samplesPerSpike = nSamples * nChannels;

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 256)
#endif
    for (int64_t i = 0; i < nSpikes; ++i) {
        const int16_t *wav = waveforms + i * samplesPerSpike;
        double peak  = 0.0;
        double score = uniformityScore(wav, nSamples, nChannels, peak);

        bool keep;
        if (score < 0.0 || peak < minAmplitude) {
            // too quiet or too flat — retain unconditionally
            keep  = true;
            score = -1.0;       // sentinel: unscored
        } else {
            keep = (score >= uniformityThreshold);
        }

        results[i] = {peak, score, keep};
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void usage(const char *prog)
{
    fprintf(stderr,
        "\nusage: %s basename group nChannels nSamples resolution [options]\n"
        "\n"
        "  basename                  session basename (no extension)\n"
        "  group                     electrode group number\n"
        "  nChannels                 channels in this group\n"
        "  nSamples                  samples per waveform\n"
        "  resolution                ADC resolution in bits (e.g. 16)\n"
        "\n"
        "  --uniformity-threshold T  score below which a spike is noise  (default 0.30)\n"
        "  --min-amplitude A         min peak ADC count to attempt scoring (default 0)\n"
        "  --dry-run                 report removed spikes but keep files intact\n"
        "  --verbose                 print per-spike scores\n"
        "  --cpu                     no-op; reserved for future GPU path\n"
        "\n"
#ifdef _OPENMP
        "  Built with OpenMP — spike classification parallelised across %d threads.\n"
        "\n",
        prog, omp_get_max_threads()
#else
        "  Built without OpenMP — single-threaded classification.\n"
        "\n",
        prog
#endif
    );
}

static FILE *xfopen(const string &path, const char *mode)
{
    FILE *f = fopen(path.c_str(), mode);
    if (!f) {
        fprintf(stderr, "error: cannot open '%s' (mode=%s)\n",
                path.c_str(), mode);
        exit(EXIT_FAILURE);
    }
    return f;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc < 6) {
        fprintf(stderr,
                "error: too few arguments (type '%s -h' for help)\n", argv[0]);
        return EXIT_FAILURE;
    }

    // ---- positional args ----
    string basename   = argv[1];
    string group      = argv[2];
    int    nChannels  = atoi(argv[3]);
    int    nSamples   = atoi(argv[4]);
    int    resolution = atoi(argv[5]);

    // ---- options ----
    double uniformityThreshold = 0.30;
    double minAmplitude        = 0.0;
    bool   dryRun              = false;
    bool   verbose             = false;

    for (int i = 6; i < argc; ++i) {
        string arg = argv[i];
        if ((arg == "--uniformity-threshold" || arg == "-u") && i + 1 < argc)
            uniformityThreshold = atof(argv[++i]);
        else if ((arg == "--min-amplitude" || arg == "-a") && i + 1 < argc)
            minAmplitude = atof(argv[++i]);
        else if (arg == "--dry-run")  dryRun  = true;
        else if (arg == "--verbose")  verbose = true;
        else if (arg == "--cpu")      { /* reserved, no-op */ }
        else {
            fprintf(stderr, "error: unknown argument '%s'\n", arg.c_str());
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // ---- validate ----
    if (nChannels < 2) {
        fprintf(stderr,
                "error: need at least 2 channels for spatial uniformity "
                "(got %d)\n", nChannels);
        return EXIT_FAILURE;
    }
    if (resolution != 16) {
        fprintf(stderr,
                "error: only 16-bit resolution is supported (got %d bits)\n",
                resolution);
        return EXIT_FAILURE;
    }

    // ---- file names ----
    string resPath = basename + ".res." + group;
    string spkPath = basename + ".spk." + group;
    string cluPath = basename + ".clu." + group;
    string fetPath = basename + ".fet." + group;

    string resTmp  = basename + ".res." + group + ".denoise_tmp";
    string spkTmp  = basename + ".spk." + group + ".denoise_tmp";
    string cluTmp  = basename + ".clu." + group + ".denoise_tmp";
    string fetTmp  = basename + ".fet." + group + ".denoise_tmp";

    // ---- measure .res to determine nSpikes ----
    FILE *resIn = xfopen(resPath, "rb");
    fseeko(resIn, 0, SEEK_END);
    off_t resBytes = ftello(resIn);
    fseeko(resIn, 0, SEEK_SET);

    if (resBytes % (off_t)sizeof(int64_t) != 0) {
        fprintf(stderr,
                "error: .res file size %lld is not a multiple of 8 bytes\n",
                (long long)resBytes);
        fclose(resIn);
        return EXIT_FAILURE;
    }
    int64_t nSpikes = resBytes / (int64_t)sizeof(int64_t);

    // ---- validate .spk size ----
    FILE *spkIn = xfopen(spkPath, "rb");
    fseeko(spkIn, 0, SEEK_END);
    off_t spkBytes    = ftello(spkIn);
    fseeko(spkIn, 0, SEEK_SET);
    int64_t expected  = nSpikes * (int64_t)nSamples * nChannels * sizeof(int16_t);

    if (spkBytes != expected) {
        fprintf(stderr,
                "error: .spk file size %lld does not match expected %lld bytes "
                "(%lld spikes × %d samples × %d channels × 2)\n",
                (long long)spkBytes, (long long)expected,
                (long long)nSpikes, nSamples, nChannels);
        fclose(resIn); fclose(spkIn);
        return EXIT_FAILURE;
    }

    // ---- optional: clu ----
    bool hasClu      = false;
    int  cluHeader   = -1;
    FILE *cluIn      = fopen(cluPath.c_str(), "r");
    if (cluIn && fscanf(cluIn, "%d", &cluHeader) == 1)
        hasClu = true;
    else if (cluIn) { fclose(cluIn); cluIn = nullptr; }

    // ---- optional: fet ----
    bool    hasFet  = false;
    int32_t fetNDim = 0;
    FILE   *fetIn   = fopen(fetPath.c_str(), "rb");
    if (fetIn) {
        if (fread(&fetNDim, sizeof(int32_t), 1, fetIn) == 1) {
            off_t expFet = (off_t)sizeof(int32_t) +
                           (off_t)nSpikes * fetNDim * sizeof(int64_t);
            fseeko(fetIn, 0, SEEK_END);
            if (ftello(fetIn) == expFet) {
                hasFet = true;
                fseeko(fetIn, sizeof(int32_t), SEEK_SET);
            } else {
                fprintf(stderr,
                        "warning: .fet file size mismatch — skipping .fet\n");
                fclose(fetIn); fetIn = nullptr;
            }
        } else { fclose(fetIn); fetIn = nullptr; }
    }

    // ---- read all spikes into RAM for parallel scoring ----
    const int64_t samplesPerSpike = (int64_t)nSamples * nChannels;

    printf("process_denoiseuniform: group %s  %lld spikes"
           "  nChannels=%d  nSamples=%d  threshold=%.3f%s%s\n",
           group.c_str(), (long long)nSpikes, nChannels, nSamples,
           uniformityThreshold,
           dryRun  ? "  [dry-run]" : "",
#ifdef _OPENMP
           "  [OpenMP]"
#else
           ""
#endif
           );

    vector<int16_t> allWaveforms((size_t)(nSpikes * samplesPerSpike));
    if ((int64_t)fread(allWaveforms.data(), sizeof(int16_t),
                       (size_t)(nSpikes * samplesPerSpike), spkIn)
        != nSpikes * samplesPerSpike) {
        fprintf(stderr, "error: short read of .spk file\n");
        fclose(resIn); fclose(spkIn);
        return EXIT_FAILURE;
    }
    fclose(spkIn);

    // ---- classify (OpenMP-parallel across spikes) ----
    vector<SpikeScore> scores((size_t)nSpikes);
    classifySpikes(allWaveforms.data(), nSpikes,
                   nSamples, nChannels,
                   uniformityThreshold, minAmplitude,
                   scores.data());

    // ---- read timestamps ----
    vector<int64_t> timestamps((size_t)nSpikes);
    if ((int64_t)fread(timestamps.data(), sizeof(int64_t),
                       (size_t)nSpikes, resIn) != nSpikes) {
        fprintf(stderr, "error: short read of .res file\n");
        fclose(resIn);
        return EXIT_FAILURE;
    }
    fclose(resIn);

    // ---- read clu & fet (small, sequential — not worth parallelising) ----
    vector<int>     cluIds((size_t)(hasClu ? nSpikes : 0));
    vector<int64_t> fetRows((size_t)(hasFet ? nSpikes * fetNDim : 0));

    if (hasClu)
        for (int64_t i = 0; i < nSpikes; ++i)
            if (fscanf(cluIn, "%d", &cluIds[(size_t)i]) != 1) {
                fprintf(stderr, "error: short read in .clu at spike %lld\n",
                        (long long)i);
                goto fail;
            }

    if (hasFet && (int64_t)fread(fetRows.data(), sizeof(int64_t),
                                 (size_t)(nSpikes * fetNDim), fetIn)
                 != nSpikes * fetNDim) {
        fprintf(stderr, "error: short read in .fet\n");
        goto fail;
    }

    // ---- statistics ----
    {
        int64_t nKept = 0, nRemoved = 0, nUnscored = 0;
        for (int64_t i = 0; i < nSpikes; ++i) {
            if (scores[(size_t)i].score < 0.0) ++nUnscored;
            if (scores[(size_t)i].keep)         ++nKept;
            else                                ++nRemoved;

            if (verbose) {
                printf("  spike %lld  t=%lld  peak=%.0f  score=%.4f%s\n",
                       (long long)i,
                       (long long)timestamps[(size_t)i],
                       scores[(size_t)i].peak,
                       scores[(size_t)i].score,
                       scores[(size_t)i].keep ? "" : "  [NOISE]");
            }
        }

        printf("process_denoiseuniform: removed %lld / %lld spikes (%.1f%%)"
               "  kept=%lld  unscored=%lld%s\n",
               (long long)nRemoved, (long long)nSpikes,
               nSpikes > 0 ? (100.0 * nRemoved / nSpikes) : 0.0,
               (long long)nKept, (long long)nUnscored,
               dryRun ? "  [dry-run — files unchanged]" : "");

        if (dryRun) goto done;
    }

    // ---- write filtered output files ----
    {
        FILE *resOut = xfopen(resTmp, "wb");
        FILE *spkOut = xfopen(spkTmp, "wb");
        FILE *cluOut = hasClu ? xfopen(cluTmp, "w")  : nullptr;
        FILE *fetOut = hasFet ? xfopen(fetTmp, "wb") : nullptr;

        if (cluOut) fprintf(cluOut, "%d\n", cluHeader);
        if (fetOut) fwrite(&fetNDim, sizeof(int32_t), 1, fetOut);

        for (int64_t i = 0; i < nSpikes; ++i) {
            if (!scores[(size_t)i].keep) continue;

            fwrite(&timestamps[(size_t)i], sizeof(int64_t), 1, resOut);
            fwrite(allWaveforms.data() + i * samplesPerSpike,
                   sizeof(int16_t), (size_t)samplesPerSpike, spkOut);
            if (cluOut) fprintf(cluOut, "%d\n", cluIds[(size_t)i]);
            if (fetOut) fwrite(fetRows.data() + i * fetNDim,
                               sizeof(int64_t), (size_t)fetNDim, fetOut);
        }

        fclose(resOut); fclose(spkOut);
        if (cluOut) fclose(cluOut);
        if (fetOut) fclose(fetOut);

        // atomic rename
        auto atomicRename = [](const string &tmp, const string &dst) {
            if (rename(tmp.c_str(), dst.c_str()) != 0) {
                fprintf(stderr, "error: cannot rename %s -> %s\n",
                        tmp.c_str(), dst.c_str());
                exit(EXIT_FAILURE);
            }
        };
        atomicRename(resTmp, resPath);
        atomicRename(spkTmp, spkPath);
        if (hasClu) atomicRename(cluTmp, cluPath);
        if (hasFet) atomicRename(fetTmp, fetPath);
    }

done:
    if (cluIn) fclose(cluIn);
    if (fetIn) fclose(fetIn);
    return EXIT_SUCCESS;

fail:
    if (cluIn) fclose(cluIn);
    if (fetIn) fclose(fetIn);
    remove(resTmp.c_str()); remove(spkTmp.c_str());
    remove(cluTmp.c_str()); remove(fetTmp.c_str());
    return EXIT_FAILURE;
}
