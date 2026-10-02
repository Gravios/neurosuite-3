// templateclass_test.cpp — the template-class CRUD engine (templateclass.hpp)
// over the in-memory .eap matrix + .tcl registry: allocate/create, membership,
// merge (survivor-keeps-offset), delete (tombstone), pool exhaustion, and a
// persistence round-trip through neurofileio.  Self-contained, assert-based.

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
    using namespace neurosuite::templateclass;
    namespace nf = neurofileio;

    const int64_t N = 10;
    const int     T = 4;
    auto fresh = [&](nf::EapFile& e, nf::TclRegistry& reg) {
        e = nf::EapFile{};
        e.nSpikes = N; e.nClasses = T; e.group = 6;
        e.cells.assign(static_cast<std::size_t>(N) * T, nf::EAP_ABSENT);
        e.ok = true;
        reg = nf::initTcl(T);
    };

    nf::EapFile e; nf::TclRegistry reg; fresh(e, reg);

    // ── create ───────────────────────────────────────────────────────────────
    check(firstFree(reg) == 0, "firstFree starts at 0");
    const int c0 = createClass(e, reg, {1, 2, 3}, 0, "u-a", 23, "gt", "2026-10-02");
    const int c1 = createClass(e, reg, {4, 5}, 2, "u-b", 9, "gt", "2026-10-02");
    check(c0 == 0 && c1 == 1, "createClass takes free columns 0 then 1");
    check(reg.entries[0].status == nf::TclStatus::Active && reg.entries[0].label == "u-a"
          && reg.entries[0].provenanceClu == 23, "class 0 active with provenance");
    check(members(e, 0) == std::vector<int64_t>({1, 2, 3}), "class 0 members");
    check(members(e, 1) == std::vector<int64_t>({4, 5}), "class 1 members");
    check(e.cells[4 * T + 1] == 2, "class 1 offset stored (spike 4, col 1 -> 2)");
    check(activeCols(reg) == std::vector<int>({0, 1}), "activeCols {0,1}");
    check(firstFree(reg) == 2, "firstFree advances to 2");

    // ── merge: survivor keeps its offset on a conflict ───────────────────────
    // Make spike 4 a member of BOTH 0 (offset 0) and 1 (offset 2).  Merging 1->0
    // must keep 0's offset (0) at spike 4, and adopt 1's offset at spike 5 (where
    // 0 was absent).
    e.cells[4 * T + 0] = 0;                        // spike 4 now in class 0 too (conflict)
    check(mergeClasses(e, reg, /*survivor=*/0, /*victim=*/1), "mergeClasses ok");
    check(reg.entries[1].status == nf::TclStatus::Merged && reg.entries[1].mergedInto == 0,
          "victim marked merged:0");
    check(members(e, 1).empty(), "victim column cleared");
    check(members(e, 0) == std::vector<int64_t>({1, 2, 3, 4, 5}), "survivor gained victim's members");
    check(e.cells[4 * T + 0] == 0, "conflict: survivor keeps its offset (0) at spike 4");
    check(e.cells[5 * T + 0] == 2, "adopt: survivor takes victim's offset (2) at spike 5");
    check(activeCols(reg) == std::vector<int>({0}), "merge leaves only class 0 active");
    // a merged (non-active) victim cannot be merged or deleted again
    check(!mergeClasses(e, reg, 0, 1), "merge rejects a non-active victim");
    check(!deleteClass(e, reg, 1), "delete rejects a non-active class");

    // ── delete: tombstone + clear, id never reused ───────────────────────────
    const int c2 = createClass(e, reg, {7, 8}, 0, "u-c", 5, "gt", "2026-10-02");
    check(c2 == 2, "next create takes column 2 (1 is merged, not free)");
    check(deleteClass(e, reg, 0), "deleteClass(0) ok");
    check(reg.entries[0].status == nf::TclStatus::Tomb && members(e, 0).empty(),
          "class 0 tombstoned + column cleared");
    check(firstFree(reg) == 3, "freed ids are not reused: next free is 3, not 0 or 1");

    // ── pool exhaustion ──────────────────────────────────────────────────────
    const int c3 = createClass(e, reg, {9}, 0, "u-d", -1, "", "");
    check(c3 == 3, "column 3 allocated");
    check(firstFree(reg) == -1, "pool exhausted");
    check(createClass(e, reg, {0}, 0, "overflow", -1, "", "") == -1, "createClass returns -1 when full");

    // ── persistence round-trip through neurofileio ───────────────────────────
    {
        const std::string ep = "tcl_rt.tmp.eap", tp = "tcl_rt.tmp.tcl";
        check(nf::writeEap(ep, e.nSpikes, e.nClasses, e.group, 0u, e.cells), "writeEap ok");
        check(nf::writeTcl(tp, reg), "writeTcl ok");
        nf::EapFile e2 = nf::readEap(ep);
        nf::TclRegistry r2 = nf::readTcl(tp);
        check(e2.ok && e2.cells == e.cells, "eap survives round-trip");
        check(r2.ok && activeCols(r2) == std::vector<int>({2, 3}), "tcl active set survives (2,3)");
        check(r2.entries[1].status == nf::TclStatus::Merged && r2.entries[1].mergedInto == 0,
              "merged:0 survives round-trip");
        check(members(e2, 2) == std::vector<int64_t>({7, 8}), "class 2 members survive");
        std::remove(ep.c_str()); std::remove(tp.c_str());
    }

    std::printf("%s: %d checks, %d failed\n", (g_fail ? "FAIL" : "OK"), g_ran, g_fail);
    return g_fail ? 1 : 0;
}
