#!/usr/bin/env bash
# =============================================================================
# run_kkexp_v5_per_channel_alternating.sh
# =============================================================================
# Production v5: builds on v4 (patches 0001-0014) by adding
#   * Phase 2a.7  per-channel amplitude+phase split   (patch 0015)
#   * Phase 4b    alternating knn-split / template-match  (patch 0016)
#
# Targets the three failure modes from the user's klusters screenshots:
#
#   Cluster #131 (cyan, 812 spikes):
#       Two real neurons separable by a SMALL combination of amplitude
#       AND phase on a single channel.  DipSplit misses (signal spreads
#       across multiple PCs).  HullSplit misses (clusters are connected
#       in PCA feature space, just elongated).  KnnSplit misses
#       (templates too similar).  Caught by Phase 2a.7 per-channel
#       4-feature × angle-sweep valley test on each channel.
#
#   Cluster #117 (orange, 451 spikes):
#       2-3 unit multimodal mixture.  Caught by aggressive KnnSplit
#       params + Phase 4b alternation (template-match alone cannot
#       merge mixture templates, but knn-split + re-merge alternation
#       resolves them).
#
#   Cluster #102 (magenta, 875 spikes, MUA):
#       Multi-unit mixture of many sub-units.  Per-chunk has only
#       ~24 spikes/chunk on average so per-chunk KnnSplit alone is
#       insufficient.  Phase 4b alternation runs knn-split AFTER
#       each Phase 4 merge — repeated splits gradually surface
#       sub-units that template-match couldn't separate.
#
# Required prior patches (apply in order before this script):
#   0001-0014  v4 baseline
#   0015       Phase 2a.7 per-channel split + module + tests
#   0016       Phase 4b alternating knn-split / template-match
#
# All paths assume the user's nphy-069 layout:
#   /datum/working_data/sirotaA-jg-000005-20120312/  for input
#   /datum/staging_kkexp_v5/group6/                  for output
# =============================================================================

set -euo pipefail

# ── Inputs ──────────────────────────────────────────────────────────────────
SESSION_DIR=/datum/working_data/sirotaA-jg-000005-20120312
SESSION_BASE=sirotaA-jg-000005-20120312
GROUP=6
SAMPLING_RATE=20000

# ── Output staging ──────────────────────────────────────────────────────────
OUT_DIR=/datum/staging_kkexp_v5/group${GROUP}
mkdir -p "${OUT_DIR}"
cp -av "${SESSION_DIR}/${SESSION_BASE}".{fet,fil,res,spk,clu}.${GROUP} "${OUT_DIR}/" \
    2>/dev/null || true
# .xml is required for nChan/nSamplesPerSpike discovery.
cp -av "${SESSION_DIR}/${SESSION_BASE}.xml" "${OUT_DIR}/" 2>/dev/null || true

cd "${OUT_DIR}"

# ── Binary ──────────────────────────────────────────────────────────────────
KKEXP_BIN=/home/gravio/neurosuite-3/build/kiloklustakwik/KiloKlustaKwik
test -x "${KKEXP_BIN}" || { echo "ERROR: KiloKlustaKwik binary not found at ${KKEXP_BIN}"; exit 1; }

# ── Run kiloklustakwik v5 ────────────────────────────────────────────────────
LOG="${OUT_DIR}/kkexp_v5_group${GROUP}.log"
echo "Logging to ${LOG}"

