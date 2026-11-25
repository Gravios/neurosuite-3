/* waveformquality.h -- how tightly a cluster's spikes sit on its median
 * waveform, as one number, without Qt.
 *
 * The waveform view shows it as a coloured dot per cluster (parents and
 * children alike): the spikes the view holds for a cluster are compared with
 * their own per-point median, and the variance of what is left is set against
 * the energy of that median.
 *
 *     median[s][c] = median over the spikes of w[s][c]
 *     v[s][c]      = mean over the spikes of (w[s][c] - median[s][c])^2
 *     noise[c]     = mean of w^2 over the spikes and the EDGE samples of
 *                    channel c (the first and last tenth of the window, where
 *                    no spike of the cluster is)
 *
 *     value = sum over the other samples and all channels of (v - noise)
 *             ---------------------------------------------------------
 *             sum over the same points of median^2
 *
 * so 0 is a cluster whose spikes differ from their median by the background
 * only, and 0.25 one whose spikes carry, beyond the background, a quarter of
 * the waveform's energy in differences from it -- from a second cell, from a
 * cell changing its amplitude (bursts) or its shape (drift), or from
 * misalignment.  Slightly negative values are background that is weaker under
 * the spike than at the window's edges.
 *
 * It is a hint, not a verdict.  Measured on sirotaA-jg-000005-20120312 group 8
 * (2026-11-22, analysis/cluster-quality-dot in the project): on 594 pipeline
 * parents and children scored against the curated file, the value separates
 * the clusters under 90 % pure from those at 98 % or more with AUC 0.71-0.74;
 * the lowest fifth of values still holds 10-15 % impure clusters and the
 * highest fifth 55-60 %.  Parent 253 of the curated file, two cells of similar
 * shape, reads 0.29 against 0.21 and 0.18 for its cells alone in stderiv_C5,
 * and the same as they do (-0.01) in the raw waveforms.  Curated parents that
 * span the whole session read higher than pipeline clusters (median 0.38): the
 * change of a cell's waveform over hours counts as residual.
 */
#ifndef KLUSTERS_WAVEFORMQUALITY_H
#define KLUSTERS_WAVEFORMQUALITY_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace waveformquality {

/** Below this many spikes the value is not computed (the median of a handful
 *  of spikes is not a template). */
constexpr long kMinSpikes = 30;

/** The value described at the top, or NaN when there are fewer than
 *  kMinSpikes spikes, the window is shorter than 5 samples, or the median
 *  waveform is flat.
 *  @p spikes holds @p n spikes one after the other, each nSamp * nChan values
 *  laid out sample after sample and, within a sample, channel after channel
 *  (the layout of the waveform cache: index = sample * nChan + channel). */
template <class T>
double normalisedResidualVariance(const T* spikes, long n, int nSamp, int nChan)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!spikes || n < kMinSpikes || nSamp < 5 || nChan < 1) return nan;
    const int edge = std::max(2, nSamp / 10);
    if (nSamp - 2 * edge < 1) return nan;
    const size_t pts = static_cast<size_t>(nSamp) * static_cast<size_t>(nChan);

    std::vector<double> noise(static_cast<size_t>(nChan), 0.0);
    std::vector<double> column(static_cast<size_t>(n));
    double excess = 0.0, energy = 0.0;

    // The background of each channel first: it is subtracted from every point of that channel.
    for (int c = 0; c < nChan; ++c) {
        double sum = 0.0;
        for (int s = 0; s < nSamp; ++s) {
            if (s >= edge && s < nSamp - edge) continue;
            const size_t at = static_cast<size_t>(s) * static_cast<size_t>(nChan) + static_cast<size_t>(c);
            for (long k = 0; k < n; ++k) {
                const double w = static_cast<double>(spikes[static_cast<size_t>(k) * pts + at]);
                sum += w * w;
            }
        }
        noise[static_cast<size_t>(c)] = sum / (static_cast<double>(n) * 2.0 * edge);
    }

    for (int s = edge; s < nSamp - edge; ++s) {
        for (int c = 0; c < nChan; ++c) {
            const size_t at = static_cast<size_t>(s) * static_cast<size_t>(nChan) + static_cast<size_t>(c);
            for (long k = 0; k < n; ++k)
                column[static_cast<size_t>(k)] = static_cast<double>(spikes[static_cast<size_t>(k) * pts + at]);
            // The median as numpy takes it: the mean of the two middle values when n is even.
            const auto mid = column.begin() + n / 2;
            std::nth_element(column.begin(), mid, column.end());
            double median = *mid;
            if (n % 2 == 0) median = 0.5 * (median + *std::max_element(column.begin(), mid));
            double v = 0.0;
            for (long k = 0; k < n; ++k) {
                const double d = column[static_cast<size_t>(k)] - median;
                v += d * d;
            }
            excess += v / static_cast<double>(n) - noise[static_cast<size_t>(c)];
            energy += median * median;
        }
    }
    if (!(energy > 0.0)) return nan;
    return excess / energy;
}

/** The four colours of the dot, and "no value". */
enum class Grade { Unknown, Green, Yellow, Orange, Red };

/** Green below @p yellow, yellow below @p orange, orange below @p red, red
 *  from there on; Unknown for a NaN. */
inline Grade grade(double value, double yellow, double orange, double red)
{
    if (!std::isfinite(value)) return Grade::Unknown;
    if (value < yellow) return Grade::Green;
    if (value < orange) return Grade::Yellow;
    if (value < red)    return Grade::Orange;
    return Grade::Red;
}

} // namespace waveformquality

#endif
