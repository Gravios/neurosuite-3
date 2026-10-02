// projectionscope.hpp — the temporally-restricted projection scope
// (claude/eap-template-class-design §7).  Header-only and Qt-free, like
// templateclass.hpp / decollide_eap.hpp, so the ClusterView gate (phase 6b) is
// thin wiring over a unit-tested core.
//
// A pinned template defines a TEMPORAL COVERAGE: the union of the time windows of
// its `.wti` drift rows (each a chunk's [start,end] seconds).  In temporally-
// restricted mode a spike is "in scope" iff its time falls in that union.  The
// scope is built from the pinned OBLIQUE-BASIS clusters: each pinned cluster maps
// to its template class via `.tcl` provenance (the "same unit -> same column"
// key), and that class's drift windows are its coverage.  The `.wti` id field is
// the class id when fiber-template wrote it with --eap, or the clu id otherwise;
// to serve both, a cluster's candidate ids are {its class (if any), itself}.
#ifndef NEUROSUITE_CORE_PROJECTIONSCOPE_HPP
#define NEUROSUITE_CORE_PROJECTIONSCOPE_HPP

#include "neurosuite/core/neurofileio.h"   // WtiIndex / WtiRow / TclRegistry / TclStatus

#include <algorithm>
#include <string>
#include <vector>

namespace neurosuite {
namespace projectionscope {

// A closed time window [a, b] in absolute session seconds.
struct Interval { double a = 0.0; double b = 0.0; };

// The lowest-id Active class in `reg` whose provenanceClu == `unit`, or -1 if the
// unit has no class (the same reuse key decollide_eap uses).
inline int classForUnit(const neurofileio::TclRegistry& reg, int unit)
{
    int found = -1;
    for (const neurofileio::TclEntry& e : reg.entries)
        if (e.status == neurofileio::TclStatus::Active && e.provenanceClu == unit &&
            (found < 0 || e.col < found))
            found = e.col;
    return found;
}

// Merge a set of windows into sorted, non-overlapping intervals (abutting/
// overlapping windows coalesce).  Degenerate (b < a) inputs are dropped.
inline std::vector<Interval> mergeIntervals(std::vector<Interval> v)
{
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const Interval& i){ return i.b < i.a; }), v.end());
    std::sort(v.begin(), v.end(),
              [](const Interval& x, const Interval& y){ return x.a < y.a; });
    std::vector<Interval> out;
    for (const Interval& i : v) {
        if (!out.empty() && i.a <= out.back().b) {
            if (i.b > out.back().b) out.back().b = i.b;   // extend
        } else {
            out.push_back(i);
        }
    }
    return out;
}

// The scope for `basisClusters`: the union of the `driftLink` `.wti` windows of
// the classes behind those clusters.  Each cluster contributes rows whose unitId
// is its class id (via `.tcl` provenance) OR the cluster id itself (so a non-eap
// `.wti` keyed by clu id still resolves).  Empty-placeholder rows (nSpikes <= 0)
// are not coverage and are skipped.  Returns merged, sorted intervals.
inline std::vector<Interval> scopeIntervals(const neurofileio::WtiIndex& wti,
                                            const neurofileio::TclRegistry& reg,
                                            const std::vector<int>& basisClusters,
                                            const std::string& driftLink = "drift")
{
    std::vector<int> ids;                       // candidate .wti id keys, de-duplicated
    for (int c : basisClusters) {
        const int cls = classForUnit(reg, c);
        if (cls >= 0 && std::find(ids.begin(), ids.end(), cls) == ids.end()) ids.push_back(cls);
        if (std::find(ids.begin(), ids.end(), c) == ids.end()) ids.push_back(c);
    }
    std::vector<Interval> windows;
    for (const neurofileio::WtiRow& r : wti.rows) {
        if (r.link != driftLink || r.nSpikes <= 0) continue;
        if (std::find(ids.begin(), ids.end(), r.unitId) == ids.end()) continue;
        windows.push_back(Interval{r.a, r.b});
    }
    return mergeIntervals(std::move(windows));
}

// True iff `t` falls in any interval of `iv` (assumed sorted, non-overlapping —
// i.e. the mergeIntervals / scopeIntervals output).  Binary search.
inline bool inScope(const std::vector<Interval>& iv, double t)
{
    // first interval with a > t; the candidate is the one before it.
    std::size_t lo = 0, hi = iv.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (iv[mid].a <= t) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return false;
    const Interval& cand = iv[lo - 1];
    return t >= cand.a && t <= cand.b;
}

// Whether the gate is active at all: temporally-restricted mode AND a non-empty
// scope.  (With an empty scope — no pinned basis, or its classes have no drift
// windows — nothing would be in scope, so the gate stays OFF rather than hiding
// every spike.)
inline bool gateActive(bool temporallyRestricted, const std::vector<Interval>& iv)
{
    return temporallyRestricted && !iv.empty();
}

}  // namespace projectionscope
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_PROJECTIONSCOPE_HPP
