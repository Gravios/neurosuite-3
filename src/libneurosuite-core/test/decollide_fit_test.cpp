// decollide_fit_test.cpp — a collision row with its partner subtracted
// (decollide_fit.hpp) and the sparse .spkc that holds such rows (neurofileio).
//
//  * shifted(): whole shifts move samples exactly and zero-fill; a half-sample
//    shift there and back returns a smooth template to within 2 %;
//  * fitPartner(): a planted partner (offset 11.4 samples, amplitude 0.8) on a
//    noisy own spike is found from a stored offset 3 samples too early AND from
//    one 3 too late -- shift within 0.2 sample, amplitude within 5 % -- and the
//    corrected row is the own spike again (residual at the noise level);
//  * NEGATIVE CONTROLS: a row with no partner in it, and a row whose "partner"
//    is the wrong unit's template, are not accepted and are left as they were;
//  * the joint fit matters: with a partner overlapping the own trough, fitting
//    the partner alone (no own template) gets its amplitude wrong by more than
//    the joint fit does;
//  * homeClusters / splitRow: the cluster a class is and the own class of a row,
//    read off the membership -- after a renumbering too;
//  * correctRow(): two partners, both subtracted; a partner without a template
//    is skipped;
//  * .spkc: round trip, lookup, and what the reader and the writer refuse.
// Self-contained, assert-based.

#include "neurosuite/core/decollide_fit.hpp"
#include "neurosuite/core/neurofileio.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran; if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

namespace df = neurosuite::decollide_fit;
namespace nf = neurofileio;

static const int NS = 52, NC = 8, PEAK = 26;

// A spike: a trough at the peak sample with a later bump, strongest at `depth`.
static std::vector<float> templ(double depth, double amp, double width)
{
    std::vector<float> t(static_cast<std::size_t>(NS) * NC);
    for (int s = 0; s < NS; ++s) {
        const double x = (s - PEAK) / (4.0 * width);
        const double y = (s - PEAK - 9.0 * width) / (5.0 * width);
        const double w = -std::exp(-0.5 * x * x) + 0.3 * std::exp(-0.5 * y * y);
        for (int c = 0; c < NC; ++c) {
            const double d = (c * 20.0 - depth) / 28.0;
            t[static_cast<std::size_t>(s) * NC + c] = static_cast<float>(amp * w * (std::exp(-0.5 * d * d) + 0.05));
        }
    }
    return t;
}

static double energy(const std::vector<float>& a, const std::vector<float>& b)
{
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) { const double d = static_cast<double>(a[i]) - b[i]; s += d * d; }
    return s;
}

