// drift_partition_test.cpp — the session-wide drift partition + forest re-grain
// (drift_partition.hpp): tiling / regionOf, split & delete (merge) a partition,
// and regrainForest COMBINING per-region running summaries by window overlap
// (exact merge, duplicate-on-split, empty where nothing overlaps) with leaf
// re-parenting.  Header-only, no link.

#include "neurosuite/core/drift_partition.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

using namespace neurosuite::drift;

static int tests = 0, fails = 0;
#define CHECK(cond, msg) do { ++tests; if(!(cond)){ ++fails; \
    std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);} } while(0)

// A node with a single-element running summary (recLen 1): mean {m}, std {sd}.
static neurofileio::WtlNode mk(int node, int cls, const char* kind, int parent,
                               double a, double b, int64_t count, float m, float sd = 0.f)
{
    neurofileio::WtlNode n; n.node=node; n.classId=cls; n.kind=kind; n.parent=parent;
    n.a=a; n.b=b; n.count=count;
    if (count > 0) { n.mean={m}; n.std={sd}; }
    return n;
}
static bool close(float a, float b, float tol = 1e-3f) { return std::fabs(a-b) <= tol; }

int main()
{
    // ── Partition basics (unchanged by the summary model) ───────────────────
    {
        Partition u = uniformPartition(90.0, 3);
        CHECK(u.nRegions() == 3 && u.bounds.size() == 2 && u.valid(), "uniform 3 regions");
        CHECK(u.regionOf(10)==0 && u.regionOf(45)==1 && u.regionOf(80)==2, "regionOf buckets");
        CHECK(u.regionOf(-5)==0 && u.regionOf(1000)==2, "regionOf clamps");
        CHECK(uniformPartition(100.0, 1).nRegions() == 1, "k=1 single region");
    }
    {
        Partition p = partitionFromWindows({{0,40},{40,100}});
        CHECK(p.tEnd==100.0 && p.bounds.size()==1 && p.bounds[0]==40.0, "partitionFromWindows edges");
        CHECK(p.regionOf(20)==0 && p.regionOf(70)==1, "recovered partition buckets");
    }
    {
        Partition p; p.tEnd=100.0; p.bounds={50.0};
        CHECK(splitRegion(p,0,15.0) && p.bounds.size()==2 && p.bounds[0]==15.0 && p.bounds[1]==50.0, "split sorted");
        CHECK(!splitRegion(p,0,15.0), "re-split at an existing boundary -> no-op");
        Partition q; q.tEnd=100.0; q.bounds={30.0,60.0};
        CHECK(mergeRegion(q,0) && q.bounds.size()==1 && q.bounds[0]==60.0, "delete boundary 0 merges 0,1");
        CHECK(!mergeRegion(q,5), "merge invalid index -> no-op");
    }

    // ── regrainForest — summaries combined by window overlap ────────────────
    // class 31: root0 [0,50] (count2 mean10), root1 [50,100] (count2 mean20),
    //           adapt-leaf (count1 mean5) under root0.
    neurofileio::WtlForest f; f.version=2; f.nSamples=1; f.nChannels=1;
    f.nodes.push_back(mk(0, 31, "drift-root", -1, 0,  50,  2, 10.f));
    f.nodes.push_back(mk(1, 31, "drift-root", -1, 50, 100, 2, 20.f));
    f.nodes.push_back(mk(2, 31, "adapt-leaf",  0, 1,  2,   1, 5.f));

    // (a) SAME 2-region partition → each new region overlaps exactly one old root.
    {
        Partition p; p.tEnd=100.0; p.bounds={50.0};
        neurofileio::WtlForest g = regrainForest(f, p);
        CHECK(g.nodes.size()==3, "same-grain: 2 roots + 1 leaf");
        CHECK(g.nodes.size()==3 && g.nodes[0].count==2 && close(g.nodes[0].mean[0],10.f), "r0 = root0 (count2 mean10)");
        CHECK(g.nodes.size()==3 && g.nodes[1].count==2 && close(g.nodes[1].mean[0],20.f), "r1 = root1 (count2 mean20)");
        CHECK(g.nodes.size()==3 && g.nodes[2].kind=="adapt-leaf" && g.nodes[2].parent==g.nodes[0].node, "leaf under region-0 root (its old parent)");
        CHECK(g.nodes.size()==3 && g.nodes[2].count==1 && close(g.nodes[2].mean[0],5.f), "leaf keeps its own summary");
    }

    // (b) SPLIT region 0 at 15 → root0 overlaps BOTH halves → DUPLICATED into each.
    {
        Partition p; p.tEnd=100.0; p.bounds={50.0};
        CHECK(splitRegion(p,0,15.0), "split region 0");
        neurofileio::WtlForest g = regrainForest(f, p);
        CHECK(g.nodes.size()==4, "split: 3 roots + 1 leaf");
        CHECK(g.nodes.size()==4 && close(g.nodes[0].mean[0],10.f) && close(g.nodes[1].mean[0],10.f),
              "root0 duplicated into both split halves (non-destructive)");
        CHECK(g.nodes.size()==4 && close(g.nodes[2].mean[0],20.f), "far region = root1");
        CHECK(g.nodes.size()==4 && g.nodes[3].parent==g.nodes[1].node, "leaf follows old parent to its max-overlap region [15,50)");
    }

    // (c) MERGE → one region overlaps both old roots → EXACT weighted combine.
    {
        Partition p; p.tEnd=100.0; p.bounds={50.0};
        CHECK(mergeRegion(p,0) && p.nRegions()==1, "merge to 1 region");
        neurofileio::WtlForest g = regrainForest(f, p);
        CHECK(g.nodes.size()==2, "merge: 1 root + 1 leaf");
        // count4; mean (10*2+20*2)/4 = 15; pop var = 10^2*2*2/4/... = 25 -> std 5.
        CHECK(g.nodes.size()==2 && g.nodes[0].count==4 && close(g.nodes[0].mean[0],15.f) && close(g.nodes[0].std[0],5.f),
              "merged root = exact combine (count4 mean15 std5)");
        CHECK(g.nodes.size()==2 && g.nodes[1].parent==g.nodes[0].node, "leaf under the single root");
    }

    // (d) A region that NO old root overlaps is an empty placeholder.
    {
        neurofileio::WtlForest one; one.version=2; one.nSamples=1; one.nChannels=1;
        one.nodes.push_back(mk(0, 9, "drift-root", -1, 0, 50, 2, 7.f));   // only covers [0,50]
        Partition p; p.tEnd=100.0; p.bounds={50.0};                       // r1=[50,100) has no overlap
        neurofileio::WtlForest g = regrainForest(one, p);
        CHECK(g.nodes.size()==2 && g.nodes[0].count==2 && g.nodes[1].count==0,
              "region with no overlapping old root -> empty placeholder");
        CHECK(g.nodes.size()==2 && g.nodes[1].kind=="drift-root" && g.nodes[1].a==50.0 && g.nodes[1].b==100.0,
              "placeholder is a drift-root spanning the empty region");
    }

    // (e) tileClassDrift: fresh 2-region tiling = empty placeholders (no spikes).
    {
        Partition p; p.tEnd=100.0; p.bounds={50.0};
        neurofileio::WtlForest g; g.version=2; g.nSamples=1; g.nChannels=1;
        tileClassDrift(g, 7, p);
        CHECK(g.nodes.size()==2 && g.nodes[0].count==0 && g.nodes[1].count==0, "tileClassDrift lays empty placeholders");
        CHECK(g.nodes.size()==2 && g.nodes[0].classId==7 && g.nodes[0].kind=="drift-root"
              && g.nodes[0].a==0 && g.nodes[0].b==50 && g.nodes[1].a==50 && g.nodes[1].b==100, "tiled class + windows");
    }

    std::printf("drift_partition_test: %d checks, %d failures%s\n",
                tests, fails, fails ? " — FAILURES" : "");
    std::printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return fails ? 1 : 0;
}
