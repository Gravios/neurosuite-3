// templatelineagestore_test.cpp — the Qt-free manual-lineage model
// (TemplateLineageStore): node ops (id assignment, orphan-on-remove, setParent/
// setKind), the commit round-trip (persist .wtl + render .wti v2/.wtf), and the
// session drift partition (seed from the .res, split / move / delete a boundary,
// region-restricted setRegionSpikes / addLeaf).  Links Neurosuite::core for the
// neurofileio + renderLineage* / drift engine.  Self-contained, run via ctest.

#include "templatelineagestore.h"
#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/template_generate.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    namespace tg = neurosuite::templategen;
    const std::string base = "linstore.tmp";
    const int group = 5, nsamp = 4, nchan = 2;
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;

    // Start from a clean slate so the test is idempotent across ctest runs (a prior
    // run's committed .wtl would otherwise make the fresh-forest check fail).
    std::remove(tg::sessionPath(base, "spk", group, "standard", "").c_str());
    std::remove(tg::sessionPath(base, "res", group, "standard", "").c_str());
    std::remove(tg::sessionPath(base, "wtl", group, "", "refine").c_str());
    std::remove(tg::sessionPath(base, "wti", group, "", "refine").c_str());
    std::remove(tg::sessionPath(base, "wtf", group, "standard", "refine").c_str());
    std::remove(tg::sessionPath(base, "wtl", group, "", "part").c_str());
    std::remove(tg::sessionPath(base, "wti", group, "", "part").c_str());
    std::remove(tg::sessionPath(base, "wtf", group, "standard", "part").c_str());
    std::remove(tg::sessionPath(base, "wtl", group, "", "grain").c_str());
    std::remove(tg::sessionPath(base, "wti", group, "", "grain").c_str());
    std::remove(tg::sessionPath(base, "wtf", group, "standard", "grain").c_str());

    // Synthetic .spk.standard.5 : 8 spikes, spike s all-equal to val[s].
    std::vector<int> val = {100,100,200,200, 50, 70, 10, 10};
    std::vector<int16_t> stack;
    for (int v : val) for (std::size_t e = 0; e < recLen; ++e) stack.push_back(static_cast<int16_t>(v));
    check(neurofileio::writeSpk(tg::sessionPath(base, "spk", group, "standard", ""),
                                nsamp, nchan, stack), "wrote synthetic .spk");

    TemplateLineageStore st;
    check(st.load(base, group, "refine", "standard", "", nsamp, nchan, 32552.0), "store load ok");
    check(st.nodeCount() == 0, "fresh forest empty");

    // Build class 31's tree: drift-root + adapt-leaf + collision-leaf, plus an
    // empty drift-root (unset region).
    const int r0 = st.addNode(31, "drift-root",     -1, 0,   120, {0,1,2,3});
    const int l1 = st.addNode(31, "adapt-leaf",     r0, 1,   2,   {4,5});
    const int l2 = st.addNode(31, "collision-leaf", r0, 0,   0,   {6,7});
    const int r3 = st.addNode(31, "drift-root",     -1, 120, 240, {});
    check(r0 == 0 && l1 == 1 && l2 == 2 && r3 == 3, "node ids assigned sequentially");
    check(st.nodeCount() == 4, "4 nodes");
    check(st.node(l1) && st.node(l1)->parent == r0, "adapt-leaf parent is the drift root");

    // Commit -> persist .wtl + render library.
    std::string wtlPath, wtiPath;
    tg::Result R = st.commit(&wtlPath, &wtiPath);
    check(R.ok, "commit ok");
    check(R.rows.size() == 4, "4 rows rendered");

    neurofileio::WtlForest rl = neurofileio::readWtl(wtlPath);
    check(rl.ok && rl.nodes.size() == 4, ".wtl persisted with 4 nodes");

    neurofileio::WtiIndex wi = neurofileio::readWti(wtiPath);
    check(wi.ok && wi.version == 2, ".wti written v2 (parents present)");
    check(wi.rows.size()==4 && wi.rows[1].link=="adapt" && wi.rows[1].parent==0, "adapt child row parent==0");
    check(wi.rows.size()==4 && wi.rows[2].link=="collision" && wi.rows[2].parent==0, "collision child row parent==0");
    check(wi.rows.size()==4 && wi.rows[3].nSpikes==0, "empty drift-root placeholder row");

    neurofileio::SpkFile wf = neurofileio::readSpk(
        tg::sessionPath(base, "wtf", group, "standard", "refine"), nsamp, nchan);
    auto recAll = [&](int r, int16_t want) {
        for (std::size_t e = 0; e < recLen; ++e) if (wf.samples[r*recLen + e] != want) return false; return true; };
    check(wf.ok && wf.nSpikes==4 && recAll(0,150) && recAll(1,60) && recAll(2,10) && recAll(3,0),
          ".wtf medians 150/60/10 + 0 placeholder");

    // ── node ops ────────────────────────────────────────────────────────────
    check(st.setKind(l2, "my-kind") && st.node(l2)->kind == "my-kind", "setKind");
    check(st.setParent(l2, -1) && st.node(l2)->parent == -1, "setParent to root");
    check(st.setWindow(r0, 5, 6) && st.node(r0)->a == 5 && st.node(r0)->b == 6, "setWindow");

    // Remove the drift root -> its remaining child (l1) is orphaned, not deleted.
    check(st.removeNode(r0), "removeNode drift root");
    check(st.nodeCount() == 3, "3 nodes after remove");
    check(st.node(r0) == nullptr, "removed node gone");
    check(st.node(l1) && st.node(l1)->parent == -1, "child orphaned (parent -> -1), not deleted");
    check(!st.removeNode(999), "removing an absent node is a no-op false");

    // A new node after removal reuses the next free id (max+1), never a stale id.
    const int rN = st.addNode(40, "drift-root", -1, 0, 1, {0});
    check(rN == 4, "nextNodeId is max+1 even after a removal");

    // ── session drift partition (§9) ─────────────────────────────────────────
    {
        // A .res with sr = 1 (so seconds == samples): 8 spikes at 10..90.
        const std::vector<int64_t> times = {10,20,30,40,60,70,80,90};
        check(neurofileio::writeResBinary(tg::sessionPath(base, "res", group, "standard", ""), times),
              "wrote synthetic .res");

        TemplateLineageStore ps;
        check(ps.load(base, group, "part", "standard", "", nsamp, nchan, 1.0), "partition store load ok");
        check(ps.partitionReady(), "partition ready (res present)");
        check(ps.partition().nRegions() == 1 && ps.tEnd() == 90.0, "seed: one session-spanning region");

        // The (class,region) drift-root's spikes, read from the live forest.
        auto rootSpikes = [&](int cls, int region) -> std::vector<int64_t> {
            for (const neurofileio::WtlNode& n : ps.forest().nodes)
                if (n.classId == cls && n.kind == "drift-root" && n.parent < 0
                    && ps.partition().regionOf(0.5 * (n.a + n.b)) == region) return n.spikes;
            return {};
        };

        check(ps.setRegionSpikes(31, 0, {0,1,2,3,4,5,6,7}) >= 0, "setRegionSpikes region 0");
        check(rootSpikes(31,0) == std::vector<int64_t>({0,1,2,3,4,5,6,7}), "region-0 root holds all 8");

        check(ps.splitAt(50.0) && ps.partition().nRegions() == 2, "split at 50 -> 2 regions");
        check(rootSpikes(31,0) == std::vector<int64_t>({0,1,2,3}), "split: region 0 = early spikes");
        check(rootSpikes(31,1) == std::vector<int64_t>({4,5,6,7}), "split: region 1 = late spikes");

        check(ps.moveBoundary(0, 25.0) && ps.partition().bounds.size()==1 && ps.partition().bounds[0]==25.0,
              "move boundary to 25");
        check(rootSpikes(31,0) == std::vector<int64_t>({0,1}), "move: region 0 = {0,1}");
        check(rootSpikes(31,1) == std::vector<int64_t>({2,3,4,5,6,7}), "move: region 1 = the rest");

        const int leaf = ps.addLeaf(31, 1, "adapt-leaf", {4,5,99});   // 99 out of range -> dropped
        check(leaf >= 0 && ps.node(leaf) && ps.node(leaf)->spikes == std::vector<int64_t>({4,5}),
              "addLeaf region-restricts + drops out-of-range");

        check(ps.deleteBoundary(0) && ps.partition().nRegions() == 1, "delete boundary -> 1 region");
        check(rootSpikes(31,0) == std::vector<int64_t>({0,1,2,3,4,5,6,7}), "merge re-pools all drift spikes");
        // the leaf survived the merge (re-grain reassigns ids); re-find it.
        int leafParent = -2, rootId = -1; std::vector<int64_t> leafSp;
        for (const neurofileio::WtlNode& n : ps.forest().nodes) {
            if (n.classId==31 && n.kind=="drift-root" && n.parent<0) rootId = n.node;
            if (n.classId==31 && n.kind=="adapt-leaf") { leafParent = n.parent; leafSp = n.spikes; }
        }
        check(leafSp == std::vector<int64_t>({4,5}) && leafParent == rootId,
              "leaf kept + re-parented under the merged root");

        // Seed grain (the no-.wti default): a fresh stage with no prior .wtl tiles
        // the session at the grain the GUI passes from the cluster time-restricted
        // mode, rather than a single region.  An existing forest's windows win, so
        // this only applies to the empty-forest branch.
        {
            TemplateLineageStore gs;                  // 90 s session, grain 30 s -> 3 regions
            check(gs.load(base, group, "grain", "standard", "", nsamp, nchan, 1.0, 30.0),
                  "grain store load ok");
            check(gs.partitionReady() && gs.partition().nRegions() == 3 && gs.tEnd() == 90.0,
                  "seed grain 30 s over a 90 s session -> 3 regions");

            TemplateLineageStore gs2;                 // grain >= session -> a single region
            check(gs2.load(base, group, "grain", "standard", "", nsamp, nchan, 1.0, 100.0)
                      && gs2.partition().nRegions() == 1,
                  "seed grain >= session -> one region");

            TemplateLineageStore gs3;                 // grain 0 (session-spanning) -> one region
            check(gs3.load(base, group, "grain", "standard", "", nsamp, nchan, 1.0, 0.0)
                      && gs3.partition().nRegions() == 1,
                  "seed grain 0 -> one region (unchanged default)");
        }

        // Not-ready guard: a store with no .res leaves the partition ops inert.
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
