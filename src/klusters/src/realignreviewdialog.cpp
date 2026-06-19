/***************************************************************************
 * realignreviewdialog.cpp
 *
 * Accept/Reject dialog shown after spike realignment.
 * No file I/O here: Accept keeps pending in-memory changes (flushed to disk
 * on next save); Reject calls doc->rejectLastRealign() via the caller.
 ***************************************************************************/

#include "realignreviewdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QFont>
#include <QKeyEvent>
#include <cmath>
#include <limits>
#include <algorithm>

// ---------------------------------------------------------------------------
// WaveformPlotWidget — local, no Q_OBJECT (no signals/slots needed)
// ---------------------------------------------------------------------------

class WaveformPlotWidget : public QWidget
{
public:
    explicit WaveformPlotWidget(int nChan, int nSamp, int peakSamp0,
                                const QVector<float>& before,
                                const QVector<float>& after,
                                QWidget* parent = nullptr)
        : QWidget(parent)
        , nChan(nChan > 0 ? nChan : 1)
        , nSamp(nSamp > 0 ? nSamp : 1)
        , peakSamp0(peakSamp0)
        , before(before)
        , after(after)
    {
        setMinimumSize(400, std::max(120, nChan * 80));
        // Never let the plot steal keyboard focus — Tab should cycle only the buttons.
        setFocusPolicy(Qt::NoFocus);
    }

    QSize sizeHint() const override {
        return QSize(600, std::max(160, nChan * 90));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(30, 30, 30));

        if (before.isEmpty() && after.isEmpty()) {
            p.setPen(Qt::white);
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No waveform data"));
            return;
        }

        const int   W      = width();
        const int   H      = height();
        const int   margin = 6;
        const float rowH   = static_cast<float>(H - 2 * margin) / nChan;

        float gMin =  std::numeric_limits<float>::max();
        float gMax = -std::numeric_limits<float>::max();
        for (float v : before) { gMin = std::min(gMin, v); gMax = std::max(gMax, v); }
        for (float v : after)  { gMin = std::min(gMin, v); gMax = std::max(gMax, v); }
        if (gMax == gMin) gMax = gMin + 1.0f;
        const float ampRange = gMax - gMin;

        p.setFont(QFont(QStringLiteral("Monospace"), 7));

        for (int ch = 0; ch < nChan; ++ch) {
            const float yTop    = margin + ch * rowH;
            const float yBottom = yTop + rowH;

            p.fillRect(QRectF(0, yTop, W, rowH),
                       ch % 2 == 0 ? QColor(40, 40, 40) : QColor(35, 35, 35));

            // Zero line
            const float zeroY = yBottom - (0.0f - gMin) / ampRange * (rowH - 2);
            p.setPen(QPen(QColor(70, 70, 70), 0.5f, Qt::DotLine));
            p.drawLine(QPointF(margin, zeroY), QPointF(W - margin, zeroY));

            // Peak marker
            if (peakSamp0 >= 0 && peakSamp0 < nSamp) {
                const float px = margin
                    + static_cast<float>(peakSamp0) / (nSamp - 1)
                    * (W - 2 * margin);
                p.setPen(QPen(QColor(200, 200, 0, 100), 1.0f, Qt::DashLine));
                p.drawLine(QPointF(px, yTop + 1), QPointF(px, yBottom - 1));
            }

            // Draw one waveform trace (channel-major layout: wv[ch * nSamp + t])
            auto drawTrace = [&](const QVector<float>& wv, QColor col, Qt::PenStyle sty) {
                if (wv.size() < nChan * nSamp) return;
                p.setPen(QPen(col, 1.3f, sty));
                QPointF prev;
                bool first = true;
                for (int t = 0; t < nSamp; ++t) {
                    const float x = margin
                        + static_cast<float>(t) / (nSamp - 1) * (W - 2 * margin);
                    const float y = yBottom
                        - (wv[ch * nSamp + t] - gMin) / ampRange * (rowH - 2);
                    QPointF pt(x, y);
                    if (!first) p.drawLine(prev, pt);
                    prev = pt;
                    first = false;
                }
            };

            drawTrace(before, QColor(150, 150, 150), Qt::DashLine);
            drawTrace(after,  QColor(80, 180, 255),  Qt::SolidLine);

            // Channel label
            p.setPen(QColor(180, 180, 180));
            p.drawText(QRectF(margin + 2, yTop + 1, 40, 12),
                       QStringLiteral("ch%1").arg(ch + 1));
        }

