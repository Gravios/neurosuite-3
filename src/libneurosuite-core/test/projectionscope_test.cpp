// projectionscope_test.cpp — the temporally-restricted projection scope
// (projectionscope.hpp): cluster->class resolution via .tcl provenance, the
// union of a class's .wti drift windows, interval merging, the in-scope test,
// and the gate-active rule.  Header-only; builds WtiIndex/TclRegistry structs
// directly, so no link.  Self-contained, assert-based.

#include "neurosuite/core/projectionscope.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran; if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    namespace nf = neurofileio;
    namespace ps = neurosuite::projectionscope;

    // .tcl: classes 0,1,2 are units 23,9,41 (cluster 99 has NO class).
    nf::TclRegistry reg;
    reg.ok = true; reg.nClasses = 3;
    auto mk = [](int col, int clu) {
        nf::TclEntry e; e.col = col; e.status = nf::TclStatus::Active; e.provenanceClu = clu; return e;
    };
    reg.entries = { mk(0, 23), mk(1, 9), mk(2, 41) };

    // .wti: class 0 two abutting drift chunks; class 1 one chunk; an adapt row
    // (ignored); class 2 a 0-spike placeholder (skipped); a direct clu-id row (99).
    nf::WtiIndex wti; wti.ok = true;
    auto row = [](int id, const char* link, double a, double b, int64_t n) {
        nf::WtiRow r; r.unitId = id; r.link = link; r.a = a; r.b = b; r.nSpikes = n; return r;
    };
    wti.rows = {
        row(0, "drift", 0,    720,  100),
        row(0, "drift", 720,  1440, 50),
        row(1, "drift", 1440, 2160, 80),
        row(0, "adapt", 0,    5,    100),   // adapt link -> not scope
        row(2, "drift", 3000, 3720, 0),     // placeholder (nSpikes 0) -> skipped
        row(99,"drift", 5000, 5720, 10),    // keyed by a bare clu id (non-eap .wti)
    };

    // ── class 0 alone: two abutting chunks coalesce ───────────────────────────
    {
        const auto iv = ps::scopeIntervals(wti, reg, {23});
        check(iv.size() == 1 && iv[0].a == 0 && iv[0].b == 1440, "unit23 -> [0,1440] (abutting merge)");
        check(ps::inScope(iv, 700) && ps::inScope(iv, 0) && ps::inScope(iv, 1440), "in-scope incl. boundaries");
        check(!ps::inScope(iv, 1441) && !ps::inScope(iv, -1), "out-of-scope past/before");
        check(ps::gateActive(true, iv) && !ps::gateActive(false, iv), "gate on only in restricted mode");
    }

    // ── two pinned classes whose coverage abuts -> one interval ───────────────
    {
        const auto iv = ps::scopeIntervals(wti, reg, {23, 9});
        check(iv.size() == 1 && iv[0].a == 0 && iv[0].b == 2160, "units 23+9 -> [0,2160]");
        check(ps::inScope(iv, 2000) && !ps::inScope(iv, 2200), "spans both classes' chunks");
    }

    // ── a class with only an empty placeholder -> no scope -> gate OFF ────────
    {
        const auto iv = ps::scopeIntervals(wti, reg, {41});
        check(iv.empty(), "unit41 has only a 0-spike placeholder -> empty scope");
        check(!ps::gateActive(true, iv), "empty scope never hides everything (gate off)");
    }

    // ── direct clu-id fallback (non-eap .wti keyed by the cluster id) ─────────
    {
        const auto iv = ps::scopeIntervals(wti, reg, {99});
        check(iv.size() == 1 && iv[0].a == 5000 && iv[0].b == 5720,
              "cluster 99 (no class) resolves via its own id");
    }

    // ── interval merge: overlap coalesces, gap stays; in-scope across both ────
    {
        const auto iv = ps::mergeIntervals({{0,10},{5,20},{30,40}});
        check(iv.size() == 2 && iv[0].a == 0 && iv[0].b == 20 && iv[1].a == 30 && iv[1].b == 40,
              "overlap->[0,20], gap keeps [30,40]");
        check(ps::inScope(iv, 15) && !ps::inScope(iv, 25) && ps::inScope(iv, 30) && !ps::inScope(iv, 41),
              "in-scope across a merged set with a gap");
    }

    std::printf("%s: %d checks, %d failed\n", (g_fail ? "FAIL" : "OK"), g_ran, g_fail);
    return g_fail ? 1 : 0;
}
