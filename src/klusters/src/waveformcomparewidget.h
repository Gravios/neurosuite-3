/***************************************************************************
 * waveformcomparewidget.h
 *
 * Custom widget that renders two sets of waveforms side by side:
 *   Left panel  — "Before" (from .spk.realign_bak)
 *   Right panel — "After"  (from live .spk)
 *
 * Each panel draws individual spike traces in a translucent colour and
 * the mean waveform in an opaque, bold colour on top.
 * Channels are stacked vertically, each in its own row.
 ***************************************************************************/

#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

class WaveformCompareWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformCompareWidget(QWidget* parent = nullptr);

    /**
     * Supply waveform data for both panels.
     *
     * @param before     Flat buffer: nBefore * nChan * nSamp int16 values,
     *                   layout [spike][s * nChan + ch].
     * @param after      Same layout for the post-realignment waveforms.
     * @param nChan      Number of channels per spike.
     * @param nSamp      Samples per channel per spike.
     * @param peakSamp   Expected peak sample index (drawn as a vertical marker).
     * @param nBefore    Number of spikes in `before`.
     * @param nAfter     Number of spikes in `after`.
     */
    void setData(const QVector<qint16>& before, int nBefore,
                 const QVector<qint16>& after,  int nAfter,
                 int nChan, int nSamp, int peakSamp);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawPanel(QPainter& p, const QRectF& rect,
                   const QVector<qint16>& waveforms, int nSpikes,
                   const QString& label);

    QVector<qint16> m_before, m_after;
    int m_nBefore = 0, m_nAfter = 0;
    int m_nChan = 0, m_nSamp = 0, m_peakSamp = 0;
};
