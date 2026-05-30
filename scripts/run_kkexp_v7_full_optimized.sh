#!/usr/bin/env bash
# ============================================================================
# run_kkexp_v7_full_optimized.sh
#
# kiloklustakwik run config incorporating patches 0037-0056.  Enables the
# full Phase 4b quality-weighted split/merge pipeline with the new
# parallelization, caching, oscillation guard, and batched-xcorr options.
#
# Usage:  ./run_kkexp_v7_full_optimized.sh <FileBase> <ElecNo>
#   e.g.  ./run_kkexp_v7_full_optimized.sh sirotaA-jg-000005-20120312 6
#
# KKEXP env var overrides the binary path (default: ./KiloKlustaKwik).
# Validate before running:  bash -n run_kkexp_v7_full_optimized.sh
# Count flags:  KKEXP=echo bash run_kkexp_v7_full_optimized.sh DUMMY 6 \
#                 | tr ' ' '\n' | grep -c '^-'
# ============================================================================
set -euo pipefail

FILEBASE="${1:?usage: $0 <FileBase> <ElecNo>}"
ELECNO="${2:?usage: $0 <FileBase> <ElecNo>}"
KKEXP="${KKEXP:-./KiloKlustaKwik}"

# NOTE: every line continued with a single trailing backslash, NO trailing
# whitespace after it (a space after '\' silently ends the command — the bug
# that broke v6).  Keep it that way.
"$KKEXP" "$FILEBASE" "$ELECNO" \
    -UseDistributional        1 \
    -SamplingRate             20000 \
    \
    `# ── Phase 4b: alternating split<->merge (MUST be on) ──` \
    -AlternatingSplitMergeEnable        1 \
    -AlternatingSplitMergeMaxIters      2 \
    -AlternatingSplitMergeAbortOnNetGrowth 1 \
    \
    `# ── Oscillation guard (patch 0053) ──` \
    -AlternatingSplitCooldownIters      3 \
    \
    `# ── Quality-weighted dispatch (patch 0046) ──` \
    -QualityWeightedSplitEnable         1 \
    -QualityWeightedSplitN              4 \
    -QualityWeightedSplitPoolFactor     2 \
    -QualityWeightedISIRefractoryMs     2.0 \
    \
    `# ── FullCem splitter + adaptive features (0042/0047/0048) ──` \
    -FullCemSplitEnable                 1 \
    -FullCemSplitAdaptiveFeatures       1 \
    -FullCemSplitFeatureBimodalThreshold 0.10 \
    -FullCemSplitMinFeatures            2 \
    -FullCemSplitMaxFeatures            0 \
    -FullCemSplitMinClusterSize         0 \
    `# Recursive reprobe (patch 0055): re-select features per sub-cluster.` \
    -FullCemSplitReprobePasses          1 \
    \
    `# ── WaveKnn splitter (klusters-faithful) ──` \
    -KnnSplitPerChunkEnable             1 \
    -KnnSplitMode                       1 \
    -WaveKnnMajorityThreshold           0.5 \
    -WaveKnnMaxSourcesPerCall           5 \
    \
    `# ── Median-kNN template merge + taper + realign (0040/0051/0052/0054) ──` \
    -MedianKnnTemplateMatchEnable       1 \
    -MedianKnnTemplateMatchK            7 \
    -TemplateMatchScore                 0.92 \
    -TemplateMatchIters                 10 \
    -TemplateMatchTaperHannSamples      8 \
    -MergeRealignEnable                 1 \
    -MergeRealignIncremental            1 \
    \
    `# ── Batched all-pairs xcorr (patch 0056; helps the all-pairs path,` \
    `#    harmless with MedianKnn on) ──` \
    -TemplateMatchBatchedXcorr          1 \
    \
    -Verbose                            1
