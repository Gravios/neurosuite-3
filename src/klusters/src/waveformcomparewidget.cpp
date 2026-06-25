/***************************************************************************
 * waveformcomparewidget.cpp
 ***************************************************************************/

#include "waveformcomparewidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>
#include <limits>

WaveformCompareWidget::WaveformCompareWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(600, 300);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void WaveformCompareWidget::setData(const QVector<qint16>& before, int nBefore,
                                    const QVector<qint16>& after,  int nAfter,
                                    int nChan, int nSamp, int peakSamp)
{
    this->before   = before;   this->nBefore = nBefore;
    this->after    = after;    this->nAfter  = nAfter;
    this->nChan    = nChan;    this->nSamp   = nSamp;
    this->peakSamp = peakSamp;
    update();
}

QSize WaveformCompareWidget::sizeHint()        const { return {800, 500}; }
QSize WaveformCompareWidget::minimumSizeHint() const { return {400, 200}; }

// ---------------------------------------------------------------------------
// paintEvent
// ---------------------------------------------------------------------------
void WaveformCompareWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect();
    p.fillRect(r, QColor(30, 30, 30));

    if (nChan == 0 || nSamp == 0) {
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, tr("No waveform data"));
        return;
    }

    const int pad = 8;
    const int labelH = QFontMetrics(font()).height() + 4;
    const int half = (r.width() - 3 * pad) / 2;

    QRectF leftRect (pad,          pad, half, r.height() - 2 * pad);
    QRectF rightRect(pad*2 + half, pad, half, r.height() - 2 * pad);

    // Dividing line
    p.setPen(QColor(80, 80, 80));
    p.drawLine(pad * 2 + half - 1, pad, pad * 2 + half - 1, r.height() - pad);

    drawPanel(p, leftRect,  before, nBefore, tr("Before"));
    drawPanel(p, rightRect, after,  nAfter,  tr("After"));
}

// ---------------------------------------------------------------------------
// drawPanel — renders one set of waveforms inside `rect`
// ---------------------------------------------------------------------------
void WaveformCompareWidget::drawPanel(QPainter& p, const QRectF& rect,
                                      const QVector<qint16>& waveforms,
                                      int nSpikes, const QString& label)
{
    if (nChan == 0 || nSamp == 0) return;

    // Label
    p.setPen(QColor(200, 200, 200));
    QRectF labelRect(rect.left(), rect.top(), rect.width(), 20);
    p.drawText(labelRect, Qt::AlignCenter, label);

    const double plotTop    = rect.top() + 22;
    const double plotH      = rect.height() - 22;
    const double chanH      = plotH / nChan;   // height allocated per channel row

    // Find global amplitude range across all spikes and channels
    double globalMax = 1.0;
    for (qint16 v : waveforms)
        globalMax = std::max(globalMax, std::abs(static_cast<double>(v)));

    // Compute mean waveform — layout [s * nChan + ch]
    QVector<double> mean(nChan * nSamp, 0.0);
    if (nSpikes > 0) {
        for (int si = 0; si < nSpikes; ++si) {
            const qint16* w = waveforms.constData() + si * nChan * nSamp;
            for (int s = 0; s < nSamp; ++s)
                for (int ch = 0; ch < nChan; ++ch)
                    mean[s * nChan + ch] += w[s * nChan + ch];
        }
        for (double& v : mean) v /= nSpikes;
    }

    // X scale: map sample index to pixel x within the panel
    auto xPix = [&](int s) -> double {
        return rect.left() + (s + 0.5) * rect.width() / nSamp;
    };
    // Y scale: map amplitude to pixel y within a channel row
    auto yPix = [&](int ch, double amp) -> double {
        double centre = plotTop + (ch + 0.5) * chanH;
        return centre - amp / globalMax * (chanH * 0.45);
    };

    // Draw peak-sample marker (thin vertical line per channel row)
    p.setPen(QPen(QColor(80, 80, 50), 1, Qt::DotLine));
    for (int ch = 0; ch < nChan; ++ch) {
        double x = xPix(peakSamp);
        double y0 = plotTop + ch * chanH + 2;
        double y1 = plotTop + (ch + 1) * chanH - 2;
        p.drawLine(QPointF(x, y0), QPointF(x, y1));
    }

    // Draw channel separator lines
    p.setPen(QColor(55, 55, 55));
    for (int ch = 1; ch < nChan; ++ch) {
        double y = plotTop + ch * chanH;
        p.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }

    // Draw individual spikes (translucent)
    {
        QColor spikeCol(100, 160, 255, 40);
        p.setPen(QPen(spikeCol, 1));
        for (int si = 0; si < nSpikes; ++si) {
            const qint16* w = waveforms.constData() + si * nChan * nSamp;
            for (int ch = 0; ch < nChan; ++ch) {
                QPolygonF poly(nSamp);
                for (int s = 0; s < nSamp; ++s)
                    poly[s] = QPointF(xPix(s), yPix(ch, w[s * nChan + ch]));
                p.drawPolyline(poly);
            }
        }
    }

    // Draw mean waveform (opaque, bold)
    if (nSpikes > 0) {
        QColor meanCol(255, 220, 60);
        p.setPen(QPen(meanCol, 2));
        for (int ch = 0; ch < nChan; ++ch) {
            QPolygonF poly(nSamp);
            for (int s = 0; s < nSamp; ++s)
                poly[s] = QPointF(xPix(s), yPix(ch, mean[s * nChan + ch]));
            p.drawPolyline(poly);
        }
    }

    // Spike count
    p.setPen(QColor(150, 150, 150));
    QRectF countRect(rect.left(), rect.bottom() - 18, rect.width(), 18);
    p.drawText(countRect, Qt::AlignCenter,
               tr("%1 spike(s)").arg(nSpikes));
}
