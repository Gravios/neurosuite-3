#include "freqbandslider.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

namespace {
constexpr int kSliderWidth = 54;   // room for a left-hand Hz label column
constexpr int kLabelW      = 28;    // width of the tick-label column
constexpr int kVMargin     = 10;    // top/bottom padding
constexpr int kHandleGrab  = 7;     // px hit tolerance around a handle
constexpr int kRightPad    = 6;     // padding right of the track

// "Nice" tick step (1/2/5 x 10^k) giving roughly `target` divisions over span.
double niceStep(double span, int target)
{
    if (span <= 0.0 || target < 1) return 1.0;
    const double raw = span / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n   = raw / mag;
    const double nice = (n < 1.5) ? 1.0 : (n < 3.0) ? 2.0 : (n < 7.0) ? 5.0 : 10.0;
    return nice * mag;
}
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
    return QRect(kLabelW, kVMargin,
                 std::max(1, width() - kLabelW - kRightPad),
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

    // Frequency ticks at nice steps, with labels in the left column.
    p.setPen(txt);
    QFont f = p.font(); f.setPointSizeF(std::max(6.0, f.pointSizeF() - 2.0)); p.setFont(f);
    const QFontMetrics fm(f);
    const double span = rangeHi - rangeLo;
    const double step = niceStep(span, 5);
    const int    minGapPx = fm.height() + 2;     // avoid label overlap
    int lastY = -10000;
    const double first = std::ceil(rangeLo / step) * step;
    for (double hz = first; hz <= rangeHi + step * 0.001; hz += step) {
        const int y = yForHz(hz);
        if (std::abs(y - lastY) < minGapPx) continue;
        lastY = y;
        // tick mark just left of the track
        p.setPen(QPen(txt, 1));
        p.drawLine(t.left() - 3, y, t.left() - 1, y);
        // right-aligned label in the label column
        const QString s = QString::number(hz, 'f', 0);
        QRect r(0, y - fm.height() / 2, kLabelW - 5, fm.height());
        p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, s);
    }

    // Current band edges, drawn at the handles so the selection reads exactly.
    p.setPen(sel.darker(160));
    auto edge = [&](double hz, int y) {
        const QString s = QString::number(hz, 'f', 0);
        QRect r(t.left(), y - fm.height() - 1, t.width(), fm.height());
        p.drawText(r, Qt::AlignHCenter | Qt::AlignBottom, s);
    };
    edge(bandHi, yHi);
    if (yLo - yHi > fm.height() + 4) edge(bandLo, yLo + fm.height() + 1);
}
