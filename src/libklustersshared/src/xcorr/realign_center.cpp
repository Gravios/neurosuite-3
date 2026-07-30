/***************************************************************************
 * realign_center.cpp — see realign_center.h
 ***************************************************************************/
#include "realign_center.h"

#include <cmath>

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


void perSpikeCentroidShifts(const double* energy, int nSpikes, int nSamp,
                            std::vector<int>& shiftOut) {
    const double TWO_PI = 2.0 * M_PI;
    shiftOut.assign(nSpikes < 0 ? 0 : nSpikes, 0);
    if (nSpikes <= 0 || nSamp <= 0 || !energy) return;

    // 1. each spike's circular energy centroid (first-DFT-bin phasor of its per-sample energy).
    std::vector<double> pos(nSpikes, 0.0);
    for (int i = 0; i < nSpikes; ++i) {
        const double* e = energy + static_cast<size_t>(i) * nSamp;
        double C = 0.0, S = 0.0;
        for (int s = 0; s < nSamp; ++s) {
            const double th = TWO_PI * s / nSamp;
            C += e[s] * std::cos(th);
            S += e[s] * std::sin(th);
        }
        double a = std::atan2(S, C);
        if (a < 0.0) a += TWO_PI;
        pos[i] = a * static_cast<double>(nSamp) / TWO_PI;
    }

    // 2. population circular-mean centroid (unit phasors so every spike weighs equally -- a pure
    //    RELATIVE de-jitter with no fixed peak, matching fiber_realign's target=mean(exp(i*pos))).
    double MC = 0.0, MS = 0.0;
    for (int i = 0; i < nSpikes; ++i) {
        const double th = TWO_PI * pos[i] / nSamp;
        MC += std::cos(th);
        MS += std::sin(th);
    }
    double at = std::atan2(MS, MC);
    if (at < 0.0) at += TWO_PI;
    const double target = at * static_cast<double>(nSamp) / TWO_PI;

    // 3. per-spike minimal signed circular shift onto the target.
    for (int i = 0; i < nSpikes; ++i) {
        double sh = std::fmod((target - pos[i]) + nSamp / 2.0, static_cast<double>(nSamp));
        if (sh < 0.0) sh += nSamp;          // C fmod is signed; the circular reduction wants [0,N)
        sh -= nSamp / 2.0;                  // -> (-N/2, N/2], the signed distance
        shiftOut[i] = static_cast<int>(std::lround(-sh));
    }
}

}
