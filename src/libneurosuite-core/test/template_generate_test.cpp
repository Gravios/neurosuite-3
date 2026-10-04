// template_generate_test.cpp — native fiber-template port (template_generate.hpp)
//
// Self-contained, assert-based.  Checks the numeric core against hand-computed
// medians on tiny synthetic sessions: clu-mode drift binning, numpy even-count
// median averaging, eap-mode offset alignment, adapt energy binning, and the
// dropEmpty placeholder policy.  No disk; the pure generate() takes in-memory
// times / membership / spk and returns the .wti rows + .wtf stacks.

#include "neurosuite/core/template_generate.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace neurosuite::templategen;

// Build a one-variant spk map from a per-spike scalar value: every element of a
// spike's record is set to that spike's value (so the median is trivial to read).
static std::map<std::string, std::vector<int16_t>>
spkConst(const std::vector<int>& valuePerSpike, int nsamp, int nchan)
{
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * nchan;
    std::vector<int16_t> stack;
    stack.reserve(valuePerSpike.size() * recLen);
    for (int v : valuePerSpike)
        for (std::size_t e = 0; e < recLen; ++e) stack.push_back(static_cast<int16_t>(v));
    return { { "standard", stack } };
}

// The record for spike `s` in a stack.
static const int16_t* rec(const std::vector<int16_t>& stack, int s, int nsamp, int nchan)
{
    return &stack[static_cast<std::size_t>(s) * nsamp * nchan];
}

static int tests = 0, fails = 0;
#define CHECK(cond, msg) do { ++tests; if(!(cond)){ ++fails; \
    std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);} } while(0)

// ── 1. clu-mode drift: two units, constant waveforms, two chunks ────────────
static void test_clu_drift()
{
    const int nsamp = 4, nchan = 2;
    //                t:  0  10  20   5  15  25 300 310 320 305 315 325
    std::vector<int64_t> times = {0,10,20,5,15,25,300,310,320,305,315,325};
    std::vector<int>     clu   = {2, 2, 2,3, 3, 3,  2,  2,  2,  3,  3,  3};
    std::vector<int>     val   = {100,100,100,200,200,200,100,100,100,200,200,200};
    auto spk = spkConst(val, nsamp, nchan);

    Params p; p.nSamples=nsamp; p.nChannels=nchan; p.sr=1000.0;
    p.linkDrift=true; p.linkAdapt=false; p.nChunks=2;

    Result R = generate(times, {"standard"}, spk, "", &clu, nullptr, {}, p);
    CHECK(R.ok, "clu_drift ok");
    CHECK(R.rows.size()==4, "clu_drift 4 rows (2 units x 2 chunks)");
    // rows: unit 2 bins 0,1 then unit 3 bins 0,1
    const int   wantUnit[4] = {2,2,3,3};
    const int   wantBin [4] = {0,1,0,1};
    const int   wantMed [4] = {100,100,200,200};
    const auto& stack = R.wtf.at("standard");
    const std::size_t recLen = static_cast<std::size_t>(nsamp)*nchan;
    CHECK(stack.size()==4*recLen, "clu_drift wtf has 4 records");
    for (int r = 0; r < 4 && r < (int)R.rows.size(); ++r) {
        CHECK(R.rows[r].link=="drift",       "clu_drift link==drift");
        CHECK(R.rows[r].unitId==wantUnit[r],  "clu_drift unit id");
        CHECK(R.rows[r].bin==wantBin[r],      "clu_drift bin");
        CHECK(R.rows[r].nSpikes==3,           "clu_drift nSpikes==3");
        bool allMed = true;
        for (std::size_t e = 0; e < recLen; ++e)
            if (stack[r*recLen + e] != wantMed[r]) allMed = false;
        CHECK(allMed, "clu_drift median waveform");
    }
}

