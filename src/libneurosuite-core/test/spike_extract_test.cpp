// spike_extract_test.cpp — extractSpikeWindows reproduces the extractor's bytes.
//
// process_extractspikes_stderiv (-R) cuts each window from the .fil together with
// the sample before it, takes the spatial derivative of all nSamp+1 samples and
// writes the temporal first difference — so sample 0 of every window is
// sd[0] − sd[−1], not sd[0].  extractSpikeWindows must write the same bytes, and
// stderivSeed() is the seed a caller passes to applyStderivTransform for that.
//
// A synthetic .fil is cut with the reference algorithm (written out here as the
// extractor does it) and with extractSpikeWindows, for every spatial order,
// for a window at sample 0 (seed 0 by definition), windows straddling the read
// buffer, the raw (untransformed) path and the spatial-only path
// (temporalDiff = false, process_extractspikes_sdiff).  Self-contained (own main), run via
// ctest.

#include "neurosuite/core/spike_extract.hpp"
#include "neurosuite/core/stderiv_transform.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace neurosuite::core;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

// The extractor's -R algorithm for one window: SDIFF over [ws-1, ws+nSamp),
// clamp, temporal difference.  Sample-major output [s * nChan + ci].
static std::vector<int16_t> reference(const std::vector<int16_t>& fil, int totalCh,
                                      const std::vector<int>& chans, SdiffOrder order,
                                      int64_t ws, int nSamp) {
    const int nChan = static_cast<int>(chans.size());
    std::vector<int16_t> sd(static_cast<size_t>(nSamp + 1) * nChan);
    std::vector<double> row(static_cast<size_t>(nChan));
    for (int s = 0; s < nSamp + 1; ++s) {
        const int64_t t = ws - 1 + s;
        for (int ci = 0; ci < nChan; ++ci)
            row[ci] = (t >= 0) ? fil[static_cast<size_t>(t) * totalCh + chans[ci]] : 0;
        for (int ci = 0; ci < nChan; ++ci)
            sd[static_cast<size_t>(s) * nChan + ci] = (t >= 0)
                ? clampToInt16(spatialDeriv(order, row.data(), ci, nChan)) : int16_t(0);
    }
    std::vector<int16_t> out(static_cast<size_t>(nSamp) * nChan);
    for (int s = 0; s < nSamp; ++s)
        for (int ci = 0; ci < nChan; ++ci)
            out[static_cast<size_t>(s) * nChan + ci] = clampToInt16(
                static_cast<double>(sd[static_cast<size_t>(s + 1) * nChan + ci])
              - static_cast<double>(sd[static_cast<size_t>(s) * nChan + ci]));
    return out;
}

