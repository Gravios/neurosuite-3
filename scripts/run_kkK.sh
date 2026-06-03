#!/usr/bin/env bash
# =============================================================================
#  run_kkexp_v8.sh  -  KiloKlustaKwik full-suite run script
# =============================================================================
#  All args live in the ARGS=( ) array below, grouped by PIPELINE PHASE in
#  execution order.  ACTIVE lines are the ones from your v7 script.  Every other
#  registered parameter is present but COMMENTED OUT at its built-in default --
#  flip one on by deleting the leading '# '.
#
#  Removed from v7 (not registered in the current build -> would be dead args):
#    -MedianSubdimSplit* (Phase 2a.8 was removed),  -KlustersRealignUseMedian
#  De-duplicated:  -CrossChunkDriftSigma (v7 set it twice; the 2.2 value is kept)
#  Changed:        -Phase2bMaxIter 0 -> 60  (0 meant 'use global MaxIter' = 500)
#
#  Validate:  bash -n run_kkexp_v8.sh
# =============================================================================
set -euo pipefail

KKEXP=${KKEXP:-KiloKlustaKwik}
DATASET=${1:?usage: $0 <session-base> <group>}
GROUP=${2:?usage: $0 <session-base> <group>}

ARGS=(

  # ===== ACQUISITION GEOMETRY (normally auto-filled from the session YAML) =====
  # -NbChannels                           0                             # spike group channel count
  # -NbSamplesPerSpike                    0                             # waveform window width
  # -PeakSampleIndex                      0                             # 0-based spike peak within window
  # -NbTotalChannels                      0                             # total channels in .fil file
  # -NbBytesPerSample                     2                             # bytes per sample in .spk
  # -SamplingRate                         0.0                           # samples/sec; auto-filled from YAML at startup
  # -ElecNo                               1
  # -UseFeatures                          all                           # feature columns to cluster on; auto from .fet nFeatures

  # ===== CORE CEM ENGINE (the classification-EM every phase calls) =====
  #   nRuns = flat run count; MergeThresh 0 auto-calibrates to chi2(nDims,0.99).
  # -MinClusters                          2
  -MaxClusters                          200                             # canonical jg05/eb05 sweep value
  -MaxPossibleClusters                  10000                           # ≈ 2.5× MaxClusters; hard cap on cluster ID space
  # -nStarts                              1
  -nRuns                                5                               # flat run count; 0 = legacy K×nStarts loop
  # -ParallelK                            0                             # 0 = serial; N = concurrent (K,start) workers
  # -InitMethod                           farthest                      # init seeding: "farthest" (deterministic) | "random"
  -PenaltyMix                           0.0
  # -MaxIter                              500
  # -FullStepEvery                        10
  # -ChangedThresh                        0.05
  # -SplitEvery                           8                             # split-probe cadence in CEM iterations
  # -SplitRecurseDepth                    8                             # max TrySplits recursion depth
  -AdaptiveMerge                        0                               # per-pair d_eff-based MergeThresh (default on)
  -MergeThresh                          46                              # 0 = auto-calibrate to χ²(nDims, 0.99) at runtime
  # -DistThresh                           static_cast<float>(std::log(1000.0))

  # ===== PHASE 0 - CHUNKING =====
  #   ChunkMinutes 0 disables chunking.
  -ChunkMinutes                         5                               # chunk size; 0 disables chunking
  -ChunkOverlapMinutes                  2                               # trailing overlap appended to next chunk; 0 disables
  -ChunkPreseedFraction                 0.1                             # fraction of spikes for Phase 0 preseed; 0 disables
  # -ChunkFile                            ""
  -PreseedCacheFile                     ${DATASET}.preseed.${GROUP}.cache
  # -PriorFile                            ""

  # ===== PHASE 1a - PER-SPIKE TIME-SHIFT ALIGNMENT (legacy; retired - use ndm_alignspikes) =====
  #   Keep all 0; align up front with ndm_alignspikes.
  -TimeShiftAlignIter                   0                               # Phase 1a alignment passes (0=skip; N runs with MStep between)
  -MaxTimeShift                         5                               # pre-shifted PCA basis half-width (0 disables, max 5)
  -TimeShiftMergeEnable                 0                               # apply min-Mahalanobis probe during cluster deletion
  -TimeShiftSplitEnable                 0                               # apply ±1-sample shift probe at split-test time
  # -TimeShiftAlignScoreThresh            0.0                           # Phase 1a / 7a minimum Mahalanobis² improvement required to commit a per-spike shift in TimeShiftMergeTighten. Best non-baseline (δ≠0) candidate must satisfy `baselineMahal² - bestMahal² > threshold`; otherwise the spike stays at δ=0. 0.0 = no gate (pure argmin, original behaviour) — any improvement, however small, accepted. Raise to suppress micro-shifts from numerical noise compounding over `TimeShiftAlignIter` passes, or to keep Phase 6a from tightening a post-merge composite cluster mean around spikes that don't really belong (the "reinforce a bad Phase 6 merge" failure mode). Typical experiment values: 0.5 (loose), 1.0 (moderate), 2.0 (strict). Applies to all TimeShiftMergeTighten callers including Phase 1a, Phase 7a, and merge-time victim tightening.
  -TimeShiftAlignAfterPhase1            0                               # Cluster-mean alignment after Phase 1 (initial per-chunk CEM). Identical site to the original Phase 1a.
  -TimeShiftAlignAfterPhase1b           0                               # Cluster-mean alignment after Phase 1b (per-chunk DipSplit). New clusters from DipSplit get tightened around their own means.
  -TimeShiftAlignAfterPhase2            0                               # Cluster-mean alignment after the Phase 2 / 2a / 2b chunk-loop completes (refractory split + per-cluster CEM + chunk re-CEM all run interleaved per-chunk, sharing one insertion site).
  -TimeShiftAlignAfterPhase4            0                               # Cluster-mean alignment after Phase 5 (within-chunk template match). Mergers consolidated similar units; re-tighten around merged means.
  -TimeShiftAlignAfterPhase5            0                               # Cluster-mean alignment after Phase 6 (cross-chunk model match). Cross-chunk consolidation now spans multiple chunks; re-align spikes vs. the (weighted) global cluster mean before Phase 7's EM.
  -TimeShiftAlignPostMerge              0                               # If 1, run TimeShiftAlignPhase one more time after Phase 6 EM, with the post-merge global cluster state. Catches misalignments that arose from boundary spike reassignments in Phase 6 / Phase 7.
  -EnergyCOMRealign                     0                               # 0 = off; 1 = enable energy-COM realignment after each active cluster-mean alignment block.
  # -EnergyCOMMetric                      1                             # Energy metric for COM: 0 = sum |x|, 1 = sum x² (default, standard signal energy).

  # ===== PHASE 1b - PER-CHUNK DipSplit (DipSplit* are reused by 2a.5 and Phase 8) =====
  -DipSplitEnable                       1                               # 0 disables automatic DipSplit pass
  # -DipSplit2D                           0                             # 0 = test each PC1/PC2/PC3 individually (1D); 1 = directional scan in (PC1,PC2) plane (2D)
  -DipSplitMinSize                      25                              # min spikes per child cluster for accepted split
  # -DipSplitBloatFactor                  1.0                           # mahal²₉₀ > factor · χ²(d,0.9) triggers evaluation;
  # -DipSplitElongationFactor             4.0                           # secondary gate (OR with bloat): if eig_top1 ≥
  # -DipSplitValleyThresh                 0.0                           # min KDE valley depth to flag bimodality
  -DipSplitBeforePhase2b                1                               # Phase 2a.5: per-chunk DipSplit between Phase 2a and Phase 2b.

  # ===== PHASE 2 - PER-CHUNK REFRACTORY SPLIT + SUBSPACE RECLUSTER =====
  #   Phase2SplitTimeLimitSec caps the split phases AND each RunEMLoop iteration.
  -SubspaceRecluster                    1                               # 1=run per-cluster subspace CEM after Phase 2
  -SubspaceDims                         3                               # 0=use all spatial dims (full feature space). >0=Phase 2a/2b run on top-N spatial features ranked by within-cluster (2a) or within-chunk (2b) variance, matches classic KlustaKwik -UseFeatures auto-K behavior. Recommended: 4–8 to escape BIC saturation at high dims.
  -RefractorySplitMaxIter               10                              # Max EM iterations for the Phase 2 refractory split's per-cluster
  -Phase2SplitTimeLimitSec              30                              # wall-clock cap (s) per per-chunk split phase (PerChannel/WaveKnn); 0 = unlimited
  -TimeMergeIter                        300                             # Phase 2 iterations; 0 = disabled

  # ===== PHASE 2a.6 - HULL SPLIT (mutual-reachability kNN) =====
  -HullSplitEnable                      1                               # 0 (default) disables. 1 enables Phase 2a.6.
  -HullSplitK                           10                              # k for the k-NN graph.
  -HullSplitMinComponentSize            75                              # components below this are absorbed.
  -HullSplitMutualReachScale            1.4                             # edge cutoff = scale · median(d_k).
  -HullSplitUseMutualReach              1                               # 1 = HDBSCAN mutual-reachability; 0 = raw Euclidean.

  # ===== PHASE 2a.7 - PER-CHANNEL amp/phase SPLIT =====
  -PerChannelSplitEnable                1
  # -PerChannelSplitMinClusterSize        50
  -PerChannelSplitValleyThreshold       0.20
  -PerChannelSplitMinSubClusterSize     25
  # -PerChannelSplitBicMarginConstant     12.0
  # -PerChannelSplitBicMarginPerLogN      6.0
  -PerChannelSplitMinChannelSnrRatio    0.5
  # -PerChannelSplitUsePeakAmp            1
  # -PerChannelSplitUsePeakTime           1
  # -PerChannelSplitUseTroughAmp          1
  # -PerChannelSplitUseTroughTime         1                             # AlternatingSplitMerge — see KlustaKwik.h for description.

  # ===== PHASE 2b - CHUNK RE-CEM =====
  #   Phase2bMaxIter 0 = use global MaxIter (500); set a real cap.
  # -Phase2bMode                          0                             # 0 = warm-start CEM, 1 = VB-GMM, 2 = CEM-with-splits + VB-GMM, 3 = residual-PCA refinement + dominant-channel xcorr realign
  -Phase2bEnableSplits                  0                               # Phase 2b mode 0 only: enable TrySplits inside the inner CEM.
  -Phase2bMaxIter                       60                              # Max iterations of inner CEM per chunk in Phase 2b. Lower

  # ===== PHASE 2b - VB-GMM variant (Phase2bMode 1 or 2 only) =====
  # -VBGMMMaxIter                         50                            # VB-GMM max iterations (Phase2bMode 1 or 2)
  # -VBGMMConvTol                         1e-3                          # VB-GMM convergence tol on max |Δr| across (n,k)
  # -VBGMMAlpha0                          1.0                           # VB-GMM Dirichlet concentration; <1 favours sparsity (more pruning)
  # -VBGMMBeta0                           1.0                           # VB-GMM Normal prior strength on means
  # -VBGMMNu0Offset                       2.0                           # VB-GMM Wishart d.o.f. = D + this; must be > 0
  # -VBGMMPriorMode                       0                             # 0 = isotropic global, 1 = per-cluster diagonal empirical, 2 = per-cluster FULL covariance empirical
  # -VBGMMPriorBlend                      0.1                           # (mode 1, 2) regularization blend toward isotropic; 0 = pure empirical

  # ===== PHASE 2b - residual-PCA variant (Phase2bMode 3 only) =====
  # -ResidualPCAIter                      3                             # (Phase2bMode=3) max outer iterations of the residual-PCA refinement loop per chunk. Early-stops when the fraction of label-changing spikes falls below ResidualPCAConvTol.
  # -ResidualPCAComponents                3                             # (Phase2bMode=3) number of top eigenvectors of the per-cluster residual covariance to use as features for the residual VBGMM split test. 1–8 typical; default 3 captures the bulk of within-cluster waveform variation while keeping the per-cluster VBGMM dim low.
  # -ResidualPCASubK                      4                             # (Phase2bMode=3) initial K for the residual VBGMM (per cluster). Dirichlet prior shrinks K toward the support of the data; this is the upper bound on how many sub-clusters can emerge from a single existing cluster in one iteration.
  # -ResidualPCADominantChannels          2                             # (DEPRECATED in mode 3) — formerly used by the per-iter dominant-channel xcorr; the post-loop realign now uses the shared XcorrDispatch::compute (all channels, normalised xcorr) for proper Klusters-style alignment. Flag retained for backwards compatibility; ignored in the current implementation.
  # -ResidualPCAConvTol                   0.01                          # (Phase2bMode=3) early-stop threshold: when the fraction of spikes whose label changed in this iteration is < ResidualPCAConvTol, the chunk's loop terminates. 0.01 = 1% of spikes changing.
  # -ResidualPCAMinScore                  0.7                           # (Phase2bMode=3) minimum normalised cross-correlation (in [-1,1]) required to accept a per-spike alignment shift in the post-loop XcorrDispatch realign pass. Below this, the spike's shift stays at zero (matches Klusters' realignSpikes gate). 0.7 is the conventional value; lower (0.5) admits noisier alignments; higher (0.85) restricts to high-confidence matches.
  # -AlignPcaCenter                       0                             # patch83/patch84: (Phase2bMode=3) PCA-centering alignment. 0 = off (default, classic xcorr alignment, m_cumShift updated). 1 = patch83 second-pass refine AFTER the xcorr iter loop, m_cumShift updated. 2 = patch84 REPLACEMENT mode: xcorr iter loop SKIPPED, replaced with in-memory circular-shift PCA-centering iter loop, m_cumShift NEVER touched (so TimeShiftFinalize writes no .spk/.fet/.res changes for any chunk run in mode 2). Mode 2 exists to avoid the temporal-dispersion failure mode that comes from cumulative-shift accumulation: highpass-filtered waveforms tolerate circular wrap at their edges (no DC content), the PCA features are recomputed from the shifted in-memory waveforms each iter, and the resulting clustering benefits from alignment while leaving on-disk waveforms intact. The user re-extracts .spk/.fet from .fil out-of-band after clustering if they want disk content to match.

  # ===== PHASE 4b - ALTERNATING SPLIT<->MERGE loop control =====
  -AlternatingSplitMergeEnable          1                               # KlustersRealignAfterPhase4 — when set, run a per-chunk klusters-faithful
  -AlternatingSplitMergeMaxIters        300                             # AlternatingSplitCooldownIters — split↔merge oscillation guard (patch
  -AlternatingSplitMergeAbortOnNetGrowth2
  -AlternatingSplitCooldownIters        2                               # Net-growth abort: if a Phase 4b iter produces more new clusters than
  -KlustersRealignAfterPhase4           1                               # Maximum number of Phase 4 iterations in which WaveKnnSplit is allowed

  # ===== PHASE 4b - SPLIT: WaveKnn (klusters-faithful kNN splitter) =====
  -KnnSplitPerChunkEnable               1
  -KnnSplitMode                         1                               # WaveKnnMajorityThreshold: fraction of the K neighbours that must share
  -KnnSplitK                            15
  -KnnSplitMinRefSize                   200
  -KnnSplitMinSourceSize                100
  -KnnSplitMinNewClusterSize            50                              # ---- Phase 4 rewrite (klusters-faithful KNN + adaptation modelling) ----
  -WaveKnnMaxSourcesPerCall             30                              # ---------------------------------------------------------------------------
  -WaveKnnMajorityThreshold             0.35                            # WaveKnnResidualBecomesCluster: when KnnSplitMode=1, controls what
  -WaveKnnResidualBecomesCluster        1
  -WaveKnnMinSourceAnisotropy           0.3                             # default mixture gate; see KlustaKwik.h.
  -WaveKnnMaskNeighbors                 0                               # WaveKnnMaxSourcesPerCall — cap on number of source clusters
  # -WaveKnnUseTraceFilter                0                             # WaveKnnSkipMuaCluster1: when KnnSplitMode=1, controls whether cluster 1
  # -WaveKnnSkipMuaCluster1               1                             # WaveKnnNoiseSourceProbability: probability that the noise cluster
  # -WaveKnnNoiseSourceProbability        0.0                           # WaveKnnMaskNeighbors — when 1 (default), wave_knn_split processes

  # ===== PHASE 4b - SPLIT: FullCem (full-CEM splitter) =====
  -FullCemSplitEnable                   1
  -FullCemSplitMaxSourcesPerCall        5                               # 0 = unlimited
  -FullCemSplitMinClusterSize           20                              # 0 = use max(nFullDims+5, 25)
  -FullCemSplitAdaptiveFeatures         1
  -FullCemSplitFeatureBimodalThreshold  0.10                            # valley depth gate
  -FullCemSplitMinFeatures              2                               # floor for CEM
  -FullCemSplitMaxFeatures              9                               # 0 = SubspaceDims/nSpatial
  -FullCemSplitReprobePasses            1                               # FullCemSplitRefractoryGate (patch 0057) — when 1, a FullCem split is
  -FullCemSplitRefractoryGate           1
  -FullCemSplitRefractoryGateMinSep     0.5                             # ── Phase 4c neighborhood-remix split (patch 0062) ──────────────────────────

  # ===== PHASE 4b - SPLIT dispatch: quality-weighted routing =====
  -QualityWeightedSplitEnable           1
  -QualityWeightedSplitN                15                              # 0 = derive from per-call caps
  -QualityWeightedSplitPoolFactor       2                               # pool = factor * N
  -QualityWeightedISIRefractoryMs       2.0                             # refractory window for ISI contam

  # ===== PHASE 4 - within-chunk MERGE: template matching =====
  -TemplateMatchScore                   0.98                            # min xcorr for within-chunk template matching (Phase 5)
  -TemplateMatchIters                   300                             # max within-chunk template match iterations
  # -TemplateMatchEigRatio                0.0                           # Phase 5 merge veto threshold: union-top-eig / max(per-cluster-top-eig). 0 disables (xcorr only).
  -TemplateMatchTaperHannSamples        8                               # MergeRealignEnable — when 1, after every chunk's merge commit in
  -TemplateMatchBatchedXcorr            1                               # 1 = batch all-pairs xcorr per reference (patch 0056)
  -MedianKnnTemplateMatchEnable         1
  -MedianKnnTemplateMatchK              15                              # Phase4RefineEnable: master switch for the new Phase-4 rewrite. When
  -MergeRealignEnable                   1
  -MergeRealignIncremental              1                               # 1 = realign only newly-absorbed spikes (patch 0054)
  # -Phase4MinClusterSize                 50                            # XcorrResidualThresh: minimum residualScore for a within-chunk merge
  # -XcorrResidualThresh                  0.85                          # XcorrMaxShiftSamples: half-width of the integer-lag search in samples
  # -XcorrMaxShiftSamples                 0                             # DipSplit parameters (Phase 8 bimodal splitter)
  # -Phase4RefineEnable                   0                             # Phase4RefineIters: number of refine-loop passes when Phase4RefineEnable=1.
  # -Phase4RefineIters                    10                            # AdaptModelEnable: fit per-cluster ISI-conditional adaptation model
  # -AdaptModelEnable                     1                             # AdaptTauGridCSV: comma-separated grid of τ values in seconds for the
  # -AdaptTauGridCSV                      ""

  # ===== PHASE 4c - neighbourhood-remix split (optional) =====
  -Phase4cRemixEnable                   0                               # master switch (default off)
  -Phase4cMaxIters                      2                               # remix split→merge passes
  -Phase4cKnnSources                    3                               # random sources routed to knn-split / iter
  -Phase4cFullCemSources                3                               # random sources routed to FullCEM / iter
  -Phase4cNeighbors                     2                               # N closest clusters pooled into each source
  -Phase4cMinClusterSize                0                               # 0 = max(nFullDims+5, 25)
  -Phase4cMaskTightClusters             1                               # exclude tight clusters from remix
  -Phase4cTightnessThreshold            0.02                            # rho = Vres/Psig below this = masked
  -Phase4cSignalChannelFraction         0.1                             # tau: signal channel if Ech >= tau*maxEch
  -Phase4cTightnessSpreadBeta           0                               # ── Phase 8 variance-targeted knn-split (patch 0067) ──────────────────────

  # ===== PHASE 5 - CROSS-CHUNK MODEL MATCH (stitch chunks into global units) =====
  -CrossChunkMaxChunkDistance           5                               # Phase 5 Pass 2 candidate gate — see KlustaKwik.h.
  -CrossChunkTemplateScore              0.97                            # min xcorr for cross-chunk template matching (Phase 2 Pass 3)
  -CrossChunkDriftSigma                 2.2                             # Phase 6 Pass 2 smoothness penalty width. Multiplies xcorr score by exp(-(dev/sigma)²/2) where dev = ||actual_displacement - expected|| / scatter, expected = mean displacement of Pass 1 confirmed matches between same chunk pair. 0 disables.
  # -CrossChunkVoteMinFraction            0.0
  # -CrossChunkVoteMinMargin              0.0

  # ===== PHASE 5b - cross-chunk affine drift transform =====
  -CrossChunkTransformDriftEnable       1                               # master switch (default off)
  -CrossChunkTransformIRLSIters         10                              # IRLS-Huber iterations
  -CrossChunkTransformMinMatches        0                               # 0 = max(D+2, 3)
  -CrossChunkTransformHuberK            1.345
  -CrossChunkTransformChainSmoothLambda 1.0                             # 0 = no chain smoothing // rho_thresh_eff = thresh * nSig^beta (0 = off)

  # ===== PHASE 6 - GLOBAL WARM-START EM + eigen-residual merge (testing) =====
  #   GlobalMergeIter 0 skips Phase 7.
  -GlobalMergeIter                      500                             # Phase 6 warm-start EM iterations; 0 skips Phase 7 entirely
  -Phase6EigenResidualEnable            1
  -Phase6EigenResidualThresh            0.45                            # admit a pair if eigen-residual <= this (sigma units)
  -Phase6EigenResidualK                 3                               # nuisance eigenmodes allowed bounded movement
  -Phase6EigenResidualC                 3                               # movement bound along each nuisance mode, in sigma
  -Phase6EigenResidualFloorFrac         0.05                            # eigenvalue shrinkage floor as fraction of top eigenvalue

  # ===== PHASE 6b - mean-subtraction merge (optional) =====
  -MeanSubtractionMergeEnable           0                               # 0 = off; 1 = run Phase 6b after Phase 7a
  -MeanSubtractionMergeThresh           0.1                             # normalised residual D below which a pair merges.
  -MeanSubtractionMergeMaxShift         5                               # Phase 6b cyclic-shift search half-width.

  # ===== PHASE 7c - KLUSTERS-FAITHFUL per-spike realignment =====
  #   (-KlustersRealignUseMedian from v7 is not a real param; use CenterMode/SelectMinVariance.)
  -KlustersRealignEnable                1                               # 0 = off; 1 = run Phase 7c after Phase 7b.
  -KlustersRealignMaxShift              8                               # Search radius in samples. Matches klusters
  -KlustersRealignMinSize               10                              # Skip clusters with fewer spikes (mean too noisy
  -KlustersRealignIters                 5                               # patch 0060: realign passes; >1 iterates the mean-rebuild+xcorr to convergence (klusters-faithful), breaking early when a pass moves no spike. 1 = original single-pass.
  -KlustersRealignSelectMinVariance     1                               # patch 0061: when 1 (and KlustersRealignIters>1), restore each cluster to the iteration with its LEAST residual waveform variance instead of the converged/last pass. Per-cluster.
  # -KlustersRealignCenterMode            1                             # Post-alignment centering: 0=off, 1=PCA (default; centering left to the PCA alignment phases), 2=RMS circular group-recenter (matches klusters --recenter-rms).
  # -KlustersRealignRMin                  0.40                          # RMS recenter: minimum mean-resultant-length R to trust the circular centroid; below this the cluster keeps its per-spike alignment.

  # ===== PHASE 8 - GLOBAL post-processing splits =====
  -Phase8VarianceSplitEnable            1                               # master switch (default off)
  -Phase8VarianceSplitMaxIters          10                              # iterations
  -Phase8VarianceThreshold              0.10                            # ρ = V_res/P_sig; ≥ this is eligible
  -Phase8VarianceSignalChannelFraction  0.1                             # τ for signal support
  -Phase8VarianceMinClusterSize         10                              # 0 = auto = max(nFullDims+5, 25)
  -DipSplitGlobalEnable                 0                               # Phase 8 global DipSplit (post-Phase-7). Set to 0 in chunked mode with drift; per-chunk Phase 1b DipSplit is unaffected.

  # ===== PHASE 9 - GLOBAL CCG REFRACTORY-DIP MERGE =====
  -Phase9CCGMergeEnable                 1
  -Phase9CCGRefractoryMs                2                               # central (refractory) half-window, ms
  -Phase9CCGFlankMs                     20                              # flank/baseline half-window, ms (> Refractory)
  -Phase9CCGDepletionRatio              0.30                            # merge if centerRate/flankRate <= this
  -Phase9CCGGateCosine                  0.95                            # only test pairs with spatial-mean Pearson >= this
  -Phase9CCGMinSpikes                   150                             # both clusters need >= this many spikes
  -Phase9CCGMinFlankPairs               75                              # need >= this many flank pairs (statistical support)

  # ===== OUTPUT / LOGGING / DEBUG / RNG =====
  -Screen                               1
  # -Log                                  ""
  -Verbose                              1
  # -Debug                                ""
  # -SaveIntermediates                    0                             # 0 = final-write only; 1 = also write per-phase .clu
  # -fSaveModel                           0                             # 1 = write .model.N (debug only)
  # -DistDump                             0
  -RandomSeed                           42

  # ===== REFINE-EXISTING mode (only when re-refining an existing .clu) =====
  # -StartCluFile                         ""
  # -RefineExisting                       ""
  # -RefineMode                           full                          # off | reassign | split | merge | full (refine-existing only)
  # -RefineIters                          5                             # CEM iterations during reassign
  # -RefineMergeThresh                    0.0                           # 0 = inherit from MergeThresh (auto-χ²)
  # -RefineSplitMinDepth                  0.4                           # DipSplit valley depth threshold
  # -RefineLockNoiseClu                   1                             # never modify cluster ids 0 and 1
)

"${KKEXP}" "${DATASET}" "${GROUP}" "${ARGS[@]}"
