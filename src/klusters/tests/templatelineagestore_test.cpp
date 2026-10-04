// templatelineagestore_test.cpp — the Qt-free manual-lineage model
// (TemplateLineageStore): node ops (id assignment, orphan-on-remove, setParent/
// setKind) and the commit round-trip (persist .wtl + render .wti v2/.wtf) on a
// synthetic session.  Links Neurosuite::core for the neurofileio + renderLineage*
// engine.  Self-contained (own main, assert-based); run via ctest.

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
    std::remove(tg::sessionPath(base, "wtl", group, "", "refine").c_str());
    std::remove(tg::sessionPath(base, "wti", group, "", "refine").c_str());
    std::remove(tg::sessionPath(base, "wtf", group, "standard", "refine").c_str());

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

    std::printf("templatelineagestore_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
