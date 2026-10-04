// template_lineage_test.cpp — renderLineage(): a manual .wtl lineage rendered to
// .wti rows + per-variant .wtf stacks (template_generate.hpp).  Unlike generate()
// it invents no binning — each node is medianed over its OWN spike set.
//
// Self-contained, assert-based.  Checks: the drift-root/adapt-leaf/collision-leaf
// kind→link mapping, the per-(class,link) bin ordinals, parent row indices, an
// empty node emitting a 0-filled placeholder (an "unset" region), multi-variant
// medians, and the out-of-range spike-index guard.  Header-only (no link).

#include "neurosuite/core/template_generate.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace neurosuite::templategen;

static int tests = 0, fails = 0;
#define CHECK(cond, msg) do { ++tests; if(!(cond)){ ++fails; \
    std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);} } while(0)

// Build a variant stack where spike s has every element equal to value[s], so a
// node's median is trivial to predict.
static std::map<std::string, std::vector<int16_t>>
spkConst(const std::vector<int>& value, int nsamp, int nchan, const std::string& variant)
{
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;
    std::vector<int16_t> stack;
    stack.reserve(value.size() * recLen);
    for (int v : value) for (std::size_t e = 0; e < recLen; ++e) stack.push_back(static_cast<int16_t>(v));
    return { { variant, stack } };
}

int main()
{
    const int nsamp = 4, nchan = 2;
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;
    // 8 spikes, values chosen so medians are exact integers.
    //            s:  0   1   2   3   4   5   6   7
    std::vector<int> val = {100,100,200,200, 50, 70, 10, 10};

    // Forest for class 31:
    //   node 0 drift-root (region 1), spikes {0,1,2,3} -> median 150
    //     node 1 adapt-leaf  (lower-amp), spikes {4,5}   -> median 60
    //     node 2 collision-leaf (locked pair), spikes {6,7} -> median 10
    //   node 3 drift-root (region 2, UNSET), spikes {}   -> placeholder (0), nSpikes 0
    neurofileio::WtlForest f; f.version = 1;
    { neurofileio::WtlNode n; n.node=0; n.classId=31; n.kind="drift-root";     n.parent=-1; n.a=0.0;   n.b=120.0; n.spikes={0,1,2,3}; f.nodes.push_back(n); }
    { neurofileio::WtlNode n; n.node=1; n.classId=31; n.kind="adapt-leaf";     n.parent=0;  n.a=1.0;   n.b=2.0;   n.spikes={4,5};     f.nodes.push_back(n); }
    { neurofileio::WtlNode n; n.node=2; n.classId=31; n.kind="collision-leaf"; n.parent=0;  n.a=0.0;   n.b=0.0;   n.spikes={6,7};     f.nodes.push_back(n); }
    { neurofileio::WtlNode n; n.node=3; n.classId=31; n.kind="drift-root";     n.parent=-1; n.a=120.0; n.b=240.0; n.spikes={};        f.nodes.push_back(n); }

    auto spk = spkConst(val, nsamp, nchan, "standard");

    Result R = renderLineage(f, {"standard"}, spk, nsamp, nchan);
    CHECK(R.ok, "renderLineage ok");
    CHECK(R.rows.size() == 4, "one row per node (4)");

    // Row/link/bin/parent/nSpikes.
    CHECK(R.rows.size()==4 && R.rows[0].link=="drift"     && R.rows[0].bin==0 && R.rows[0].parent==-1 && R.rows[0].nSpikes==4, "drift-root -> link drift, bin 0, root, 4 spikes");
    CHECK(R.rows.size()==4 && R.rows[1].link=="adapt"     && R.rows[1].bin==0 && R.rows[1].parent==0  && R.rows[1].nSpikes==2, "adapt-leaf -> link adapt, parent row 0");
    CHECK(R.rows.size()==4 && R.rows[2].link=="collision" && R.rows[2].bin==0 && R.rows[2].parent==0  && R.rows[2].nSpikes==2, "collision-leaf -> link collision, parent row 0");
    CHECK(R.rows.size()==4 && R.rows[3].link=="drift"     && R.rows[3].bin==1 && R.rows[3].parent==-1 && R.rows[3].nSpikes==0, "second drift-root -> bin 1 (per class,link), empty");
    CHECK(R.rows.size()==4 && R.rows[0].unitId==31 && R.rows[3].a==120.0 && R.rows[3].b==240.0, "unit id + window coords carried");

    // Medians in the .wtf stack (one record per row).
    const auto& s = R.wtf.at("standard");
    CHECK(s.size() == 4 * recLen, "wtf has 4 records");
    auto recAll = [&](int r, int16_t want) {
        for (std::size_t e = 0; e < recLen; ++e) if (s[r*recLen + e] != want) return false; return true; };
    CHECK(s.size()==4*recLen && recAll(0, 150), "drift-root median {100,100,200,200} -> 150");
    CHECK(s.size()==4*recLen && recAll(1, 60),  "adapt-leaf median {50,70} -> 60");
    CHECK(s.size()==4*recLen && recAll(2, 10),  "collision-leaf median {10,10} -> 10");
    CHECK(s.size()==4*recLen && recAll(3, 0),   "empty drift-root -> 0-filled placeholder");

    // Multi-variant: a second variant (values + 5) renders its own stack, rows shared.
    auto spk2 = spk;
    { std::vector<int> v2; for (int x : val) v2.push_back(x + 4);
      auto m = spkConst(v2, nsamp, nchan, "alt"); spk2["alt"] = m.at("alt"); }
    Result R2 = renderLineage(f, {"standard","alt"}, spk2, nsamp, nchan);
    CHECK(R2.ok && R2.wtf.count("alt") && R2.wtf.at("alt").size()==4*recLen, "multi-variant: alt stack present");
    const auto& sa = R2.wtf.at("alt");
    CHECK(sa.size()==4*recLen && sa[0]==154, "alt drift-root median {104,104,204,204} -> 154");
    CHECK(R2.rows.size()==4 && R2.wtf.at("standard")[0]==150, "standard stack unchanged with 2 variants");

    // Out-of-range spike index -> error, nothing rendered.
    neurofileio::WtlForest bad; bad.version=1;
    { neurofileio::WtlNode n; n.node=0; n.classId=1; n.kind="drift-root"; n.parent=-1; n.spikes={0, 999}; bad.nodes.push_back(n); }
    Result RB = renderLineage(bad, {"standard"}, spk, nsamp, nchan);
    CHECK(!RB.ok, "out-of-range spike index rejected");

    std::printf("template_lineage_test: %d checks, %d failures%s\n",
                tests, fails, fails ? " — FAILURES" : "");
    std::printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return fails ? 1 : 0;
}
