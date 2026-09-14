// =============================================================================
// pca_row_projection kernel — equivalence regression
//
// projectSpikeOntoPca() replaced two hand-written copies of the same four
// nested loops, one in the realign row builder and one in the nudge's.  This
// test keeps the extracted kernel numerically identical to BOTH of them: the
// references below are the loops as they read before the extraction, and the
// test drives the real header rather than a transcription of it, so an edit to
// the kernel that changes a coefficient fails here instead of silently moving
// every spike's features on the next realign.
//
// Qt-free by design, like the other tests in this directory.
// =============================================================================
#include "pca_row_projection.h"
#include <cstdio>
#include <random>
#include <vector>
using neurosuite::core::PcaBasis;

// --- the realign loop, exactly as it read before the extraction -------------
static void realign_reference(const PcaBasis& pca, const int16_t* wav, int nSamp,
                              std::vector<int64_t>& row)
{
    int outCol = 0;
    for (int ch = 0; ch < pca.nCh; ++ch) {
        const double* E    = pca.evec[(size_t)ch].data();
        const double* mean = pca.means[(size_t)ch].data();
        for (int c = 0; c < pca.nComp; ++c) {
            double dot = 0.0;
            for (int j2 = 0; j2 < pca.data2use; ++j2) {
                double x = (double)wav[(size_t)(ch * nSamp + pca.recShift + j2)];
                if (pca.centered) x -= mean[j2];
                dot += E[j2 + c * pca.data2use] * x;
            }
            row[(size_t)(outCol++)] = (int64_t)std::llround(dot);
        }
    }
}
// --- the nudge loop, both of its branches ----------------------------------
static void nudge_reference(const PcaBasis& pca, bool isStderiv,
                            const std::vector<double>& xform,
                            const std::vector<int16_t>& wavRaw, int nSamp,
                            std::vector<int64_t>& row)
{
    int outCol = 0;
    for (int ch = 0; ch < pca.nCh; ++ch) {
        const double* E    = pca.evec[(size_t)ch].data();
        const double* mean = pca.means[(size_t)ch].data();
        for (int c = 0; c < pca.nComp; ++c) {
            double dot = 0.0;
            for (int j2 = 0; j2 < pca.data2use; ++j2) {
                double x;
                if (isStderiv) x = xform[(size_t)((pca.recShift + j2) * pca.nCh + ch)];
                else           x = (double)wavRaw[(size_t)(ch * nSamp + pca.recShift + j2)];
                if (pca.centered) x -= mean[j2];
                dot += E[j2 + c * pca.data2use] * x;
            }
            row[(size_t)(outCol++)] = std::llround(dot);
        }
    }
}

int main()
{
    std::mt19937 g(11);
    std::uniform_real_distribution<double> ud(-3, 3);
    std::uniform_int_distribution<int> wd(-4000, 4000);
    int checked = 0, mismatches = 0;

    for (int trial = 0; trial < 200; ++trial) {
        const int nChan = 3 + (int)(g() % 8), nSamp = 20 + (int)(g() % 30);
        PcaBasis pca;
        pca.nCh       = 1 + (int)(g() % (unsigned)nChan);
        pca.nComp     = 1 + (int)(g() % 5);
        pca.data2use  = 3 + (int)(g() % (unsigned)(nSamp - 3));
        pca.recShift  = (int)(g() % (unsigned)(nSamp - pca.data2use + 1));
        pca.centered  = (g() % 2) == 0;
        pca.evec.resize((size_t)pca.nCh);
        pca.means.resize((size_t)pca.nCh);
        for (int ch = 0; ch < pca.nCh; ++ch) {
            pca.evec[(size_t)ch].resize((size_t)(pca.data2use * pca.nComp));
            for (auto& v : pca.evec[(size_t)ch]) v = ud(g);
            pca.means[(size_t)ch].resize((size_t)pca.data2use);
            for (auto& v : pca.means[(size_t)ch]) v = ud(g) * 100.0;
        }
        std::vector<int16_t> wav((size_t)(nChan * nSamp));
        for (auto& v : wav) v = (int16_t)wd(g);
        std::vector<double> xform((size_t)(nSamp * pca.nCh));
        for (auto& v : xform) v = ud(g) * 500.0;

        const size_t width = (size_t)(pca.nCh * pca.nComp) + 4;
        std::vector<int64_t> a(width, 0), b(width, 0), c(width, 0), d(width, 0), e(width, 0);

        realign_reference(pca, wav.data(), nSamp, a);
        const int wrote = klusters::projectSpikeOntoPca(pca,
            [&](int ch, int j) { return (double)wav[(size_t)(ch * nSamp + pca.recShift + j)]; },
            b.data());
        nudge_reference(pca, false, xform, wav, nSamp, c);
        nudge_reference(pca, true,  xform, wav, nSamp, d);
        klusters::projectSpikeOntoPca(pca,
            [&](int ch, int j) { return xform[(size_t)((pca.recShift + j) * pca.nCh + ch)]; },
            e.data());

        if (wrote != pca.nCh * pca.nComp) { printf("FAIL: wrote %d\n", wrote); return 1; }
        if (a != b) { ++mismatches; printf("FAIL realign trial %d\n", trial); }
        if (a != c) { ++mismatches; printf("FAIL: the two references disagree on raw input\n"); }
        if (d != e) { ++mismatches; printf("FAIL stderiv trial %d\n", trial); }
        checked += (int)width * 3;
    }
    printf("kernel equivalence: %d coefficients compared across 200 random bases, "
           "%d mismatches\n", checked, mismatches);
    return mismatches ? 1 : 0;
}
