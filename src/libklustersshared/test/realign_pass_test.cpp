// realign_pass_test.cpp -- realign_pass::clusterShifts against the code it was lifted from.
//
// The pass loop was a part of KlustersDoc::realignSpikes until 2026-11-24.  The copy of
// that loop below (frozenLoop, realignKernelWeights) is the text of klustersdoc_realign.cpp
// at commit 554a058, unchanged but for M_PI written out: the reference the shared function
// must reproduce exactly -- every shift, every score, every rolled sample -- over the
// thresholds, pass counts, channel masks and correlation windows the realign offers.
// DO NOT tidy the frozen copy; it is the record of the old behaviour.  If the realign is
// changed on purpose, change the shared function and replace this comparison by fixed
// expectations in the same patch.
//
// No Qt, no test framework: prints what failed and returns non-zero.

#include "realign_pass.h"
#include "realign_xcorr.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                              std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// Stands in for the realign's log: keeps the numbers ("  iter <k>: <moved> shifted").
struct NumberLog {
    std::vector<int> numbers;
    NumberLog& operator<<(int v) { numbers.push_back(v); return *this; }
    template <class T> NumberLog& operator<<(const T&) { return *this; }
};

// ---- frozen copy: begin ---------------------------------------------------------------
enum class RealignKernel { None, Gauss, Hann, Template };

std::vector<float> realignKernelWeights(RealignKernel kernel, int kernelWidth,
                                               int peakSamp0, int nSamp, int nChan,
                                               const int16_t* tmpl)
{
    std::vector<float> w;
    if (kernel == RealignKernel::None || nSamp <= 0) return w;
    w.assign(static_cast<size_t>(nSamp), 0.0f);
    if (kernel == RealignKernel::Template) {
        std::vector<double> e(static_cast<size_t>(nSamp), 0.0);
        for (int ch = 0; ch < nChan; ++ch)
            for (int s = 0; s < nSamp; ++s) {
                const double v = static_cast<double>(tmpl[ch * nSamp + s]);
                e[static_cast<size_t>(s)] += v * v;
            }
        double mx = 0.0;
        for (int s = 0; s < nSamp; ++s) {
            double acc = 0.0; int n = 0;
            for (int k = -1; k <= 1; ++k) {
                const int t = s + k;
                if (t >= 0 && t < nSamp) { acc += e[static_cast<size_t>(t)]; ++n; }
            }
            w[static_cast<size_t>(s)] = static_cast<float>(acc / std::max(n, 1));
            mx = std::max(mx, static_cast<double>(w[static_cast<size_t>(s)]));
        }
        if (!(mx > 0.0)) { w.clear(); return w; }           // a zero template: no window
        for (int s = 0; s < nSamp; ++s)
            w[static_cast<size_t>(s)] = std::max(0.02f, static_cast<float>(w[static_cast<size_t>(s)] / mx));
        return w;
    }
    const double hw = std::max(1, kernelWidth);
    for (int s = 0; s < nSamp; ++s) {
        const double d = static_cast<double>(s - peakSamp0);
        if (kernel == RealignKernel::Gauss) {
            const double sig = hw / 2.0;
            w[static_cast<size_t>(s)] = static_cast<float>(std::exp(-0.5 * (d / sig) * (d / sig)));
        } else {                                              // Hann
            w[static_cast<size_t>(s)] = (std::abs(d) <= hw)
                ? static_cast<float>(0.5 * (1.0 + std::cos(3.14159265358979323846 * d / hw))) : 0.0f;
        }
    }
    return w;
}

// The mean waveform of the in-memory buffer (channel-major, spkElems wide): what

