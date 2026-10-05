// wtl_test.cpp — round-trips the .wtl manual template-linkage sidecar
// (neurofileio readWtl/writeWtl + the wtl* view-model helpers).  v2 stores a
// per-node running SUMMARY (mean/std/count), not spikes.
//
// Self-contained (own main, assert-based); built when NS_BUILD_TESTS=ON, run via
// ctest.  Writes scratch files in CWD.
//
// Covers: a two-class v2 forest round-trip (drift roots + adapt/collision leaves,
// a populated node's mean/std, an empty placeholder, a verbatim/unknown kind
// token, the nSamples/nChannels header); v1 (spike-list) back-compat mapping to
// count + empty summary; version + header rejections; the nNodes check; a corrupt
// node line skipped; and the class/tree navigation helpers.

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

static bool vecEq(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;   // writer precision is fine here
    return true;
}
static bool nodeEq(const WtlNode& a, const WtlNode& b) {
    return a.node == b.node && a.classId == b.classId && a.kind == b.kind
        && a.parent == b.parent && a.a == b.a && a.b == b.b && a.count == b.count
        && vecEq(a.mean, b.mean) && vecEq(a.std, b.std);
}
static void writeRaw(const std::string& path, const std::string& body) {
    std::ofstream o(path); o << body; o.close();
}

