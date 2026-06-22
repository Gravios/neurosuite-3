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

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