bool frozenLoop(std::vector<int16_t>& wavBuf, int64_t N, int nChan, int nSamp, int peakSamp0,
                int maxShift, float minScore, int nIter, int nTopChan,
                RealignKernel kernel, int kernelWidth,
                std::vector<int>& cumShift, std::vector<float>& bestScore, std::vector<int>& movedPerPass)
{
    const size_t spkElems = static_cast<size_t>(nChan) * static_cast<size_t>(nSamp);
    cumShift.assign(static_cast<size_t>(N), 0);
    bestScore.assign(static_cast<size_t>(N), 0.0f);
    NumberLog log;
    for (int iter = 0; iter < nIter; ++iter) {
        // Build mean template from current wavBuf
        std::vector<int64_t> acc(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e)
                acc[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> tmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e)
            tmpl[e] = static_cast<int16_t>(acc[e] / N);

        // Pre-align template: shift so its peak lands at peakSamp0.
        // Without this the xcorr moves every spike away from the true peak.
        //
        // Top-K channel selection: when nTopChan > 0 and < nChan, select the
        // nTopChan channels with the largest absolute amplitude in the
        // template.  Energy for peak detection is computed only on those.
        // The same mask is then applied to the template and all waveform
        // buffers (non-top channels zeroed) so the xcorr ignores them too.
        // This excludes collision / residual activity on non-primary
        // channels from influencing alignment.
        std::vector<uint8_t> chanUse(static_cast<size_t>(nChan), 1);
        if (nTopChan > 0 && nTopChan < nChan) {
            std::vector<std::pair<int64_t,int>> chanAmp(static_cast<size_t>(nChan));
            for (int ch = 0; ch < nChan; ++ch) {
                int64_t pk = 0;
                for (int t = 0; t < nSamp; ++t) {
                    const int64_t v = std::abs(static_cast<int64_t>(
                        tmpl[static_cast<size_t>(ch * nSamp + t)]));
                    if (v > pk) pk = v;
                }
                chanAmp[static_cast<size_t>(ch)] = {pk, ch};
            }
            std::partial_sort(chanAmp.begin(),
                              chanAmp.begin() + nTopChan,
                              chanAmp.end(),
                              [](const auto& a, const auto& b){
                                  return a.first > b.first;
                              });
            std::fill(chanUse.begin(), chanUse.end(), 0);
            for (int k = 0; k < nTopChan; ++k)
                chanUse[static_cast<size_t>(chanAmp[static_cast<size_t>(k)].second)] = 1;
        }

        {
            int    tmplPeak = 0;
            double bestAmp  = -1.0;
            for (int t = 0; t < nSamp; ++t) {
                double amp = 0.0;
                for (int ch = 0; ch < nChan; ++ch)
                    if (chanUse[static_cast<size_t>(ch)])
                        amp += std::abs(static_cast<double>(
                                   tmpl[static_cast<size_t>(ch * nSamp + t)]));
                if (amp > bestAmp) { bestAmp = amp; tmplPeak = t; }
            }
            const int tShift = peakSamp0 - tmplPeak;
            if (tShift != 0) {
                // To move the peak from tmplPeak to peakSamp0: src = (t - tShift + N) % N
                std::vector<int16_t> shifted(spkElems);
                for (int ch = 0; ch < nChan; ++ch)
                    for (int t = 0; t < nSamp; ++t) {
                        const int src = (t - tShift + nSamp) % nSamp;
                        shifted[static_cast<size_t>(ch * nSamp + t)] =
                            tmpl[static_cast<size_t>(ch * nSamp + src)];
                    }
                tmpl = std::move(shifted);
            }
        }

        // Apply top-K mask to a SCRATCH copy for the xcorr call only.
        // Mutating wavBuf would destroy data needed for the next iteration's
        // template build, the final xcorr pass, and the post-realign mean.
        // Same reasoning for tmpl — keep the original, mask a copy.
        //
        // Layout is channel-major [ch * nSamp + s]; zeroing ranges of
        // contiguous samples for masked-out channels gives the xcorr
        // dispatcher zeros on both sides of the correlation for those
        // channels, so they contribute nothing at any lag.
        const int16_t* xcorrWav = wavBuf.data();
        const int16_t* xcorrTmpl = tmpl.data();
        std::vector<int16_t> wavMasked;
        std::vector<int16_t> tmplMasked;
        if (nTopChan > 0 && nTopChan < nChan) {
            tmplMasked = tmpl;   // copy
            wavMasked  = wavBuf; // copy (N × spkElems int16 — cheap vs. xcorr)
            for (int ch = 0; ch < nChan; ++ch) {
                if (chanUse[static_cast<size_t>(ch)]) continue;
                std::fill(tmplMasked.begin() + ch * nSamp,
                          tmplMasked.begin() + (ch + 1) * nSamp,
                          int16_t(0));
                for (int64_t i = 0; i < N; ++i) {
                    int16_t* row = wavMasked.data()
                                 + static_cast<ptrdiff_t>(i)
                                 * static_cast<ptrdiff_t>(spkElems);
                    std::fill(row + ch * nSamp,
                              row + (ch + 1) * nSamp,
                              int16_t(0));
                }
            }
            xcorrWav  = wavMasked.data();
            xcorrTmpl = tmplMasked.data();
        }

        std::vector<int>   sh(static_cast<size_t>(N), 0);
        std::vector<float> sc(static_cast<size_t>(N), 0.0f);
        // The correlation window for this pass, on the template as rolled (its
        // peak at peakSamp0) and masked (the template kernel sees the top-K
        // channels only, like the correlation).
        const std::vector<float> kw = realignKernelWeights(kernel, kernelWidth, peakSamp0,
                                                           nSamp, nChan, xcorrTmpl);
        int rc = XcorrDispatch::computeWeighted(
            xcorrWav, xcorrTmpl, kw.empty() ? nullptr : kw.data(),
            static_cast<int>(N), nChan, nSamp,
            maxShift, minScore, sh.data(), sc.data());
        if (rc != 0) {
            log << "ERROR: XcorrDispatch rc=" << rc << "\n";
            return false;
        }

        int changed = 0;
        for (int64_t i = 0; i < N; ++i) {
            const int s = sh[static_cast<size_t>(i)];
            if (s == 0) continue;
            int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            // Circular shift: newSpike[t] = oldSpike[(t + shift) % N].
            // Derivation: score(lag) = Σ tmpl[s]·spike[(s+lag)%N] peaks at
            // lag = bestLag when spike[(s+bestLag)%N] ≈ tmpl[s], meaning the
            // spike peak is late by bestLag.  Rolling forward by bestLag
            // brings the peak back to the template position.  The timestamp
            // must move in the opposite direction (see newTs below).
            std::vector<int16_t> tmp(spkElems);
            for (int t = 0; t < nSamp; ++t) {
                const int src = (t + s + nSamp) % nSamp;
                for (int ch = 0; ch < nChan; ++ch)
                    tmp[static_cast<size_t>(ch * nSamp + t)] =
                        w[static_cast<size_t>(ch * nSamp + src)];
            }
            std::copy(tmp.begin(), tmp.end(), w);
            cumShift[static_cast<size_t>(i)] += s;
            bestScore[static_cast<size_t>(i)] = sc[static_cast<size_t>(i)];
            ++changed;
        }
        log << "  iter " << (iter+1) << ": " << changed << " shifted\n";
        if (changed == 0) break;
    }

    movedPerPass.clear();
    for (size_t k = 1; k < log.numbers.size(); k += 2) movedPerPass.push_back(log.numbers[k]);
    return true;
}
// ---- frozen copy: end -----------------------------------------------------------------

