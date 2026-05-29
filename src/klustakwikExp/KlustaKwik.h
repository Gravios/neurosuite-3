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

// Same OpenMP critical section as Output(), but writes to stderr instead of
// stdout.  Use this for status/diagnostic lines that must always be visible
// (regardless of -Screen) AND should not splice into the middle of Output()'s
// stdout text when both streams are merged by `2>&1 | tee` at the shell.
void LockedStderr(const char *fmt, ...);
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

// PreseedCacheFile — path to a binary file that holds the output of
// PreseedSubsampleCEM (the global preseed cluster centres) keyed to the
// input data and the relevant CEM parameters.  When set and the file
// exists, KKE skips the preseed CEM and loads the cached centres
// directly.  After computing fresh centres, KKE writes them to this
// path for the next run.
//
// Cache invalidation is automatic: the file embeds the input .fet's
// mtime + size and the parameters that affect preseed output
// (RandomSeed, MaxClusters, ChunkPreseedFraction, PenaltyMix,
// TimeMergeIter, nPoints, nDims, nSpatialDims).  Any mismatch
// triggers a recompute.
//
// Empty (default) — disabled, run preseed every time.
// Suggested path: "<dataset>.preseed.<group>.cache" alongside the
// .fet file, so the cache is naturally co-located with its input.
extern char  PreseedCacheFile[];
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
// Phase2bEnableSplits: in Phase 2b mode 0 (default), 0 disables TrySplits
// inside the inner CEM (recommended; Phase 2a already discovered splits);
// 1 enables.  Default 0.
extern int   Phase2bEnableSplits;
// Phase2bMaxIter: max inner CEM iterations per chunk in Phase 2b.
// Default 60.  0 falls back to global MaxIter.
extern int   Phase2bMaxIter;
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
extern int   AlignPcaCenter;   // patch83 — opt-in PCA-centering refine pass
extern float TemplateMatchEigRatio;
// Phase 4 within-chunk merge: Hann-taper templates (width N centred on
// PeakSampleIndex) before xcorr.  See KlustaKwik.cpp.  Default 0 = no taper.
extern int   TemplateMatchTaperHannSamples;
// Phase 4 within-chunk merge: realign+refeaturize merged clusters
// immediately after merge commit.  See KlustaKwik.cpp.  Default 0.
extern int   MergeRealignEnable;
extern int   MergeRealignIncremental;
extern int   TemplateMatchBatchedXcorr;
extern int   DipSplitGlobalEnable;
extern int   DipSplit2D;
extern float CrossChunkDriftSigma;
extern float CrossChunkVoteMinFraction;
extern float CrossChunkVoteMinMargin;

// CrossChunkMaxChunkDistance — Phase 5 Pass 2 (edge xcorr) candidate-pair
// gate.  Restricts the cross-chunk xcorr search to chunk pairs whose
// chunkIdx differ by AT MOST this value.  Default 1 (adjacent chunks
// only) — the typical and physically correct setting for drift-aware
// recording: a unit's mean-waveform template persists across adjacent
// chunks but can only match non-adjacent ones via a chain of adjacent
// matches.  Larger values widen the search; 0 disables Pass 2 entirely;
// >=nChunks restores the legacy O(N²) all-pairs behaviour.
//
// Without this gate, Pass 2 is O(N_leftovers × N_total) over ALL cluster
// pairs across ALL chunks — for typical fragmented sessions (~10k local
// clusters across 36 chunks) that is ~100M xcorr calls, dominating
// wall time and pinning multi-GB of GPU workspace for the duration.
// With default=1 the work drops to O(N × K_per_chunk × 2) which is
// roughly nChunks× smaller (~3M for the same session).
extern int   CrossChunkMaxChunkDistance;
extern int   TimeShiftAlignPostMerge;
extern float TimeShiftAlignScoreThresh;
extern int   TimeShiftAlignAfterPhase1;
extern int   TimeShiftAlignAfterPhase1b;
extern int   TimeShiftAlignAfterPhase2;
extern int   TimeShiftAlignAfterPhase4;
extern int   TimeShiftAlignAfterPhase5;
extern int   EnergyCOMRealign;
extern int   EnergyCOMMetric;
extern int   MeanSubtractionMergeEnable;   ///< Phase 6b enable
extern float MeanSubtractionMergeThresh;   ///< Phase 6b residual threshold
extern int   MeanSubtractionMergeMaxShift; ///< Phase 6b cyclic-shift half-width

