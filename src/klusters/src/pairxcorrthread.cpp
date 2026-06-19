#include "pairxcorrthread.h"
#include "templatematrixview.h"
#include "templatematrixthread.h"   // for tmNormXcorr
#include "configuration.h"

#include <QApplication>
#include <cmath>

void PairXcorrThread::run()
{
    auto post = [this]() {
        QApplication::postEvent(&view, new PairXcorrEvent(*this));
    };

    const int maxShift = std::max(1, nSamp / 4);
    const bool pearson = configuration().getTemplateXcorrPearson();
    const long nSpk  = static_cast<long>(sourceFileIdx.size());

    scores.reserve(static_cast<size_t>(nSpk));

    FILE* spk = fopen(spkPath.toLocal8Bit().constData(), "rb");
    if (!spk) { post(); return; }

    std::vector<int16_t> raw;
    std::vector<float>   sp;

    for (long s = 0; s < nSpk; ++s) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) break;

        const int  fileIdx0 = sourceFileIdx[static_cast<size_t>(s)];
        const bool ok = tmReadSpikeFloat(spk, fileIdx0, nChan, nSamp, raw, sp);

        float sc = ok ? tmNormXcorr(sp, targetMean, maxShift, pearson) : 0.0f;
        scores.emplace_back(fileIdx0, sc);
    }

    fclose(spk);
    post();
}
