#ifndef PCA_ROW_PROJECTION_H
#define PCA_ROW_PROJECTION_H

// The one place a spike is projected onto a per-channel PCA basis.
//
// This kernel existed twice in klustersdoc_realign.cpp -- once in the realign
// commit's row builder and once in the nudge's -- as the same four nested
// loops with the same centring and the same rounding.  Two copies of one
// formula is the failure this codebase keeps paying for: the channel-width
// rule had two homes and they disagreed, which is what refused valid realigns.
// The two callers differ in where their samples come from (raw channel-major
// waveform, or the spatially differentiated sample-major buffer) and in what
// they do with the remaining .fet columns; they do not differ in the algebra,
// so only the algebra lives here.
//
// Deliberately klusters-local.  It reads neurosuite::core::PcaBasis, but it is
// NOT part of libneurosuite-core: putting it there would make it a cross-repo
// contract, with the lockstep obligations that carries, for a helper only
// Klusters uses.  Header-only so the standalone test can drive the real code
// rather than a transcription of it.

#include <cmath>
#include <cstdint>

#include <neurosuite/core/pca_projection.hpp>

namespace klusters {

/** Projects one spike onto @p pca, writing pca.nCh * pca.nComp coefficients
 *  into @p out in channel-major order (all components of channel 0, then
 *  channel 1, ...).
 *
 *  @param sample  called as sample(ch, j) for channel @p ch and sample offset
 *                 @p j within the basis window; must return the RAW value.
 *                 Centring (pca.means) and rounding are applied here, so a
 *                 caller cannot forget one of them.
 *  @return the number of coefficients written, so the caller knows where its
 *          own extra columns begin without recomputing nCh * nComp.
 */
template <typename SampleFn>
inline int projectSpikeOntoPca(const neurosuite::core::PcaBasis& pca,
                               SampleFn sample, int64_t* out)
{
    int outCol = 0;
    for (int ch = 0; ch < pca.nCh; ++ch) {
        const double* E    = pca.evec[static_cast<size_t>(ch)].data();
        const double* mean = pca.means[static_cast<size_t>(ch)].data();
        for (int c = 0; c < pca.nComp; ++c) {
            double dot = 0.0;
            for (int j = 0; j < pca.data2use; ++j) {
                double x = sample(ch, j);
                if (pca.centered) x -= mean[j];
                dot += E[j + c * pca.data2use] * x;
            }
            out[outCol++] = static_cast<int64_t>(std::llround(dot));
        }
    }
    return outCol;
}

}   // namespace klusters

#endif