// A small deterministic generator: the two implementations get the same buffers whatever
// the platform's <random> does.
struct Rng {
    uint64_t s;
    uint32_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<uint32_t>(s >> 33); }
    double uniform() { return (next() + 0.5) / 2147483648.0; }                 // (0, 1)
    double normal() { double a = 0.0; for (int i = 0; i < 12; ++i) a += uniform(); return a - 6.0; }
    int between(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
};

struct Cluster { std::string name; int nChan, nSamp, peak, n; std::vector<int16_t> wav; std::vector<int> jitter; };

// One unit, channel-major: a biphasic spike over the channels, sampled per spike at a
// jittered time (a re-sampling, not a roll), with amplitude variation, coloured noise and,
// for a fraction of the spikes, a second spike elsewhere in the window.
Cluster make(const std::string& name, uint64_t seed, int nChan, int nSamp, int peak, int n,
             double amp, double noise, int maxJitter, int offset, double collisions)
{
    Cluster c{ name, nChan, nSamp, peak, n, {}, {} };
    c.wav.resize(static_cast<size_t>(n) * static_cast<size_t>(nChan) * static_cast<size_t>(nSamp));
    c.jitter.resize(static_cast<size_t>(n));
    Rng r{ seed };
    auto wave = [](double t, double width) {
        return -std::exp(-0.5 * t * t / (width * width))
               + 0.35 * std::exp(-0.5 * std::pow((t - 4.0 * width) / (2.2 * width), 2));
    };
    for (int i = 0; i < n; ++i) {
        const int jit = r.between(-maxJitter, maxJitter) + offset;
        const double sub = r.uniform() - 0.5, a = 1.0 + 0.12 * r.normal();
        const bool coll = r.uniform() < collisions;
        const double collAt = peak + (r.uniform() < 0.5 ? -1.0 : 1.0) * (4.0 + 8.0 * r.uniform());
        c.jitter[static_cast<size_t>(i)] = jit;
        for (int ch = 0; ch < nChan; ++ch) {
            const double chAmp  = amp * std::exp(-0.5 * std::pow((ch - nChan * 0.35) / 1.6, 2));
            const double chAmp2 = amp * 0.9 * std::exp(-0.5 * std::pow((ch - nChan * 0.75) / 1.4, 2));
            double prev = 0.0;
            for (int s = 0; s < nSamp; ++s) {
                double v = a * chAmp * wave(s - (peak + jit + sub), 2.0 + 0.15 * ch);
                if (coll) v += chAmp2 * wave(s - collAt, 2.4);
                prev = 0.6 * prev + noise * r.normal();
                v += prev;
                c.wav[(static_cast<size_t>(i) * static_cast<size_t>(nChan) + static_cast<size_t>(ch))
                      * static_cast<size_t>(nSamp) + static_cast<size_t>(s)]
                    = static_cast<int16_t>(std::lround(std::clamp(v, -32000.0, 32000.0)));
            }
        }
    }
    return c;
}