int main()
{
    const int nS = 2, nC = 2;              // recLen = 4

    // ── 1. Two-class v2 forest round-trip ───────────────────────────────────
    // class 31: drift root (0) with an adapt leaf (1) and a collision leaf (2).
    // class 40: a single EMPTY placeholder drift root (3).
    WtlForest f;
    f.version = 2; f.nSamples = nS; f.nChannels = nC;
    {
        WtlNode n; n.node=0; n.classId=31; n.kind="drift-root";     n.parent=-1;
        n.a=0.0; n.b=120.0; n.count=540; n.mean={10.f,20.f,30.f,40.f}; n.std={1.f,2.f,3.f,4.f};
        f.nodes.push_back(n);
    }
    {
        WtlNode n; n.node=1; n.classId=31; n.kind="adapt-leaf";     n.parent=0;
        n.a=1.0; n.b=2.0; n.count=210; n.mean={5.f,6.f,7.f,8.f}; n.std={0.5f,0.5f,0.5f,0.5f};
        f.nodes.push_back(n);
    }
    {
        WtlNode n; n.node=2; n.classId=31; n.kind="collision-leaf"; n.parent=0;
        n.a=0.0; n.b=0.0; n.count=8; n.mean={-3.f,-2.f,-1.f,0.f}; n.std={2.f,2.f,2.f,2.f};
        f.nodes.push_back(n);
    }
    {
        WtlNode n; n.node=3; n.classId=40; n.kind="drift-root";     n.parent=-1;
        n.a=0.0; n.b=240.0; n.count=0;     // empty placeholder: mean/std stay empty
        f.nodes.push_back(n);
    }

    const std::string path = "wtl_roundtrip.tmp.wtl";
    check(writeWtl(path, f), "writeWtl ok");

    WtlForest r = readWtl(path);
    check(r.ok, "readWtl ok");
    check(r.version == 2, "writer always emits v2");
    check(r.nSamples == nS && r.nChannels == nC, "geometry header round-trips");
    check(r.nodes.size() == 4, "all 4 nodes parsed");
    bool allEq = (r.nodes.size() == f.nodes.size());
    for (std::size_t i = 0; i < r.nodes.size() && i < f.nodes.size(); ++i)
        if (!nodeEq(r.nodes[i], f.nodes[i])) allEq = false;
    check(allEq, "every node field + mean/std/count round-trips exactly");
    check(!r.nodes.empty() && r.nodes[3].count == 0 && r.nodes[3].mean.empty(),
          "empty placeholder round-trips as count 0 / no summary");
    check(r.nodes.size() > 0 && r.nodes[0].mean.size() == 4 && r.nodes[0].mean[2] == 30.f,
          "populated mean vector intact");

    // ── 2. View-model helpers ────────────────────────────────────────────────
    check(wtlClasses(r) == std::vector<int>({31, 40}), "wtlClasses ascending + unique");
    const std::vector<WtlNode> c31 = wtlClassNodes(r, 31);
    check(c31.size() == 3 && c31[0].node == 0 && c31[1].node == 1 && c31[2].node == 2,
          "wtlClassNodes preserves file order (root before leaves)");
    check(wtlClassNodes(r, 99).empty(), "wtlClassNodes of an absent class is empty");
    const std::vector<WtlNode> kids = wtlChildren(r, 0);
    check(kids.size() == 2 && kids[0].node == 1 && kids[1].node == 2,
          "node 0's two children in file order");
    check(wtlChildren(r, 1).empty(), "a leaf has no children");

    // ── 3. v1 (spike-list) back-compat: count = nSpikes, empty summary ───────
    writeRaw("wtl_v1.tmp.wtl",
             "wtl 1\n"
             "nNodes 2\n"
             "node 0 31 drift-root -1 0 120 3 12 37 59\n"       // 3 spikes
             "node 1 31 adapt-leaf 0 1 2 0\n");                  // 0 spikes
    WtlForest rv1 = readWtl("wtl_v1.tmp.wtl");
    check(rv1.ok && rv1.version == 1 && rv1.nodes.size() == 2, "v1 file still reads");
    check(rv1.nodes.size() == 2 && rv1.nodes[0].count == 3 && rv1.nodes[0].mean.empty(),
          "v1 spike count maps to count, summary empty (repopulate to fill)");

    // ── 4. Header / version rejections ───────────────────────────────────────
    writeRaw("wtl_badver.tmp.wtl", "wtl 3\nnNodes 0\n");
    check(!readWtl("wtl_badver.tmp.wtl").ok, "unknown version (3) rejected");
    writeRaw("wtl_nohdr.tmp.wtl", "# a comment\nnNodes 1\nnode 0 1 drift-root -1 0 0 1\n");
    check(!readWtl("wtl_nohdr.tmp.wtl").ok, "missing 'wtl <ver>' header rejected");
    check(!readWtl("wtl_does_not_exist.tmp.wtl").ok, "missing file -> ok=false");

    // ── 5. nNodes declared-count check ───────────────────────────────────────
    writeRaw("wtl_countbad.tmp.wtl",
             "wtl 2\nnNodes 2\nnode 0 1 drift-root -1 0 0 0\nmean 0\nstd 0\n");  // declares 2, has 1
    check(!readWtl("wtl_countbad.tmp.wtl").ok, "nNodes mismatch rejected");

    // ── 6. Corrupt node line skipped (no nNodes, so the count check is off) ──
    writeRaw("wtl_corrupt.tmp.wtl",
             "wtl 2\n"
             "node 0 7 drift-root -1 0 0\n"           // missing the count field -> skipped
             "node 1 7 adapt-leaf 0 1 2 5\n"          // valid node line, count 5
             "mean 4 1 2 3 4\nstd 4 0 0 0 0\n");
    WtlForest rc = readWtl("wtl_corrupt.tmp.wtl");
    check(rc.ok && rc.nodes.size() == 1 && rc.nodes[0].node == 1 && rc.nodes[0].count == 5,
          "malformed node line skipped, valid node kept with its summary");
    check(rc.nodes.size() == 1 && rc.nodes[0].mean == std::vector<float>({1,2,3,4}),
          "the valid node's mean line attaches to it");

    // ── 7. A verbatim/unknown kind token is preserved ────────────────────────
    writeRaw("wtl_kind.tmp.wtl", "wtl 2\nnNodes 1\nnode 0 3 my-custom-kind -1 0 0 0\nmean 0\nstd 0\n");
    WtlForest rk = readWtl("wtl_kind.tmp.wtl");
    check(rk.ok && rk.nodes.size() == 1 && rk.nodes[0].kind == "my-custom-kind",
          "unknown kind token kept verbatim (extensible)");

    std::printf("wtl_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
