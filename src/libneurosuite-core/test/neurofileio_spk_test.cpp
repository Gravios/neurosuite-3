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
#include <utility>

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

    // ── .wti waveform-template index: round-trip + .wtf row alignment ────────
    {
        WtiIndex w;
        w.version = 1; w.nSamples = nSamples; w.nChannels = nChannels;
        w.peakSample = 2; w.sr = 32552.0;
        // 2 units, each a 2-bin drift series then a 2-bin adapt series = 8 rows,
        // row index == .wtf record index.
        int row = 0;
        for (int u : {31, 9}) {
            for (int bin = 0; bin < 2; ++bin)
                w.rows.push_back(WtiRow{row++, u, "drift", bin, bin*120.0, (bin+1)*120.0, 500 + bin});
            for (int bin = 0; bin < 2; ++bin)
                w.rows.push_back(WtiRow{row++, u, "adapt", bin, 1.0+bin, 2.0+bin, 400 + bin});
        }
        const std::string wp = "nfio_wti_roundtrip.tmp.wti";
        check(writeWti(wp, w), "writeWti ok");

        WtiIndex r = readWti(wp);
        check(r.ok, "readWti ok");
        check(r.version==1 && r.nSamples==nSamples && r.nChannels==nChannels
              && r.peakSample==2 && std::fabs(r.sr-32552.0) < 1e-6, "readWti geometry round-trips");
        check(r.rows.size()==8, "readWti row count");
        if (r.rows.size()==8) {
            check(r.rows[0].unitId==31 && r.rows[0].link=="drift" && r.rows[0].bin==0
                  && r.rows[0].nSpikes==500, "wti row 0 fields");
            check(r.rows[2].unitId==31 && r.rows[2].link=="adapt" && r.rows[2].bin==0,
                  "wti row 2 is the unit's first adapt bin");
            check(r.rows[4].unitId==9 && r.rows[4].link=="drift", "wti row 4 switches unit");
            check(std::fabs(r.rows[1].b-240.0) < 1e-6, "wti drift bin coordinate round-trips");
        }

        // view-model helpers: unit set, per-unit links, per-(unit,link) series.
        const auto units = wtiUnits(r);
        check(units.size()==2 && units[0]==9 && units[1]==31, "wtiUnits -> ascending {9,31}");
        const auto links = wtiLinks(r, 31);
        check(links.size()==2 && links[0]=="drift" && links[1]=="adapt", "wtiLinks first-seen {drift,adapt}");
        const auto driftSer = wtiSeries(r, 31, "drift");
        check(driftSer.size()==2 && driftSer[0].bin==0 && driftSer[1].bin==1
              && driftSer[0].row==0 && driftSer[1].row==1, "wtiSeries(31,drift) bins/rows in order");
        const auto adaptSer = wtiSeries(r, 9, "adapt");
        check(adaptSer.size()==2 && adaptSer[0].row==6 && adaptSer[1].row==7,
              "wtiSeries(9,adapt) maps to that unit's .wtf records");
        check(wtiSeries(r, 31, "nope").empty(), "wtiSeries unknown link -> empty");

        // The .wtf is a headerless .spk-layout int16 stack: one record per wti row.
        std::vector<int16_t> wtf(static_cast<size_t>(r.rows.size()) * recVals);
        for (size_t k = 0; k < wtf.size(); ++k) wtf[k] = static_cast<int16_t>(k % 97 - 48);
        const std::string wtfp = "nfio_wtf_roundtrip.tmp.wtf";
        check(writeSpk(wtfp, nSamples, nChannels, wtf), "writeSpk(.wtf) ok");
        SpkFile wf = readSpk(wtfp, nSamples, nChannels);
        check(wf.ok && static_cast<std::size_t>(wf.nSpikes)==r.rows.size(),
              ".wtf record count == wti row count (alignment contract)");

        // Negative cases: bad header, version, nRows mismatch, missing file.
        { std::ofstream bad("nfio_wti_bad.tmp.wti"); bad << "notwti 1\nnSamples 4\n"; }
        check(!readWti("nfio_wti_bad.tmp.wti").ok, "readWti rejects a bad header line");
        { std::ofstream bad("nfio_wti_ver.tmp.wti"); bad << "wti 2\nnRows 0\n"; }
        check(!readWti("nfio_wti_ver.tmp.wti").ok, "readWti rejects an unknown version");
        { std::ofstream bad("nfio_wti_cnt.tmp.wti");
          bad << "wti 1\nnRows 3\nrow 0 1 drift 0 0 1 5\n"; }
        check(!readWti("nfio_wti_cnt.tmp.wti").ok, "readWti rejects a declared/actual row mismatch");
        check(!readWti("nope.wti").ok, "readWti missing file -> not ok");

        std::remove(wp.c_str()); std::remove(wtfp.c_str());
        std::remove("nfio_wti_bad.tmp.wti"); std::remove("nfio_wti_ver.tmp.wti");
        std::remove("nfio_wti_cnt.tmp.wti");
    }

    // ── .eap membership+offset matrix: round-trip, sentinel, grow, queries ──
    {
        const int64_t N = 4; const int T = 3;
        std::vector<int8_t> cells(static_cast<size_t>(N) * T, EAP_ABSENT);
        auto set = [&](int64_t i, int j, int8_t v){ cells[static_cast<size_t>(i)*T + j] = v; };
        set(0, 1, 0);                 // spike0: class1 at offset 0 (offset 0 is NOT "absent")
        set(1, 0, -5); set(1, 2, 7);  // spike1: a collision — classes 0 and 2
        // spike2: nothing
        set(3, 2, -127);              // spike3: class2 at the extreme offset

        const std::string ep = "nfio_eap_roundtrip.tmp.eap";
        check(writeEap(ep, N, T, 6, 0u, cells), "writeEap ok");
        EapFile e = readEap(ep);
        check(e.ok && e.nSpikes==N && e.nClasses==T && e.group==6, "readEap geometry");
        check(e.cells == cells, "eap cells byte-identical");
        check(eapPresent(e.cells[0*T+1]) && e.cells[0*T+1]==0, "offset 0 is present, not absent");
        check(!eapPresent(e.cells[2*T+0]), "unset cell reads as absent");

        const auto c2 = eapClassSpikes(e, 2);
        check(c2.size()==2 && c2[0]==1 && c2[1]==3, "eapClassSpikes(2) -> {1,3}");
        check(eapClassSpikes(e, 1).size()==1, "eapClassSpikes(1) -> {0}");
        const auto s1 = eapSpikeClasses(e, 1);
        check(s1.size()==2 && s1[0].first==0 && s1[0].second==-5
              && s1[1].first==2 && s1[1].second==7, "eapSpikeClasses(1) collision pairs");
        check(eapSpikeClasses(e, 2).empty(), "eapSpikeClasses(2) -> empty");

        // grow 3 -> 5: existing cells preserved at the new stride, new cols absent.
        EapFile g = growEap(e, 5);
        check(g.nClasses==5 && g.nSpikes==N, "growEap widens T");
        check(g.cells[1*5+0]==-5 && g.cells[1*5+2]==7, "growEap preserves existing cells");
        check(!eapPresent(g.cells[1*5+3]) && !eapPresent(g.cells[3*5+4]), "grown columns are absent");
        check(growEap(e, 2).nClasses==T, "growEap is a no-op when newT <= T");

        // init: all-absent preallocation.
        const std::string ip = "nfio_eap_init.tmp.eap";
        check(initEap(ip, N, T, 6), "initEap ok");
        EapFile ie = readEap(ip);
        bool allAbsent = ie.ok;
        for (int8_t v : ie.cells) if (eapPresent(v)) allAbsent = false;
        check(allAbsent && ie.nClasses==T, "initEap is all-absent");

        check(!writeEap("x.eap", N, T, 6, 0u, std::vector<int8_t>(5)), "writeEap refuses a wrong-size matrix");
        check(!readEap("nope.eap").ok, "readEap missing file -> not ok");

        std::remove(ep.c_str()); std::remove(ip.c_str());
    }

    // ── .tcl template-class registry: round-trip + statuses + empty fields ──
    {
        TclRegistry reg = initTcl(4);
        check(reg.ok && reg.nClasses==4 && reg.entries.size()==4
              && reg.entries[0].status==TclStatus::Free, "initTcl -> 4 free slots");
        reg.entries[0].status = TclStatus::Active;
        reg.entries[0].label = "CA1 pyr a";          // a label WITH spaces
        reg.entries[0].provenanceClu = 23;
        reg.entries[0].provenanceStage = "gt";
        reg.entries[0].created = "2026-10-02";
        reg.entries[1].status = TclStatus::Merged; reg.entries[1].mergedInto = 0;
        reg.entries[2].status = TclStatus::Tomb;   reg.entries[2].provenanceClu = 41;
        // entry[3] stays Free with all-empty fields

        const std::string tp = "nfio_tcl_roundtrip.tmp.tcl";
        check(writeTcl(tp, reg), "writeTcl ok");
        TclRegistry r = readTcl(tp);
        check(r.ok && r.nClasses==4 && r.entries.size()==4, "readTcl geometry");
        check(r.entries[0].status==TclStatus::Active && r.entries[0].label=="CA1 pyr a"
              && r.entries[0].provenanceClu==23 && r.entries[0].provenanceStage=="gt"
              && r.entries[0].created=="2026-10-02", "tcl active entry (label with spaces) round-trips");
        check(r.entries[1].status==TclStatus::Merged && r.entries[1].mergedInto==0, "tcl merged:<col> round-trips");
        check(r.entries[2].status==TclStatus::Tomb && r.entries[2].provenanceClu==41, "tcl tomb round-trips");
        check(r.entries[3].status==TclStatus::Free && r.entries[3].label.empty()
              && r.entries[3].provenanceClu==-1 && r.entries[3].provenanceStage.empty(),
              "tcl free slot: empty fields read back empty");

        check(!readTcl("nope.tcl").ok, "readTcl missing file -> not ok");
        { std::ofstream bad("nfio_tcl_bad.tmp.tcl"); bad << "nottcl 1\n"; }
        check(!readTcl("nfio_tcl_bad.tmp.tcl").ok, "readTcl rejects a bad header");

        std::remove(tp.c_str()); std::remove("nfio_tcl_bad.tmp.tcl");
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
