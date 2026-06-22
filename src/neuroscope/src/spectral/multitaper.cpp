#include "multitaper.h"
#include "realfft.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace neuroscope {
namespace spectral {

std::vector<double> psdFrequencies(int nfft, double fs)
{
    const int half = nfft / 2;
    std::vector<double> f(half + 1);
    for (int k = 0; k <= half; ++k)
        f[k] = static_cast<double>(k) * fs / static_cast<double>(nfft);
    return f;
}

std::vector<double> multitaperPsd(const double* x, int len,
                                  const DpssTapers& tapers,
                                  double fs, const RealFftPlan& plan,
                                  TaperWeighting weighting)
{
    const int N = tapers.N;
    const int K = tapers.K;
    const int nfft = plan.nfft();
    const int half = nfft / 2;
    std::vector<double> psd(half + 1, 0.0);
    if (!tapers.valid() || nfft < N || fs <= 0.0) return psd;

    const int useLen = std::min(len, N);

    // Sum of weights for normalisation.
    double wsum = 0.0;
    for (int k = 0; k < K; ++k)
        wsum += (weighting == TaperWeighting::Eigenvalue) ? tapers.lambda[k] : 1.0;
    if (wsum <= 0.0) wsum = static_cast<double>(K);

    std::vector<double> seg(nfft, 0.0);
    std::vector<double> pk;
    for (int k = 0; k < K; ++k) {
        const double wk = (weighting == TaperWeighting::Eigenvalue) ? tapers.lambda[k] : 1.0;
        const std::vector<double>& h = tapers.taper[k];
        std::fill(seg.begin(), seg.end(), 0.0);
        for (int i = 0; i < useLen; ++i) seg[i] = x[i] * h[i];
        plan.power(seg.data(), pk);
        for (int b = 0; b <= half; ++b) psd[b] += wk * pk[b];
    }

    // Normalise to a one-sided PSD: divide by the weight sum and fs, fold the
    // negative frequencies onto the interior bins.
    const double norm = 1.0 / (wsum * fs);
    for (int b = 0; b <= half; ++b) {
        double v = psd[b] * norm;
        if (b != 0 && b != half) v *= 2.0;
        psd[b] = v;
    }
    return psd;
}

std::vector<double> multitaperPsd(const double* x, int len,
                                  const DpssTapers& tapers,
                                  double fs, int nfft,
                                  TaperWeighting weighting)
{
    RealFftPlan plan(nfft);
    return multitaperPsd(x, len, tapers, fs, plan, weighting);
}

void multitaperSpectrogram(const double* signal, int n,
                           const DpssTapers& tapers,
                           double fs, int nfft, int step,
                           TaperWeighting weighting,
                           std::vector<std::vector<double>>& out)
{
    out.clear();
    const int N = tapers.N;
    if (!tapers.valid() || step <= 0 || n < N) return;

    const int nCols = 1 + (n - N) / step;
    out.assign(nCols, {});

    // One plan shared across all windows; power() is thread-safe.
    RealFftPlan plan(nfft);

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int c = 0; c < nCols; ++c) {
        const int start = c * step;
        out[c] = multitaperPsd(signal + start, N, tapers, fs, plan, weighting);
    }
}

