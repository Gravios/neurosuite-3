// KlustaKwik.cpp — main entry point, parameter handling, and outer CEM loop.
//
// For a full description of changes from the original v1.7 release see
// CHANGES.md in this directory.

#include "KlustaKwik.h"
#include "KK.h"
#include "KlustaSave.h"
#include "KlustaKwikYaml.h"   // auto-detect spike params from YAML config
#include "KK_prior.h"         // empirical prior loader

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <sys/stat.h>   // stat() for file-exists check
#include <unistd.h>     // readlink()
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
int   MaxClusters            = 200;       // canonical jg05/eb05 sweep value
int   MaxPossibleClusters    = 500;       // ≈ 2.5× MaxClusters; hard cap on cluster ID space
int   nStarts                = 1;
int   ParallelK              = 0;    // 0 = serial; N = concurrent (K,start) workers
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

// ── Refine-existing-clustering parameters ──────────────────────────────────
// When set, KlustaKwik loads an existing .clu.N as full-Gaussian cluster
// models and runs a curation pass (reassign + split + merge) instead of the
// standard init/CEM dispatch.  See KK::RefineExisting in KK.cpp.
//
// Drift integration is implicit: when -ChunkFile is also given, RefineExisting
// uses the chunk boundaries to compute per-cluster temporal occupancy, which
// gates the merge phase (clusters concentrated in disjoint chunks merge more
// readily than co-occurring clusters of similar Mahalanobis distance — the
// drifting-unit case).
char  RefineExisting[STRLEN] = "";       // path to seed .clu; empty = disabled
char  RefineMode[STRLEN]     = "full";   // off | reassign | split | merge | full
int   RefineIters            = 5;        // CEM iterations during reassign
float RefineMergeThresh      = 0.0f;     // 0 = inherit from MergeThresh (auto-χ²)
float RefineSplitMinDepth    = 0.4f;     // DipSplit valley depth threshold
int   RefineLockNoiseClu     = 1;        // never modify cluster ids 0 and 1
                                         // (artefact / MUA bins per neurosuite convention)
float PenaltyMix             = 0.0f;
char  InitMethod[STRLEN]     = "farthest";  // "farthest" (default, deterministic),
                                            // "kmeans++" (D²-weighted random, useful
                                            // for diversifying nRuns), or "random"
                                            // (canonical KlustaKwik random init).
int   TimeMergeIter          = 100;         // Phase 2 iterations; 0 = disabled

// Three-phase chunked CEM parameters
float ChunkMinutes           = 7.0f;    // chunk size; 0 disables chunking
float ChunkOverlapMinutes    = 4.0f;    // trailing overlap appended to next chunk; 0 disables
float ChunkPreseedFraction   = 0.1f;    // fraction of spikes for Phase 0 preseed; 0 disables
char  ChunkFile[STRLEN]      = "";      // path to .chunks.N boundary file; overrides ChunkMinutes
float SamplingRate           = 0.0f;    // samples/sec; auto-filled from YAML at startup
float MergeThresh            = 0.0f;    // 0 = auto-calibrate to χ²(nDims, 0.99) at runtime
int   GlobalMergeIter        = 0;       // Phase 6 warm-start EM iterations; 0 skips Phase 7 entirely
int   SaveIntermediates      = 0;       // 0 = final-write only; 1 = also write per-phase .clu
// Phase 1a waveform realignment parameters
int   NbChannels             = 0;    ///< spike group channel count
int   NbSamplesPerSpike      = 0;    ///< waveform window width
int   PeakSampleIndex        = 0;    ///< 0-based spike peak within window
int   NbTotalChannels        = 0;    ///< total channels in .fil file
int   NbBytesPerSample       = 2;    ///< bytes per sample in .spk
std::vector<int> GroupChannelIds;    ///< ADC channel indices for this group
int   nRuns                  = 20;   ///< flat run count; 0 = legacy K×nStarts loop
int   TimeShiftAlignIter     = 5;    ///< Phase 1a alignment passes (0=skip; N runs with MStep between)

// ── Empirical prior ────────────────────────────────────────────────────────
char  PriorFile[STRLEN]      = "";   ///< path to .prior.N.yaml
int   AdaptiveMerge          = 1;    ///< per-pair d_eff-based MergeThresh (default on)
std::vector<float> ExternalPreseedCentres;  ///< populated by applyKKPrior()
int   MaxTimeShift           = 3;    ///< pre-shifted PCA basis half-width (0 disables, max 5)
int   TimeShiftMergeEnable   = 1;    ///< apply min-Mahalanobis probe during cluster deletion
int   TimeShiftSplitEnable   = 0;    ///< apply ±1-sample shift probe at split-test time
int   Phase2bMode            = 0;    ///< 0 = warm-start CEM, 1 = VB-GMM, 2 = CEM-with-splits + VB-GMM, 3 = residual-PCA refinement + dominant-channel xcorr realign
int   Phase2bEnableSplits    = 0;    ///< Phase 2b mode 0 only: enable TrySplits inside the inner CEM.
                                     ///< 0 (default, recommended for batch): disable.  Phase 2a's
                                     ///<   per-cluster CEM has already done split discovery; calling
                                     ///<   TrySplits again in Phase 2b is mostly redundant work and
                                     ///<   for high-K chunks (K > ~60 from Phase 2a's fan-out) costs
                                     ///<   244*K inner CEM invocations per outer iter — dominates
                                     ///<   the per-phase runtime.
                                     ///< 1: enable.  Use when working with data where Phase 2a's
                                     ///<   per-cluster discovery misses bimodality that only emerges
                                     ///<   when the chunk's clusters are seen together (rare).
int   Phase2bMaxIter         = 60;   ///< Max iterations of inner CEM per chunk in Phase 2b.  Lower
                                     ///< values bound the per-chunk runtime; warm-start CEM converges
                                     ///< in 10-30 iter typically.  Set to 0 to use the global MaxIter.
int   VBGMMMaxIter           = 50;   ///< VB-GMM max iterations (Phase2bMode 1 or 2)
float VBGMMConvTol           = 1e-3f;///< VB-GMM convergence tol on max |Δr| across (n,k)
float VBGMMAlpha0            = 1.0f; ///< VB-GMM Dirichlet concentration; <1 favours sparsity (more pruning)
float VBGMMBeta0             = 1.0f; ///< VB-GMM Normal prior strength on means
float VBGMMNu0Offset         = 2.0f; ///< VB-GMM Wishart d.o.f. = D + this; must be > 0
int   VBGMMPriorMode         = 0;    ///< 0 = isotropic global, 1 = per-cluster diagonal empirical, 2 = per-cluster FULL covariance empirical
float VBGMMPriorBlend        = 0.1f; ///< (mode 1, 2) regularization blend toward isotropic; 0 = pure empirical
// ── ResidualPCA — Phase 2b mode 3 hyperparameters ─────────────────────────
int   ResidualPCAIter             = 3;     ///< (Phase2bMode=3) max outer iterations of the residual-PCA refinement loop per chunk.  Early-stops when the fraction of label-changing spikes falls below ResidualPCAConvTol.
int   ResidualPCAComponents       = 3;     ///< (Phase2bMode=3) number of top eigenvectors of the per-cluster residual covariance to use as features for the residual VBGMM split test.  1–8 typical; default 3 captures the bulk of within-cluster waveform variation while keeping the per-cluster VBGMM dim low.
int   ResidualPCASubK             = 4;     ///< (Phase2bMode=3) initial K for the residual VBGMM (per cluster).  Dirichlet prior shrinks K toward the support of the data; this is the upper bound on how many sub-clusters can emerge from a single existing cluster in one iteration.
int   ResidualPCADominantChannels = 2;     ///< (DEPRECATED in mode 3) — formerly used by the per-iter dominant-channel xcorr; the post-loop realign now uses the shared XcorrDispatch::compute (all channels, normalised xcorr) for proper Klusters-style alignment.  Flag retained for backwards compatibility; ignored in the current implementation.
float ResidualPCAConvTol          = 0.01f; ///< (Phase2bMode=3) early-stop threshold: when the fraction of spikes whose label changed in this iteration is < ResidualPCAConvTol, the chunk's loop terminates.  0.01 = 1% of spikes changing.
float ResidualPCAMinScore         = 0.7f;  ///< (Phase2bMode=3) minimum normalised cross-correlation (in [-1,1]) required to accept a per-spike alignment shift in the post-loop XcorrDispatch realign pass.  Below this, the spike's shift stays at zero (matches Klusters' realignSpikes gate).  0.7 is the conventional value; lower (0.5) admits noisier alignments; higher (0.85) restricts to high-confidence matches.
int   AlignPcaCenter              = 0;     ///< patch83/patch84: (Phase2bMode=3) PCA-centering alignment.  0 = off (default, classic xcorr alignment, m_cumShift updated).  1 = patch83 second-pass refine AFTER the xcorr iter loop, m_cumShift updated.  2 = patch84 REPLACEMENT mode: xcorr iter loop SKIPPED, replaced with in-memory circular-shift PCA-centering iter loop, m_cumShift NEVER touched (so TimeShiftFinalize writes no .spk/.fet/.res changes for any chunk run in mode 2).  Mode 2 exists to avoid the temporal-dispersion failure mode that comes from cumulative-shift accumulation: highpass-filtered waveforms tolerate circular wrap at their edges (no DC content), the PCA features are recomputed from the shifted in-memory waveforms each iter, and the resulting clustering benefits from alignment while leaving on-disk waveforms intact.  The user re-extracts .spk/.fet from .fil out-of-band after clustering if they want disk content to match.
float TemplateMatchEigRatio  = 0.0f; ///< Phase 5 merge veto threshold: union-top-eig / max(per-cluster-top-eig). 0 disables (xcorr only).
int   DipSplitGlobalEnable   = 1;    ///< Phase 8 global DipSplit (post-Phase-7).  Set to 0 in chunked mode with drift; per-chunk Phase 1b DipSplit is unaffected.
int   DipSplit2D             = 0;    ///< 0 = test each PC1/PC2/PC3 individually (1D); 1 = directional scan in (PC1,PC2) plane (2D)
float CrossChunkDriftSigma   = 0.0f; ///< Phase 6 Pass 2 smoothness penalty width. Multiplies xcorr score by exp(-(dev/sigma)²/2) where dev = ||actual_displacement - expected|| / scatter, expected = mean displacement of Pass 1 confirmed matches between same chunk pair. 0 disables.
int   TimeShiftAlignPostMerge = 0;   ///< If 1, run TimeShiftAlignPhase one more time after Phase 6 EM, with the post-merge global cluster state.  Catches misalignments that arose from boundary spike reassignments in Phase 6 / Phase 7.
float TimeShiftAlignScoreThresh = 0.0f; ///< Phase 1a / 7a minimum Mahalanobis² improvement required to commit a per-spike shift in TimeShiftMergeTighten. Best non-baseline (δ≠0) candidate must satisfy `baselineMahal² - bestMahal² > threshold`; otherwise the spike stays at δ=0. 0.0 = no gate (pure argmin, original behaviour) — any improvement, however small, accepted. Raise to suppress micro-shifts from numerical noise compounding over `TimeShiftAlignIter` passes, or to keep Phase 6a from tightening a post-merge composite cluster mean around spikes that don't really belong (the "reinforce a bad Phase 6 merge" failure mode).  Typical experiment values: 0.5 (loose), 1.0 (moderate), 2.0 (strict).  Applies to all TimeShiftMergeTighten callers including Phase 1a, Phase 7a, and merge-time victim tightening.

