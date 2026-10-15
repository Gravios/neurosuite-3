// neurofileio_robust_test.cpp — regression tests for the neurofileio readers'
// behaviour on files that are valid-but-unusual or corrupt: format detection,
// truncated and crafted headers, empty groups, malformed registries.  Every case
// here once returned garbage with ok=true or threw out of a reader.
// Self-contained (own main, check-based); built when NS_BUILD_TESTS=ON.
//
// Usage: neurofileio_robust_test   (no args; writes scratch files in CWD)

#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/template_generate.hpp"   // templategen::readResAny

#include <cmath>
#include <cstdint>
#include <iterator>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_ran  = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static void writeBytes(const std::string& path, const std::string& bytes) {
    std::ofstream os(path, std::ios::binary);
    os.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// ── 1. .res text/binary detection ───────────────────────────────────────────
static void testResDetection()
{
    using namespace neurofileio;
    // Binary .res whose first timestamp is 50: low byte 0x32 == '2'.  The old
    // "first byte is not an ASCII digit" rule read it as text.
    {
        const std::string rp = "nfio_rob_res50.tmp.res", cp = "nfio_rob_res50.tmp.clu";
        const std::vector<int64_t> t = {50, 130, 1000, 32552, 7000000};
        check(writeResBinary(rp, t), "res50: writeResBinary");
        check(writeCluBinary(cp, 3, {2, 2, 3, 1, 2}), "res50: writeCluBinary");
        check(isBinaryClusterRes(rp), "res50: detected as binary");

        bool ok = false, bin = false;
        const std::vector<int64_t> a = readResAuto(rp, &ok, &bin);
        check(ok && bin && a == t, "res50: readResAuto returns the binary timestamps");

        ok = false;
        const std::vector<int64_t> b = neurosuite::templategen::readResAny(rp, &ok);
        check(ok && b == t, "res50: templategen::readResAny returns the binary timestamps");

        const ClusterResData cr = readClusterRes(cp, rp);
        check(cr.ok && cr.binary && cr.times == t && cr.nClusters == 3 && cr.ids.size() == 5,
              "res50: readClusterRes reads the pair as binary");
        std::remove(rp.c_str()); std::remove(cp.c_str());
    }
    // Every first-byte value 0x30..0x39 ('0'..'9') as the first timestamp.
    {
        const std::string rp = "nfio_rob_resdigit.tmp.res";
        bool allOk = true;
        for (int64_t first = 0x30; first <= 0x39; ++first) {
            const std::vector<int64_t> t = {first, first + 256};
            writeResBinary(rp, t);
            bool ok = false;
            const std::vector<int64_t> a = readResAuto(rp, &ok);
            if (!isBinaryClusterRes(rp) || !ok || a != t) allOk = false;
        }
        check(allOk, "first timestamp low byte '0'..'9': all detected binary and read exactly");
        std::remove(rp.c_str());
    }
    // A single tiny binary timestamp (8 bytes; 7 zero bytes) is binary.
    {
        const std::string rp = "nfio_rob_restiny.tmp.res";
        writeResBinary(rp, {7});
        bool ok = false;
        const std::vector<int64_t> a = readResAuto(rp, &ok);
        check(isBinaryClusterRes(rp) && ok && a == std::vector<int64_t>{7},
              "single tiny binary timestamp read as binary");
        std::remove(rp.c_str());
    }
    // Text .res whose size is a multiple of 8 (and one with a leading space,
    // which the old first-byte rule called binary) stays text.
    {
        const std::string rp = "nfio_rob_restext.tmp.res";
        writeBytes(rp, "1\n2\n3\n4\n");                 // 8 bytes
        bool ok = false, bin = true;
        std::vector<int64_t> a = readResAuto(rp, &ok, &bin);
        check(ok && !bin && a == std::vector<int64_t>{1, 2, 3, 4}, "8-byte text .res read as text");

        writeBytes(rp, " 10\r\n 200\r\n-3\r\n");        // 16 bytes, leading space, CRLF
        ok = false; bin = true;
        a = readResAuto(rp, &ok, &bin);
        check(ok && !bin && a == std::vector<int64_t>{10, 200, -3},
              "16-byte text .res with leading space + CRLF read as text");
        std::remove(rp.c_str());
    }
    // Inconsistent files fail (ok=false, empty) instead of returning garbage.
    {
        const std::string rp = "nfio_rob_resbad.tmp.res";
        // Text-looking prefix, but a non-integer line later: strict text parse fails.
        std::string txt;
        for (int i = 0; i < 2000; ++i) txt += std::to_string(i) + "\n";
        txt += "12x\n";
        writeBytes(rp, txt);
        bool ok = true;
        std::vector<int64_t> a = readResAuto(rp, &ok);
        check(!ok && a.empty(), "text .res with a malformed line -> ok=false, empty");
        ok = true;
        a = neurosuite::templategen::readResAny(rp, &ok);
        check(!ok && a.empty(), "readResAny: malformed text -> ok=false");

        // Text with a stray non-text byte (size % 8 == 0) looks binary, but its
        // decoded values put ASCII in the high byte: rejected, not garbage.
        writeBytes(rp, "100\n200\n300\n40e\n");          // 16 bytes, 'e' outside the alphabet
        ok = true;
        a = readResAuto(rp, &ok);
        check(isBinaryClusterRes(rp) && !ok && a.empty(),
              "text with a stray byte looks binary but fails the plausibility check");

        // Binary-sized garbage that is not a multiple of 8 falls to text and fails.
        writeBytes(rp, std::string("\x32\x00\x00\x00\x00\x00\x00", 7));
        ok = true;
        a = readResAuto(rp, &ok);
        check(!ok && a.empty(), "7-byte binary-ish file -> ok=false");
        std::remove(rp.c_str());
    }
    // Empty file: zero spikes, ok.
    {
        const std::string rp = "nfio_rob_resempty.tmp.res";
        writeBytes(rp, "");
        bool ok = false;
        const std::vector<int64_t> a = readResAuto(rp, &ok);
        check(ok && a.empty(), "empty .res -> ok, zero spikes");
        std::remove(rp.c_str());
    }
}

