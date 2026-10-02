// neurofileio_spk_test.cpp — round-trips the .spk int16 waveform I/O and the
// stage-tagged path composition added to the shared library, so klusters'
// "Save As stage" / grown-waveform writing and any other consumer share one
// implementation.  Self-contained (own main, assert-based); built when
// NS_BUILD_TESTS=ON, run via ctest.
//
// Usage: neurofileio_spk_test   (no args; writes a scratch file in CWD)

#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/custody.hpp"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_ran  = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    using namespace neurofileio;

    // ── .spk round-trip ─────────────────────────────────────────────────────
    const int nSamples = 4, nChannels = 3;
    const int64_t nSpikes = 5;
    const int64_t recVals = static_cast<int64_t>(nSamples) * nChannels;  // 12
    std::vector<int16_t> buf(static_cast<size_t>(nSpikes * recVals));
    // Fill with a value that encodes (spike, sample, channel) so a transpose or
    // off-by-one in the flat index is caught, spanning negative int16 too.
    for (int64_t s = 0; s < nSpikes; ++s)
        for (int t = 0; t < nSamples; ++t)
            for (int c = 0; c < nChannels; ++c) {
                const int64_t idx = (s * nSamples + t) * nChannels + c;
                buf[static_cast<size_t>(idx)] =
                    static_cast<int16_t>((s * 100 + t * 10 + c) - 250);
            }

    const std::string path = "nfio_spk_roundtrip.tmp.spk";
    check(writeSpk(path, nSamples, nChannels, buf), "writeSpk ok");

    SpkFile r = readSpk(path, nSamples, nChannels);
    check(r.ok, "readSpk ok");
    check(r.nSamples == nSamples && r.nChannels == nChannels, "readSpk geometry preserved");
    check(r.nSpikes == nSpikes, "readSpk nSpikes derived from file size");
    check(r.samples == buf, "readSpk bytes identical to writeSpk input");

    // Wrong geometry → not a whole number of records → rejected (no silent misread).
    SpkFile bad = readSpk(path, nSamples, nChannels + 1);
    check(!bad.ok && bad.nSpikes == 0, "readSpk rejects a geometry mismatch");

    // Missing file → not ok.
    check(!readSpk("does_not_exist.spk", nSamples, nChannels).ok, "readSpk missing file not ok");

    // writeSpk refuses a buffer that is not a whole number of records.
    std::vector<int16_t> ragged(static_cast<size_t>(recVals + 1), 0);
    check(!writeSpk("nfio_spk_ragged.tmp.spk", nSamples, nChannels, ragged),
          "writeSpk refuses a partial-record buffer");

    // Empty (zero-spike) .spk is valid and round-trips to nSpikes == 0.
    check(writeSpk(path, nSamples, nChannels, {}), "writeSpk empty ok");
    SpkFile empty = readSpk(path, nSamples, nChannels);
    check(empty.ok && empty.nSpikes == 0 && empty.samples.empty(), "readSpk empty -> 0 spikes");

    // ── binary .fet write/read round-trip ───────────────────────────────────
    {
        const int nFeat = 4;
        std::vector<int64_t> vals;                       // 3 spikes x 4 features, row-major
        for (int64_t k = 0; k < 12; ++k) vals.push_back(k * 7 - 20);
        const std::string fp = "nfio_fet_roundtrip.tmp.fet";
        check(writeFetBinary(fp, nFeat, vals), "writeFetBinary ok");
        FetBinaryFile fb = readFetBinary(fp);
        check(fb.ok && fb.nFeatures == nFeat && fb.nSpikes == 3, "readFetBinary geometry");
        check(fb.values == vals, "fet values round-trip identical");
        check(!writeFetBinary("x.fet", nFeat, {1, 2, 3}), "writeFetBinary refuses partial rows");
        std::remove(fp.c_str());
    }

    // ── .col collision sidecar: accepted-record parse ───────────────────────
    {
        const std::string cp = "nfio_col.tmp.col";
        std::ofstream os(cp, std::ios::binary);
        auto wU32 = [&](uint32_t v){ os.write(reinterpret_cast<char*>(&v),4); };
        auto wI32 = [&](int32_t  v){ os.write(reinterpret_cast<char*>(&v),4); };
        auto wI64 = [&](int64_t  v){ os.write(reinterpret_cast<char*>(&v),8); };
        auto wF32 = [&](float    v){ os.write(reinterpret_cast<char*>(&v),4); };
        const unsigned char magic[4] = {'C','O','L',0x01};
        os.write(reinterpret_cast<const char*>(magic),4);
        wU32(100); wU32(3); wU32(0); wU32(6); wU32(0);       // n_spikes,n_records,n_templates,group,flags
        for(int i=0;i<8;++i) os.put(0);                       // header pad[8]
        for(int i=0;i<32;++i) os.put(0);                      // ColParams (32B)
        // 3 records (60B each): rec0 accepted, rec1 NOT, rec2 accepted
        auto wRec = [&](int64_t ts,int idx,uint32_t fl,int u1,int sh1,float a1,int u2,int sh2,float a2){
            wI64(ts); wI32(idx); wI32(0); wF32(0.0f); wU32(fl); wF32(0.1f);
            wI32(u1); wI32(sh1); wF32(0.0f); wF32(a1);
            wI32(u2); wI32(sh2); wF32(0.0f); wF32(a2);
        };
        wRec(1000, 47, 1u, 3, -4, 1.03f, 2, 6, 0.97f);        // accepted
        wRec(2000, 50, 0u, 3,  0, 1.00f, 2, 0, 1.00f);        // NOT accepted
        wRec(3000, 61, 1u, 5,  2, 0.80f, 9, -1, 1.20f);       // accepted
        os.close();

        const auto cols = readColAccepted(cp);
        check(cols.size() == 2, "readColAccepted returns only accepted records");
        if (cols.size() == 2) {
            check(cols[0].spikeIndex == 47 && cols[0].u1 == 3 && cols[0].sh1 == -4
                  && cols[0].u2 == 2 && cols[0].sh2 == 6, "col record 0 fields");
            check(std::fabs(cols[0].a1 - 1.03) < 1e-4 && std::fabs(cols[0].a2 - 0.97) < 1e-4, "col record 0 amps");
            check(cols[1].spikeIndex == 61 && cols[1].u1 == 5 && cols[1].u2 == 9, "col record 2 fields (skipped the rejected one)");
        }
        check(readColAccepted("nope.col").empty(), "readColAccepted missing file -> empty");
        std::remove(cp.c_str());
    }

    // ── stage-tagged path composition ───────────────────────────────────────
    // <base>.<type>.<method>.<group>[.<stage>], and parseAnchor reads the stage
    // back as the suffix (no leading dot), so a staged file round-trips.
    const std::string sp =
        stagePath("/d/sirotaA-jg-000005-20120316", "spk", "stderiv_C5_D34", 6, "final");
    check(sp == "/d/sirotaA-jg-000005-20120316.spk.stderiv_C5_D34.6.final",
          "stagePath composes <base>.<type>.<method>.<group>.<stage>");
    const auto a = neurosuite::custody::parseAnchor(sp);
    check(a.ok && a.suffix == "final" && a.method == "stderiv_C5_D34" && a.group == 6,
          "parseAnchor round-trips stagePath (suffix == stage)");
    // Empty stage degrades to the plain methodPath.
    check(stagePath("/d/base", "clu", "standard", 8, "") == methodPath("/d/base", "clu", "standard", 8),
          "stagePath empty stage == methodPath");

    std::remove(path.c_str());

    std::printf("%s: %d checks, %d failed\n", (g_fail ? "FAIL" : "OK"), g_ran, g_fail);
    return g_fail ? 1 : 0;
}
