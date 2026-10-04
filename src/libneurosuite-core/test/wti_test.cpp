// wti_test.cpp — the .wti template index reader/writer (neurofileio
// readWti/writeWti), with the version-2 schema bump: a trailing `parent` column
// (the manual-lineage tree) and the `collision` link kind.
//
// Self-contained (own main, assert-based); built when NS_BUILD_TESTS=ON, run via
// ctest.  Writes scratch files in CWD.
//
// Covers: a v1 round-trip (no parents) that stays byte-shape v1; a v2 round-trip
// that carries parent + a collision link; readWti accepting a hand-written v1 and
// v2 file (v1 -> parent defaults -1); the version-3 rejection; and geometry.

#include "neurosuite/core/neurofileio.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace neurofileio;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static void writeRaw(const std::string& path, const std::string& body) {
    std::ofstream o(path); o << body; o.close();
}
static std::string firstLine(const std::string& path) {
    std::ifstream in(path); std::string l; std::getline(in, l); return l;
}
// token count of the first "row ..." line in a .wti file
static int rowTokenCount(const std::string& path) {
    std::ifstream in(path); std::string l;
    while (std::getline(in, l)) {
        if (l.rfind("row ", 0) == 0) {
            std::istringstream ls(l); std::string t; int n = 0;
            while (ls >> t) ++n;
            return n;
        }
    }
    return -1;
}

int main()
{
    const int nsamp = 42, nchan = 8;

    // ── 1. v1 round-trip (no parents) stays v1 ──────────────────────────────
    WtiIndex v1;
    v1.version = 1; v1.nSamples = nsamp; v1.nChannels = nchan; v1.peakSample = 21; v1.sr = 32552.0;
    v1.rows.push_back(WtiRow{0, 31, "drift", 0, 0.0,   120.0, 540, -1});
    v1.rows.push_back(WtiRow{1, 31, "drift", 1, 120.0, 240.0, 613, -1});
    v1.rows.push_back(WtiRow{2, 31, "adapt", 0, 1.0,   2.0,   410, -1});
    const std::string p1 = "wti_v1.tmp.wti";
    check(writeWti(p1, v1), "writeWti v1 ok");
    check(firstLine(p1) == "wti 1", "no parents -> header stays 'wti 1'");
    check(rowTokenCount(p1) == 8, "v1 row line has 8 tokens (no parent column appended)");

    WtiIndex r1 = readWti(p1);
    check(r1.ok && r1.version == 1, "readWti v1 ok, version 1");
    check(r1.nSamples == nsamp && r1.nChannels == nchan && r1.peakSample == 21 && r1.sr == 32552.0,
          "v1 geometry round-trips");
    check(r1.rows.size() == 3, "v1 3 rows");
    bool allMinus1 = true;
    for (const WtiRow& r : r1.rows) if (r.parent != -1) allMinus1 = false;
    check(allMinus1, "v1 rows read back with parent == -1");
    check(r1.rows.size() == 3 && r1.rows[0].link == "drift" && r1.rows[2].link == "adapt",
          "v1 links round-trip");

    // ── 2. v2 round-trip: parent + a collision link ─────────────────────────
    WtiIndex v2;
    v2.version = 1;                     // writer decides the version from the rows
    v2.nSamples = nsamp; v2.nChannels = nchan; v2.peakSample = 21; v2.sr = 32552.0;
    v2.rows.push_back(WtiRow{0, 31, "drift",     0, 0.0, 120.0, 540, -1});  // root
    v2.rows.push_back(WtiRow{1, 31, "adapt",     0, 1.0, 2.0,   410,  0});  // child of row 0
    v2.rows.push_back(WtiRow{2, 31, "collision", 0, 0.0, 0.0,    12,  0});  // child of row 0
    const std::string p2 = "wti_v2.tmp.wti";
    check(writeWti(p2, v2), "writeWti v2 ok");
    check(firstLine(p2) == "wti 2", "a parent present -> header becomes 'wti 2'");
    check(rowTokenCount(p2) == 9, "v2 row line has 9 tokens (parent column appended)");

    WtiIndex r2 = readWti(p2);
    check(r2.ok && r2.version == 2, "readWti v2 ok, version 2");
    check(r2.rows.size() == 3, "v2 3 rows");
    check(r2.rows.size() == 3 && r2.rows[0].parent == -1 && r2.rows[1].parent == 0
          && r2.rows[2].parent == 0, "v2 parent column round-trips");
    check(r2.rows.size() == 3 && r2.rows[2].link == "collision", "collision link round-trips verbatim");

    // ── 3. Hand-written v1 and v2 files read correctly ──────────────────────
    writeRaw("wti_handv1.tmp.wti",
             "wti 1\nnSamples 4\nnChannels 2\npeakSample -1\nsr 0\nnRows 1\n"
             "# row unit link bin a b nSpikes\nrow 0 7 drift 0 0 1 99\n");
    WtiIndex h1 = readWti("wti_handv1.tmp.wti");
    check(h1.ok && h1.rows.size() == 1 && h1.rows[0].parent == -1 && h1.rows[0].nSpikes == 99,
          "hand-written v1 reads (parent defaults -1)");

    writeRaw("wti_handv2.tmp.wti",
             "wti 2\nnSamples 4\nnChannels 2\npeakSample -1\nsr 0\nnRows 2\n"
             "# row unit link bin a b nSpikes parent\n"
             "row 0 7 drift 0 0 1 99 -1\nrow 1 7 collision 0 0 0 4 0\n");
    WtiIndex h2 = readWti("wti_handv2.tmp.wti");
    check(h2.ok && h2.version == 2 && h2.rows.size() == 2
          && h2.rows[1].parent == 0 && h2.rows[1].link == "collision",
          "hand-written v2 reads parent + collision");

    // A v2 reader tolerates a row missing its trailing parent (defaults -1).
    writeRaw("wti_v2short.tmp.wti",
             "wti 2\nnRows 1\n# ...\nrow 0 7 drift 0 0 1 99\n");
    WtiIndex hs = readWti("wti_v2short.tmp.wti");
    check(hs.ok && hs.rows.size() == 1 && hs.rows[0].parent == -1,
          "v2 row without a parent token defaults to -1");

    // ── 4. Unknown version rejected ─────────────────────────────────────────
    writeRaw("wti_v3.tmp.wti", "wti 3\nnRows 0\n");
    check(!readWti("wti_v3.tmp.wti").ok, "version 3 rejected");

    std::printf("wti_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
