/***************************************************************************
 * rangeslider.cpp — see rangeslider.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "rangeslider.h"

#include <QPainter>
#include <QMouseEvent>
#include <QStyle>
#include <algorithm>
#include <cmath>

static constexpr int kHandleW    = 7;    // handle width (px)
static constexpr int kHandleGrab = 6;    // grab tolerance either side (px)
static constexpr int kMargin     = kHandleW;   // room for a handle at each end

RangeSlider::RangeSlider(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(false);
    setCursor(Qt::ArrowCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize RangeSlider::sizeHint() const { return QSize(160, 20); }

QSize RangeSlider::minimumSizeHint() const
{
    // Deliberately small: this sits in the template matrix's control bar, whose
    // width already floors the tabbed matrix dock frame.  Reporting a wide
    // minimum here would make that worse.
    return QSize(4 * kHandleW, 16);
}

QRect RangeSlider::trackRect() const
{
    return QRect(kMargin, height() / 2 - 2, std::max(1, width() - 2 * kMargin), 4);
}

double RangeSlider::valueForX(int x) const
{
    const QRect t = trackRect();
    if (t.width() <= 0 || rangeHi <= rangeLo) return rangeLo;
    const double f = static_cast<double>(x - t.left()) / t.width();
    return std::clamp(rangeLo + f * (rangeHi - rangeLo), rangeLo, rangeHi);
}

int RangeSlider::xForValue(double v) const
{
    const QRect t = trackRect();
    if (rangeHi <= rangeLo) return t.left();
    const double f = (std::clamp(v, rangeLo, rangeHi) - rangeLo) / (rangeHi - rangeLo);
    return t.left() + static_cast<int>(std::lround(f * t.width()));
}

void RangeSlider::clampSpan()
{
    spanLo = std::clamp(spanLo, rangeLo, rangeHi);
    spanHi = std::clamp(spanHi, rangeLo, rangeHi);
    if (spanHi < spanLo) std::swap(spanLo, spanHi);
}

void RangeSlider::emitSpan() { emit spanChanged(spanLo, spanHi); }

void RangeSlider::setRange(double low, double high)
{
    rangeLo = low;
    rangeHi = high;
    if (rangeHi < rangeLo) std::swap(rangeLo, rangeHi);
    clampSpan();
    update();
}

void RangeSlider::setSpan(double low, double high)
{
    spanLo = low;
    spanHi = high;
    clampSpan();
    update();          // no emit: programmatic set must not echo back
}

void RangeSlider::mousePressEvent(QMouseEvent* e)
{
    const int xLo = xForValue(spanLo);
    const int xHi = xForValue(spanHi);
    const int x   = static_cast<int>(e->position().x());

    if (std::abs(x - xLo) <= kHandleGrab)      drag = Drag::Low;
    else if (std::abs(x - xHi) <= kHandleGrab) drag = Drag::High;
    else if (x > xLo && x < xHi) {
        drag = Drag::Span;
        dragGrabOffset = valueForX(x) - (spanLo + spanHi) / 2.0;
    } else {
        // Outside the span: grab the nearer handle and start resizing from here.
        drag = (std::abs(x - xLo) < std::abs(x - xHi)) ? Drag::Low : Drag::High;
        if (drag == Drag::Low) spanLo = valueForX(x); else spanHi = valueForX(x);
        clampSpan(); emitSpan(); update();
    }
    e->accept();
}

void RangeSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (drag == Drag::None) return;
    const double v = valueForX(static_cast<int>(e->position().x()));
    if (drag == Drag::Low) {
        spanLo = std::min(v, spanHi);
    } else if (drag == Drag::High) {
        spanHi = std::max(v, spanLo);
    } else {   // Span: move both, preserving width, clamped to the range
        const double w = spanHi - spanLo;
        double centre  = v - dragGrabOffset;
        double lo      = std::clamp(centre - w / 2.0, rangeLo, rangeHi - w);
        spanLo = lo;
        spanHi = lo + w;
    }
    clampSpan();
    emitSpan();
    update();
    e->accept();
}

void RangeSlider::mouseReleaseEvent(QMouseEvent* e)
{
    drag = Drag::None;
    e->accept();
}

void RangeSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect t = trackRect();
    const int   xLo = xForValue(spanLo);
    const int   xHi = xForValue(spanHi);

    // Groove, then the selected span highlighted over it.
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Mid));
    p.drawRoundedRect(t, 2, 2);
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawRoundedRect(QRect(xLo, t.top(), std::max(1, xHi - xLo), t.height()), 2, 2);

    // Handles.  The upper one is the highlight threshold as well as the top of
    // the colour axis, so it is drawn filled to read as the "active" edge.
    const int hTop = height() / 2 - 7;
    const int hH   = 14;
    p.setPen(QPen(palette().color(QPalette::Shadow), 1));
    p.setBrush(palette().color(QPalette::Button));
    p.drawRoundedRect(QRect(xLo - kHandleW / 2, hTop, kHandleW, hH), 2, 2);
    p.setBrush(palette().color(QPalette::ButtonText));
    p.drawRoundedRect(QRect(xHi - kHandleW / 2, hTop, kHandleW, hH), 2, 2);
}
