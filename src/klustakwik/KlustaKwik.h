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
extern int   ParallelK;
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
extern float ChunkOverlapMinutes;
extern float ChunkPreseedFraction;
extern char  ChunkFile[];
extern float SamplingRate;
extern float MergeThresh;
extern int   GlobalMergeIter;

// Phase 1.5 waveform realignment
// Phase 1.5 waveform realignment parameters.
// NbChannels, NbSamplesPerSpike, PeakSampleIndex, NbTotalChannels and
// GroupChannelIds are auto-detected from <FileBase>.yaml at startup.
// Override NbChannels / NbSamplesPerSpike explicitly if needed.
extern int   NbChannels;       ///< channels in this spike group (.spk layout)
extern int   NbSamplesPerSpike; ///< waveform window width in samples
extern int   PeakSampleIndex;  ///< 0-based peak position within the window
extern int   NbTotalChannels;  ///< total channels in the .fil file
// NbBytesPerSample: bytes per sample in the .spk file (2 for ≤16-bit, 4 for 32-bit).
extern int   NbBytesPerSample;
// GroupChannelIds: 0-based ADC channel indices for this spike group.
// Used to extract the right columns from the .fil file during Phase 1.5.
extern std::vector<int> GroupChannelIds;
// nRuns: when > 0 in chunked mode, replaces the (MaxClusters-MinClusters+1)×nStarts
// outer loop with nRuns independent runs, each seeded differently.
// MinClusters/MaxClusters then act only as per-chunk TrySplits bounds.
extern int   nRuns;
extern int   Phase15Iters; ///< realignment iterations in RealignChunkWaveforms (default 2)

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
