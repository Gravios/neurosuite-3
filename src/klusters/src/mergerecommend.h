/***************************************************************************
 * mergerecommend.h — rank candidate cluster merges by agreement between the
 * error matrix and the residual matrix.
 *
 * Qt-free and header-only so the ranking can be unit-tested without a GUI.
 *
 * The two matrices disagree about almost everything mechanical:
 *
 *   - POLARITY.  The error matrix holds probabilities: HIGH means "these two
 *     look like one unit".  The residual matrix holds distances: LOW means the
 *     same thing.  They cannot be added without flipping one.
 *   - SYMMETRY.  Neither is symmetric.  The residual matrix is asymmetric by
 *     construction — upper-right is A-vs-B, lower-left B-vs-A, each row scaled
 *     by its own reference variance — and the error matrix's P(spike|cluster)
 *     normalisation is per-column.  Both are symmetrised by AVERAGING the two
 *     directions, which is what KlustersApp::slotSortClustersByErrorPval already
 *     does with the same matrix: 0.5 * (M(i,j) + M(j,i)).
 *
 *     This started out taking the pessimistic direction instead (min probability,
 *     max residual).  That was wrong, and wrong in a way that quietly hid the
 *     best candidates: these matrices are asymmetric for a REASON.  A fragment
 *     sitting inside a larger unit scores high one way (the fragment's spikes fit
 *     the big model) and low the other (the big cluster's spikes do not fit the
 *     fragment's tight model), and a fragment rejoining its parent is exactly the
 *     merge this panel exists to find.  Taking the minimum threw those away.
 *   - SCALE.  A probability and a variance-scaled residual share no units, and
 *     the residual's spread is data-dependent.  They are therefore combined by
 *     RANK, not by value: each metric is turned into its own percentile over
 *     the pairs actually on offer.
 *   - MEMBERSHIP.  The two matrices are computed independently and can hold
 *     different cluster sets (one recomputed since the last edit, the other
 *     not).  Only the intersection is scored; a pair missing from either matrix
 *     has no opinion from it and is dropped rather than guessed at.
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

/**One recommended merge.  @a errorScore and @a residualScore are the raw
 * symmetrised values (for display); @a quality is the combined rank in [0,1],
 * 1 being the best pair on offer.*/
struct MergeCandidate {
    int    a = -1;
    int    b = -1;
    double errorScore    = 0.0;   // symmetrised probability, higher = closer
    double residualScore = 0.0;   // symmetrised residual, lower = closer
    double quality       = 0.0;   // min of the two percentile ranks
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
 * @param resIds   cluster ids, in residual-matrix row/col order.
 * @param resAt    0-based accessor into the residual matrix.
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
inline std::vector<MergeCandidate> mrRecommendMerges(
    const std::vector<int>& errIds,
    const std::function<double(int,int)>& errAt,
    const std::vector<int>& resIds,
    const std::function<double(int,int)>& resAt,
    std::size_t maxCount,
    double errorMin,
    double qualityFloor,
    const std::vector<int>& restrictTo = std::vector<int>())
{
    std::vector<MergeCandidate> out;

    std::map<int,int> errRow, resRow;
    for (std::size_t i = 0; i < errIds.size(); ++i) errRow[errIds[i]] = static_cast<int>(i);
    for (std::size_t i = 0; i < resIds.size(); ++i) resRow[resIds[i]] = static_cast<int>(i);

    // Only clusters BOTH matrices know about can be scored by both.
    std::vector<int> shared;
    for (const int id : errIds)
        if (resRow.count(id)) shared.push_back(id);
    std::sort(shared.begin(), shared.end());
    if (shared.size() < 2) return out;

    std::vector<int> sel(restrictTo);
    std::sort(sel.begin(), sel.end());
    const bool restricted = !sel.empty();
    auto isSelected = [&sel](int id){
        return std::binary_search(sel.begin(), sel.end(), id);
    };

    // NB the restriction is applied AFTER ranking, not here.  Ranking only the
    // selected cluster's own pairs would make "top decile" mean "top decile among
    // this cluster's partners", so a cluster with nothing worth merging would
    // still present its least-bad partner as a 0.9+ recommendation.  Quality has
    // to mean the same thing whatever is selected, so the percentiles are always
    // taken over every pair in the session and the selection only filters what is
    // shown.
    std::vector<MergeCandidate> pairs;
    std::vector<double> errVals, resVals;
    for (std::size_t i = 0; i < shared.size(); ++i) {
        for (std::size_t j = i + 1; j < shared.size(); ++j) {
            const int a = shared[i], b = shared[j];

            const int ea = errRow[a], eb = errRow[b];
            const int ra = resRow[a], rb = resRow[b];

            // Mean of the two directions, matching slotSortClustersByErrorPval.
            const double e = 0.5 * (errAt(ea, eb) + errAt(eb, ea));
            const double r = 0.5 * (resAt(ra, rb) + resAt(rb, ra));

            // Absolute gate before ranking, so a disbelieved pair cannot be
            // ranked into the list by being merely less bad than the rest — and
            // so it does not occupy a rank slot and depress the real ones.
            if (e < errorMin) continue;

            MergeCandidate c;
            c.a = a; c.b = b; c.errorScore = e; c.residualScore = r;
            pairs.push_back(c);
            errVals.push_back(e);
            resVals.push_back(r);
        }
    }
    if (pairs.empty()) return out;

    const std::vector<double> eRank = mrPercentileRanks(errVals, /*bestIsHigh*/ true);
    const std::vector<double> rRank = mrPercentileRanks(resVals, /*bestIsHigh*/ false);
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

#endif // MERGERECOMMEND_H