// ── Phase 7c — klusters-faithful per-spike realignment ────────────────────
// Per-cluster, per-spike normalised xcorr against a pre-aligned mean
// template (same algorithm as klusters' interactive realign).  Final
// tightening pass after Phase 7a/7b; updates m_cumShift so
// TimeShiftFinalize's RefeaturizeFromShifts re-extracts shifted spikes
// from .fil and reprojects through the PCA basis.  See KK::KlustersStyle-
// RealignAllClusters and klusters_realign.{h,cpp}.
extern int   KlustersRealignEnable;    ///< Phase 7c enable (0 off, 1 on)
extern int   KlustersRealignMaxShift;  ///< xcorr search radius (samples), capped at nSamp/4
extern int   KlustersRealignMinSize;   ///< skip clusters smaller than this
extern int   KlustersRealignIters;     ///< realign passes (>1 = iterate to convergence)
extern int   KlustersRealignSelectMinVariance; ///< per-cluster min-variance pass restore
// ---- KnnSplitPerChunk parameters (Phase 2b.5, K-template chunk split) ---
// KnnSplitPerChunkEnable: on/off gate.  Default 0 (off).  When enabled,
// runs immediately after Phase 2b (ChunkReCEMPerChunk).  For each chunk,
// picks K well-isolated reference clusters (largest by nbSpikes, lowest
// by trace(Σ)/nSpatial) and partitions each remaining "source" cluster's
// spikes by their nearest reference template, materializing each
// (source, ref) bucket of ≥ KnnSplitMinNewClusterSize spikes as a new
// chunk-local cluster.  Phase 6 cross-chunk template matching then
// consolidates the new chunk-local clusters into global units.
extern int   KnnSplitPerChunkEnable;
// KnnSplitK: number of reference templates (cluster means) per chunk.
// Default 10.  Lower → coarser split; higher → finer.
extern int   KnnSplitK;
// KnnSplitMinRefSize: minimum spikes per reference cluster.  A "good"
// reference must have at least this many spikes AND be in the bottom-
// half-of-the-chunk by trace(Σ)/nSpatial (the "low variance" criterion).
// Default 50.
extern int   KnnSplitMinRefSize;
// KnnSplitMinSourceSize: minimum spikes per source cluster to consider
// splitting.  Default 20.  Below this, tiny noise / fragment clusters
// are left alone (they would just shatter into noise across templates).
extern int   KnnSplitMinSourceSize;
// KnnSplitMinNewClusterSize: minimum bucket size to materialize as a new
// chunk-local cluster.  Smaller buckets leave their spikes in the source.
// Default 10.
extern int   KnnSplitMinNewClusterSize;

// ---- Phase-4 rewrite controls --------------------------------------------
// KnnSplitMode: 0 = legacy nearest-template, 1 = klusters-faithful KNN
// majority-vote (see wave_knn_split.h).  Default 0.
extern int   KnnSplitMode;
// WaveKnnMajorityThreshold: majority fraction in mode 1.  Default 0.6.
extern float WaveKnnMajorityThreshold;
// WaveKnnResidualBecomesCluster: in mode 1, 1 = klusters-faithful (residual
// pool of ambiguous + small-winner spikes materialises as its own new
// cluster if large enough); 0 = ambiguous spikes always stay in source.
// Default 1.
extern int   WaveKnnResidualBecomesCluster;

// WaveKnnMinSourceAnisotropy: mixture-detector gate for source-candidate
// selection in wave_knn_split (applies to BOTH the Phase 2b.5 single-
// pass call AND the Phase 4b alternating call).  For each cluster the
// anisotropy ratio λ_max(Σ)/tr(Σ) of its residual covariance is
// computed via power iteration on the [nDims×nDims] cov matrix.
// Clusters whose ratio is BELOW this threshold are removed from the
// source-candidate set (references are unaffected).
//
// Rationale: a unimodal cluster has roughly isotropic residuals so
// anisotropy ≈ 1/nDims (≈ 0.045 for nDims=22).  A mixture stretches
// Σ along the separation axis and the ratio rises to 0.3–0.7.  The
// gate prevents WaveKnnSplit from attacking well-isolated clusters
// and producing spurious "splits" from random K-NN voting noise —
// the root cause of the runaway over-fragmentation when Phase 4b
// alternation was enabled without the gate (10k+ local clusters
// across 36 chunks).
//
// Default 0.10 (~2.2× the isotropic floor for nDims=22) — admits
// obvious mixtures while sparing unimodals.  0.0 disables the gate
// (pre-patch-0021 behaviour).
extern float WaveKnnMinSourceAnisotropy;
// WaveKnnUseTraceFilter: in mode 1, 1 = KKE auto-pick (low-trace = ref,
// high-trace = source); 0 = klusters mode (every cluster is both ref and
// source candidate, own-cluster excluded per-spike).  Default 1.
extern int   WaveKnnUseTraceFilter;
// WaveKnnSkipMuaCluster1: in mode 1, 1 = klusters-compat (exclude cluster 1);
// 0 = KKE default (include cluster 1).  Default 0.
extern int   WaveKnnSkipMuaCluster1;
// WaveKnnNoiseSourceProbability: probability the noise cluster (cid=0)
// is treated as a source candidate in mode 1.  0.0 = never (default).
extern float WaveKnnNoiseSourceProbability;
// See KlustaKwik.cpp.  Sequential per-source-cluster processing with
// k-NN-neighborhood masking inside wave_knn_split::Run.  Default 1.
extern int   WaveKnnMaskNeighbors;
// Cap on source clusters processed per wave_knn_split::Run.  See
// KlustaKwik.cpp.  Default 0 = unlimited.
extern int   WaveKnnMaxSourcesPerCall;

