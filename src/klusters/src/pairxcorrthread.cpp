#include "pairxcorrthread.h"
#include "templatematrixview.h"
#include "templatematrixthread.h"   // for tmNormXcorr

#include <QApplication>
#include <cmath>

void PairXcorrThread::run()
{
    auto post = [this]() {
        QApplication::postEvent(&view, new PairXcorrEvent(*this));
    };

    const int nPts   = m_nChan * m_nSamp;
    const int sBytes = m_twoBytes ? 2 : 4;
    const int maxShift = std::max(1, m_nSamp / 4);
    const long nSpk  = static_cast<long>(m_sourceFileIdx.size());

    scores.reserve(static_cast<size_t>(nSpk));

    FILE* spk = fopen(m_spkPath.toLocal8Bit().constData(), "rb");
    if (!spk) { post(); return; }

    std::vector<int16_t> buf16(static_cast<size_t>(nPts));
    std::vector<int32_t> buf32(static_cast<size_t>(nPts));

    for (long s = 0; s < nSpk; ++s) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) break;

        const int  fileIdx0 = m_sourceFileIdx[static_cast<size_t>(s)];
        off_t      offset   = static_cast<off_t>(fileIdx0)
                            * static_cast<off_t>(nPts)
                            * static_cast<off_t>(sBytes);

        std::vector<float> sp(static_cast<size_t>(nPts), 0.0f);
        bool ok = false;

        if (fseeko(spk, offset, SEEK_SET) == 0) {
            if (m_twoBytes) {
                ok = (fread(buf16.data(), 2, static_cast<size_t>(nPts), spk)
                      == static_cast<size_t>(nPts));
                if (ok)
                    for (int ch = 0; ch < m_nChan; ++ch)
                        for (int sm = 0; sm < m_nSamp; ++sm)
                            sp[static_cast<size_t>(ch*m_nSamp+sm)] =
                                static_cast<float>(
                                    buf16[static_cast<size_t>(sm*m_nChan+ch)]);
            } else {
                ok = (fread(buf32.data(), 4, static_cast<size_t>(nPts), spk)
                      == static_cast<size_t>(nPts));
                if (ok)
                    for (int ch = 0; ch < m_nChan; ++ch)
                        for (int sm = 0; sm < m_nSamp; ++sm)
                            sp[static_cast<size_t>(ch*m_nSamp+sm)] =
                                static_cast<float>(
                                    buf32[static_cast<size_t>(sm*m_nChan+ch)]);
            }
        }

        float sc = ok ? tmNormXcorr(sp, m_targetMean, maxShift) : 0.0f;
        scores.emplace_back(fileIdx0, sc);
    }

    fclose(spk);
    post();
}
