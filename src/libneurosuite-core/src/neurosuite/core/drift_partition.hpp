// ═══════════════════════════════════════════════════════════════════════════
//  drift_partition.hpp — the SESSION-WIDE drift partition + forest re-grain
//
//  The curator's manual template lineage (.wtl, see template-curation-plan.md)
//  organises each class's drift roots along TIME.  The time regions are a single
//  SESSION-WIDE partition, shared by every template class — all cells drift
//  together, so the grain is global, not per class.  This header is that shared
//  object and the operations that re-grain a .wtl forest when the partition
//  changes.
//
//  Model: a Partition tiles [0, tEnd) seconds into contiguous, DISJOINT regions
//  (a strictly-increasing list of interior boundaries).  Every region carries a
//  drift-root per class (an empty placeholder when count 0), so the partition is
//  fully recoverable from the tiling drift-root windows — no separate on-disk
//  partition object is needed.
//
//  Re-grain is a pure FOREST → FOREST transform over the nodes' running SUMMARIES
//  (mean/std/count — no spikes).  Each new region's drift-root is the EXACT weighted
//  combine of every old drift-root whose window overlaps it (combineMeanStd): a
//  merge combines, a split duplicates the parent into each half, a region nothing
//  overlaps is an empty placeholder.  Leaves keep their own summary and re-parent to
//  the new region their old parent root maps to.  renderLineage() then just copies
//  the stored means (no re-median — the median cannot be recombined from summaries).
//
//  Qt-free, header-only (std + neurofileio structs), so it is harness-testable and
//  usable from both Klusters and a headless tool.
// ═══════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_DRIFT_PARTITION_HPP
#define NEUROSUITE_CORE_DRIFT_PARTITION_HPP

