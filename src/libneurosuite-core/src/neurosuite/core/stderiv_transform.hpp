// ════════════════════════════════════════════════════════════════════════════
//  stderiv_transform.hpp — the single spatial-derivative + temporal
//  first-difference transform, shared by every tool that produces or
//  reprojects stderiv-domain waveforms.
//
//  Today the same math is hand-copied in at least five places: the extractor
//  (process_extractspikes_stderiv::computeSDiff + fill_sdiff_buffer), the PCA
//  channel-reducer (process_pca_stderiv::computeSDiff), the klusters nudge
//  lambda, and two klusters realign/reproject inner loops — each hardcoding the
//  ALLPAIRS order.  This header is the one definition they can all call, with the
//  spatial order taken from the PCAE basis (spatialOrder(pca.method)) rather than
//  hardcoded.
//
//  Ground truth (matched byte-for-byte):
//    spatial : process_extractspikes_stderiv::computeSDiff   (double result)
//    clamp   : round() then saturate to int16                (fill_sdiff_buffer Step 1)
//    temporal: out[t] = clamp16( sd[t] − sd[t−1] ), sd[-1] = prevSeed (default 0)
//                                                            (fill_sdiff_buffer Step 2)
// ════════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_STDERIV_TRANSFORM_HPP
#define NEUROSUITE_CORE_STDERIV_TRANSFORM_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "pca_projection.hpp"   // SdiffOrder

namespace neurosuite {
namespace core {

// Saturating round of a double to int16, exactly as fill_sdiff_buffer does it.
inline int16_t clampToInt16(double v) {
    int iv = static_cast<int>(std::lround(v));
    if (iv >  32767) iv =  32767;
    if (iv < -32768) iv = -32768;
    return static_cast<int16_t>(iv);
}

// Spatial derivative of channel `idx` within one time sample's channel row
// `chanRow[0..nChan)`.  Byte-identical to
// process_extractspikes_stderiv::computeSDiff (and the process_pca_stderiv copy).
inline double spatialDeriv(SdiffOrder order, const double* chanRow,
                           int idx, int nChan, const int* partner = nullptr) {
    const double val = chanRow[idx];
    switch (order) {
        case SdiffOrder::None:
            return val;
        case SdiffOrder::First:
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
            // differenced against (from the session's sdiffPairs).  Byte-identical
            // to process_extractspikes_stderiv's SDIFF_CUSTOM case.  A null map (or
            // single-channel group) falls back to pass-through so a caller that
            // mislabels the order can never read out of bounds.
            return (partner && nChan > 1) ? val - chanRow[partner[idx]] : val;
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

// Full stderiv transform of a channel-major waveform (in[ci*nSamp + t]) into
// out (same layout): spatial derivative per `order` (rounded+clamped to int16),
// then a temporal first-difference per channel (clamped), seeded by prevSeed
// (per channel; nullptr ⇒ 0, the per-spike windowed case).  In-place is safe
// (out may alias in): each time sample's raw row is snapshotted before writing,
// so neighbour reads (First/Laplacian) always see untouched values.
inline void applyStderivTransform(SdiffOrder order,
                                  const int16_t* in, int nChan, int nSamp,
                                  int16_t* out,
                                  const int* partner = nullptr,
                                  const int16_t* prevSeed = nullptr) {
    std::vector<double>  row(static_cast<size_t>(nChan));
    std::vector<int16_t> prev(static_cast<size_t>(nChan));
    for (int ci = 0; ci < nChan; ++ci)
        prev[static_cast<size_t>(ci)] = prevSeed ? prevSeed[ci] : int16_t(0);

    for (int t = 0; t < nSamp; ++t) {
        for (int ci = 0; ci < nChan; ++ci)
            row[static_cast<size_t>(ci)] =
                static_cast<double>(in[static_cast<size_t>(ci) * nSamp + t]);
        for (int ci = 0; ci < nChan; ++ci) {
            const int16_t sd = clampToInt16(spatialDeriv(order, row.data(), ci, nChan, partner));
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
