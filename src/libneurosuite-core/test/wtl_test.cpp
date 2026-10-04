// wtl_test.cpp — round-trips the .wtl manual template-linkage sidecar
// (neurofileio readWtl/writeWtl + the wtl* view-model helpers), the per-node
// lineage the curator hand-builds and the native generator re-medians.
//
// Self-contained (own main, assert-based); built when NS_BUILD_TESTS=ON, run via
// ctest.  Writes scratch files in CWD.
//
// Covers: a two-class forest round-trip (drift roots + adapt/collision leaves,
// non-contiguous and empty spike sets, a verbatim/unknown kind token); the
// version + header rejections; the nNodes declared-count check; a corrupt node
// line (short / long index tail) being skipped without aborting the forest; and
// the class/tree navigation helpers.

#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using namespace neurofileio;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static bool nodeEq(const WtlNode& a, const WtlNode& b) {
    return a.node == b.node && a.classId == b.classId && a.kind == b.kind
        && a.parent == b.parent && a.a == b.a && a.b == b.b && a.spikes == b.spikes;
}

static void writeRaw(const std::string& path, const std::string& body) {
    std::ofstream o(path); o << body; o.close();
}

int main()
{
    // ── 1. Two-class forest round-trip ──────────────────────────────────────
    // class 31: drift root (node 0) with an adapt leaf (1) and a collision leaf (2).
    // class 40: a single drift root (node 3) with an EMPTY spike set (placeholder).
    WtlForest f;
    f.version = 1;
    {
        WtlNode n; n.node=0; n.classId=31; n.kind="drift-root";     n.parent=-1;
        n.a=0.000; n.b=120.000; n.spikes={12,37,59,1024,2048,40000}; f.nodes.push_back(n);
    }
    {
        WtlNode n; n.node=1; n.classId=31; n.kind="adapt-leaf";     n.parent=0;
        n.a=1.0; n.b=2.0; n.spikes={12,59}; f.nodes.push_back(n);
    }
    {
        WtlNode n; n.node=2; n.classId=31; n.kind="collision-leaf"; n.parent=0;
        n.a=0.0; n.b=0.0; n.spikes={88,91,37}; f.nodes.push_back(n);
    }
    {
        WtlNode n; n.node=3; n.classId=40; n.kind="drift-root";     n.parent=-1;
        n.a=0.0; n.b=240.0; n.spikes={};     f.nodes.push_back(n);     // empty placeholder
    }

    const std::string path = "wtl_roundtrip.tmp.wtl";
    check(writeWtl(path, f), "writeWtl ok");

    WtlForest r = readWtl(path);
    check(r.ok, "readWtl ok");
    check(r.version == 1, "version preserved");
    check(r.nodes.size() == 4, "all 4 nodes parsed");
    bool allEq = (r.nodes.size() == f.nodes.size());
    for (std::size_t i = 0; i < r.nodes.size() && i < f.nodes.size(); ++i)
        if (!nodeEq(r.nodes[i], f.nodes[i])) allEq = false;
    check(allEq, "every node field + spike set round-trips exactly");
    check(!r.nodes.empty() && r.nodes[3].spikes.empty(), "empty spike set round-trips as 0 spikes");
    check(r.nodes.size() > 0 && r.nodes[0].spikes.size() == 6, "large non-contiguous spike set intact");

    // ── 2. View-model helpers ────────────────────────────────────────────────
    const std::vector<int> cls = wtlClasses(r);
    check(cls == std::vector<int>({31, 40}), "wtlClasses ascending + unique");

    const std::vector<WtlNode> c31 = wtlClassNodes(r, 31);
    check(c31.size() == 3, "wtlClassNodes(31) returns the 3 class-31 nodes");
    check(c31.size() == 3 && c31[0].node == 0 && c31[1].node == 1 && c31[2].node == 2,
          "wtlClassNodes preserves file order (root before leaves)");
    check(wtlClassNodes(r, 40).size() == 1, "wtlClassNodes(40) returns the lone root");
    check(wtlClassNodes(r, 99).empty(), "wtlClassNodes of an absent class is empty");

    const std::vector<WtlNode> kids = wtlChildren(r, 0);
    check(kids.size() == 2, "node 0 has two children (the adapt + collision leaves)");
    check(kids.size() == 2 && kids[0].node == 1 && kids[1].node == 2, "children in file order");
    check(wtlChildren(r, 1).empty(), "a leaf has no children");
    check(wtlChildren(r, 3).empty(), "the lone root has no children");

    // ── 3. Header / version rejections ───────────────────────────────────────
    writeRaw("wtl_badver.tmp.wtl", "wtl 2\nnNodes 0\n");
    check(!readWtl("wtl_badver.tmp.wtl").ok, "unknown version rejected");

    writeRaw("wtl_nohdr.tmp.wtl", "# a comment\nnNodes 1\nnode 0 1 drift-root -1 0 0 1 5\n");
    check(!readWtl("wtl_nohdr.tmp.wtl").ok, "missing 'wtl <ver>' header rejected");

    check(!readWtl("wtl_does_not_exist.tmp.wtl").ok, "missing file -> ok=false");

    // ── 4. nNodes declared-count check ───────────────────────────────────────
    writeRaw("wtl_countbad.tmp.wtl",
             "wtl 1\nnNodes 2\nnode 0 1 drift-root -1 0 0 1 5\n");   // declares 2, has 1
    check(!readWtl("wtl_countbad.tmp.wtl").ok, "nNodes mismatch rejected");

    // ── 5. Corrupt node line skipped (no nNodes line, so the count check is off) ─
    // First node: a SHORT tail (declares 3 spikes, supplies 2) -> skipped.
    // Second node: a LONG tail (declares 2, supplies 4) -> accepted with the first 2.
    writeRaw("wtl_corrupt.tmp.wtl",
             "wtl 1\n"
             "node 0 7 drift-root -1 0 0 3 10 11\n"            // short -> dropped
             "node 1 7 adapt-leaf 0 1 2 2 20 21 22 23\n");     // long  -> kept, spikes {20,21}
    WtlForest rc = readWtl("wtl_corrupt.tmp.wtl");
    check(rc.ok, "corrupt-tail file still parses (bad node skipped, not fatal)");
    check(rc.nodes.size() == 1, "short-tail node dropped, long-tail node kept");
    check(rc.nodes.size() == 1 && rc.nodes[0].node == 1
          && rc.nodes[0].spikes == std::vector<int64_t>({20, 21}),
          "long tail truncated to the declared nSpikes");

    // ── 6. A verbatim/unknown kind token is preserved ────────────────────────
    writeRaw("wtl_kind.tmp.wtl", "wtl 1\nnNodes 1\nnode 0 3 my-custom-kind -1 0 0 1 99\n");
    WtlForest rk = readWtl("wtl_kind.tmp.wtl");
    check(rk.ok && rk.nodes.size() == 1 && rk.nodes[0].kind == "my-custom-kind",
          "unknown kind token kept verbatim (extensible)");

    std::printf("wtl_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
