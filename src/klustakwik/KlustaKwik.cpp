// KlustaKwik.cpp — C++17 modernisation
//
// Changes from original v1.7:
//   - #include <iostream.h>/<fstream.h> → standard headers
//   - Error() marked [[noreturn]], const char* signatures
//   - Cholesky: no longer allocates temporary Array objects — works directly
//     on the raw float pointers (eliminates 2 heap allocs per cluster per EStep)
//   - TriSolve: same — direct pointer arithmetic, no Array temporaries
//   - MatPrint: const-correct
//   - All global C-string arrays kept as-is for param.c compatibility

#include "KlustaKwik.h"
#include "KK.h"
#include "KlustaSave.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <iostream>
#include <stdexcept>

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
int   Verbose                = 1;
char  UseFeatures[STRLEN]    = "11111111111100001";
int   DistDump               = 0;
float DistThresh             = static_cast<float>(std::log(1000.0));
int   FullStepEvery          = 10;
float ChangedThresh          = 0.05f;
char  Log                    = 1;
char  Screen                 = 1;
int   MaxIter                = 500;
char  StartCluFile[STRLEN]   = "";
float PenaltyMix             = 0.0f;
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

    if (Screen) print_params(stdout);

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
    va_list arg;
    va_start(arg, fmt);
    if (Screen) vprintf(fmt, arg);
    if (Log)    vfprintf(logfp, fmt, arg);
    va_end(arg);
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
// ---------------------------------------------------------------------------
void export_model(FILE *fp) {
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
                    (*kSv.pBestChol)[c][i * kSv.nDimsBest + j] = 0.0f;
                else if (c == 0)
                    (*kSv.pBestChol)[c][i * kSv.nDimsBest + j] = (i == j) ? 1.0f : 0.0f;
                fprintf(fp, "%f%c", (*kSv.pBestChol)[c][i * kSv.nDimsBest + j],
                        (j < kSv.nDimsBest - 1) ? ' ' : '\n');
            }
        }
    }
}

// ---------------------------------------------------------------------------
// SaveOutput — write .clu file, relabelling to remove gaps
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
    FILE *fp = fopen_safe(fname, "w");
    fprintf(fp, "%d\n", maxClass);
    for (int p = 0; p < OutputClass.size(); p++)
        fprintf(fp, "%d\n", NewLabel[OutputClass[p]]);
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

        // Start from provided cluster file if given
        if (*StartCluFile) {
            Output("Starting from cluster file %s\n", StartCluFile);
            BestScore = K1.CEM(StartCluFile);
            kSv.BestScoreSave = BestScore;
            Output("%d->%d Clusters: Score %f\n\n",
                   K1.nStartingClusters, K1.nClustersAlive, BestScore);
            for (int p = 0; p < K1.nPoints; p++) K1.BestClass[p] = K1.Class[p];
            SaveOutput(K1.BestClass);
            K1.SaveBestMeans();
        }

        // Main loop over cluster counts
        for (K1.nStartingClusters = MinClusters;
             K1.nStartingClusters <= MaxClusters;
             K1.nStartingClusters++) {
            for (int i = 0; i < nStarts; i++) {
                Output("Starting from %d clusters...\n", K1.nStartingClusters);
                const float score = K1.CEM();

                Output("%d->%d Clusters: Score %f, best is %f\n",
                       K1.nStartingClusters, K1.nClustersAlive, score, BestScore);

                if (score < BestScore) {
                    Output("THE BEST YET!\n");
                    BestScore = score;
                    if (BestScore < kSv.BestScoreSave) {
                        std::cout << "BestScoreSave updated from " << kSv.BestScoreSave
                                  << " to " << BestScore << std::endl;
                        kSv.BestScoreSave = BestScore;
                    }
                    for (int p = 0; p < K1.nPoints; p++) K1.BestClass[p] = K1.Class[p];
                    SaveOutput(K1.BestClass);
                }
                Output("\n");
            }
        }

        SaveOutput(K1.BestClass);   // final write (harmless duplicate)

        if (fSaveModel) { export_model(pModelFile); fclose(pModelFile); }

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
