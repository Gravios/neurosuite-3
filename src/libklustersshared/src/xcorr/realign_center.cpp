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

}  // namespace realign_center
