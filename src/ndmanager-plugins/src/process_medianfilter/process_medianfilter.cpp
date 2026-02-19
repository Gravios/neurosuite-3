/***************************************************************************
    process_medianfilter.cpp  (CUDA-aware entry point)
    Dispatches to CUDA GPU path when compiled with USE_CUDA,
    falls back to OpenMP CPU path otherwise.
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace std;

bool verbose = false;

// ---------------------------------------------------------------------------
// CPU: per-channel sliding sorted-window median filter
// ---------------------------------------------------------------------------
static void filterChannel(short windowHalfLength, const short* widebandData,
                           short* filteredData, long int nSamplesPerChannel,
                           int nChannels, int channel)
{
    const int windowLength = 2 * windowHalfLength + 1;
    vector<short> window(windowLength);
    const int halfOffset = nChannels * windowHalfLength;
    const int offset     = nChannels * windowLength;

    const short* input = widebandData + channel - halfOffset;
    for (int i = 0; i < windowLength; ++i, input += nChannels)
        window[i] = *input;
    sort(window.begin(), window.end());

    input  = widebandData + channel + halfOffset + nChannels;
    short* output = filteredData + channel;

    for (long int samplesLeft = nSamplesPerChannel * nChannels;
         samplesLeft > 0;
         samplesLeft -= nChannels, input += nChannels, output += nChannels)
    {
        const short newVal = *input;
        const short oldVal = *(input - offset);
        if (newVal != oldVal) {
            int lo = 0, hi = windowLength - 1, idx = 0;
            while (lo <= hi) {
                idx = (lo + hi) / 2;
                if      (oldVal > window[idx]) lo = idx + 1;
                else if (oldVal < window[idx]) hi = idx - 1;
                else break;
            }
            if (newVal > oldVal) {
                while (newVal > window[idx] && idx < windowLength - 1) {
                    window[idx] = window[idx + 1]; ++idx;
                }
                if (newVal > window[idx]) window[idx] = newVal;
                else                      window[idx - 1] = newVal;
            } else {
                while (newVal < window[idx] && idx > 0) {
                    window[idx] = window[idx - 1]; --idx;
                }
                if (newVal < window[idx]) window[idx] = newVal;
                else                      window[idx + 1] = newVal;
            }
        }
        *output = *(input - halfOffset - nChannels) - window[windowHalfLength];
    }
}

static void filterCPU(short windowHalfLength, short* widebandData,
                       short* filteredData, long int nSamplesPerChannel,
                       short nChannels = 1)
{
    if (verbose) {
        for (int i = 0; i < nChannels; ++i) cout << ".";
        for (int i = 0; i < nChannels; ++i) cout << "\b";
        cout << flush;
    }
#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (int channel = 0; channel < nChannels; ++channel) {
        filterChannel(windowHalfLength, widebandData, filteredData,
                      nSamplesPerChannel, nChannels, channel);
        if (verbose) {
#ifdef _OPENMP
            #pragma omp critical
#endif
            cout << "#" << flush;
        }
    }
    if (verbose) cout << endl;
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
    long int chunkSize        = 512L * 1024 * 1024;
    bool     forceCPU         = false;

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

    fseeko(inputFile, 0, SEEK_END);
    long long int size = ftello(inputFile);
    fseeko(inputFile, 0, SEEK_SET);

    long long int nSamples = size / sampleSize;
    long long int nSamplesPerChannel = nSamples / nChannels;
    int nSamplesPerChunkPerChannel = (int)(chunkSize / sampleSize / nChannels);
    chunkSize = (long int)nSamplesPerChunkPerChannel * sampleSize * nChannels;
    int nSamplesPerChunk = (int)(chunkSize / sampleSize);

    int      nPaddingSamples = nChannels * windowHalfLength;
    int      nOverlapSamples = nChannels * windowHalfLength;
    long int overlapSize     = nOverlapSamples * sampleSize;
    short   *input, *output;

    if (nSamplesPerChunk >= nSamples) {
        input  = new short[nPaddingSamples + nSamples];
        memset(input, 0, nPaddingSamples * sizeof(short));
        output = new short[nSamples];
        fread((char*)&input[nPaddingSamples], sizeof(char), (size_t)size, inputFile);
        filterCPU(windowHalfLength, &input[nPaddingSamples], output,
                  nSamplesPerChannel, nChannels);
        fwrite((char*)output, sizeof(char), (size_t)size, outputFile);
    } else {
        long long int chunk = 1, nChunks = size / chunkSize;
        if (nChunks * chunkSize < size) nChunks++;
        input  = new short[nSamplesPerChunk + 2 * nOverlapSamples];
        output = new short[nSamplesPerChunk];
        for (long long int sizeLeft = size; sizeLeft > 0; sizeLeft -= chunkSize, ++chunk) {
            if (sizeLeft <= chunkSize) {
                chunkSize = sizeLeft;
                nSamplesPerChunkPerChannel = (int)(sizeLeft / nChannels / sampleSize);
            }
            if (verbose) cout << "Chunk " << chunk << "/" << nChunks << " ";
            if (sizeLeft == size) {
                fread((char*)&input[nOverlapSamples], sizeof(char),
                      (size_t)(chunkSize + overlapSize), inputFile);
                filterCPU(windowHalfLength, &input[nOverlapSamples], output,
                          nSamplesPerChunkPerChannel, nChannels);
            } else {
                memcpy(input, &input[nSamplesPerChunk], (size_t)(2 * overlapSize));
                fread((char*)&input[2 * nOverlapSamples], sizeof(char),
                      (size_t)chunkSize, inputFile);
                filterCPU(windowHalfLength, &input[nOverlapSamples], output,
                          nSamplesPerChunkPerChannel, nChannels);
            }
            fwrite((char*)output, sizeof(char), (size_t)chunkSize, outputFile);
        }
    }

    delete[] input;
    delete[] output;
    fclose(inputFile);
    fclose(outputFile);
    if (verbose) cout << "\n";
    return 0;
}