namespace {

// Cyclic Jacobi eigen-decomposition of a small symmetric matrix a (n x n,
// row-major). On return eigenvalues are in w and eigenvectors are the
// columns of v (row-major n x n). n is small (channels in one group).
void jacobiSymmetric(std::vector<double>& a, int n,
                     std::vector<double>& w, std::vector<double>& v)
{
    v.assign(n * n, 0.0);
    for (int i = 0; i < n; ++i) v[i * n + i] = 1.0;

    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) off += a[p * n + q] * a[p * n + q];
        if (off < 1e-30) break;

        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                const double apq = a[p * n + q];
                if (std::abs(apq) < 1e-300) continue;
                const double app = a[p * n + p], aqq = a[q * n + q];
                const double phi = 0.5 * std::atan2(2.0 * apq, aqq - app);
                const double cs = std::cos(phi), sn = std::sin(phi);
                for (int i = 0; i < n; ++i) {
                    const double aip = a[i * n + p], aiq = a[i * n + q];
                    a[i * n + p] = cs * aip - sn * aiq;
                    a[i * n + q] = sn * aip + cs * aiq;
                }
                for (int i = 0; i < n; ++i) {
                    const double api = a[p * n + i], aqi = a[q * n + i];
                    a[p * n + i] = cs * api - sn * aqi;
                    a[q * n + i] = sn * api + cs * aqi;
                }
                for (int i = 0; i < n; ++i) {
                    const double vip = v[i * n + p], viq = v[i * n + q];
                    v[i * n + p] = cs * vip - sn * viq;
                    v[i * n + q] = sn * vip + cs * viq;
                }
            }
        }
    }
    w.resize(n);
    for (int i = 0; i < n; ++i) w[i] = a[i * n + i];
}

} // namespace

void commonWhiten(double* data, int nChannels, int nSamples, double eps)
{
    if (nChannels < 1 || nSamples < 2) return;

    // De-mean each channel.
    for (int c = 0; c < nChannels; ++c) {
        double m = 0.0;
        double* row = data + static_cast<std::size_t>(c) * nSamples;
        for (int s = 0; s < nSamples; ++s) m += row[s];
        m /= nSamples;
        for (int s = 0; s < nSamples; ++s) row[s] -= m;
    }
    if (nChannels == 1) return; // nothing to decorrelate

    // Covariance C = X X^T / (nSamples - 1).
    std::vector<double> cov(static_cast<std::size_t>(nChannels) * nChannels, 0.0);
    for (int a = 0; a < nChannels; ++a) {
        const double* ra = data + static_cast<std::size_t>(a) * nSamples;
        for (int b = a; b < nChannels; ++b) {
            const double* rb = data + static_cast<std::size_t>(b) * nSamples;
            double acc = 0.0;
            for (int s = 0; s < nSamples; ++s) acc += ra[s] * rb[s];
            acc /= (nSamples - 1);
            cov[a * nChannels + b] = acc;
            cov[b * nChannels + a] = acc;
        }
    }

    std::vector<double> w, v;
    jacobiSymmetric(cov, nChannels, w, v); // cov destroyed; v columns are eigenvectors

    // Regularised inverse square root scale per eigenvector.
    double maxw = 0.0;
    for (double x : w) maxw = std::max(maxw, x);
    const double floor = eps * (maxw > 0.0 ? maxw : 1.0);
    std::vector<double> invsqrt(nChannels);
    for (int i = 0; i < nChannels; ++i)
        invsqrt[i] = 1.0 / std::sqrt(std::max(w[i], floor));

    // ZCA whitening matrix Wm = V diag(invsqrt) V^T (symmetric).
    std::vector<double> Wm(static_cast<std::size_t>(nChannels) * nChannels, 0.0);
    for (int a = 0; a < nChannels; ++a)
        for (int b = 0; b < nChannels; ++b) {
            double acc = 0.0;
            for (int i = 0; i < nChannels; ++i)
                acc += v[a * nChannels + i] * invsqrt[i] * v[b * nChannels + i];
            Wm[a * nChannels + b] = acc;
        }

    // Apply per sample: x_w[:,s] = Wm * x[:,s]. Column-wise to bound memory.
    std::vector<double> col(nChannels), out(nChannels);
    for (int s = 0; s < nSamples; ++s) {
        for (int c = 0; c < nChannels; ++c)
            col[c] = data[static_cast<std::size_t>(c) * nSamples + s];
        for (int a = 0; a < nChannels; ++a) {
            double acc = 0.0;
            for (int b = 0; b < nChannels; ++b) acc += Wm[a * nChannels + b] * col[b];
            out[a] = acc;
        }
        for (int c = 0; c < nChannels; ++c)
            data[static_cast<std::size_t>(c) * nSamples + s] = out[c];
    }
}

} // namespace spectral
} // namespace neuroscope