// ── Per-phase cluster-mean alignment intervals ────────────────────────────
// Each flag enables a TimeShiftAlignPhase (cluster-mean xcorr/Mahal²) pass
// at the boundary AFTER the named phase completes.  Default 0 = off.  When
// any of these is non-zero, the pre-existing force-disable of
// MaxTimeShift / TimeShiftAlignIter is BYPASSED (the user has opted into
// alignment); sensible defaults are applied to MaxTimeShift and
// TimeShiftAlignIter if they would otherwise be zero.  Phase 6a continues
// to use the legacy TimeShiftAlignPostMerge flag for back-compat.
int   TimeShiftAlignAfterPhase1  = 0;  ///< Cluster-mean alignment after Phase 1 (initial per-chunk CEM).  Identical site to the original Phase 1a.
int   TimeShiftAlignAfterPhase1b = 0;  ///< Cluster-mean alignment after Phase 1b (per-chunk DipSplit).  New clusters from DipSplit get tightened around their own means.
int   TimeShiftAlignAfterPhase2  = 0;  ///< Cluster-mean alignment after the Phase 2 / 2a / 2b chunk-loop completes (refractory split + per-cluster CEM + chunk re-CEM all run interleaved per-chunk, sharing one insertion site).
int   TimeShiftAlignAfterPhase4  = 0;  ///< Cluster-mean alignment after Phase 5 (within-chunk template match).  Mergers consolidated similar units; re-tighten around merged means.
int   TimeShiftAlignAfterPhase5  = 0;  ///< Cluster-mean alignment after Phase 6 (cross-chunk model match).  Cross-chunk consolidation now spans multiple chunks; re-align spikes vs. the (weighted) global cluster mean before Phase 7's EM.

// ── Energy-COM (centre-of-mass) realignment ───────────────────────────────
// When enabled, after EACH active TimeShiftAlign* phase, computes the
// weighted-mean time of channel-summed waveform energy for every spike,
// applies an ADDITIVE integer shift to put that COM at the canonical
// PeakSampleIndex (bounded by the cumulative-shift cap), then calls
// RefeaturizeFromShifts to re-extract and re-project the affected spikes
// from .fil at the new offsets.
//
// Operates per-spike on the ORIGINAL .spk waveform (no cluster context):
// COM is a waveform-intrinsic anchor, complementary to the cluster-mean
// shift.  Cluster-mean handles cluster-fit drift; energy-COM handles
// spike-anchor inconsistency between .spk's captured peak and the
// canonical PeakSampleIndex.
int   EnergyCOMRealign      = 0;       ///< 0 = off; 1 = enable energy-COM realignment after each active cluster-mean alignment block.
int   EnergyCOMMetric       = 1;       ///< Energy metric for COM: 0 = sum |x|, 1 = sum x² (default, standard signal energy).

// Phase 6b — optional final mean-waveform subtraction merge.  Runs after
// all alignment is done (post-Phase-7a if enabled).  For each pair of
// live clusters, computes the normalised L2 residual between their mean
// waveforms (per-cluster aggregated across ALL chunks, with per-spike
// shifts applied):
//
//     D(i, j) = ||mean[i] - mean[j]||² / max(||mean[i]||², ||mean[j]||²)
//
// Pairs with D < MeanSubtractionMergeThresh are merged via union-find,
// smallest D first (transitive merges allowed).  Differs from Phase
// 5/6 xcorr matching in two key ways: (1) the metric is amplitude-
// sensitive (xcorr is amplitude-invariant), so two clusters with the
// same shape at very different amplitudes will NOT merge here — useful
// when amplitude carries identity (e.g. distinct neurons with similar
// shape); (2) operates on globally-aggregated means rather than per-
// chunk means, so it catches residual fragmentation that survived Phase
// 6's cross-chunk match.  Disabled by default — opt in with
// -MeanSubtractionMergeEnable 1.
int   MeanSubtractionMergeEnable = 0;    ///< 0 = off; 1 = run Phase 6b after Phase 7a
float MeanSubtractionMergeThresh = 0.05f;///< normalised residual D below which a pair merges.
                                          ///< Default is 0.05 because the implementation
                                          ///< searches over cyclic time-shifts τ ∈ [−K, K]
                                          ///< and takes the minimum D over shifts, which
                                          ///< systematically lowers D vs a single-shift
                                          ///< comparison.  Empirically D=0.30 merges
                                          ///< virtually all clusters; D=0.05 catches
                                          ///< genuine duplicates with ~10× SNR margin.
int   MeanSubtractionMergeMaxShift = 3;  ///< Phase 6b cyclic-shift search half-width.
                                          ///< For each pair, residual is computed at every
                                          ///< τ ∈ [−K, K] (cyclic time-shift on one mean
                                          ///< waveform) and the minimum is taken.  Default
                                          ///< 3 covers typical alignment-residual jitter
                                          ///< (cluster-mean alignment converges to ±1-2
                                          ///< samples; cyclic search adds robustness to
                                          ///< sub-sample drift between clusters).  Hard-
                                          ///< capped to nSamplesPerSpike/2 at runtime.

// ── Phase 7c — klusters-faithful per-spike realignment ────────────────────
// Ports the algorithm used by klusters' interactive "Realign top-ch" button
// (src/klusters/src/spikerealign.cpp).  Per-spike normalised xcorr against
// a pre-aligned cluster mean, sample-major.  Updates m_cumShift so that
// TimeShiftFinalize's RefeaturizeFromShifts re-extracts each spike from
// .fil at the new offset and reprojects through the saved PCA basis.
//
// Designed as an OPTIONAL final tightening pass at the end of Phase 7,
// after Phase 7a (TimeShiftAlignPhase) and Phase 7b (mean-subtraction
// merge).  Disabled by default; opt in with -KlustersRealignEnable 1.
//
// On the user's sirotaA-jg-000005 data (189k spikes, 8 channels, 32
// samples) the OpenMP CPU backend completes a full pass in ~1-2 seconds;
// the RTX 5070 Ti backend completes in well under 1 second.
int   KlustersRealignEnable   = 0;    ///< 0 = off; 1 = run Phase 7c after Phase 7b.
int   KlustersRealignMaxShift = 8;    ///< Search radius in samples.  Matches klusters
                                       ///< spikerealign.cpp default (maxShift=8).  Capped
                                       ///< at nSamplesPerSpike/4 at runtime so the cyclic
                                       ///< xcorr never gets close to half-window.
int   KlustersRealignMinSize  = 10;   ///< Skip clusters with fewer spikes (mean too noisy
                                       ///< to make a useful template).  Klusters skips
                                       ///< empty clusters but otherwise applies the same
                                       ///< algorithm regardless of size; we pick a small
                                       ///< floor to avoid degenerate single-spike "means".

// KnnSplitPerChunk parameters (Phase 2b.5, K-template chunk split).
// Disabled by default.  Enable with -KnnSplitPerChunkEnable 1 to run a
// per-chunk K-template nearest-mean partition between Phase 2b
// (ChunkReCEMPerChunk) and Phase 2c (alignment).  Generates new
// chunk-local clusters from high-residual-variance sources using the
// chunk's K best-isolated cluster means as a vocabulary; Phase 6
// cross-chunk template matching handles consolidation.
int   KnnSplitPerChunkEnable    = 0;
int   KnnSplitK                 = 10;
int   KnnSplitMinRefSize        = 50;
int   KnnSplitMinSourceSize     = 20;
int   KnnSplitMinNewClusterSize = 10;

