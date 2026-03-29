// KlustaKwik.cpp — main entry point, parameter handling, and outer CEM loop.
//
// For a full description of changes from the original v1.7 release see
// CHANGES.md in this directory.

#include "KlustaKwik.h"
#include "KK.h"
#include "KlustaSave.h"
#include "KlustaKwikYaml.h"   // auto-detect spike params from YAML config

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <iostream>
#include <stdexcept>
#ifdef _OPENMP
#include <omp.h>
#endif

const double PI = 3.14159265358979323846;

char HelpString[] =
    "\nKlustaKwik\n\n"
    "Uses the CEM algorithm to do automatic clustering.\n\n";

// ---- Global parameter storage ---------------------------------------------
char  FileBase[STRLEN]       = "tetrode";
int   ElecNo                 = 1;
int   MinClusters            = 2;
int   MaxClusters            = 10;
int   MaxPossibleClusters    = 100;
int   nStarts                = 1;
int   RandomSeed             = 1;
char  Debug                  = 0;
int   Verbose                = 0;
char  UseFeatures[STRLEN]    = "all";  // auto-filled from nFeatures in .fet file if not overridden
int   DistDump               = 0;
float DistThresh             = static_cast<float>(std::log(1000.0));
int   FullStepEvery          = 10;
float ChangedThresh          = 0.05f;
char  Log                    = 0;
char  Screen                 = 0;
int   MaxIter                = 500;
char  StartCluFile[STRLEN]   = "";
float PenaltyMix             = 0.0f;
char  InitMethod[STRLEN]     = "farthest";  // "farthest" | "random"
int   TimeMergeIter          = 30;          // Phase 2 iterations; 0 = disabled

// Three-phase chunked CEM parameters
float ChunkMinutes           = 0.0f;    // 0 = disabled (use two-phase only)
float ChunkOverlapMinutes    = 0.0f;    // trailing overlap appended to next chunk; 0 = disabled
float ChunkPreseedFraction   = 0.0f;    // fraction of spikes for Phase 0 preseed; 0 = disabled
char  ChunkFile[STRLEN]      = "";      // path to .chunks.N boundary file; overrides ChunkMinutes
float SamplingRate           = 0.0f;    // samples/sec; auto-filled from YAML or defaults to 20000
float MergeThresh            = 30.0f;   // symmetric Mahalanobis² threshold for cluster matching
int   GlobalMergeIter        = 20;      // Phase 3 warm-start EM iterations
int   SaveIntermediates      = 1;       // 0 = suppress mid-run .clu writes; final write only
// Phase 1.5 waveform realignment (requires chunked mode)
// Both must be > 0 to enable realignment; 0 disables (default, safe for callers
// that do not pass .spk parameters, e.g. when running in two-phase-only mode).
int   NbChannels             = 0;       // channels per spike group (matches .spk layout)
int   NbSamplesPerSpike      = 0;       // samples per channel per spike in .spk file
int   NbBytesPerSample       = 2;       // bytes per sample: 2 for ≤16-bit, 4 for 32-bit
int   fSaveModel             = 1;
FILE *pModelFile             = nullptr;
int   SplitEvery             = 50;
FILE *logfp                  = nullptr;
FILE *Distfp                 = nullptr;
KlustaSave kSv;
float HugeScore              = 1e32f;

