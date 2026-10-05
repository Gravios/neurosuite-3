// template_lineage_io_test.cpp — the on-disk manual-lineage writer
// (template_generate.hpp renderLineageToFiles): persist a v2 .wtl forest (tree +
// running mean/std/count) and render it to the MODEL files (.mti v2 + .mtf) by
// COPYING the stored means — no .spk read, no re-median.
//
// Links the library (writeWti/readWti/writeWtl/readWtl/readSpk live in
// neurofileio.cpp).  Writes scratch files in CWD.

#include "neurosuite/core/template_generate.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace neurosuite::templategen;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

// A node whose stored mean is constant `m` over recLen, count `c`.
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
    const std::string base = "lin_io.tmp";
    const int group = 5, nsamp = 4, nchan = 2;
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;

    // Forest: class 31 — drift-root (mean 150, count 300) with adapt-leaf
    // (mean 60, count 120) and collision-leaf (mean 10, count 8) children; plus an
    // empty drift-root (unset region, count 0).
    neurofileio::WtlForest f; f.version = 2; f.nSamples = nsamp; f.nChannels = nchan;
    f.nodes.push_back(mkNode(0, 31, "drift-root",     -1, 0,   120, 300, 150.f, recLen));
    f.nodes.push_back(mkNode(1, 31, "adapt-leaf",      0,  1,   2,   120, 60.f,  recLen));
    f.nodes.push_back(mkNode(2, 31, "collision-leaf",  0,  0,   0,   8,   10.f,  recLen));
    f.nodes.push_back(mkNode(3, 31, "drift-root",     -1, 120, 240, 0,   0.f,   recLen));

    LineageFileParams fp;
    fp.base = base; fp.group = group; fp.variants = {"standard"};
    fp.stage = "refine"; fp.spkTag = ""; fp.nSamples = nsamp; fp.nChannels = nchan; fp.sr = 32552.0;

    // Clear stale outputs from a prior run (ctest reuses the build dir).
    std::remove(sessionPath(base, "wtl", group, "", "refine").c_str());
    std::remove(sessionPath(base, "wti", group, "", "refine").c_str());
    std::remove(sessionPath(base, "mti", group, "", "refine").c_str());
    std::remove(sessionPath(base, "mtf", group, "standard", "refine").c_str());

    std::string wtlPath, mtiPath;
    std::map<std::string, std::string> mtfPaths;
    Result R = renderLineageToFiles(fp, f, &wtlPath, &mtiPath, &mtfPaths);
    check(R.ok, "renderLineageToFiles ok");

    // The render targets the MODEL files (.mti/.mtf), NOT the .wti/.wtf library.
    check(mtiPath == sessionPath(base, "mti", group, "", "refine"), "model index path is .mti");
    check(mtfPaths["standard"] == sessionPath(base, "mtf", group, "standard", "refine"),
          "model waveform path is .mtf");

    // .wtl (the source) persisted == forest (v2, geometry stamped).
    neurofileio::WtlForest rl = neurofileio::readWtl(wtlPath);
    check(rl.ok && rl.version==2 && rl.nSamples==nsamp && rl.nChannels==nchan, ".wtl v2 + geometry header");
    check(rl.ok && rl.nodes.size()==4 && rl.nodes[2].kind=="collision-leaf" && rl.nodes[2].count==8,
          "collision-leaf node persisted with its count");
    check(rl.ok && rl.nodes.size()==4 && rl.nodes[0].mean.size()==recLen && rl.nodes[0].mean[0]==150.f,
          "drift-root mean persisted");
    check(rl.ok && rl.nodes.size()==4 && rl.nodes[3].count==0 && rl.nodes[3].mean.empty(), "empty placeholder persisted");

    // .mti is the wti v2 schema with the parent tree + kind→link mapping.
    neurofileio::WtiIndex wi = neurofileio::readWti(mtiPath);
    check(wi.ok && wi.version == 2, ".mti written as v2 (has parents)");
    check(wi.rows.size() == 4, ".mti 4 rows");
    check(wi.rows.size()==4 && wi.rows[0].link=="drift" && wi.rows[0].parent==-1 && wi.rows[0].nSpikes==300, "row0 drift root, count 300");
    check(wi.rows.size()==4 && wi.rows[1].link=="adapt" && wi.rows[1].parent==0, "row1 adapt child of row0");
    check(wi.rows.size()==4 && wi.rows[2].link=="collision" && wi.rows[2].parent==0, "row2 collision child of row0");
    check(wi.rows.size()==4 && wi.rows[3].link=="drift" && wi.rows[3].nSpikes==0, "row3 empty drift placeholder");
    check(wi.nSamples == nsamp && wi.nChannels == nchan, ".mti geometry header");

    // .mtf = the stored means (one record per row), empty row zero-filled.
    neurofileio::SpkFile wf = neurofileio::readSpk(mtfPaths["standard"], nsamp, nchan);
    check(wf.ok && wf.nSpikes == 4, ".mtf has 4 records");
    auto recAll = [&](int r, int16_t want) {
        for (std::size_t e = 0; e < recLen; ++e) if (wf.samples[r*recLen + e] != want) return false; return true; };
    check(wf.ok && recAll(0,150) && recAll(1,60) && recAll(2,10) && recAll(3,0),
          ".mtf means 150/60/10 + 0 placeholder");

    // The commit does NOT touch the auto-generated .wti/.wtf library (no clobber).
    check(!neurofileio::readWti(sessionPath(base, "wti", group, "", "refine")).ok,
          "no .wti written by the lineage render");

    // An all-placeholder forest (every node count 0) is refused — nothing to commit.
    neurofileio::WtlForest empty; empty.version=2; empty.nSamples=nsamp; empty.nChannels=nchan;
    empty.nodes.push_back(mkNode(0, 31, "drift-root", -1, 0, 120, 0, 0.f, recLen));
    const Result ER = renderLineageToFiles(fp, empty);
    check(!ER.ok && !ER.err.empty(), "all-placeholder forest -> commit refused");

    std::printf("template_lineage_io_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