// The spread of what is left of the jitter once the shifts are applied.
double residualSd(const Cluster& c, const std::vector<int>& shift)
{
    std::vector<int> r(static_cast<size_t>(c.n));
    for (int i = 0; i < c.n; ++i) r[static_cast<size_t>(i)] = c.jitter[static_cast<size_t>(i)] - shift[static_cast<size_t>(i)];
    std::vector<int> t = r;
    std::nth_element(t.begin(), t.begin() + c.n / 2, t.end());
    const int med = t[static_cast<size_t>(c.n / 2)];
    double ss = 0.0;
    for (int v : r) ss += static_cast<double>(v - med) * (v - med);
    return std::sqrt(ss / c.n);
}

const char* kernelName(realign_pass::Kernel k)
{
    switch (k) {
    case realign_pass::Kernel::None:     return "none";
    case realign_pass::Kernel::Gauss:    return "gauss";
    case realign_pass::Kernel::Hann:     return "hann";
    case realign_pass::Kernel::Template: return "template";
    }
    return "?";
}

RealignKernel frozenKernel(realign_pass::Kernel k)
{
    switch (k) {
    case realign_pass::Kernel::None:     return RealignKernel::None;
    case realign_pass::Kernel::Gauss:    return RealignKernel::Gauss;
    case realign_pass::Kernel::Hann:     return RealignKernel::Hann;
    case realign_pass::Kernel::Template: return RealignKernel::Template;
    }
    return RealignKernel::None;
}

