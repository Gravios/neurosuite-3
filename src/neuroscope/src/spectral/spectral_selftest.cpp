// Standalone numerical validation of the spectral DSP core. Builds without
// Qt; run as a plain executable. Exits non-zero if any check fails.
//
//   g++ -O2 -fopenmp -std=c++20 spectral_selftest.cpp dpss.cpp multitaper.cpp -o selftest
//
// Checks: FFT against a naive DFT, DPSS concentration/orthonormality/polarity,
// multitaper PSD Parseval normalisation and peak localisation for a pure
// tone, white-noise flatness, and ZCA common-whitening of correlated data.

#include "realfft.h"
#include "dpss.h"
#include "multitaper.h"
#include "spectralengine.h"
#include "colormap.h"
#include "spectralgpu_kernels.h"
#include "spectralwindow.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace neuroscope::spectral;

static int failures = 0;
static void check(bool ok, const char* name, double got = 0, double want = 0)
{
    if (ok) { std::printf("  PASS  %s\n", name); }
    else    { std::printf("  FAIL  %s   (got %.6g, want %.6g)\n", name, got, want); ++failures; }
}

int main()
{
    // ---- FFT vs naive DFT ------------------------------------------------
    {
        const int n = 64;
        std::vector<std::complex<double>> x(n), ref(n);
        std::mt19937 rng(1);
        std::uniform_real_distribution<double> u(-1, 1);
        for (int i = 0; i < n; ++i) x[i] = {u(rng), u(rng)};
        for (int k = 0; k < n; ++k) {
            std::complex<double> s(0, 0);
            for (int t = 0; t < n; ++t) {
                double a = -2.0 * M_PI * k * t / n;
                s += x[t] * std::complex<double>(std::cos(a), std::sin(a));
            }
            ref[k] = s;
        }
        auto y = x;
        fftRadix2(y, false);
        double maxerr = 0;
        for (int k = 0; k < n; ++k) maxerr = std::max(maxerr, std::abs(y[k] - ref[k]));
        check(maxerr < 1e-9, "fft matches naive DFT", maxerr, 0);

        // round trip
        fftRadix2(y, true);
        double rterr = 0;
        for (int k = 0; k < n; ++k) rterr = std::max(rterr, std::abs(y[k] - x[k]));
        check(rterr < 1e-12, "fft inverse round-trip", rterr, 0);
    }

    // ---- DPSS concentration, orthonormality, polarity --------------------
    {
        const int N = 256; const double NW = 4.0; const int K = 7;
        DpssTapers d = computeDpss(N, NW, K);
        check(d.valid() && d.K == K, "dpss generated K tapers", d.K, K);

        check(d.lambda[0] > 0.999, "dpss lambda_0 ~ 1 (concentrated)", d.lambda[0], 1.0);
        bool decreasing = true;
        for (int k = 1; k < K; ++k) if (d.lambda[k] > d.lambda[k - 1] + 1e-9) decreasing = false;
        check(decreasing, "dpss lambda decreasing in k", 0, 0);
        // Tapers up to ~2NW-1 remain well concentrated.
        check(d.lambda[K - 1] > 0.9, "dpss low-order tapers stay concentrated", d.lambda[K - 1], 0.9);

        double maxOff = 0, maxNormErr = 0;
        for (int i = 0; i < K; ++i)
            for (int j = 0; j < K; ++j) {
                double dot = 0;
                for (int t = 0; t < N; ++t) dot += d.taper[i][t] * d.taper[j][t];
                if (i == j) maxNormErr = std::max(maxNormErr, std::abs(dot - 1.0));
                else        maxOff     = std::max(maxOff, std::abs(dot));
            }
        check(maxNormErr < 1e-6, "dpss tapers unit energy", maxNormErr, 0);
        check(maxOff < 1e-6, "dpss tapers orthogonal", maxOff, 0);

        double sum0 = 0; for (double v : d.taper[0]) sum0 += v;
        check(sum0 > 0, "dpss taper 0 positive polarity", sum0, 0);
    }

    // ---- Multitaper PSD: Parseval + tone localisation --------------------
    {
        const double fs = 1000.0;
        const int N = 1024; const int nfft = 1024;
        const double f0 = 125.0;     // lands on an exact bin (125/1000*1024 = 128)
        const double amp = 2.0;
        std::vector<double> x(N);
        for (int t = 0; t < N; ++t) x[t] = amp * std::sin(2.0 * M_PI * f0 * t / fs);

        DpssTapers d = computeDpss(N, 4.0, 7);
        std::vector<double> psd = multitaperPsd(x.data(), N, d, fs, nfft, TaperWeighting::Uniform);
        std::vector<double> f = psdFrequencies(nfft, fs);

        // Peak bin near f0.
        int peak = 0; double pv = -1;
        for (size_t k = 0; k < psd.size(); ++k) if (psd[k] > pv) { pv = psd[k]; peak = (int)k; }
        check(std::abs(f[peak] - f0) <= fs / nfft + 1e-9, "mtm psd peak at tone", f[peak], f0);

        // Parseval: integral of one-sided PSD ~ variance = amp^2/2.
        const double df = fs / nfft;
        double integral = 0; for (double v : psd) integral += v * df;
        const double var = amp * amp / 2.0;
        check(std::abs(integral - var) / var < 0.02, "mtm psd Parseval (tone)", integral, var);
    }

    // ---- White-noise flatness + Parseval ---------------------------------
    {
        const double fs = 1000.0; const int N = 4096; const int nfft = 1024;
        std::mt19937 rng(7);
        std::normal_distribution<double> g(0.0, 1.0);
        std::vector<double> x(N); for (auto& v : x) v = g(rng);
        double mean = 0; for (double v : x) mean += v; mean /= N;
        double var = 0; for (double v : x) var += (v - mean) * (v - mean); var /= (N - 1);

        DpssTapers d = computeDpss(nfft, 4.0, 7); // taper length = window = nfft
        std::vector<std::vector<double>> sg;
        multitaperSpectrogram(x.data(), N, d, fs, nfft, nfft / 2, TaperWeighting::Eigenvalue, sg);
        check(!sg.empty(), "spectrogram produced columns", (double)sg.size(), 1);

        // Average PSD across windows, integrate, compare to variance.
        std::vector<double> avg(sg[0].size(), 0.0);
        for (auto& col : sg) for (size_t k = 0; k < col.size(); ++k) avg[k] += col[k];
        for (auto& v : avg) v /= sg.size();
        const double df = fs / nfft;
        double integral = 0; for (double v : avg) integral += v * df;
        check(std::abs(integral - var) / var < 0.10, "white-noise Parseval", integral, var);
    }

    // ---- Common whitening: correlated -> identity covariance -------------
    {
        const int nch = 3, ns = 20000;
        std::mt19937 rng(11);
        std::normal_distribution<double> g(0.0, 1.0);
        std::vector<double> data((size_t)nch * ns);
        // Shared common component + per-channel noise -> strong cross-correlation.
        for (int s = 0; s < ns; ++s) {
            double common = g(rng);
            for (int c = 0; c < nch; ++c)
                data[(size_t)c * ns + s] = common + 0.3 * g(rng);
        }
        commonWhiten(data.data(), nch, ns, 1e-6);

        // Resulting covariance should be ~ identity.
        double maxOff = 0, maxDiagErr = 0;
        for (int a = 0; a < nch; ++a)
            for (int b = 0; b < nch; ++b) {
                double acc = 0;
                for (int s = 0; s < ns; ++s)
                    acc += data[(size_t)a * ns + s] * data[(size_t)b * ns + s];
                acc /= (ns - 1);
                if (a == b) maxDiagErr = std::max(maxDiagErr, std::abs(acc - 1.0));
                else        maxOff     = std::max(maxOff, std::abs(acc));
            }
        check(maxDiagErr < 0.05, "whitening unit variance", maxDiagErr, 0);
        check(maxOff < 0.05, "whitening decorrelates channels", maxOff, 0);
    }

    // ---- Arbitrary (non-power-of-two) nfft via FFTW ----------------------
    // Exercised only when the FFTW backend is compiled in; the radix-2
    // fallback rounds nfft to a power of two and is covered above.
    {
        std::printf("  ....  FFT backend: %s\n", fftwAvailable() ? "FFTW" : "radix-2 fallback");
        if (fftwAvailable()) {
            const double fs = 1000.0;
            const int N = 1000;        // non-power-of-two window
            const int nfft = 1000;     // exact, no zero-padding
            const double f0 = 150.0;   // 150/1000*1000 = bin 150 exactly
            const double amp = 1.5;
            std::vector<double> x(N);
            for (int t = 0; t < N; ++t) x[t] = amp * std::sin(2.0 * M_PI * f0 * t / fs);

            RealFftPlan plan(nfft);
            check(plan.nfft() == nfft, "fftw honours non-power-of-two nfft", plan.nfft(), nfft);

            DpssTapers d = computeDpss(N, 4.0, 7);
            std::vector<double> psd = multitaperPsd(x.data(), N, d, fs, plan, TaperWeighting::Uniform);
            std::vector<double> f = psdFrequencies(plan.nfft(), fs);
            int peak = 0; double pv = -1;
            for (size_t k = 0; k < psd.size(); ++k) if (psd[k] > pv) { pv = psd[k]; peak = (int)k; }
            check(std::abs(f[peak] - f0) <= fs / nfft + 1e-9, "non-pow2 mtm peak at tone", f[peak], f0);
            const double df = fs / nfft;
            double integral = 0; for (double v : psd) integral += v * df;
            const double var = amp * amp / 2.0;
            check(std::abs(integral - var) / var < 0.02, "non-pow2 mtm Parseval", integral, var);
        }
    }

    // ---- Compute engine: modes A/B, whitening, caching -------------------
    {
        const double fs = 1000.0;
        const int nCh = 4, nS = 4096;
        const double f0 = 125.0;                 // tone frequency
        std::mt19937 rng(21);
        std::normal_distribution<double> g(0.0, 0.05);
        // sample-major buffer: data[s*nCh + c]. Channel 2 carries the tone.
        std::vector<double> data((size_t)nS * nCh);
        for (int s = 0; s < nS; ++s)
            for (int c = 0; c < nCh; ++c) {
                double v = g(rng);
                if (c == 2) v += std::sin(2.0 * M_PI * f0 * s / fs);
                data[(size_t)s * nCh + c] = v;
            }
        std::vector<int> channels{0, 1, 2, 3};

        SpectralEngine engine;

        // Mode B: spectrogram of channel 2 -> dominant frequency near f0.
        SpectralParams pb;
        pb.mode = SpectralMode::TimeFrequencySingleChannel;
        pb.samplingRate = fs; pb.windowSamples = 256; pb.nfft = 256;
        pb.stepSamples = 128; pb.nw = 3.0; pb.nTapers = 5;
        pb.singleChannel = 2; pb.freqLow = 0.0; pb.freqHigh = 0.0;
        const SpectralImage& B = engine.compute(data.data(), nS, nCh, channels, pb, 1);
        check(B.valid() && B.mode == SpectralMode::TimeFrequencySingleChannel,
              "engine mode B image", B.rows, 1);
        // row with the largest time-averaged power.
        int bestRow = 0; double bestAvg = -1;
        for (int r = 0; r < B.rows; ++r) {
            double a = 0; for (int c = 0; c < B.cols; ++c) a += B.at(r, c); a /= B.cols;
            if (a > bestAvg) { bestAvg = a; bestRow = r; }
        }
        check(std::abs(B.freqs[bestRow] - f0) <= 2.0 * fs / pb.nfft,
              "engine mode B localises tone", B.freqs[bestRow], f0);

        // Cache: identical window + params returns the same cached object.
        const SpectralImage& B2 = engine.compute(data.data(), nS, nCh, channels, pb, 1);
        check(&B2 == &B, "engine caches unchanged window/params", 0, 0);

        // Mode A: band power across channels; channel 2 (in-band tone) >> others.
        SpectralParams pa = pb;
        pa.mode = SpectralMode::FrequencyAcrossChannels;
        pa.freqLow = 100.0; pa.freqHigh = 150.0;
        const SpectralImage& A = engine.compute(data.data(), nS, nCh, channels, pa, 1);
        check(A.valid() && A.rows == nCh, "engine mode A image", A.rows, nCh);
        double pTone = 0, pOther = 0;
        for (int c = 0; c < A.cols; ++c) { pTone += A.at(2, c); pOther += A.at(0, c); }
        check(pTone > 5.0 * pOther, "engine mode A separates in-band channel", pTone, pOther);

        // Whitening path runs and yields a finite image.
        SpectralParams pw = pa; pw.whiten = true;
        const SpectralImage& W = engine.compute(data.data(), nS, nCh, channels, pw, 2);
        bool finite = W.valid();
        for (float v : W.data) if (!std::isfinite(v)) finite = false;
        check(finite, "engine whitening path finite", 0, 0);

        // GPU backend agrees with the CPU path. With no device it falls back
        // and the match is exact; on real hardware cuFFT vs FFTW differ only by
        // double-precision rounding, so compare with a relative tolerance.
        SpectralParams pcpu = pb; pcpu.backend = SpectralBackend::Cpu;
        SpectralImage cpu = engine.compute(data.data(), nS, nCh, channels, pcpu, 10);
        SpectralParams pgpu = pb; pgpu.backend = SpectralBackend::Cuda;
        SpectralImage gpu = engine.compute(data.data(), nS, nCh, channels, pgpu, 11);
        bool agree = cpu.rows == gpu.rows && cpu.cols == gpu.cols
                     && cpu.data.size() == gpu.data.size();
        double maxRel = 0.0;
        for (std::size_t i = 0; i < gpu.data.size() && agree; ++i) {
            const double a = cpu.data[i], b = gpu.data[i];
            const double denom = std::max(1e-12, std::fabs(a));
            maxRel = std::max(maxRel, std::fabs(a - b) / denom);
        }
        check(agree && maxRel < 1e-3, "GPU backend agrees with CPU (or falls back)",
              maxRel, 0.0);
    }

    // ---- Mode transitions and frequency-range updates --------------------
    {
        const double fs = 1000.0;
        const int nCh = 3, nS = 4096;
        std::mt19937 rng(99);
        std::normal_distribution<double> g(0.0, 0.1);
        std::vector<double> data((size_t)nS * nCh);
        for (int s = 0; s < nS; ++s)
            for (int c = 0; c < nCh; ++c)
                data[(size_t)s * nCh + c] = g(rng) + std::sin(2.0 * M_PI * 120.0 * s / fs);
        std::vector<int> channels{0, 1, 2};
        SpectralEngine engine;

        SpectralParams pb;
        pb.mode = SpectralMode::TimeFrequencySingleChannel;
        pb.samplingRate = fs; pb.windowSamples = 256; pb.nfft = 256; pb.stepSamples = 128;
        pb.nw = 3.0; pb.nTapers = 5; pb.singleChannel = 1; pb.freqLow = 0.0; pb.freqHigh = 0.0;

        // Mode B: freqs populated, rowChannels empty, rows == freq bins.
        SpectralImage B = engine.compute(data.data(), nS, nCh, channels, pb, 1);
        check(B.mode == SpectralMode::TimeFrequencySingleChannel && !B.freqs.empty()
              && B.rowChannels.empty() && (int)B.freqs.size() == B.rows,
              "mode B image fields", (int)B.freqs.size(), B.rows);

        // Switch to mode A on the same engine/window: distinct recompute, mode A
        // fields (rowChannels set, freqs empty, rows == channels).
        SpectralParams pa = pb;
        pa.mode = SpectralMode::FrequencyAcrossChannels;
        pa.freqLow = 100.0; pa.freqHigh = 150.0;
        SpectralImage A = engine.compute(data.data(), nS, nCh, channels, pa, 1);
        check(A.mode == SpectralMode::FrequencyAcrossChannels && A.freqs.empty()
              && (int)A.rowChannels.size() == nCh && A.rows == nCh,
              "mode A image fields after switch", A.rows, nCh);

        // Switch back to mode B: cache keyed on mode, so a mode B image returns.
        SpectralImage B2 = engine.compute(data.data(), nS, nCh, channels, pb, 1);
        check(B2.mode == SpectralMode::TimeFrequencySingleChannel && !B2.freqs.empty()
              && B2.rows == B.rows, "mode B restored after A", B2.rows, B.rows);

        // Frequency range crops the mode B rows to the requested band.
        SpectralParams pn = pb; pn.freqLow = 100.0; pn.freqHigh = 200.0;
        SpectralImage Bn = engine.compute(data.data(), nS, nCh, channels, pn, 1);
        const double binHz = fs / pn.nfft;
        check(!Bn.freqs.empty() && Bn.freqs.front() >= 100.0 - binHz
              && Bn.freqs.back() <= 200.0 + binHz && Bn.rows < B.rows,
              "freq range crops mode B band", Bn.rows, B.rows);

        // Mode A band power tracks the integration band: the 120 Hz tone lands
        // inside [100,150] but not inside [10,40].
        SpectralParams pIn = pa;  pIn.freqLow = 100.0; pIn.freqHigh = 150.0;
        SpectralParams pOut = pa; pOut.freqLow = 10.0;  pOut.freqHigh = 40.0;
        SpectralImage Ain  = engine.compute(data.data(), nS, nCh, channels, pIn, 1);
        SpectralImage Aout = engine.compute(data.data(), nS, nCh, channels, pOut, 1);
        double pin = 0, pout = 0;
        for (int c = 0; c < Ain.cols; ++c)  pin  += Ain.at(0, c);
        for (int c = 0; c < Aout.cols; ++c) pout += Aout.at(0, c);
        check(pin > 5.0 * pout, "mode A band power follows freq range", pin, pout);
    }

    // ---- Colormaps -------------------------------------------------------
    {
        std::uint8_t r, g, b;
        colormapRgb(0.0, Colormap::Grayscale, r, g, b);
        check(r == 0 && g == 0 && b == 0, "grayscale t=0 is black", r, 0);
        colormapRgb(1.0, Colormap::Grayscale, r, g, b);
        check(r == 255 && g == 255 && b == 255, "grayscale t=1 is white", r, 255);
        // clamping out-of-range
        std::uint8_t r2, g2, b2;
        colormapRgb(-1.0, Colormap::Viridis, r, g, b);
        colormapRgb(0.0,  Colormap::Viridis, r2, g2, b2);
        check(r == r2 && g == g2 && b == b2, "colormap clamps below 0", r, r2);
        // grayscale monotone luminance
        bool mono = true; int prev = -1;
        for (int i = 0; i <= 10; ++i) {
            colormapRgb(i / 10.0, Colormap::Grayscale, r, g, b);
            if (r < prev) mono = false; prev = r;
        }
        check(mono, "grayscale monotone in t", 0, 0);
        // viridis endpoints distinct (dark -> bright)
        std::uint8_t r0, g0, b0, r1, g1, b1;
        colormapRgb(0.0, Colormap::Viridis, r0, g0, b0);
        colormapRgb(1.0, Colormap::Viridis, r1, g1, b1);
        check((r1 + g1 + b1) > (r0 + g0 + b0), "viridis brightens with t",
              r1 + g1 + b1, r0 + g0 + b0);

        // jet: blue at the low end, red at the high end, bright green mid.
        std::uint8_t jr0, jg0, jb0, jr1, jg1, jb1, jrm, jgm, jbm;
        colormapRgb(0.0, Colormap::Jet, jr0, jg0, jb0);
        colormapRgb(1.0, Colormap::Jet, jr1, jg1, jb1);
        colormapRgb(0.5, Colormap::Jet, jrm, jgm, jbm);
        check(jb0 > jr0, "jet low end is blue", jb0, jr0);
        check(jr1 > jb1, "jet high end is red", jr1, jb1);
        check(jgm > jg0 && jgm > jg1, "jet mid is bright green", jgm, jg0);
    }

    // ---- GPU kernel math (host/device shared; validated on the CPU) ------
    // The CUDA kernels are thin wrappers over these functions, so checking them
    // here covers the index decomposition, tapering and reduction arithmetic
    // that runs on the device.
    {
        using namespace neuroscope::spectral::gpu::kern;

        // Index round-trips: decode then re-encode must reproduce the thread id.
        const int K = 5, nfft = 256, nbins = nfft / 2 + 1;
        bool segOk = true, psdOk = true;
        long segIds[] = {0, 1, nfft - 1, (long)nfft, (long)nfft * K + 7, 123456};
        for (long gid : segIds) {
            int c, k, i; decodeSeg(gid, nfft, K, c, k, i);
            if (((long)(c * K + k) * nfft + i) != gid) segOk = false;
        }
        check(segOk, "decodeSeg round-trips", 0, 0);
        long psdIds[] = {0, 1, nbins - 1, (long)nbins, (long)nbins * 3 + 5, 99999};
        for (long gid : psdIds) {
            int c, b; decodePsd(gid, nbins, c, b);
            if (((long)c * nbins + b) != gid) psdOk = false;
        }
        check(psdOk, "decodePsd round-trips", 0, 0);

        // Tapered sample: in-window product, zero-pad, and out-of-range guard.
        const int N = 4, step = 2;
        std::vector<double> sig = {1, 2, 3, 4, 5, 6, 7, 8};
        std::vector<double> tf = {1, 1, 1, 1,  0.5, 0.5, 0.5, 0.5}; // K2 x N4
        check(taperedSample(sig.data(), sig.size(), tf.data(), N, step, /*c*/1, /*k*/0, /*i*/2) == 5.0,
              "taperedSample window+taper", taperedSample(sig.data(), sig.size(), tf.data(), N, step, 1, 0, 2), 5.0);
        check(taperedSample(sig.data(), sig.size(), tf.data(), N, step, 0, 1, 1) == 1.0,
              "taperedSample applies taper k", taperedSample(sig.data(), sig.size(), tf.data(), N, step, 0, 1, 1), 1.0);
        check(taperedSample(sig.data(), sig.size(), tf.data(), N, step, 0, 0, /*i>=N*/N) == 0.0,
              "taperedSample zero-pads", 0, 0);
        check(taperedSample(sig.data(), sig.size(), tf.data(), N, step, /*c far*/9, 0, 0) == 0.0,
              "taperedSample guards out-of-range", 0, 0);

        // psdValue: weighted magnitude sum, divide by wsum*fs, interior x2.
        const int Kp = 2, nb = 5, halfp = 4;
        std::vector<double> wts = {2.0, 1.0};
        const double wsum = 3.0, fs = 100.0;
        // spectra interleaved re,im for (c=0): two tapers x nb bins.
        std::vector<double> spec(2 * (size_t)Kp * nb, 0.0);
        // bin b=2 (interior): taper0 (3,4)->25, taper1 (1,0)->1
        spec[2 * (0 * nb + 2) + 0] = 3; spec[2 * (0 * nb + 2) + 1] = 4;
        spec[2 * (1 * nb + 2) + 0] = 1; spec[2 * (1 * nb + 2) + 1] = 0;
        double got = psdValue(spec.data(), wts.data(), wsum, fs, nb, halfp, Kp, 0, 2);
        double expect = ((2.0 * 25.0 + 1.0 * 1.0) / (wsum * fs)) * 2.0; // interior doubled
        check(std::abs(got - expect) < 1e-12, "psdValue weighting+norm+interior", got, expect);
        // bin 0 (not doubled)
        spec[2 * (0 * nb + 0) + 0] = 2; // taper0 re=2 -> 4
        double got0 = psdValue(spec.data(), wts.data(), wsum, fs, nb, halfp, Kp, 0, 0);
        double expect0 = (2.0 * 4.0) / (wsum * fs); // only taper0 nonzero, not doubled
        check(std::abs(got0 - expect0) < 1e-12, "psdValue DC bin not doubled", got0, expect0);

        // chunkRange must tile [0,nCols) exactly for the streamed pipeline.
        bool tile = true;
        for (int nc : {1, 2, 3, 4, 5, 8}) {
            for (int total : {0, 1, 2, 3, 7, 16, 17, 100}) {
                int next = 0; long sum = 0;
                for (int s = 0; s < nc; ++s) {
                    int st, cnt; chunkRange(nc, total, s, st, cnt);
                    if (cnt < 0 || st != next) tile = false;
                    next += cnt; sum += cnt;
                }
                if (next != total || sum != total) tile = false;
            }
        }
        check(tile, "chunkRange tiles windows exactly", 0, 0);
    }

    // ---- Spectral window centring (trace -> spectral) --------------------
    {
        // Locked: returns the trace window unchanged.
        TimeWindow a = spectralWindow(1000, 400, /*lock*/true, /*span*/2000, /*rec*/100000);
        check(a.start == 1000 && a.width == 400, "window locked = trace window", a.width, 400);

        // Unlocked wider span: same centre, wider width.
        // trace [1000,1400) centre 1200; span 800 -> [800,1600), centre 1200.
        TimeWindow b = spectralWindow(1000, 400, false, 800, 100000);
        check(b.width == 800 && (b.start + b.width / 2) == 1200,
              "window unlocked keeps centre", b.start + b.width / 2, 1200);

        // Clamp at the start: centre near 0 can't go negative.
        TimeWindow c = spectralWindow(100, 200, false, 2000, 100000);
        check(c.start == 0 && c.width == 2000, "window clamps at start", c.start, 0);

        // Clamp at the end: window pushed back to fit the recording.
        TimeWindow d = spectralWindow(99000, 400, false, 4000, 100000);
        check(d.start + d.width <= 100000 && d.width == 4000, "window clamps at end",
              d.start + d.width, 100000);

        // span <= 0 follows the trace width even when unlocked.
        TimeWindow e = spectralWindow(1000, 400, false, 0, 100000);
        check(e.width == 400, "window span<=0 follows trace", e.width, 400);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