// ---------------------------------------------------------------------------
void SetupParams(int argc, char **argv) {
    char fname[STRLEN + 16];
    init_params(argc, argv);

    STRING_PARAM(FileBase);
    INT_PARAM(ElecNo);
    INT_PARAM(MinClusters);
    INT_PARAM(MaxClusters);
    INT_PARAM(MaxPossibleClusters);
    INT_PARAM(nStarts);
    INT_PARAM(RandomSeed);
    BOOLEAN_PARAM(Debug);
    INT_PARAM(Verbose);
    STRING_PARAM(UseFeatures);
    FLOAT_PARAM(PenaltyMix);
    STRING_PARAM(InitMethod);
    INT_PARAM(TimeMergeIter);
    FLOAT_PARAM(ChunkMinutes);
    FLOAT_PARAM(ChunkOverlapMinutes);
    FLOAT_PARAM(ChunkPreseedFraction);
    STRING_PARAM(ChunkFile);
    FLOAT_PARAM(SamplingRate);
    FLOAT_PARAM(MergeThresh);
    INT_PARAM(GlobalMergeIter);
    INT_PARAM(SaveIntermediates);
    INT_PARAM(NbChannels);
    INT_PARAM(NbSamplesPerSpike);
    INT_PARAM(NbBytesPerSample);
    INT_PARAM(DistDump);
    FLOAT_PARAM(DistThresh);
    INT_PARAM(FullStepEvery);
    FLOAT_PARAM(ChangedThresh);
    BOOLEAN_PARAM(Log);
    BOOLEAN_PARAM(Screen);
    INT_PARAM(MaxIter);
    STRING_PARAM(StartCluFile);
    INT_PARAM(fSaveModel);
    INT_PARAM(SplitEvery);

    // Clamp PenaltyMix to [0,1]
    PenaltyMix = std::max(0.0f, std::min(1.0f, PenaltyMix));

    if (argc < 3) {
        fprintf(stderr, "Usage: KlustaKwik FileBase ElecNo [Arguments]\n\n");
        fprintf(stderr, "Default Parameters:\n");
        print_params(stderr);
        exit(1);
    }

    strncpy(FileBase, argv[1], STRLEN - 1);
    ElecNo = atoi(argv[2]);

    if (Screen && Verbose) print_params(stdout);

    if (Log) {
        snprintf(fname, sizeof(fname), "%s.klg.%d", FileBase, ElecNo);
        logfp = fopen_safe(fname, "w");
        print_params(logfp);
    }
}

// ---------------------------------------------------------------------------
[[noreturn]] void Error(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
    std::abort();
}

void Output(const char *fmt, ...) {
    if (!Screen && !Log) return;
#ifdef _OPENMP
    #pragma omp critical(output_lock)
#endif
    {
        va_list arg;
        va_start(arg, fmt);
        if (Screen) vprintf(fmt, arg);
        va_end(arg);
        if (Log) {
            va_list arg2;
            va_start(arg2, fmt);
            vfprintf(logfp, fmt, arg2);
            va_end(arg2);
        }
    }
}

int irand(int min, int max) {
    return rand() % (max - min + 1) + min;
}

FILE *fopen_safe(const char *fname, const char *mode) {
    FILE *fp = fopen(fname, mode);
    if (!fp) {
        fprintf(stderr, "Could not open file %s\n", fname);
        std::abort();
    }
    return fp;
}

void MatPrint(FILE *fp, const float *Mat, int nRows, int nCols) {
    for (int i = 0; i < nRows; i++) {
        for (int j = 0; j < nCols; j++)
            fprintf(fp, "%.5g ", Mat[i * nCols + j]);
        fprintf(fp, "\n");
    }
}

