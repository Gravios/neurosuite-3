// decollide_test.cpp — exercises the two-component collision-apply engine
// (decollide.hpp), the C++ port of fiber-kit's decollide_spikes.  Header-only,
// self-contained (own main, assert-based); built when NS_BUILD_TESTS=ON, run via
// ctest.  The noise-free linear case is EXACT, so the reconstructed constituents
// are checked against their closed form, not a tolerance.

#include "neurosuite/core/decollide.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace neurosuite::decollide;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static const int NS = 5, NC = 2;                 // nSamples, nChannels
static std::size_t REC() { return static_cast<std::size_t>(NS) * NC; }

// A distinct per-unit template shape (channel-fastest), small integers.
static std::vector<int16_t> tmpl(int base) {
    std::vector<int16_t> w(REC());
    for (int t = 0; t < NS; ++t)
        for (int c = 0; c < NC; ++c)
            w[static_cast<std::size_t>(t) * NC + c] =
                static_cast<int16_t>(base + 10 * t + c);
    return w;
}

int main()
{
    // ── roll0: zero-filled, non-circular, matches fiber-kit _roll0 ───────────
    {
        std::vector<float> w(REC()), out;
        for (std::size_t j = 0; j < REC(); ++j) w[j] = static_cast<float>(j + 1);
        roll0(w, NS, NC, 2, out);               // shift later by 2: first 2 samples zero
        check(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0, "roll0 +2 zero-fills head");
        check(out[2 * NC + 0] == w[0] && out[2 * NC + 1] == w[1], "roll0 +2 moves sample0 -> sample2");
        roll0(w, NS, NC, -1, out);              // shift earlier by 1: last sample zero
        check(out[(NS - 1) * NC + 0] == 0 && out[(NS - 1) * NC + 1] == 0, "roll0 -1 zero-fills tail");
        check(out[0] == w[1 * NC + 0] && out[1] == w[1 * NC + 1], "roll0 -1 moves sample1 -> sample0");
        roll0(w, NS, NC, 0, out);
        check(out == w, "roll0 0 is identity");
    }

    // Templates T2 (unit 2) and T3 (unit 3); a few noise-free spikes of each so
    // meanTemplates recovers them exactly.
    const std::vector<int16_t> T2 = tmpl(100), T3 = tmpl(5);
    std::vector<int64_t> res;
    std::vector<int>     clu;
    std::vector<int16_t> spk;
    auto addSpike = [&](int64_t t, int k, const std::vector<int16_t>& w) {
        res.push_back(t); clu.push_back(k);
        spk.insert(spk.end(), w.begin(), w.end());
    };
    addSpike(10, 2, T2);
    addSpike(20, 2, T2);
    addSpike(30, 3, T3);
    addSpike(40, 3, T3);

    // ── exact apply, no shift (tau=0): c = a1*T2 + a2*T3 ─────────────────────
    const double a1 = 2.0, a2 = 1.0;
    std::vector<int16_t> collision(REC());
    for (std::size_t j = 0; j < REC(); ++j)
        collision[j] = static_cast<int16_t>(a1 * T2[j] + a2 * T3[j]);
    const int64_t collTime = 25;
    addSpike(collTime, 1, collision);           // index 4, cluster 1 (a collision bucket)

    std::vector<Decomp> decomps;
    { Decomp d; d.spikeIndex = 4; d.c1 = {2, 0, a1}; d.c2 = {3, 0, a2}; decomps.push_back(d); }

    const Grown g = applyMethod(res, clu, spk, NS, NC, decomps);
    check(g.ok, "applyMethod ok");
    check(g.res.size() == res.size() + 1, "net +1 spike (collision -> two events)");

    // Find the two synthesized events (both at collTime since tau=0), by cluster.
    auto rowEq = [&](std::size_t o, const std::vector<int16_t>& ref, double amp) {
        for (std::size_t j = 0; j < REC(); ++j)
            if (g.spk[o * REC() + j] != static_cast<int16_t>(std::nearbyint(amp * ref[j]))) return false;
        return true;
    };
    int found2 = -1, found3 = -1;
    for (std::size_t o = 0; o < g.res.size(); ++o) {
        if (g.res[o] == collTime && g.clu[o] == 2) found2 = static_cast<int>(o);
        if (g.res[o] == collTime && g.clu[o] == 3) found3 = static_cast<int>(o);
    }
    check(found2 >= 0 && found3 >= 0, "both constituents present at det+tau");
    // clean1 = c - a2*T3 = a1*T2 ; clean2 = c - a1*T2 = a2*T3 (tau=0, exact)
    check(found2 >= 0 && rowEq(static_cast<std::size_t>(found2), T2, a1), "constituent k1 == a1*T2");
    check(found3 >= 0 && rowEq(static_cast<std::size_t>(found3), T3, a2), "constituent k2 == a2*T3");
    // The original collision (cluster 1) is gone; carried-over spikes remain.
    int nClu1 = 0, nClu2 = 0, nClu3 = 0;
    for (int k : g.clu) { nClu1 += (k == 1); nClu2 += (k == 2); nClu3 += (k == 3); }
    check(nClu1 == 0, "collision bucket emptied");
    check(nClu2 == 3 && nClu3 == 3, "each unit gained exactly its constituent");
    // Output is time-sorted.
    bool sorted = true;
    for (std::size_t o = 1; o < g.res.size(); ++o) if (g.res[o] < g.res[o - 1]) sorted = false;
    check(sorted, "output stable-sorted by time");

    // ── cross-method invariance: same res/clu/decomp, DIFFERENT spk -> identical
    //    res/clu (the property the per-method apply relies on) ────────────────
    std::vector<int16_t> spk2 = spk;
    for (auto& v : spk2) v = static_cast<int16_t>(v / 2 - 3);   // a different "method"
    const Grown g2 = applyMethod(res, clu, spk2, NS, NC, decomps);
    check(g2.ok && g2.res == g.res && g2.clu == g.clu, "res/clu identical across methods");
    check(g2.spk != g.spk, "spk differs across methods (as it must)");

    // ── a shift (tau != 0) exercises the roll/recentre path without crashing and
    //    keeps the +1 growth and the det+tau timestamps ───────────────────────
    {
        std::vector<Decomp> ds;
        Decomp d; d.spikeIndex = 4; d.c1 = {2, 1, a1}; d.c2 = {3, -1, a2}; ds.push_back(d);
        const Grown gs = applyMethod(res, clu, spk, NS, NC, ds);
        check(gs.ok && gs.res.size() == res.size() + 1, "shifted apply: net +1");
        bool t1 = false, t2 = false;
        for (std::size_t o = 0; o < gs.res.size(); ++o) {
            if (gs.res[o] == collTime + 1 && gs.clu[o] == 2) t1 = true;
            if (gs.res[o] == collTime - 1 && gs.clu[o] == 3) t2 = true;
        }
        check(t1 && t2, "shifted apply: events land at det+tau1 / det+tau2");
    }

    // ── missing template -> still grows (+1), constituents copy the collision;
    //    row alignment across methods is preserved regardless of templates ─────
    {
        std::vector<Decomp> ds;
        Decomp d; d.spikeIndex = 4; d.c1 = {2, 0, a1}; d.c2 = {99, 0, a2}; ds.push_back(d);  // unit 99 absent
        const Grown gm = applyMethod(res, clu, spk, NS, NC, ds);
        check(gm.ok && gm.res.size() == res.size() + 1, "missing template -> still grows (+1), aligned");
    }

    // ── applyFeatures: grow the feature table on the SAME plan, by PCA linearity
    //    from in-memory features (f(clean1)=f(c)-a2*meanFeat(k2), etc.) ─────────
    {
        const std::size_t D = 3;
        const std::vector<int64_t> MF2 = {100, 0, 10}, MF3 = {0, 80, 5};
        // collision features chosen so the exact (tau=0) split recovers a1*MF2 / a2*MF3
        std::vector<int64_t> fc(D);
        for (std::size_t j = 0; j < D; ++j)
            fc[j] = (int64_t)std::llround(a1 * MF2[j] + a2 * MF3[j]);
        // fet table aligned with the main res/clu (idx 0,1=unit2; 2,3=unit3; 4=collision)
        std::vector<std::vector<int64_t>> fet = {MF2, MF2, MF3, MF3, fc};
        std::vector<Decomp> dsf;
        { Decomp d; d.spikeIndex = 4; d.c1 = {2, 0, a1}; d.c2 = {3, 0, a2}; dsf.push_back(d); }

        const Grown gA = applyMethod(res, clu, spk, NS, NC, dsf);
        const auto gf = applyFeatures(res, clu, fet, dsf);
        check(gf.size() == gA.res.size(), "applyFeatures row count == applyMethod (aligned)");
        auto rowEqV = [&](const std::vector<int64_t>& r, const std::vector<int64_t>& m, double s) {
            if (r.size() != D) return false;
            for (std::size_t j = 0; j < D; ++j) if (r[j] != (int64_t)std::llround(s * m[j])) return false;
            return true;
        };
        int okU2 = 0, okU3 = 0, pass = 0;
        for (std::size_t o = 0; o < gA.res.size(); ++o) {
            if (gA.res[o] == collTime && gA.clu[o] == 2 && rowEqV(gf[o], MF2, a1)) ++okU2;  // f=a1*MF2
            if (gA.res[o] == collTime && gA.clu[o] == 3 && rowEqV(gf[o], MF3, a2)) ++okU3;  // f=a2*MF3
            if (gA.clu[o] == 2 && gA.res[o] == 10 && gf[o] == MF2) ++pass;                   // a passthrough row
        }
        check(okU2 == 1 && okU3 == 1, "new feature rows = constituent features (exact at tau=0)");
        check(pass == 1, "passthrough feature rows carried over unchanged");
    }

    // ── fitPair: two-template matching pursuit against a fixed basis pair ────
    // Orthogonal templates (A on ch0, B on ch1) so the greedy two-pass fit
    // recovers the components EXACTLY (modulo int16 rounding of the collision).
    {
        const int uA = 2, uB = 3, mShift = 3;
        std::vector<float> FA(REC(), 0.0f), FB(REC(), 0.0f);
        FA[2 * NC + 0] = 100.0f; FA[1 * NC + 0] = 40.0f;   // A: channel 0 only
        FB[2 * NC + 1] = 80.0f;  FB[3 * NC + 1] = 30.0f;    // B: channel 1 only

        const double aA = 2.0, aB = 1.5; const int tA = 1, tB = -1;
        std::vector<float> rA, rB;
        roll0(FA, NS, NC, tA, rA);
        roll0(FB, NS, NC, tB, rB);
        std::vector<int16_t> coll(REC());
        for (std::size_t j = 0; j < REC(); ++j)
            coll[j] = static_cast<int16_t>(std::nearbyint(aA * rA[j] + aB * rB[j]));

        const std::vector<int64_t> at = {0};
        std::vector<Decomp> ds = fitPair(coll, NS, NC, at, uA, FA, uB, FB, mShift, 0.25);
        check(ds.size() == 1, "fitPair resolves the collision");
        if (ds.size() == 1) {
            const Component& c1 = ds[0].c1; const Component& c2 = ds[0].c2;
            // by unit, regardless of which came out stronger
            const Component& cA = (c1.unit == uA) ? c1 : c2;
            const Component& cB = (c1.unit == uB) ? c1 : c2;
            check(cA.unit == uA && cB.unit == uB, "both basis units present in the fit");
            check(cA.tau == tA && cB.tau == tB, "shifts recovered exactly");
            check(std::fabs(cA.amp - aA) < 0.05 && std::fabs(cB.amp - aB) < 0.05, "amplitudes recovered");
        }

        // A pure single-unit spike (only A) is NOT a collision: pass-2 amplitude
        // collapses to ~0, so the positivity gate rejects it.
        std::vector<int16_t> pureA(REC());
        for (std::size_t j = 0; j < REC(); ++j) pureA[j] = static_cast<int16_t>(std::nearbyint(aA * rA[j]));
        check(fitPair(pureA, NS, NC, at, uA, FA, uB, FB, mShift, 0.25).empty(),
              "pure single-unit spike rejected (not a two-component collision)");

        // Unexplainable waveform (energy off both templates) -> residFrac high -> rejected.
        std::vector<int16_t> junk(REC(), 0);
        junk[0 * NC + 0] = 500; junk[4 * NC + 1] = -500;   // nothing like A@ch0-peak or B@ch1-peak
        check(fitPair(junk, NS, NC, at, uA, FA, uB, FB, mShift, 0.25).empty(),
              "high-residual waveform rejected");

        // Amplitude gate: tightening B's band to exclude its true p2p rejects the fit.
        const double bPP = peakToPeak(FB, aB);                 // the accepted component's p2p
        AmpGate gA{}, gBbad{bPP + 10.0, bPP + 20.0};           // band entirely above the true value
        check(fitPair(coll, NS, NC, at, uA, FA, uB, FB, mShift, 0.25, gA, gBbad).empty(),
              "amplitude-percentile gate rejects an out-of-band component");
    }

    std::printf("%s: %d checks, %d failed\n", (g_fail ? "FAIL" : "OK"), g_ran, g_fail);
    return g_fail ? 1 : 0;
}
