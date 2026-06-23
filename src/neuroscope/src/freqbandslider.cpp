#include "freqbandslider.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

namespace {
constexpr int kSliderWidth = 38;   // thin
constexpr int kVMargin     = 14;   // room for the top/bottom labels
constexpr int kHandleGrab  = 7;    // px hit tolerance around a handle
constexpr int kTrackInset  = 10;   // left inset of the track within the widget
}

FreqBandSlider::FreqBandSlider(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::SizeVerCursor);
    setToolTip(tr("Frequency band for the per-channel spectral power.\n"
                  "Drag the handles to resize the band, the middle to move it."));
}

QSize FreqBandSlider::sizeHint() const        { return QSize(kSliderWidth, 200); }
QSize FreqBandSlider::minimumSizeHint() const { return QSize(kSliderWidth, 80); }

QRect FreqBandSlider::trackRect() const
{
    return QRect(kTrackInset, kVMargin,
                 width() - kTrackInset - 6,
                 std::max(1, height() - 2 * kVMargin));
}

int FreqBandSlider::yForHz(double hz) const
{
    const QRect t = trackRect();
    const double span = (rangeHi > rangeLo) ? (rangeHi - rangeLo) : 1.0;
    const double frac = (rangeHi - hz) / span;            // top = rangeHi
    return t.top() + static_cast<int>(std::lround(frac * t.height()));
}

double FreqBandSlider::hzForY(int y) const
{
    const QRect t = trackRect();
    const double frac = static_cast<double>(y - t.top()) / std::max(1, t.height());
    double hz = rangeHi - frac * (rangeHi - rangeLo);
    return std::clamp(hz, rangeLo, rangeHi);
}

void FreqBandSlider::clampBand()
{
    bandLo = std::clamp(bandLo, rangeLo, rangeHi);
    bandHi = std::clamp(bandHi, rangeLo, rangeHi);
    if (bandHi < bandLo) std::swap(bandLo, bandHi);
}

void FreqBandSlider::emitBand()
{
    emit bandChanged(bandLo, bandHi);
}

void FreqBandSlider::setRange(double lowHz, double highHz)
{
    if (highHz < lowHz) std::swap(lowHz, highHz);
    if (highHz <= lowHz) highHz = lowHz + 1.0;
    rangeLo = lowHz; rangeHi = highHz;
    clampBand();
    update();
}

void FreqBandSlider::setBand(double lowHz, double highHz)
{
    if (highHz < lowHz) std::swap(lowHz, highHz);
    bandLo = lowHz; bandHi = highHz;
    clampBand();
    update();
}

void FreqBandSlider::mousePressEvent(QMouseEvent* e)
{
    const int yHi = yForHz(bandHi);
    const int yLo = yForHz(bandLo);
    const int y = static_cast<int>(e->position().y());
    if (std::abs(y - yHi) <= kHandleGrab)      drag = Drag::High;
    else if (std::abs(y - yLo) <= kHandleGrab) drag = Drag::Low;
    else if (y > yHi && y < yLo) {
        drag = Drag::Band;
        dragGrabOffset = hzForY(y) - (bandLo + bandHi) / 2.0;
    } else {
        // Click outside the band: grab the nearer edge and start resizing.
        drag = (std::abs(y - yHi) < std::abs(y - yLo)) ? Drag::High : Drag::Low;
        if (drag == Drag::High) bandHi = hzForY(y); else bandLo = hzForY(y);
        clampBand(); emitBand(); update();
    }
    e->accept();
}

void FreqBandSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (drag == Drag::None) return;
    const double hz = hzForY(static_cast<int>(e->position().y()));
    if (drag == Drag::High) {
        bandHi = std::max(hz, bandLo);
    } else if (drag == Drag::Low) {
        bandLo = std::min(hz, bandHi);
    } else { // Band: move both, preserving width, clamped to the range
        const double w = bandHi - bandLo;
        double centre = hz - dragGrabOffset;
        double lo = centre - w / 2.0;
        lo = std::clamp(lo, rangeLo, rangeHi - w);
        bandLo = lo; bandHi = lo + w;
    }
    clampBand();
    emitBand();
    update();
}

void FreqBandSlider::mouseReleaseEvent(QMouseEvent*)
{
    drag = Drag::None;
}

void FreqBandSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect t = trackRect();
    const QColor base = palette().color(QPalette::Mid);
    const QColor sel  = palette().color(QPalette::Highlight);
    const QColor txt  = palette().color(QPalette::WindowText);

    // Track.
    p.setPen(Qt::NoPen);
    p.setBrush(base.darker(115));
    p.drawRoundedRect(t, 3, 3);

    // Selected band.
    const int yHi = yForHz(bandHi);
    const int yLo = yForHz(bandLo);
    QRect bandR(t.left(), yHi, t.width(), std::max(2, yLo - yHi));
    p.setBrush(sel);
    p.drawRoundedRect(bandR, 3, 3);

    // Handles.
    p.setPen(QPen(txt, 1));
    p.setBrush(palette().color(QPalette::Base));
    for (int y : {yHi, yLo}) {
        QRect h(t.left() - 2, y - 3, t.width() + 4, 6);
        p.drawRoundedRect(h, 2, 2);
    }

    // Edge labels (top = range high, bottom = range low) and the band edges.
    p.setPen(txt);
    QFont f = p.font(); f.setPointSizeF(std::max(6.0, f.pointSizeF() - 2.0)); p.setFont(f);
    const QFontMetrics fm(f);
    auto label = [&](double hz, int y, int flags) {
        const QString s = QString::number(hz, 'f', 0);
        QRect r(0, y - fm.height() / 2, width(), fm.height());
        p.drawText(r, flags | Qt::AlignVCenter, s);
    };
    label(rangeHi, t.top() - kVMargin / 2 - 2, Qt::AlignHCenter);
    label(rangeLo, t.bottom() + kVMargin / 2 + 2, Qt::AlignHCenter);
}
