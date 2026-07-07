/***************************************************************************
    process_lowpass.cpp

    Linear-phase FIR low-pass filter for multiplexed int16 binary data.

    Used together with process_medianfilter (via ndm_bandpass) to turn the
    median high-pass .fil into a band-pass .fil: the median stage sets the
    LOWER edge while preserving spike-waveform shape, and this stage attenuates
    noise ABOVE the spike band.  A windowed-sinc (Hamming) FIR is used because
    it is symmetric -> linear phase -> constant group delay: the only timing
    effect is a fixed shift, which is compensated here by centring the kernel
    on the output sample, so spike waveforms are not distorted.

    CPU / OpenMP only.  Reads and writes int16, channel-interleaved, headerless
    (the same layout as .dat / .fil).  Chunked with per-chunk overlap read so
    memory use is bounded regardless of file size.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your option)
    any later version.
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef __linux__
#  include <fcntl.h>   // posix_fallocate
#endif

using namespace std;

static bool         verbose = false;
static const double PI      = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Windowed-sinc low-pass FIR kernel, Hamming window, unit DC gain.
//   fcn  = cutoff / samplingRate  (normalised; must be in (0, 0.5))
//   half = FIR half length; kernel has 2*half+1 taps (symmetric -> linear phase)
// ---------------------------------------------------------------------------
static vector<double> makeLowpassKernel(double fcn, int half)
{
    const int      taps = 2 * half + 1;
    vector<double> h(static_cast<size_t>(taps));
    double         sum = 0.0;
    for (int k = 0; k < taps; ++k) {
        const int    n    = k - half;
        const double sinc = (n == 0) ? 2.0 * fcn
                                     : sin(2.0 * PI * fcn * n) / (PI * n);
        const double win  = 0.54 - 0.46 * cos(2.0 * PI * k / (taps - 1)); // Hamming
        h[static_cast<size_t>(k)] = sinc * win;
        sum += h[static_cast<size_t>(k)];
    }
    for (double& v : h) v /= sum;   // normalise to unit gain at DC
    return h;
}

// ---------------------------------------------------------------------------
// Convolve one output chunk for one channel.  inbuf holds (len + 2*half)
// samples per channel, channel-interleaved; per-channel index (j + half) is the
// centre for output sample j, so the window for output j is inbuf[j .. j+2*half]
// (per channel).  Output is clamped to the int16 range.
// ---------------------------------------------------------------------------
static void convolveChannel(const double* kernel, int half, int nChannels,
                            int channel, long long len,
                            const short* inbuf, short* outbuf)
{
    const int taps = 2 * half + 1;
    for (long long j = 0; j < len; ++j) {
        const short* w   = inbuf + j * nChannels + channel;
        double       acc = 0.0;
        for (int k = 0; k < taps; ++k)
            acc += kernel[k] * static_cast<double>(w[static_cast<long long>(k) * nChannels]);
        long v = lround(acc);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        outbuf[j * nChannels + channel] = static_cast<short>(v);
    }
}

static void error(char* name)
{
    cerr << "usage: " << name
         << " [options] input output (type '" << name << " -h' for details)" << endl;
    exit(1);
}

static void help(char* name)
{
    cout << "usage: " << name << " [options] input output\n"
         << " -n nChannels   number of data channels\n"
         << " -f cutoffHz    low-pass cutoff frequency (Hz)\n"
         << " -s rateHz      sampling rate of the data (Hz)\n"
         << " -m nSamples    FIR half length (taps = 2*m+1, default 32)\n"
         << " -b buffer      chunk size in bytes (default 128MB)\n"
         << " -t nThreads    CPU OpenMP threads (default: all cores)\n"
         << " -v             verbose mode\n";
    exit(0);
}

int main(int argc, char* argv[])
{
    const short sampleSize   = sizeof(short);
    int         nChannels    = 1;
    double      cutoffHz     = 0.0;
    double      samplingRate = 0.0;
    int         half         = 32;
    long int    chunkSize    = 128L * 1024 * 1024;   // 128 MB

    int i = 0;
    while (true) {
        i++;
        if (i >= argc) error(argv[0]);
        if (argv[i][0] != '-') { if (i > argc - 2) error(argv[0]); break; }
        if (strlen(argv[i]) < 2) error(argv[0]);
        switch (argv[i][1]) {
            case 'h': help(argv[0]); break;
            case 'n': i++; nChannels    = atoi(argv[i]); break;
            case 'f': i++; cutoffHz     = atof(argv[i]); break;
            case 's': i++; samplingRate = atof(argv[i]); break;
            case 'm': i++; half         = atoi(argv[i]); break;
            case 'b': i++; chunkSize    = atol(argv[i]); break;
            case 'v': verbose = true; break;
            case 't':
                i++;
#ifdef _OPENMP
                omp_set_num_threads(atoi(argv[i]));
#else
                cerr << "warning: -t ignored (not compiled with OpenMP)\n";
#endif
                break;
            default: error(argv[0]); break;
        }
    }

    const char* inputPath  = argv[argc - 2];
    const char* outputPath = argv[argc - 1];

    if (nChannels < 1)       { cerr << "error: -n nChannels must be >= 1\n"; exit(1); }
    if (samplingRate <= 0.0) { cerr << "error: -s samplingRate (Hz) is required\n"; exit(1); }
    if (cutoffHz <= 0.0)     { cerr << "error: -f cutoffHz (Hz) is required\n"; exit(1); }
    if (half < 1)            { cerr << "error: -m half length must be >= 1\n"; exit(1); }

    const double fcn = cutoffHz / samplingRate;
    if (fcn >= 0.5) {
        cerr << "error: cutoff " << cutoffHz << " Hz >= Nyquist "
             << (samplingRate / 2.0) << " Hz; nothing to low-pass\n";
        exit(1);
    }

    const vector<double> kernel = makeLowpassKernel(fcn, half);

    if (verbose)
        cout << "process_lowpass: cutoff " << cutoffHz << " Hz @ " << samplingRate
             << " Hz, " << (2 * half + 1) << " taps"
#ifdef _OPENMP
             << ", OpenMP " << omp_get_max_threads() << " threads"
#endif
             << "\n";

    FILE* in  = fopen(inputPath,  "rb");
    if (!in)  { cerr << "error: cannot open '"  << inputPath  << "'\n"; exit(1); }
    FILE* out = fopen(outputPath, "wb");
    if (!out) { cerr << "error: cannot write '" << outputPath << "'\n"; exit(1); }

    fseeko(in, 0, SEEK_END);
    const long long fileBytes = ftello(in);
    fseeko(in, 0, SEEK_SET);

#ifdef __linux__
    { int fd = fileno(out); if (fd >= 0) (void)posix_fallocate(fd, 0, (off_t)fileBytes); }
#endif

    const long long nSamplesPerChannel = fileBytes / sampleSize / nChannels;
    long long       chunkSPC           = chunkSize / sampleSize / nChannels;
    if (chunkSPC < 1) chunkSPC = 1;
    const long long nChunks = (nSamplesPerChannel + chunkSPC - 1) / chunkSPC;

    vector<short> inbuf, outbuf;
    long long     chunkIdx = 0;

    for (long long start = 0; start < nSamplesPerChannel; start += chunkSPC) {
        const long long len      = min(chunkSPC, nSamplesPerChannel - start);
        const long long readFrom = start - half;           // may be negative
        const long long readLen  = len + 2 * half;         // samples per channel

        inbuf.assign(static_cast<size_t>(readLen * nChannels), 0);  // zero-padded edges

        const long long validFrom = max(static_cast<long long>(0), readFrom);
        const long long validTo   = min(nSamplesPerChannel, readFrom + readLen);
        if (validTo > validFrom) {
            const long long dstSPC = validFrom - readFrom;         // per-channel offset in inbuf
            fseeko(in, validFrom * nChannels * static_cast<long long>(sampleSize), SEEK_SET);
            const size_t want = static_cast<size_t>((validTo - validFrom) * nChannels);
            size_t got = fread(&inbuf[static_cast<size_t>(dstSPC * nChannels)],
                               sampleSize, want, in);
            (void)got;
        }

        outbuf.resize(static_cast<size_t>(len * nChannels));
        if (verbose) cout << "Chunk " << (++chunkIdx) << "/" << nChunks << " " << flush;
#ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic)
#endif
        for (int ch = 0; ch < nChannels; ++ch)
            convolveChannel(kernel.data(), half, nChannels, ch, len,
                            inbuf.data(), outbuf.data());

        fwrite(outbuf.data(), sampleSize, static_cast<size_t>(len * nChannels), out);
        if (verbose) cout << "#" << flush;
    }
    if (verbose) cout << "\n";

    fclose(in);
    fclose(out);
    return 0;
}