// FullCemSplit — Phase 4b per-cluster full CEM splitter.  See
// KlustaKwik.cpp doc block for design.
extern int   FullCemSplitEnable;
extern int   FullCemSplitMaxSourcesPerCall;
extern int   FullCemSplitMinClusterSize;
extern int   FullCemSplitAdaptiveFeatures;
extern float FullCemSplitFeatureBimodalThreshold;
extern int   FullCemSplitMinFeatures;
extern int   FullCemSplitMaxFeatures;
extern int   FullCemSplitReprobePasses;
extern int   FullCemSplitRefractoryGate;
extern float FullCemSplitRefractoryGateMinSep;
extern int   Phase4cRemixEnable;
extern int   Phase4cMaxIters;
extern int   Phase4cKnnSources;
extern int   Phase4cFullCemSources;
extern int   Phase4cNeighbors;
extern int   Phase4cMinClusterSize;
extern int   Phase4cMaskTightClusters;
extern float Phase4cTightnessThreshold;
extern float Phase4cSignalChannelFraction;
extern float Phase4cTightnessSpreadBeta;
extern int   Phase8VarianceSplitEnable;
extern int   Phase8VarianceSplitMaxIters;
extern float Phase8VarianceThreshold;
extern float Phase8VarianceSignalChannelFraction;
extern int   Phase8VarianceMinClusterSize;
extern int   CrossChunkTransformDriftEnable;
extern int   CrossChunkTransformIRLSIters;
extern int   CrossChunkTransformMinMatches;
extern float CrossChunkTransformHuberK;
extern float CrossChunkTransformChainSmoothLambda;

// QualityWeightedSplit — Phase 4b quality-routed splitter dispatch.
// See KlustaKwik.cpp doc block.
extern int   QualityWeightedSplitEnable;
extern int   QualityWeightedSplitN;
extern int   QualityWeightedSplitPoolFactor;
extern float QualityWeightedISIRefractoryMs;

// MedianKnnTemplateMatch — alternative Phase 4 merge.  See KlustaKwik.cpp
// and KK::WithinChunkTemplateMatchMedianKnn.  Default 0 (off, use
// all-pairs WithinChunkTemplateMatch).
extern int   MedianKnnTemplateMatchEnable;
extern int   MedianKnnTemplateMatchK;
// Phase4RefineEnable: master switch for the rewritten Phase-4 pipeline
// (proxy_isi → adapt_model → clust_quality → xcorr_match).  Default 0
// (legacy WithinChunkTemplateMatch preserved).
extern int   Phase4RefineEnable;
// Phase4RefineIters: number of refine-loop passes; replaces
// TemplateMatchIters when Phase4RefineEnable=1.  Default 10.
extern int   Phase4RefineIters;
// AdaptModelEnable: fit ISI-conditional adaptation model before computing
// residuals.  Default 1 when Phase4RefineEnable=1.
extern int   AdaptModelEnable;
// AdaptTauGridCSV: comma-separated τ grid in seconds.  Empty → default
// {0.005, 0.015, 0.050, 0.150}.
extern char  AdaptTauGridCSV[STRLEN];
// Phase4MinClusterSize: minimum cluster spikes for the refine-loop to
// touch a cluster.  Default 50.
extern int   Phase4MinClusterSize;
// XcorrResidualThresh: minimum residualScore for a within-chunk merge in
// the new pipeline.  Default 0.85.
extern float XcorrResidualThresh;
// XcorrMaxShiftSamples: half-width of integer-lag xcorr search.  0 →
// NbSamplesPerSpike/4 at runtime.  Default 0.
extern int   XcorrMaxShiftSamples;

