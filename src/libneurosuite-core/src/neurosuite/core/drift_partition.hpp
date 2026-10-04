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
//  drift-root per class (a 0-spike placeholder when empty), so the partition is
//  fully recoverable from the tiling drift-root windows — no separate on-disk
//  partition object is needed.
//
//  Re-grain is a pure FOREST → FOREST transform: it pools each class's drift-root
//  spikes (the source of truth) and re-bins them onto the new regions, then
//  re-parents each adapt/collision leaf under the region containing its spikes'
//  median time.  renderLineage() then produces the waveforms, so regrain never
//  interpolates a median — it re-medians by reassigning spikes (exact).  Splitting
//  and deleting a partition both reduce to "re-bin the pooled spikes onto p".
//
//  Qt-free, header-only (std + neurofileio structs), so it is harness-testable and
//  usable from both Klusters and a headless tool.
// ═══════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_DRIFT_PARTITION_HPP
#define NEUROSUITE_CORE_DRIFT_PARTITION_HPP

#include "neurosuite/core/neurofileio.h"

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

// Re-tile `forest` onto partition `p`.  For every class present, the class's
// drift-root spikes are POOLED and re-binned by region: one drift-root per region
// (its spikes = the pooled spikes whose res time falls in that region; an empty
// region is a 0-spike placeholder, so the result tiles the session).  Each
// adapt/collision leaf is kept (its spikes unchanged) and re-parented under the
// drift-root of the region containing its spikes' median time.  Node ids are
// reassigned densely; windows are set to each region's [a,b] seconds.  Out-of-
// range spike indices (a session that shrank under the lineage) are dropped.
inline neurofileio::WtlForest regrainForest(const neurofileio::WtlForest& forest,
                                            const std::vector<int64_t>& times, double sr,
                                            const Partition& p)
{
    neurofileio::WtlForest out;
    out.version = 1;
    const int nR = p.nRegions();

    // Distinct classes in first-seen order (stable output).
    std::vector<int> classes;
    for (const neurofileio::WtlNode& n : forest.nodes)
        if (std::find(classes.begin(), classes.end(), n.classId) == classes.end())
            classes.push_back(n.classId);

    int nextId = 0;
    for (int cls : classes) {
        // Pool this class's drift-root spikes (the source of truth), re-bin by region.
        std::vector<std::vector<int64_t>> byRegion(static_cast<std::size_t>(nR));
        for (const neurofileio::WtlNode& n : forest.nodes) {
            if (n.classId != cls || !detail::isDriftKind(n.kind) || n.parent >= 0) continue;
            for (int64_t s : n.spikes) {
                const double t = detail::timeOf(times, sr, s);
                if (t < 0.0) continue;                       // drop out-of-range
                byRegion[static_cast<std::size_t>(p.regionOf(t))].push_back(s);
            }
        }
        // One drift-root per region (placeholder if empty); record its node id so
        // leaves can re-parent to it.
        std::vector<int> rootId(static_cast<std::size_t>(nR), -1);
        for (int r = 0; r < nR; ++r) {
            std::vector<int64_t>& sp = byRegion[static_cast<std::size_t>(r)];
            std::sort(sp.begin(), sp.end());
            sp.erase(std::unique(sp.begin(), sp.end()), sp.end());
            neurofileio::WtlNode root;
            root.node = nextId++;
            root.classId = cls;
            root.kind = "drift-root";
            root.parent = -1;
            const auto ab = p.region(r);
            root.a = ab.first; root.b = ab.second;
            root.spikes = sp;
            rootId[static_cast<std::size_t>(r)] = root.node;
            out.nodes.push_back(std::move(root));
        }
        // Re-attach this class's leaves under the region of their median time.
        for (const neurofileio::WtlNode& n : forest.nodes) {
            if (n.classId != cls || (detail::isDriftKind(n.kind) && n.parent < 0)) continue;
            const double mt = detail::medianTime(n.spikes, times, sr);
            const int r = (mt >= 0.0) ? p.regionOf(mt) : 0;
            neurofileio::WtlNode leaf = n;
            leaf.node = nextId++;
            leaf.parent = rootId[static_cast<std::size_t>(r)];
            out.nodes.push_back(std::move(leaf));
        }
    }
    out.ok = true;
    return out;
}

// Build a fresh tiled drift series for ONE class from a flat spike set (the
// no-.wtl default, or a cluster selection), appending to `forest`: one drift-root
// per region, placeholder if empty.  New node ids continue after the max present.
inline void tileClassDrift(neurofileio::WtlForest& forest, int classId,
                           const std::vector<int64_t>& driftSpikes,
                           const std::vector<int64_t>& times, double sr, const Partition& p)
{
    int nextId = 0;
    for (const neurofileio::WtlNode& n : forest.nodes) nextId = std::max(nextId, n.node + 1);

    const int nR = p.nRegions();
    std::vector<std::vector<int64_t>> byRegion(static_cast<std::size_t>(nR));
    for (int64_t s : driftSpikes) {
        const double t = detail::timeOf(times, sr, s);
        if (t < 0.0) continue;
        byRegion[static_cast<std::size_t>(p.regionOf(t))].push_back(s);
    }
    for (int r = 0; r < nR; ++r) {
        std::vector<int64_t>& sp = byRegion[static_cast<std::size_t>(r)];
        std::sort(sp.begin(), sp.end());
        sp.erase(std::unique(sp.begin(), sp.end()), sp.end());
        neurofileio::WtlNode root;
        root.node = nextId++;
        root.classId = classId;
        root.kind = "drift-root";
        root.parent = -1;
        const auto ab = p.region(r);
        root.a = ab.first; root.b = ab.second;
        root.spikes = sp;
        forest.nodes.push_back(std::move(root));
    }
}

} // namespace drift
} // namespace neurosuite

#endif // NEUROSUITE_CORE_DRIFT_PARTITION_HPP
