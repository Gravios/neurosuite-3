// golden_formats_test.cpp — reference files for the template-library formats.
//
// test/golden/ holds one small file per format, written by neurofileio's C++
// writers: golden.wti.1 (v1), golden.wti.2 (v2, with parents),
// golden.wtf.standard.1, golden.wtl.1 (v2 summaries), golden.eap.1 and
// golden.tcl.1.  fk.wti.1, fk.wti.2 and fk.wtl.1 (v1, spike lists) are written
// by fiber-kit's neuro_io.  fiber-kit ships byte-identical copies of all of
// them (test/test_golden_formats.py); contract.yaml records their hashes.
//
// This test writes the golden.* content again and checks it is byte-identical
// to the committed files (so a writer change shows up here), then reads every
// committed file, fk.* included, and checks every field.
//
//     core_test_golden_formats <golden dir>            check
//     core_test_golden_formats <golden dir> --write    regenerate golden.* (then update
//                                                      both contract.yaml files
//                                                      and fiber-kit's copies)
//
// The content is spelled out once here and once in fiber-kit's test; keep the
// two in step.
#include <neurosuite/core/neurofileio.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace nf = neurofileio;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++fails; }
}

static constexpr int NS = 4, NCH = 2;

static nf::WtiIndex wti(bool withParents) {
    nf::WtiIndex x;
    x.nSamples = NS; x.nChannels = NCH; x.peakSample = 1; x.sr = 20000.0;
    auto row = [](int r, int u, const char* link, int bin, double a, double b, int64_t n, int parent) {
        nf::WtiRow w; w.row = r; w.unitId = u; w.link = link; w.bin = bin;
        w.a = a; w.b = b; w.nSpikes = n; w.parent = parent; return w;
    };
    x.rows = {row(0, 7, "drift", 0, 0.0,   120.5,  540, -1),
              row(1, 7, "drift", 1, 120.5, 241.25, 0,   withParents ? 0 : -1),
              row(2, 9, "adapt", 0, 1.5,   2.75,   33,  -1)};
    if (withParents) x.rows.push_back(row(3, 7, "collision", 0, 0.0, 120.5, 12, 0));
    return x;
}

static std::vector<int16_t> wtf() {
    return {0, 1, -1, 32767, -32768, 100, -100, 7,          // record 0
            -5, -4, -3, -2, -1, 0, 1, 2,                    // record 1
            1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000};
}

