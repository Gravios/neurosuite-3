#!/usr/bin/env bash
# =============================================================================
# run_kkexp_v6_per_channel_alternating.sh
# =============================================================================
# v6 cumulative patch state (incremental from v5):
#
#   Patch 0040  Hann taper xcorr + post-merge realign  (NEW)
#   Patch 0041  -WaveKnnMaxSourcesPerCall              (NEW)
#   Patch 0042  -FullCemSplitEnable etc                (NEW)
#
# Bash syntax NOTE: every line ending must be `\` immediately followed by
# newline.  `\ ` (backslash + space) escapes the space and terminates the
# command -- silent breakage where everything after that line is parsed as
# new commands.  Validate with `bash -n run_kkexp_v6_per_channel_alternating.sh`
# before any production run.
# =============================================================================

set -euo pipefail

KKEXP=${KKEXP:-KiloKlustaKwik}
DATASET=${1:?usage: $0 <session-base> <group>}
GROUP=${2:?usage: $0 <session-base> <group>}

"${KKEXP}" "${DATASET}" "${GROUP}" \
  \
  `# -- core sweep + chunking ----------------------------------------- ` \
  -MaxClusters                       200    \
  -MaxPossibleClusters               10000  \
  -nRuns                             1      \
  -ChunkMinutes                      10     \
  -ChunkOverlapMinutes               5      \
  -ChunkPreseedFraction              0.05   \
  -PreseedCacheFile                  ${DATASET}.preseed.${GROUP}.cache \
  \
  -Screen                            1      \
  \
  `# -- Phase 2a.5 per-chunk DipSplit (patch 0008) -------------------- ` \
  -DipSplitEnable                    0      \
  -DipSplitMinSize                   25     \
  -SubspaceRecluster                 1      \
  -SubspaceDims                      3      \
  -DipSplitBeforePhase2b             1      \
  \
  `# -- Phase 2a.6 k-NN hull splitter (patch 0009) -------------------- ` \
  -HullSplitEnable                   0      \
  -HullSplitK                        10     \
  -HullSplitMinComponentSize         75     \
  -HullSplitMutualReachScale         1.4    \
  -HullSplitUseMutualReach           1      \
  \
  `# -- Phase 2a.7 per-channel amp+phase split (patch 0015) OFF ----- ` \
  -PerChannelSplitEnable             0      \
  -PerChannelSplitValleyThreshold    0.50   \
  -PerChannelSplitMinSubClusterSize  25     \
  -PerChannelSplitMinChannelSnrRatio 0.5    \
  \
  `# -- Phase 2a.8 median-subtracted subdim split (patch 0033) OFF -- ` \
  -MedianSubdimSplitEnable           0      \
  -MedianSubdimSplitMinClusterSize   50     \
  -MedianSubdimSplitTopK             3      \
  -MedianSubdimSplitValleyThreshold  0.40   \
  -MedianSubdimSplitMinSubClusterSize 25    \
  -MedianSubdimSplitBicMarginConstant 0.0   \
  -MedianSubdimSplitBicMarginPerLogN  0.0   \
  \
  `# -- KnnSplit (klusters-faithful) -- required by Phase 4b -------- ` \
  -KnnSplitPerChunkEnable            1      \
  -KnnSplitMode                      1      \
  -KnnSplitK                         25     \
  -KnnSplitMinRefSize                200    \
  -KnnSplitMinSourceSize             20     \
  -KnnSplitMinNewClusterSize         10     \
  -WaveKnnMaxSourcesPerCall          5      \
  -WaveKnnMajorityThreshold          0.3    \
  -WaveKnnResidualBecomesCluster     1      \
  -WaveKnnMinSourceAnisotropy        0.3    \
  -WaveKnnMaskNeighbors              0      \
  \
  `# -- Phase 4b FullCemSplit (patch 0042) -- NEW -------------------- ` \
  -FullCemSplitEnable                0      \
  -FullCemSplitMaxSourcesPerCall     0      \
  -FullCemSplitMinClusterSize        0      \
  \
  `# -- Phase 2b perf fixes (patch 0006/0007) ------------------------ ` \
  -Phase2bEnableSplits               0      \
  -Phase2bMaxIter                    0      \
  \
  `# -- Long-standing consolidators ---------------------------------- ` \
  -CrossChunkMaxChunkDistance        1      \
  -TemplateMatchScore                0.99   \
  -TemplateMatchIters                49     \
  -CrossChunkTemplateScore           0.98   \
  -CrossChunkDriftSigma              2.0    \
  -GlobalMergeIter                   0      \
  -AdaptiveMerge                     0      \
  -MergeThresh                       52     \
  -TimeMergeIter                     40     \
  \
  `# -- Phase 4 merge selector (patch 0035/0036) -- MEDIAN k-NN -------- ` \
  -MedianKnnTemplateMatchEnable      1      \
  -MedianKnnTemplateMatchK           7      \
  \
  `# -- Phase 4 Hann taper + post-merge realign (patch 0040) -- NEW -- ` \
  -TemplateMatchTaperHannSamples     8      \
  -MergeRealignEnable                1      \
  \
  `# -- Phase 4b alternating knn-split / template-match (patch 0016) - ` \
  -AlternatingSplitMergeEnable           0  \
  -AlternatingSplitMergeMaxIters        200 \
  -AlternatingSplitMergeAbortOnNetGrowth 1  \
  \
  `# -- Phase 6b mean-subtraction merge (off) ------------------------- ` \
  -MeanSubtractionMergeEnable        0      \
  -MeanSubtractionMergeThresh        0.1    \
  -MeanSubtractionMergeMaxShift      5      \
  \
  `# -- Phase 7c klusters-faithful realignment (patch 0013, 0032) --- ` \
  -KlustersRealignEnable             1      \
  -KlustersRealignMaxShift           8      \
  -KlustersRealignMinSize            10     \
  -KlustersRealignUseMedian          1      \
  \
  `# -- Phase 4 in-loop realignment (patch 0031) -- heavy + cached - ` \
  -KlustersRealignAfterPhase4        0      \
  \
  `# -- Legacy TimeShift: FULLY OFF (deferred 'random shifts' bug) --- ` \
  -TimeShiftAlignIter                0      \
  -TimeShiftSplitEnable              0      \
  -TimeShiftMergeEnable              0      \
  -TimeShiftAlignAfterPhase1         0      \
  -TimeShiftAlignAfterPhase1b        0      \
  -TimeShiftAlignAfterPhase2         0      \
  -TimeShiftAlignAfterPhase4         0      \
  -TimeShiftAlignAfterPhase5         0      \
  -TimeShiftAlignPostMerge           0      \
  -EnergyCOMRealign                  0      \
  \
  -PenaltyMix                        0.0    \
  -DipSplitGlobalEnable              0      \
  -RandomSeed                        42     \
  -Verbose                           0