// ---- DipSplit parameters (bimodal-cluster splitter, Phase 8) -----------
// DipSplitEnable: on/off gate for the automatic DipSplit pass.  Default 1.
extern int   DipSplitEnable;
// DipSplitBeforePhase2b: when 1 (default), runs a per-chunk DipSplit pass
// between Phase 2a (per-cluster CEM) and Phase 2b (chunk warm-start CEM).
// Catches single-dim bimodality that Phase 2a's parametric CEM misses.
extern int   DipSplitBeforePhase2b;

// HullSplit (Phase 2a.6): k-NN-graph connected-components split.
// Default off; see cluster_hull_split.h.
extern int   HullSplitEnable;
extern int   HullSplitK;
extern int   HullSplitMinComponentSize;
extern float HullSplitMutualReachScale;
extern int   HullSplitUseMutualReach;

// PerChannelSplit (Phase 2a.7): per-channel amplitude+phase bimodality split.
// Reads full waveforms per cluster, extracts 4 features per channel (peak
// amplitude, peak time, trough amplitude, trough time), and tests 1D and
// 2D-angle-sweep projections per channel for KDE-detectable valleys.
// Catches the failure mode where two units differ by a small combination
// of amplitude AND phase on a single channel — invisible to PCA-based
// DipSplit (which spreads the signal across multiple PCs) and HullSplit
// (since the clusters are topologically connected in feature space).
// Default off (PerChannelSplitEnable=0).  See per_channel_split.h.
extern int   PerChannelSplitEnable;
extern int   PerChannelSplitMinClusterSize;     ///< default 50
extern float PerChannelSplitValleyThreshold;    ///< default 0.50
extern int   PerChannelSplitMinSubClusterSize;  ///< default 25
extern float PerChannelSplitBicMarginConstant;  ///< default 12.0
extern float PerChannelSplitBicMarginPerLogN;   ///< default 6.0
extern float PerChannelSplitMinChannelSnrRatio; ///< default 0.5
extern int   PerChannelSplitUsePeakAmp;         ///< default 1
extern int   PerChannelSplitUsePeakTime;        ///< default 1
extern int   PerChannelSplitUseTroughAmp;       ///< default 1
extern int   PerChannelSplitUseTroughTime;      ///< default 1

// AlternatingSplitMerge (Phase 4 alternation): interleave per-chunk
// WaveKnnSplit with WithinChunkTemplateMatch inside Phase 4's existing
// loop.  The goal is to drive intra-cluster spike waveform variance to a
// minimum per chunk BEFORE Phase 5 cross-chunk template matching:
// templates that look like mixtures get re-split; over-fragments produced
// by the split that share a true mean template get re-merged in the
// following iteration's TemplateMatch.  The loop's natural convergence
// criterion is "neither nMerged nor nSpikesSplit changed in this iter".
//
// Requires both -KnnSplitPerChunkEnable 1 and -KnnSplitMode 1 (klusters-
// faithful K-NN majority-vote variant — the only variant that respects
// the "low-confidence spikes stay in source" convention needed to avoid
// runaway over-splitting).
//
// Default off.  The existing Phase 2b.5 single-pass KnnSplit is still
// the default entry; this flag adds re-splitting inside Phase 4.
extern int   AlternatingSplitMergeEnable;
// See KlustaKwik.cpp for the full description.  Heavy in-Phase-4
// realignment: applies the klusters-style xcorr realign per cluster
// per chunk at the end of every Phase 4 iteration, with full
// RefeaturizeChangedSpikes propagation so subsequent iters see the
// shifts in both meanWav and PCA features.  Default 0.
extern int   KlustersRealignAfterPhase4;
// Cap on the number of Phase 4 iterations in which WaveKnnSplit runs.
// Once reached, the Phase 4 loop continues with template-match-only
// iters (still bounded by TemplateMatchIters) so over-fragments get
// merged.  Default 2 — conservative; alternation is a tug-of-war
// and unbounded split tends to win.
extern int   AlternatingSplitMergeMaxIters;
extern int   AlternatingSplitCooldownIters;
// Stop further Phase 4b split-iters when iter K-growth (new clusters
// from split minus pairs merged by template-match) is positive for
// two consecutive iters.  Net-growth indicates split is winning the
// tug-of-war and further iters will diverge.  1 = enable, 0 = run all
// allowed iters.
extern int   AlternatingSplitMergeAbortOnNetGrowth;
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
