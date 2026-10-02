// decollide_eap_test.cpp — the Decomp -> .eap bridge (decollide_eap.hpp):
// constituent-unit membership, stable class reuse by provenance, fresh-class
// allocation, pool growth on exhaustion, int8 offset clamping, and a persistence
// round-trip through neurofileio.  Self-contained, assert-based.

#include "neurosuite/core/decollide_eap.hpp"
#include "neurosuite/core/templateclass.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran; if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    namespace nf = neurofileio;
    namespace dc = neurosuite::decollide;
    using neurosuite::templateclass::activeCols;
    using neurosuite::templateclass::members;

    const int64_t N = 20;
    const int     T = 4;
    auto fresh = [&](nf::EapFile& e, nf::TclRegistry& reg) {
        e = nf::EapFile{};
        e.nSpikes = N; e.nClasses = T; e.group = 6;
        e.cells.assign(static_cast<std::size_t>(N) * T, nf::EAP_ABSENT);
        e.ok = true;
        reg = nf::initTcl(T);
    };

    // ── basic: two decomps, four constituent units -> four fresh classes ──────
    {
        nf::EapFile e; nf::TclRegistry reg; fresh(e, reg);
        std::vector<dc::Decomp> decomps = {
            { 5, {/*unit*/23, /*tau*/0, 1.0}, {/*unit*/9,  /*tau*/3,  0.6} },
            { 8, {/*unit*/23, /*tau*/1, 0.9}, {/*unit*/41, /*tau*/-2, 0.5} },
        };
        const dc::EapApply r = dc::applyDecompsToEap(e, reg, decomps, "gt", "2026-10-02");
        check(r.ok, "apply ok");
        check(r.cellsWritten == 4, "4 cells written (2 per decomp)");
        // unit 23 appears in BOTH decomps -> one class, reused (3 distinct units)
        check(r.classesCreated == 3, "3 classes created (unit 23 reused across decomps)");

        const int c23 = dc::eapClassForUnit(reg, 23);
        const int c9  = dc::eapClassForUnit(reg, 9);
        const int c41 = dc::eapClassForUnit(reg, 41);
        check(c23 == 0 && c9 == 1 && c41 == 2, "columns allocated in first-seen order 23,9,41");
        check(reg.entries[0].provenanceClu == 23 && reg.entries[0].provenanceStage == "gt",
              "class 0 provenance = unit 23 / stage gt");
        check(activeCols(reg) == std::vector<int>({0, 1, 2}), "three active classes");

        // membership cells carry the integer taus
        check(e.cells[5 * T + c23] == 0 && e.cells[5 * T + c9]  == 3,  "spike 5: c23@0, c9@+3");
        check(e.cells[8 * T + c23] == 1 && e.cells[8 * T + c41] == -2, "spike 8: c23@+1, c41@-2");
        // unit 23 is present in spikes 5 AND 8 (one stable class across two collisions)
        check(members(e, c23) == std::vector<int64_t>({5, 8}), "unit-23 class spans both spikes");
        check(members(e, c9)  == std::vector<int64_t>({5}),    "unit-9 class is spike 5 only");

        // a spike not in any decomp stays all-absent; .clu-style other classes untouched
        check(!nf::eapPresent(e.cells[0 * T + 0]), "untouched spike row stays absent");
    }

    // ── reuse an EXISTING class (provenance already set), don't allocate ──────
    {
        nf::EapFile e; nf::TclRegistry reg; fresh(e, reg);
        // pre-make class 0 for unit 23 (as a template-create would have)
        neurosuite::templateclass::createClass(e, reg, {1, 2}, 0, "pyr", 23, "gt", "2026-10-01");
        std::vector<dc::Decomp> decomps = { { 7, {23, 2, 1.0}, {99, 0, 0.4} } };
        const dc::EapApply r = dc::applyDecompsToEap(e, reg, decomps, "gt", "2026-10-02");
        check(r.ok && r.classesCreated == 1, "only the NEW unit (99) allocates; 23 reused");
        check(dc::eapClassForUnit(reg, 23) == 0, "unit 23 kept its pre-existing class 0");
        check(e.cells[7 * T + 0] == 2, "decollide wrote unit-23 offset into its existing class");
        check(members(e, 0) == std::vector<int64_t>({1, 2, 7}), "spike 7 joined unit-23's members");
    }

    // ── pool growth: more distinct units than preallocated columns ────────────
    {
        nf::EapFile e; nf::TclRegistry reg;
        e = nf::EapFile{}; e.nSpikes = 5; e.nClasses = 2; e.group = 6;
        e.cells.assign(5u * 2, nf::EAP_ABSENT); e.ok = true;
        reg = nf::initTcl(2);
        std::vector<dc::Decomp> decomps = {
            { 0, {10, 0, 1}, {11, 0, 1} },     // fills cols 0,1 (pool now full)
            { 1, {12, 0, 1}, {13, 0, 1} },     // needs 2 more -> grow
        };
        const dc::EapApply r = dc::applyDecompsToEap(e, reg, decomps, "", "", /*growBy=*/8);
        check(r.ok && r.grows >= 1, "pool grew at least once");
        check(e.nClasses == 10 && reg.nClasses == 10, "widened to 2 + growBy(8) = 10");
        check(r.classesCreated == 4, "all four distinct units got classes");
        check(dc::eapClassForUnit(reg, 12) >= 2 && dc::eapClassForUnit(reg, 13) >= 2,
              "overflow units landed in the grown columns");
    }

    // ── int8 offset clamping (never the ABSENT sentinel) ──────────────────────
    {
        nf::EapFile e; nf::TclRegistry reg; fresh(e, reg);
        std::vector<dc::Decomp> decomps = {
            { 3, {10,  200, 1}, {11, -200, 1} },   // out of int8 range both ways
        };
        dc::applyDecompsToEap(e, reg, decomps, "", "");
        check(e.cells[3 * T + dc::eapClassForUnit(reg, 10)] ==  127, "positive overflow clamps to +127");
        check(e.cells[3 * T + dc::eapClassForUnit(reg, 11)] == -127, "negative clamps to -127, not -128");
        check(dc::eapClampOffset(-128) == -127, "clamp refuses the ABSENT sentinel");
    }

    // ── persistence round-trip ────────────────────────────────────────────────
    {
        nf::EapFile e; nf::TclRegistry reg; fresh(e, reg);
        std::vector<dc::Decomp> decomps = { { 2, {7, 4, 1}, {8, -1, 1} } };
        dc::applyDecompsToEap(e, reg, decomps, "gt", "2026-10-02");
        const std::string ep = "dceap_rt.tmp.eap", tp = "dceap_rt.tmp.tcl";
        check(nf::writeEap(ep, e.nSpikes, e.nClasses, e.group, 0u, e.cells), "writeEap");
        check(nf::writeTcl(tp, reg), "writeTcl");
        nf::EapFile e2 = nf::readEap(ep);
        nf::TclRegistry r2 = nf::readTcl(tp);
        check(e2.ok && e2.cells == e.cells, "eap survives round-trip");
        check(r2.ok && dc::eapClassForUnit(r2, 7) == dc::eapClassForUnit(reg, 7),
              "class<-unit mapping survives round-trip");
        std::remove(ep.c_str()); std::remove(tp.c_str());
    }

    std::printf("%s: %d checks, %d failed\n", (g_fail ? "FAIL" : "OK"), g_ran, g_fail);
    return g_fail ? 1 : 0;
}
