/***************************************************************************
                   KK_phase2b_mode3.cpp
                   --------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Phase 2b mode 3 of the chunked pipeline: KK::RunPhase2bMode3Chunk, the
 per-chunk residual-PCA refinement with dominant-channel xcorr realignment.
 Moved verbatim out of KK_chunked.cpp; it reaches the shared numerical
 kernels through KK_internal.h.  Member definitions of the KK class declared
 in KK.h.
 ***************************************************************************/
#include "KK.h"
#include "KK_internal.h"
#include "KlustaKwik.h"
#include <neurosuite/core/dipsplit.hpp>
#include "proxy_isi.h"
#include "clust_quality.h"
#include "realign_xcorr.h"
#include "realign_center.h"
#include "klusters_realign.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <unordered_map>

#include <unordered_set>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <omp.h>



// ---------------------------------------------------------------------------
// RunPhase2bMode3Chunk — Phase 2b mode 3 per-chunk driver.
//
// Iterative residual-PCA refinement + dominant-channel xcorr realignment
// for one chunk's spikes.  See the long comment at the Phase2bMode == 3
// dispatch (in the Phase 2b chunk loop) for the full algorithm rationale.
//
// Inputs:
//   Ks                — the per-chunk sub-KK (warm-started from Phase 2a)
//   pts               — global spike indices in this chunk [nPts]
//   nChan             — channel count for this group
//   nSamplesPerSpike  — sample count per spike window
//   nStart            — initial cluster count (incl. noise=cluster 0)
//
// Side effects:
//   * Modifies Ks.Class[], Ks.ClassAlive[]: refined per-chunk labels.
//   * Modifies this->m_cumShift[] for indices in `pts`: per-spike
//     realignment shifts (additive, capped at ±m_timeShiftMaxAbs).
//
// Reads .spk via TimeShiftReadSpikeWave (mmap path is thread-safe; the
// FILE* fallback is serialised here with a local critical section).
//
// Convergence: stops early when the fraction of label-changing spikes in
// an iteration falls below ResidualPCAConvTol, or after ResidualPCAIter
// outer iterations.
// ---------------------------------------------------------------------------
void KK::RunPhase2bMode3Chunk(KK& Ks, const std::vector<int>& pts,
                              int nChan, int nSamplesPerSpike,
                              int nStart, int chunkIdx)
{
    const int nPts = static_cast<int>(pts.size());
    if (nPts < 5 || nChan <= 0 || nSamplesPerSpike <= 0) return;
    if (!m_timeShiftReady) {
        // mmap/FILE not set up — fall back to mode 0 behaviour silently.
        Ks.MStep(); Ks.EStep();
        Ks.RunEMLoop(true, false, 0, "[2b-m3-fallback]");
        return;
    }

    const int waveSamples = nChan * nSamplesPerSpike;
    const int nSubDims    = Ks.nDims;          // VBGMM feature width
    const int K_init      = nStart;            // initial K (noise + real)

    // Sanitise hyperparameters from CLI globals
    const int maxIter      = std::max(1, ResidualPCAIter);
    const int nResidComp   = std::clamp(ResidualPCAComponents, 1, 8);
    const int nDomCh       = std::clamp(ResidualPCADominantChannels, 1,
                                        std::max(1, nChan));
    const int subK         = std::clamp(ResidualPCASubK, 2, 8);
    const double convTol   = std::max(0.0,
                                     static_cast<double>(ResidualPCAConvTol));

    // Entry banner.  Each per-chunk worker emits one of these — when the
    // chunk loop is parallel the lines interleave across threads, which is
    // why every line is self-contained with its chunk index.
    Output("[Stage 2.8 m3] chunk %d START: %d spikes, %d sub-dims, K_init=%d, "
           "maxIter=%d, K_resid=%d, K_sub=%d, domCh=%d, convTol=%.4f, "
           "maxShift=%d\n",
           chunkIdx, nPts, nSubDims, K_init, maxIter, nResidComp, subK,
           nDomCh, convTol, m_timeShiftMaxAbs);

    // ── Read all chunk spikes into a local buffer ───────────────────────
    // Bulk-read upfront so the inner loop has random access without per-spike
    // file ops.  Memory cost: nPts × waveSamples × 2 bytes — for a typical
    // chunk of 50k spikes × 336 samples × 2B = ~32 MiB per chunk worker.
    std::vector<int16_t> waveBuf(static_cast<size_t>(nPts) * waveSamples);
    {
        int16_t scratch[2];  // unused; TimeShiftReadSpikeWave writes directly
        (void)scratch;
        bool needCrit = (m_timeShiftSpkMap == nullptr);  // FILE* path → serial
        if (needCrit) {
            #pragma omp critical (KK_TimeShiftReadSpikeWave)
            {
                for (int i = 0; i < nPts; i++) {
                    int16_t* dst = waveBuf.data() +
                                   static_cast<size_t>(i) * waveSamples;
                    if (!TimeShiftReadSpikeWave(pts[i], waveSamples, dst))
                        std::fill(dst, dst + waveSamples, 0);
                }
            }
        } else {
            // mmap is reentrant — issue reads in parallel-safe loop
            for (int i = 0; i < nPts; i++) {
                int16_t* dst = waveBuf.data() +
                               static_cast<size_t>(i) * waveSamples;
                if (!TimeShiftReadSpikeWave(pts[i], waveSamples, dst))
                    std::fill(dst, dst + waveSamples, 0);
            }
        }
    }

    // ── Iteration buffers ───────────────────────────────────────────────
    std::vector<int>    prevLabels(nPts), labels(nPts);
    std::vector<double> meanWave(static_cast<size_t>(waveSamples), 0.0);
    std::vector<double> residual;                  // re-sized per cluster
    std::vector<double> covR(static_cast<size_t>(waveSamples) *
                              waveSamples, 0.0);
    std::vector<double> eigvecs;                   // [K * waveSamples]
    std::vector<double> eigvals;                   // [K]
    std::vector<float>  subFeat;                   // [N_c * nResidComp]
    std::vector<int>    subLabels;
    std::vector<int>    members;
    std::vector<int>    domChans;

    // Seed labels from the warm-started Ks.Class[].
    for (int i = 0; i < nPts; i++) labels[i] = Ks.Class[i];

    int totalSplitsApplied = 0;
    int totalRealigned     = 0;
    int actualItersRun     = 0;

    for (int iter = 0; iter < maxIter; iter++) {
        prevLabels = labels;
        ++actualItersRun;

        // Snapshot alive-cluster count before this iter's VBGMM
        int aliveBefore = 0;
        {
            std::vector<int> seen; seen.reserve(64);
            for (int v : labels) {
                if (v <= 0) continue;
                bool found = false;
                for (int s : seen) if (s == v) { found = true; break; }
                if (!found) seen.push_back(v);
            }
            aliveBefore = static_cast<int>(seen.size());
        }

        // ── Step 1: VBGMM on chunk features with current label warm-start ─
        // First iter: warm-start from Phase 2a (already in labels).  Later
        // iters: warm-start from the previous iter's split-and-realign
        // labels (re-featurized via Data[] which doesn't change inside this
        // chunk loop, but the labels carry forward the structure we found).
        int vbgmmSurvivors = 0;
        {
            std::vector<int> lbuf = labels;
            // Cluster-id space may have expanded by splits in previous
            // iter; bound K_init by the labels actually present.
            int kCur = K_init;
            for (int v : lbuf) if (v + 1 > kCur) kCur = v + 1;
            vbgmmSurvivors = RunVBGMM(
                Ks.Data.m_Data, lbuf.data(), kCur, nPts, nSubDims,
                VBGMMMaxIter, static_cast<double>(VBGMMConvTol));
            labels = lbuf;
        }

        // Per-iter counters
        int splitsThisIter      = 0;  // # clusters that got split
        int newSubClustersIter  = 0;  // # new IDs created from splits
        int clustersExamined    = 0;
        int clustersTooSmall    = 0;
        int realignThisIter     = 0;

        // ── Step 2/3: per-cluster residual-PCA + split via residual VBGMM ─
        // Find max alive cluster id to know where to assign new sub-ids.
        int nextId = 0;
        for (int v : labels) if (v + 1 > nextId) nextId = v + 1;

        // Collect ids of currently-alive non-noise clusters (snapshot
        // BEFORE we start splitting — we don't recurse into freshly-made
        // sub-clusters in this same iter).
        std::vector<int> activeIds;
        {
            std::vector<int> seen;
            seen.reserve(64);
            for (int v : labels) {
                if (v <= 0) continue;
                bool found = false;
                for (int s : seen) if (s == v) { found = true; break; }
                if (!found) seen.push_back(v);
            }
            activeIds = std::move(seen);
        }

        for (int cid : activeIds) {
            ++clustersExamined;
            // Gather members of cluster cid
            members.clear();
            for (int i = 0; i < nPts; i++)
                if (labels[i] == cid) members.push_back(i);
            const int Nc = static_cast<int>(members.size());
            // Need enough spikes for stable mean + residual PCA: a soft
            // floor of 2·waveSamples keeps the per-cluster covariance
            // non-degenerate.  Below that, attempting to split is more
            // likely to overfit noise than discover real structure.
            if (Nc < std::max(20, 2 * nResidComp)) { ++clustersTooSmall; continue; }

            // Mean waveform of the cluster
            std::fill(meanWave.begin(), meanWave.end(), 0.0);
            for (int m : members) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(m) * waveSamples;
                for (int j = 0; j < waveSamples; j++)
                    meanWave[j] += static_cast<double>(w[j]);
            }
            const double invN = 1.0 / Nc;
            for (int j = 0; j < waveSamples; j++) meanWave[j] *= invN;

            // Build residuals: residual[i, j] = waveBuf[member_i, j] − mean[j]
            residual.assign(static_cast<size_t>(Nc) * waveSamples, 0.0);
            for (int i = 0; i < Nc; i++) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(members[i]) * waveSamples;
                double* rrow = residual.data() +
                               static_cast<size_t>(i) * waveSamples;
                for (int j = 0; j < waveSamples; j++)
                    rrow[j] = static_cast<double>(w[j]) - meanWave[j];
            }

            // Residual covariance Σ = (1/(N-1)) Rᵀ R  in [waveSamples × waveSamples]
            std::fill(covR.begin(), covR.end(), 0.0);
            const double cscale = 1.0 / std::max(1, Nc - 1);
            for (int i = 0; i < Nc; i++) {
                const double* rrow = residual.data() +
                                     static_cast<size_t>(i) * waveSamples;
                for (int a = 0; a < waveSamples; a++) {
                    const double ra = rrow[a];
                    double* crow = covR.data() +
                                   static_cast<size_t>(a) * waveSamples;
                    for (int b = 0; b < waveSamples; b++)
                        crow[b] += ra * rrow[b];
                }
            }
            for (size_t k = 0; k < covR.size(); k++) covR[k] *= cscale;

            // Top-K eigenvectors via power iteration with deflation
            TopKEigenPowerDeflation(covR, waveSamples, nResidComp,
                                    /*maxIter=*/50, /*relTol=*/1e-4,
                                    eigvecs, eigvals);

            // Project residuals onto the top eigenvectors → sub-features
            // subFeat[i * nResidComp + k] = ⟨residual[i], eigvecs[k]⟩
            subFeat.assign(static_cast<size_t>(Nc) * nResidComp, 0.0f);
            for (int i = 0; i < Nc; i++) {
                const double* rrow = residual.data() +
                                     static_cast<size_t>(i) * waveSamples;
                for (int k = 0; k < nResidComp; k++) {
                    const double* ev = eigvecs.data() +
                                       static_cast<size_t>(k) * waveSamples;
                    double s = 0.0;
                    for (int j = 0; j < waveSamples; j++) s += rrow[j] * ev[j];
                    subFeat[static_cast<size_t>(i) * nResidComp + k] =
                        static_cast<float>(s);
                }
            }

            // VBGMM on sub-features: initialise everyone in cluster 0 of
            // the sub-space, K_init = subK (Dirichlet prior shrinks K).
            subLabels.assign(static_cast<size_t>(Nc), 0);
            // Spread initial labels round-robin so VBGMM has K seeds to
            // grow from rather than collapsing immediately.
            for (int i = 0; i < Nc; i++)
                subLabels[static_cast<size_t>(i)] = i % subK;
            const int nSurv = RunVBGMM(subFeat.data(), subLabels.data(),
                                       subK, Nc, nResidComp,
                                       VBGMMMaxIter,
                                       static_cast<double>(VBGMMConvTol));

            if (nSurv <= 1) continue;  // no split discovered → keep cluster

            // Find the majority sub-cluster id (gets to keep cid); the rest
            // become new clusters numbered nextId, nextId+1, ...
            std::vector<int> subCounts(subK, 0);
            for (int v : subLabels) if (v >= 0 && v < subK) subCounts[v]++;
            int majority = 0, majCount = subCounts[0];
            for (int k = 1; k < subK; k++)
                if (subCounts[k] > majCount) { majCount = subCounts[k]; majority = k; }

            std::vector<int> subRemap(subK, -1);
            subRemap[majority] = cid;
            int newIdsAddedHere = 0;
            for (int k = 0; k < subK; k++) {
                if (k == majority) continue;
                if (subCounts[k] == 0)  continue;
                if (nextId >= MaxPossibleClusters) {
                    // Out of ID space — collapse remaining sub-clusters
                    // back to the original cid.  No structural loss; just
                    // skip the split for this iter.
                    subRemap[k] = cid;
                } else {
                    subRemap[k] = nextId++;
                    ++newIdsAddedHere;
                }
            }
            // Apply remap to chunk-wide labels[]
            for (int i = 0; i < Nc; i++) {
                const int sk  = subLabels[static_cast<size_t>(i)];
                const int newCid = (sk >= 0 && sk < subK) ? subRemap[sk] : cid;
                labels[members[i]] = newCid;
            }
            if (newIdsAddedHere > 0) {
                ++splitsThisIter;
                newSubClustersIter += newIdsAddedHere;
                ++totalSplitsApplied;
            }
        }

        // (Step 4 — per-spike xcorr realign — moved OUT of the iter loop.
        // Reason: TimeShiftReadSpikeWave reads the original .spk verbatim
        // (no .fil re-extraction mid-chunk for parallel-safety), so the
        // cluster mean computed in iter N+1 is identical to iter N's.
        // Per-iter xcorr would therefore return the same lag every iter
        // and either double-shift or cap-clamp.  One pass at the end on
        // the final cluster labels is both correct and matches Klusters'
        // single-shot realignSpikes behaviour.)
        (void)realignThisIter;  // intentionally always 0 inside the loop

        // ── Step 5: convergence check + per-iter banner ──────────────────
        int nChanged = 0;
        for (int i = 0; i < nPts; i++)
            if (labels[i] != prevLabels[i]) ++nChanged;
        const double frac = static_cast<double>(nChanged) / nPts;
        const bool   converged = (frac < convTol);

        Output("[Stage 2.8 m3] chunk %d iter %d/%d: alive %d -> VBGMM survivors %d, "
               "examined %d (%d too small), %d splits -> %d new sub-clusters, "
               "%d spike realigns, %d/%d (%.2f%%) labels changed%s\n",
               chunkIdx, iter + 1, maxIter,
               aliveBefore, vbgmmSurvivors,
               clustersExamined, clustersTooSmall,
               splitsThisIter, newSubClustersIter,
               realignThisIter,
               nChanged, nPts, 100.0 * frac,
               converged ? " [CONVERGED]" : "");

        if (converged) break;
    }

    // ──── Final per-cluster alignment (post-split-iterations) ────────────
    //
    // patch71 — Iterative on-the-spot xcorr alignment.
    //
    // Replaces the prior single-pass-with-cap-rejection design with an
    // iterative one that re-extracts each spike's waveform from .fil at
    // its updated position IMMEDIATELY after each xcorr step, and
    // re-projects features into Data[] in the same place.  Three things
    // changed from the original:
    //
    //   (a) DROP CAP-REJECTION on the cumulative shift.  The previous
    //       version refused any shift whose accumulated magnitude would
    //       exceed m_timeShiftMaxAbs (default 3), leaving spikes that
    //       needed larger total motion STUCK at their original
    //       mis-detected positions.  Visible result: ±5-sample
    //       within-cluster temporal dispersion after KiloKlustaKwik
    //       completes.  Per-iter lag is naturally bounded by xcorr's
    //       maxShift parameter; cumulative motion across iters is
    //       unbounded by design — a spike needing 5 samples can get
    //       +3 in iter 0, then +2 in iter 1, then 0 in iter 2.  The
    //       only safety gate is the extraction-window bounds check
    //       (off must fit within sessionSamples).
    //
    //   (b) RE-EXTRACT AND RE-FEATURIZE ON THE SPOT.  After updating
    //       m_cumShift[gp], read the new waveform from .fil at
    //       (rawTs + cumShift - PeakSampleIndex), apply the stderiv
    //       transform if the pipeline is .pcaD/.spkD, overwrite
    //       waveBuf[i_local] so the NEXT iter's xcorr sees the
    //       re-extracted waveform, and project through the canonical
    //       PCA basis to update Data[gp].  This is what makes "merge
    //       toward the centre" converge: subsequent iters see the
    //       already-shifted waveforms and the cluster mean re-built
    //       from them is well-centred on PeakSampleIndex.
    //
    //   (c) USE INT64 rawTs from .res, NOT float-recovery from
    //       Data[timeDim].  RefeaturizeFromShifts at lines 2818-2822
    //       explicitly warns that recovering rawTs by inverse-
    //       normalising the float feature introduces up to ±13 samples
    //       of error for late-session spikes.  For a 1-hour 32 kHz
    //       session that error is ±~7 samples on the last spike —
    //       larger than the per-iter maxShift, so any code relying on
    //       float recovery never converges.  We pre-load int64 rawTs
    //       for every chunk-spike from .res ONCE at function entry and
    //       index by local i_local.
    //
    // "Merge toward the centre": the existing patch64 template pre-
    // alignment forces every cluster's mean template peak to land on
    // PeakSampleIndex (the waveform-window centre).  When two clusters
    // would merge (similar waveforms), both pre-aligned templates land
    // on the same centre; xcorr pulls both clusters' spikes there.
    // Clusters therefore merge AT the centre rather than at one
    // cluster's drifted peak.
    //
    // Disk writes (.spk / .fet / .res .pending) remain deferred to the
    // existing TimeShiftFinalize → RefeaturizeFromShifts path.  This
    // function only updates in-memory state (waveBuf, Data[], m_cumShift)
    // because the parallel chunk loop would race on the global disk
    // files otherwise.  TimeShiftFinalize is single-threaded and reads
    // the final m_cumShift values to do the actual disk write.
    int finalAligned     = 0;
    int finalLowScore    = 0;
    int finalOutOfBounds = 0;
    int alignItersRun    = 0;
    if (AlignPcaCenter == 2) {
        // ─── patch84: in-memory circular-shift PCA-centering, no m_cumShift ───
        //
        // REPLACES the xcorr iter loop + patch83 refine + patch74 monotonicity
        // entirely.  Designed to avoid the temporal-dispersion failure mode
        // that surfaces when cumulative shifts (m_cumShift) accumulate across
        // phases and survive into TimeShiftFinalize's disk writes.
        //
        // Algorithm:
        //   for iter = 0 .. alignMaxIter:
        //       for each alive cluster cid:
        //           μ_pca[f] = mean over cluster of Data[gp * nDims + f]
        //                      for f in 0 .. (basisNChan*basisNComp - 1)
        //           for each spike (member i) of cluster:
        //               for each candidate shift s in [-maxShift, +maxShift]:
        //                   scratch = circular_shift(waveBuf[i], s)
        //                   project scratch through canonical PCA basis with
        //                       dimMin_/dimRange_ normalisation
        //                   dist²(s) = sum_f (pca_f - μ_pca[f])²
        //               bestS = argmin_s dist²(s)
        //               if bestS != 0:
        //                   waveBuf[i] = circular_shift(waveBuf[i], bestS)
        //                   Data[gp]   = recomputed PCA features of new wave
        //       if no spike moved this iter: break
        //
        // What it touches:
        //   waveBuf[]      → circularly shifted in-place per spike
        //   Data[gp][f<D]  → PCA columns refreshed from shifted waveform
        //                    (NOT the time column at index nDims-1 —
        //                    circular shift doesn't change detection time)
        //
        // What it deliberately does NOT touch:
        //   m_cumShift[]   → left exactly as found.  If prior phases set
        //                    nonzero values, those are preserved; if zero,
        //                    they stay zero.  This is the design point:
        //                    TimeShiftFinalize keys off m_cumShift to write
        //                    .spk/.fet/.res, and we want it to write nothing
        //                    (for chunks running mode 2 fresh) or only what
        //                    earlier phases already settled.
        //
        // What's deliberately SKIPPED relative to modes 0/1:
        //   - raw-source verify (no .fil reads in mode 2)
        //   - patch73 pre-refresh of waveBuf from .fil (waveBuf stays in
        //     its as-loaded .spk state; circular shifts compose from there)
        //   - patch74 monotonicity check (m_cumShift unchanged so .res
        //     order is preserved by construction)
        //
        // Why circular shift is safe here: the .spk content is highpass-
        // filtered (or stderiv-transformed, which is doubly high-pass-like),
        // so there's effectively no DC and the wrap discontinuity at the
        // window edge is small.  More importantly, the PCA basis windows
        // around basisRecShift .. basisRecShift + basisData2use — typically
        // centered on the peak with margin — so a few samples of wrap at
        // the window EDGE don't enter the projection at small shift sizes.
        //
        // What the user does afterward: if they want the on-disk .spkD /
        // .fetD to reflect the alignment chosen in mode 2, they re-extract
        // out-of-band from .fil using their own tooling.  KKExp neither
        // assumes nor performs that step.
        const bool hasBasisP84 = m_timeShiftBasis.valid() &&
                                 !m_timeShiftBasis.meanShifted.empty();
        if (!hasBasisP84) {
            Output("[Stage 2.8 m3] chunk %d patch84 mode 2: "
                   "PCA basis not loaded — alignment skipped entirely.\n",
                   chunkIdx);
        } else {
            const int   candCanonicalP84 = m_timeShiftBasis.N;
            const int   basisNCompP84    = m_timeShiftBasis.nComp;
            const int   basisData2useP84 = m_timeShiftBasis.data2use;
            const int   basisRecShiftP84 = m_timeShiftBasis.recShift;
            const int   basisNChanP84    = m_timeShiftBasis.nChan;
            const bool  basisCenteredP84 = m_timeShiftBasis.isCentered;
            const int   nPCAFeatsP84     = basisNChanP84 * basisNCompP84;
            const int   maxShiftP84      = m_timeShiftMaxAbs;
            const int   alignMaxIterP84  = std::clamp(ResidualPCAIter, 2, 10);
            const int   nSampP84         = nSamplesPerSpike;

            const auto& meanCanonP84 =
                m_timeShiftBasis.meanShifted[
                    static_cast<size_t>(candCanonicalP84)];
            const auto& evecCanonP84 =
                m_timeShiftBasis.eigvecShifted[
                    static_cast<size_t>(candCanonicalP84)];

            std::vector<int16_t> scratchWaveP84(
                static_cast<size_t>(waveSamples));

            int p84TotalShifts = 0;
            int p84ItersRun    = 0;

            // The candidate-shift sweep and the post-commit refeaturize
            // both project a sample-major waveform through the canonical
            // PCA basis using dimMin_/dimRange_ normalisation.  Both are
            // inlined below (rather than factored into a lambda) because
            // (a) the hot path is the inner-most loop, function-call
            // overhead matters, and (b) the sweep version also needs to
            // accumulate distance² against muPca, while the refeaturize
            // version writes into Data[gp] — fusing those into a single
            // lambda would force conditional branching inside the loop.

            for (int iterP84 = 0; iterP84 < alignMaxIterP84; ++iterP84) {
                ++p84ItersRun;
                int shiftsThisIter = 0;

                // Recollect alive cluster ids from CURRENT labels (clusters
                // may have died/split between iters of the outer phase).
                std::vector<int> idsP84;
                idsP84.reserve(64);
                for (int v : labels) {
                    if (v <= 0) continue;
                    bool found = false;
                    for (int s : idsP84)
                        if (s == v) { found = true; break; }
                    if (!found) idsP84.push_back(v);
                }

                for (int cid : idsP84) {
                    std::vector<int> members;
                    for (int i = 0; i < nPts; ++i)
                        if (labels[i] == cid) members.push_back(i);
                    const int Nc = static_cast<int>(members.size());
                    if (Nc < 3) continue;

                    // μ_pca over cluster from current Data[]
                    std::vector<double> muPca(
                        static_cast<size_t>(nPCAFeatsP84), 0.0);
                    int contributors = 0;
                    for (int m : members) {
                        const int gp = pts[static_cast<size_t>(m)];
                        if (gp < 0) continue;
                        const float* row = Data.m_Data + gp * nDims;
                        for (int f = 0; f < nPCAFeatsP84; ++f)
                            muPca[static_cast<size_t>(f)] +=
                                static_cast<double>(row[f]);
                        ++contributors;
                    }
                    if (contributors < 3) continue;
                    const double invN = 1.0 / contributors;
                    for (int f = 0; f < nPCAFeatsP84; ++f)
                        muPca[static_cast<size_t>(f)] *= invN;

                    for (int sp = 0; sp < Nc; ++sp) {
                        const int iLocal = members[static_cast<size_t>(sp)];
                        const int gp     = pts[static_cast<size_t>(iLocal)];
                        if (gp < 0) continue;

                        int16_t* wave = waveBuf.data()
                            + static_cast<ptrdiff_t>(iLocal)
                            * static_cast<ptrdiff_t>(waveSamples);

                        int    bestS    = 0;
                        double bestDist = std::numeric_limits<double>::infinity();

                        for (int s = -maxShiftP84; s <= maxShiftP84; ++s) {
                            // Circular shift wave by s into scratchWaveP84.
                            // Sample-major: scratch[((t + s) mod N) * nChan + c]
                            //               = wave[t * nChan + c]
                            for (int t = 0; t < nSampP84; ++t) {
                                int tNew = (t + s) % nSampP84;
                                if (tNew < 0) tNew += nSampP84;
                                const int rowDst = tNew * nChan;
                                const int rowSrc = t    * nChan;
                                for (int c = 0; c < nChan; ++c)
                                    scratchWaveP84[
                                        static_cast<size_t>(rowDst + c)] =
                                            wave[rowSrc + c];
                            }

                            // PCA project scratchWaveP84 and accumulate
                            // distance² to muPca, inline (lambda-free for
                            // speed — this is the inner-most loop).
                            double dist2 = 0.0;
                            for (int ch = 0; ch < basisNChanP84; ++ch) {
                                const auto& mu = meanCanonP84[
                                    static_cast<size_t>(ch)];
                                const auto& ev = evecCanonP84[
                                    static_cast<size_t>(ch)];
                                for (int k = 0; k < basisNCompP84; ++k) {
                                    double val = 0.0;
                                    for (int u = 0; u < basisData2useP84; ++u) {
                                        const int sIdx = basisRecShiftP84 + u;
                                        double x = static_cast<double>(
                                            scratchWaveP84[
                                                static_cast<size_t>(
                                                    sIdx * nChan + ch)]);
                                        if (basisCenteredP84)
                                            x -= mu[static_cast<size_t>(u)];
                                        val += ev[static_cast<size_t>(
                                            k * basisData2useP84 + u)] * x;
                                    }
                                    const int featIdx = ch * basisNCompP84 + k;
                                    if (featIdx >= nPCAFeatsP84 ||
                                        featIdx >= static_cast<int>(
                                            dimMin_.size()))
                                        continue;
                                    const double normVal =
                                        (static_cast<float>(val)
                                       - dimMin_[featIdx])
                                      * dimRange_[featIdx];
                                    const double diff = normVal
                                      - muPca[static_cast<size_t>(featIdx)];
                                    dist2 += diff * diff;
                                }
                            }

                            if (dist2 < bestDist) {
                                bestDist = dist2;
                                bestS    = s;
                            }
                        }

                        if (bestS == 0) continue;  // already at the centre

                        // Commit the chosen circular shift to waveBuf in
                        // place (via scratch since source and dest overlap).
                        for (int t = 0; t < nSampP84; ++t) {
                            int tNew = (t + bestS) % nSampP84;
                            if (tNew < 0) tNew += nSampP84;
                            const int rowDst = tNew * nChan;
                            const int rowSrc = t    * nChan;
                            for (int c = 0; c < nChan; ++c)
                                scratchWaveP84[
                                    static_cast<size_t>(rowDst + c)] =
                                        wave[rowSrc + c];
                        }
                        std::memcpy(wave, scratchWaveP84.data(),
                                    static_cast<size_t>(waveSamples)
                                  * sizeof(int16_t));

                        // Refresh Data[gp]'s PCA features (only — the
                        // timestamp at index nDims-1 stays put because
                        // circular shift does not change detection time).
                        float* dataRow = Data.m_Data + gp * nDims;
                        for (int ch = 0; ch < basisNChanP84; ++ch) {
                            const auto& mu = meanCanonP84[
                                static_cast<size_t>(ch)];
                            const auto& ev = evecCanonP84[
                                static_cast<size_t>(ch)];
                            for (int k = 0; k < basisNCompP84; ++k) {
                                double val = 0.0;
                                for (int u = 0; u < basisData2useP84; ++u) {
                                    const int sIdx = basisRecShiftP84 + u;
                                    double x = static_cast<double>(
                                        wave[sIdx * nChan + ch]);
                                    if (basisCenteredP84)
                                        x -= mu[static_cast<size_t>(u)];
                                    val += ev[static_cast<size_t>(
                                        k * basisData2useP84 + u)] * x;
                                }
                                const int featIdx = ch * basisNCompP84 + k;
                                if (featIdx >= nPCAFeatsP84 ||
                                    featIdx >= static_cast<int>(
                                        dimMin_.size()))
                                    continue;
                                dataRow[featIdx] =
                                    (static_cast<float>(val)
                                   - dimMin_[featIdx])
                                  * dimRange_[featIdx];
                            }
                        }

                        ++shiftsThisIter;
                        ++p84TotalShifts;
                    }  // end per-spike loop
                }      // end per-cluster loop

                if (shiftsThisIter == 0) break;
            }          // end iter loop

            alignItersRun = p84ItersRun;
            finalAligned  = p84TotalShifts;

            Output("[Stage 2.8 m3] chunk %d patch84 PCA-centering (circular): "
                   "%d total shifts over %d iter(s); "
                   "m_cumShift NOT modified (mode 2)\n",
                   chunkIdx, p84TotalShifts, p84ItersRun);
        }
    } else
    {
        // Re-collect alive non-noise cluster ids from final labels
        std::vector<int> finalIds;
        finalIds.reserve(64);
        for (int v : labels) {
            if (v <= 0) continue;
            bool found = false;
            for (int s : finalIds) if (s == v) { found = true; break; }
            if (!found) finalIds.push_back(v);
        }

        // Scratch buffers — sized for the largest cluster's batch call
        std::vector<int16_t> tmpl(static_cast<size_t>(waveSamples), 0);
        std::vector<int16_t> xcWfm;
        std::vector<int>     xcShifts;
        std::vector<float>   xcScores;
        const float minScore = ResidualPCAMinScore;
        const int   maxShift = m_timeShiftMaxAbs;

        // ── Re-extract / re-featurize setup ──────────────────────────────
        char filPathP71[STRLEN + 16] = {0};
        pickInputPath(filPathP71, sizeof(filPathP71), FileBase, "fil", ElecNo);
        FILE* filFpP71 = fopen(filPathP71, "rb");

        const float   sessionSamplesF   = timeRawMax - timeRawMin;
        const int64_t sessionSamplesP71 = static_cast<int64_t>(
            std::max(0.0f, sessionSamplesF));
        const int     timeDimIdxP71     = nDims - 1;

        const bool hasBasis = m_timeShiftBasis.valid() &&
                              !m_timeShiftBasis.meanShifted.empty();
        const int  candCanonical = hasBasis ? m_timeShiftBasis.N : 0;
        const int  basisNComp    = hasBasis ? m_timeShiftBasis.nComp : 0;
        const int  basisData2use = hasBasis ? m_timeShiftBasis.data2use : 0;
        const int  basisRecShift = hasBasis ? m_timeShiftBasis.recShift : 0;
        const int  basisNChan    = hasBasis ? m_timeShiftBasis.nChan : nChan;
        const bool basisCentered = hasBasis ? m_timeShiftBasis.isCentered : false;
        const bool isStderiv     = hasBasis ? m_timeShiftBasis.isStderiv : false;
        const int  nPCAFeatsP71  = hasBasis ? basisNChan * basisNComp : 0;

        // Pre-load int64 rawTs for this chunk's pts from .res.  Indexed
        // by local i_local.  Avoids the ±~7 sample float-precision error
        // (see paragraph (c) above).
        std::vector<int64_t> rawTsByLocal(static_cast<size_t>(nPts), 0);
        {
            char resPathP71[STRLEN + 16] = {0};
            resolveAnyC(resPathP71, sizeof(resPathP71), FileBase, "res", ElecNo);
            FILE* resFpP71 = fopen(resPathP71, "rb");
            if (resFpP71) {
                for (int i = 0; i < nPts; ++i) {
                    const int gp = pts[static_cast<size_t>(i)];
                    if (gp < 0) continue;
                    if (fseeko(resFpP71,
                               static_cast<off_t>(gp)
                                 * static_cast<off_t>(sizeof(int64_t)),
                               SEEK_SET) != 0) continue;
                    int64_t ts = 0;
                    if (fread(&ts, sizeof(int64_t), 1, resFpP71) == 1
                            && ts > 0) {
                        rawTsByLocal[static_cast<size_t>(i)] = ts;
                    }
                }
                fclose(resFpP71);
            }
        }

        // Iteration bound: collapses to 1 if we can't refresh waveBuf
        // (no .fil or no PCA basis), where iterating is pointless.
        const int alignMaxIter = (filFpP71 && hasBasis)
            ? std::clamp(ResidualPCAIter, 2, 10)
            : 1;

        // Scratch row buffers reused across iterations
        std::vector<int16_t> filRowP71(static_cast<size_t>(NbTotalChannels));
        std::vector<int16_t> waveScratchP71(static_cast<size_t>(waveSamples));

        // patch72 — verify that .fil content matches .spk before any
        // alignment work.
        //
        // Ports the verifyRawSource check from Klusters' patch69
        // (klustersdoc.cpp:4046-4188) into KKExp.  Without it, a stale
        // .fil (e.g. recompiled with different filter settings, or
        // overwritten by a later pipeline stage) would silently corrupt
        // .spk on every shifted spike during the patch71 on-the-spot
        // re-extract and again during TimeShiftFinalize's
        // RefeaturizeFromShifts re-projection.
        //
        // Algorithm: pick the first chunk-spike whose extraction window
        // fits inside the .fil, read its waveform at the spike's
        // ORIGINAL timestamp (rawTsByLocal, no m_cumShift applied yet),
        // apply the stderiv transform if .spk is in stderiv format,
        // compare to that spike's existing waveBuf slot (loaded from
        // .spk at function entry).  RMS difference exceeding 50% of the
        // .spk content's RMS indicates the two raw sources do not
        // match — abort this chunk's alignment to prevent further
        // corruption.
        //
        // Skipped when .fil is unavailable or no spike's window fits
        // inside the recording (extremely unusual; would only happen
        // if every chunk-spike sits at a file boundary).
        bool rawSourceOk = true;
        if (filFpP71) {
            bool verifyDone = false;
            for (int iV = 0; iV < nPts && !verifyDone; ++iV) {
                const int64_t tsV = rawTsByLocal[static_cast<size_t>(iV)];
                if (tsV <= 0) continue;
                const int64_t startV = tsV - PeakSampleIndex;
                if (startV < 0 ||
                    startV + nSamplesPerSpike > sessionSamplesP71 + 1) continue;
                if (fseeko(filFpP71,
                           startV * static_cast<off_t>(NbTotalChannels)
                                  * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) continue;

                bool readVOk = true;
                for (int s = 0; s < nSamplesPerSpike && readVOk; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels),
                              filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        readVOk = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!readVOk) continue;

                if (isStderiv)
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);

                // Compare to waveBuf[iV * waveSamples ..] which holds the
                // .spk contents in the same sample-major layout.
                const int16_t* refSpk = waveBuf.data()
                    + static_cast<ptrdiff_t>(iV)
                    * static_cast<ptrdiff_t>(waveSamples);
                double sumDiff2 = 0.0, sumRef2 = 0.0;
                for (int e = 0; e < waveSamples; ++e) {
                    const double dv =
                        static_cast<double>(waveScratchP71[
                            static_cast<size_t>(e)])
                      - static_cast<double>(refSpk[static_cast<size_t>(e)]);
                    sumDiff2 += dv * dv;
                    sumRef2  += static_cast<double>(refSpk[
                        static_cast<size_t>(e)])
                              * static_cast<double>(refSpk[
                        static_cast<size_t>(e)]);
                }
                const double rmsRatio = (sumRef2 > 0.0)
                    ? std::sqrt(sumDiff2 / sumRef2)
                    : 0.0;

                if (rmsRatio > 0.5) {
                    Output("[Stage 2.8 m3] chunk %d patch72: RAW SOURCE "
                           "MISMATCH — .fil content does not match .spk\n"
                           "  verify spike local idx %d (gp=%d, ts=%lld)\n"
                           "  RMS difference: %.1f%% of .spk content "
                           "(threshold 50%%)\n"
                           "  Likely cause: .spk was extracted from a "
                           "different raw source than the .fil currently "
                           "on disk (e.g. .fil overwritten by a later "
                           "pipeline stage, or filter settings changed).\n"
                           "  Aborting alignment for this chunk to "
                           "prevent further .spk corruption.  Re-extract "
                           "waveforms with process_extractspikes before "
                           "re-running.\n",
                           chunkIdx, iV,
                           pts[static_cast<size_t>(iV)],
                           static_cast<long long>(tsV),
                           rmsRatio * 100.0);
                    rawSourceOk = false;
                } else {
                    Output("[Stage 2.8 m3] chunk %d patch72: raw source "
                           "verified (verify spike %d, RMS diff %.1f%%)\n",
                           chunkIdx, iV, rmsRatio * 100.0);
                }
                verifyDone = true;
            }
            if (!verifyDone) {
                Output("[Stage 2.8 m3] chunk %d patch72: WARNING — could "
                       "not verify raw source (no spike's window fit "
                       "within .fil); proceeding without verification\n",
                       chunkIdx);
            }
        }

        // Skip the iteration loop entirely if raw-source verification
        // tripped.  m_cumShift values from prior phases stay untouched;
        // TimeShiftFinalize will still process them, but at least we
        // don't compound the damage by shifting from a wrong source.
        if (rawSourceOk) {

        // patch73 — refresh waveBuf for spikes with non-zero m_cumShift.
        //
        // At function entry, waveBuf was loaded via TimeShiftReadSpikeWave
        // which reads .spk verbatim (no m_cumShift applied — see the
        // comment at line 9118 about parallel-safety).  If Phase 1
        // (cross-chunk alignment) or Phase 2.5 already populated
        // m_cumShift with non-zero values, those shifts are NOT reflected
        // in waveBuf at entry.  Iter 0's cluster mean is then built from
        // un-shifted .spk content, producing a slightly-off template,
        // and the iter-0 xcorr lag is computed against a not-quite-right
        // mean.
        //
        // Spikes whose iter-0 lag is zero (already match the off-mean)
        // never trigger the on-the-spot .fil re-extract in the iter
        // loop, so their waveBuf stays in the .spk state and the
        // prior-phase shifts remain "invisible" through all iters of
        // this function.  Final disk output is still correct (finalize
        // re-extracts everyone), but per-iter mean quality suffers and
        // alignment converges to a slightly-suboptimal m_cumShift.
        //
        // Fix: at function entry into the alignment block, for every
        // chunk-spike with prior m_cumShift != 0, re-extract its
        // waveform from .fil at (rawTs + prevCum - PeakSampleIndex) and
        // overwrite waveBuf[iLocal] so iter 0's mean is built from
        // already-shifted content.  Sample-major layout matches the
        // .spk load.  Spikes that fail the bounds or read check stay
        // in their .spk state — same fallback as the iter-loop body.
        //
        // Cost: one .fil read per pre-shifted spike at function entry,
        // independent of the iter loop.  For sessions where most chunks
        // have m_cumShift == 0 (the common case before Phase 1 starts
        // operating), this is a no-op.
        if (filFpP71 && hasBasis) {
            int prerefreshed = 0;
            for (int i = 0; i < nPts; ++i) {
                const int gp = pts[static_cast<size_t>(i)];
                if (gp < 0 || gp >= static_cast<int>(m_cumShift.size()))
                    continue;
                const int prevCum = m_cumShift[static_cast<size_t>(gp)];
                if (prevCum == 0) continue;
                const int64_t tsP = rawTsByLocal[static_cast<size_t>(i)];
                if (tsP <= 0) continue;
                const int64_t offP = tsP + prevCum - PeakSampleIndex;
                if (offP < 0 ||
                    offP + nSamplesPerSpike > sessionSamplesP71 + 1)
                    continue;
                if (fseeko(filFpP71,
                           offP * static_cast<off_t>(NbTotalChannels)
                                * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) continue;
                bool okR = true;
                for (int s = 0; s < nSamplesPerSpike && okR; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels),
                              filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        okR = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!okR) continue;
                if (isStderiv)
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);
                int16_t* dstBuf = waveBuf.data()
                    + static_cast<ptrdiff_t>(i)
                    * static_cast<ptrdiff_t>(waveSamples);
                std::copy(waveScratchP71.begin(),
                          waveScratchP71.end(), dstBuf);
                ++prerefreshed;
            }
            if (prerefreshed > 0) {
                Output("[Stage 2.8 m3] chunk %d patch73: pre-refreshed "
                       "waveBuf for %d spikes with non-zero prior "
                       "m_cumShift before iter loop\n",
                       chunkIdx, prerefreshed);
            }
        }

        for (int iterAlign = 0; iterAlign < alignMaxIter; ++iterAlign) {
            ++alignItersRun;
            int shiftsThisIter = 0;

        for (int cid : finalIds) {
            members.clear();
            for (int i = 0; i < nPts; i++)
                if (labels[i] == cid) members.push_back(i);
            const int Nc = static_cast<int>(members.size());
            if (Nc < 5) continue;

            // Build channel-major template = int16 mean.  Use int64
            // accumulator to avoid overflow on large clusters.
            std::vector<int64_t> accCh(static_cast<size_t>(waveSamples), 0);
            for (int m : members) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(m) * waveSamples;
                for (int s = 0; s < nSamplesPerSpike; s++)
                    for (int ch = 0; ch < nChan; ch++)
                        accCh[static_cast<size_t>(ch * nSamplesPerSpike + s)]
                            += w[s * nChan + ch];
            }
            for (int e = 0; e < waveSamples; e++)
                tmpl[static_cast<size_t>(e)] = static_cast<int16_t>(
                    accCh[static_cast<size_t>(e)] / Nc);

            // patch64 — pre-align the template to PeakSampleIndex (so
            // xcorr pulls every spike toward the window centre, and any
            // two clusters that would merge converge at the same point).
            if (PeakSampleIndex >= 0 && PeakSampleIndex < nSamplesPerSpike) {
                int    meanPeakSamp = PeakSampleIndex;
                double bestAmp      = -1.0;
                for (int s = 0; s < nSamplesPerSpike; ++s) {
                    double amp = 0.0;
                    for (int ch = 0; ch < nChan; ++ch)
                        amp += std::abs(static_cast<double>(
                            accCh[static_cast<size_t>(ch * nSamplesPerSpike + s)]));
                    if (amp > bestAmp) { bestAmp = amp; meanPeakSamp = s; }
                }
                const int tmplShift = PeakSampleIndex - meanPeakSamp;
                if (tmplShift != 0) {
                    std::vector<int16_t> shifted(static_cast<size_t>(waveSamples), 0);
                    for (int ch = 0; ch < nChan; ++ch) {
                        for (int s = 0; s < nSamplesPerSpike; ++s) {
                            const int src = s - tmplShift;
                            if (src < 0 || src >= nSamplesPerSpike) continue;
                            shifted[static_cast<size_t>(ch * nSamplesPerSpike + s)] =
                                tmpl[static_cast<size_t>(ch * nSamplesPerSpike + src)];
                        }
                    }
                    tmpl = std::move(shifted);
                }
            }

            // Repack cluster's waveforms into channel-major batch
            xcWfm.assign(static_cast<size_t>(Nc) * waveSamples, 0);
            for (int sp = 0; sp < Nc; sp++) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(members[sp]) * waveSamples;
                int16_t* dst = xcWfm.data() +
                               static_cast<size_t>(sp) * waveSamples;
                for (int s = 0; s < nSamplesPerSpike; s++)
                    for (int ch = 0; ch < nChan; ch++)
                        dst[ch * nSamplesPerSpike + s] = w[s * nChan + ch];
            }

            xcShifts.assign(static_cast<size_t>(Nc), 0);
            xcScores.assign(static_cast<size_t>(Nc), 0.0f);
            const int rc = XcorrDispatch::compute(
                xcWfm.data(), tmpl.data(),
                Nc, nChan, nSamplesPerSpike,
                maxShift, minScore,
                xcShifts.data(), xcScores.data());
            (void)rc;

            // Apply each non-zero lag on the spot.
            for (int sp = 0; sp < Nc; sp++) {
                const int lag = xcShifts[static_cast<size_t>(sp)];
                if (lag == 0) {
                    if (xcScores[static_cast<size_t>(sp)] < minScore)
                        ++finalLowScore;
                    continue;
                }
                const int iLocal = members[static_cast<size_t>(sp)];
                const int gp     = pts[static_cast<size_t>(iLocal)];
                if (gp < 0 || gp >= static_cast<int>(m_cumShift.size()))
                    continue;
                const int prevCum = m_cumShift[static_cast<size_t>(gp)];
                const int newCum  = prevCum + lag;

                // Fast path when re-extract is unavailable: just bump
                // m_cumShift and let TimeShiftFinalize handle the disk
                // catch-up.  No cap-reject regardless.
                if (!filFpP71 || !hasBasis) {
                    m_cumShift[static_cast<size_t>(gp)] = newCum;
                    ++finalAligned;
                    ++totalRealigned;
                    ++shiftsThisIter;
                    continue;
                }

                // Use precise int64 rawTs from .res (no float recovery).
                const int64_t rawTsInt = rawTsByLocal[static_cast<size_t>(iLocal)];
                if (rawTsInt <= 0) {
                    // No valid .res entry — skip rather than fall back
                    // to lossy float recovery.
                    ++finalOutOfBounds;
                    continue;
                }
                const int64_t off = rawTsInt + newCum - PeakSampleIndex;
                if (off < 0 ||
                    off + nSamplesPerSpike > sessionSamplesP71 + 1) {
                    // Window off the recording — skip without committing.
                    ++finalOutOfBounds;
                    continue;
                }

                // Tentative commit; reverted on .fil read failure.
                m_cumShift[static_cast<size_t>(gp)] = newCum;

                // Read sample-major waveform from .fil.
                bool readOk = true;
                if (fseeko(filFpP71,
                           off * static_cast<off_t>(NbTotalChannels)
                               * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) {
                    readOk = false;
                }
                for (int s = 0; s < nSamplesPerSpike && readOk; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels), filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        readOk = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!readOk) {
                    m_cumShift[static_cast<size_t>(gp)] = prevCum;
                    ++finalOutOfBounds;
                    continue;
                }

                if (isStderiv) {
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);
                }

                // Overwrite waveBuf so next iter's xcorr sees the
                // re-extracted waveform (sample-major, matching the
                // original TimeShiftReadSpikeWave load).
                int16_t* dstBuf = waveBuf.data() +
                                  static_cast<ptrdiff_t>(iLocal)
                                * static_cast<ptrdiff_t>(waveSamples);
                std::copy(waveScratchP71.begin(),
                          waveScratchP71.end(), dstBuf);

                // Re-project through the canonical (cand=N) PCA basis
                // and update Data[gp] features in place.  Eigenvector
                // indexing matches RefeaturizeFromShifts:
                //     ev[k * data2use + s]   (col-major)
                {
                    float* dataRow = Data.m_Data + gp * nDims;
                    const auto& meanCanon = m_timeShiftBasis.meanShifted[
                        static_cast<size_t>(candCanonical)];
                    const auto& evecCanon = m_timeShiftBasis.eigvecShifted[
                        static_cast<size_t>(candCanonical)];
                    for (int ch = 0; ch < basisNChan; ++ch) {
                        const auto& mu = meanCanon[static_cast<size_t>(ch)];
                        const auto& ev = evecCanon[static_cast<size_t>(ch)];
                        for (int k = 0; k < basisNComp; ++k) {
                            double val = 0.0;
                            for (int s = 0; s < basisData2use; ++s) {
                                const int sIdx = basisRecShift + s;
                                double xraw = static_cast<double>(
                                    waveScratchP71[static_cast<size_t>(
                                        sIdx * nChan + ch)]);
                                if (basisCentered)
                                    xraw -= mu[static_cast<size_t>(s)];
                                val += ev[static_cast<size_t>(
                                    k * basisData2use + s)] * xraw;
                            }
                            const int featIdx = ch * basisNComp + k;
                            if (featIdx < nPCAFeatsP71 &&
                                featIdx < static_cast<int>(dimMin_.size())) {
                                dataRow[featIdx] =
                                    (static_cast<float>(val) - dimMin_[featIdx])
                                    * dimRange_[featIdx];
                            }
                        }
                    }
                    if (sessionSamplesF > 0.0f) {
                        dataRow[timeDimIdxP71] =
                            (static_cast<float>(rawTsInt + newCum) - timeRawMin)
                            / sessionSamplesF;
                    }
                }

                ++finalAligned;
                ++totalRealigned;
                ++shiftsThisIter;
            }
        }   // end cluster loop

            // Converged: nobody moved this iter (every remaining shift
            // is below minScore or out-of-bounds).  Stop early.
            if (shiftsThisIter == 0) break;
        }   // end iter loop

        // patch83 — PCA-centering refine pass (opt-in via AlignPcaCenter)
        //
        // After the xcorr alignment loop converges, optionally run a
        // second pass that refines each spike's position to MINIMIZE
        // its distance to the cluster's mean PCA position:
        //
        //     dist²(s) = sum_f (pca_f(spike at shift s) − μ_pca[f])²
        //
        // where μ_pca[f] is the mean of feature f across the cluster's
        // current members and pca_f comes from projecting the freshly
        // re-extracted waveform through the canonical .pca basis with
        // the same normalisation Data[] uses (dimMin_/dimRange_).
        //
        // This is the KKExp analogue of klusters' patch82 PCA-energy
        // refine pass, but with the energy criterion (||pca||²) swapped
        // for the centering criterion (||pca − μ_pca_cluster||²).  The
        // centering version directly tightens cluster compactness in
        // PCA space — exactly the geometric property that downstream
        // EM / merge decisions key off.  Energy is useful when there
        // is no cluster mean to compare against (e.g. the klusters
        // single-cluster refine path); here we have full cluster
        // membership from the converged iter loop, so centering is
        // the natural choice.
        //
        // Cost: per qualifying spike, (2·m_timeShiftMaxAbs + 1) .fil
        // reads + PCA projections.  For typical maxShift=3, that's
        // 7 reads/projections per spike — well under a second per
        // chunk on a warm cache.
        //
        // Falls back silently (logs an info line, runs no refine) when:
        //   - filFpP71 is null (no .fil available)
        //   - hasBasis is false (no canonical PCA basis loaded)
        //   - AlignPcaCenter is 0 (the default)
        if (AlignPcaCenter && filFpP71 && hasBasis) {
            int p83Refined  = 0;
            int p83Skipped  = 0;
            int p83Examined = 0;
            for (int cid : finalIds) {
                members.clear();
                for (int i = 0; i < nPts; i++)
                    if (labels[i] == cid) members.push_back(i);
                const int Nc = static_cast<int>(members.size());
                if (Nc < 3) continue;

                // Compute cluster mean PCA position μ_pca over the kept
                // PCA feature columns.  Read straight from Data[] — it
                // was kept in sync by the iter loop's on-the-spot
                // re-featurize step.
                std::vector<double> muPca(
                    static_cast<size_t>(nPCAFeatsP71), 0.0);
                int clusterMembersWithGp = 0;
                for (int m : members) {
                    const int gp = pts[static_cast<size_t>(m)];
                    if (gp < 0 || gp >=
                        static_cast<int>(m_cumShift.size())) continue;
                    const float* row = Data.m_Data + gp * nDims;
                    for (int f = 0; f < nPCAFeatsP71; ++f)
                        muPca[static_cast<size_t>(f)] +=
                            static_cast<double>(row[f]);
                    ++clusterMembersWithGp;
                }
                if (clusterMembersWithGp < 3) continue;
                const double invN = 1.0 / clusterMembersWithGp;
                for (int f = 0; f < nPCAFeatsP71; ++f)
                    muPca[static_cast<size_t>(f)] *= invN;

                // Per-spike: sweep candidate shifts, pick the one that
                // minimizes distance² to μ_pca.
                for (int sp = 0; sp < Nc; ++sp) {
                    const int iLocal = members[static_cast<size_t>(sp)];
                    const int gp     = pts[static_cast<size_t>(iLocal)];
                    if (gp < 0 || gp >=
                        static_cast<int>(m_cumShift.size())) continue;

                    const int     prevCum   = m_cumShift[static_cast<size_t>(gp)];
                    const int64_t rawTsInt  = rawTsByLocal[static_cast<size_t>(iLocal)];
                    if (rawTsInt <= 0) continue;
                    ++p83Examined;

                    int    bestS    = 0;
                    double bestDist = std::numeric_limits<double>::infinity();
                    int    nValid   = 0;

                    for (int s = -m_timeShiftMaxAbs;
                             s <=  m_timeShiftMaxAbs; ++s) {
                        const int     candCum = prevCum + s;
                        const int64_t off     = rawTsInt + candCum - PeakSampleIndex;
                        if (off < 0 ||
                            off + nSamplesPerSpike > sessionSamplesP71 + 1)
                            continue;

                        if (fseeko(filFpP71,
                                   off * static_cast<off_t>(NbTotalChannels)
                                       * static_cast<off_t>(sizeof(int16_t)),
                                   SEEK_SET) != 0) continue;

                        bool readOk = true;
                        for (int t = 0; t < nSamplesPerSpike && readOk; ++t) {
                            if (fread(filRowP71.data(), sizeof(int16_t),
                                      static_cast<size_t>(NbTotalChannels),
                                      filFpP71)
                                != static_cast<size_t>(NbTotalChannels)) {
                                readOk = false; break;
                            }
                            for (int c = 0; c < nChan; ++c)
                                waveScratchP71[static_cast<size_t>(t * nChan + c)] =
                                    filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                        }
                        if (!readOk) continue;

                        if (isStderiv)
                            ApplySdiffAllpairsTemporalDiff(
                                waveScratchP71.data(), nChan, nSamplesPerSpike);

                        // PCA projection.  Mirrors the per-spike code in
                        // the iter loop body — same basis access, same
                        // dimMin_/dimRange_ normalisation.  Distance² is
                        // accumulated against muPca[featIdx] in the same
                        // normalised feature space.
                        double dist2 = 0.0;
                        const auto& meanCanon = m_timeShiftBasis.meanShifted[
                            static_cast<size_t>(candCanonical)];
                        const auto& evecCanon = m_timeShiftBasis.eigvecShifted[
                            static_cast<size_t>(candCanonical)];
                        for (int ch = 0; ch < basisNChan; ++ch) {
                            const auto& mu = meanCanon[static_cast<size_t>(ch)];
                            const auto& ev = evecCanon[static_cast<size_t>(ch)];
                            for (int k = 0; k < basisNComp; ++k) {
                                double val = 0.0;
                                for (int u = 0; u < basisData2use; ++u) {
                                    const int sIdx = basisRecShift + u;
                                    double xraw = static_cast<double>(
                                        waveScratchP71[static_cast<size_t>(
                                            sIdx * nChan + ch)]);
                                    if (basisCentered)
                                        xraw -= mu[static_cast<size_t>(u)];
                                    val += ev[static_cast<size_t>(
                                        k * basisData2use + u)] * xraw;
                                }
                                const int featIdx = ch * basisNComp + k;
                                if (featIdx >= nPCAFeatsP71 ||
                                    featIdx >= static_cast<int>(dimMin_.size()))
                                    continue;
                                const double normVal =
                                    (static_cast<float>(val) - dimMin_[featIdx])
                                  * dimRange_[featIdx];
                                const double diff = normVal
                                  - muPca[static_cast<size_t>(featIdx)];
                                dist2 += diff * diff;
                            }
                        }

                        ++nValid;
                        if (dist2 < bestDist) {
                            bestDist = dist2;
                            bestS    = s;
                        }
                    }   // end candidate-shift loop

                    if (nValid == 0) { ++p83Skipped; continue; }
                    if (bestS == 0) continue;   // already at the centre

                    m_cumShift[static_cast<size_t>(gp)] = prevCum + bestS;
                    ++p83Refined;
                }
            }   // end cluster loop

            Output("[Stage 2.8 m3] chunk %d patch83 PCA-centering: "
                   "examined %d, refined %d, skipped %d (off-edge)\n",
                   chunkIdx, p83Examined, p83Refined, p83Skipped);
        } else if (AlignPcaCenter) {
            Output("[Stage 2.8 m3] chunk %d patch83: AlignPcaCenter "
                   "requested but %s%s%s — skipping.\n",
                   chunkIdx,
                   !filFpP71 ? ".fil unavailable" : "",
                   (!filFpP71 && !hasBasis) ? " and " : "",
                   !hasBasis ? "PCA basis not loaded" : "");
        }

        }   // end if (rawSourceOk) — patch72

        // patch74 — post-alignment .res monotonicity check.
        //
        // After cumulative shifts are applied, two adjacent spikes whose
        // original detection timestamps were close together may end up
        // with crossed (newTs1 > newTs2 where origTs1 < origTs2).
        // Klusters explicitly handles this case (klustersdoc.cpp lines
        // 3922-3938) by sorting spikes by their new timestamps before
        // writing .res, but KKExp leaves spikes in original detection
        // order — TimeShiftFinalize writes them at their original .res
        // slots with only the shifted-timestamp value updated.
        //
        // Non-monotonic .res is technically valid (most consumers index
        // by spike number, not by sample-time), but several tools assume
        // .res is sorted (process_smrconvert, some plotting helpers,
        // grep-by-time analyses).  And a non-monotonic .res signals
        // something has likely gone wrong with the alignment — large
        // cross-detection shifts that the alignment shouldn't be
        // producing under normal operation.
        //
        // This check scans this chunk's pts in their original gp order,
        // computes the effective post-shift timestamp for each
        // (rawTs + m_cumShift), and warns when adjacent pairs cross.
        // It does NOT modify .res or m_cumShift — the audit purpose is
        // visibility, not correction.  If crossings are detected and
        // matter, the user can decide whether to: (a) tighten
        // m_timeShiftMaxAbs to prevent them, (b) port Klusters' sort
        // step into TimeShiftFinalize, or (c) accept them.
        {
            int crossings = 0;
            int64_t maxCross = 0;
            int firstCrossGp1 = -1, firstCrossGp2 = -1;
            for (int i = 1; i < nPts; ++i) {
                const int gpPrev = pts[static_cast<size_t>(i - 1)];
                const int gpCur  = pts[static_cast<size_t>(i)];
                if (gpPrev < 0 || gpCur < 0) continue;
                if (gpPrev >= static_cast<int>(m_cumShift.size()) ||
                    gpCur  >= static_cast<int>(m_cumShift.size())) continue;
                const int64_t tsPrev = rawTsByLocal[
                    static_cast<size_t>(i - 1)];
                const int64_t tsCur  = rawTsByLocal[
                    static_cast<size_t>(i)];
                if (tsPrev <= 0 || tsCur <= 0) continue;
                const int64_t effPrev = tsPrev
                    + m_cumShift[static_cast<size_t>(gpPrev)];
                const int64_t effCur  = tsCur
                    + m_cumShift[static_cast<size_t>(gpCur)];
                // Original order: tsPrev <= tsCur (assuming .res was
                // monotonic to start with — true for typical sessions).
                // Crossing: post-shift, effPrev > effCur.
                if (effPrev > effCur) {
                    if (crossings == 0) {
                        firstCrossGp1 = gpPrev;
                        firstCrossGp2 = gpCur;
                    }
                    const int64_t gap = effPrev - effCur;
                    if (gap > maxCross) maxCross = gap;
                    ++crossings;
                }
            }
            if (crossings > 0) {
                Output("[Stage 2.8 m3] chunk %d patch74: WARNING — "
                       "%d adjacent-spike timestamp crossings detected "
                       "after alignment (max gap %lld samples, "
                       "first at gp %d → gp %d).  .res will be written "
                       "non-monotonic by TimeShiftFinalize.  Tools that "
                       "assume .res is sorted may misbehave; consider "
                       "lowering m_timeShiftMaxAbs or porting Klusters' "
                       "sortedOrder+targetPos permutation (klustersdoc."
                       "cpp:3922-3938) into TimeShiftFinalize.\n",
                       chunkIdx, crossings,
                       static_cast<long long>(maxCross),
                       firstCrossGp1, firstCrossGp2);
            }
        }

        if (filFpP71) fclose(filFpP71);
    }

    // Write final labels back into the sub-KK and refresh its bookkeeping.
    for (int i = 0; i < nPts; i++) {
        const int v = labels[i];
        Ks.Class[i] = (v >= 0 && v < MaxPossibleClusters) ? v : 0;
    }
    for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
    for (int i = 0; i < nPts; i++) {
        const int c = Ks.Class[i];
        if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
    }
    Ks.Reindex();
    Ks.MStep();

    // The outer Phase 9 (TimeShiftFinalize) refeaturizes globally; we
    // don't call RefeaturizeFromShifts here because the parallel chunk
    // loop would race on Data[] writes if multiple chunks tried.
    //
    // Per-chunk final-alignment summary — bookend to the chunk-START
    // banner so each chunk has a paired START/ALIGN/END trio in the log.
    Output("[Stage 2.8 m3] chunk %d ALIGN: %d spike realigns (xcorr >= %.2f); "
           "skipped: %d low-score, %d out-of-bounds; ran %d align iters\n",
           chunkIdx, finalAligned,
           static_cast<double>(ResidualPCAMinScore),
           finalLowScore, finalOutOfBounds, alignItersRun);
    Output("[Stage 2.8 m3] chunk %d END: %d total splits, %d total realigns, "
           "ran %d/%d iters\n",
           chunkIdx, totalSplitsApplied, totalRealigned,
           actualItersRun, maxIter);
}
