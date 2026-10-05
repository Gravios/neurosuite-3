// ═══════════════════════════════════════════════════════════════════════════
//  running_stats.hpp — running mean/std summaries for the manual template lineage
//
//  A lineage node stores the MEAN (not median) of the spikes behind it, plus
//  their population std and count.  The mean composes EXACTLY under weighting, so
//  a node is updated by folding a new selection's summary into its own with
//  combineMeanStd (the parallel-variance / Chan formula) and the originating
//  spikes are never kept — see claude/template-curation-plan.md.  A median, by
//  contrast, cannot be recombined from summaries (it would need the spikes).
//
//  Pure: depends only on the standard library.  Shared by template_generate.hpp
//  (render = copy the stored mean) and drift_partition.hpp (re-grain = combine
//  overlapping region summaries).
// ═══════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_RUNNING_STATS_HPP
#define NEUROSUITE_CORE_RUNNING_STATS_HPP

#include <cmath>
#include <cstdint>
#include <vector>

namespace neurosuite {
namespace stats {

struct MeanStd {
    std::vector<float> mean;        ///< per-element mean (empty when count==0)
    std::vector<float> std;         ///< per-element population std (empty when count==0)
    int64_t            count = 0;
};

// Mean + population std over a COMPACT int16 record stack (`recs` = count*recLen
// values; count = recs.size()/recLen) — one streaming pass, O(count·recLen).
inline MeanStd meanStdOfRecords(const std::vector<int16_t>& recs, std::size_t recLen)
{
    MeanStd r;
    if (recLen == 0 || recs.size() % recLen != 0) return r;
    const std::size_t n = recs.size() / recLen;
    r.count = static_cast<int64_t>(n);
    if (n == 0) return r;
    std::vector<double> sum(recLen, 0.0), sumsq(recLen, 0.0);
    for (std::size_t m = 0; m < n; ++m)
        for (std::size_t e = 0; e < recLen; ++e) {
            const double x = static_cast<double>(recs[m * recLen + e]);
            sum[e] += x; sumsq[e] += x * x;
        }
    r.mean.assign(recLen, 0.f);
    r.std .assign(recLen, 0.f);
    for (std::size_t e = 0; e < recLen; ++e) {
        const double mean = sum[e] / static_cast<double>(n);
        double var = sumsq[e] / static_cast<double>(n) - mean * mean;   // population variance
        if (var < 0.0) var = 0.0;                                       // fp guard
        r.mean[e] = static_cast<float>(mean);
        r.std [e] = static_cast<float>(std::sqrt(var));
    }
    return r;
}

// EXACT combine of two running summaries A and B (parallel-variance / Chan's
// formula, per element, population variance).  Either may be empty (count 0).
// This is the weighted update: fold a new selection's summary into a node's.
inline MeanStd combineMeanStd(const std::vector<float>& meanA, const std::vector<float>& stdA, int64_t countA,
                              const std::vector<float>& meanB, const std::vector<float>& stdB, int64_t countB)
{
    MeanStd r;
    if (countA <= 0) { r.mean = meanB; r.std = stdB; r.count = (countB > 0 ? countB : 0); return r; }
    if (countB <= 0) { r.mean = meanA; r.std = stdA; r.count = countA; return r; }
    const std::size_t L = meanA.size();
    if (meanB.size() != L) { r.mean = meanA; r.std = stdA; r.count = countA; return r; }  // geometry guard
    r.count = countA + countB;
    r.mean.assign(L, 0.f); r.std.assign(L, 0.f);
    const double nA = static_cast<double>(countA), nB = static_cast<double>(countB), n = nA + nB;
    for (std::size_t e = 0; e < L; ++e) {
        const double mA = meanA[e], mB = meanB[e];
        const double sA = (e < stdA.size() ? static_cast<double>(stdA[e]) : 0.0);
        const double sB = (e < stdB.size() ? static_cast<double>(stdB[e]) : 0.0);
        const double M2A = sA * sA * nA, M2B = sB * sB * nB;   // M2 = var*count (population)
        const double delta = mB - mA;
        const double mean  = mA + delta * nB / n;
        const double M2    = M2A + M2B + delta * delta * nA * nB / n;
        double var = M2 / n;
        if (var < 0.0) var = 0.0;
        r.mean[e] = static_cast<float>(mean);
        r.std [e] = static_cast<float>(std::sqrt(var));
    }
    return r;
}

} // namespace stats
} // namespace neurosuite

#endif // NEUROSUITE_CORE_RUNNING_STATS_HPP
