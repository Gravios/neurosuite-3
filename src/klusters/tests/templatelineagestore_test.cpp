// templatelineagestore_test.cpp — the Qt-free manual-lineage model
// (TemplateLineageStore).  v2 nodes carry running SUMMARIES (mean/std/count), so
// setRegionSpikes/addLeaf FOLD a selection in (read the .spk records, summarise,
// combine — the weighted update) and never keep spikes.  Covers: fold +
// accumulation, the commit round-trip (.wtl v2 + .mti v2/.mtf = copied means),
// node ops (orphan-on-remove, setParent/setKind/setWindow, nextNodeId), and the
// drift partition — split DUPLICATES a region's summary, merge COMBINES (exact),
// move preserves the mean, plus seed grain + the not-ready guard.
// Links Neurosuite::core.  Self-contained, run via ctest.

#include "templatelineagestore.h"
#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/template_generate.hpp"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}
static bool close(float a, float b, float tol = 1e-2f) { return std::fabs(a - b) <= tol; }

int main()
{
    namespace tg = neurosuite::templategen;
    const std::string base = "linstore.tmp";
    const int group = 5, nsamp = 4, nchan = 2;
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;

    // Clean slate (ctest reuses the build dir between runs).
    for (const char* st : {"refine","part","grain","nores"}) {
        std::remove(tg::sessionPath(base, "wtl", group, "", st).c_str());
        std::remove(tg::sessionPath(base, "mti", group, "", st).c_str());
        std::remove(tg::sessionPath(base, "mtf", group, "standard", st).c_str());
    }
    std::remove(tg::sessionPath(base, "spk", group, "standard", "").c_str());
    std::remove(tg::sessionPath(base, "res", group, "standard", "").c_str());

    // Synthetic .spk.standard.5 : 8 spikes, spike s all-equal to val[s].
    std::vector<int> val = {100,100,200,200, 50, 70, 10, 10};
    std::vector<int16_t> stack;
    for (int v : val) for (std::size_t e = 0; e < recLen; ++e) stack.push_back(static_cast<int16_t>(v));
    check(neurofileio::writeSpk(tg::sessionPath(base, "spk", group, "standard", ""),
                                nsamp, nchan, stack), "wrote synthetic .spk");
    // A .res with sr = 1 (seconds == samples): 8 spikes at 10..90.
    const std::vector<int64_t> times = {10,20,30,40,60,70,80,90};
    check(neurofileio::writeResBinary(tg::sessionPath(base, "res", group, "standard", ""), times),
          "wrote synthetic .res");

    // Summary of a (class,region) drift root in the live forest.
    auto rootSummary = [](const TemplateLineageStore& s, int cls, int region, int64_t* cnt, float* mean0) {
        for (const neurofileio::WtlNode& n : s.forest().nodes)
            if (n.classId == cls && n.kind == "drift-root" && n.parent < 0
                && s.partition().regionOf(0.5 * (n.a + n.b)) == region) {
                *cnt = n.count; *mean0 = n.mean.empty() ? 0.f : n.mean[0]; return;
            }
        *cnt = -1; *mean0 = 0.f;
    };

    // ── A. fold + accumulation + commit (one region) ────────────────────────
    TemplateLineageStore st;
    check(st.load(base, group, "refine", "standard", "", nsamp, nchan, 1.0), "store load ok");
    check(st.partitionReady() && st.partition().nRegions() == 1 && st.tEnd() == 90.0, "one session-spanning region");
    check(st.nodeCount() == 0, "fresh forest empty");

    const int rid = st.setRegionSpikes(31, 0, {0,1});        // fold spikes 0,1 (val 100) -> mean 100, cnt 2
    int64_t c; float m;
    rootSummary(st, 31, 0, &c, &m);
    check(rid >= 0 && c == 2 && close(m, 100.f), "first fold: region root mean 100, count 2");
    st.setRegionSpikes(31, 0, {2,3});                        // fold 2,3 (val 200) -> combine -> mean 150, cnt 4
    rootSummary(st, 31, 0, &c, &m);
    check(c == 4 && close(m, 150.f), "accumulation: two folds -> mean 150, count 4 (weighted)");

    const int l1 = st.addLeaf(31, 0, "adapt-leaf",     {4,5});   // val 50,70 -> mean 60
    const int l2 = st.addLeaf(31, 0, "collision-leaf", {6,7});   // val 10,10 -> mean 10
    check(st.node(l1) && st.node(l1)->count == 2 && close(st.node(l1)->mean[0], 60.f), "adapt leaf mean 60");
    check(st.node(l2) && st.node(l2)->count == 2 && close(st.node(l2)->mean[0], 10.f), "collision leaf mean 10");
    check(st.node(l1)->parent == rid, "leaf parent is the region root");

    std::string wtlPath, mtiPath;
    tg::Result R = st.commit(&wtlPath, &mtiPath);
    check(R.ok && R.rows.size() == 3, "commit ok, 3 rows (root + 2 leaves)");
    check(mtiPath == tg::sessionPath(base, "mti", group, "", "refine"), "model index is .mti");
    neurofileio::WtlForest rl = neurofileio::readWtl(wtlPath);
    check(rl.ok && rl.version == 2 && rl.nodes.size() == 3, ".wtl v2 persisted, 3 nodes");
    neurofileio::SpkFile wf = neurofileio::readSpk(
        tg::sessionPath(base, "mtf", group, "standard", "refine"), nsamp, nchan);
    auto recAll = [&](int r, int16_t want) {
        for (std::size_t e = 0; e < recLen; ++e) if (wf.samples[r*recLen + e] != want) return false; return true; };
    check(wf.ok && wf.nSpikes == 3 && recAll(0,150) && recAll(1,60) && recAll(2,10),
          ".mtf = copied means 150/60/10");

    // ── B. node ops ──────────────────────────────────────────────────────────
    check(st.setKind(l2, "my-kind") && st.node(l2)->kind == "my-kind", "setKind");
    check(st.setParent(l2, -1) && st.node(l2)->parent == -1, "setParent to root");
    check(st.setWindow(rid, 5, 6) && st.node(rid)->a == 5 && st.node(rid)->b == 6, "setWindow");
    check(st.removeNode(rid), "removeNode drift root");
    check(st.nodeCount() == 2 && st.node(rid) == nullptr, "node removed");
    check(st.node(l1) && st.node(l1)->parent == -1, "child orphaned (parent -> -1), not deleted");
    check(!st.removeNode(999), "removing an absent node is a no-op false");
    const int rN = st.addNode(40, "drift-root", -1, 0, 1);
    check(rN == 3 && st.node(rN) && st.node(rN)->count == 0, "addNode: next id (max+1), empty");

    // ── C. drift partition: split DUPLICATES, merge COMBINES ────────────────
    {
        TemplateLineageStore ps;
        check(ps.load(base, group, "part", "standard", "", nsamp, nchan, 1.0), "partition store load ok");
        check(ps.splitAt(50.0) && ps.partition().nRegions() == 2, "split empty partition -> 2 regions");

        // Populate the two regions from EMPTY with distinct values (restricted by time):
        //   region 0 [0,50): spikes 0..3 (val 100,100,200,200) -> mean 150, cnt 4
        //   region 1 [50,90]: spikes 4..7 (val 50,70,10,10)    -> mean 35,  cnt 4
        ps.setRegionSpikes(31, 0, {0,1,2,3,4,5,6,7});
        ps.setRegionSpikes(31, 1, {0,1,2,3,4,5,6,7});
        rootSummary(ps, 31, 0, &c, &m); check(c == 4 && close(m, 150.f), "region 0 mean 150, cnt 4");
        rootSummary(ps, 31, 1, &c, &m); check(c == 4 && close(m, 35.f),  "region 1 mean 35,  cnt 4");

        // addLeaf region-restricts the selection by time.
        const int leaf = ps.addLeaf(31, 1, "adapt-leaf", {0,1,2,3,4,5,6,7});
        check(leaf >= 0 && ps.node(leaf) && ps.node(leaf)->count == 4 && close(ps.node(leaf)->mean[0], 35.f),
              "addLeaf folds only the in-region spikes (region 1 -> mean 35, cnt 4)");

        // MERGE (delete boundary) -> one region = EXACT combine of the two:
        //   mean (150*4 + 35*4)/8 = 92.5, cnt 8.
        check(ps.deleteBoundary(0) && ps.partition().nRegions() == 1, "delete boundary -> 1 region");
        rootSummary(ps, 31, 0, &c, &m);
        check(c == 8 && close(m, 92.5f), "merge: exact weighted combine -> mean 92.5, count 8");

        // SPLIT again -> the merged root overlaps both halves -> DUPLICATED into each.
        check(ps.splitAt(50.0) && ps.partition().nRegions() == 2, "re-split -> 2 regions");
        int64_t c0, c1; float m0, m1;
        rootSummary(ps, 31, 0, &c0, &m0); rootSummary(ps, 31, 1, &c1, &m1);
        check(c0 == 8 && c1 == 8 && close(m0, 92.5f) && close(m1, 92.5f),
              "split duplicates the summary into both halves (non-destructive)");

        // MOVE preserves the mean (counts may inflate under repeated duplicate+combine).
        check(ps.moveBoundary(0, 25.0) && ps.partition().bounds[0] == 25.0, "move boundary to 25");
        rootSummary(ps, 31, 0, &c0, &m0); rootSummary(ps, 31, 1, &c1, &m1);
        check(close(m0, 92.5f) && close(m1, 92.5f), "move keeps the region means");
    }

    // ── D. seed grain + not-ready guard ──────────────────────────────────────
    {
        TemplateLineageStore gs;                  // 90 s session, grain 30 s -> 3 regions
        check(gs.load(base, group, "grain", "standard", "", nsamp, nchan, 1.0, 30.0)
                  && gs.partition().nRegions() == 3, "seed grain 30 s over 90 s -> 3 regions");
        TemplateLineageStore gs2;
        check(gs2.load(base, group, "grain", "standard", "", nsamp, nchan, 1.0, 0.0)
                  && gs2.partition().nRegions() == 1, "seed grain 0 -> one region");

        TemplateLineageStore nr;
        std::remove(tg::sessionPath(base, "res", group, "standard", "").c_str());
        nr.load(base, group, "nores", "standard", "", nsamp, nchan, 1.0);
        check(!nr.partitionReady() && !nr.splitAt(5.0) && !nr.deleteBoundary(0),
              "no .res -> partition not ready, edits are no-ops");
    }

    std::printf("templatelineagestore_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