// ── 2. numpy even-count median: {100,100,200,200} -> 150 ────────────────────
static void test_even_median()
{
    const int nsamp = 2, nchan = 1;
    std::vector<int64_t> times = {0,1,2,3};
    std::vector<int>     clu   = {2,2,2,2};
    std::vector<int>     val   = {100,100,200,200};
    auto spk = spkConst(val, nsamp, nchan);

    Params p; p.nSamples=nsamp; p.nChannels=nchan; p.sr=1000.0;
    p.linkDrift=true; p.nChunks=1;

    Result R = generate(times, {"standard"}, spk, "", &clu, nullptr, {}, p);
    CHECK(R.ok && R.rows.size()==1, "even_median 1 row");
    CHECK(R.rows.size()==1 && R.rows[0].nSpikes==4, "even_median nSpikes==4");
    const auto& s = R.wtf.at("standard");
    bool all150 = !s.empty();
    for (int16_t x : s) if (x != 150) all150 = false;
    CHECK(all150, "even_median == mean of two middles (150)");
}

// ── 2b. np.rint on an even-count median landing on .5: round-half-to-even ───
static void test_rint_half_even()
{
    const int nsamp = 1, nchan = 1;
    std::vector<int64_t> times = {0,1,2,3};
    std::vector<int>     clu   = {2,2,3,3};
    // unit 2: median{100,101}=100.5 -> rint -> 100 (even).
    // unit 3: median{101,102}=101.5 -> rint -> 102 (even).
    std::vector<int>     val   = {100,101,101,102};
    auto spk = spkConst(val, nsamp, nchan);

    Params p; p.nSamples=nsamp; p.nChannels=nchan; p.sr=1000.0;
    p.linkDrift=true; p.nChunks=1;
    Result R = generate(times, {"standard"}, spk, "", &clu, nullptr, {}, p);
    CHECK(R.ok && R.rows.size()==2, "rint_half 2 rows");
    const auto& s = R.wtf.at("standard");
    CHECK(s.size()==2 && s[0]==100, "rint_half 100.5 -> 100 (half to even)");
    CHECK(s.size()==2 && s[1]==102, "rint_half 101.5 -> 102 (half to even)");
}

// ── 3. eap-mode offset alignment: a +1-shifted member rolls back onto the peak ─
static void test_eap_align()
{
    const int nsamp = 4, nchan = 1;
    std::vector<int64_t> times = {0,1};
    // spike0 peak at sample 1; spike1 peak at sample 2 (shifted +1).
    std::vector<int16_t> stack = { 0,10,0,0,   0,0,10,0 };
    std::map<std::string,std::vector<int16_t>> spk = { {"standard", stack} };

    neurofileio::EapFile e;
    e.nSpikes=2; e.nClasses=1; e.group=1; e.ok=true;
    e.cells = { 0,            // spike0, class0: offset 0
                1 };          // spike1, class0: offset 1 (its EAP is +1 from res)

    Params p; p.nSamples=nsamp; p.nChannels=nchan; p.sr=1000.0;
    p.linkDrift=true; p.nChunks=1; p.eap=true;

    Result R = generate(times, {"standard"}, spk, "", nullptr, &e, {}, p);
    CHECK(R.ok && R.rows.size()==1, "eap_align 1 row");
    CHECK(R.rows.size()==1 && R.rows[0].unitId==0 && R.rows[0].nSpikes==2, "eap_align class0 nSpikes==2");
    // Aligned, both members are [0,10,0,0], so the median is [0,10,0,0] — NOT the
    // unaligned [0,5,5,0] a clu-mode median would give.
    const int16_t want[4] = {0,10,0,0};
    const auto& s = R.wtf.at("standard");
    bool aligned = (s.size()==4);
    for (int i=0;i<4 && aligned;++i) if (s[i]!=want[i]) aligned=false;
    CHECK(aligned, "eap_align median is the offset-aligned peak [0,10,0,0]");

    // Cross-check: clu-mode on the SAME waveforms (no alignment) -> [0,5,5,0].
    std::vector<int> clu = {2,2};
    Params pc = p; pc.eap=false;
    Result Rc = generate(times, {"standard"}, spk, "", &clu, nullptr, {}, pc);
    const int16_t wantC[4] = {0,5,5,0};
    const auto& sc = Rc.wtf.at("standard");
    bool unaligned = (sc.size()==4);
    for (int i=0;i<4 && unaligned;++i) if (sc[i]!=wantC[i]) unaligned=false;
    CHECK(unaligned, "clu_mode (no align) median is [0,5,5,0]");
}

