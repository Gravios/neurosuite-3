// KlustaKwik.h — C++17 modernised header
// Changes from original:
//   - Replaced <fstream.h>/<iostream.h> with standard <fstream>/<iostream>
//   - Function signatures use std::string / const char* consistently
//   - Error() and Output() wrapped with [[noreturn]] / format string safety
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <vector>
#include <stdexcept>

#include "param.h"
#include "Array.h"

class KK;
void export_model(FILE *fp, KK& K1);
void SetupParams(int argc, char **argv);
[[noreturn]] void Error(const char *fmt, ...);
void Output(const char *fmt, ...);
int  irand(int min, int max);
FILE *fopen_safe(const char *fname, const char *mode);
void MatPrint(FILE *fp, const float *Mat, int nRows, int nCols);
int  Cholesky(const float *m_In, float *m_Out, int D);
void TriSolve(const float *M, const float *x, float *Out, int D);

void SaveOutput(const Array<int> &OutputClass);

// ---- Parameters (defined in KlustaKwik.cpp) --------------------------------
extern char  FileBase[];
extern int   ElecNo;
extern int   MinClusters;
extern int   MaxClusters;
extern int   MaxPossibleClusters;
extern int   nStarts;
extern int   RandomSeed;
extern char  Debug;
extern int   Verbose;
extern char  UseFeatures[];
extern int   DistDump;
extern float DistThresh;
extern int   FullStepEvery;
extern float ChangedThresh;
extern char  Log;
extern char  Screen;
extern int   MaxIter;
extern char  StartCluFile[];
extern float PenaltyMix;

extern char  InitMethod[];
extern int   TimeMergeIter;

// Three-phase chunked CEM
extern float ChunkMinutes;
extern float SamplingRate;
extern float MergeThresh;
extern int   GlobalMergeIter;

// Phase 1.5 waveform realignment
// NbChannels > 0 && NbSamplesPerSpike > 0  →  realignment enabled in chunked mode.
// Both are auto-detected from <FileBase>.yaml at startup (see KlustaKwikYaml.cpp).
// Override explicitly with -NbChannels and -NbSamplesPerSpike if needed.
extern int   NbChannels;
extern int   NbSamplesPerSpike;
// NbBytesPerSample: bytes per sample in the .spk file (2 for 12/14/16-bit, 4 for 32-bit).
// Defaults to 2. Pass -NbBytesPerSample 4 for 32-bit recordings.
extern int   NbBytesPerSample;

// Output control
// SaveIntermediates=1 (default): write .clu whenever a new best is found.
// SaveIntermediates=0: suppress all mid-run .clu writes; single final write only.
extern int   SaveIntermediates;

// ---- Model saving ----------------------------------------------------------
class KlustaSave;
extern KlustaSave kSv;
extern int   fSaveModel;
extern FILE *pModelFile;
extern int   SplitEvery;

// ---- Globals ---------------------------------------------------------------
extern FILE *logfp, *Distfp;
extern float HugeScore;
extern const double PI;