static nf::WtlForest wtl() {
    nf::WtlForest f;
    f.nSamples = NS; f.nChannels = NCH;
    auto node = [](int id, int cls, const char* kind, int parent, double a, double b, int64_t count,
                   std::vector<float> mean, std::vector<float> sd) {
        nf::WtlNode n; n.node = id; n.classId = cls; n.kind = kind; n.parent = parent;
        n.a = a; n.b = b; n.count = count; n.mean = std::move(mean); n.std = std::move(sd); return n;
    };
    f.nodes = {node(0, 7, "drift-root", -1, 0.0, 120.5, 540,
                    {0.1f, -1.5f, 1e-3f, 1234.5678f, -0.0f, 3.0f, -250.25f, 1e-7f},
                    {2.1f, 3.0f, 0.5f, 10.75f, 0.0f, 1e-4f, 7.0f, 0.333333f}),
               node(1, 7, "adapt-leaf", 0, 1.5, 2.75, 0, {}, {}),
               node(2, 9, "drift-root", -1, 241.25, 360.0, 3,
                    {-1.0f, -2.0f, -3.0f, -4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
                    {1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 2.0f})};
    return f;
}

static const int EAP_N = 5, EAP_T = 4, EAP_GROUP = 1;
static const uint32_t EAP_FLAGS = 3;
static std::vector<int8_t> eap() {
    const int8_t A = nf::EAP_ABSENT;
    return {0, A, A, A,
            A, 2, A, A,
            -3, A, 127, A,
            A, A, A, -127,
            1, -1, A, 0};
}

static nf::TclRegistry tcl() {
    nf::TclRegistry r = nf::initTcl(5);
    r.entries[0].status = nf::TclStatus::Active; r.entries[0].label = "CA1 pyr a";
    r.entries[0].provenanceClu = 23; r.entries[0].provenanceStage = "gt"; r.entries[0].created = "2026-10-02";
    r.entries[2].status = nf::TclStatus::Tomb; r.entries[2].label = "old"; r.entries[2].provenanceClu = 4;
    r.entries[3].status = nf::TclStatus::Merged; r.entries[3].mergedInto = 0; r.entries[3].label = "x";
    r.entries[4].status = nf::TclStatus::Active; r.entries[4].provenanceClu = 0;
    return r;
}

static bool writeAll(const fs::path& d) {
    const std::string b = (d / "golden").string();
    return nf::writeWti(b + ".wti.1", wti(false))
        && nf::writeWti(b + ".wti.2", wti(true))
        && nf::writeSpk(b + ".wtf.standard.1", NS, NCH, wtf())
        && nf::writeWtl(b + ".wtl.1", wtl())
        && nf::writeEap(b + ".eap.1", EAP_N, EAP_T, EAP_GROUP, EAP_FLAGS, eap())
        && nf::writeTcl(b + ".tcl.1", tcl());
}

static const char* const kFiles[] = {"golden.wti.1", "golden.wti.2", "golden.wtf.standard.1",
                                     "golden.wtl.1", "golden.eap.1", "golden.tcl.1"};

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void checkWti(const nf::WtiIndex& got, const nf::WtiIndex& want, int version, const std::string& n) {
    check(got.ok, n + ": reads");
    check(got.version == version, n + ": version " + std::to_string(got.version));
    check(got.nSamples == NS && got.nChannels == NCH && got.peakSample == 1 && got.sr == 20000.0,
          n + ": header");
    check(got.rows.size() == want.rows.size(), n + ": row count");
    for (size_t i = 0; i < got.rows.size() && i < want.rows.size(); ++i) {
        const auto &g = got.rows[i], &w = want.rows[i];
        check(g.row == w.row && g.unitId == w.unitId && g.link == w.link && g.bin == w.bin
                  && g.a == w.a && g.b == w.b && g.nSpikes == w.nSpikes && g.parent == w.parent,
              n + ": row " + std::to_string(i));
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s <golden dir> [--write]\n", argv[0]); return 2; }
    const fs::path golden = argv[1];
    if (argc > 2 && std::string(argv[2]) == "--write") {
        fs::create_directories(golden);
        if (!writeAll(golden)) { std::printf("cannot write into %s\n", golden.c_str()); return 1; }
        std::printf("wrote the golden files into %s\n", golden.c_str());
        return 0;
    }

    // 1. The writers still produce the committed bytes.
    const fs::path tmp = fs::temp_directory_path() / ("golden_formats_" + std::to_string(std::random_device{}()));
    fs::create_directories(tmp);
    check(writeAll(tmp), "write into a temporary directory");
    for (const char* f : kFiles) {
        const std::string want = slurp(golden / f);
        check(!want.empty(), std::string(f) + ": committed file present");
        check(slurp(tmp / f) == want, std::string(f) + ": the C++ writer reproduces the committed bytes");
    }
    fs::remove_all(tmp);

    // 2. The readers read every field back.
    const std::string b = (golden / "golden").string();
    checkWti(nf::readWti(b + ".wti.1"), wti(false), 1, "golden.wti.1");
    checkWti(nf::readWti(b + ".wti.2"), wti(true), 2, "golden.wti.2");

    const nf::SpkFile s = nf::readSpk(b + ".wtf.standard.1", NS, NCH);
    check(s.ok && s.nSpikes == 3 && s.samples == wtf(), "golden.wtf.standard.1: 3 records, every sample");

    const nf::WtlForest gw = nf::readWtl(b + ".wtl.1"), ww = wtl();
    check(gw.ok && gw.version == 2 && gw.nSamples == NS && gw.nChannels == NCH, "golden.wtl.1: header");
    check(gw.nodes.size() == ww.nodes.size(), "golden.wtl.1: node count");
    for (size_t i = 0; i < gw.nodes.size() && i < ww.nodes.size(); ++i) {
        const auto &g = gw.nodes[i], &w = ww.nodes[i];
        check(g.node == w.node && g.classId == w.classId && g.kind == w.kind && g.parent == w.parent
                  && g.a == w.a && g.b == w.b && g.count == w.count,
              "golden.wtl.1: node " + std::to_string(i) + " fields");
        check(g.mean == w.mean && g.std == w.std,
              "golden.wtl.1: node " + std::to_string(i) + " mean/std exactly (round-trip precision)");
    }

    const nf::EapFile e = nf::readEap(b + ".eap.1");
    check(e.ok && e.nSpikes == EAP_N && e.nClasses == EAP_T && e.group == EAP_GROUP
              && e.flags == EAP_FLAGS && e.cells == eap(),
          "golden.eap.1: header and every cell");

    const nf::TclRegistry gt = nf::readTcl(b + ".tcl.1"), wt = tcl();
    check(gt.ok && gt.version == 1 && gt.nClasses == 5 && gt.entries.size() == 5, "golden.tcl.1: header");
    for (size_t i = 0; i < gt.entries.size() && i < wt.entries.size(); ++i) {
        const auto &g = gt.entries[i], &w = wt.entries[i];
        check(g.col == w.col && g.status == w.status && g.label == w.label
                  && g.provenanceClu == w.provenanceClu && g.provenanceStage == w.provenanceStage
                  && g.created == w.created && g.mergedInto == w.mergedInto,
              "golden.tcl.1: entry " + std::to_string(i));
    }

    // 3. The files fiber-kit writes: v1/v2 .wti with its trailing provenance
    //    columns (ignored here), and a v1 .wtl, read as legacy stubs (count =
    //    the number of spikes listed, no summary).
    const std::string k = (golden / "fk").string();
    checkWti(nf::readWti(k + ".wti.1"), wti(false), 1, "fk.wti.1");
    checkWti(nf::readWti(k + ".wti.2"), wti(true), 2, "fk.wti.2");
    const nf::WtlForest fw = nf::readWtl(k + ".wtl.1");
    struct V1 { int node, cls; const char* kind; int parent; double a, b; int64_t count; };
    const V1 v1[] = {{0, 7, "drift-root", -1, 0.0, 120.5, 3},
                     {1, 7, "adapt-leaf", 0, 1.5, 2.75, 0},
                     {2, 9, "collision-leaf", -1, 241.25, 360.0, 2}};
    check(fw.ok && fw.version == 1 && fw.nodes.size() == 3, "fk.wtl.1: a v1 forest of 3 nodes");
    for (size_t i = 0; i < fw.nodes.size() && i < 3; ++i) {
        const auto& g = fw.nodes[i];
        check(g.node == v1[i].node && g.classId == v1[i].cls && g.kind == v1[i].kind
                  && g.parent == v1[i].parent && g.a == v1[i].a && g.b == v1[i].b
                  && g.count == v1[i].count && g.mean.empty() && g.std.empty(),
              "fk.wtl.1: node " + std::to_string(i) + " (a stub: count, no summary)");
    }

    if (fails) { std::printf("%d check(s) failed\n", fails); return 1; }
    std::printf("golden_formats: all checks passed\n");
    return 0;
}
