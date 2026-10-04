// drift_partition_test.cpp — the session-wide drift partition + forest re-grain
// (drift_partition.hpp): tiling / regionOf, split & delete (merge) a partition,
// and regrainForest re-binning pooled drift spikes with leaf re-parenting and
// empty placeholders.  Header-only (neurofileio structs only), no link.

#include "neurosuite/core/drift_partition.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace neurosuite::drift;

static int tests = 0, fails = 0;
#define CHECK(cond, msg) do { ++tests; if(!(cond)){ ++fails; \
    std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);} } while(0)

static neurofileio::WtlNode mk(int node, int cls, const char* kind, int parent,
                               double a, double b, std::vector<int64_t> spikes)
{
    neurofileio::WtlNode n; n.node=node; n.classId=cls; n.kind=kind; n.parent=parent;
    n.a=a; n.b=b; n.spikes=std::move(spikes); return n;
}

int main()
{
    // ── Partition basics ────────────────────────────────────────────────────
    {
        Partition u = uniformPartition(90.0, 3);
        CHECK(u.nRegions() == 3, "uniform 3 regions");
        CHECK(u.bounds.size() == 2, "uniform 2 interior bounds");
        CHECK(u.valid(), "uniform valid");
        CHECK(u.regionOf(10) == 0 && u.regionOf(45) == 1 && u.regionOf(80) == 2, "regionOf buckets");
        CHECK(u.regionOf(-5) == 0 && u.regionOf(1000) == 2, "regionOf clamps");
        CHECK(uniformPartition(100.0, 1).nRegions() == 1, "k=1 single region");
    }
    {
        // Windows [(0,40),(40,100)] -> one interior edge at 40, tEnd 100.
        Partition p = partitionFromWindows({{0,40},{40,100}});
        CHECK(p.tEnd == 100.0 && p.bounds.size() == 1 && p.bounds[0] == 40.0, "partitionFromWindows edges");
        CHECK(p.regionOf(20) == 0 && p.regionOf(70) == 1, "recovered partition buckets");
    }
    {
        Partition p; p.tEnd = 100.0; p.bounds = {50.0};
        CHECK(splitRegion(p, 0, 15.0), "split region 0 at 15 ok");
        CHECK(p.bounds.size() == 2 && p.bounds[0] == 15.0 && p.bounds[1] == 50.0, "boundary inserted, sorted");
        CHECK(!splitRegion(p, 0, 15.0), "re-split at an existing boundary -> no-op (not strictly inside)");
        CHECK(!splitRegion(p, 1, 999.0), "split with t outside the region -> no-op");
        Partition q; q.tEnd = 100.0; q.bounds = {30.0, 60.0};
        CHECK(mergeRegion(q, 0) && q.bounds.size() == 1 && q.bounds[0] == 60.0, "delete boundary 0 merges regions 0,1");
        CHECK(!mergeRegion(q, 5), "merge invalid index -> no-op");
    }

    // ── regrainForest ─────────────────────────────────────────────────────────
    // class 31: drift-root [0,50]{0,1}, drift-root [50,100]{2,3}, adapt-leaf{0} under root0.
    // times (sr=1): spike 0@10 1@20 2@60 3@70.
    neurofileio::WtlForest f; f.version = 1;
    f.nodes.push_back(mk(0, 31, "drift-root",  -1, 0,  50,  {0,1}));
    f.nodes.push_back(mk(1, 31, "drift-root",  -1, 50, 100, {2,3}));
    f.nodes.push_back(mk(2, 31, "adapt-leaf",   0, 1,  2,   {0}));
    const std::vector<int64_t> times = {10,20,60,70};
    const double sr = 1.0;

    // (a) re-grain onto the SAME 2-region partition → roots {0,1} / {2,3}, leaf under r0.
    {
        Partition p; p.tEnd = 100.0; p.bounds = {50.0};
        neurofileio::WtlForest g = regrainForest(f, times, sr, p);
        CHECK(g.nodes.size() == 3, "same-grain: 2 roots + 1 leaf");
        CHECK(g.nodes.size()==3 && g.nodes[0].spikes == std::vector<int64_t>({0,1}), "r0 spikes {0,1}");
        CHECK(g.nodes.size()==3 && g.nodes[1].spikes == std::vector<int64_t>({2,3}), "r1 spikes {2,3}");
        CHECK(g.nodes.size()==3 && g.nodes[2].kind=="adapt-leaf" && g.nodes[2].parent==g.nodes[0].node,
              "leaf re-parented under region-0 root (median time 10)");
        CHECK(g.nodes.size()==3 && g.nodes[0].a==0 && g.nodes[0].b==50 && g.nodes[1].a==50 && g.nodes[1].b==100,
              "root windows match regions");
    }

    // (b) SPLIT region 0 at t=15 → 3 regions [0,15)[15,50)[50,100): r0{0} r1{1} r2{2,3}.
    {
        Partition p; p.tEnd = 100.0; p.bounds = {50.0};
        CHECK(splitRegion(p, 0, 15.0), "split region 0");
        neurofileio::WtlForest g = regrainForest(f, times, sr, p);
        CHECK(g.nodes.size() == 4, "split: 3 roots + 1 leaf");
        CHECK(g.nodes.size()==4 && g.nodes[0].spikes==std::vector<int64_t>({0}), "r0 {0}");
        CHECK(g.nodes.size()==4 && g.nodes[1].spikes==std::vector<int64_t>({1}), "r1 {1}");
        CHECK(g.nodes.size()==4 && g.nodes[2].spikes==std::vector<int64_t>({2,3}), "r2 {2,3}");
        CHECK(g.nodes.size()==4 && g.nodes[3].parent==g.nodes[0].node, "leaf under region-0 root after split");
    }

    // (c) DELETE the boundary (merge) → 1 region: r0 {0,1,2,3}, leaf under it.
    {
        Partition p; p.tEnd = 100.0; p.bounds = {50.0};
        CHECK(mergeRegion(p, 0) && p.nRegions() == 1, "merge to 1 region");
        neurofileio::WtlForest g = regrainForest(f, times, sr, p);
        CHECK(g.nodes.size() == 2, "merge: 1 root + 1 leaf");
        CHECK(g.nodes.size()==2 && g.nodes[0].spikes==std::vector<int64_t>({0,1,2,3}), "merged root pools all spikes");
        CHECK(g.nodes.size()==2 && g.nodes[1].parent==g.nodes[0].node, "leaf under the single root");
    }

    // (d) Empty placeholder: a region past all spikes stays a 0-spike root.
    {
        Partition p; p.tEnd = 100.0; p.bounds = {80.0};   // spikes all < 80 -> region1 empty
        neurofileio::WtlForest g = regrainForest(f, times, sr, p);
        CHECK(g.nodes.size()==3 && g.nodes[0].spikes.size()==4 && g.nodes[1].spikes.empty(),
              "empty region -> 0-spike placeholder root (tiling kept)");
        CHECK(g.nodes.size()==3 && g.nodes[1].kind=="drift-root" && g.nodes[1].a==80.0 && g.nodes[1].b==100.0,
              "placeholder is a drift-root spanning the empty region");
    }

    // (e) Out-of-range spike index is dropped on re-grain.
    {
        neurofileio::WtlForest bad = f;
        bad.nodes[0].spikes.push_back(999);              // no such spike in `times`
        Partition p; p.tEnd = 100.0; p.bounds = {50.0};
        neurofileio::WtlForest g = regrainForest(bad, times, sr, p);
        CHECK(g.nodes.size()>=1 && g.nodes[0].spikes==std::vector<int64_t>({0,1}), "out-of-range spike dropped");
    }

    // (f) tileClassDrift: fresh 2-region tiling of a flat spike set.
    {
        Partition p; p.tEnd = 100.0; p.bounds = {50.0};
        neurofileio::WtlForest g; g.version = 1;
        tileClassDrift(g, 7, {0,1,2,3}, times, sr, p);
        CHECK(g.nodes.size()==2 && g.nodes[0].spikes==std::vector<int64_t>({0,1})
              && g.nodes[1].spikes==std::vector<int64_t>({2,3}), "tileClassDrift bins by region");
        CHECK(g.nodes.size()==2 && g.nodes[0].classId==7 && g.nodes[0].kind=="drift-root", "tiled class + kind");
    }

    std::printf("drift_partition_test: %d checks, %d failures%s\n",
                tests, fails, fails ? " — FAILURES" : "");
    std::printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return fails ? 1 : 0;
}