int main() {
    const int totalCh = 6, nSamp = 12, peak = 4;
    const int64_t nTotal = 4000;
    std::vector<int16_t> fil(static_cast<size_t>(nTotal) * totalCh);
    uint32_t x = 12345u;                                   // deterministic noise
    for (size_t i = 0; i < fil.size(); ++i) {
        x = x * 1664525u + 1013904223u;
        fil[i] = static_cast<int16_t>(static_cast<int>((x >> 16) % 2001) - 1000
                 + static_cast<int>(300.0 * std::sin(0.05 * static_cast<double>(i / totalCh))));
    }
    const char* path = "spike_extract_test.fil";
    std::FILE* w = std::fopen(path, "wb");
    if (!w) { std::printf("FAIL: cannot write %s\n", path); return 1; }
    std::fwrite(fil.data(), sizeof(int16_t), fil.size(), w);
    std::fclose(w);

    const std::vector<int> chans = {5, 1, 3, 0};           // a group in arbitrary order
    // window starts: 0 (seed 0), 1 (seed = sample 0), interior, the file's end
    const std::vector<int64_t> ts = {peak + 0, peak + 1, 50, 51, 777, 1500, 2999,
                                     nTotal - nSamp + peak};

    for (SdiffOrder order : {SdiffOrder::None, SdiffOrder::First,
                             SdiffOrder::Laplacian, SdiffOrder::AllPairs}) {
        for (std::size_t bufferBytes : {std::size_t(8u << 20), std::size_t(64)}) {
            SpikeWindowSpec spec;
            spec.nSamp = nSamp; spec.peakSample = peak; spec.totalNbChannels = totalCh;
            spec.groupChannels = chans.data(); spec.nChan = static_cast<int>(chans.size());
            spec.applyTransform = true; spec.order = order; spec.bufferBytes = bufferBytes;
            std::FILE* f = std::fopen(path, "rb");
            std::vector<std::vector<int16_t>> got;
            int64_t oor = -1;
            extractSpikeWindows(f, static_cast<int64_t>(ts.size()), spec,
                [&](int64_t i) { return ts[static_cast<size_t>(i)]; },
                [&](int64_t, const int16_t* r) { got.emplace_back(r, r + nSamp * chans.size()); },
                &oor);
            std::fclose(f);
            const std::string tag = "order " + std::to_string(static_cast<int>(order))
                                  + (bufferBytes < 1024 ? ", small buffer" : "");
            check(oor == 0, tag + ": no window out of range");
            for (size_t i = 0; i < ts.size(); ++i)
                check(got[i] == reference(fil, totalCh, chans, order, ts[i] - peak, nSamp),
                      tag + ": window at sample " + std::to_string(ts[i] - peak)
                      + " matches the extractor");
        }
    }

    // stderivSeed + applyStderivTransform on a window equals the reference too
    {
        const int64_t ws = 321;
        std::vector<int16_t> cm(static_cast<size_t>(nSamp) * chans.size()), lead(chans.size()),
                             seed(chans.size());
        for (int s = 0; s < nSamp; ++s)
            for (size_t ci = 0; ci < chans.size(); ++ci)
                cm[ci * nSamp + s] = fil[static_cast<size_t>(ws + s) * totalCh + chans[ci]];
        for (size_t ci = 0; ci < chans.size(); ++ci)
            lead[ci] = fil[static_cast<size_t>(ws - 1) * totalCh + chans[ci]];
        stderivSeed(SdiffOrder::AllPairs, lead.data(), static_cast<int>(chans.size()), seed.data());
        applyStderivTransform(SdiffOrder::AllPairs, cm.data(), static_cast<int>(chans.size()),
                              nSamp, cm.data(), nullptr, seed.data());
        const auto ref = reference(fil, totalCh, chans, SdiffOrder::AllPairs, ws, nSamp);
        bool same = true;
        for (int s = 0; s < nSamp; ++s)
            for (size_t ci = 0; ci < chans.size(); ++ci)
                same &= cm[ci * nSamp + s] == ref[static_cast<size_t>(s) * chans.size() + ci];
        check(same, "stderivSeed + applyStderivTransform matches the extractor");
    }

    // raw path: plain copy of the window
    {
        SpikeWindowSpec spec;
        spec.nSamp = nSamp; spec.peakSample = peak; spec.totalNbChannels = totalCh;
        spec.groupChannels = chans.data(); spec.nChan = static_cast<int>(chans.size());
        std::FILE* f = std::fopen(path, "rb");
        bool same = true;
        extractSpikeWindows(f, static_cast<int64_t>(ts.size()), spec,
            [&](int64_t i) { return ts[static_cast<size_t>(i)]; },
            [&](int64_t i, const int16_t* r) {
                const int64_t ws = ts[static_cast<size_t>(i)] - peak;
                for (int s = 0; s < nSamp; ++s)
                    for (size_t ci = 0; ci < chans.size(); ++ci)
                        same &= r[static_cast<size_t>(s) * chans.size() + ci]
                             == fil[static_cast<size_t>(ws + s) * totalCh + chans[ci]];
            });
        std::fclose(f);
        check(same, "raw windows are copied unchanged");
    }

    // temporalDiff = false: the spatial derivative only (process_extractspikes_sdiff),
    // and a window outside the file is a row of zeros, so the rows stay aligned
    // with the timestamps.
    for (SdiffOrder order : {SdiffOrder::None, SdiffOrder::First,
                             SdiffOrder::Laplacian, SdiffOrder::AllPairs}) {
        SpikeWindowSpec spec;
        spec.nSamp = nSamp; spec.peakSample = peak; spec.totalNbChannels = totalCh;
        spec.groupChannels = chans.data(); spec.nChan = static_cast<int>(chans.size());
        spec.applyTransform = true; spec.temporalDiff = false; spec.order = order;
        const std::vector<int64_t> t2 = {peak - 1, peak, 50, 2999, nTotal - nSamp + peak,
                                         nTotal - nSamp + peak + 1};   // first/last out of range
        std::FILE* f = std::fopen(path, "rb");
        bool same = true, zeros = true;
        int64_t n = 0, oor = -1;
        extractSpikeWindows(f, static_cast<int64_t>(t2.size()), spec,
            [&](int64_t i) { return t2[static_cast<size_t>(i)]; },
            [&](int64_t i, const int16_t* r) {
                ++n;
                const int64_t ws = t2[static_cast<size_t>(i)] - peak;
                const bool out = ws < 0 || ws + nSamp > nTotal;
                std::vector<double> row(chans.size());
                for (int s = 0; s < nSamp; ++s) {
                    if (!out)
                        for (size_t ci = 0; ci < chans.size(); ++ci)
                            row[ci] = fil[static_cast<size_t>(ws + s) * totalCh + chans[ci]];
                    for (size_t ci = 0; ci < chans.size(); ++ci) {
                        const int16_t v = r[static_cast<size_t>(s) * chans.size() + ci];
                        if (out) zeros &= v == 0;
                        else same &= v == clampToInt16(spatialDeriv(order, row.data(),
                                               static_cast<int>(ci), static_cast<int>(chans.size())));
                    }
                }
            }, &oor);
        std::fclose(f);
        const std::string tag = "spatial only, order " + std::to_string(static_cast<int>(order));
        check(same, tag + ": each sample is the clamped spatial derivative");
        check(zeros && oor == 2 && n == static_cast<int64_t>(t2.size()),
              tag + ": the two windows outside the file are zero rows, one row per timestamp");
    }

    // seedFromPrevious = false: every window seeded with 0 (older .spk files)
    std::vector<std::vector<int16_t>> zeroSeeded, seeded;
    {
        for (bool seedPrev : {false, true}) {
            SpikeWindowSpec spec;
            spec.nSamp = nSamp; spec.peakSample = peak; spec.totalNbChannels = totalCh;
            spec.groupChannels = chans.data(); spec.nChan = static_cast<int>(chans.size());
            spec.applyTransform = true; spec.order = SdiffOrder::AllPairs;
            spec.seedFromPrevious = seedPrev;
            std::FILE* f = std::fopen(path, "rb");
            auto& dst = seedPrev ? seeded : zeroSeeded;
            extractSpikeWindows(f, static_cast<int64_t>(ts.size()), spec,
                [&](int64_t i) { return ts[static_cast<size_t>(i)]; },
                [&](int64_t, const int16_t* r) { dst.emplace_back(r, r + nSamp * chans.size()); });
            std::fclose(f);
        }
        bool zeroOk = true, restSame = true;
        for (size_t i = 0; i < ts.size(); ++i) {
            const int64_t ws = ts[i] - peak;
            // reference with sd[-1] = 0: the reference algorithm at a window whose
            // previous sample is treated as absent
            std::vector<double> row(chans.size());
            for (size_t ci = 0; ci < chans.size(); ++ci)
                row[ci] = fil[static_cast<size_t>(ws) * totalCh + chans[ci]];
            for (size_t ci = 0; ci < chans.size(); ++ci)
                zeroOk &= zeroSeeded[i][ci] == clampToInt16(spatialDeriv(
                    SdiffOrder::AllPairs, row.data(), static_cast<int>(ci),
                    static_cast<int>(chans.size())));
            for (size_t e = chans.size(); e < zeroSeeded[i].size(); ++e)
                restSame &= zeroSeeded[i][e] == seeded[i][e];
        }
        check(zeroOk, "seedFromPrevious=false: sample 0 is the spatial derivative itself");
        check(restSame, "the two conventions differ only in sample 0");
    }

    // detectStderivSeeding tells the two conventions apart from a stored file
    {
        std::vector<int64_t> many;
        for (int64_t t = 40; t < nTotal - nSamp; t += 37) many.push_back(t);
        SpikeWindowSpec spec;
        spec.nSamp = nSamp; spec.peakSample = peak; spec.totalNbChannels = totalCh;
        spec.groupChannels = chans.data(); spec.nChan = static_cast<int>(chans.size());
        spec.applyTransform = true; spec.order = SdiffOrder::AllPairs;
        std::vector<std::vector<int16_t>> stored[2];
        for (int k = 0; k < 2; ++k) {
            spec.seedFromPrevious = (k == 1);
            std::FILE* f = std::fopen(path, "rb");
            extractSpikeWindows(f, static_cast<int64_t>(many.size()), spec,
                [&](int64_t i) { return many[static_cast<size_t>(i)]; },
                [&](int64_t, const int16_t* r) { stored[k].emplace_back(r, r + nSamp * chans.size()); });
            std::fclose(f);
        }
        auto detect = [&](auto storedAt) {
            std::FILE* f = std::fopen(path, "rb");
            const StderivSeeding r = detectStderivSeeding(f, static_cast<int64_t>(many.size()), spec,
                [&](int64_t i) { return many[static_cast<size_t>(i)]; }, storedAt);
            std::fclose(f);
            return r;
        };
        // sample-major stored row: sample 0 of channel ci is element ci
        check(detect([&](int64_t i, int ci) { return stored[1][static_cast<size_t>(i)][static_cast<size_t>(ci)]; })
                  == StderivSeeding::Previous, "detect: seeded file → Previous");
        check(detect([&](int64_t i, int ci) { return stored[0][static_cast<size_t>(i)][static_cast<size_t>(ci)]; })
                  == StderivSeeding::Zero, "detect: zero-seeded file → Zero");
        check(detect([&](int64_t i, int ci) {
                  return stored[static_cast<size_t>(i % 2)][static_cast<size_t>(i)][static_cast<size_t>(ci)]; })
                  == StderivSeeding::Unknown, "detect: half-and-half file → Unknown");
        check(detect([&](int64_t, int) { return int16_t(12345); }) == StderivSeeding::Unknown,
              "detect: unrelated data → Unknown");
    }

    // ── spatialDeriv First on a single-channel group: pass-through, no chanRow[-1]
    {
        using neurosuite::core::spatialDeriv;
        using neurosuite::core::SdiffOrder;
        const double buf[3] = {999.0, 7.0, 555.0};          // chanRow = buf+1, one channel
        check(spatialDeriv(SdiffOrder::First, buf + 1, 0, 1) == 7.0,
              "First, nChan==1: pass-through (did not read chanRow[-1])");
        check(spatialDeriv(SdiffOrder::Laplacian, buf + 1, 0, 1) == 7.0,
              "Laplacian, nChan==1: pass-through (unchanged)");
        const double two[2] = {5.0, 2.0};
        check(spatialDeriv(SdiffOrder::First, two, 0, 2) == 3.0 &&
              spatialDeriv(SdiffOrder::First, two, 1, 2) == -3.0, "First, nChan==2 unchanged");
    }

    std::remove(path);
    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
