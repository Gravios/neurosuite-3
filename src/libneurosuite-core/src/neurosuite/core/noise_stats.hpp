// noise_stats.hpp — background-noise statistics of a spike group, in the
// waveform space of an extracted .spk, for the clusterers' noise whitener.
//
// The windows are cut by extractSpikeWindows (spike_extract.hpp) with the SAME
// SpikeWindowSpec as the group's .spk -- same geometry, same transform (raw,
// sdiff, stderiv_S<n>/C<n>), same int16 rounding -- at random times that lie
// clear of every detected spike, so the statistics describe exactly what a
// noise-only .spk row would look like.  A whitener built from .fil samples in
// any other domain is a model of a different signal.
//
// Outliers are rejected before the moments are taken.  Random windows still
// catch sub-threshold spikes, the tails of spikes the detector merged or missed,
// artifacts, and zero-filled gaps; any of them inflates the covariance along the
// directions the clusterer most needs to resolve.  Three gates, each robust
// (median / MAD), iterated `passes` times on the surviving set:
//   flat      a channel holding one value for the whole window (gap, stuck line);
//   amplitude any sample with |x - med_c| > zMax * sigma_c, sigma_c = 1.4826 MAD;
//   energy    window energy sum((x - med_c) / sigma_c)^2 more than zMax robust
//             SDs above the median energy (broadband events no single sample
//             gives away).
//
//   .nst file format — NSTE v1 (little-endian):
//     header [int32 x 8]: magic = 0x4E535445 ("NSTE"), version, nSamples,
//       nChannels, peakSampleIndex, guard, passes, reserved (0)
//     [int64 x 5]: nRequested, nDrawn, nKept, nRejectedFlat, nRejectedOutlier
//     [double x 2]: zMax, seed
//     [double x nChannels]           robust sigma per channel (kept windows)
//     [double x D]                   mean window, D = nSamples * nChannels,
//                                    sample-major (t * nChannels + c) like a .spk row
//     [double x D * D]               covariance of the kept windows (n - 1
//                                    normalisation), same layout, row-major
//   The transform is the one named in the file's method token
//   (<base>.nst.<method>.<group>), exactly as for the .spk beside it.
//
// DEPENDENCY-FREE: std only, header-only (spike_extract.hpp's rule).
#ifndef NEUROSUITE_CORE_NOISE_STATS_HPP
#define NEUROSUITE_CORE_NOISE_STATS_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "spike_extract.hpp"

namespace neurosuite {
namespace core {

inline constexpr std::int32_t kNsteMagic   = 0x4E535445;  // "NSTE"
inline constexpr std::int32_t kNsteVersion = 1;

struct NoiseStatsParams {
    std::int64_t nWindows = 20000;   // windows requested
    int          guard    = -1;      // extra clearance (samples) around each spike window; <0 = nSamples
    double       zMax     = 5.0;     // amplitude / energy gate, robust SDs
    int          passes   = 2;       // rejection passes
    std::uint64_t seed    = 1;       // draw seed (reproducible files)
};

struct NoiseStats {
    int nSamp = 0, nChan = 0, peakSample = 0, guard = 0, passes = 0;
    std::int64_t nRequested = 0, nDrawn = 0, nKept = 0, nRejectedFlat = 0, nRejectedOutlier = 0;
    double zMax = 0.0, seed = 0.0;
    std::vector<double> sigma;   // nChan
    std::vector<double> mean;    // D
    std::vector<double> cov;     // D * D
    int dim() const { return nSamp * nChan; }
    bool valid() const {
        const std::size_t D = static_cast<std::size_t>(dim());
        return nSamp > 0 && nChan > 0 && sigma.size() == static_cast<std::size_t>(nChan)
               && mean.size() == D && cov.size() == D * D;
    }
};

namespace detail {
inline double medianOf(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const std::size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(m), v.end());
    double hi = v[m];
    if (v.size() % 2) return hi;
    const double lo = *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(m));
    return 0.5 * (lo + hi);
}
}  // namespace detail

