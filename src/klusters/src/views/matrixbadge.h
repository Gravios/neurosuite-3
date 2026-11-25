/***************************************************************************
 * matrixbadge.h — small "computing" overlay for the matrix views.
 *
 * The matrix views used to replace their whole frame with a centred
 * "Computing…" line while a thread ran, which throws away the matrix the user
 * was reading.  These views already have a precedent for showing outdated data
 * rather than nothing: a cluster edit marks the matrix stale and draws a red
 * border around it while leaving it on screen.  This continues that idea — the
 * previous matrix stays up and a small badge says a recompute is in flight.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXBADGE_H
#define MATRIXBADGE_H

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QString>
#include <algorithm>

/**Geometry of the badge, split out so the placement can be reasoned about (and
 * unit-tested) without a QPainter.
 *
 * @param viewRect   the widget's rectangle.
 * @param textW/textH the rendered text size.
 * @param haveMatrix  true when a previous matrix is still on screen.
 *
 * With a matrix underneath, the badge tucks into the top-left corner so it
 * covers as few cells as possible.  With nothing underneath (the very first
 * compute) it sits in the middle, where a lone corner box on an empty frame
 * would just look lost.  It is clamped to the widget, so a narrow dock shrinks
 * it rather than pushing it off-screen.
 */
inline QRect mbBadgeRect(const QRect& viewRect, int textW, int textH,
                         bool haveMatrix)
{
    const int pad    = 6;
    const int margin = 8;
    int w = textW + 2 * pad;
    int h = textH + 2 * pad;
    w = std::min(w, std::max(1, viewRect.width()  - 2 * margin));
    h = std::min(h, std::max(1, viewRect.height() - 2 * margin));

    if (!haveMatrix) {
        return QRect(viewRect.x() + (viewRect.width()  - w) / 2,
                     viewRect.y() + (viewRect.height() - h) / 2, w, h);
    }
    return QRect(viewRect.x() + margin, viewRect.y() + margin, w, h);
}

/**Draw the badge.  @p haveMatrix tells it whether a matrix is underneath (see
 * mbBadgeRect).  Translucent so a cell it covers is still readable.*/
inline void mbDrawComputingBadge(QPainter& p, const QRect& viewRect,
                                 const QString& text, bool haveMatrix)
{
    const QFontMetrics fm(p.font());
    const QRect bounds = fm.boundingRect(text);
    const QRect box    = mbBadgeRect(viewRect, bounds.width(), fm.height(),
                                     haveMatrix);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(0, 0, 0, 170));
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    p.drawRoundedRect(box, 4, 4);
    p.setPen(QColor(255, 255, 255, 230));
    p.drawText(box, Qt::AlignCenter, text);
    p.restore();
}

#endif // MATRIXBADGE_H
