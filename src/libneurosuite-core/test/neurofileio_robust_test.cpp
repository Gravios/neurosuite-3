// neurofileio_robust_test.cpp — regression tests for the neurofileio readers'
// behaviour on files that are valid-but-unusual or corrupt: format detection,
// truncated and crafted headers, empty groups, malformed registries.  Every case
// here once returned garbage with ok=true or threw out of a reader.
// Self-contained (own main, check-based); built when NS_BUILD_TESTS=ON.
//
// Usage: neurofileio_robust_test   (no args; writes scratch files in CWD)

#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/template_generate.hpp"   // templategen::readResAny

#include <cstdint>
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

int main()
{
    testResDetection();

    if (g_fail == 0) std::printf("OK: %d checks, 0 failed\n", g_ran);
    else             std::printf("FAILED: %d of %d checks\n", g_fail, g_ran);
    return g_fail == 0 ? 0 : 1;
}
