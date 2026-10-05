// template_lineage_test.cpp — renderLineage(): a manual .wtl lineage rendered to
// .wti rows + a single .mtf-layout stack (template_generate.hpp).  v2 nodes carry
// a stored running MEAN, so render is a COPY (rounded) — no spikes, no re-median.
//
// Self-contained, assert-based.  Checks: the drift-root/adapt-leaf/collision-leaf
// kind→link mapping, the per-(class,link) bin ordinals, parent row indices,
// nSpikes = count, the stored mean copied (rounded) into each record, and an empty
// node emitting a 0-filled placeholder.  Header-only (no link).

#include "neurosuite/core/template_generate.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace neurosuite::templategen;

static int tests = 0, fails = 0;
#define CHECK(cond, msg) do { ++tests; if(!(cond)){ ++fails; \
    std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);} } while(0)

// A node whose stored mean has every element = `m` (so the rendered record is a
// constant), count `c`.
static neurofileio::WtlNode mkNode(int id, int cls, const char* kind, int parent,
                                   double a, double b, int64_t c, float m, std::size_t recLen)
{
    neurofileio::WtlNode n; n.node=id; n.classId=cls; n.kind=kind; n.parent=parent;
    n.a=a; n.b=b; n.count=c;
    if (c > 0) { n.mean.assign(recLen, m); n.std.assign(recLen, 1.f); }
    return n;
}

int main()
{
    const int nsamp = 4, nchan = 2;
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;

    // Forest for class 31:
    //   node 0 drift-root (region 1), mean 150.4 (-> rounds to 150), count 540
    //     node 1 adapt-leaf,  mean 60.0,  count 210
    //     node 2 collision-leaf, mean 10.0, count 8
    //   node 3 drift-root (region 2, UNSET placeholder), count 0
    neurofileio::WtlForest f; f.version = 2; f.nSamples = nsamp; f.nChannels = nchan;
    f.nodes.push_back(mkNode(0, 31, "drift-root",     -1, 0.0,   120.0, 540, 150.4f, recLen));
    f.nodes.push_back(mkNode(1, 31, "adapt-leaf",      0, 1.0,   2.0,   210, 60.0f,  recLen));
    f.nodes.push_back(mkNode(2, 31, "collision-leaf",  0, 0.0,   0.0,   8,   10.0f,  recLen));
    f.nodes.push_back(mkNode(3, 31, "drift-root",     -1, 120.0, 240.0, 0,   0.0f,   recLen));

    Result R = renderLineage(f, "standard", nsamp, nchan);
    CHECK(R.ok, "renderLineage ok");
    CHECK(R.rows.size() == 4, "one row per node (4)");

    // Row/link/bin/parent/nSpikes(=count).
    CHECK(R.rows.size()==4 && R.rows[0].link=="drift"     && R.rows[0].bin==0 && R.rows[0].parent==-1 && R.rows[0].nSpikes==540, "drift-root -> link drift, bin 0, root, count 540");
    CHECK(R.rows.size()==4 && R.rows[1].link=="adapt"     && R.rows[1].bin==0 && R.rows[1].parent==0,  "adapt-leaf -> link adapt, parent row 0");
    CHECK(R.rows.size()==4 && R.rows[2].link=="collision" && R.rows[2].bin==0 && R.rows[2].parent==0,  "collision-leaf -> link collision, parent row 0");
    CHECK(R.rows.size()==4 && R.rows[3].link=="drift"     && R.rows[3].bin==1 && R.rows[3].parent==-1 && R.rows[3].nSpikes==0, "second drift-root -> bin 1 (per class,link), empty");
    CHECK(R.rows.size()==4 && R.rows[0].unitId==31 && R.rows[3].a==120.0 && R.rows[3].b==240.0, "unit id + window coords carried");

    // The stored means copied (rounded) into the .mtf stack (one record per row).
    const auto& s = R.wtf.at("standard");
    CHECK(s.size() == 4 * recLen, "stack has 4 records");
    auto recAll = [&](int r, int16_t want) {
        for (std::size_t e = 0; e < recLen; ++e) if (s[r*recLen + e] != want) return false; return true; };
    CHECK(s.size()==4*recLen && recAll(0, 150), "drift-root mean 150.4 -> rounded 150");
    CHECK(s.size()==4*recLen && recAll(1, 60),  "adapt-leaf mean 60 -> 60");
    CHECK(s.size()==4*recLen && recAll(2, 10),  "collision-leaf mean 10 -> 10");
    CHECK(s.size()==4*recLen && recAll(3, 0),   "empty placeholder -> 0-filled record");

    // A geometry-mismatched node mean (wrong length) renders as a 0-filled record
    // rather than corrupting the stack.
    neurofileio::WtlForest g; g.version=2; g.nSamples=nsamp; g.nChannels=nchan;
    { neurofileio::WtlNode n = mkNode(0, 1, "drift-root", -1, 0, 1, 3, 7.f, recLen);
      n.mean.resize(recLen - 1); g.nodes.push_back(n); }     // short mean
    Result RG = renderLineage(g, "standard", nsamp, nchan);
    CHECK(RG.ok && RG.wtf.at("standard").size()==recLen, "short-mean node still renders one record");
    CHECK(RG.wtf.at("standard").size()==recLen && RG.wtf.at("standard")[0]==0, "short mean -> 0-filled (no corruption)");

    std::printf("template_lineage_test: %d checks, %d failures%s\n",
                tests, fails, fails ? " — FAILURES" : "");
    std::printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return fails ? 1 : 0;
}
