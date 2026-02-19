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
#include <iostream>
#include <fstream>
#include <string>

#include "param.h"
#include "Array.h"

void export_model(FILE *fp);
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
