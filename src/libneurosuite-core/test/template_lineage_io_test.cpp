// template_lineage_io_test.cpp — the on-disk manual-lineage writer
// (template_generate.hpp renderLineageToFiles): persist a .wtl forest and render
// it to the shared library (.wti v2 + per-variant .wtf) on a synthetic session.
//
// Links the library (readSpk/writeSpk/writeWti/writeWtl/readWti/readWtl live in
// neurofileio.cpp).  Writes scratch files in CWD.

#include "neurosuite/core/template_generate.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace neurosuite::templategen;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    const std::string base = "lin_io.tmp";
    const int group = 5, nsamp = 4, nchan = 2;
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;

    // Synthetic .spk.standard.5 : 8 spikes, spike s all-equal to val[s].
    std::vector<int> val = {100,100,200,200, 50, 70, 10, 10};
    std::vector<int16_t> stack;
    for (int v : val) for (std::size_t e = 0; e < recLen; ++e) stack.push_back(static_cast<int16_t>(v));
    const std::string spkPath =
        neurosuite::templategen::sessionPath(base, "spk", group, "standard", "");
    check(neurofileio::writeSpk(spkPath, nsamp, nchan, stack), "wrote synthetic .spk");

    // Forest: class 31 — drift-root {0,1,2,3}=150 with adapt-leaf {4,5}=60 and
    // collision-leaf {6,7}=10 children; plus an empty drift-root (unset region).
    neurofileio::WtlForest f; f.version = 1;
    { neurofileio::WtlNode n; n.node=0; n.classId=31; n.kind="drift-root";     n.parent=-1; n.a=0;   n.b=120; n.spikes={0,1,2,3}; f.nodes.push_back(n); }
    { neurofileio::WtlNode n; n.node=1; n.classId=31; n.kind="adapt-leaf";     n.parent=0;  n.a=1;   n.b=2;   n.spikes={4,5};     f.nodes.push_back(n); }
    { neurofileio::WtlNode n; n.node=2; n.classId=31; n.kind="collision-leaf"; n.parent=0;  n.a=0;   n.b=0;   n.spikes={6,7};     f.nodes.push_back(n); }
    { neurofileio::WtlNode n; n.node=3; n.classId=31; n.kind="drift-root";     n.parent=-1; n.a=120; n.b=240; n.spikes={};        f.nodes.push_back(n); }

    LineageFileParams fp;
    fp.base = base; fp.group = group; fp.variants = {"standard"};
    fp.stage = "refine"; fp.spkTag = ""; fp.nSamples = nsamp; fp.nChannels = nchan; fp.sr = 32552.0;

    std::string wtlPath, wtiPath;
    std::map<std::string, std::string> wtfPaths;
    Result R = renderLineageToFiles(fp, f, &wtlPath, &wtiPath, &wtfPaths);
    check(R.ok, "renderLineageToFiles ok");

    // .wtl persisted == forest.
    neurofileio::WtlForest rl = neurofileio::readWtl(wtlPath);
    check(rl.ok && rl.nodes.size() == 4, "readback .wtl has 4 nodes");
    check(rl.ok && rl.nodes.size()==4 && rl.nodes[2].kind == "collision-leaf"
          && rl.nodes[2].spikes == std::vector<int64_t>({6,7}), "collision-leaf node persisted");
    check(rl.ok && rl.nodes.size()==4 && rl.nodes[3].spikes.empty(), "empty drift-root persisted");

    // .wti is v2 with the parent tree + kind→link mapping.
    neurofileio::WtiIndex wi = neurofileio::readWti(wtiPath);
    check(wi.ok && wi.version == 2, ".wti written as v2 (has parents)");
    check(wi.rows.size() == 4, ".wti 4 rows");
    check(wi.rows.size()==4 && wi.rows[0].link=="drift" && wi.rows[0].parent==-1, "row0 drift root");
    check(wi.rows.size()==4 && wi.rows[1].link=="adapt" && wi.rows[1].parent==0, "row1 adapt child of row0");
    check(wi.rows.size()==4 && wi.rows[2].link=="collision" && wi.rows[2].parent==0, "row2 collision child of row0");
    check(wi.rows.size()==4 && wi.rows[3].link=="drift" && wi.rows[3].nSpikes==0, "row3 empty drift placeholder");
    check(wi.nSamples == nsamp && wi.nChannels == nchan, ".wti geometry header");

    // .wtf medians (one record per row), empty row zero-filled.
    neurofileio::SpkFile wf = neurofileio::readSpk(wtfPaths["standard"], nsamp, nchan);
    check(wf.ok && wf.nSpikes == 4, ".wtf has 4 records");
    auto recAll = [&](int r, int16_t want) {
        for (std::size_t e = 0; e < recLen; ++e) if (wf.samples[r*recLen + e] != want) return false; return true; };
    check(wf.ok && recAll(0,150) && recAll(1,60) && recAll(2,10) && recAll(3,0),
          ".wtf medians 150/60/10 + 0 placeholder");

    // A missing .spk variant is a clean error (nothing half-written beyond this call's inputs).
    LineageFileParams bad = fp; bad.variants = {"nope"};
    check(!renderLineageToFiles(bad, f).ok, "missing .spk variant -> error");

    std::printf("template_lineage_io_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