// ── 2. .evt precision ───────────────────────────────────────────────────────
static void testEvtPrecision()
{
    using namespace neurofileio;
    const std::string ep = "nfio_rob_evt.tmp.evt";
    // 3 h into a session: the stream default (6 significant digits) wrote
    // 10800123.456789 ms as "1.08001e+07" -- 1 s resolution, scientific.
    const std::vector<EvtEntry> ev = {{10800123.456789, "stim on"}, {0.5, "start"}, {-2.25, "pre"}};
    check(writeEvt(ep, ev), "evt: writeEvt ok");
    std::ifstream in(ep); std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(all == "10800123.456789\tstim on\n0.500000\tstart\n-2.250000\tpre\n",
          "evt: %.6f ms (process_plxconvert convention), no scientific notation");
    bool ok = false;
    const std::vector<EvtEntry> r = readEvt(ep, &ok);
    check(ok && r.size() == 3 && std::fabs(r[0].timeMs - ev[0].timeMs) < 1e-6 && r[0].label == "stim on",
          "evt: re-read keeps sub-microsecond time + label");
    std::remove(ep.c_str());
}

// ── 4. .col: a truncated file is rejected, not returned as a prefix ─────────
static std::string colFile(uint32_t nRecords, uint32_t nTemplates, int recordsWritten)
{
    std::string b = "COL";
    b += '\x01';
    auto u32 = [&](uint32_t v) { b.append(reinterpret_cast<const char*>(&v), 4); };
    auto i32 = [&](int32_t v)  { b.append(reinterpret_cast<const char*>(&v), 4); };
    auto f32 = [&](float v)    { b.append(reinterpret_cast<const char*>(&v), 4); };
    u32(100); u32(nRecords); u32(nTemplates); u32(0); u32(0);
    b.append(8, '\0');                       // header pad -> 32 B
    b.append(32, '\0');                      // ColParams
    b.append(static_cast<std::size_t>(nTemplates) * 24, '\0');
    for (int r = 0; r < recordsWritten; ++r) {
        const int64_t ts = 1000 + r; b.append(reinterpret_cast<const char*>(&ts), 8);
        i32(10 + r); i32(2); f32(0.5f); u32(1u /*accepted*/); f32(0.1f);
        i32(2); i32(-1); f32(0.f); f32(1.0f);
        i32(3); i32(4);  f32(0.f); f32(0.9f);
    }
    return b;
}
static void testColTruncation()
{
    using namespace neurofileio;
    const std::string cp = "nfio_rob.tmp.col";
    writeBytes(cp, colFile(3, 2, 3));
    bool ok = false;
    std::vector<ColDecomp> v = readColAccepted(cp, &ok);
    check(ok && v.size() == 3 && v[2].spikeIndex == 12, "col: complete file -> 3 accepted records, ok");

    std::string cut = colFile(3, 2, 3); cut.resize(cut.size() - 30);   // last record half-written
    writeBytes(cp, cut);
    ok = true;
    v = readColAccepted(cp, &ok);
    check(!ok && v.empty(), "col: truncated last record -> empty, ok=false (no 2-record prefix)");

    writeBytes(cp, colFile(3, 2, 1));        // header says 3 records, 1 present
    ok = true;
    v = readColAccepted(cp, &ok);
    check(!ok && v.empty(), "col: header declares more records than present -> empty, ok=false");

    writeBytes(cp, colFile(0, 0, 0));
    ok = false;
    v = readColAccepted(cp, &ok);
    check(ok && v.empty(), "col: valid zero-record file -> empty, ok=true");

    ok = true;
    v = readColAccepted("nfio_rob_missing.tmp.col", &ok);
    check(!ok && v.empty(), "col: missing file -> ok=false");
    std::remove(cp.c_str());
}

int main()
{
    testResDetection();
    testEvtPrecision();
    testColTruncation();

    if (g_fail == 0) std::printf("OK: %d checks, 0 failed\n", g_ran);
    else             std::printf("FAILED: %d of %d checks\n", g_fail, g_ran);
    return g_fail == 0 ? 0 : 1;
}
