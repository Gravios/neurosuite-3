/***************************************************************************
 * realign_center.cpp — see realign_center.h
 ***************************************************************************/
#include "realign_center.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace realign_center {

RecenterResult circularRecenterShift(const double* energy, int nSamp,
                                     int peakPos, double rMin)
{
    RecenterResult out;
    if (!energy || nSamp <= 0) return out;

    const double TWO_PI = 6.283185307179586476925286766559;

    double C = 0.0, S = 0.0, W = 0.0;
    for (int s = 0; s < nSamp; ++s) {
        const double th = TWO_PI * static_cast<double>(s)
                        / static_cast<double>(nSamp);
        const double w  = energy[s];
        C += w * std::cos(th);
        S += w * std::sin(th);
        W += w;
    }
    out.R = (W > 0.0) ? std::hypot(C, S) / W : 0.0;
    if (out.R < rMin) return out;          // applied stays false, shift 0

    double centroid = std::atan2(S, C);
    if (centroid < 0.0) centroid += TWO_PI;
    centroid *= static_cast<double>(nSamp) / TWO_PI;   // -> sample units
    out.centroid = centroid;

    // Minimal signed circular shift to move the centroid onto peakPos.
    // Roll convention new[t]=old[(t+shift)%N] sends old index c to (c-shift),
    // so shift = c - peakPos, reduced to (-N/2, N/2].
    const double Nd = static_cast<double>(nSamp);
    double dgf = centroid - static_cast<double>(peakPos);
    dgf = std::fmod(dgf, Nd);
    if (dgf >  Nd / 2.0) dgf -= Nd;
    if (dgf < -Nd / 2.0) dgf += Nd;
    out.shift   = static_cast<int>(std::lround(dgf));
    out.applied = true;
    return out;
}


RecenterResult circularRecenterShiftWindowed(const double* energy, const float* weights,
                                             int nSamp, int peakPos, double rMin, bool follow)
{
    if (!weights) return circularRecenterShift(energy, nSamp, peakPos, rMin);
    RecenterResult out;
    if (!energy || nSamp <= 0) return out;

    // The window offset is kept FRACTIONAL (the window is read by circular linear interpolation):
    // rolled by whole samples, the mean shift stalls up to a sample short of the lobe as soon as a
    // pedestal dilutes the last step below half a sample.
    const double Nd = static_cast<double>(nSamp);
    std::vector<double> prod(static_cast<size_t>(nSamp), 0.0);
    double offset = 0.0;                                        // the window sits on peakPos + offset
    const int maxPass = follow ? 200 : 1;
    for (int pass = 0; pass < maxPass; ++pass) {
        double W = 0.0;
        for (int s = 0; s < nSamp; ++s) {
            double x = std::fmod(static_cast<double>(s) - offset, Nd);
            if (x < 0.0) x += Nd;
            const int    i0 = static_cast<int>(x) % nSamp;
            const int    i1 = (i0 + 1) % nSamp;
            const double f  = x - std::floor(x);
            const double wv = (1.0 - f) * static_cast<double>(weights[i0]) + f * static_cast<double>(weights[i1]);
            const double v  = wv * energy[s];
            prod[static_cast<size_t>(s)] = v;
            W += v;
        }
        if (!(W > 0.0)) return RecenterResult{};                // nothing under the window
        // rMin 0 here: the guard is applied once, to the pass that is kept.
        out = circularRecenterShift(prod.data(), nSamp, peakPos, 0.0);
        double d = std::fmod(out.centroid - static_cast<double>(peakPos), Nd);
        if (d >  Nd / 2.0) d -= Nd;
        if (d < -Nd / 2.0) d += Nd;
        const bool settled = std::abs(d - offset) < 1e-3;
        offset = d;
        if (settled) break;
    }
    if (out.R < rMin) { out.shift = 0; out.applied = false; }
    return out;
}

void perSpikeCentroidShifts(const double* energy, int nSpikes, int nSamp,
                            std::vector<int>& shiftOut) {
    const double TWO_PI = 2.0 * std::numbers::pi;
    shiftOut.assign(nSpikes < 0 ? 0 : nSpikes, 0);
    if (nSpikes <= 0 || nSamp <= 0 || !energy) return;

    // A spike whose resultant length is ~0 has no centroid: a zero-energy spike (a zero-filled
    // failed read) gives atan2(0,0) = 0, which would roll it by ~N/2 and pull the population target
    // toward sample 0; a flat energy profile is just as undefined.  Such spikes keep shift 0 and are
    // left out of the target.
    const double kMinR = 1e-9;

    // 1. each spike's circular energy centroid (first-DFT-bin phasor of its per-sample energy).
    std::vector<double> pos(nSpikes, 0.0);
    std::vector<char>   valid(nSpikes, 0);
    for (int i = 0; i < nSpikes; ++i) {
        const double* e = energy + static_cast<size_t>(i) * nSamp;
        double C = 0.0, S = 0.0, W = 0.0;
        for (int s = 0; s < nSamp; ++s) {
            const double th = TWO_PI * s / nSamp;
            C += e[s] * std::cos(th);
            S += e[s] * std::sin(th);
            W += e[s];
        }
        if (!(W > 0.0) || !(std::hypot(C, S) > kMinR * W)) continue;   // R ~ 0: undefined
        valid[i] = 1;
        double a = std::atan2(S, C);
        if (a < 0.0) a += TWO_PI;
        pos[i] = a * static_cast<double>(nSamp) / TWO_PI;
    }

    // 2. population circular-mean centroid (unit phasors so every spike weighs equally -- a pure
    //    RELATIVE de-jitter with no fixed peak, matching fiber_realign's target=mean(exp(i*pos))).
    //    Only spikes with a defined centroid contribute.
    double MC = 0.0, MS = 0.0;
    int nValid = 0;
    for (int i = 0; i < nSpikes; ++i) {
        if (!valid[i]) continue;
        const double th = TWO_PI * pos[i] / nSamp;
        MC += std::cos(th);
        MS += std::sin(th);
        ++nValid;
    }
    if (nValid == 0) return;                // nothing to align: every shift stays 0
    double at = std::atan2(MS, MC);
    if (at < 0.0) at += TWO_PI;
    const double target = at * static_cast<double>(nSamp) / TWO_PI;

    // 3. per-spike minimal signed circular shift onto the target.
    for (int i = 0; i < nSpikes; ++i) {
        if (!valid[i]) continue;            // undefined centroid: shift stays 0
        double sh = std::fmod((target - pos[i]) + nSamp / 2.0, static_cast<double>(nSamp));
        if (sh < 0.0) sh += nSamp;          // C fmod is signed; the circular reduction wants [0,N)
        sh -= nSamp / 2.0;                  // -> (-N/2, N/2], the signed distance
        shiftOut[i] = static_cast<int>(std::lround(-sh));
    }
}

}
