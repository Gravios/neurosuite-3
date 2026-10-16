// ════════════════════════════════════════════════════════════════════════════
//  stderiv_transform.hpp — the single spatial-derivative + temporal
//  first-difference transform, shared by every tool that produces or
//  reprojects stderiv-domain waveforms.
//
//  This is the only definition: the extractors (stderiv and sdiff), the
//  re-extractor, process_pca_stderiv, the aligners and Klusters' realign and
//  nudge all call it, and fiber-kit's fiber_stderiv mirrors it.  Both sides are
//  held to test/stderiv_vectors.tsv (core_test_stderiv_vectors here,
//  test_stderiv_vectors.py in fiber-kit).  Do not copy the switch below into a
//  tool: call spatialDeriv / applyStderivTransform instead.
//
//  Definition:
//    spatial : spatialDeriv                                    (double result)
//    clamp   : round half away from zero, saturate to int16    (clampToInt16)
//    temporal: out[t] = clamp16( sd[t] − sd[t−1] ), sd[-1] = prevSeed, the
//              spatial derivative of the sample before the window (stderivSeed)
//    misfit  : a custom map that does not fit the group (null / wrong size)
//              degrades to all-pairs, for orders 4, 5 and 6 alike
// ════════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_STDERIV_TRANSFORM_HPP
#define NEUROSUITE_CORE_STDERIV_TRANSFORM_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "channel_expr.hpp"     // SdiffOrder::Expr
#include "pca_projection.hpp"   // SdiffOrder

