/***************************************************************************
 * mergerecommend.h — rank candidate cluster merges by agreement between the
 * error matrix and the residual matrix.
 *
 * Qt-free and header-only so the ranking can be unit-tested without a GUI.
 *
 * The two witnesses are the error matrix and the waveform-envelope overlap
 * (waveformiou.h).  They are combined by RANK, because they have no common unit:
 * one is a probability under a Gaussian feature model, the other a fraction of
 * overlapping envelope area.
 *
 * The second witness used to be the residual matrix.  It was replaced because it
 * forced two things this does not:
 *
 *   - SYMMETRY.  The residual matrix is asymmetric by construction (each row
 *     scaled by its own reference variance), so it demanded a choice about which
 *     direction to believe — and the first choice, the pessimistic one, silently
 *     discarded exactly the fragment/parent pairs the panel exists to find.  IOU
 *     is symmetric with nothing to choose.
 *   - AVAILABILITY.  It required a residual matrix display to be open and
 *     computed.  The overlap reads the cached mean and SD, which are already
 *     there for the waveform view.
 *
 * The error matrix remains asymmetric, so it is still symmetrised — by averaging
 * the two directions, matching KlustersApp::slotSortClustersByErrorPval:
 * 0.5 * (M(i,j) + M(j,i)).
 *
 * Membership still matters: a cluster with no computed waveform has no overlap to
 * report, and such pairs are dropped rather than guessed at.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MERGERECOMMEND_H
#define MERGERECOMMEND_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <vector>

/**One recommended merge.  @a errorScore is the symmetrised probability and
 * @a overlapScore the envelope IOU (both for display); @a quality is the
 * combined rank in [0,1], 1 being the best pair on offer.*/
struct MergeCandidate {
    int    a = -1;
    int    b = -1;
    double errorScore   = 0.0;   // symmetrised probability, higher = closer
    double overlapScore = 0.0;   // envelope IOU in [0,1], higher = closer
    double quality      = 0.0;   // min of the two percentile ranks
};

/**Percentile rank of each value in @p v, in [0,1].  @p bestIsHigh selects the
 * direction.  Ties share the mean rank so two identical scores cannot be
 * ordered arbitrarily.  A degenerate spread (every value equal) maps to 1 —
 * nothing distinguishes the pairs, so that metric abstains rather than
 * inventing an order.*/
inline std::vector<double> mrPercentileRanks(const std::vector<double>& v,
                                             bool bestIsHigh)
{
    const std::size_t n = v.size();
    std::vector<double> out(n, 1.0);
    if (n < 2) return out;

    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](std::size_t x, std::size_t y){
        return bestIsHigh ? (v[x] > v[y]) : (v[x] < v[y]);
    });

    // Walk runs of equal value so ties get the same rank.
    std::size_t i = 0;
    while (i < n) {
        std::size_t j = i;
        while (j + 1 < n && v[idx[j + 1]] == v[idx[i]]) ++j;
        const double meanPos = 0.5 * (static_cast<double>(i) + static_cast<double>(j));
        const double rank    = 1.0 - meanPos / static_cast<double>(n - 1);
        for (std::size_t k = i; k <= j; ++k) out[idx[k]] = rank;
        i = j + 1;
    }
    return out;
}

/**Rank candidate merges by agreement between the two matrices.
 *
 * @param errIds   cluster ids, in error-matrix row/col order.
 * @param errAt    0-based accessor into the error matrix.
 * @param overlapOf  envelope IOU for a pair of CLUSTER IDS; returns false when
 *        either cluster has no computed waveform, and that pair is then dropped.
 * @param maxCount hard cap on the returned list.
 * @param errorMin  ABSOLUTE floor on the symmetrised error probability.
 * @param qualityFloor minimum combined rank to be called "high quality".
 * @param restrictTo if non-empty, keep only pairs with at least one member in
 *        this set — the panel passes the palette selection, so recommendations
 *        answer "what should merge with what I am looking at".  Empty = every
 *        pair.
 *
 * quality = min(errorRank, residualRank): a pair is only as good as its WEAKER
 * witness, so one matrix cannot carry a pair the other dislikes.  That is the
 * whole point of consulting two.
 *
 * Ranks alone are not enough to call anything "high quality": a percentile is
 * relative, so the best pairs ON OFFER always rank top even in a session where
 * nothing should merge at all, and a pair the error matrix scores at 0.02 —
 * i.e. "certainly not one unit" — can still rank well simply by being less bad
 * than its neighbours.  @p errorMin is therefore an absolute gate: the error
 * matrix holds probabilities, which mean something on their own, so a pair it
 * disbelieves is dropped outright no matter how it ranks.  The residual has no
 * comparable absolute reading — its spread is data-dependent — so it is left to
 * speak by rank only.
 */
/**A pair that has cleared the absolute error gate, carrying its symmetrised
 * error score.  This is the boundary between the two halves of the ranking: the
 * O(clusters^2) sweep that produces these, and the expensive per-pair overlap
 * scoring that consumes them.*/
struct MergePair {
    int    a = -1;
    int    b = -1;
    double errorScore = 0.0;
};

/**First half: sweep every unordered pair and keep those clearing @p errorMin.
 *
 * Split out of mrRecommendMerges so the caller can run this cheap arithmetic
 * sweep and the expensive ranking half on DIFFERENT THREADS -- the sweep needs
 * live access to the error matrix, which is owned by the GUI thread and freed
 * wholesale when a new one lands, while the ranking half needs only the values
 * gathered here.  Splitting is what lets the ranking be handed to a worker
 * without copying the whole clusters x clusters matrix (610 MB at 8736
 * clusters) or risking a use-after-free on it.
 *
 * The gate is absolute and applied BEFORE ranking so a disbelieved pair cannot
 * be ranked in by being merely less bad than the rest, and so it does not
 * occupy a rank slot and depress the real ones.*/