// ---- Phase 4 rewrite (klusters-faithful KNN + adaptation modelling) ----
// KnnSplitMode: 0 = legacy nearest-template (KKE built-in, every spike
// reassigned to the closest reference mean — fragments source clusters
// up to K-way regardless of confidence), 1 = klusters-faithful K-nearest-
// neighbours majority vote (per-spike K-NN in feature space against a
// pool of reference SPIKES, with a majority-vote threshold; spikes with
// no clear majority stay in the source).  Mode 1 is the fix for the
// 3125-fragment cascade observed in the sirotaA group-6 benchmark.
// Default 0 (legacy behaviour preserved).
int   KnnSplitMode              = 0;
// WaveKnnMajorityThreshold: fraction of the K neighbours that must share
// the winning label for a spike to be reassigned.  Below this threshold
// the spike stays in source (residual bucket).  Klusters' GUI default
// is 0.6.  Stricter values (0.7–0.8) produce more residual but cleaner
// splits.  Only used in KnnSplitMode=1.
float WaveKnnMajorityThreshold  = 0.6f;
// WaveKnnResidualBecomesCluster: when KnnSplitMode=1, controls what
// happens to ambiguous (no-majority) spikes plus small confident winners
// (those below KnnSplitMinNewClusterSize):
//   1 (default, klusters semantics): they pool together; if the pool
//     reaches KnnSplitMinNewClusterSize it materialises as its own new
//     cluster ID (a "residual" cluster), otherwise stays in source.
//     Isolates ambiguous spikes from the source mean waveform so
//     downstream Phase 5 cross-chunk matching sees them separately.
//   0: small winners revert to source; ambiguous spikes stay in source;
//     no residual cluster ever materialised.
int   WaveKnnResidualBecomesCluster = 1;
// WaveKnnUseTraceFilter: when KnnSplitMode=1 and no explicit references
// are provided, controls auto-pick of refs vs sources.
//   1 (default, KKE mode): trace filter — clusters with tr(Σ)/nDim below
//     the chunk median are references; the rest are sources.  Pool and
//     source sets DISJOINT.  Cheap, but requires trace info to work.
//   0 (klusters mode): no trace filter.  Every sized-OK cluster is both
//     a pool member AND a source candidate; per-spike K-NN excludes
//     own-cluster pool entries so a source spike doesn't trivially vote
//     for itself.  Matches klusters' algorithm semantics.  More expensive
//     (no trusted-ref subset) but allows cluster A to receive spikes
//     from cluster B if their K-NN votes for B.
int   WaveKnnUseTraceFilter         = 1;
// WaveKnnSkipMuaCluster1: when KnnSplitMode=1, controls whether cluster 1
// (klusters MUA convention) is excluded from both pool and source
// candidates.  0 = include (KKE default; cluster 1 not reserved).
// 1 = exclude (klusters-compat).  Default 0.
int   WaveKnnSkipMuaCluster1        = 0;
// WaveKnnNoiseSourceProbability: probability that the noise cluster
// (cid=0) is added as a source candidate this Run.  0.0 = never (default;
// matches klusters' cid≤1 skip).  Use 0.1-0.3 to occasionally recover
// real-but-misclassified spikes from the noise cluster via KNN majority
// vote.  Noise cluster is NEVER part of the reference pool.
float WaveKnnNoiseSourceProbability = 0.0f;

// Phase4RefineEnable: master switch for the new Phase-4 rewrite.  When
// enabled, replaces the existing WithinChunkTemplateMatch loop with the
// new pipeline:  proxy_isi → adaptation model fit → cluster-quality
// scoring → amplitude-scaled xcorr_match with sub-sample alignment.
// Default 0 (legacy WithinChunkTemplateMatch preserved).
int   Phase4RefineEnable        = 0;
// Phase4RefineIters: number of refine-loop passes when Phase4RefineEnable=1.
// Replaces the existing TemplateMatchIters when the new pipeline is active.
// Default 10.
int   Phase4RefineIters         = 10;
// AdaptModelEnable: fit per-cluster ISI-conditional adaptation model
// (w_i ≈ w₀ + α·h(ISI;τ)·v) before computing residuals.  0 = no fit
// (residuals against raw mean), 1 = fit.  Default 1 when Phase4RefineEnable=1.
int   AdaptModelEnable          = 1;
// AdaptTauGridCSV: comma-separated grid of τ values in seconds for the
// adaptation model fit.  Empty string → default grid (5, 15, 50, 150 ms).
// Example: "0.003,0.010,0.030,0.100".
char  AdaptTauGridCSV[STRLEN]   = "";
// Phase4MinClusterSize: minimum cluster spikes for the refine-loop to
// touch a cluster.  Below this, the cluster is left alone.  Default 50.
int   Phase4MinClusterSize      = 50;
// XcorrResidualThresh: minimum residualScore for a within-chunk merge
// in the new pipeline (replaces TemplateMatchScore when Phase4RefineEnable=1).
// residualScore = fraction of waveform energy explained at α* alignment.
// Default 0.85; raise to 0.90 for tighter merges if false-merges seen.
float XcorrResidualThresh       = 0.85f;
// XcorrMaxShiftSamples: half-width of the integer-lag search in samples
// for the new xcorr_match.  Default 0 → use NbSamplesPerSpike/4 at runtime.
int   XcorrMaxShiftSamples      = 0;


// DipSplit parameters (Phase 8 bimodal splitter)
int   DipSplitEnable            = 1;     ///< 0 disables automatic DipSplit pass
int   DipSplitBeforePhase2b     = 1;     ///< Phase 2a.5: per-chunk DipSplit between Phase 2a and Phase 2b.
                                         ///< Catches single-dim bimodality that Phase 2a's parametric CEM
                                         ///< absorbs into inflated variance.  With this on, Phase 2b sees
                                         ///< finer-grained warm-start clusters and converges faster via
                                         ///< ConsiderDeletion-driven merging of any oversplits.  The
                                         ///< existing Phase 2.5 (after-Phase-2b) DipSplit pass continues
                                         ///< to run as a safety net.  Default 1 (enabled).

// ─── HullSplit: Phase 2a.6 k-NN-graph connected-components split ─────────
// Complementary mechanism to DipSplit at Phase 2a.5.  DipSplit catches 1D
// bimodality per PC marginal; HullSplit catches multi-D topology — two
// distinct units occupying separate regions of (PC1, PC2, ..., PC_K) space
// that no single PC marginal would reveal.  Default off pending real-data
// validation; cluster_hull_split.h has the algorithm rationale.
int   HullSplitEnable           = 0;     ///< 0 (default) disables.  1 enables Phase 2a.6.
int   HullSplitK                = 10;    ///< k for the k-NN graph.
int   HullSplitMinComponentSize = 50;    ///< components below this are absorbed.
float HullSplitMutualReachScale = 1.5f;  ///< edge cutoff = scale · median(d_k).
int   HullSplitUseMutualReach   = 1;     ///< 1 = HDBSCAN mutual-reachability; 0 = raw Euclidean.

// PerChannelSplit (Phase 2a.7) — see KlustaKwik.h for description.
int   PerChannelSplitEnable           = 0;
int   PerChannelSplitMinClusterSize   = 50;
float PerChannelSplitValleyThreshold  = 0.50f;
int   PerChannelSplitMinSubClusterSize = 25;
float PerChannelSplitBicMarginConstant = 12.0f;
float PerChannelSplitBicMarginPerLogN  = 6.0f;
float PerChannelSplitMinChannelSnrRatio = 0.5f;
int   PerChannelSplitUsePeakAmp        = 1;
int   PerChannelSplitUsePeakTime       = 1;
int   PerChannelSplitUseTroughAmp      = 1;
int   PerChannelSplitUseTroughTime     = 1;
int   DipSplitMinSize           = 50;    ///< min spikes per child cluster for accepted split
float DipSplitBloatFactor       = 1.0f;  ///< mahal²₉₀ > factor · χ²(d,0.9) triggers evaluation;
                                          ///< lowered from 2.0 because the χ² test is itself
                                          ///< already conservative — bimodal mixtures whose
                                          ///< covariance has been inflated by CEM to absorb
                                          ///< the separation can have mahal²₉₀ ≈ χ²(d,0.9),
                                          ///< barely passing even at factor=1.0.  Real
                                          ///< single-Gaussian clusters cluster around mahal²₉₀
                                          ///< = χ²(d,0.9) too; the elongation gate below is
                                          ///< the actual second line of defence.
float DipSplitElongationFactor  = 4.0f;  ///< secondary gate (OR with bloat): if eig_top1 ≥
                                          ///< factor · median(eig_top1..3) of the cluster's
                                          ///< covariance, evaluate dip even if bloat failed.
                                          ///< Catches the absorbed-bimodal case: a mixture of
                                          ///< two well-separated modes fitted as one inflated
                                          ///< Gaussian shows up as a strongly elongated
                                          ///< covariance (top eigenvalue ≫ next ones), even
                                          ///< though mahal²₉₀ stays near the χ² expectation.
                                          ///< Threshold 4.0 chosen by inspection: a unimodal
                                          ///< Gaussian rarely exceeds 3× elongation in 3 PCs;
                                          ///< two modes separated by ≥ 2σ inflate the top
                                          ///< eigenvalue past 5× before they cease to be a
                                          ///< single visible cluster.  Set 0.0 to disable
                                          ///< this gate (bloat-only behaviour).