int main()
{
    const std::size_t REC = static_cast<std::size_t>(NS) * NC;
    const std::vector<float> A = templ(40.0, 400.0, 1.0), B = templ(110.0, 320.0, 0.8), C = templ(70.0, 300.0, 1.3);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 12.0f);
    const double noiseEnergy = 12.0 * 12.0 * REC;

    // ── shifted ──
    {
        std::vector<float> q, back;
        df::shifted(A.data(), NS, NC, 3.0, q);
        bool exact = true;
        for (int s = 0; s < NS; ++s)
            for (int c = 0; c < NC; ++c) {
                const float want = s >= 3 ? A[static_cast<std::size_t>(s - 3) * NC + c] : 0.0f;
                if (q[static_cast<std::size_t>(s) * NC + c] != want) exact = false;
            }
        check(exact, "shifted: +3 samples moves every sample 3 later and zero-fills the start");
        df::shifted(A.data(), NS, NC, -2.0, q);
        check(q[0] == A[static_cast<std::size_t>(2) * NC] && q[static_cast<std::size_t>(NS - 1) * NC] == 0.0f,
              "shifted: -2 samples moves earlier and zero-fills the end");
        df::shifted(A.data(), NS, NC, 0.5, q);
        df::shifted(q.data(), NS, NC, -0.5, back);
        std::vector<float> zero(REC, 0.0f);
        check(energy(back, A) < 0.02 * 0.02 * energy(A, zero), "shifted: half a sample there and back is the template within 2 %");
    }

    // ── a planted partner is found and removed ──
    {
        std::vector<float> q, w(REC), own(REC);
        df::shifted(B.data(), NS, NC, 11.4, q);
        for (std::size_t i = 0; i < REC; ++i) { own[i] = 1.05f * A[i] + noise(rng); w[i] = own[i] + 0.8f * q[i]; }
        const df::Fit f = df::fitPartner(w.data(), A.data(), B.data(), NS, NC, 8);     // stored offset 3.4 off
        check(f.accepted, "planted partner: accepted");
        check(std::fabs(f.shift - 11.4) < 0.2 && f.tau == 11, "planted partner: shift within 0.2 sample, tau 11");
        check(std::fabs(f.amp - 0.8) < 0.04, "planted partner: amplitude within 5 %");
        check(std::fabs(f.ampOwn - 1.05) < 0.05, "planted partner: the own amplitude comes out of the joint fit too");
        std::vector<float> corrected = w;
        df::subtractPartner(corrected, B.data(), NS, NC, f);
        check(energy(corrected, own) < 0.25 * noiseEnergy, "planted partner: the corrected row is the own spike (error under a quarter of the noise energy)");
        check(energy(w, own) > 20.0 * energy(corrected, own), "... which the uncorrected row was not (20 x further from it)");
        // the stored offset on the OTHER side: too late by 2.6 samples
        const df::Fit late = df::fitPartner(w.data(), A.data(), B.data(), NS, NC, 14);
        check(late.accepted && std::fabs(late.shift - 11.4) < 0.2 && late.tau == 11,
              "planted partner: found from a stored offset that is too LATE as well as from one too early");
        // out of reach: the stored offset is 9 samples off and the radius is 4
        const df::Fit far = df::fitPartner(w.data(), A.data(), B.data(), NS, NC, 2);
        check(!far.accepted || std::fabs(far.shift - 11.4) > 2.0, "a stored offset beyond the radius does not find the partner's place");
    }

    // ── negative controls ──
    {
        std::vector<float> w(REC);
        int accepted = 0;
        for (int rep = 0; rep < 200; ++rep) {
            for (std::size_t i = 0; i < REC; ++i) w[i] = A[i] + noise(rng);
            if (df::fitPartner(w.data(), A.data(), B.data(), NS, NC, 10).accepted) ++accepted;
        }
        check(accepted == 0, "NEGATIVE CONTROL: no partner in the row -> none accepted in 200 rows");
        std::vector<float> q;
        df::shifted(B.data(), NS, NC, 12.0, q);
        accepted = 0;
        std::vector<float> keep;
        for (int rep = 0; rep < 200; ++rep) {
            for (std::size_t i = 0; i < REC; ++i) w[i] = A[i] + 0.15f * q[i] + noise(rng);   // too weak to be a spike of B
            keep = w;
            const df::Fit f = df::fitPartner(w.data(), A.data(), B.data(), NS, NC, 12);
            if (f.accepted) ++accepted;
            df::subtractPartner(w, B.data(), NS, NC, f);
            if (!f.accepted && w != keep) ++accepted;                                        // a rejected fit must not touch the row
        }
        check(accepted == 0, "NEGATIVE CONTROL: a partner at 0.15 of its template is not accepted, and the row is left alone");
    }

    // ── the joint fit ──
    {
        std::vector<float> q, w(REC);
        df::shifted(C.data(), NS, NC, 3.0, q);                 // overlaps the own trough
        for (std::size_t i = 0; i < REC; ++i) w[i] = A[i] + 0.9f * q[i] + noise(rng);
        df::Params p; p.radius = 0;
        const df::Fit joint = df::fitPartner(w.data(), A.data(), C.data(), NS, NC, 3, p);
        const df::Fit alone = df::fitPartner(w.data(), nullptr, C.data(), NS, NC, 3, p);
        check(std::fabs(joint.amp - 0.9) < 0.05, "overlapping partner: the joint fit has its amplitude within 0.05");
        check(std::fabs(alone.amp - 0.9) > 3.0 * std::fabs(joint.amp - 0.9) && std::fabs(alone.amp - 0.9) > 0.1,
              "... fitting the partner alone does not (it absorbs the own spike)");
    }

    // ── homeClusters / splitRow ──
    {
        nf::EapFile e;
        e.nSpikes = 10; e.nClasses = 4; e.ok = true;
        e.cells.assign(40, nf::EAP_ABSENT);
        auto set = [&](int row, int cls, int off) { e.cells[static_cast<std::size_t>(row) * 4 + cls] = static_cast<int8_t>(off); };
        // class 0 lives in cluster 5 (rows 0-3), class 2 in cluster 9 (rows 4-6); class 1 has no row of its own
        for (int r = 0; r < 4; ++r) set(r, 0, 0);
        for (int r = 4; r < 7; ++r) set(r, 2, 0);
        set(7, 0, 0); set(7, 2, 13);                            // a collision owned by cluster 5
        set(8, 2, 1); set(8, 0, -9);                            // a collision owned by cluster 9
        set(9, 0, 6); set(9, 1, -2);                            // a row in the noise with two classes
        std::vector<int> clu = {5, 5, 5, 9, 9, 9, 9, 5, 9, 1};
        std::vector<int> home = df::homeClusters(e, clu);
        check(home == std::vector<int>({5, -1, 9, -1}), "homeClusters: class 0 -> 5 (3 of 4), class 2 -> 9, classes without a row of their own -> -1");
        df::Member own; std::vector<df::Member> partners;
        check(df::splitRow(e, 7, clu[7], home, own, partners) && own.classId == 0 && partners.size() == 1
              && partners[0].classId == 2 && partners[0].offset == 13, "splitRow: own = the class of the row's cluster, partner at +13");
        check(df::splitRow(e, 8, clu[8], home, own, partners) && own.classId == 2 && partners[0].classId == 0
              && partners[0].offset == -9, "splitRow: the other way round");
        check(df::splitRow(e, 9, clu[9], home, own, partners) && own.classId == 1 && partners[0].classId == 0,
              "splitRow: a row whose cluster is nobody's home -> own = the class nearest offset 0");
        check(!df::splitRow(e, 0, clu[0], home, own, partners) && own.classId == 0 && partners.empty(),
              "splitRow: a single-class row has an own class and no partner");
        {   // most of a class's rows sit OUTSIDE its unit (a cluster of one-unit spikes of every unit)
            nf::EapFile big;
            big.nSpikes = 200; big.nClasses = 2; big.ok = true;
            big.cells.assign(400, nf::EAP_ABSENT);
            std::vector<int> c2(200);
            for (int r = 0; r < 200; ++r) {
                big.cells[static_cast<std::size_t>(r) * 2 + (r % 2)] = 0;      // class 0 on even rows, class 1 on odd
                c2[static_cast<std::size_t>(r)] = r < 80 ? (r % 2 ? 9 : 5) : 77;   // 40 rows each in units 5 and 9, 120 in cluster 77
            }
            std::vector<int> h = df::homeClusters(big, c2);
            check(h == std::vector<int>({77, 77}), "homeClusters alone: the mixed cluster holds most rows of both classes");
            df::preferProvenance(h, big, c2, {5, 9});
            check(h == std::vector<int>({5, 9}), "preferProvenance: the unit each class was made from, as it still holds 40 of its rows");
            h = df::homeClusters(big, c2);
            df::preferProvenance(h, big, c2, {31, -1});           // a provenance that holds none of the class
            check(h == std::vector<int>({77, 77}), "preferProvenance: a provenance that no longer holds the class changes nothing");
            h = df::homeClusters(big, c2);
            df::preferProvenance(h, big, c2, {5, 9}, 41);
            check(h == std::vector<int>({77, 77}), "preferProvenance: fewer rows than minRows is not enough");
        }
        for (int& c : clu) c = c == 5 ? 31 : c == 9 ? 12 : c;  // the clusters are renumbered
        home = df::homeClusters(e, clu);
        check(home[0] == 31 && home[2] == 12 && df::splitRow(e, 7, clu[7], home, own, partners) && own.classId == 0,
              "after a renumbering the homes follow and the own class is the same");
    }

    // ── correctRow: two partners; one without a template ──
    {
        std::vector<float> qb, qc, w(REC), own(REC);
        df::shifted(B.data(), NS, NC, 14.0, qb);
        df::shifted(C.data(), NS, NC, -12.0, qc);
        for (std::size_t i = 0; i < REC; ++i) { own[i] = A[i] + noise(rng); w[i] = own[i] + 0.9f * qb[i] + 1.1f * qc[i]; }
        const df::Member me{0, 0};
        const std::vector<df::Member> partners = {{1, 13}, {2, -11}};
        std::map<int, std::vector<float>> T = {{0, A}, {1, B}, {2, C}};
        std::vector<df::Fit> fits;
        std::vector<float> x = w;
        check(df::correctRow(x, NS, NC, me, partners, T, fits) == 2 && fits[0].tau == 14 && fits[1].tau == -12,
              "correctRow: both partners subtracted, at 14 and -12");
        check(energy(x, own) < 0.5 * noiseEnergy, "correctRow: what is left is the own spike");
        T.erase(2);
        x = w;
        check(df::correctRow(x, NS, NC, me, partners, T, fits) == 1 && fits[0].accepted && !fits[1].accepted
              && fits[1].tau == -11, "correctRow: a partner without a template is skipped and keeps its stored offset");
        T = {{1, B}, {2, C}};                                  // the OWN unit has no template
        x = w;
        check(df::correctRow(x, NS, NC, me, partners, T, fits) == 0 && !fits[0].accepted && !fits[1].accepted
              && fits[0].tau == 13 && x == w, "correctRow: without the own unit's template nothing is subtracted");
        std::vector<int16_t> i16(REC);
        std::vector<float> big(REC, 40000.0f); big[1] = -40000.0f; big[2] = std::nanf("");
        df::toInt16(big, i16.data());
        check(i16[0] == 32767 && i16[1] == -32768 && i16[2] == 0, "toInt16 saturates and maps NaN to 0");
    }

    // ── .spkc ──
    {
        const std::string p = "decollide_fit_test.spkc";
        nf::SpkcFile f;
        f.nSpikes = 1000; f.nSamples = 4; f.nChannels = 2; f.group = 8; f.flags = 0;
        f.rows = {3, 17, 999};
        f.waveforms.resize(3 * 8);
        for (std::size_t i = 0; i < f.waveforms.size(); ++i) f.waveforms[i] = static_cast<int16_t>(100 - 7 * static_cast<int>(i));
        check(nf::writeSpkc(p, f), "writeSpkc");
        const nf::SpkcFile g = nf::readSpkc(p);
        check(g.ok && g.nSpikes == 1000 && g.nSamples == 4 && g.nChannels == 2 && g.group == 8
              && g.rows == f.rows && g.waveforms == f.waveforms, ".spkc survives the round trip");
        check(nf::spkcFind(g, 17) == 1 && nf::spkcFind(g, 3) == 0 && nf::spkcFind(g, 999) == 2
              && nf::spkcFind(g, 18) == -1 && nf::spkcFind(g, 0) == -1, "spkcFind: the position of a listed row, -1 otherwise");
        {
            std::ifstream in(p, std::ios::binary | std::ios::ate);
            check(in.tellg() == static_cast<std::streamoff>(32 + 3 * 8 + 3 * 8 * 2), ".spkc size: 32 B header + index + records");
        }
        nf::SpkcFile bad = f; bad.rows = {17, 3, 999};
        check(!nf::writeSpkc("decollide_fit_bad.spkc", bad), "writeSpkc refuses an index that is not ascending");
        bad = f; bad.rows = {3, 3, 999};
        check(!nf::writeSpkc("decollide_fit_bad.spkc", bad), "... a repeated row");
        bad = f; bad.rows = {3, 17, 1000};
        check(!nf::writeSpkc("decollide_fit_bad.spkc", bad), "... a row beyond the .spk");
        bad = f; bad.waveforms.pop_back();
        check(!nf::writeSpkc("decollide_fit_bad.spkc", bad), "... and records that do not match the index");
        {   // truncated body
            std::ifstream in(p, std::ios::binary);
            std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::ofstream out("decollide_fit_short.spkc", std::ios::binary);
            out.write(all.data(), static_cast<std::streamsize>(all.size() - 5));
        }
        check(!nf::readSpkc("decollide_fit_short.spkc").ok, "readSpkc rejects a short file");
        {   // an index out of order on disk
            std::fstream io(p, std::ios::binary | std::ios::in | std::ios::out);
            io.seekp(32);
            const int64_t v = 500;
            io.write(reinterpret_cast<const char*>(&v), 8);
        }
        check(!nf::readSpkc(p).ok, "readSpkc rejects an index that is not ascending");
        nf::SpkcFile empty; empty.nSpikes = 10; empty.nSamples = 4; empty.nChannels = 2;
        check(nf::writeSpkc(p, empty) && nf::readSpkc(p).ok && nf::readSpkc(p).rows.empty(), "an .spkc with no rows is valid");
        check(!nf::readSpkc("decollide_fit_missing.spkc").ok, "a missing file is not ok");
        std::remove(p.c_str()); std::remove("decollide_fit_short.spkc"); std::remove("decollide_fit_bad.spkc");
    }

    std::printf("%s: %d checks, %d failed\n", (g_fail ? "FAIL" : "OK"), g_ran, g_fail);
    return g_fail ? 1 : 0;
}