inline std::vector<MergePair> mrGatePairsByError(
    const std::vector<int>& errIds,
    const std::function<double(int,int)>& errAt,
    double errorMin)
{
    std::vector<MergePair> gated;

    std::map<int,int> errRow;
    for (std::size_t i = 0; i < errIds.size(); ++i) errRow[errIds[i]] = static_cast<int>(i);

    std::vector<int> shared(errIds);
    std::sort(shared.begin(), shared.end());
    if (shared.size() < 2) return gated;

    // Resolve each cluster's matrix row ONCE, up front.  Doing the two
    // errRow[] lookups inside the inner loop costs a std::map probe per pair --
    // 76 M cache-missing probes at 8736 clusters, which measured 3.5 s and
    // dominated the sweep.  Hoisted, the inner loop is two vector reads.
    std::vector<int> rowOf(shared.size());
    for (std::size_t i = 0; i < shared.size(); ++i) rowOf[i] = errRow[shared[i]];

    for (std::size_t i = 0; i < shared.size(); ++i) {
        const int a  = shared[i];
        const int ea = rowOf[i];
        for (std::size_t j = i + 1; j < shared.size(); ++j) {
            const int b  = shared[j];
            const int eb = rowOf[j];

            // The error matrix is asymmetric: mean of the two directions, matching
            // slotSortClustersByErrorPval.  The overlap needs no such choice.
            const double e = 0.5 * (errAt(ea, eb) + errAt(eb, ea));
            if (e < errorMin) continue;

            MergePair p; p.a = a; p.b = b; p.errorScore = e;
            gated.push_back(p);
        }
    }
    return gated;
}

/**Second half: score each gated pair's envelope overlap, rank both witnesses by
 * percentile, and return the best @p maxCount.
 *
 * @p cancelled, if set, is polled during the overlap loop; returning true
 * abandons the run and yields an empty result.  This is the expensive half --
 * one envelope IOU with a lag search per surviving pair -- so it must be
 * interruptible when it runs on a worker.*/
inline std::vector<MergeCandidate> mrRankGatedPairs(
    const std::vector<MergePair>& gated,
    const std::function<bool(int,int,double&)>& overlapOf,
    std::size_t maxCount,
    double qualityFloor,
    const std::vector<int>& restrictTo = std::vector<int>(),
    const std::function<bool()>& cancelled = std::function<bool()>())
{
    std::vector<MergeCandidate> out;

    std::vector<int> sel(restrictTo);
    std::sort(sel.begin(), sel.end());
    const bool restricted = !sel.empty();
    auto isSelected = [&sel](int id){
        return std::binary_search(sel.begin(), sel.end(), id);
    };

    // NB the restriction is applied AFTER ranking, not during the sweep.  Ranking
    // only the selected cluster's own pairs would make "top decile" mean "top
    // decile among this cluster's partners", so a cluster with nothing worth
    // merging would still present its least-bad partner as a 0.9+
    // recommendation.  Quality has to mean the same thing whatever is selected,
    // so the percentiles are always taken over every pair in the session and the
    // selection only filters what is shown.
    std::vector<MergeCandidate> pairs;
    std::vector<double> errVals, resVals;
    for (std::size_t k = 0; k < gated.size(); ++k) {
        if (cancelled && (k % 4096u) == 0u && cancelled()) return out;

        const int a = gated[k].a, b = gated[k].b;
        const double e = gated[k].errorScore;

        double ov = 0.0;
        if (!overlapOf(a, b, ov)) continue;   // no waveform -> no opinion

        MergeCandidate c;
        c.a = a; c.b = b; c.errorScore = e; c.overlapScore = ov;
        pairs.push_back(c);
        errVals.push_back(e);
        resVals.push_back(ov);
    }
    if (pairs.empty()) return out;

    const std::vector<double> eRank = mrPercentileRanks(errVals, /*bestIsHigh*/ true);
    const std::vector<double> rRank = mrPercentileRanks(resVals, /*bestIsHigh*/ true);   // IOU: higher = closer
    for (std::size_t k = 0; k < pairs.size(); ++k)
        pairs[k].quality = std::min(eRank[k], rRank[k]);

    std::stable_sort(pairs.begin(), pairs.end(),
        [](const MergeCandidate& x, const MergeCandidate& y){
            return x.quality > y.quality;
        });

    for (const MergeCandidate& c : pairs) {
        if (out.size() >= maxCount) break;
        if (c.quality < qualityFloor) break;   // sorted: nothing after this passes
        if (restricted && !isSelected(c.a) && !isSelected(c.b)) continue;
        out.push_back(c);
    }
    return out;
}

/**Both halves, in sequence, on the calling thread.  Behaviour is exactly what it
 * was before the split, so existing callers and tests are unaffected.*/
inline std::vector<MergeCandidate> mrRecommendMerges(
    const std::vector<int>& errIds,
    const std::function<double(int,int)>& errAt,
    const std::function<bool(int,int,double&)>& overlapOf,
    std::size_t maxCount,
    double errorMin,
    double qualityFloor,
    const std::vector<int>& restrictTo = std::vector<int>())
{
    return mrRankGatedPairs(mrGatePairsByError(errIds, errAt, errorMin),
                            overlapOf, maxCount, qualityFloor, restrictTo);
}

#endif // MERGERECOMMEND_H