std::vector<Cluster> clusters()
{
    const int nChan = 8, nSamp = 32, peak = 16;
    std::vector<Cluster> cs;
    cs.push_back(make("high SNR",        1, nChan, nSamp, peak, 250, 900, 60, 3,  0, 0.0));
    cs.push_back(make("medium SNR",      2, nChan, nSamp, peak, 250, 400, 60, 3,  0, 0.0));
    cs.push_back(make("low SNR",         3, nChan, nSamp, peak, 250, 180, 60, 3,  0, 0.0));
    cs.push_back(make("20% collisions",  4, nChan, nSamp, peak, 250, 400, 60, 3,  0, 0.2));
    cs.push_back(make("off by +3",       5, nChan, nSamp, peak, 250, 400, 60, 2,  3, 0.0));
    cs.push_back(make("off by -5",       6, nChan, nSamp, peak, 250, 400, 60, 2, -5, 0.0));
    cs.push_back(make("wide jitter",     7, nChan, nSamp, peak, 250, 400, 60, 6,  0, 0.0));
    cs.push_back(make("small",           8, nChan, nSamp, peak,  40, 400, 60, 3,  0, 0.0));
    cs.push_back(make("one channel",     9, 1,     nSamp, peak, 300, 400, 60, 3,  0, 0.0));
    cs.push_back(make("peak not centred", 10, 4,   40,    12,   300, 400, 60, 3,  0, 0.0));
    return cs;
}

void sameAsBefore()
{
    using realign_pass::Kernel;
    const float thresholds[] = { 0.0f, 0.10f, 0.70f };
    const int   passes[]     = { 1, 2, 4 };
    const int   tops[]       = { 0, 2, 4 };
    const struct { Kernel kernel; int width; } windows[] = {
        { Kernel::None, 1 }, { Kernel::Gauss, 6 }, { Kernel::Hann, 8 }, { Kernel::Template, 1 } };
    const int maxShift = 8;
    int cases = 0, moved = 0;
    for (const Cluster& c : clusters())
        for (float minScore : thresholds)
            for (int nIter : passes)
                for (int top : tops)
                    for (const auto& w : windows) {
                        std::vector<int16_t> a = c.wav, b = c.wav;
                        std::vector<int> shiftA, movedA; std::vector<float> scoreA;
                        const bool okA = frozenLoop(a, c.n, c.nChan, c.nSamp, c.peak, maxShift, minScore, nIter, top,
                                                    frozenKernel(w.kernel), w.width, shiftA, scoreA, movedA);
                        realign_pass::Options o;
                        o.peakSample = c.peak; o.maxShift = maxShift; o.minScore = minScore; o.iterations = nIter;
                        o.topChannels = top; o.kernel = w.kernel; o.kernelWidth = w.width;
                        realign_pass::Result res;
                        const bool okB = realign_pass::clusterShifts(b.data(), c.n, c.nChan, c.nSamp, o, res);
                        ++cases;
                        const bool same = okA && okB && shiftA == res.shift && scoreA == res.score && a == b && movedA == res.moved;
                        CHECK(same, "%s, threshold %.2f, %d pass(es), top %d, window %s: differs from the old loop "
                                    "(ok %d/%d, shifts %s, scores %s, waveforms %s, passes %s)",
                              c.name.c_str(), static_cast<double>(minScore), nIter, top, kernelName(w.kernel),
                              okA, okB, shiftA == res.shift ? "same" : "DIFFER",
                              scoreA == res.score ? "same" : "DIFFER", a == b ? "same" : "DIFFER",
                              movedA == res.moved ? "same" : "DIFFER");
                        for (int v : res.shift) moved += (v != 0);
                    }
    std::printf("same as before: %d cases, %d moved spikes compared\n", cases, moved);
    CHECK(moved > 100000, "the comparison moved only %d spikes: it is not testing much", moved);
}