// Random window start times clear of every spike.  A window at time t occupies
// [t - peak - 1, t - peak + nSamp) (the -1 is the stderiv seed sample); it is
// accepted when no spike time s has |t - s| < nSamp + guard, so neither window
// touches the other plus `guard` samples.  `spikes` must be sorted.  Returns the
// accepted times sorted (what extractSpikeWindows requires).
inline std::vector<std::int64_t> drawNoiseTimes(const std::vector<std::int64_t>& spikes,
                                                std::int64_t totalSamples, int nSamp, int peak,
                                                int guard, std::int64_t nWanted, std::uint64_t seed)
{
    std::vector<std::int64_t> out;
    const std::int64_t lo = static_cast<std::int64_t>(peak) + 1;
    const std::int64_t hi = totalSamples - (nSamp - peak);           // inclusive
    if (nWanted <= 0 || hi < lo) return out;
    const std::int64_t clear = static_cast<std::int64_t>(nSamp) + guard;
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int64_t> pick(lo, hi);
    out.reserve(static_cast<std::size_t>(nWanted));
    const std::int64_t maxTries = 50 * nWanted;
    std::int64_t tries = 0;
    // Draw until nWanted DISTINCT times (a repeated time is not a new window) or the
    // try budget runs out, so a shortfall means the recording has too little spike-free
    // time, never that the generator repeated itself.
    while (static_cast<std::int64_t>(out.size()) < nWanted && tries < maxTries) {
        while (static_cast<std::int64_t>(out.size()) < nWanted && tries < maxTries) {
            ++tries;
            const std::int64_t t = pick(rng);
            auto it = std::lower_bound(spikes.begin(), spikes.end(), t - clear + 1);
            if (it != spikes.end() && *it < t + clear) continue;       // a spike within the clearance
            out.push_back(t);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }
    return out;
}

// Estimate the noise statistics of one group.  `spec` is the group's .spk spec
// (geometry + transform); its read buffer is shrunk here, since noise windows are
// sparse and a large buffer would be re-read for every one.  `spikes` are the
// group's spike times (any order).  Returns false when the .fil cannot be read or
// no window survives.
inline bool estimateNoiseStats(std::FILE* fil, SpikeWindowSpec spec, std::vector<std::int64_t> spikes,
                               const NoiseStatsParams& prm, NoiseStats& out)
{
    if (!fil || spec.nSamp <= 0 || spec.nChan <= 0 || spec.totalNbChannels <= 0) return false;
    const long long bytes = detail::sizeOf(fil);
    if (bytes < 0) return false;
    const std::int64_t totalSamples = static_cast<std::int64_t>(bytes)
        / (static_cast<std::int64_t>(sizeof(std::int16_t)) * spec.totalNbChannels);
    const int nS = spec.nSamp, nC = spec.nChan, D = nS * nC;
    const int guard = prm.guard < 0 ? nS : prm.guard;
    std::sort(spikes.begin(), spikes.end());
    const std::vector<std::int64_t> times =
        drawNoiseTimes(spikes, totalSamples, nS, spec.peakSample, guard, prm.nWindows, prm.seed);

    out = NoiseStats{};
    out.nSamp = nS; out.nChan = nC; out.peakSample = spec.peakSample; out.guard = guard;
    out.passes = prm.passes; out.zMax = prm.zMax; out.seed = static_cast<double>(prm.seed);
    out.nRequested = prm.nWindows; out.nDrawn = static_cast<std::int64_t>(times.size());
    if (times.empty()) return false;

    spec.bufferBytes = 0;                                   // one window (+ seed sample) per read
    std::vector<std::int16_t> win(times.size() * static_cast<std::size_t>(D));
    const std::int64_t n = extractSpikeWindows(
        fil, static_cast<std::int64_t>(times.size()), spec,
        [&](std::int64_t i) { return times[static_cast<std::size_t>(i)]; },
        [&](std::int64_t i, const std::int16_t* row) {
            std::copy(row, row + D, win.begin() + static_cast<std::ptrdiff_t>(i) * D);
        });
    if (n != static_cast<std::int64_t>(times.size())) return false;
    const std::size_t N = times.size();
    auto at = [&](std::size_t w, int t, int c) {
        return static_cast<double>(win[w * static_cast<std::size_t>(D) + static_cast<std::size_t>(t) * nC + c]);
    };

    // flat gate (once: it does not depend on the robust scale)
    std::vector<char> keep(N, 1);
    for (std::size_t w = 0; w < N; ++w)
        for (int c = 0; c < nC && keep[w]; ++c) {
            bool flat = true;
            for (int t = 1; t < nS && flat; ++t) flat = (at(w, t, c) == at(w, 0, c));
            if (flat) { keep[w] = 0; ++out.nRejectedFlat; }
        }

    std::vector<double> med(static_cast<std::size_t>(nC), 0.0), sig(static_cast<std::size_t>(nC), 1.0);
    auto robustScale = [&]() {
        std::vector<double> v;
        for (int c = 0; c < nC; ++c) {
            v.clear();
            for (std::size_t w = 0; w < N; ++w)
                if (keep[w]) for (int t = 0; t < nS; ++t) v.push_back(at(w, t, c));
            const double m = detail::medianOf(v);
            for (double& x : v) x = std::fabs(x - m);
            const double mad = detail::medianOf(v);
            med[static_cast<std::size_t>(c)] = m;
            sig[static_cast<std::size_t>(c)] = mad > 0 ? 1.4826 * mad : 1.0;
        }
    };
    for (int pass = 0; pass < std::max(prm.passes, 0); ++pass) {
        robustScale();
        std::vector<double> energy; energy.reserve(N);
        std::vector<double> eAll(N, 0.0);
        for (std::size_t w = 0; w < N; ++w) {
            if (!keep[w]) continue;
            double e = 0.0;
            for (int t = 0; t < nS; ++t)
                for (int c = 0; c < nC; ++c) {
                    const double z = (at(w, t, c) - med[static_cast<std::size_t>(c)]) / sig[static_cast<std::size_t>(c)];
                    e += z * z;
                }
            eAll[w] = e; energy.push_back(e);
        }
        std::vector<double> tmp = energy;
        const double eMed = detail::medianOf(tmp);
        for (double& x : tmp) x = std::fabs(x - eMed);
        const double eSig = 1.4826 * detail::medianOf(tmp);
        const double eMax = eMed + prm.zMax * (eSig > 0 ? eSig : 1.0);
        for (std::size_t w = 0; w < N; ++w) {
            if (!keep[w]) continue;
            bool bad = eAll[w] > eMax;
            for (int t = 0; t < nS && !bad; ++t)
                for (int c = 0; c < nC && !bad; ++c)
                    bad = std::fabs(at(w, t, c) - med[static_cast<std::size_t>(c)])
                          > prm.zMax * sig[static_cast<std::size_t>(c)];
            if (bad) { keep[w] = 0; ++out.nRejectedOutlier; }
        }
    }
    robustScale();                                           // final scale on the kept set

    std::int64_t kept = 0;
    for (char k : keep) kept += k;
    out.nKept = kept;
    if (kept < 2) return false;
    out.sigma = sig;
    out.mean.assign(static_cast<std::size_t>(D), 0.0);
    out.cov.assign(static_cast<std::size_t>(D) * D, 0.0);
    std::vector<double> x(static_cast<std::size_t>(D));
    for (std::size_t w = 0; w < N; ++w) if (keep[w])
        for (int j = 0; j < D; ++j) out.mean[static_cast<std::size_t>(j)] += win[w * static_cast<std::size_t>(D) + j];
    for (double& m : out.mean) m /= static_cast<double>(kept);
    for (std::size_t w = 0; w < N; ++w) {
        if (!keep[w]) continue;
        for (int j = 0; j < D; ++j)
            x[static_cast<std::size_t>(j)] = win[w * static_cast<std::size_t>(D) + j] - out.mean[static_cast<std::size_t>(j)];
        for (int a = 0; a < D; ++a) {
            const double xa = x[static_cast<std::size_t>(a)];
            double* row = out.cov.data() + static_cast<std::size_t>(a) * D;
            for (int b = a; b < D; ++b) row[b] += xa * x[static_cast<std::size_t>(b)];
        }
    }
    const double norm = 1.0 / static_cast<double>(kept - 1);
    for (int a = 0; a < D; ++a)
        for (int b = a; b < D; ++b) {
            const double v = out.cov[static_cast<std::size_t>(a) * D + b] * norm;
            out.cov[static_cast<std::size_t>(a) * D + b] = v;
            out.cov[static_cast<std::size_t>(b) * D + a] = v;
        }
    return true;
}

inline bool writeNoiseStats(const std::string& path, const NoiseStats& s) {
    if (!s.valid()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::int32_t h[8] = {kNsteMagic, kNsteVersion, s.nSamp, s.nChan, s.peakSample,
                               s.guard, s.passes, 0};
    const std::int64_t cnt[5] = {s.nRequested, s.nDrawn, s.nKept, s.nRejectedFlat, s.nRejectedOutlier};
    const double prm[2] = {s.zMax, s.seed};
    bool ok = std::fwrite(h, 4, 8, f) == 8 && std::fwrite(cnt, 8, 5, f) == 5
              && std::fwrite(prm, 8, 2, f) == 2
              && std::fwrite(s.sigma.data(), 8, s.sigma.size(), f) == s.sigma.size()
              && std::fwrite(s.mean.data(), 8, s.mean.size(), f) == s.mean.size()
              && std::fwrite(s.cov.data(), 8, s.cov.size(), f) == s.cov.size();
    if (std::fclose(f) != 0) ok = false;
    return ok;
}

// Returns false on open / bad magic / unsupported version / short read.
inline bool loadNoiseStats(const std::string& path, NoiseStats& s) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::int32_t h[8]; std::int64_t cnt[5]; double prm[2];
    bool ok = std::fread(h, 4, 8, f) == 8 && h[0] == kNsteMagic && h[1] >= 1 && h[1] <= kNsteVersion
              && h[2] > 0 && h[3] > 0 && std::fread(cnt, 8, 5, f) == 5 && std::fread(prm, 8, 2, f) == 2;
    if (ok) {
        s = NoiseStats{};
        s.nSamp = h[2]; s.nChan = h[3]; s.peakSample = h[4]; s.guard = h[5]; s.passes = h[6];
        s.nRequested = cnt[0]; s.nDrawn = cnt[1]; s.nKept = cnt[2];
        s.nRejectedFlat = cnt[3]; s.nRejectedOutlier = cnt[4];
        s.zMax = prm[0]; s.seed = prm[1];
        const std::size_t D = static_cast<std::size_t>(s.dim());
        s.sigma.resize(static_cast<std::size_t>(s.nChan)); s.mean.resize(D); s.cov.resize(D * D);
        ok = std::fread(s.sigma.data(), 8, s.sigma.size(), f) == s.sigma.size()
             && std::fread(s.mean.data(), 8, D, f) == D
             && std::fread(s.cov.data(), 8, D * D, f) == D * D;
    }
    std::fclose(f);
    return ok;
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_NOISE_STATS_HPP
