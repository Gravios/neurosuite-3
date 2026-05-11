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

// Build a path for an ndmanager-plugins input file, preferring the canonical
// extension but falling back to the stderiv "D" variant when the canonical
// file is absent.  Writes into `out` (caller-provided buffer of `outSize`
// bytes) and returns:
//   0 if  `<base>.<ext>.<elec>`  exists (canonical; out = canonical path)
//   1 if  `<base>.<extD>.<elec>` exists (stderiv;  out = D-variant path)
//  -1 if neither exists (out = canonical path — caller can use it with
//      fopen_safe to get the usual "Could not open file" abort with the
//      expected filename).
//
// `ext` should be the canonical extension without leading dot ("fet",
// "spk", "pca").  `extD` is derived by appending 'D' to `ext`.  The
// canonical-preferred ordering matches how the reextract scripts symlink
// stderiv outputs to canonical names before invoking downstream tools:
// when both files are present the symlinked canonical one wins, which is
// what the caller expects.
//
// Thread-safe: depends only on filesystem state and caller-supplied
// buffers.  Used by LoadData() and the workflows that rewrite .spk /
// .fet / .pca in place.
int pickInputPath(char *out, size_t outSize,
                  const char *base, const char *ext, int elec);
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
// Refine-existing-clustering parameters (see KlustaKwik.cpp)
extern char  RefineExisting[];
extern char  RefineMode[];
extern int   RefineIters;
extern float RefineMergeThresh;
extern float RefineSplitMinDepth;
extern int   RefineLockNoiseClu;
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
extern int   TimeShiftAlignIter;

// ── Empirical prior ────────────────────────────────────────────────────────
extern char  PriorFile[];  ///< path to .prior.N.yaml built by kk_build_prior.py
/** Per-pair adaptive MergeThresh = chi²(d_eff, 0.9999).  Default on. */
extern int   AdaptiveMerge;
/** Preseed centres from prior; populated by applyKKPrior(). */
extern std::vector<float> ExternalPreseedCentres; ///< Phase 1.5 alignment passes (0=skip, N=run N passes with MStep between) (default 1)
// Post-split shift-probe parameters (klustakwikExp only).
// MaxTimeShift: the half-width of the pre-shifted PCA basis fan.  The probe
// builds bases for δ ∈ {-N, …, +N} at session start, and picks the δ that
// maximises per-dim variance on split-children (or minimises Mahalanobis
// distance to the merge target on merge-children).  N=0 disables the probe
// entirely (equivalent to canonical KlustaKwik behaviour).  Valid range: 0..5.
extern int   MaxTimeShift;
// TimeShiftMergeEnable: when non-zero and shift-probe is active, apply a
// min-Mahalanobis shift probe to spikes reassigned during ConsiderDeletion
// (cluster deletion/implicit merge).  Default 1.
extern int   TimeShiftMergeEnable;
extern int   TimeShiftSplitEnable;
extern int   Phase2bMode;
extern int   VBGMMMaxIter;
extern float VBGMMConvTol;
extern float VBGMMAlpha0;
extern float VBGMMBeta0;
extern float VBGMMNu0Offset;
extern int   VBGMMPriorMode;
extern float VBGMMPriorBlend;
// ResidualPCA — Phase 2b mode 3 hyperparameters (per-chunk iterative
// residual-PCA refinement + dominant-channel xcorr realignment).
extern int   ResidualPCAIter;
extern int   ResidualPCAComponents;
extern int   ResidualPCASubK;
extern int   ResidualPCADominantChannels;
extern float ResidualPCAConvTol;
extern float ResidualPCAMinScore;
extern float TemplateMatchEigRatio;
extern int   DipSplitGlobalEnable;
extern int   DipSplit2D;
extern float CrossChunkDriftSigma;
extern int   TimeShiftAlignPostMerge;
extern float TimeShiftAlignScoreThresh;
extern int   TimeShiftAlignAfterPhase1;
extern int   TimeShiftAlignAfterPhase1b;
extern int   TimeShiftAlignAfterPhase2;
extern int   TimeShiftAlignAfterPhase5;
extern int   TimeShiftAlignAfterPhase6;
extern int   EnergyCOMRealign;
extern int   EnergyCOMMetric;
// ---- DipSplit parameters (bimodal-cluster splitter, Phase 8) -----------
// DipSplitEnable: on/off gate for the automatic DipSplit pass.  Default 1.
extern int   DipSplitEnable;
// DipSplitMinSize: minimum spike count per child cluster for an accepted
// split, and minimum per parent cluster for the splitter to even look
// (parent must have ≥ 2·DipSplitMinSize members).  Default 50.
extern int   DipSplitMinSize;
// DipSplitBloatFactor: the bloat gate fires when the cluster's 90th-
// percentile Mahalanobis² exceeds this factor times χ²(nDims, 0.9).
// Larger → more conservative (fewer false positives).  Default 1.0.
// Lowered from 1.5 because the χ²(d, 0.9) target is itself the
// expected p90 of a unimodal Gaussian — bimodal mixtures whose
// covariance has been inflated by CEM can sit just under it.  The
// elongation gate (DipSplitElongationFactor) is the actual second
// line of defence for that absorbed-bimodal case.
extern float DipSplitBloatFactor;
// DipSplitElongationFactor: secondary OR-gate that fires when the
// cluster's covariance is strongly elongated along one direction —
// specifically when the top eigenvalue is ≥ this factor times the
// median of the top-3 eigenvalues.  Catches the failure mode where
// CEM has fitted two well-separated sub-populations with one inflated
// Gaussian: the bloat gate misses it (because the inflated covariance
// keeps mahal²₉₀ near the χ² expectation), but the eigenvalue
// signature is unmistakable.  Default 4.0.  Set 0.0 to disable
// (revert to bloat-only behaviour).
extern float DipSplitElongationFactor;
// DipSplitValleyThresh: minimum valley depth on the KDE of a PC projection
// for the cluster to be flagged as bimodal.  Depth ∈ [0, 1]; 0.15 means
// "the valley is at most 85% as high as the shorter peak".  Default 0.0
// (let any detected valley through to the BIC gate).
extern float DipSplitValleyThresh;
extern int   SubspaceDims;      ///< top-N eigenvectors for Phase 2 subspace Mahal (0=full-space)
extern int   SubspaceRecluster;    ///< per-cluster subspace CEM after Phase 2 (0=disabled)
extern float TemplateMatchScore;       ///< min xcorr for WITHIN-chunk template matching (0=disabled)
extern int   TemplateMatchIters;       ///< max within-chunk template match iterations (default 10)
extern int   SplitRecurseDepth;        ///< max TrySplits recursion depth (default 1)
extern float CrossChunkTemplateScore;  ///< min xcorr for CROSS-chunk template matching in Phase 2 (0=disabled)

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