float DipSplitValleyThresh      = 0.0f;  ///< min KDE valley depth to flag bimodality
int   SubspaceDims              = 0;     ///< 0=use all spatial dims (full feature space). >0=Phase 2a/2b run on top-N spatial features ranked by within-cluster (2a) or within-chunk (2b) variance, matches classic KlustaKwik -UseFeatures auto-K behavior. Recommended: 4–8 to escape BIC saturation at high dims.
int   SubspaceRecluster         = 1;     ///< 1=run per-cluster subspace CEM after Phase 2
float TemplateMatchScore        = 0.85f; ///< min xcorr for within-chunk template matching (Phase 5)
int   TemplateMatchIters        = 10;    ///< max within-chunk template match iterations
int   SplitRecurseDepth         = 8;     ///< max TrySplits recursion depth
float CrossChunkTemplateScore   = 0.80f; ///< min xcorr for cross-chunk template matching (Phase 2 Pass 3)
int   fSaveModel             = 0;        ///< 1 = write .model.N (debug only)
FILE *pModelFile             = nullptr;
int   SplitEvery             = 8;        ///< split-probe cadence in CEM iterations
FILE *logfp                  = nullptr;
FILE *Distfp                 = nullptr;
KlustaSave kSv;
float HugeScore              = 1e32f;

// Returns true if argv contains a flag of the form "-Name" — used to
// detect whether the user explicitly set a parameter on the command
// line.  Distinct from change_param() (param.c) which mutates the
// global; this helper is read-only and safe to call before/after
// argv processing.  Comparison is case-sensitive to match
// search_command_line() in param.c.
static bool cli_has_flag(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc; ++i) {
        if (argv[i] && argv[i][0] == '-' && std::strcmp(argv[i] + 1, name) == 0)
            return true;
    }
    return false;
}

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
    INT_PARAM(ParallelK);
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
    INT_PARAM(PeakSampleIndex);
    INT_PARAM(NbTotalChannels);
    INT_PARAM(NbBytesPerSample);
    INT_PARAM(nRuns);
    INT_PARAM(TimeShiftAlignIter);
    STRING_PARAM(PriorFile);
    INT_PARAM(AdaptiveMerge);
    INT_PARAM(MaxTimeShift);
    INT_PARAM(TimeShiftMergeEnable);
    INT_PARAM(TimeShiftSplitEnable);
    INT_PARAM(Phase2bMode);
    INT_PARAM(Phase2bEnableSplits);
    INT_PARAM(Phase2bMaxIter);
    INT_PARAM(VBGMMMaxIter);
    FLOAT_PARAM(VBGMMConvTol);
    FLOAT_PARAM(VBGMMAlpha0);
    FLOAT_PARAM(VBGMMBeta0);
    FLOAT_PARAM(VBGMMNu0Offset);
    INT_PARAM(VBGMMPriorMode);
    FLOAT_PARAM(VBGMMPriorBlend);
    INT_PARAM(ResidualPCAIter);
    INT_PARAM(ResidualPCAComponents);
    INT_PARAM(ResidualPCASubK);
    INT_PARAM(ResidualPCADominantChannels);
    FLOAT_PARAM(ResidualPCAConvTol);
    FLOAT_PARAM(ResidualPCAMinScore);
    INT_PARAM(AlignPcaCenter);   // patch83
    FLOAT_PARAM(TemplateMatchEigRatio);
    INT_PARAM(DipSplitGlobalEnable);
    INT_PARAM(DipSplit2D);
    FLOAT_PARAM(CrossChunkDriftSigma);
    INT_PARAM(TimeShiftAlignPostMerge);
    INT_PARAM(TimeShiftAlignAfterPhase1);
    INT_PARAM(TimeShiftAlignAfterPhase1b);
    INT_PARAM(TimeShiftAlignAfterPhase2);
    INT_PARAM(TimeShiftAlignAfterPhase4);
    INT_PARAM(TimeShiftAlignAfterPhase5);
    INT_PARAM(EnergyCOMRealign);
    INT_PARAM(EnergyCOMMetric);
    INT_PARAM(MeanSubtractionMergeEnable);
    FLOAT_PARAM(MeanSubtractionMergeThresh);
    INT_PARAM(MeanSubtractionMergeMaxShift);

    INT_PARAM(KlustersRealignEnable);
    INT_PARAM(KlustersRealignMaxShift);
    INT_PARAM(KlustersRealignMinSize);
    FLOAT_PARAM(TimeShiftAlignScoreThresh);
    INT_PARAM(KnnSplitPerChunkEnable);
    INT_PARAM(KnnSplitK);
    INT_PARAM(KnnSplitMinRefSize);
    INT_PARAM(KnnSplitMinSourceSize);
    INT_PARAM(KnnSplitMinNewClusterSize);
    // Phase-4 rewrite controls
    INT_PARAM(KnnSplitMode);
    FLOAT_PARAM(WaveKnnMajorityThreshold);
    INT_PARAM(WaveKnnResidualBecomesCluster);
    INT_PARAM(WaveKnnUseTraceFilter);
    INT_PARAM(WaveKnnSkipMuaCluster1);
    FLOAT_PARAM(WaveKnnNoiseSourceProbability);
    INT_PARAM(Phase4RefineEnable);
    INT_PARAM(Phase4RefineIters);
    INT_PARAM(AdaptModelEnable);
    STRING_PARAM(AdaptTauGridCSV);
    INT_PARAM(Phase4MinClusterSize);
    FLOAT_PARAM(XcorrResidualThresh);
    INT_PARAM(XcorrMaxShiftSamples);
    INT_PARAM(DipSplitEnable);
    INT_PARAM(DipSplitBeforePhase2b);
    INT_PARAM(HullSplitEnable);
    INT_PARAM(HullSplitK);
    INT_PARAM(HullSplitMinComponentSize);
    FLOAT_PARAM(HullSplitMutualReachScale);
    INT_PARAM(HullSplitUseMutualReach);
    INT_PARAM(PerChannelSplitEnable);
    INT_PARAM(PerChannelSplitMinClusterSize);
    FLOAT_PARAM(PerChannelSplitValleyThreshold);
    INT_PARAM(PerChannelSplitMinSubClusterSize);
    FLOAT_PARAM(PerChannelSplitBicMarginConstant);
    FLOAT_PARAM(PerChannelSplitBicMarginPerLogN);
    FLOAT_PARAM(PerChannelSplitMinChannelSnrRatio);
    INT_PARAM(PerChannelSplitUsePeakAmp);
    INT_PARAM(PerChannelSplitUsePeakTime);
    INT_PARAM(PerChannelSplitUseTroughAmp);
    INT_PARAM(PerChannelSplitUseTroughTime);
    INT_PARAM(DipSplitMinSize);
    FLOAT_PARAM(DipSplitBloatFactor);
    FLOAT_PARAM(DipSplitElongationFactor);
    FLOAT_PARAM(DipSplitValleyThresh);
    INT_PARAM(SubspaceDims);
    INT_PARAM(SubspaceRecluster);
    FLOAT_PARAM(TemplateMatchScore);
    INT_PARAM(TemplateMatchIters);
    INT_PARAM(SplitRecurseDepth);
    FLOAT_PARAM(CrossChunkTemplateScore);
    INT_PARAM(DistDump);
    FLOAT_PARAM(DistThresh);
    INT_PARAM(FullStepEvery);
    FLOAT_PARAM(ChangedThresh);
    BOOLEAN_PARAM(Log);
    BOOLEAN_PARAM(Screen);
    INT_PARAM(MaxIter);
    STRING_PARAM(StartCluFile);
    STRING_PARAM(RefineExisting);
    STRING_PARAM(RefineMode);
    INT_PARAM(RefineIters);
    FLOAT_PARAM(RefineMergeThresh);
    FLOAT_PARAM(RefineSplitMinDepth);
    INT_PARAM(RefineLockNoiseClu);
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

    // -----------------------------------------------------------------
    // Auto-fill spike-group params from the session YAML
    // -----------------------------------------------------------------
    // The session YAML (<FileBase>.yaml) carries authoritative values
    // for nbChannels / nSamples / peakSampleIndex / nTotalChannels /
    // samplingRate per spike group.  Reading them here removes the
    // need for the user to repeat them on every CLI invocation, and
    // — more importantly — eliminates a class of silent corruption
    // bugs where the CLI claims (say) 41 samples per spike but the
    // .spk files were extracted with 32.  KK then reads garbage past
    // the end of each spike and feature space collapses.
    //
    // CLI flags still win when present, both for diagnostic overrides
    // and for transitional cases where the YAML is missing or
    // out-of-date.  A flag like "-NbChannels 8" in argv unconditionally
    // wins; otherwise we fill the global from the YAML, and finally
    // hard-fail if any required value is still 0 after both passes.
    //
    // We use a struct of {global pointer, CLI name, YAML value} pairs
    // so the printed audit trail enumerates every field in one place.
    {
        const KKYamlSpikeParams yp = kkReadYamlSpikeParams(FileBase, ElecNo);
        if (!yp.valid) {
            fprintf(stderr,
                "[YAML] %s.yaml not found or unreadable for group %d — "
                "every parameter must come from the command line.\n",
                FileBase, ElecNo);
        } else {
            fprintf(stderr, "[YAML] %s.yaml group %d: nChan=%d nSamp=%d "
                            "peak=%d nTotalChan=%d sampRate=%g\n",
                    FileBase, ElecNo,
                    yp.nbChannels, yp.nbSamples, yp.peakSampleIndex,
                    yp.nTotalChannels, yp.samplingRate);
        }

        // Overlay YAML → globals only when the user didn't override on
        // the CLI.  Each fixup logs a one-line provenance note.
        // When CLI and YAML *both* specify a value and they disagree,
        // emit a loud warning: that mismatch is exactly the silent
        // corruption mode that this whole feature exists to avoid
        // (e.g. CLI says 41 samples per spike but the .spk files were
        // actually extracted with 32 — KK then reads garbage past
        // every spike and feature space collapses).  We still honour
        // the CLI override (escape hatch for transitional cases) but
        // make the disagreement impossible to miss.
        auto applyInt = [&](const char* name, int* slot, int yamlVal) {
            const bool onCli = cli_has_flag(argc, argv, name);
            if (onCli) {
                if (yamlVal > 0 && yamlVal != *slot) {
                    fprintf(stderr,
                        "  %-18s = %-7d  ⚠️  CLI OVERRIDE: YAML says %d "
                        "— check this is intentional!\n",
                        name, *slot, yamlVal);
                } else {
                    fprintf(stderr,
                        "  %-18s = %-7d  (CLI override; YAML had %d)\n",
                        name, *slot, yamlVal);
                }
            } else if (yamlVal > 0) {
                *slot = yamlVal;
                fprintf(stderr, "  %-18s = %-7d  (from YAML)\n", name, *slot);
            } else {
                fprintf(stderr,
                    "  %-18s = %-7d  (default — YAML missing)\n",
                    name, *slot);
            }
        };
        auto applyFloat = [&](const char* name, float* slot, double yamlVal) {
            const bool onCli = cli_has_flag(argc, argv, name);
            if (onCli) {
                const double diff = std::abs(yamlVal - double(*slot));
                if (yamlVal > 0 && diff > 1e-3) {
                    fprintf(stderr,
                        "  %-18s = %-7g  ⚠️  CLI OVERRIDE: YAML says %g "
                        "— check this is intentional!\n",
                        name, *slot, yamlVal);
                } else {
                    fprintf(stderr,
                        "  %-18s = %-7g  (CLI override; YAML had %g)\n",
                        name, *slot, yamlVal);
                }
            } else if (yamlVal > 0) {
                *slot = static_cast<float>(yamlVal);
                fprintf(stderr, "  %-18s = %-7g  (from YAML)\n", name, *slot);
            } else {
                fprintf(stderr,
                    "  %-18s = %-7g  (default — YAML missing)\n",
                    name, *slot);
            }
        };

        applyInt  ("NbChannels",        &NbChannels,        yp.nbChannels);
        applyInt  ("NbSamplesPerSpike", &NbSamplesPerSpike, yp.nbSamples);
        applyInt  ("PeakSampleIndex",   &PeakSampleIndex,   yp.peakSampleIndex);
        applyInt  ("NbTotalChannels",   &NbTotalChannels,   yp.nTotalChannels);
        applyFloat("SamplingRate",      &SamplingRate,      yp.samplingRate);

        // Required-value sanity check.  A 0 in any of these silently
        // produces wrong results (cluster collapse, garbage features,
        // out-of-bounds .fil reads), so we abort loudly instead.
        // PeakSampleIndex == 0 is technically legal (peak at sample 0)
        // but vanishingly unlikely in practice; we still allow it.
        if (NbChannels        <= 0) Error("NbChannels not set: pass -NbChannels or fix YAML\n");
        if (NbSamplesPerSpike <= 0) Error("NbSamplesPerSpike not set: pass -NbSamplesPerSpike or fix YAML\n");
        if (NbTotalChannels   <= 0) Error("NbTotalChannels not set: pass -NbTotalChannels or fix YAML\n");
        if (SamplingRate      <= 0) Error("SamplingRate not set: pass -SamplingRate or fix YAML\n");
    }

    // -----------------------------------------------------------------
    // Time-shift probe — force-disabled UNLESS per-phase alignment opt-in
    // -----------------------------------------------------------------
    // The Phase-1.5 time-shift probe (MaxTimeShift / TimeShiftAlignIter)
    // was an experimental refinement that fanned a (2N+1)-candidate PCA
    // basis over each spike and re-projected to find the alignment
    // minimising within-cluster Mahalanobis distance.  In practice it
    // did not reliably improve sort quality, and ndm_alignspikes (run
    // as a pre-pass over .spk before clustering) produced better
    // results at a fraction of the runtime cost.
    //
    // The implementation is kept in tree (KK::InitTimeShift /
    // TimeShiftFinalize / shiftprobe_disabled.cpp) and the legacy
    // `-MaxTimeShift` / `-TimeShiftAlignIter` CLI route remains
    // force-disabled by default.  However, if the user opts into ANY
    // of the new per-phase alignment intervals (TimeShiftAlignAfter*
    // or TimeShiftAlignPostMerge), the force-disable is bypassed and
    // sensible defaults are applied to MaxTimeShift / TimeShiftAlignIter
    // if those would otherwise be zero — without this, the per-phase
    // calls would early-return inside TimeShiftAlignPhase.
    const bool anyPhaseAlignSet =
           TimeShiftAlignAfterPhase1  != 0
        || TimeShiftAlignAfterPhase1b != 0
        || TimeShiftAlignAfterPhase2  != 0
        || TimeShiftAlignAfterPhase4  != 0
        || TimeShiftAlignAfterPhase5  != 0
        || TimeShiftAlignPostMerge    != 0;

    if (!anyPhaseAlignSet) {
        if (MaxTimeShift != 0 || TimeShiftAlignIter != 0) {
            fprintf(stderr,
                    "[notice] -MaxTimeShift / -TimeShiftAlignIter ignored; the "
                    "time-shift align/merge probe is disabled in this build (use "
                    "ndm_alignspikes for spike alignment, or one of "
                    "-TimeShiftAlignAfterPhase* to enable in-pipeline alignment). "
                    " For split-time alignment refinement (+/-1 sample), use "
                    "-TimeShiftSplitEnable 1.\n");
        }
        MaxTimeShift        = 0;
        TimeShiftAlignIter  = 0;
    } else {
        if (MaxTimeShift <= 0)        MaxTimeShift       = 3;
        if (TimeShiftAlignIter <= 0)  TimeShiftAlignIter = 1;
        fprintf(stderr,
                "[info] per-phase cluster-mean alignment enabled "
                "(MaxTimeShift=%d, TimeShiftAlignIter=%d, EnergyCOMRealign=%d)\n",
                MaxTimeShift, TimeShiftAlignIter, EnergyCOMRealign);
    }

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