// ---------------------------------------------------------------------------
// Cholesky decomposition — modernised, no temporary Array allocations
//
// In:  m_In  — upper triangle of symmetric positive-definite matrix (D×D)
// Out: m_Out — lower triangle L such that L * L^T = In
// Returns 0 on success, 1 if matrix is not positive definite.
//
// GPU note: called once per cluster per EStep on CPU.  With D≤17 this is
// ≈1700 flops — not worth GPU-ing.  The Cholesky result is then uploaded
// to device constant/shared memory for the EStep kernel.
// ---------------------------------------------------------------------------
int Cholesky(const float *m_In, float *m_Out, int D) {
    // Zero output
    std::fill(m_Out, m_Out + D * D, 0.0f);

    for (int i = 0; i < D; i++) {
        for (int j = i; j < D; j++) {
            float sum = m_In[i * D + j];
            for (int k = i - 1; k >= 0; k--)
                sum -= m_Out[i * D + k] * m_Out[j * D + k];
            if (i == j) {
                if (sum <= 0.0f) return 1;   // not positive definite
                m_Out[i * D + i] = std::sqrt(sum);
            } else {
                m_Out[j * D + i] = sum / m_Out[i * D + i];
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TriSolve — solve lower-triangular system M*Out = x
// No temporary allocations.  Inlined into EStep in KK.cpp for speed.
// ---------------------------------------------------------------------------
void TriSolve(const float *M, const float *x, float *Out, int D) {
    for (int i = 0; i < D; i++) {
        float s = x[i];
        for (int j = i - 1; j >= 0; j--) s -= M[i * D + j] * Out[j];
        Out[i] = s / M[i * D + i];
    }
}

// ---------------------------------------------------------------------------
// export_model — write cluster model to file (format unchanged)
// bestCholFlat lives on the KK instance, so we take K1 by reference.
// ---------------------------------------------------------------------------
void export_model(FILE *fp, KK& K1) {
    fprintf(fp, "%d %d %d\n", kSv.nDimsBest, kSv.nBestClustersAlive, kSv.cEStepCallsSave);

    for (int cc = 0; cc < kSv.nBestClustersAlive; cc++) {
        fprintf(fp, "%d %f\n", cc, kSv.BestWeight[cc]);
        for (int i = 0; i < kSv.nDimsBest; i++)
            fprintf(fp, "%f%c", kSv.BestMean.m_Data[cc * kSv.nDimsBest + i],
                    (i < kSv.nDimsBest - 1) ? ' ' : '\n');

        const int c = kSv.BestAliveIndex[cc];
        for (int i = 0; i < kSv.nDimsBest; i++) {
            for (int j = 0; j < kSv.nDimsBest; j++) {
                if (j > i)
                    K1.bestCholFlat[static_cast<size_t>(c) * K1.nDims2 + i * kSv.nDimsBest + j] = 0.0f;
                else if (c == 0)
                    K1.bestCholFlat[static_cast<size_t>(c) * K1.nDims2 + i * kSv.nDimsBest + j] = (i == j) ? 1.0f : 0.0f;
                fprintf(fp, "%f%c", K1.bestCholFlat[static_cast<size_t>(c) * K1.nDims2 + i * kSv.nDimsBest + j],
                        (j < kSv.nDimsBest - 1) ? ' ' : '\n');
            }
        }
    }
}

// ---------------------------------------------------------------------------
// SaveOutput — write binary .clu file, relabelling to remove gaps
// Binary format: int32_t nClusters; nSpikes * int32_t clusterIDs (1-based)
// ---------------------------------------------------------------------------
void SaveOutput(const Array<int> &OutputClass) {
    Array<int> cClustMembs(MaxPossibleClusters);
    Array<int> NewLabel(MaxPossibleClusters);

    for (int p = 0; p < OutputClass.size(); p++) ++cClustMembs[OutputClass[p]];

    NewLabel[0] = 1;
    int maxClass = 1;
    for (int c = 1; c < MaxPossibleClusters; c++)
        if (cClustMembs[c] > 0) NewLabel[c] = ++maxClass;

    char fname[STRLEN + 16];
    snprintf(fname, sizeof(fname), "%s.clu.%d", FileBase, ElecNo);
    FILE *fp = fopen_safe(fname, "wb");
    int32_t hdr = (int32_t)maxClass;
    fwrite(&hdr, sizeof(int32_t), 1, fp);
    const int n = OutputClass.size();
    for (int p = 0; p < n; p++) {
        int32_t id = (int32_t)NewLabel[OutputClass[p]];
        fwrite(&id, sizeof(int32_t), 1, fp);
    }
    fclose(fp);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    float BestScore = HugeScore;
    kSv.BestScoreSave = BestScore;

    try {
        SetupParams(argc, argv);

        // ---------------------------------------------------------------
        // Auto-detect spike-group parameters from <FileBase>.yaml
        //
        // The three globals NbChannels, NbSamplesPerSpike, and SamplingRate
        // are auto-filled from the YAML parameter file when the user has not
        // provided them explicitly on the command line.  Command-line values
        // always win (they are already set by SetupParams above).
        //
        // NbBytesPerSample is NOT read from YAML because the ndmanager
        // pipeline hardcodes int16 for all .spk files regardless of the
        // acquisition system's ADC resolution — see KlustaKwikYaml.cpp for
        // the full explanation.  The default value of 2 is always correct
        // for recordings processed through ndm_hipass + ndm_extractspikes
        // (or ndm_extractspikes_sdiff).
        // ---------------------------------------------------------------
        {
            const KKYamlSpikeParams yp = kkReadYamlSpikeParams(FileBase, ElecNo);
            if (yp.valid) {
                if (NbChannels == 0 && yp.nbChannels > 0) {
                    NbChannels = yp.nbChannels;
                    fprintf(stderr,
                            "KlustaKwik: NbChannels=%d  (from YAML, group %d)\n",
                            NbChannels, ElecNo);
                }
                if (NbSamplesPerSpike == 0 && yp.nbSamples > 0) {
                    NbSamplesPerSpike = yp.nbSamples;
                    fprintf(stderr,
                            "KlustaKwik: NbSamplesPerSpike=%d  (from YAML, group %d)\n",
                            NbSamplesPerSpike, ElecNo);
                }
                // SamplingRate: only override when the user has not provided
                // a value on the command line (sentinel = 0.0f).
                if (SamplingRate == 0.0f && yp.samplingRate > 0.0) {
                    SamplingRate = static_cast<float>(yp.samplingRate);
                    fprintf(stderr,
                            "KlustaKwik: SamplingRate=%.0f  (from YAML)\n",
                            static_cast<double>(SamplingRate));
                }
            }
        }
        // Last-resort default if neither command line nor YAML provided SR.
        if (SamplingRate <= 0.0f) {
            SamplingRate = 20000.0f;
            fprintf(stderr,
                    "KlustaKwik: SamplingRate defaulting to 20000 Hz"
                    " (no YAML found and -SamplingRate not given)\n");
        }

        clock_t clock0 = clock();

        KK K1;
        K1.penaltyMix = PenaltyMix;

        if (fSaveModel) {
            char fname[STRLEN + 16];
            snprintf(fname, sizeof(fname), "%s.model.%d", FileBase, ElecNo);
            pModelFile = fopen_safe(fname, "w");
        }

        K1.LoadData();

        kSv.BestWeight.SetSize(MaxPossibleClusters);
        kSv.BestMean.SetSize(MaxPossibleClusters * K1.nDims);

        srand(RandomSeed);

        if (DistDump) Distfp = fopen("DISTDUMP", "w");

        // -------------------------------------------------------------------
        // If a ChunkFile was provided, read the boundary times (seconds) now.
        // The file may contain comment lines beginning with '#'; all other
        // lines are float seconds in ascending order.
        // ChunkFile takes precedence over ChunkMinutes when both are given.
        // -------------------------------------------------------------------
        std::vector<float> extChunkBoundsSec;
        if (*ChunkFile) {
            FILE *cf = fopen(ChunkFile, "r");
            if (!cf) {
                fprintf(stderr, "KlustaKwik: cannot open ChunkFile '%s'\n", ChunkFile);
                return 1;
            }
            char linebuf[256];
            float prev = -1.0f;
            int lineNo = 0;
            while (fgets(linebuf, sizeof(linebuf), cf)) {
                ++lineNo;
                // Skip comment / empty lines
                const char *p = linebuf;
                while (*p == ' ' || *p == '\t') ++p;
                if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') continue;
                float t = 0.0f;
                if (sscanf(p, "%f", &t) != 1) {
                    fprintf(stderr, "KlustaKwik: ChunkFile line %d not a float — skipped\n", lineNo);
                    continue;
                }
                if (t < prev) {
                    fprintf(stderr, "KlustaKwik: ChunkFile boundary %.3f < previous %.3f"
                                    " at line %d — file must be sorted ascending\n",
                            t, prev, lineNo);
                    fclose(cf);
                    return 1;
                }
                extChunkBoundsSec.push_back(t);
                prev = t;
            }
            fclose(cf);
            if (extChunkBoundsSec.size() < 2) {
                fprintf(stderr, "KlustaKwik: ChunkFile '%s' has fewer than 2 boundaries"
                                " — ignoring and falling back to ChunkMinutes\n", ChunkFile);
                extChunkBoundsSec.clear();
            } else {
                fprintf(stderr, "KlustaKwik: loaded %zu chunk boundaries from '%s'"
                                " (%zu chunks)\n",
                        extChunkBoundsSec.size(), ChunkFile,
                        extChunkBoundsSec.size() - 1);
            }
        }

        const bool useExtChunks = !extChunkBoundsSec.empty();
        const bool useChunked   = useExtChunks || (ChunkMinutes > 0.0f);

        // -------------------------------------------------------------------
        // Startup banner — always written to stderr regardless of Screen/Log.
        // Gives the user confirmation the binary started, data loaded, and
        // parallelism is active before the first (potentially long) CEM call.
        // -------------------------------------------------------------------
        {
            fprintf(stderr, "KlustaKwik  %s.fet.%d\n", FileBase, ElecNo);
            fprintf(stderr, "  %d spikes, %d dims, clusters %d-%d\n",
                    K1.nPoints, K1.nDims, MinClusters, MaxClusters);

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            if (K1.gpu)
                fprintf(stderr, "  compute: GPU (%s)\n", GPU_BACKEND_NAME);
            else
                fprintf(stderr, "  compute: CPU only (no %s device found)\n", GPU_BACKEND_NAME);
#endif

            if (useExtChunks) {
                fprintf(stderr, "  mode: chunked  %zu drift-adaptive chunks  SR=%.0f\n",
                        extChunkBoundsSec.size() - 1, SamplingRate);
                fprintf(stderr, "  chunk file: %s\n", ChunkFile);
#ifndef _OPENMP
                fprintf(stderr, "  WARNING: built without OpenMP — chunks run serially.\n"
                                "           Recompile with -fopenmp to enable parallelism.\n");
#else
                {
                    const int nThreads = omp_get_max_threads();
                    const int nProcs   = omp_get_num_procs();
                    if (nThreads < nProcs)
                        fprintf(stderr,
                                "  parallel: %d of %d cores  "
                                "(OMP_NUM_THREADS=%d limits parallelism — "
                                "unset it or set to %d to use all cores)\n",
                                nThreads, nProcs, nThreads, nProcs);
                    else
                        fprintf(stderr, "  parallel: %d OpenMP threads\n", nThreads);
                }
#endif
            } else if (ChunkMinutes > 0.0f) {
                const float sessionSamples = K1.timeRawMax - K1.timeRawMin;
                const float chunkSamples   = SamplingRate * ChunkMinutes * 60.0f;
                const int   nChunksEst     = (sessionSamples > 0 && chunkSamples > 0)
                    ? std::max(1, static_cast<int>(std::ceil(sessionSamples / chunkSamples)))
                    : 1;
                fprintf(stderr, "  mode: chunked  chunk=%.1f min  SR=%.0f  ~%d chunks\n",
                        ChunkMinutes, SamplingRate, nChunksEst);
#ifndef _OPENMP
                fprintf(stderr, "  WARNING: built without OpenMP — chunks run serially.\n"
                                "           Recompile with -fopenmp to enable parallelism.\n");
#else
                {
                    const int nThreads = omp_get_max_threads();
                    const int nProcs   = omp_get_num_procs();
                    if (nThreads < nProcs)
                        fprintf(stderr,
                                "  parallel: %d of %d cores  "
                                "(OMP_NUM_THREADS=%d limits parallelism — "
                                "unset it or set to %d to use all cores)\n",
                                nThreads, nProcs, nThreads, nProcs);
                    else
                        fprintf(stderr, "  parallel: %d OpenMP threads\n", nThreads);
                }
#endif
            } else if (strcmp(InitMethod, "farthest") == 0) {
                fprintf(stderr, "  mode: two-phase farthest-point\n");
            } else {
                fprintf(stderr, "  mode: original random-init\n");
            }
            fflush(stderr);
        }

        // Start from provided cluster file if given
        if (*StartCluFile) {
            Output("Starting from cluster file %s\n", StartCluFile);
            BestScore = K1.CEM(StartCluFile);
            kSv.BestScoreSave = BestScore;
            Output("%d->%d Clusters: Score %f\n\n",
                   K1.nStartingClusters, K1.nClustersAlive, BestScore);
            for (int p = 0; p < K1.nPoints; p++) K1.BestClass[p] = K1.Class[p];
            if (SaveIntermediates) SaveOutput(K1.BestClass);
            K1.SaveBestMeans();
        }

        // Dispatch: three-phase chunked > two-phase farthest > original random
        const bool useFarthest  = (strcmp(InitMethod, "farthest") == 0);

        if (useExtChunks)
            Output("Mode: three-phase chunked CEM  "
                   "(drift-adaptive: %zu chunks, SR=%.0f, mergeThresh=%.1f, "
                   "globalIter=%d, timeMergeIter=%d)\n",
                   extChunkBoundsSec.size() - 1, SamplingRate, MergeThresh,
                   GlobalMergeIter, TimeMergeIter);
        else if (useChunked)
            Output("Mode: three-phase chunked CEM  "
                   "(chunk=%.1f min, SR=%.0f, mergeThresh=%.1f, "
                   "globalIter=%d, timeMergeIter=%d)\n",
                   ChunkMinutes, SamplingRate, MergeThresh,
                   GlobalMergeIter, TimeMergeIter);
        else if (useFarthest)
            Output("Mode: two-phase farthest-point CEM  (timeMergeIter=%d)\n",
                   TimeMergeIter);
        else
            Output("Mode: original random-init CEM\n");

        // Main loop over starting cluster counts
        for (K1.nStartingClusters = MinClusters;
             K1.nStartingClusters <= MaxClusters;
             K1.nStartingClusters++) {
            // Propagate the hard deletion floor so ConsiderDeletion never
            // collapses below the user's -MinClusters — both in the main EM
            // and in per-chunk sub-objects created by RunChunkedCEM.
            K1.minClustersAlive = MinClusters;
            for (int i = 0; i < nStarts; i++) {
                // Always show outer-loop progress on stderr so the user can
                // monitor a silent run without enabling Screen/Log.
                fprintf(stderr, "  K=%d/%d start=%d/%d\r",
                        K1.nStartingClusters, MaxClusters, i + 1, nStarts);
                fflush(stderr);
                Output("Starting from %d clusters...\n", K1.nStartingClusters);

                float score;
                if (useExtChunks)
                    score = K1.RunChunkedCEM(extChunkBoundsSec, SamplingRate,
                                              MergeThresh, GlobalMergeIter,
                                              TimeMergeIter);
                else if (useChunked)
                    score = K1.RunChunkedCEM(ChunkMinutes, SamplingRate,
                                              MergeThresh, GlobalMergeIter,
                                              TimeMergeIter, ChunkOverlapMinutes,
                                              ChunkPreseedFraction);
                else if (useFarthest)
                    score = K1.CEMTwoPhase(TimeMergeIter);
                else
                    score = K1.CEM();

                Output("%d->%d Clusters: Score %f, best is %f\n",
                       K1.nStartingClusters, K1.nClustersAlive, score, BestScore);

                if (score < BestScore) {
                    Output("THE BEST YET!\n");
                    BestScore = score;
                    if (BestScore < kSv.BestScoreSave) {
                        Output("BestScoreSave updated from %g to %g\n",
                               kSv.BestScoreSave, BestScore);
                        kSv.BestScoreSave = BestScore;
                    }
                    for (int p = 0; p < K1.nPoints; p++) K1.BestClass[p] = K1.Class[p];
                    if (SaveIntermediates) SaveOutput(K1.BestClass);
                }
                Output("\n");
            }
        }

        SaveOutput(K1.BestClass);   // final write — always runs regardless of SaveIntermediates
        fprintf(stderr, "  done                    \n");  // clear the \r progress line

        if (fSaveModel) { export_model(pModelFile, K1); fclose(pModelFile); }

        Output("That took %f seconds.\n",
               static_cast<float>(clock() - clock0) / CLOCKS_PER_SEC);

        if (DistDump) fclose(Distfp);

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception thrown\n";
        return 1;
    }

    return 0;
}
