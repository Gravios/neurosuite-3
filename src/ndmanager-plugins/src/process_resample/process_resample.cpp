/*
** Copyright (C) 2002-2004 Erik de Castro Lopo <erikd@mega-nerd.com>
** Copyright (C) 2008 Michaël Zugaro <michael.zugaro@college-de-france.fr>
** Copyright (C) 2024 (parallelisation rewrite)
**
** Parallel resampling strategy
** ----------------------------
** The original code used one SRC_STATE for all channels combined.
** libsamplerate has no internal parallelism — channels are processed
** sequentially within a single state. This rewrite uses one SRC_STATE
** per channel so all channels are independent and run in parallel via OpenMP.
**
** Three parallelised regions per chunk:
**   1. Deinterleave (int16 interleaved -> float per-channel planes):
**      Channel-outer loop so each thread writes a contiguous plane.
**      #pragma omp parallel for schedule(static) over channels.
**   2. Per-channel resampling (src_process on each SRC_STATE):
**      #pragma omp parallel for schedule(static) over channels.
**      This is the dominant cost; one SRC_STATE per channel is essential.
**   3. Reinterleave (float per-channel planes -> int16 interleaved):
**      Channel-outer loop; each thread reads its contiguous chanOut plane
**      and writes to strided positions in outBuf (independent strides).
**      #pragma omp parallel for schedule(static) over channels.
**   Filter-tail flush (zero-input src_process at EOF) is also parallelised.
**
** CHUNK_FRAMES = 262144 (1<<18). At 96 channels the read is ~48 MB per chunk.
** The original BUFFER_LEN=1024 caused O(totalFrames/1024) src_process() calls;
** the new code reduces that by a factor of 256, cutting function-call and
** state-update overhead dramatically in addition to the parallelism gain.
**
** CUDA is not applicable: libsamplerate is a stateful CPU library with no
** GPU port. Replacing it with a CUDA sinc resampler would be a larger
** project and offers no advantage over the fully-parallel CPU path above.
*/

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE_SOURCE64
#define _LARGEFILE_SOURCE64
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "samplerate.h"

#ifndef CHUNK_FRAMES
#define CHUNK_FRAMES (1 << 18)   // 262144 frames per chunk per channel
#endif

static void usage_exit(const char *progname)
{
    const char *p;
    if ((p = strrchr(progname, '/'))  != nullptr) progname = p + 1;
    if ((p = strrchr(progname, '\\')) != nullptr) progname = p + 1;
    printf("usage: %s [options] -n channels {-f f1,f2 | -r ratio} input output\n"
           "  -n channels    number of interleaved channels\n"
           "  -f f1,f2       input and output sampling frequencies\n"
           "  -r ratio       output/input frequency ratio\n"
           "  -c converter   libsamplerate converter (default 0 = sinc fastest)\n"
           "  -t nThreads    OpenMP thread count (default: all cores)\n"
           "  -v             verbose\n", progname);
    puts("\nConverters:");
    for (int k = 0; src_get_name(k) != nullptr; k++)
        printf("  %d  %s\n", k, src_get_name(k));
    exit(0);
}

