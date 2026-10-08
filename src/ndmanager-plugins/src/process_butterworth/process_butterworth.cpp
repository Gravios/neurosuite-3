/***************************************************************************
    process_butterworth.cpp  --  zero-phase Butterworth filter for multiplexed
                                 int16 recordings (.dat -> .fil)

    High-pass (default 800 Hz) and optional low-pass Butterworth sections,
    designed in double precision, run forward then backward (zero phase).
    Arithmetic in double or single precision (-p); by default double on the CPU
    and single on a GPU (consumer GPUs run double at a small fraction of single
    speed; on the reference data single changes 0.04% of samples by 1 LSB).  The
    computation is split into independent (channel, block) units with a settling
    margin (see butterworth_core.h), run with OpenMP on the CPU or one thread per
    unit on a CUDA GPU when one is available (-c forces the CPU).

    copyright  (C) 2026 neurosuite-3 contributors
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#ifdef _OPENMP
#  include <omp.h>
#endif

#include "butterworth_core.h"

using std::cerr;
using std::cout;
using std::endl;

#ifdef USE_CUDA
extern "C" int   bwCudaAvailable(int verbose);
extern "C" void* bwCudaInit(int useDouble, const double* sos, int nSec, int nCh,
                            long long maxChunkRows, long long maxOutRows, int M, int B);
extern "C" void  bwCudaChunk(void* handle, const short* chunk, long long row0,
                             long long rows, long long nRows, long long c0,
                             long long c1, short* out);
extern "C" void  bwCudaFree(void* handle);
#endif

static void usage(const char* name)
{
    cout << "usage: " << name << " [options] input output\n"
         << "Zero-phase Butterworth filter (forward + backward) of an int16 multiplexed file.\n"
         << " -n nChannels   number of channels (required)\n"
         << " -s rate        sampling rate in Hz (required)\n"
         << " -l Hz          high-pass corner (default 800; 0 = no high-pass)\n"
         << " -u Hz          low-pass corner (default 0 = no low-pass)\n"
         << " -o order       order of each Butterworth stage, 1-" << BW_MAX_SECTIONS
         << " sections in total (default 3;\n"
         << "                zero phase doubles the effective order; -6 dB at each corner)\n"
         << " -p auto|double|single  arithmetic precision (default auto: double on the\n"
         << "                CPU, single on a GPU)\n"
         << " -b bytes       input bytes per chunk (default 256 MiB)\n"
         << " -B samples     samples per parallel block (default: CPU 65536, GPU 8192)\n"
         << " -t nThreads    CPU threads (default: all cores)\n"
#ifdef USE_CUDA
         << " -c             force the CPU path\n"
#endif
         << " -v             verbose\n";
}

static bool readRows(int fd, short* dst, long long row, long long rows, int nCh)
{
    size_t want = (size_t)rows * nCh * sizeof(short);
    off_t off = (off_t)row * nCh * (off_t)sizeof(short);
    char* p = reinterpret_cast<char*>(dst);
    while (want > 0) {
        ssize_t r = pread(fd, p, want, off);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return false; }
        p += r; off += r; want -= (size_t)r;
    }
    return true;
}

static bool writeRows(int fd, const short* src, long long row, long long rows, int nCh)
{
    size_t want = (size_t)rows * nCh * sizeof(short);
    off_t off = (off_t)row * nCh * (off_t)sizeof(short);
    const char* p = reinterpret_cast<const char*>(src);
    while (want > 0) {
        ssize_t r = pwrite(fd, p, want, off);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return false; }
        p += r; off += r; want -= (size_t)r;
    }
    return true;
}

template <typename T>
static void cpuChunk(const BwCoefs<T>& c, const short* chunk, long long row0,
                     long long nRows, int nCh, long long c0, long long c1, int M,
                     long long B, short* out)
{
    const long long nBlk = (c1 - c0 + B - 1) / B;
    const long long nUnits = nBlk * nCh;
#pragma omp parallel
    {
        std::vector<T> scratch((size_t)(B + 2LL * M));
#pragma omp for schedule(dynamic, 1)
        for (long long u = 0; u < nUnits; ++u) {
            const int ch = (int)(u % nCh);
            const long long s = c0 + (u / nCh) * B;
            const long long e = (s + B < c1) ? s + B : c1;
            bwFilterBlock<T>(chunk, row0, nRows, nCh, ch, s, e, M, c,
                             scratch.data(), 1, out, c0);
        }
    }
}

int main(int argc, char* argv[])
{
    int nCh = 0, order = 3;
    double fs = 0.0, fLow = 800.0, fHigh = 0.0;
    int precision = -1;                       // -1 auto, 0 single, 1 double
    bool verbose = false, forceCPU = false;
    long long chunkBytes = 256LL << 20, B = 0;

    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { usage(argv[0]); exit(1); }
            return argv[++i];
        };
        if (a == "-h") { usage(argv[0]); return 0; }
        else if (a == "-n") nCh = atoi(next());
        else if (a == "-s") fs = atof(next());
        else if (a == "-l") fLow = atof(next());
        else if (a == "-u") fHigh = atof(next());
        else if (a == "-o") order = atoi(next());
        else if (a == "-p") {
            const std::string p = next();
            if (p == "double") precision = 1;
            else if (p == "single" || p == "float") precision = 0;
            else if (p == "auto") precision = -1;
            else { cerr << "error: -p must be auto, double or single" << endl; return 1; }
        }
        else if (a == "-b") chunkBytes = atoll(next());
        else if (a == "-B") B = atoll(next());
        else if (a == "-t") {
            const int t = atoi(next());
#ifdef _OPENMP
            if (t > 0) omp_set_num_threads(t);
#else
            (void)t;
#endif
        }
        else if (a == "-c") forceCPU = true;
        else if (a == "-v") verbose = true;
        else { cerr << "error: unknown option " << a << endl; usage(argv[0]); return 1; }
    }
    if (argc - i != 2) { usage(argv[0]); return 1; }
    const char* inPath = argv[i];
    const char* outPath = argv[i + 1];

    if (nCh <= 0) { cerr << "error: -n nChannels is required" << endl; return 1; }
    if (fs <= 0.0) { cerr << "error: -s samplingRate is required" << endl; return 1; }
    if (fLow < 0.0 || fHigh < 0.0 || fLow >= fs / 2 || fHigh >= fs / 2) {
        cerr << "error: corners must lie in [0, " << fs / 2 << ") Hz" << endl; return 1;
    }
    if (fLow == 0.0 && fHigh == 0.0) { cerr << "error: no filter requested (-l and -u are both 0)" << endl; return 1; }
    if (fHigh > 0.0 && fLow >= fHigh) { cerr << "error: high-pass corner must be below the low-pass corner" << endl; return 1; }
    const int nStages = (fLow > 0.0) + (fHigh > 0.0);
    if (order < 1 || nStages * ((order + 1) / 2) > BW_MAX_SECTIONS) {
        cerr << "error: order must be 1.." << (BW_MAX_SECTIONS * 2 / nStages) << endl; return 1;
    }

    std::vector<BwSection> sos;
    if (fLow > 0.0)  { auto h = bwDesign(order, fLow, fs, true);   sos.insert(sos.end(), h.begin(), h.end()); }
    if (fHigh > 0.0) { auto l = bwDesign(order, fHigh, fs, false); sos.insert(sos.end(), l.begin(), l.end()); }
    const int M = bwMargin(sos);

    const int inFd = open(inPath, O_RDONLY);
    if (inFd < 0) { cerr << "error: cannot open '" << inPath << "'" << endl; return 1; }
    struct stat st;
    fstat(inFd, &st);
    const long long nRows = (long long)st.st_size / ((long long)nCh * (long long)sizeof(short));
    if ((long long)st.st_size != nRows * nCh * (long long)sizeof(short))
        cerr << "warning: file size is not a whole number of " << nCh
             << "-channel samples; the trailing partial sample is dropped" << endl;
    if (nRows <= M) {
        cerr << "error: the recording (" << nRows << " samples) is shorter than the filter's "
             << "settling length (" << M << " samples)" << endl;
        return 1;
    }
    const int outFd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outFd < 0) { cerr << "error: cannot write '" << outPath << "'" << endl; return 1; }
#ifdef __linux__
    (void)posix_fallocate(outFd, 0, (off_t)nRows * nCh * (off_t)sizeof(short));
#endif

    bool gpu = false;
#ifdef USE_CUDA
    if (!forceCPU) gpu = bwCudaAvailable(verbose ? 1 : 0) != 0;
#else
    (void)forceCPU;
#endif
    const bool useDouble = precision < 0 ? !gpu : precision == 1;
    if (B <= 0) B = gpu ? 8192 : 65536;
    if (B < 16LL * M && !gpu) B = 16LL * M;     // keep the margin overhead small
    long long C = chunkBytes / ((long long)nCh * (long long)sizeof(short));
    if (C < 4 * B) C = 4 * B;
    C = (C / B) * B;
    if (C > nRows) C = nRows;

    if (verbose) {
        cout << "process_butterworth: " << nCh << " channels, " << nRows << " samples at "
             << fs << " Hz\n  filter: ";
        if (fLow > 0.0)  cout << "high-pass " << fLow << " Hz ";
        if (fHigh > 0.0) cout << "low-pass " << fHigh << " Hz ";
        cout << "order " << order << ", zero phase (" << sos.size() << " sections, effective order "
             << 2 * order << ")\n  precision: " << (useDouble ? "double" : "single")
             << ", settling margin " << M << " samples, block " << B << ", chunk " << C
             << " samples\n  path: " << (gpu ? "CUDA GPU" : "CPU")
#ifdef _OPENMP
             << (gpu ? "" : " (OpenMP " + std::to_string(omp_get_max_threads()) + " threads)")
#endif
             << endl;
        for (size_t k = 0; k < sos.size(); ++k)
            cout << "  sos[" << k << "] b = " << sos[k].b0 << " " << sos[k].b1 << " " << sos[k].b2
                 << "  a = 1 " << sos[k].a1 << " " << sos[k].a2 << "\n";
    }

    std::vector<short> in((size_t)(C + 2LL * M) * nCh), out((size_t)C * nCh);
    const BwCoefs<double> cd = bwToCoefs<double>(sos);
    const BwCoefs<float>  cf = bwToCoefs<float>(sos);
#ifdef USE_CUDA
    void* dev = nullptr;
    if (gpu) {
        std::vector<double> flat;
        for (auto& q : sos) { flat.push_back(q.b0); flat.push_back(q.b1); flat.push_back(q.b2);
                              flat.push_back(q.a1); flat.push_back(q.a2); }
        dev = bwCudaInit(useDouble ? 1 : 0, flat.data(), (int)sos.size(), nCh, C + 2LL * M, C, M, (int)B);
    }
#endif
    const long long nChunks = (nRows + C - 1) / C;
    for (long long k = 0; k < nChunks; ++k) {
        const long long c0 = k * C, c1 = (c0 + C < nRows) ? c0 + C : nRows;
        const long long r0 = (c0 - M > 0) ? c0 - M : 0;
        const long long r1 = (c1 + M < nRows) ? c1 + M : nRows;
        if (!readRows(inFd, in.data(), r0, r1 - r0, nCh)) { cerr << "error: read failed" << endl; return 1; }
#ifdef USE_CUDA
        if (gpu) bwCudaChunk(dev, in.data(), r0, r1 - r0, nRows, c0, c1, out.data());
        else
#endif
        if (useDouble) cpuChunk<double>(cd, in.data(), r0, nRows, nCh, c0, c1, M, B, out.data());
        else           cpuChunk<float>(cf, in.data(), r0, nRows, nCh, c0, c1, M, B, out.data());
        if (!writeRows(outFd, out.data(), c0, c1 - c0, nCh)) { cerr << "error: write failed" << endl; return 1; }
        if (verbose) { cout << "\r  chunk " << k + 1 << "/" << nChunks << std::flush; }
    }
    if (verbose) cout << endl;
#ifdef USE_CUDA
    if (dev) bwCudaFree(dev);
#endif
    close(inFd);
    if (close(outFd) != 0) { cerr << "error: closing output failed" << endl; return 1; }
    return 0;
}