// ── 4. adapt energy binning: four energies into two equal-occupancy bins ────
static void test_adapt_energy()
{
    const int nsamp = 1, nchan = 1;         // energy = |value|
    std::vector<int64_t> times = {0,1,2,3};
    std::vector<int>     clu   = {2,2,2,2};
    std::vector<int>     val   = {10,20,30,40};
    auto spk = spkConst(val, nsamp, nchan);

    Params p; p.nSamples=nsamp; p.nChannels=nchan; p.sr=1000.0;
    p.linkDrift=false; p.linkAdapt=true; p.nEnergy=2;

    Result R = generate(times, {"standard"}, spk, "", &clu, nullptr, {}, p);
    CHECK(R.ok && R.rows.size()==2, "adapt 2 energy bins");
    // edges = [10, 25, 40+eps]: bin0={10,20}->15, bin1={30,40}->35.
    const int wantMed[2] = {15,35};
    const auto& s = R.wtf.at("standard");
    bool ok = (R.rows.size()==2 && s.size()==2);
    for (int r=0; r<2 && ok; ++r) {
        if (R.rows[r].link!="adapt" || R.rows[r].bin!=r || R.rows[r].nSpikes!=2) ok=false;
        if (s[r]!=wantMed[r]) ok=false;
    }
    CHECK(ok, "adapt per-energy-bin medians (15, 35)");
    (void)rec;
}

// ── 5. dropEmpty: a unit present in only one of two chunks drops the empty one ─
static void test_drop_empty()
{
    const int nsamp = 1, nchan = 1;
    // nChunks=3 over [0, 101): edges ~[0, 33.67, 67.33, 101].  Two spikes in
    // chunk 0, one in chunk 2 (at tmax), chunk 1 empty.
    std::vector<int64_t> times = {0,1,100};
    std::vector<int>     clu   = {2,2,2};
    std::vector<int>     val   = {50,50,50};
    auto spk = spkConst(val, nsamp, nchan);

    Params p; p.nSamples=nsamp; p.nChannels=nchan; p.sr=1000.0;
    p.linkDrift=true; p.nChunks=3; p.dropEmpty=true;
    Result R = generate(times, {"standard"}, spk, "", &clu, nullptr, {}, p);
    CHECK(R.ok && R.rows.size()==2, "dropEmpty: the empty middle chunk is dropped (2 rows)");
    CHECK(R.rows.size()==2 && R.rows[0].bin==0 && R.rows[1].bin==2, "dropEmpty: bins 0 and 2 kept");

    p.dropEmpty=false;
    Result R2 = generate(times, {"standard"}, spk, "", &clu, nullptr, {}, p);
    CHECK(R2.rows.size()==3, "dropEmpty=false: all three chunks emitted");
    CHECK(R2.rows.size()==3 && R2.rows[1].bin==1 && R2.rows[1].nSpikes==0 && R2.wtf.at("standard")[1]==0,
          "dropEmpty=false: empty chunk is a 0-filled placeholder");
}

int main()
{
    test_clu_drift();
    test_even_median();
    test_rint_half_even();
    test_eap_align();
    test_adapt_energy();
    test_drop_empty();
    std::printf("template_generate_test: %d checks, %d failures%s\n",
                tests, fails, fails ? " — FAILURES" : "");
    std::printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return fails ? 1 : 0;
}