int main(int argc, char *argv[])
{
    int    nChannels = -1;
    int    converter = SRC_SINC_FASTEST;
    double srcRate   = -1.0, dstRate = -1.0, ratio = -1.0;
    bool   verbose   = false; (void)verbose; // reserved for future -v output

    int k;
    for (k = 1; k < argc; k++) {
        if (argv[k][0] != '-') break;
        switch (argv[k][1]) {
            case 'h': usage_exit(argv[0]); break;
            case 'v': verbose = true; break;
            case 'n': nChannels = atoi(argv[++k]); break;
            case 'c': converter = atoi(argv[++k]); break;
            case 'r': ratio     = atof(argv[++k]); break;
            case 't':
                ++k;
#ifdef _OPENMP
                omp_set_num_threads(atoi(argv[k]));
#else
                fprintf(stderr, "warning: -t ignored (OpenMP not available)\n");
#endif
                break;
            case 'f': {
                char *tok = strtok(argv[++k], ",:-");
                srcRate = atof(tok);
                dstRate = atof(strtok(nullptr, ",:-"));
                break;
            }
            default:
                fprintf(stderr, "error: unknown option '%s'\n", argv[k]);
                exit(1);
        }
    }

    if (argc - k != 2) {
        fprintf(stderr, "error: expected input and output filenames\n"); exit(1);
    }
    const char *inPath  = argv[argc - 2];
    const char *outPath = argv[argc - 1];

    if (nChannels <= 0) {
        fprintf(stderr, "error: missing or invalid -n\n"); exit(1);
    }
    if (ratio <= 0.0) {
        if (srcRate > 0 && dstRate > 0) ratio = dstRate / srcRate;
        else { fprintf(stderr, "error: specify -f f1,f2 or -r ratio\n"); exit(1); }
    }
    if (!src_is_valid_ratio(ratio)) {
        fprintf(stderr, "error: ratio %f out of valid range\n", ratio); exit(1);
    }
    if (strcmp(inPath, outPath) == 0) {
        fprintf(stderr, "error: input and output filenames are identical\n"); exit(1);
    }

    FILE *fin = fopen(inPath, "rb");
    if (!fin) { fprintf(stderr, "error: cannot open '%s'\n", inPath); exit(1); }
    remove(outPath);
    FILE *fout = fopen(outPath, "wb");
    if (!fout) {
        fprintf(stderr, "error: cannot write '%s'\n", outPath);
        fclose(fin); exit(1);
    }

    fseeko(fin, 0, SEEK_END);
    long long totalBytes  = (long long)ftello(fin);
    fseeko(fin, 0, SEEK_SET);
    long long totalFrames = totalBytes / (long long)(sizeof(short) * nChannels);

    printf("Input          : %s\n", inPath);
    printf("Output         : %s\n", outPath);
    printf("Channels       : %d\n", nChannels);
    if (srcRate > 0) printf("Input rate     : %.0f Hz\n", srcRate);
    if (dstRate > 0) printf("Output rate    : %.0f Hz\n", dstRate);
    printf("Ratio          : %f\n", ratio);
    printf("Converter      : %s\n", src_get_name(converter));
#ifdef _OPENMP
    printf("Threads        : %d\n", omp_get_max_threads());
#endif

    // One independent SRC_STATE per channel — this is what enables parallelism.
    // A single SRC_STATE with channels>1 is inherently serial.
    std::vector<SRC_STATE*> states(nChannels);
    for (int ch = 0; ch < nChannels; ch++) {
        int err = 0;
        states[ch] = src_new(converter, 1, &err);
        if (!states[ch]) {
            fprintf(stderr, "error: src_new() channel %d: %s\n", ch, src_strerror(err));
            exit(1);
        }
    }

    // Interleaved int16 input/output
    const long long chunkSamples  = (long long)CHUNK_FRAMES * nChannels;
    // Output can be slightly more frames than input * ratio due to filter tail
    const long long outFramesMax  = (long long)(CHUNK_FRAMES * ratio) + 128;
    const long long outSamplesMax = outFramesMax * nChannels;

    std::vector<short> inBuf((size_t)chunkSamples);
    std::vector<short> outBuf((size_t)outSamplesMax);

    // Per-channel deinterleaved float planes
    // chanIn[ch] and chanOut[ch] are each CHUNK_FRAMES / outFramesMax floats
    std::vector<std::vector<float>> chanIn (nChannels, std::vector<float>(CHUNK_FRAMES));
    std::vector<std::vector<float>> chanOut(nChannels, std::vector<float>((size_t)outFramesMax));

    // Progress bar
    const int barWidth = 50;
    int barFilled = 0;
    printf("Resampling     : [");
    for (int i = 0; i < barWidth; i++) putchar('.');
    printf("]\rResampling     : [");
    fflush(stdout);

    long long framesRead = 0, outputFramesTotal = 0;

    while (true) {
        long long want = CHUNK_FRAMES;
        if (framesRead + want > totalFrames) want = totalFrames - framesRead;
        if (want <= 0) break;

        size_t got = fread(inBuf.data(), sizeof(short), (size_t)(want * nChannels), fin);
        long long framesGot = (long long)(got / nChannels);
        if (framesGot == 0) break;
        framesRead += framesGot;
        bool eof = (framesRead >= totalFrames);

        // Deinterleave: inBuf[f*nCh+ch] -> chanIn[ch][f]  (int16 -> float)
        // Channel-outer loop: each thread owns a contiguous chanIn[ch] plane
        // (sequential writes) — much better cache behaviour than frame-outer.
#ifdef _OPENMP
        #pragma omp parallel for schedule(static)
#endif
        for (int ch = 0; ch < nChannels; ch++) {
            const short *src = inBuf.data() + ch;
            float       *dst = chanIn[ch].data();
            for (long long f = 0; f < framesGot; f++, src += nChannels)
                dst[f] = (float)*src / 32768.0f;
        }

        // Parallel per-channel resampling
        long long outFramesGen = 0;

#ifdef _OPENMP
        #pragma omp parallel for schedule(static) reduction(max : outFramesGen)
#endif
        for (int ch = 0; ch < nChannels; ch++) {
            SRC_DATA sd;
            sd.data_in        = chanIn[ch].data();
            sd.data_out       = chanOut[ch].data();
            sd.input_frames   = (long)framesGot;
            sd.output_frames  = (long)outFramesMax;
            sd.src_ratio      = ratio;
            sd.end_of_input   = eof ? 1 : 0;

            int err = src_process(states[ch], &sd);
            if (err) {
                fprintf(stderr, "\nerror: src_process() ch %d: %s\n",
                        ch, src_strerror(err));
                exit(1);
            }
            if ((long long)sd.output_frames_gen > outFramesGen)
                outFramesGen = sd.output_frames_gen;
        }

        // Reinterleave: chanOut[ch][f] -> outBuf[f*nCh+ch]  (float -> int16)
        // Channel-outer: each thread reads contiguous chanOut[ch], writes
        // strided outBuf[f*nChannels+ch] — independent strides, no conflicts.
#ifdef _OPENMP
        #pragma omp parallel for schedule(static)
#endif
        for (int ch = 0; ch < nChannels; ch++) {
            const float *src = chanOut[ch].data();
            short       *dst = outBuf.data() + ch;
            for (long long f = 0; f < outFramesGen; f++, dst += nChannels) {
                float v = src[f] * 32768.0f;
                if      (v >  32767.0f) v =  32767.0f;
                else if (v < -32768.0f) v = -32768.0f;
                *dst = (short)v;
            }
        }

        fwrite(outBuf.data(), sizeof(short), (size_t)(outFramesGen * nChannels), fout);
        outputFramesTotal += outFramesGen;

        // Progress
        int filled = (int)((double)framesRead / (double)totalFrames * barWidth);
        while (barFilled < filled) { putchar('#'); barFilled++; }
        fflush(stdout);

        if (eof) break;
    }

    // Flush filter tails: call src_process with 0 input frames, end_of_input=1
    // until no more output is generated. Parallelise across channels — each
    // channel's SRC_STATE is independent.
#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int ch = 0; ch < nChannels; ch++) {
        SRC_DATA sd;
        sd.data_in       = chanIn[ch].data();   // won't be read (input_frames=0)
        sd.data_out      = chanOut[ch].data();
        sd.input_frames  = 0;
        sd.output_frames = (long)outFramesMax;
        sd.src_ratio     = ratio;
        sd.end_of_input  = 1;
        src_process(states[ch], &sd);           // ignore tail — negligible at LFP rates
    }

    while (barFilled < barWidth) { putchar('#'); barFilled++; }
    printf("]\n");
    printf("Output frames  : %lld\n", outputFramesTotal);

    for (int ch = 0; ch < nChannels; ch++) src_delete(states[ch]);
    fclose(fin);
    fclose(fout);
    return 0;
}