"${KKEXP_BIN}" "${SESSION_BASE}" "${GROUP}" \
    \
    `# === Core CEM / chunked-EM (unchanged from v4) ============================` \
    -UseDistributional 1 \
    -MaxPossibleClusters 10000 \
    -MaxIter 500 \
    -DistDump 0 \
    -PriorPoint 1 \
    -MinClusters 2 \
    -MaxClusters 200 \
    -StartCluFile 0 \
    -SplitFirst 20 \
    -SplitEvery 50 \
    -DropLastNFeatures 1 \
    -ChunkMinutes 0.0 \
    \
    `# === Phase 5 cross-chunk merge (raised to handle aggressive sub-clusters) =` \
    -CrossChunkTemplateScore 0.94 \
    -GlobalMergeIter 500 \
    \
    `# === Phase 2a.5 DipSplit (PCA-based, before Phase 2b) =====================` \
    -DipSplitEnable 1 \
    -DipSplitBeforePhase2b 1 \
    -DipSplitMinSize 50 \
    -DipSplitBloatFactor 1.0 \
    -DipSplitElongationFactor 4.0 \
    -DipSplitValleyThresh 0.40 \
    -SubspaceDims 6 \
    \
    `# === Phase 2a.6 HullSplit (k-NN-graph connected components) ===============` \
    -HullSplitEnable 1 \
    -HullSplitK 10 \
    -HullSplitMinComponentSize 50 \
    -HullSplitMutualReachScale 1.5 \
    -HullSplitUseMutualReach 1 \
    \
    `# === Phase 2a.7 NEW: per-channel amplitude+phase split ====================` \
    `# Targets cluster #131 case.  Reads full waveforms; tests each channel's` \
    `# (peak amp, peak time, trough amp, trough time) via 1D + 2D angle sweep` \
    `# for KDE valleys.  SNR gate filters low-signal channels.  BIC gate is` \
    `# weak on continuous data (see header) but downstream Phase 4/5/6b` \
    `# merges re-consolidate any over-splits.` \
    -PerChannelSplitEnable 1 \
    -PerChannelSplitMinClusterSize 50 \
    -PerChannelSplitValleyThreshold 0.50 \
    -PerChannelSplitMinSubClusterSize 25 \
    -PerChannelSplitBicMarginConstant 12.0 \
    -PerChannelSplitBicMarginPerLogN 6.0 \
    -PerChannelSplitMinChannelSnrRatio 0.5 \
    -PerChannelSplitUsePeakAmp 1 \
    -PerChannelSplitUsePeakTime 1 \
    -PerChannelSplitUseTroughAmp 1 \
    -PerChannelSplitUseTroughTime 1 \
    \
    `# === Phase 2b ChunkReCEM (unchanged from v4) ==============================` \
    -ChunkReCEMTrySplits 0 \
    \
    `# === Phase 2b.5 KnnSplit (klusters-faithful, aggressive) ==================` \
    `# Lowered MinNewClusterSize from 10 -> 5 to catch the 5-58 spike global` \
    `# sub-clusters the user found by manual klusters knn-split.  Per-chunk` \
    `# minimum lowered from 20 -> 10 to qualify more sources.  Majority` \
    `# threshold 0.5 (was 0.6) widens the accepted-split surface.  These` \
    `# parameters are the entry knob; Phase 4b alternation will iterate on top.` \
    -KnnSplitPerChunkEnable 1 \
    -KnnSplitMode 1 \
    -KnnSplitK 10 \
    -WaveKnnMajorityThreshold 0.5 \
    -KnnSplitMinRefSize 30 \
    -KnnSplitMinSourceSize 20 \
    -KnnSplitMinNewClusterSize 5 \
    -WaveKnnResidualBecomesCluster 1 \
    \
    `# === Phase 4 within-chunk template matching ===============================` \
    -TemplateMatchScore 0.94 \
    -TemplateMatchIters 10 \
    \
    `# === Phase 4b NEW: alternating knn-split / template-match =================` \
    `# Targets clusters #117 / #102 (mixture cases that pure template-match` \
    `# cannot resolve because the mixture template IS the cluster mean).` \
    `# After each Phase 4 template-match iter, runs WaveKnnSplit again to` \
    `# re-fragment any surviving mixtures; the next iter re-merges any` \
    `# over-fragments.  Converges when neither merge nor split changes` \
    `# labels in one iteration.  Cap = TemplateMatchIters (10).` \
    -AlternatingSplitMergeEnable 1 \
    \
    `# === Phase 6b mean-subtraction merge (unchanged from v4) ==================` \
    -MeanSubtractionMergeEnable 1 \
    -MeanSubtractionMergeThresh 0.10 \
    -MeanSubtractionMergeMaxShift 3 \
    \
    `# === Phase 7c klusters realignment (unchanged from v4) ====================` \
    -KlustersRealignEnable 1 \
    -KlustersRealignMaxShift 8 \
    -KlustersRealignMinSize 10 \
    \
    `# === Legacy TimeShift OFF (Phase 7c is the replacement) ===================` \
    -TimeShiftAlignIter 0 \
    \
    `# === Logging ==============================================================` \
    -Screen 1 \
    -Log 1 \
    -Verbose 1 \
    2>&1 | tee "${LOG}"

# ── Post-run diagnostic checks ──────────────────────────────────────────────
echo "" | tee -a "${LOG}"
echo "=============================================================" | tee -a "${LOG}"
echo "Post-run checks:" | tee -a "${LOG}"
echo "=============================================================" | tee -a "${LOG}"

echo "" | tee -a "${LOG}"
echo "[1] Phase 2a.7 PerChannelSplit hits:" | tee -a "${LOG}"
grep -E "\[Phase 2a\.7\] PerChannelSplit per-chunk:" "${LOG}" || echo "  (no Phase 2a.7 lines — module disabled or no splits)"  | tee -a "${LOG}"

echo "" | tee -a "${LOG}"
echo "[2] Phase 4b AlternatingKnnSplit hits:" | tee -a "${LOG}"
grep -E "\[Phase 4b\] AlternatingKnnSplit" "${LOG}" || echo "  (no Phase 4b lines — disabled or did not fire)" | tee -a "${LOG}"

echo "" | tee -a "${LOG}"
echo "[3] Per-phase CLUSTER STATE evolution (last 30 lines):" | tee -a "${LOG}"
grep -E "CLUSTER STATE:" "${LOG}" | tail -30 | tee -a "${LOG}"

echo "" | tee -a "${LOG}"
echo "[4] Phase 7c klusters realignment summary:" | tee -a "${LOG}"
grep -E "\[Phase 7c\]" "${LOG}" || echo "  (no Phase 7c lines)" | tee -a "${LOG}"

echo "" | tee -a "${LOG}"
echo "[5] Final cluster count (.clu.${GROUP}):" | tee -a "${LOG}"
if test -f "${SESSION_BASE}.clu.${GROUP}"; then
    K=$(head -1 "${SESSION_BASE}.clu.${GROUP}")
    NSPIKES=$(($(wc -l < "${SESSION_BASE}.clu.${GROUP}") - 1))
    NOISE=$(awk 'NR>1 && $1==0' "${SESSION_BASE}.clu.${GROUP}" | wc -l)
    echo "  K=${K}  N=${NSPIKES}  noise=${NOISE} ($(echo "scale=1; ${NOISE}*100.0/${NSPIKES}" | bc)%)" | tee -a "${LOG}"
fi

echo "" | tee -a "${LOG}"
echo "[6] MaxPossibleClusters overflow warnings (must be empty for valid run):" | tee -a "${LOG}"
grep -E "\[Phase 5\] WARNING: MergeChunkModels produced" "${LOG}" || echo "  (none — good)" | tee -a "${LOG}"

echo "" | tee -a "${LOG}"
echo "Done.  Open in klusters:" | tee -a "${LOG}"
echo "  klusters ${OUT_DIR}/${SESSION_BASE}.xml" | tee -a "${LOG}"
