/***************************************************************************
    process_extractemg.h
    --------------------
    Shared types and declarations for process_extractemg.

    EMG component identification and removal for multichannel
    extracellular .dat recordings.  C++ port of the algorithmic core of
    the labbox MATLAB EMG_removing toolbox (Chen 2019), using FastICA
    deflation pow3 (faithful port of FastICA_25/fpica.m, case 10):

      1. spatial-mode identification via FastICA on a high-pass-filtered
         subsample, picking the IC whose channel loading is most
         spatially uniform (max |sum(A[:,k]/||A[:,k]||)|),
      2. linear projection of the raw (un-filtered, de-meaned) trace
         onto the unmixing row Ws,
      3. subtraction of the projected component from every channel in
         the group using the mixing column As,
      4. discrete event detection from the |EMG_au| envelope.

    The "groups" passed in via --channel-groups are typically probes
    (one entry per probe, channels = union of that probe's
    anatomicalDescription groups).  All groups share a single output
    .dat (each group writes only to its own channels — no overlap).

    OpenMP parallelism
    ------------------
      - Pass 1 FastICA: across-probes parallel via 'omp parallel for' over
        groups, with the dat ID-window pre-loaded into a shared buffer.
      - Pass 1 inner pow3 kernel: parallel reduction over t.
      - Pass 2 streaming projection / subtraction: across-probes parallel
        per chunk (each probe writes to disjoint channels of bufClean).

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
    // channel indices into the dat for group g.  Wrapper-side semantics:
    // each group is conventionally one probe, with channels gathered
    // from the union of that probe's anatomicalDescription groups.
    // Channels not in any group pass through unchanged.
    std::vector<std::vector<int>> groupChannels;

    // Output filename suffixes for per-group artefacts (.emg.<id>,
    // .res.emg.<id>, .spk.emg.<id>, and the metadata-yaml entry id).
    // When empty, defaults to 1..groupChannels.size().
    std::vector<int> groupIds;

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

    // GPU backend for the FastICA pow3 inner kernel.  "auto" picks the
    // first available among CUDA, HIP, SYCL, falling back to CPU.
    // String-form is parsed in main(); kept as enum here so the rest of
    // the code is type-safe.  IcaBackend lives in process_extractemg_gpu.h.
    int /*IcaBackend*/ backend = 0;   // = IcaBackend::Auto
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
// channelMean        per-channel mean over the identification window
//                    (used for both the FastICA centring and the Pass 2
//                    projection EMG_au[t] = (raw[t,:] - mu) · Ws).
struct GroupModel {
    std::vector<double> loading;            // As (nch)
    std::vector<double> unmixing;           // Ws (nch)
    std::vector<double> channelMean;        // μ  (nch) — over ID window
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
// Pow3 kernel injection
// ---------------------
// The pow3 inner update is the only heavy step per FastICA iteration
// and is also the only part that benefits from a GPU.  Callers pass a
// Pow3Kernel object (CPU / CUDA / HIP / SYCL) which has prepare(Z)
// called once and step(w, newW) called per iteration.  See
// process_extractemg_gpu.h for the interface.  Pass nullptr to use the
// CPU OpenMP fallback inline (no factory, no virtual call) — useful for
// the unit tests.
// ---------------------------------------------------------------------------
class Pow3Kernel;   // forward declaration (process_extractemg_gpu.h)

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
                          uint64_t                  seed            = 0,
                          Pow3Kernel               *pow3Kernel      = nullptr);
