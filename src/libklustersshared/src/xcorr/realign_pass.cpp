// realign_pass.cpp -- see realign_pass.h.

#include "realign_pass.h"

#include "realign_xcorr.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace realign_pass {

namespace {
const double pi = 3.14159265358979323846;
}

std::vector<float> kernelWeights(Kernel kernel, int kernelWidth, int peakSample,
                                 int nSamp, int nChan, const int16_t* tmpl)
{
    std::vector<float> w;
    if (kernel == Kernel::None || nSamp <= 0) return w;
    w.assign(static_cast<size_t>(nSamp), 0.0f);
    if (kernel == Kernel::Template) {
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
        const double d = static_cast<double>(s - peakSample);
        if (kernel == Kernel::Gauss) {
            const double sig = hw / 2.0;
            w[static_cast<size_t>(s)] = static_cast<float>(std::exp(-0.5 * (d / sig) * (d / sig)));
        } else {                                              // Hann
            w[static_cast<size_t>(s)] = (std::abs(d) <= hw)
                ? static_cast<float>(0.5 * (1.0 + std::cos(pi * d / hw))) : 0.0f;
        }
    }
    return w;
}

void rollSpike(int16_t* spike, int nChan, int nSamp, int shift)
{
    if (shift == 0 || nSamp <= 0) return;
    std::vector<int16_t> tmp(static_cast<size_t>(nChan) * static_cast<size_t>(nSamp));
    for (int t = 0; t < nSamp; ++t) {
        const int src = (t + shift + nSamp) % nSamp;
        for (int ch = 0; ch < nChan; ++ch)
            tmp[static_cast<size_t>(ch * nSamp + t)] = spike[static_cast<size_t>(ch * nSamp + src)];
    }
    std::copy(tmp.begin(), tmp.end(), spike);
}

bool clusterShifts(int16_t* waveforms, int64_t N, int nChan, int nSamp,
                   const Options& o, Result& result)
{
    const size_t spkElems = static_cast<size_t>(nChan) * static_cast<size_t>(nSamp);
    result.shift.assign(static_cast<size_t>(std::max<int64_t>(N, 0)), 0);
    result.score.assign(static_cast<size_t>(std::max<int64_t>(N, 0)), 0.0f);
    result.moved.clear();
    result.error = 0;
    if (N <= 0 || spkElems == 0) return true;
    const bool masked = o.topChannels > 0 && o.topChannels < nChan;

    for (int iter = 0; iter < o.iterations; ++iter) {
        // The mean template of the spikes as aligned so far.
        std::vector<int64_t> acc(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = waveforms + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e) acc[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> tmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e) tmpl[e] = static_cast<int16_t>(acc[e] / N);

        // The channels that count: the topChannels largest of the template, or all.
        std::vector<uint8_t> chanUse(static_cast<size_t>(nChan), 1);
        if (masked) {
            std::vector<std::pair<int64_t, int>> chanAmp(static_cast<size_t>(nChan));
            for (int ch = 0; ch < nChan; ++ch) {
                int64_t pk = 0;
                for (int t = 0; t < nSamp; ++t) {
                    const int64_t v = std::abs(static_cast<int64_t>(tmpl[static_cast<size_t>(ch * nSamp + t)]));
                    if (v > pk) pk = v;
                }
                chanAmp[static_cast<size_t>(ch)] = { pk, ch };
            }
            std::partial_sort(chanAmp.begin(), chanAmp.begin() + o.topChannels, chanAmp.end(),
                              [](const auto& a, const auto& b) { return a.first > b.first; });
            std::fill(chanUse.begin(), chanUse.end(), 0);
            for (int k = 0; k < o.topChannels; ++k)
                chanUse[static_cast<size_t>(chanAmp[static_cast<size_t>(k)].second)] = 1;
        }

        // Roll the template so that its peak is on peakSample.  Without this the
        // correlation would move every spike away from the true peak.
        {
            int    tmplPeak = 0;
            double bestAmp  = -1.0;
            for (int t = 0; t < nSamp; ++t) {
                double amp = 0.0;
                for (int ch = 0; ch < nChan; ++ch)
                    if (chanUse[static_cast<size_t>(ch)])
                        amp += std::abs(static_cast<double>(tmpl[static_cast<size_t>(ch * nSamp + t)]));
                if (amp > bestAmp) { bestAmp = amp; tmplPeak = t; }
            }
            const int tShift = o.peakSample - tmplPeak;
            if (tShift != 0) {
                std::vector<int16_t> shifted(spkElems);
                for (int ch = 0; ch < nChan; ++ch)
                    for (int t = 0; t < nSamp; ++t) {
                        const int src = (t - tShift + nSamp) % nSamp;
                        shifted[static_cast<size_t>(ch * nSamp + t)] = tmpl[static_cast<size_t>(ch * nSamp + src)];
                    }
                tmpl = std::move(shifted);
            }
        }

        // The mask goes on scratch copies for the correlation only: the buffer itself is
        // needed whole for the next pass's template and by the caller afterwards.  A
        // masked channel is zero on both sides of the correlation and adds nothing at any lag.
        const int16_t* xcorrWav  = waveforms;
        const int16_t* xcorrTmpl = tmpl.data();
        std::vector<int16_t> wavMasked;
        std::vector<int16_t> tmplMasked;
        if (masked) {
            tmplMasked = tmpl;
            wavMasked.assign(waveforms, waveforms + static_cast<size_t>(N) * spkElems);
            for (int ch = 0; ch < nChan; ++ch) {
                if (chanUse[static_cast<size_t>(ch)]) continue;
                std::fill(tmplMasked.begin() + ch * nSamp, tmplMasked.begin() + (ch + 1) * nSamp, int16_t(0));
                for (int64_t i = 0; i < N; ++i) {
                    int16_t* row = wavMasked.data() + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
                    std::fill(row + ch * nSamp, row + (ch + 1) * nSamp, int16_t(0));
                }
            }
            xcorrWav  = wavMasked.data();
            xcorrTmpl = tmplMasked.data();
        }

        // The correlation window for this pass, on the template as rolled and masked.
        std::vector<int>   sh(static_cast<size_t>(N), 0);
        std::vector<float> sc(static_cast<size_t>(N), 0.0f);
        const std::vector<float> kw = kernelWeights(o.kernel, o.kernelWidth, o.peakSample, nSamp, nChan, xcorrTmpl);
        const int rc = XcorrDispatch::computeWeighted(xcorrWav, xcorrTmpl, kw.empty() ? nullptr : kw.data(),
                                                      static_cast<int>(N), nChan, nSamp,
                                                      o.maxShift, o.minScore, sh.data(), sc.data());
        if (rc != 0) { result.error = rc; return false; }

        // A best lag of s means the spike's peak is late by s: roll it forward by s, and
        // its time moves by +s (the caller's newTs = ts + shift).
        int changed = 0;
        for (int64_t i = 0; i < N; ++i) {
            const int s = sh[static_cast<size_t>(i)];
            if (s == 0) continue;
            rollSpike(waveforms + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems), nChan, nSamp, s);
            result.shift[static_cast<size_t>(i)] += s;
            result.score[static_cast<size_t>(i)] = sc[static_cast<size_t>(i)];
            ++changed;
        }
        result.moved.push_back(changed);
        if (changed == 0) break;
    }
    return true;
}

} // namespace realign_pass