// LockedStderr — see KlustaKwik.h for rationale.
//
// Format into a stack buffer first so the actual write is one fputs.  The
// critical section is shared with Output() (same omp critical name), so the
// kernel ordering of stdout/stderr bytes when `2>&1` merges the two streams
// is now deterministic at line granularity: a LockedStderr() call cannot
// interleave its bytes into the middle of an Output() call and vice versa.
//
// Buffer is 2 KB - sufficient for the multi-field GLOBAL STATE / CLUSTER
// STATE lines (the largest current call sites are < 200 chars); messages
// longer than 2 KB get truncated with no continuation marker, which is
// preferable to heap-allocating during diagnostic logging.
void LockedStderr(const char *fmt, ...) {
    char buf[2048];
    {
        va_list arg;
        va_start(arg, fmt);
        const int n = vsnprintf(buf, sizeof(buf), fmt, arg);
        va_end(arg);
        // n < 0: encoding error; n >= sizeof(buf): truncated.  Both cases:
        // buf is still null-terminated, fputs is safe.  We don't try to
        // signal truncation - this is a logging path, not data.
        (void)n;
    }
#ifdef _OPENMP
    #pragma omp critical(output_lock)
#endif
    {
        std::fputs(buf, stderr);
        std::fflush(stderr);
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

// -----------------------------------------------------------------------------
// pickInputPath — prefer canonical (.fet / .spk / .pca), fall back to stderiv
// D variant (.fetD / .spkD / .pcaD) when canonical is absent.
//
// Rationale: reextract pipelines produce D-suffixed variants for stderiv
// sorting.  Rather than requiring callers (ndm_subcluster_unmatched,
// ndm_reextractspikes{,_stderiv}, etc.) to symlink or rename before every
// invocation, KlustaKwik itself now walks the two candidates and picks
// whichever exists.
//
// If both exist simultaneously — typically because a script has symlinked
// the D variant to the canonical name to satisfy legacy tools —  the
// canonical path wins.  That matches the pre-existing reextract-script
// convention.
//
// If neither exists, the canonical path is returned.  The caller will then
// invoke fopen_safe() (or open the file directly) and fail with the normal
// "Could not open file" diagnostic, naming the canonical path that the user
// was expecting.  Emitting a D-variant name in the error message when the
// user's session never had a D variant would be confusing.
//
// Stat via fopen with mode "rb" rather than stat(2) so symlinked targets
// are followed transparently and permission errors on the D variant fall
// through to canonical (matching what would happen in a manual workflow).
// -----------------------------------------------------------------------------
int pickInputPath(char *out, size_t outSize,
                  const char *base, const char *ext, int elec) {
    // Canonical first
    snprintf(out, outSize, "%s.%s.%d", base, ext, elec);
    if (FILE *probe = fopen(out, "rb")) {
        fclose(probe);
        return 0;
    }

    // Try D variant
    char dPath[STRLEN + 32];
    snprintf(dPath, sizeof(dPath), "%s.%sD.%d", base, ext, elec);
    if (FILE *probe = fopen(dPath, "rb")) {
        fclose(probe);
        if (outSize > 0) {
            strncpy(out, dPath, outSize);
            out[outSize - 1] = '\0';
        }
        return 1;
    }

    // Neither: leave `out` = canonical path, let caller handle the error.
    return -1;
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
// ---------------------------------------------------------------------------
// _RunInlineDriftEstimation
//
// Called after the final .clu write in a chunked-mode run.  Uses the
// freshly written SESSION.clu.N as input to process_estimatedrift.py
// (per-unit amplitude-profile xcorr drift estimation) and then
// process_applydrift.py (adaptive chunk boundary computation), producing:
//
//   SESSION.drift          — per-window drift in µm (YAML)
//   SESSION.chunks.N       — adaptive KlustaKwik chunk boundaries
//   SESSION.dat.drift.P    — per-sample int16 drift signal (binary)
//
// The SESSION.chunks.N file is consumed by the NEXT KlustaKwik invocation
// via -ChunkFile SESSION.chunks.N, giving drift-corrected chunk boundaries
// without the user needing to run ndm_estimatedrift separately.
//
// Probe geometry (probeId, shankIndex, probeFile, probeLibraryPath) is
// read from the YAML by KlustaKwikYaml.cpp and passed to the script so
// it can convert xcorr lags from electrode sites to µm.
//
// nSpatialDims is nDims-1 (the time dimension is excluded from the drift
// estimate; the script uses waveform PTP amplitudes, not PCA features).
// ---------------------------------------------------------------------------
static void __attribute__((unused)) _RunInlineDriftEstimation(const char* fileBase,
                                       int         elecNo,
                                       int         /*nSpatialDims*/)
{
    // ── Guard: SESSION.drift already exists → skip ───────────────────────
    {
        char driftPath[STRLEN + 16];
        snprintf(driftPath, sizeof(driftPath), "%s.drift", fileBase);
        struct stat st;
        if (stat(driftPath, &st) == 0) {
            fprintf(stderr,
                    "KlustaKwik: %s already exists — skipping inline drift estimation.\n"
                    "  Delete it and re-run to refresh.\n", driftPath);
            return;
        }
    }

    // ── Guard: python3 available ─────────────────────────────────────────
    if (system("python3 --version > /dev/null 2>&1") != 0) {
        fprintf(stderr,
                "KlustaKwik: python3 not found — skipping inline drift estimation.\n"
                "  Install python3 + pyyaml + numpy to enable automatic drift correction.\n");
        return;
    }

    // ── Read probe geometry from YAML ────────────────────────────────────
    const KKYamlSpikeParams yp = kkReadYamlSpikeParams(fileBase, elecNo);
    if (!yp.valid || yp.probeId < 0 || yp.probeFile.empty()) {
        fprintf(stderr,
                "KlustaKwik: no probe geometry in YAML for group %d — "
                "skipping inline drift estimation.\n"
                "  Add probeId/probeFile to spikeDetection.channelGroups[%d] "
                "and the probes: list to enable.\n",
                elecNo, elecNo - 1);
        return;
    }

    // ── Locate YAML parameter file ────────────────────────────────────────
    char yamlPath[STRLEN + 8];
    snprintf(yamlPath, sizeof(yamlPath), "%s.yaml", fileBase);
    {
        struct stat st;
        if (stat(yamlPath, &st) != 0) {
            snprintf(yamlPath, sizeof(yamlPath), "%s.yml", fileBase);
            if (stat(yamlPath, &st) != 0) {
                fprintf(stderr,
                        "KlustaKwik: YAML file not found for session %s — "
                        "skipping drift estimation.\n", fileBase);
                return;
            }
        }
    }

    // ── Locate process_estimatedrift.py and process_applydrift.py ─────────
    // Search order: same directory as this binary, then PATH.
    auto findScript = [](const char* name) -> std::string {
        // Try alongside the running binary (readlink /proc/self/exe)
        char exePath[4096] = {};
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len > 0) {
            exePath[len] = '\0';
            // Replace binary name with script name
            char* slash = strrchr(exePath, '/');
            if (slash) {
                snprintf(slash + 1, sizeof(exePath) - (slash - exePath) - 1,
                         "%s", name);
                struct stat st;
                if (stat(exePath, &st) == 0) return std::string(exePath);
            }
        }
        // Fall back to PATH lookup
        return std::string(name);
    };
    const std::string estimateScript = findScript("process_estimatedrift.py");
    const std::string applyScript    = findScript("process_applydrift.py");

    // ── Count spike groups (for --n-groups arg) ─────────────────────────
    // Read from YAML: acquisitionSystem fields already loaded by SetupParams.
    // We need nSpikeGroups and nSamples per group for the script.
    // Use a quick Python one-liner rather than duplicating YAML parsing in C++.
    int nSpikeGroups = elecNo;  // conservative lower bound
    {
        char cmd[STRLEN * 3];
        snprintf(cmd, sizeof(cmd),
                 "python3 -c \"import yaml; d=yaml.safe_load(open('%s')); "
                 "print(len(d.get('spikeDetection',{}).get('channelGroups',[])))\" "
                 "2>/dev/null",
                 yamlPath);
        FILE* fp = popen(cmd, "r");
        if (fp) {
            int ng = 0;
            if (fscanf(fp, "%d", &ng) == 1 && ng > 0) nSpikeGroups = ng;
            pclose(fp);
        }
    }

    // Build --n-samples-per-group: read nSamples for each group from YAML
    std::string nSamplesArg;
    for (int g = 1; g <= nSpikeGroups; g++) {
        const KKYamlSpikeParams gp = kkReadYamlSpikeParams(fileBase, g);
        int ns = (gp.valid && gp.nbSamples > 0) ? gp.nbSamples : 32;
        if (!nSamplesArg.empty()) nSamplesArg += ",";
        nSamplesArg += std::to_string(ns);
    }

    fprintf(stderr,
            "KlustaKwik: running inline drift estimation\n"
            "  probe %d / shank %d / probeFile: %s\n",
            yp.probeId, yp.shankIndex, yp.probeFile.c_str());

    // ── Step 1: process_estimatedrift.py ────────────────────────────────
    {
        std::ostringstream cmd;
        cmd << "python3 \"" << estimateScript << "\"";
        cmd << " --session \""       << fileBase  << "\"";
        cmd << " --param-file \""    << yamlPath  << "\"";
        cmd << " --sampling-rate "     << SamplingRate;
        cmd << " --n-channels "        << (NbChannels > 0 ? NbChannels : 1);
        cmd << " --n-bits "            << 16;
        cmd << " --n-groups "          << nSpikeGroups;
        cmd << " --n-samples-per-group \"" << nSamplesArg << "\"";
        cmd << " --source-group "      << elecNo;
        cmd << " --output \""        << fileBase << ".drift\"";
        if (!yp.probeLibraryPath.empty())
            cmd << " --probe-library \"" << yp.probeLibraryPath << "\"";
        const std::string cmdStr = cmd.str();
        fprintf(stderr, "  %s\n", cmdStr.c_str());
        int rc = system(cmdStr.c_str());
        if (rc != 0) {
            fprintf(stderr,
                    "KlustaKwik: process_estimatedrift.py failed (exit %d)\n"
                    "  Drift estimation skipped; chunked boundaries not updated.\n", rc);
            return;
        }
    }

    // ── Step 2: process_applydrift.py ────────────────────────────────────
    // Writes SESSION.chunks.G for all groups on the same probe as elecNo.
    {
        // Build target groups: all groups sharing the same probeId
        std::string targetGroups;
        for (int g = 1; g <= nSpikeGroups; g++) {
            if (g == elecNo) continue;  // source group already included by script
            const KKYamlSpikeParams gp = kkReadYamlSpikeParams(fileBase, g);
            if (gp.valid && gp.probeId == yp.probeId) {
                if (!targetGroups.empty()) targetGroups += " ";
                targetGroups += std::to_string(g);
            }
        }

        std::ostringstream cmd;
        cmd << "python3 \"" << applyScript << "\"";
        cmd << " --session \""      << fileBase << "\"";
        cmd << " --drift-file \""   << fileBase << ".drift\"";
        cmd << " --source-group "     << elecNo;
        if (!targetGroups.empty())
            cmd << " --target-groups " << targetGroups;
        cmd << " --sampling-rate "    << SamplingRate;
        const std::string cmdStr = cmd.str();
        fprintf(stderr, "  %s\n", cmdStr.c_str());
        int rc = system(cmdStr.c_str());
        if (rc != 0) {
            fprintf(stderr,
                    "KlustaKwik: process_applydrift.py failed (exit %d)\n"
                    "  Chunk boundary files not written.\n", rc);
            return;
        }
    }

    fprintf(stderr,
            "KlustaKwik: drift estimation complete.\n"
            "  SESSION.drift written.\n"
            "  SESSION.chunks.%d written — use -ChunkFile SESSION.chunks.%d\n"
            "  on the next KlustaKwik run for drift-adaptive chunking.\n",
            elecNo, elecNo);
}

int main(int argc, char **argv) {
    float BestScore = HugeScore;
    kSv.BestScoreSave = BestScore;

    try {
        SetupParams(argc, argv);

        // ---------------------------------------------------------------
        // Populate GroupChannelIds from the YAML.
        //
        // The five scalar fields (NbChannels, NbSamplesPerSpike,
        // PeakSampleIndex, NbTotalChannels, SamplingRate) are now filled
        // earlier in SetupParams() with full provenance reporting and
        // hard-fail on missing values.  GroupChannelIds is a vector-
        // valued field consumed only by .fil-reading paths (drift
        // estimation, re-extraction); it lives here because some
        // downstream code expects it to be available before LoadData()
        // but after argument parsing.
        // ---------------------------------------------------------------
        {
            const KKYamlSpikeParams yp = kkReadYamlSpikeParams(FileBase, ElecNo);
            if (yp.valid && GroupChannelIds.empty() && !yp.channelIds.empty()) {
                GroupChannelIds = yp.channelIds;
                fprintf(stderr, "[YAML] GroupChannelIds=[");
                for (int i = 0; i < std::min((int)GroupChannelIds.size(), 4); i++)
                    fprintf(stderr, "%d%s", GroupChannelIds[i],
                            i + 1 < (int)GroupChannelIds.size() ? "," : "");
                if ((int)GroupChannelIds.size() > 4) fprintf(stderr, "...");
                fprintf(stderr, "]  (from YAML, group %d)\n", ElecNo);
            }
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

        // ── MergeThresh auto-calibration ─────────────────────────────────
        // MergeThresh is a Mahalanobis² threshold; the natural scale is
        // the chi-square distribution.  χ²(d, 0.99) means "two clusters
        // whose Mahal² distance exceeds this are >99% unlikely to be
        // the same Gaussian" — a sensible default that scales with
        // feature dimensionality.  The previous fixed default of 30
        // was right for ~17 dims and catastrophically large for
        // smaller feature spaces (everything merges into one).
        //
        // Wilson-Hilferty cube-root approximation: cheap, monotonic,
        // accurate to <1% for d ≥ 3.  Same formula used by the
        // existing "MergeThresh too large" warning in KK.cpp:2673,
        // kept consistent so the recommendation matches what we
        // auto-pick.  z_0.99 = 2.326347874.
        //
        // CLI -MergeThresh wins UNLESS the user explicitly passes 0, which
        // is the documented "auto-calibrate" sentinel.  Any positive value
        // bypasses auto-calibration entirely; negative is treated like 0.
        const bool cliMerge   = cli_has_flag(argc, argv, "MergeThresh");
        const bool wantAuto   = (!cliMerge && MergeThresh <= 0.0f)
                             || ( cliMerge && MergeThresh <= 0.0f);
        if (wantAuto) {
            const float d = static_cast<float>(K1.nDims);
            const float t = d * std::pow(
                1.0f - 2.0f / (9.0f * d) + 2.326347874f * std::sqrt(2.0f / (9.0f * d)),
                3.0f);
            MergeThresh = t;
            fprintf(stderr,
                    "[auto] MergeThresh = %.2f  (χ²(%d, 0.99); auto-calibrated "
                    "from .fet header%s)\n",
                    MergeThresh, K1.nDims,
                    cliMerge ? " — user passed -MergeThresh 0" : "");
        } else if (cliMerge) {
            // Sanity-check the user's positive override against χ²(d, 0.99)
            // so a wildly miscalibrated value is at least flagged here.
            // With AdaptiveMerge=1 the per-pair calibration handles
            // miscalibration anyway, so the warning is suppressed in that
            // case to avoid false alarms.
            const float d = static_cast<float>(K1.nDims);
            const float t99 = d * std::pow(
                1.0f - 2.0f / (9.0f * d) + 2.326347874f * std::sqrt(2.0f / (9.0f * d)),
                3.0f);
            if (!AdaptiveMerge && t99 > 0.0f && MergeThresh > 0.0f) {
                const float ratio = MergeThresh / t99;
                if (ratio < 0.5f || ratio > 3.0f) {
                    fprintf(stderr,
                            "[warn] MergeThresh=%.2f is %s χ²(%d, 0.99)=%.2f "
                            "by %.1f×.  Auto-default would be %.2f.\n",
                            MergeThresh,
                            ratio < 1.0f ? "below" : "above",
                            K1.nDims, t99,
                            ratio < 1.0f ? 1.0f / ratio : ratio,
                            t99);
                }
            }
        }

        // ── klustakwikExp: post-split shift-probe refeaturization ───────────
        // Load PCA basis + open .spk read-only once for the run.  If either
        // is unavailable (legacy session without .pca or .spk), the probe
        // silently disables itself and the rest of the clustering runs
        // identically to the canonical klustakwik.  MaxTimeShift controls
        // the half-width of the pre-shifted basis fan (0..5) and gates
        // initialisation entirely — independent of TimeShiftAlignIter, which only
        // controls the canonical xcorr realignment path.  Setting
        // TimeShiftAlignIter=0 with MaxTimeShift>0 disables only the xcorr
        // alignment stage while keeping the split/merge probe stages active.
        if (MaxTimeShift < 0) MaxTimeShift = 0;
        if (MaxTimeShift > 5) {
            Output("Warning: MaxTimeShift=%d clamped to 5 (N>5 is excessive; "
                   "the PCA support is typically <= data2use samples)\n",
                   MaxTimeShift);
            MaxTimeShift = 5;
        }
        // Initialise time-shift machinery if any consumer is enabled.
        // Split probe (TimeShiftSplitCluster, called from TrySplits) only
        // tests δ ∈ {−1, 0, +1}, so a half-width of 1 is sufficient for
        // split-only operation.  When MaxTimeShift > 0 (align/merge paths),
        // use the larger configured half-width.
        const int initHalfWidth = (MaxTimeShift > 0) ? MaxTimeShift : 1;
        if (MaxTimeShift > 0 || TimeShiftSplitEnable != 0)
            K1.InitTimeShift(NbChannels, NbSamplesPerSpike, initHalfWidth);

        // ── Empirical prior ────────────────────────────────────────────────────
        if (PriorFile[0] != '\0') {
            const KKPrior prior = loadKKPrior(PriorFile);
            applyKKPrior(prior);
            if (!ExternalPreseedCentres.empty())
                K1.preseedCentres = std::move(ExternalPreseedCentres);
        }

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
            {
                // Print the actual .fet path we loaded.  pickInputPath gives
                // us the canonical name if it exists, else the .fetD variant;
                // matching what LoadData() already resolved in K1.
                char fetBanner[STRLEN + 16];
                const int _fetVar = pickInputPath(fetBanner, sizeof(fetBanner),
                                                  FileBase, "fet", ElecNo);
                fprintf(stderr, "KlustaKwikExp  %s%s  [build 2026-04-22 shift-probe]\n",
                        fetBanner, (_fetVar == 1) ? "  (stderiv variant)" : "");
            }
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
            } else if (strcmp(InitMethod, "kmeans++") == 0) {
                fprintf(stderr, "  mode: two-phase k-means++\n");
            } else {
                fprintf(stderr, "  mode: original random-init\n");
            }
            fflush(stderr);
        }

        // ── RefineExisting short-circuit ───────────────────────────────────
        // When -RefineExisting <path> is given, skip the K-sweep entirely.
        // RefineExistingClustering loads the supplied .clu, fits full-Gaussian
        // models from the existing assignments, and runs reassign / split /
        // merge phases.  K_in defines the operating point — there's no point
        // in iterating MinClusters..MaxClusters because the seed itself
        // pins K.  ParallelK is also irrelevant here: we run exactly one
        // refinement, single-threaded at the outer level (RunEMLoop and the
        // pairwise merge already exploit OpenMP internally).
        if (*RefineExisting) {
            if (strcmp(RefineMode, "off") == 0) {
                Output("RefineMode=off — exiting without changing %s\n",
                       RefineExisting);
                exit(0);
            }
            // Effective merge threshold: 0 means "inherit from MergeThresh".
            // MergeThresh itself defaults to 0 = auto-calibrate to χ²(d, 0.99).
            // KK::RefineExistingClustering requires a concrete value, so we
            // compute χ²(nDims-1, 0.99) inline if both knobs are zero.
            float effMergeThresh = (RefineMergeThresh > 0.0f)
                                 ? RefineMergeThresh
                                 : MergeThresh;
            if (effMergeThresh <= 0.0f) {
                // Wilson–Hilferty approx to χ²(d, 0.99).  Conservative — the
                // true value at d=21 is 38.93; the approx gives 38.96.
                const int   d = std::max(1, K1.nDims - 1);
                const double z = 2.3263478740408408;  // standard normal 0.99
                const double m = 1.0 - 2.0 / (9.0 * d);
                const double k = 1.0 - 2.0 / (9.0 * d) + z * std::sqrt(2.0 / (9.0 * d));
                effMergeThresh = static_cast<float>(d * std::pow(k / m, 3.0));
                Output("RefineExisting: auto-calibrated MergeThresh = "
                       "χ²(%d, 0.99) ≈ %.2f\n", d, effMergeThresh);
            }

            BestScore = K1.RefineExistingClustering(
                RefineExisting, RefineMode, RefineIters,
                effMergeThresh, RefineSplitMinDepth,
                RefineLockNoiseClu != 0,
                /*chunkBoundsSec=*/ extChunkBoundsSec);
            kSv.BestScoreSave = BestScore;
            Output("RefineExisting %s -> K=%d  Score %f\n",
                   RefineMode, K1.nClustersAlive, BestScore);
            for (int p = 0; p < K1.nPoints; p++) K1.BestClass[p] = K1.Class[p];
            SaveOutput(K1.BestClass);  // always save: this is the final output
            if (fSaveModel) K1.SaveBestMeans();
            // Skip the K-sweep / chunked-CEM dispatch entirely.
            return 0;
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

        // Dispatch: three-phase chunked > two-phase (farthest|kmeans++) > original random
        const bool useFarthest  = (strcmp(InitMethod, "farthest") == 0)
                              || (strcmp(InitMethod, "kmeans++") == 0);

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

#ifdef _OPENMP
        const int nCoresAvail = omp_get_max_threads();
#else
        const int nCoresAvail = 1;
#endif
        // When nRuns > 0 in chunked mode, the outer loop collapses to nRuns
        // independent runs.  MinClusters/MaxClusters remain as per-chunk
        // TrySplits bounds only; the K sweep is removed.
        // When nRuns = 0, fall back to the original (MaxClusters-MinClusters+1)×nStarts
        // loop for backward compatibility with non-chunked usage.
        // nRuns > 0 in chunked mode means per-chunk restarts inside
        // RunChunkedCEM; the outer pipeline runs exactly once.
        // nRuns == 0 in non-chunked mode keeps the K×nStarts outer loop.
        // In chunked mode the outer K sweep is irrelevant — per-chunk TrySplits
        // handles cluster count variation. The outer loop always runs once;
        // nRuns controls per-chunk restarts inside RunChunkedCEM.
        // In non-chunked mode the original K×nStarts sweep is preserved.
        const int  nRunsEff = useChunked ? 1
                           : (MaxClusters - MinClusters + 1) * nStarts;
        const int nWorkers     = (ParallelK > 0) ? std::min(ParallelK, nRunsEff) : 1;
        const int threadsPerJob = std::max(1, nCoresAvail / nWorkers);

        // ── Serial path (ParallelK=0) ───────────────────────────────────────
        if (nWorkers == 1) {
            for (int run = 0; run < nRunsEff; run++) {
                const int K   = MinClusters + run / nStarts;
                const int i   = run % nStarts;
                K1.nStartingClusters = K;
                K1.minClustersAlive  = MinClusters;
                fprintf(stderr, "  K=%d/%d start=%d/%d\r",
                        K, MaxClusters, i + 1, nStarts);
                fflush(stderr);
                Output("Run %d / %d  (K=%d)...\n", run + 1, nRunsEff, K);
                srand(RandomSeed + i);

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
                    kSv.BestScoreSave = BestScore;
                    for (int p2 = 0; p2 < K1.nPoints; p2++) K1.BestClass[p2] = K1.Class[p2];
                    if (SaveIntermediates) SaveOutput(K1.BestClass);
                }
                Output("\n");
            }
        } else {
        // ── Parallel path (ParallelK>0): flatten all (K, start) pairs ──────
        //
        // Each job is an independent KK clone with its own KlustaSave.
        // GPU is disabled on all clones (cpu-only); Phase-3 GPU runtime is
        // negligible compared to Phase-1 OMP chunk speedup.
        struct KJob { int K; int run; };
        std::vector<KJob> jobs;
        jobs.reserve(nRunsEff);
        for (int run = 0; run < nRunsEff; run++) {
            const int K = MinClusters + run / nStarts;
            jobs.push_back({K, run});
        }

        const int nJobs = (int)jobs.size();

        fprintf(stderr,
                "ParallelK=%d: %d jobs, %d concurrent workers, "
                "%d OMP threads/job\n",
                ParallelK, nJobs, nWorkers, threadsPerJob);

        // Pre-allocate all nJobs KK clones and KlustaSave objects upfront.
        // With 700 GB RAM this is fine: 117 jobs × ~700 MB each ≈ 80 GB.
        // Each worker[j] is fully independent — no shared state during the
        // parallel loop, so no locks are needed.
        std::vector<KlustaSave> workerKsv(nJobs);
        std::vector<KK>         workers(nJobs);
        for (int j = 0; j < nJobs; j++) {
            K1.cloneInto(workers[j], threadsPerJob);
            workers[j].nStartingClusters = jobs[j].K;
            workers[j].minClustersAlive  = MinClusters;
            workerKsv[j].BestScoreSave   = HugeScore;
            workerKsv[j].BestWeight.SetSize(MaxPossibleClusters);
            workerKsv[j].BestMean.SetSize(MaxPossibleClusters * K1.nDims);
            workerKsv[j].BestAliveIndex.resize(MaxPossibleClusters);
            workerKsv[j].nDims    = K1.nDims;
            workerKsv[j].FileBase = K1.ksv().FileBase;
            workers[j].pKsv       = &workerKsv[j];
        }

        std::vector<float> jobScores(nJobs, HugeScore);

#ifdef _OPENMP
        omp_set_nested(1);
        omp_set_max_active_levels(2);
        #pragma omp parallel for schedule(dynamic) num_threads(nWorkers)
#endif
        for (int j = 0; j < nJobs; j++) {
            const int K   = jobs[j].K;
            const int run = jobs[j].run;

            srand(RandomSeed + run);

            float score;
            if (useExtChunks)
                score = workers[j].RunChunkedCEM(
                    extChunkBoundsSec, SamplingRate,
                    MergeThresh, GlobalMergeIter, TimeMergeIter);
            else if (useChunked)
                score = workers[j].RunChunkedCEM(
                    ChunkMinutes, SamplingRate,
                    MergeThresh, GlobalMergeIter, TimeMergeIter,
                    ChunkOverlapMinutes, ChunkPreseedFraction);
            else if (useFarthest)
                score = workers[j].CEMTwoPhase(TimeMergeIter);
            else
                score = workers[j].CEM();

            jobScores[j] = score;

            fprintf(stderr, "  K=%d/%d start=%d/%d score=%.6g\n",
                    K, MaxClusters, run + 1, nStarts, (double)score);
            fflush(stderr);
        }

#ifdef _OPENMP
        omp_set_nested(0);
        omp_set_max_active_levels(1);
#endif

        // ── Reduction: find overall best, update K1 and global kSv ─────────
        for (int j = 0; j < nJobs; j++) {
            Output("%d->%d Clusters: Score %f, best is %f\n",
                   jobs[j].K, workers[j].nClustersAlive,
                   jobScores[j], BestScore);
            if (jobScores[j] < BestScore) {
                Output("THE BEST YET! (K=%d start=%d)\n",
                       jobs[j].K, jobs[j].run + 1);
                BestScore              = jobScores[j];
                kSv.BestScoreSave      = BestScore;
                kSv.nDimsBest          = workerKsv[j].nDimsBest;
                kSv.nBestClustersAlive = workerKsv[j].nBestClustersAlive;
                kSv.cEStepCallsSave    = workerKsv[j].cEStepCallsSave;
                kSv.BestAliveIndex     = workerKsv[j].BestAliveIndex;
                kSv.BestWeight         = workerKsv[j].BestWeight;
                kSv.BestMean           = workerKsv[j].BestMean;
                K1.bestCholFlat        = workers[j].bestCholFlat;
                for (int p2 = 0; p2 < K1.nPoints; p2++)
                    K1.BestClass[p2]   = workers[j].Class[p2];  // Class[] = final EM state; BestClass[] is never set on workers
                if (SaveIntermediates) SaveOutput(K1.BestClass);
            }
        }
        }  // end parallel path

        SaveOutput(K1.BestClass);   // final write — always runs regardless of SaveIntermediates
        fprintf(stderr, "  done                    \n");  // clear the \r progress line

        // ── klustakwikExp: finalize shift-probe ─────────────────────────────
        // Hand m_cumShift to RefeaturizeFromShifts + WritePhase15Checkpoint.
        // This is the ONLY point at which .fil is read and .spk/.fet are
        // rewritten — scoped to spikes whose cumulative shift is non-zero.
        // Runs AFTER SaveOutput so the .clu.N already reflects final cluster
        // assignments; WritePhase15Checkpoint only touches .spk and .fet.
        K1.TimeShiftFinalize(NbChannels, NbSamplesPerSpike);

        // ── Inline drift estimation (chunked mode only) ───────────────────────
        //
        // When KlustaKwik ran in three-phase chunked mode AND the YAML parameter
        // file has probe geometry (probeId + probeFile in the probes: list), we
        // automatically run process_estimatedrift.py + process_applydrift.py as
        // subprocesses to produce SESSION.drift and SESSION.chunks.N.
        //
        // These files are used by the NEXT KlustaKwik invocation via -ChunkFile,
        // which gives drift-adaptive chunk boundaries derived from the unit
        // spatial profiles of THIS run's curated output.
        //
        // Skipped when:
        //   - Not running in chunked mode (-ChunkMinutes = 0 and no -ChunkFile)
        //   - -ChunkFile was already provided (drift-adaptive mode already active)
        //   - YAML has no probeId / probeFile for this electrode group
        //   - SESSION.drift already exists (prevents redundant re-estimation)
        //   - python3 not in PATH
        //
        // The nSamples value is read from the YAML for this group so
        // process_estimatedrift.py uses the correct waveform dimensions.
        //if (useChunked && !(*ChunkFile)) {
        //    _RunInlineDriftEstimation(FileBase, ElecNo, K1.nDims - 1);
        //}

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
