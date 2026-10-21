/***************************************************************************
    process_medianfilter.cpp  (CUDA-aware entry point)
    Dispatches to CUDA GPU path when compiled with USE_CUDA,
    falls back to OpenMP CPU path otherwise.
    The filter is defined (and tested, test/test_medianfilter.py) above
    slideWindow() below.
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include <iostream>
#include <neurosuite/core/fileio64.hpp>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef __linux__
#  include <fcntl.h>   // posix_fallocate
#endif
#include <string>
#include "progressbar.h"   // BlockProgress — stage progress on the terminal

using namespace std;

bool verbose = false;

// ---------------------------------------------------------------------------
// The filter, as the CPU computes it (and the Metal path reproduces):
//
//   out[t] = x[t] - median(x[t - w .. t + w])      (w = windowHalfLength)
//
// per channel, with x = 0 before the first and after the last frame of the
// recording, and the difference stored back in a short (wrapping, as it
// always has).  The data are filtered in chunks; each chunk is handed over
// with w frames of context on either side, so chunk boundaries do not show.
// ---------------------------------------------------------------------------

// Removes `oldV` from the sorted window and inserts `newV`, keeping it sorted.
static inline void slideWindow(short* win, int wLen, short oldV, short newV)
{
    if (newV == oldV) return;
    int idx = static_cast<int>(std::lower_bound(win, win + wLen, oldV) - win);
    if (newV > oldV) {
        while (idx + 1 < wLen && win[idx + 1] < newV) { win[idx] = win[idx + 1]; ++idx; }
    } else {
        while (idx > 0 && win[idx - 1] > newV) { win[idx] = win[idx - 1]; --idx; }
    }
    win[idx] = newV;
}

// Filters frames [0, nFrames) of one channel.  `data` points at frame 0 of
// an interleaved buffer that also holds frames -w .. -1 and nFrames ..
// nFrames + w - 1.
static void filterChannel(int windowHalfLength, const short* data, short* filtered,
                          long long nFrames, int nChannels, int channel)
{
    const int wLen = 2 * windowHalfLength + 1;
    std::vector<short> win(static_cast<std::size_t>(wLen));
    const short* x = data + channel;
    short* out = filtered + channel;
    auto at = [&](long long t) { return x[t * nChannels]; };

    for (int i = 0; i < wLen; ++i) win[i] = at(i - windowHalfLength);
    std::sort(win.begin(), win.end());
    out[0] = static_cast<short>(at(0) - win[windowHalfLength]);
    for (long long t = 1; t < nFrames; ++t) {
        slideWindow(win.data(), wLen, at(t - windowHalfLength - 1), at(t + windowHalfLength));
        out[t * nChannels] = static_cast<short>(at(t) - win[windowHalfLength]);
    }
}

// Filters one chunk on the CPU.  @p prog is moved from @p pos0 by @p span as
// the channels finish.
static void filterCPU(int windowHalfLength, const short* data, short* filtered,
                      long long nFrames, int nChannels, BlockProgress& prog,
                      long long pos0, long long span)
{
    int channelsDone = 0;
#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (int channel = 0; channel < nChannels; ++channel) {
        filterChannel(windowHalfLength, data, filtered, nFrames, nChannels, channel);
#ifdef _OPENMP
        #pragma omp critical(median_progress)
#endif
        {
            ++channelsDone;
            prog.setPosition(pos0 + span * channelsDone / nChannels);
        }
    }
}

// ---------------------------------------------------------------------------
// CUDA path (compiled in separately as process_medianfilter_cuda.cu)
// ---------------------------------------------------------------------------
#ifdef USE_CUDA
extern "C" void runCudaMedianFilter(const char* inputPath,
                                     const char* outputPath,
                                     int   nChannels,
                                     int   windowHalfLength,
                                     long int chunkSizeBytes,
                                     bool  verbose);
#endif

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
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
         << " -w nSamples    window half length\n"
         << " -b buffer      chunk size in bytes (default 512MB)\n"
         << " -t nThreads    CPU OpenMP threads (default: all cores)\n"
#ifdef USE_CUDA
         << " -c             force CPU path\n"
#endif
         << " -v             verbose mode\n";
    exit(0);
}

int main(int argc, char *argv[])
{
    short    sampleSize       = sizeof(short);
    short    windowHalfLength = 10;
    short    nChannels        = 1;
    // Default chunk size: 128 MB.  Large enough for good throughput, small
    // enough to leave RAM headroom for the overlap buffers and OS on machines
    // with typical 64-128 GB RAM and 256-channel recordings.
    // ndm_hipass does not pass -b, so this is the operative default.
    long int chunkSize        = 128L * 1024 * 1024;
    [[maybe_unused]] bool forceCPU = false;   // -c; read by the GPU builds

    int i = 0;
    while (true) {
        i++;
        if (i >= argc) error(argv[0]);
        if (argv[i][0] != '-') { if (i > argc - 2) error(argv[0]); break; }
        if (strlen(argv[i]) < 2) error(argv[0]);
        switch (argv[i][1]) {
            case 'h': help(argv[0]); break;
            case 'n': i++; nChannels        = (short)atoi(argv[i]); break;
            case 'b': i++; chunkSize        = atol(argv[i]); break;
            case 'v': verbose = true; break;
            case 'w': i++; windowHalfLength = (short)atoi(argv[i]); break;
            case 'c': forceCPU = true; break;
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

#ifdef USE_CUDA
    if (!forceCPU) {
        if (verbose) cout << "Using CUDA GPU path\n";
        runCudaMedianFilter(inputPath, outputPath, nChannels,
                            windowHalfLength, chunkSize, verbose);
        return 0;
    }
#endif

    if (verbose) cout << "Using CPU"
#ifdef _OPENMP
        << " (OpenMP, " << omp_get_max_threads() << " threads)"
#endif
        << " path\n";

    FILE* inputFile  = fopen(inputPath,  "rb");
    if (!inputFile)  { cerr << "error: cannot open '"  << inputPath  << "'\n"; exit(1); }
    FILE* outputFile = fopen(outputPath, "wb");
    if (!outputFile) { cerr << "error: cannot write '" << outputPath << "'\n"; exit(1); }

    neurosuite::core::fseek64(inputFile, 0, SEEK_END);
    long long int size = neurosuite::core::ftell64(inputFile);
    neurosuite::core::fseek64(inputFile, 0, SEEK_SET);

    // Preallocate the output file to the exact input size.
    // On Linux this uses fallocate(2) to reserve contiguous extents, avoiding
    // repeated filesystem metadata updates and fragmentation during streaming
    // writes.  The call is best-effort: failure is silently ignored so the
    // binary works on filesystems that do not support fallocate (e.g. tmpfs).
#ifdef __linux__
    {
        int outFd = fileno(outputFile);
        if (outFd >= 0)
            (void)posix_fallocate(outFd, 0, (std::int64_t)size);
    }
#endif

    // Whole frames are filtered; a trailing partial frame (a file whose size
    // is not a multiple of the frame) is copied through unchanged.
    const long long frameBytes = static_cast<long long>(nChannels) * sampleSize;
    const long long nFrames = size / frameBytes;
    const long long tailBytes = size - nFrames * frameBytes;
    const long long w = windowHalfLength;
    long long chunkFrames = chunkSize / frameBytes;
    if (chunkFrames < 1) chunkFrames = 1;
    if (chunkFrames > nFrames && nFrames > 0) chunkFrames = nFrames;

    // input: w frames of context, the chunk, w frames of context.
    std::vector<short> input(static_cast<std::size_t>((chunkFrames + 2 * w) * nChannels));
    std::vector<short> output(static_cast<std::size_t>(chunkFrames * nChannels));

    // Stage progress on the terminal (drawn to /dev/tty, so it shows under the
    // wrapper's output capture), by bytes of the input.
    std::string label = outputPath;
    if (label.size() > 4 && label.compare(label.size() - 4, 4, ".fil") == 0)
        label.resize(label.size() - 4);
    BlockProgress prog(label);
    prog.beginStage("MEDIAN", size);

    for (long long f0 = 0; f0 < nFrames; f0 += chunkFrames) {
        const long long nf = std::min(chunkFrames, nFrames - f0);
        // Frames f0 - w .. f0 + nf + w - 1, zero outside the recording.
        const long long first = std::max(0LL, f0 - w);
        const long long last = std::min(nFrames, f0 + nf + w);     // exclusive
        std::fill(input.begin(), input.end(), short(0));
        short* dst = input.data() + (first - (f0 - w)) * nChannels;
        neurosuite::core::fseek64(inputFile, first * frameBytes, SEEK_SET);
        const std::size_t want = static_cast<std::size_t>((last - first) * nChannels);
        if (fread(dst, sizeof(short), want, inputFile) != want) {
            cerr << "error: cannot read '" << inputPath << "'\n";
            exit(1);
        }
        filterCPU(windowHalfLength, input.data() + w * nChannels, output.data(), nf,
                  nChannels, prog, f0 * frameBytes, nf * frameBytes);
        fwrite(output.data(), sizeof(short), static_cast<std::size_t>(nf * nChannels), outputFile);
    }
    if (tailBytes > 0) {
        std::vector<char> tail(static_cast<std::size_t>(tailBytes));
        neurosuite::core::fseek64(inputFile, nFrames * frameBytes, SEEK_SET);
        if (fread(tail.data(), 1, tail.size(), inputFile) == tail.size())
            fwrite(tail.data(), 1, tail.size(), outputFile);
    }

    fclose(inputFile);
    fclose(outputFile);
    prog.endStage();
    prog.finish();
    return 0;
}