namespace neurosuite {
namespace core {

// Saturating round of a double to int16, exactly as fill_sdiff_buffer does it.
// The double is saturated before it is rounded, so a value beyond the range of
// int (an expression pattern can produce one) saturates instead of overflowing;
// every value in range rounds exactly as before.
inline int16_t clampToInt16(double v) {
    if (!(v == v))       return 0;         // NaN
    if (v >=  32767.0)   return  32767;
    if (v <= -32768.0)   return -32768;
    int iv = static_cast<int>(std::lround(v));
    if (iv >  32767) iv =  32767;
    if (iv < -32768) iv = -32768;
    return static_cast<int16_t>(iv);
}

// Spatial derivative of channel `idx` within one time sample's channel row
// `chanRow[0..nChan)`.
inline double spatialDeriv(SdiffOrder order, const double* chanRow,
                           int idx, int nChan, const int* partner = nullptr,
                           const int* setOff = nullptr, const int* setMem = nullptr,
                           const ChannelExprSet* expr = nullptr) {
    const double val = chanRow[idx];
    switch (order) {
        case SdiffOrder::None:
            return val;
        case SdiffOrder::First:
            // A single-channel group has no neighbour: pass through, like
            // Laplacian (the ternary below would read chanRow[-1]).
            if (nChan == 1) return val;
            return (idx < nChan - 1) ? val - chanRow[idx + 1]
                                     : val - chanRow[idx - 1];
        case SdiffOrder::Laplacian:
            if (nChan == 1)        return val;
            if (idx == 0)          return val - chanRow[1];
            if (idx == nChan - 1)  return val - chanRow[nChan - 2];
            return val - 0.5 * (chanRow[idx - 1] + chanRow[idx + 1]);
        case SdiffOrder::Custom:
            // Per-channel custom difference: x[idx] - x[partner[idx]], where
            // partner[] is the within-group index each output channel is
            // differenced against (from the session's sdiffPairs).  A null map (or
            // single-channel group) means the pattern does not fit this group, and
            // like every custom order it degrades to all-pairs -- the rule the
            // extractor documents ("a size that does not match the pattern uses
            // order 3").
            if (partner && nChan > 1) return val - chanRow[partner[idx]];
            [[fallthrough]];
        case SdiffOrder::CustomCar: {
            // Per-channel reference set: x[idx] - mean(x[set[idx]]), the session's
            // sdiffPairs written with '+'.  setOff is a (nChan+1) prefix table into
            // the flattened setMem, so the header stays free of std containers.
            // A set map that does not fit the group degrades to all-pairs rather
            // than reading out of bounds.  (Reached by Custom's fall-through only
            // with order != CustomCar, hence the test.)
            if (order == SdiffOrder::CustomCar && setOff && setMem && nChan > 1) {
                const int b = setOff[idx], e = setOff[idx + 1];
                if (e > b) {
                    double sum = 0.0;
                    for (int k = b; k < e; ++k) sum += chanRow[setMem[k]];
                    return val - sum / static_cast<double>(e - b);
                }
            }
            [[fallthrough]];
        }
        case SdiffOrder::Expr:
            // Output channel idx = its expression over the group's channels (the
            // session's sdiffPairs written with c0, c1, ...).  Like the other
            // custom orders, a set that does not fit the group degrades to
            // all-pairs rather than reading out of bounds; the engines refuse
            // such a group up front.  (Reached by CustomCar's fall-through only
            // with order != Expr, hence the test.)
            if (order == SdiffOrder::Expr && expr && nChan > 1 && expr->fits(nChan))
                return expr->eval(idx, chanRow);
            [[fallthrough]];
        case SdiffOrder::AllPairs:
        default: {
            // Single-channel group has no pairs; the extractor returns 0 here.
            if (nChan == 1) return 0.0;
            double sum = 0.0;
            for (int j = 0; j < nChan; ++j) sum += chanRow[j];
            return static_cast<double>(nChan) * val - sum;  // n·x_i − Σx
        }
    }
}

// The prevSeed of a window cut out of a continuous recording: the clamped
// spatial derivative of the raw sample just BEFORE the window, one value per
// group channel (`groupRow[ci]` = that sample on the group's ci-th channel).
// process_extractspikes_stderiv seeds every window this way (its -R path reads
// one extra leading sample), so a tool that re-cuts windows from the .fil must
// pass this seed to reproduce the extractor's bytes; without it, sample 0 of
// every window is the spatial derivative itself instead of its difference.
inline void stderivSeed(SdiffOrder order, const int16_t* groupRow, int nChan,
                        int16_t* seedOut,
                        const int* partner = nullptr,
                        const int* setOff = nullptr, const int* setMem = nullptr,
                        const ChannelExprSet* expr = nullptr) {
    std::vector<double> row(static_cast<size_t>(nChan));
    for (int ci = 0; ci < nChan; ++ci)
        row[static_cast<size_t>(ci)] = static_cast<double>(groupRow[ci]);
    for (int ci = 0; ci < nChan; ++ci)
        seedOut[ci] = clampToInt16(
            spatialDeriv(order, row.data(), ci, nChan, partner, setOff, setMem, expr));
}

// Full stderiv transform of a channel-major waveform (in[ci*nSamp + t]) into
// out (same layout): spatial derivative per `order` (rounded+clamped to int16),
// then a temporal first-difference per channel (clamped), seeded by prevSeed
// (per channel; nullptr ⇒ 0).  Windows cut from a recording should pass the
// stderivSeed() of the sample before the window — that is what the extractor
// writes; nullptr is right only for a window at the very start of the file.  In-place is safe
// (out may alias in): each time sample's raw row is snapshotted before writing,
// so neighbour reads (First/Laplacian) always see untouched values.
inline void applyStderivTransform(SdiffOrder order,
                                  const int16_t* in, int nChan, int nSamp,
                                  int16_t* out,
                                  const int* partner = nullptr,
                                  const int16_t* prevSeed = nullptr,
                                  const int* setOff = nullptr,
                                  const int* setMem = nullptr,
                                  const ChannelExprSet* expr = nullptr) {
    std::vector<double>  row(static_cast<size_t>(nChan));
    std::vector<int16_t> prev(static_cast<size_t>(nChan));
    for (int ci = 0; ci < nChan; ++ci)
        prev[static_cast<size_t>(ci)] = prevSeed ? prevSeed[ci] : int16_t(0);

    for (int t = 0; t < nSamp; ++t) {
        for (int ci = 0; ci < nChan; ++ci)
            row[static_cast<size_t>(ci)] =
                static_cast<double>(in[static_cast<size_t>(ci) * nSamp + t]);
        for (int ci = 0; ci < nChan; ++ci) {
            const int16_t sd = clampToInt16(
                spatialDeriv(order, row.data(), ci, nChan, partner, setOff, setMem, expr));
            const int diff = static_cast<int>(sd)
                           - static_cast<int>(prev[static_cast<size_t>(ci)]);
            prev[static_cast<size_t>(ci)] = sd;
            out[static_cast<size_t>(ci) * nSamp + t] =
                clampToInt16(static_cast<double>(diff));
        }
    }
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_STDERIV_TRANSFORM_HPP
