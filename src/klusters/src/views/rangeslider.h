/***************************************************************************
 * rangeslider.h — horizontal two-handle range slider.
 *
 * Qt ships no two-handle slider.  This is the horizontal, double-valued
 * counterpart of neuroscope's FreqBandSlider (a vertical one, used to pick a
 * spectral integration band) and deliberately mirrors its interaction model:
 * drag either handle, or grab the span between them to move both at once, or
 * click outside the span to pull the nearer handle to the click.
 *
 * The two are close enough that a shared widget in libklustersshared would be
 * the tidier home; that is a cross-application move (it would touch neuroscope's
 * build) and is left for its own patch.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef RANGESLIDER_H
#define RANGESLIDER_H

#include <QWidget>

class RangeSlider : public QWidget {
    Q_OBJECT
public:
    explicit RangeSlider(QWidget* parent = nullptr);

    double lowerValue() const { return spanLo; }
    double upperValue() const { return spanHi; }
    double rangeMinimum() const { return rangeLo; }
    double rangeMaximum() const { return rangeHi; }

public Q_SLOTS:
    /// Set the slider's full extent.  The current span is clamped into it.
    void setRange(double low, double high);
    /// Set the selected span WITHOUT emitting spanChanged (for programmatic
    /// updates, so a caller can push state in without echoing back).
    void setSpan(double low, double high);

Q_SIGNALS:
    /// Emitted while the user drags a handle or the span.
    void spanChanged(double low, double high);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    enum class Drag { None, Low, High, Span };

    QRect  trackRect() const;            // the horizontal track area
    double valueForX(int x) const;       // pixel -> value (clamped to the range)
    int    xForValue(double v) const;    // value -> pixel
    void   clampSpan();
    void   emitSpan();

    double rangeLo = 0.0;
    double rangeHi = 1.0;
    double spanLo  = 0.0;
    double spanHi  = 1.0;

    Drag   drag = Drag::None;
    double dragGrabOffset = 0.0;         // value offset for Span drags
};

#endif // RANGESLIDER_H
