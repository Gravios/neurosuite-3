/***************************************************************************
    process_extractemg.h
    --------------------
    Shared types and declarations for process_extractemg.

    EMG component identification and removal for multichannel
    extracellular .dat recordings.  C++ port of the algorithmic core of
    the labbox MATLAB EMG_removing toolbox (Chen 2019), restricted to the
    operations that have an unambiguous translation:

      1. spatial-mode identification on high-pass-filtered data,
      2. linear projection of the raw trace onto the EMG loading vector,
      3. subtraction of the projected component from every channel in
         the spike-detection group,
      4. discrete event detection from the |EMG_au| envelope.

    The MATLAB toolbox uses FastICA on whitened data; the C++ port uses
    PCA (leading eigenvector of the channel-channel covariance) on the
    high-pass-filtered subsample.  The two are equivalent up to a sign
    when the EMG mode dominates the high-band variance — which by
    construction it does, because the high-pass cutoff is chosen above
    the LFP corner — and the eigendecomposition has the advantages of
    being deterministic, dependency-free, trivial to parallelise, and
    fast for the channel counts we care about (8–32 per shank).

    copyright  (C) 2026 neurosuite-3 contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// CLI parameters
// ---------------------------------------------------------------------------
struct EmgArgs {
    std::string basename;            // session basename (no extension)
    int         nChannels    = 0;    // total channels in dat
    double      samplingRate = 0.0;  // Hz

    // Per-group channel lists.  groupChannels[g] is a vector of 0-based
    // channel indices into the dat for group g (g is 0-based here; the
    // wrapper script and the on-disk filenames use 1-based group numbers).
    std::vector<std::vector<int>> groupChannels;

    int         spikeLength      = 32;     // samples per event waveform
    int         peakSampleIndex  = 16;     // index of peak within waveform

    double      hpFreq           = 100.0;  // high-pass cutoff (Hz)
    double      thresholdFactor  = 5.0;    // threshold = factor * MAD(|EMG_au|)
    double      envelopeWindowMs = 20.0;   // smoothing window for envelope
    int         refractorySamples = 0;     // 0 → fill in from envelopeWindowMs
    int         peakSearchLength = 0;      // 0 → refractorySamples
    int         downsample       = 3;      // for component identification

    double      noiseStartSec    = 0.0;    // window for component identification
    double      noiseDurationSec = 60.0;
    int64_t     chunkSamples     = 1<<20;  // streaming chunk size, samples per channel

    bool        writeCleanDat    = true;   // <session>-emgclean.dat
    bool        writeEmgTrace    = true;   // <session>.emg.N
    bool        writeWaveforms   = true;   // <session>.spk.emg.N
    bool        verbose          = false;
};

// ---------------------------------------------------------------------------
// Per-group result of the identification pass.
//
// loading            mixing column for the EMG component
//                    (As in MATLAB notation; nch-vector, signal space).
//                    EMG signal in channel c is reconstructed as
//                        loading[c] * EMG_au[t]
// unmixing           unmixing row for the EMG component
//                    (Ws in MATLAB notation; nch-vector, signal space).
//                    EMG_au[t] is computed as dot( unmixing, x[t,:] - mu ).
// uniformityScore    |sum( loading / ||loading|| )| in [0, sqrt(nch)] —
//                    the diagnostic the MATLAB toolbox uses to select
//                    the EMG component out of all ICs.
// eigenvalueRatio    leading eigenvalue / sum(eigenvalues) of the spatial
//                    covariance, kept for diagnostics.
// threshold          absolute amplitude threshold for event detection,
//                    computed as thresholdFactor * 1.4826 * MAD(|EMG_au|)
//                    on the identification subsample.
// medianEmgEnvelope  MAD(|EMG_au|) on the subsample (for diagnostics).
// nIcaSweeps         number of fixed-point iterations consumed for the
//                    chosen component.
// converged          whether FastICA converged within maxIter for the
//                    chosen component.
// ---------------------------------------------------------------------------
struct GroupModel {
    std::vector<double> loading;            // As (nch)
    std::vector<double> unmixing;           // Ws (nch)
    double              uniformityScore   = 0.0;
    double              eigenvalueRatio   = 0.0;
    double              threshold         = 0.0;
    double              medianEmgEnvelope = 0.0;
    int                 nIcaSweeps        = 0;
    bool                converged         = false;
};

// ---------------------------------------------------------------------------
// 4th-order Butterworth high-pass filter, two cascaded biquads in
// transposed-direct-form-II.  Stateful: each instance carries its own pair
// of (z1, z2) state variables and is meant to be applied to a single
// channel of a streaming signal.
//
// Coefficients are computed for the given digital cutoff once, then
// step() is called sample-by-sample.  Designed to be lightweight enough
// to instantiate one per (group × channel) pair.
// ---------------------------------------------------------------------------
class Butter4HP {
public:
    Butter4HP() = default;
    void   design(double cutoffHz, double sampleRateHz);
    double step(double x);
    void   reset();

private:
    // Two biquad stages: section[k] = {b0,b1,b2,a1,a2}
    double b0_[2] = {0,0}, b1_[2] = {0,0}, b2_[2] = {0,0};
    double a1_[2] = {0,0}, a2_[2] = {0,0};
    double z1_[2] = {0,0}, z2_[2] = {0,0};
};

// ---------------------------------------------------------------------------
// Symmetric eigendecomposition by Jacobi rotations.
//
// On entry  A is an n × n symmetric matrix (row-major, A[i*n + j]).
// On exit   eigenvalues holds the n eigenvalues (unsorted),
//           eigenvectors holds the corresponding eigenvectors as columns
//           (also row-major: eigenvectors[i*n + j] = i-th component of
//           j-th eigenvector).
//
// Returns true on convergence within maxSweeps × n^2 / 2 rotations.
//
// For our use case n is at most a few dozen, so the O(n^3) cost per
// sweep is irrelevant.  Numerically stable; no LAPACK dependency.
// ---------------------------------------------------------------------------
bool jacobiEigenSym(std::vector<double> &A,
                    int n,
                    std::vector<double> &eigenvalues,
                    std::vector<double> &eigenvectors,
                    int maxSweeps = 50);

// ---------------------------------------------------------------------------
// FastICA — Hyvärinen's fixed-point algorithm, direct C++ port of the
// labbox FastICA_25 toolbox (fastica.m / fpica.m / pcamat.m / whitenv.m)
// restricted to the configuration the MATLAB EMG_removing toolbox
// actually uses by default:
//
//     approach        = 'defl'         (deflation, one IC at a time)
//     g (nonlinearity)= 'pow3'         u → u³
//     finetune        = 'off'
//     stabilization   = 'off'
//     mu              = 1
//     epsilon         = 1e-4
//     maxIterations   = 1000
//     sampleSize      = 1
//     initState       = 'rand'
//
// Layout (N rows × nch cols, row-major)
// -------------------------------------
// Input  X       : N × nch matrix, X[t*nch + c] = sample t, channel c.
//                  Caller must have already removed per-channel mean
//                  (remmean).  Sample-major storage gives both the
//                  spatial-covariance kernel and the FastICA pow3 kernel
//                  contiguous t-strided reads, which is what we want for
//                  the typical regime N ≫ nch.
// Outputs
//   A           : nch × K mixing matrix, column-major  A[c + nch*k]
//                 (column k is the k-th IC's loading on channels)
//   W           : K × nch unmixing matrix, row-major   W[k*nch + c]
//                 (row k applied to a centred sample → IC k's value)
//
// Internally
//   - PCA on the channel covariance (jacobiEigenSym), no dimension
//     reduction
//   - whitening matrix  wm  = D^(-1/2) E^T
//     dewhitening       dwm = E D^(1/2)
//   - whiten the input → Z (also N × nch row-major)
//   - deflation loop with random init, projection orthogonal to already
//     accepted ICs, fixed-point pow3 update, normalisation, convergence
//     test on cosine to previous iterate (handles sign flip)
//
// Returns true if all K components converged within maxIter; otherwise
// returns false but still populates A, W with whatever components were
// successfully extracted (the caller can decide how strict to be).
//
// Determinism
// -----------
// The fixed-point algorithm starts from a random unit vector for each
// IC.  We expose a `seed` parameter so callers can request reproducible
// runs.  Pass 0 for nondeterministic seeding from std::random_device.
// ---------------------------------------------------------------------------
bool fastIcaDeflationPow3(const std::vector<double> &X,    // N × nch (row-major, sample-major)
                          int                       nch,
                          int                       N,
                          int                       K,
                          std::vector<double>      &A,     // nch × K (column-major: A[c + nch*k])
                          std::vector<double>      &W,     // K × nch (row-major:    W[k*nch + c])
                          int                      &outIterTotal,
                          int                      &outAcceptedK,
                          double                    epsilon         = 1e-4,
                          int                       maxIterations   = 1000,
                          int                       failureLimit    = 5,
                          uint64_t                  seed            = 0);