void alignsWhatItShould()
{
    for (const Cluster& c : clusters()) {
        realign_pass::Options o;
        o.peakSample = c.peak; o.maxShift = 8; o.minScore = 0.10f; o.iterations = 2;
        std::vector<int16_t> w = c.wav;
        realign_pass::Result res;
        CHECK(realign_pass::clusterShifts(w.data(), c.n, c.nChan, c.nSamp, o, res), "%s: failed", c.name.c_str());
        const double before = residualSd(c, std::vector<int>(static_cast<size_t>(c.n), 0));
        const double after  = residualSd(c, res.shift);
        std::printf("  %-18s jitter sd %.2f -> %.2f\n", c.name.c_str(), before, after);
        if (c.name == "low SNR" || c.name == "20% collisions") continue;      // reported, not required
        CHECK(after < 0.6 * before, "%s: jitter sd %.2f -> %.2f, expected a clear drop", c.name.c_str(), before, after);
    }
}

void edges()
{
    realign_pass::Result res;
    res.shift.assign(3, 7);
    realign_pass::Options o;
    CHECK(realign_pass::clusterShifts(nullptr, 0, 4, 32, o, res) && res.shift.empty() && res.moved.empty(),
          "no spikes: expected success and empty results");

    const Cluster c = make("x", 11, 4, 32, 16, 50, 400, 60, 3, 0, 0.0);
    std::vector<int16_t> w = c.wav;
    o.peakSample = 16; o.maxShift = 8; o.iterations = 0;
    CHECK(realign_pass::clusterShifts(w.data(), c.n, c.nChan, c.nSamp, o, res), "zero passes: failed");
    CHECK(w == c.wav && res.moved.empty() && std::all_of(res.shift.begin(), res.shift.end(), [](int v) { return v == 0; }),
          "zero passes: something moved");

    // rollSpike: new[t] = old[(t + shift) mod nSamp]; the inverse shift restores the spike.
    std::vector<int16_t> spike = { 0, 1, 2, 3, 4,   10, 11, 12, 13, 14 };
    const std::vector<int16_t> orig = spike;
    realign_pass::rollSpike(spike.data(), 2, 5, 2);
    CHECK((spike == std::vector<int16_t>{ 2, 3, 4, 0, 1,   12, 13, 14, 10, 11 }), "rollSpike by +2 is wrong");
    realign_pass::rollSpike(spike.data(), 2, 5, -2);
    CHECK(spike == orig, "rollSpike by -2 does not undo +2");

    // kernelWeights: none and a zero template give no window; gauss and hann are 1 at the peak.
    const std::vector<int16_t> zero(64, 0);
    CHECK(realign_pass::kernelWeights(realign_pass::Kernel::None, 4, 8, 16, 4, zero.data()).empty(), "none: not empty");
    CHECK(realign_pass::kernelWeights(realign_pass::Kernel::Template, 4, 8, 16, 4, zero.data()).empty(),
          "template of a zero template: not empty");
    const std::vector<float> g = realign_pass::kernelWeights(realign_pass::Kernel::Gauss, 4, 8, 16, 4, zero.data());
    const std::vector<float> h = realign_pass::kernelWeights(realign_pass::Kernel::Hann, 4, 8, 16, 4, zero.data());
    CHECK(g.size() == 16 && g[8] == 1.0f && g[7] == g[9] && g[0] < 0.01f, "gauss window is wrong");
    CHECK(h.size() == 16 && h[8] == 1.0f && h[3] == 0.0f && h[13] == 0.0f && h[6] == h[10], "hann window is wrong");
}

} // namespace

int main()
{
    std::printf("xcorr back end: %s\n", XcorrDispatch::backendName());
    sameAsBefore();
    alignsWhatItShould();
    edges();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("ok\n");
    return 0;
}
