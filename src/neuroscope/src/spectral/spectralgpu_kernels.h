#ifndef NEUROSCOPE_SPECTRAL_SPECTRALGPU_KERNELS_H
#define NEUROSCOPE_SPECTRAL_SPECTRALGPU_KERNELS_H

// Per-element arithmetic for the GPU multitaper kernels, written once and
// shared between the device (the .cu kernels call these) and the host (the
// self-test calls them directly). Marking them __host__ __device__ when
// compiled by nvcc, and plain inline otherwise, means the exact code that runs
// on the GPU is the code the CPU test validates — so the index math, tapering
// and reduction are checked even though the kernel launches themselves are not
// built in CPU-only CI.

#ifdef __CUDACC__
#define NS_SPECTRAL_HD __host__ __device__
#else
#define NS_SPECTRAL_HD
#endif

namespace neuroscope {
namespace spectral {
namespace gpu {
namespace kern {

// Partition the nCols windows into nChunks contiguous ranges for the streamed
// pipeline (one chunk per CUDA stream). The first (nCols % nChunks) chunks get
// one extra column so the ranges tile [0, nCols) exactly with no gaps/overlap.
// Host-only (no device need) but kept here so the self-test validates the
// bookkeeping that the streamed kernels rely on.
inline void chunkRange(int nChunks, int nCols, int s, int& start, int& count)
{
    if (nChunks < 1) nChunks = 1;
    const int base = nCols / nChunks;
    const int rem  = nCols % nChunks;
    if (s < rem) { start = s * (base + 1);            count = base + 1; }
    else         { start = rem * (base + 1) + (s - rem) * base; count = base; }
}

// Decompose a tapering thread id into (window c, taper k, sample i).
// Segment layout is (c*K + k) consecutive blocks of nfft samples.
NS_SPECTRAL_HD inline void decodeSeg(long gid, int nfft, int K,
                                     int& c, int& k, int& i)
{
    i = static_cast<int>(gid % nfft);
    const long seg = gid / nfft;        // segment index = c*K + k
    c = static_cast<int>(seg / K);
    k = static_cast<int>(seg % K);
}

// Decompose a reduction thread id into (window c, frequency bin b).
NS_SPECTRAL_HD inline void decodePsd(long gid, int nbins, int& c, int& b)
{
    b = static_cast<int>(gid % nbins);
    c = static_cast<int>(gid / nbins);
}

// One sample of the tapered, zero-padded segment for (window c, taper k, i).
// tapersFlat is K*N row-major (taper k at offset k*N). Out-of-window samples
// and the zero-pad region return 0.
NS_SPECTRAL_HD inline double taperedSample(const double* signal, int sigLen,
                                           const double* tapersFlat, int N,
                                           int step, int c, int k, int i)
{
    if (i >= N) return 0.0;
    const long s = static_cast<long>(c) * step + i;
    if (s < 0 || s >= sigLen) return 0.0;
    return signal[s] * tapersFlat[static_cast<long>(k) * N + i];
}

// One-sided PSD value for (window c, bin b) from the per-taper complex spectra.
// spectra is interleaved re,im (cufftDoubleComplex reinterpreted as double),
// laid out as ((c*K + k)*nbins + b). Matches the CPU multitaperPsd: eigenvalue
// (or uniform) weighting, divide by wsum*fs, and double the interior bins.
NS_SPECTRAL_HD inline double psdValue(const double* spectraInterleaved,
                                      const double* w, double wsum, double fs,
                                      int nbins, int half, int K, int c, int b)
{
    double acc = 0.0;
    for (int k = 0; k < K; ++k) {
        const long idx = (static_cast<long>(c) * K + k) * nbins + b;
        const double re = spectraInterleaved[2 * idx];
        const double im = spectraInterleaved[2 * idx + 1];
        acc += w[k] * (re * re + im * im);
    }
    double v = acc / (wsum * fs);
    if (b != 0 && b != half) v *= 2.0;
    return v;
}

} // namespace kern
} // namespace gpu
} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_SPECTRALGPU_KERNELS_H