#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/running_stats.hpp"   // combineMeanStd (re-grain = combine overlapping summaries)

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace neurosuite {
namespace drift {

// ── the session-wide partition ──────────────────────────────────────────────
struct Partition {
    double              tEnd = 0.0;   ///< session end (seconds); region n-1 ends here
    std::vector<double> bounds;       ///< interior boundaries (s), strictly increasing in (0,tEnd)

    int nRegions() const { return static_cast<int>(bounds.size()) + 1; }

    // Region edges: edge(0)=0, edge(nRegions)=tEnd, edge(i)=bounds[i-1] otherwise.
    double edge(int i) const {
        if (i <= 0)            return 0.0;
        if (i >= nRegions())   return tEnd;
        return bounds[static_cast<std::size_t>(i) - 1];
    }
    std::pair<double,double> region(int i) const { return { edge(i), edge(i + 1) }; }

    // Half-open [edge(i), edge(i+1)); the last region is closed at tEnd so a spike
    // exactly at tEnd lands in it.  Clamped to [0, nRegions-1].
    int regionOf(double t) const {
        const int n = nRegions();
        if (t <= 0.0) return 0;
        if (t >= tEnd) return n - 1;
        // first boundary strictly greater than t = the region index
        int idx = static_cast<int>(std::upper_bound(bounds.begin(), bounds.end(), t) - bounds.begin());
        if (idx < 0) idx = 0;
        if (idx > n - 1) idx = n - 1;
        return idx;
    }

    // Boundaries strictly increasing and strictly inside (0, tEnd).
    bool valid() const {
        if (tEnd <= 0.0) return false;
        double prev = 0.0;
        for (double b : bounds) {
            if (!(b > prev) || !(b < tEnd)) return false;
            prev = b;
        }
        return true;
    }
};

// A uniform k-region partition of [0, tEnd).  k<=1 (or tEnd<=0) → a single region.
inline Partition uniformPartition(double tEnd, int k)
{
    Partition p;
    p.tEnd = tEnd > 0.0 ? tEnd : 0.0;
    if (k > 1 && p.tEnd > 0.0)
        for (int i = 1; i < k; ++i) p.bounds.push_back(p.tEnd * static_cast<double>(i) / k);
    return p;
}

// Recover the partition from a set of drift-root windows (the tiling read back
// from a .wtl/.wti): the distinct interior edges, with tEnd the largest `b`.
// Degenerate/overlapping inputs are cleaned (sorted-unique edges inside (0,tEnd)).
inline Partition partitionFromWindows(const std::vector<std::pair<double,double>>& windows,
                                      double tEndHint = 0.0)
{
    Partition p;
    double tEnd = tEndHint;
    for (const auto& w : windows) { tEnd = std::max(tEnd, w.first); tEnd = std::max(tEnd, w.second); }
    p.tEnd = tEnd;
    if (tEnd <= 0.0) return p;
    std::vector<double> edges;
    for (const auto& w : windows) {
        if (w.first  > 0.0 && w.first  < tEnd) edges.push_back(w.first);
        if (w.second > 0.0 && w.second < tEnd) edges.push_back(w.second);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    p.bounds = std::move(edges);
    return p;
}

// Split region i at time t: insert a boundary at t.  No-op (false) unless t lies
// strictly inside region i.
inline bool splitRegion(Partition& p, int i, double t)
{
    if (i < 0 || i >= p.nRegions()) return false;
    const auto ab = p.region(i);
    if (!(t > ab.first && t < ab.second)) return false;
    p.bounds.push_back(t);
    std::sort(p.bounds.begin(), p.bounds.end());
    return true;
}

// Delete the boundary between region i and i+1 (merge them).  No-op (false)
// unless 0 <= i < nRegions()-1 (there is a boundary i to remove).
inline bool mergeRegion(Partition& p, int i)
{
    if (i < 0 || i >= p.nRegions() - 1) return false;   // boundary index == i
    p.bounds.erase(p.bounds.begin() + i);
    return true;
}

// ── forest re-grain ──────────────────────────────────────────────────────────
namespace detail {
inline bool isDriftKind(const std::string& kind) { return kind.rfind("drift", 0) == 0; }

// Res time (seconds) of a spike index, or a sentinel for out-of-range.
inline double timeOf(const std::vector<int64_t>& times, double sr, int64_t i)
{
    if (sr <= 0.0 || i < 0 || i >= static_cast<int64_t>(times.size())) return -1.0;
    return static_cast<double>(times[static_cast<std::size_t>(i)]) / sr;
}

// Median time (seconds) of a spike set, or -1 if none are in range.
inline double medianTime(const std::vector<int64_t>& spikes,
                         const std::vector<int64_t>& times, double sr)
{
    std::vector<double> ts;
    ts.reserve(spikes.size());
    for (int64_t s : spikes) { const double t = timeOf(times, sr, s); if (t >= 0.0) ts.push_back(t); }
    if (ts.empty()) return -1.0;
    std::sort(ts.begin(), ts.end());
    return ts[ts.size() / 2];
}
} // namespace detail

namespace detail {
// Do windows [a0,b0) and [a1,b1) overlap?
inline bool overlaps(double a0, double b0, double a1, double b1) { return a0 < b1 && b0 > a1; }
// Overlap length of [a0,b0) and [a1,b1) (0 if disjoint).
inline double overlapLen(double a0, double b0, double a1, double b1)
{ const double lo = std::max(a0, a1), hi = std::min(b0, b1); return (hi > lo) ? (hi - lo) : 0.0; }
} // namespace detail

// Re-tile `forest`'s running SUMMARIES onto partition `p` (no spikes needed — the
// nodes carry {mean,std,count}).  For every class, each NEW region's drift-root is
// the EXACT weighted combine (combineMeanStd) of every OLD drift-root whose window
// overlaps it.  This covers all three cases with one rule:
//   * exact match      — one old root overlaps → copied;
//   * merge regions    — several old roots fall inside → combined (exact);
//   * split a region   — one old root overlaps both halves → DUPLICATED into each
//                        (per the curator's "duplicates are fine" — non-destructive;
//                        refine each with the next update);
//   * boundary move    — partial overlaps on both sides → combined (approximate,
//                        reweighted on the next update).
// Leaves keep their own summary and re-parent to the new region their OLD parent
// root maps to (max overlap).  Node ids are reassigned densely; windows become each
// region's [a,b].
inline neurofileio::WtlForest regrainForest(const neurofileio::WtlForest& forest,
                                            const Partition& p)
{
    neurofileio::WtlForest out;
    out.version = 2; out.nSamples = forest.nSamples; out.nChannels = forest.nChannels;
    const int nR = p.nRegions();

    // Distinct classes in first-seen order (stable output).
    std::vector<int> classes;
    for (const neurofileio::WtlNode& n : forest.nodes)
        if (std::find(classes.begin(), classes.end(), n.classId) == classes.end())
            classes.push_back(n.classId);

    auto isRoot = [](const neurofileio::WtlNode& n){ return detail::isDriftKind(n.kind) && n.parent < 0; };

    int nextId = 0;
    for (int cls : classes) {
        // This class's OLD drift roots (with their windows + summaries).
        std::vector<const neurofileio::WtlNode*> oldRoots;
        for (const neurofileio::WtlNode& n : forest.nodes)
            if (n.classId == cls && isRoot(n)) oldRoots.push_back(&n);

        // One NEW drift-root per region = combine of the old roots overlapping it.
        std::vector<int> rootId(static_cast<std::size_t>(nR), -1);
        for (int r = 0; r < nR; ++r) {
            const auto ab = p.region(r);
            neurosuite::stats::MeanStd acc;                 // empty = placeholder
            for (const neurofileio::WtlNode* orp : oldRoots)
                if (detail::overlaps(orp->a, orp->b, ab.first, ab.second))
                    acc = neurosuite::stats::combineMeanStd(acc.mean, acc.std, acc.count,
                                                            orp->mean, orp->std, orp->count);
            neurofileio::WtlNode root;
            root.node = nextId++;
            root.classId = cls;
            root.kind = "drift-root";
            root.parent = -1;
            root.a = ab.first; root.b = ab.second;
            root.count = acc.count; root.mean = std::move(acc.mean); root.std = std::move(acc.std);
            rootId[static_cast<std::size_t>(r)] = root.node;
            out.nodes.push_back(std::move(root));
        }

        // Old root node id -> the new region it overlaps most (for leaf re-parenting).
        std::map<int,int> oldRootRegion;
        for (const neurofileio::WtlNode* orp : oldRoots) {
            int best = 0; double bestOv = -1.0;
            for (int r = 0; r < nR; ++r) {
                const auto ab = p.region(r);
                const double ov = detail::overlapLen(orp->a, orp->b, ab.first, ab.second);
                if (ov > bestOv) { bestOv = ov; best = r; }
            }
            oldRootRegion[orp->node] = best;
        }

        // Re-attach this class's leaves (own summary kept) under the new region of
        // their old parent root.
        for (const neurofileio::WtlNode& n : forest.nodes) {
            if (n.classId != cls || isRoot(n)) continue;    // leaves only
            auto it = oldRootRegion.find(n.parent);
            const int r = (it != oldRootRegion.end()) ? it->second : 0;
            neurofileio::WtlNode leaf = n;                  // copies mean/std/count
            leaf.node = nextId++;
            leaf.parent = rootId[static_cast<std::size_t>(r)];
            out.nodes.push_back(std::move(leaf));
        }
    }
    out.ok = true;
    return out;
}

// Build a fresh tiled drift series for ONE class, appending to `forest`: one EMPTY
// placeholder drift-root per region (count 0).  The curator populates each region
// by folding a selection in later; no spikes are needed to seed.  New node ids
// continue after the max present.
inline void tileClassDrift(neurofileio::WtlForest& forest, int classId, const Partition& p)
{
    int nextId = 0;
    for (const neurofileio::WtlNode& n : forest.nodes) nextId = std::max(nextId, n.node + 1);
    const int nR = p.nRegions();
    for (int r = 0; r < nR; ++r) {
        neurofileio::WtlNode root;
        root.node = nextId++;
        root.classId = classId;
        root.kind = "drift-root";
        root.parent = -1;
        const auto ab = p.region(r);
        root.a = ab.first; root.b = ab.second;
        root.count = 0;                                     // empty placeholder
        forest.nodes.push_back(std::move(root));
    }
}

} // namespace drift
} // namespace neurosuite

#endif // NEUROSUITE_CORE_DRIFT_PARTITION_HPP