        // Legend
        const int lx = W - 130, ly = H - 24;
        p.setFont(QFont(QStringLiteral("Sans"), 8));
        p.setPen(QPen(QColor(150, 150, 150), 1.5f, Qt::DashLine));
        p.drawLine(lx, ly + 6, lx + 20, ly + 6);
        p.setPen(Qt::white);
        p.drawText(lx + 24, ly + 10, QStringLiteral("Before"));
        p.setPen(QPen(QColor(80, 180, 255), 1.5f));
        p.drawLine(lx, ly + 18, lx + 20, ly + 18);
        p.setPen(Qt::white);
        p.drawText(lx + 24, ly + 22, QStringLiteral("After"));
    }

private:
    int            nChan, nSamp, peakSamp0;
    QVector<float> before, after;
};

// ---------------------------------------------------------------------------
// RealignReviewDialog
// ---------------------------------------------------------------------------

RealignReviewDialog::RealignReviewDialog(
        int clusterId, int nShifted, int nSwapped,
        int nChan, int nSamp,
        const QVector<float>& meanBefore,
        const QVector<float>& meanAfter,
        const QString& /*backupBase — unused, kept for API compat*/,
        QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Review Realignment \u2014 Cluster %1").arg(clusterId));
    setModal(true);
    resize(640, std::max(400, nChan * 90 + 160));

    // Derive peakSamp0: sample with max summed |amplitude| in after-mean
    int peakSamp0 = nSamp / 2;
    if (!meanAfter.isEmpty() && nChan > 0 && nSamp > 0) {
        float best = -1.0f;
        for (int t = 0; t < nSamp; ++t) {
            float amp = 0.0f;
            for (int ch = 0; ch < nChan; ++ch)
                amp += std::abs(meanAfter[ch * nSamp + t]);
            if (amp > best) { best = amp; peakSamp0 = t; }
        }
    }

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    auto* summaryLabel = new QLabel(
        tr("<b>Cluster %1</b>: %2 spike(s) shifted, %3 sort-order correction(s).<br>"
           "Mean waveform: before (grey dashed) and after (blue) realignment.<br>"
           "Yellow dashed line marks the expected peak sample.")
        .arg(clusterId).arg(nShifted).arg(nSwapped), this);
    summaryLabel->setWordWrap(true);
    mainLayout->addWidget(summaryLabel);

    auto* plot = new WaveformPlotWidget(nChan, nSamp, peakSamp0,
                                        meanBefore, meanAfter, this);
    plot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(plot, 1);

    auto* noteLabel = new QLabel(
        tr("<small>Accept: keep realignment in memory \u2014 writes to disk on next save.<br>"
           "Reject: discard changes, restore previous state immediately.<br>"
           "Use Left/Right arrows or Tab to switch between buttons.</small>"), this);
    noteLabel->setWordWrap(true);
    mainLayout->addWidget(noteLabel);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    rejectBtn = new QPushButton(tr("Reject"), this);
    acceptBtn = new QPushButton(tr("Accept"), this);
    acceptBtn->setDefault(true);

    // Ensure both buttons are Tab-focusable.
    rejectBtn->setFocusPolicy(Qt::StrongFocus);
    acceptBtn->setFocusPolicy(Qt::StrongFocus);

    btnLayout->addWidget(rejectBtn);
    btnLayout->addWidget(acceptBtn);
    mainLayout->addLayout(btnLayout);

    // Explicit Tab order: Reject → Accept → (wrap).
    QWidget::setTabOrder(rejectBtn, acceptBtn);
    QWidget::setTabOrder(acceptBtn, rejectBtn);

    connect(acceptBtn, &QPushButton::clicked, this, &RealignReviewDialog::slotAccept);
    connect(rejectBtn, &QPushButton::clicked, this, &RealignReviewDialog::slotReject);

    // Start with Accept focused so Enter immediately accepts.
    acceptBtn->setFocus();
}

void RealignReviewDialog::keyPressEvent(QKeyEvent* e)
{
    // Left/Right arrows move focus between the two buttons.
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        QPushButton* current = qobject_cast<QPushButton*>(focusWidget());
        if (current == acceptBtn)
            rejectBtn->setFocus();
        else
            acceptBtn->setFocus();
        e->accept();
        return;
    }
    QDialog::keyPressEvent(e);
}

void RealignReviewDialog::slotAccept()
{
    accepted = true;
    accept();
}

void RealignReviewDialog::slotReject()
{
    accepted = false;
    reject();
}

