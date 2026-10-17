/***************************************************************************
 * radialmenu.cpp — see radialmenu.h.
 *
 * Angles follow QPainterPath::arcTo: degrees, 0 at 3 o'clock, positive
 * counter-clockwise.  Within a ring, item i sits at base − i·step, so items
 * read clockwise from the ring's base angle.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "radialmenu.h"

#include <QAction>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kHubRadius = 30.0;   ///< dead zone at the centre (px)
constexpr double kRingWidth = 82.0;   ///< radial thickness of one ring (px)
constexpr double kRingGap   = 3.0;    ///< gap between rings (px)
constexpr double kMargin    = 6.0;    ///< widget border around the outermost ring (px)
constexpr int    kDepthCap  = 4;      ///< rings never nest deeper than this
constexpr double kPi        = 3.14159265358979323846;

/** Fold an angle into [0, 360). */
double norm360(double a)
{
    a = std::fmod(a, 360.0);
    return a < 0.0 ? a + 360.0 : a;
}

/** Minimum wedge angle for a child ring: outer rings are longer, so their
 *  wedges can be narrower and still hold a label. */
double minStepForRing(int ringIndex)
{
    return ringIndex <= 1 ? 34.0 : 26.0;
}
} // namespace

RadialMenu::RadialMenu(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void RadialMenu::setMenu(QMenu* menuRoot)
{
    root = menuRoot;
    maxDepth = root ? std::min(treeDepth(root), kDepthCap) : 1;
    const double r = outerRadius(maxDepth - 1) + kMargin;
    const int side = static_cast<int>(std::ceil(2.0 * r));
    setFixedSize(side, side);
    resetRings();
}

QMenu* RadialMenu::menu() const
{
    return root.data();
}

// --------------------------------------------------------------------------
// tree helpers
// --------------------------------------------------------------------------

QList<QAction*> RadialMenu::visibleActions(const QMenu* m)
{
    QList<QAction*> out;
    if (!m) return out;
    const QList<QAction*> all = m->actions();
    for (QAction* a : all) {
        if (!a || !a->isVisible() || a->isSeparator()) continue;
        if (a->menu() && visibleActions(a->menu()).isEmpty()) continue;   // empty submenu
        out.append(a);
    }
    return out;
}

int RadialMenu::treeDepth(const QMenu* m, int level)
{
    int deepest = level;
    if (level >= kDepthCap) return deepest;
    for (const QAction* a : visibleActions(m))
        if (a->menu() && !visibleActions(a->menu()).isEmpty())
            deepest = std::max(deepest, treeDepth(a->menu(), level + 1));
    return deepest;
}

void RadialMenu::setLabel(const QAction* action, const QString& text)
{
    if (!action) return;
    if (text.isEmpty()) labels.remove(action);
    else labels.insert(action, text);
    update();
}

void RadialMenu::setAdjustable(const QAction* action, bool on)
{
    if (!action) return;
    if (on) adjustable.insert(action);
    else adjustable.remove(action);
    update();
}

QString RadialMenu::labelFor(const QAction* a) const
{
    const auto it = labels.constFind(a);
    return it != labels.constEnd() ? it.value() : plainText(a);
}

QString RadialMenu::plainText(const QAction* a)
{
    // Drop mnemonic markers ("&Split" -> "Split") but keep a literal "&&" as "&".
    QString t = a->text();
    QString out;
    out.reserve(t.size());
    for (int i = 0; i < t.size(); ++i) {
        if (t.at(i) == QLatin1Char('&')) {
            if (i + 1 < t.size() && t.at(i + 1) == QLatin1Char('&')) { out.append(QLatin1Char('&')); ++i; }
            continue;
        }
        out.append(t.at(i));
    }
    if (out.endsWith(QStringLiteral("..."))) out.chop(3);
    else if (out.endsWith(QChar(0x2026))) out.chop(1);
    return out.trimmed();
}

RadialMenu::Ring RadialMenu::makeRing(const QMenu* m, double parentMid, double parentStep,
                                      int ringIndex) const
{
    Ring ring;
    ring.items = visibleActions(m);
    const int n = std::max<int>(1, static_cast<int>(ring.items.size()));
    if (ringIndex == 0) {
        ring.span = 360.0;
        ring.step = 360.0 / n;
        ring.base = 90.0;                               // first item at 12 o'clock
    } else {
        ring.span = std::min(360.0, std::max(parentStep, n * minStepForRing(ringIndex)));
        ring.step = ring.span / n;
        // Centre the arc on the parent wedge; item 0 at its counter-clockwise end so the
        // children read clockwise like the root.
        ring.base = (ring.span >= 360.0) ? parentMid
                                         : parentMid + ring.span / 2.0 - ring.step / 2.0;
    }
    return ring;
}

void RadialMenu::resetRings()
{
    rings.clear();
    if (root) rings.append(makeRing(root, 90.0, 360.0, 0));
    lastHovered = nullptr;
    update();
}

// --------------------------------------------------------------------------
// geometry
// --------------------------------------------------------------------------

double RadialMenu::innerRadius(int ringIndex) const
{
    return kHubRadius + ringIndex * (kRingWidth + kRingGap);
}

double RadialMenu::outerRadius(int ringIndex) const
{
    return innerRadius(ringIndex) + kRingWidth;
}

double RadialMenu::angleAt(const QPointF& p) const
{
    // Widget y grows downward; the angle convention is counter-clockwise, y up.
    const double dx = p.x() - centre.x();
    const double dy = centre.y() - p.y();
    return norm360(std::atan2(dy, dx) * 180.0 / kPi);
}

RadialMenu::Hit RadialMenu::hitTest(const QPointF& p) const
{
    Hit hit;
    if (rings.isEmpty()) return hit;
    const double r = std::hypot(p.x() - centre.x(), p.y() - centre.y());
    if (r < kHubRadius) return hit;                     // dead zone

    // Radius picks the ring; past the outermost ring the outermost one still answers
    // (overshoot is normal in a flick).
    int k = static_cast<int>((r - kHubRadius) / (kRingWidth + kRingGap));
    k = std::min(k, static_cast<int>(rings.size()) - 1);
    if (r > outerRadius(static_cast<int>(rings.size()) - 1) + kRingWidth) return hit;

    const double a = angleAt(p);
    // A partial ring that does not cover this angle hands the point to the ring below
    // it (ring 0 always covers the full circle), so sliding sideways out of a child arc
    // re-targets the parent ring — the usual marking-menu behaviour.
    for (; k >= 0; --k) {
        const Ring& ring = rings.at(k);
        const int n = static_cast<int>(ring.items.size());
        if (n == 0) continue;
        const double delta = norm360(ring.base + ring.step / 2.0 - a);
        if (delta >= ring.span) continue;
        const int i = static_cast<int>(delta / ring.step);
        if (i < 0 || i >= n) continue;
        hit.ring = k;
        hit.item = i;
        return hit;
    }
    return hit;
}

bool RadialMenu::updateHover(const QPointF& p)
{
    if (rings.isEmpty()) return false;
    const Hit hit = hitTest(p);
    const double r = std::hypot(p.x() - centre.x(), p.y() - centre.y());

    if (hit.ring < 0) {
        if (r < kHubRadius) {                           // back in the hub: collapse
            while (rings.size() > 1) rings.removeLast();
            rings.first().hovered = -1;
        }
    } else {
        while (rings.size() > hit.ring + 1) rings.removeLast();
        Ring& ring = rings[hit.ring];
        ring.hovered = hit.item;
        QAction* a = ring.items.at(hit.item);
        if (a->menu() && a->isEnabled() && hit.ring + 1 < maxDepth
            && !visibleActions(a->menu()).isEmpty()) {
            const double mid = ring.base - hit.item * ring.step;
            rings.append(makeRing(a->menu(), mid, ring.step, hit.ring + 1));
        }
    }

    QAction* now = hoveredAction();
    if (now == lastHovered) { update(); return false; }
    lastHovered = now;
    Q_EMIT hovered(now);
    update();
    return true;
}

QAction* RadialMenu::hoveredAction() const
{
    // The deepest ring with a hovered wedge.
    for (int k = static_cast<int>(rings.size()) - 1; k >= 0; --k)
        if (rings.at(k).hovered >= 0) return rings.at(k).items.at(rings.at(k).hovered);
    return nullptr;
}

QAction* RadialMenu::leafAt(const QPointF& p)
{
    const Hit hit = hitTest(p);
    if (hit.ring < 0) return nullptr;
    QAction* a = rings.at(hit.ring).items.at(hit.item);
    if (!a->isEnabled() || a->menu() || adjustable.contains(a)) return nullptr;
    return a;
}

// --------------------------------------------------------------------------
// showing / picking
// --------------------------------------------------------------------------

void RadialMenu::popup(const QPoint& globalPos)
{
    if (!root || visibleActions(root).isEmpty()) return;
    resetRings();

    // Centre on the cursor, then slide the window back inside the screen.  The rings stay
    // centred on the cursor (centre moves inside the widget), so the gesture directions
    // are unchanged even at a screen edge; only far-out rings may be clipped.
    QScreen* scr = QGuiApplication::screenAt(globalPos);
    if (!scr) scr = QGuiApplication::primaryScreen();
    QPoint topLeft = globalPos - QPoint(width() / 2, height() / 2);
    if (scr) {
        const QRect avail = scr->availableGeometry();
        topLeft.setX(std::clamp(topLeft.x(), avail.left(), std::max(avail.left(), avail.right() - width() + 1)));
        topLeft.setY(std::clamp(topLeft.y(), avail.top(),  std::max(avail.top(),  avail.bottom() - height() + 1)));
    }
    move(topLeft);
    centre = QPointF(globalPos - topLeft);

    gestureArmed = (QGuiApplication::mouseButtons() != Qt::NoButton);
    dragged = false;
    clickPressed = false;

    show();
    raise();
    setFocus(Qt::PopupFocusReason);
}

void RadialMenu::activate(QAction* a)
{
    // Close first so the popup's mouse grab is gone before the action runs (an action
    // may open a modal dialog); trigger on the next event-loop turn.
    QPointer<QAction> guarded(a);
    close();
    Q_EMIT triggered(a);
    QTimer::singleShot(0, a, [guarded]{ if (guarded) guarded->trigger(); });
}

void RadialMenu::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF p = event->position();
    if (std::hypot(p.x() - centre.x(), p.y() - centre.y()) >= kHubRadius) dragged = true;
    updateHover(p);
}

void RadialMenu::mousePressEvent(QMouseEvent* event)
{
    // Reached in click mode (a press outside the widget closes a Qt::Popup by itself).
    const QPointF p = event->position();
    updateHover(p);
    const Hit hit = hitTest(p);
    if (hit.ring < 0) { close(); return; }               // hub or empty corner: cancel
    clickPressed = true;
}

void RadialMenu::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF p = event->position();
    updateHover(p);

    if (gestureArmed) {
        // The release that ends the opening press.  A drag that ends on an enabled leaf
        // picks it; a release without leaving the hub (or on a submenu / disabled wedge)
        // leaves the menu open in click mode.
        gestureArmed = false;
        if (dragged) {
            if (QAction* a = leafAt(p)) activate(a);
        }
        return;
    }
    if (clickPressed) {
        clickPressed = false;
        if (QAction* a = leafAt(p)) activate(a);
    }
}

void RadialMenu::wheelEvent(QWheelEvent* event)
{
    const QPointF p = event->position();
    updateHover(p);
    const Hit hit = hitTest(p);
    QAction* a = (hit.ring >= 0) ? rings.at(hit.ring).items.at(hit.item) : nullptr;
    if (!a || !a->isEnabled() || !adjustable.contains(a)) {
        wheelRemainder = 0;
        event->accept();                 // a popup: never let the wheel scroll what is behind
        return;
    }
    // One step per 120 units (a notch); touchpads deliver fractions, so keep the rest.
    wheelRemainder += event->angleDelta().y();
    const int steps = wheelRemainder / 120;
    wheelRemainder -= steps * 120;
    if (steps != 0) {
        Q_EMIT adjusted(a, steps);
        update();
    }
    event->accept();
}

void RadialMenu::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        // Esc backs out one ring at a time, then closes.
        if (rings.size() > 1) {
            rings.removeLast();
            rings.last().hovered = -1;
            lastHovered = nullptr;
            Q_EMIT hovered(hoveredAction());
            update();
        } else {
            close();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

void RadialMenu::hideEvent(QHideEvent* event)
{
    gestureArmed = false;
    clickPressed = false;
    resetRings();
    Q_EMIT hovered(nullptr);
    QWidget::hideEvent(event);
}

// --------------------------------------------------------------------------
// painting
// --------------------------------------------------------------------------

void RadialMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QPalette pal = palette();
    QColor fill = pal.color(QPalette::Window);   fill.setAlpha(242);
    QColor edge = pal.color(QPalette::Mid);
    const QColor hilite   = pal.color(QPalette::Highlight);
    QColor pathFill = hilite;                    pathFill.setAlpha(110);
    const QColor text     = pal.color(QPalette::Active,   QPalette::WindowText);
    const QColor textHi   = pal.color(QPalette::Active,   QPalette::HighlightedText);
    const QColor textOff  = pal.color(QPalette::Disabled, QPalette::WindowText);

    QFont f = font();
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 0.9);
    p.setFont(f);

    const int lastRing = static_cast<int>(rings.size()) - 1;
    for (int k = 0; k <= lastRing; ++k) {
        const Ring& ring = rings.at(k);
        const double r0 = innerRadius(k);
        const double r1 = outerRadius(k);
        const QRectF inner(centre.x() - r0, centre.y() - r0, 2 * r0, 2 * r0);
        const QRectF outer(centre.x() - r1, centre.y() - r1, 2 * r1, 2 * r1);
        const int n = static_cast<int>(ring.items.size());

        for (int i = 0; i < n; ++i) {
            QAction* a = ring.items.at(i);
            const double mid = ring.base - i * ring.step;
            const bool whole = (n == 1 && ring.span >= 360.0);
            const double pad = whole ? 0.0 : std::min(0.8, ring.step * 0.05);
            const double a0 = whole ? 0.0 : mid - ring.step / 2.0 + pad;
            const double sweep = whole ? 360.0 : ring.step - 2.0 * pad;

            QPainterPath wedge;
            wedge.arcMoveTo(outer, a0);
            wedge.arcTo(outer, a0, sweep);
            wedge.arcTo(inner, a0 + sweep, -sweep);
            wedge.closeSubpath();

            const bool isHover = (ring.hovered == i);
            const bool onPath  = isHover && k < lastRing;      // an ancestor of the open ring
            p.setPen(QPen(edge, 1.0));
            p.setBrush(onPath ? pathFill : (isHover && a->isEnabled() ? hilite : fill));
            p.drawPath(wedge);

            const double rad = mid * kPi / 180.0;
            const QPointF dir(std::cos(rad), -std::sin(rad));

            // Submenu marker: a small outward-pointing triangle on the outer edge.
            if (a->menu()) {
                const QPointF tip  = centre + dir * (r1 - 3.0);
                const QPointF back = centre + dir * (r1 - 10.0);
                const QPointF side(-dir.y() * 4.5, dir.x() * 4.5);
                QPolygonF tri; tri << tip << back + side << back - side;
                p.setPen(Qt::NoPen);
                p.setBrush(isHover && !onPath ? textHi : (a->isEnabled() ? text : textOff));
                p.drawPolygon(tri);
            }
            // Value-wedge marker: a pair of opposed triangles on the outer edge (the
            // wheel steps it up / down).
            if (adjustable.contains(a)) {
                const QPointF side(-dir.y() * 4.0, dir.x() * 4.0);
                const QPointF upTip   = centre + dir * (r1 - 3.0);
                const QPointF upBack  = centre + dir * (r1 - 9.0);
                const QPointF dnTip   = centre + dir * (r1 - 17.0);
                const QPointF dnBack  = centre + dir * (r1 - 11.0);
                QPolygonF up; up << upTip << upBack + side << upBack - side;
                QPolygonF dn; dn << dnTip << dnBack + side << dnBack - side;
                p.setPen(Qt::NoPen);
                p.setBrush(isHover ? textHi : (a->isEnabled() ? text : textOff));
                p.drawPolygon(up);
                p.drawPolygon(dn);
            }
            // Checked marker: a dot on the inner edge.
            if (a->isCheckable() && a->isChecked()) {
                p.setPen(Qt::NoPen);
                p.setBrush(isHover && !onPath ? textHi : hilite);
                p.drawEllipse(centre + dir * (r0 + 7.0), 3.5, 3.5);
            }

            // Label: word-wrapped in a box centred on the wedge's mid point, as wide as
            // the wedge's chord there (capped), so neighbours rarely overlap.
            const double rm = 0.5 * (r0 + r1);
            const double halfStepRad = std::min(ring.step, 180.0) * kPi / 360.0;
            const double chord = (ring.step >= 180.0) ? 2.0 * rm : 2.0 * rm * std::sin(halfStepRad);
            const double w = std::clamp(chord * 0.92, 34.0, kRingWidth * 1.5);
            const double h = kRingWidth - 14.0;
            QRectF box(0, 0, w, h);
            box.moveCenter(centre + dir * rm);

            const QIcon icon = a->icon();
            if (!icon.isNull()) {
                const int is = 16;
                const QRect ir(static_cast<int>(box.center().x() - is / 2.0),
                               static_cast<int>(box.top() + 2.0), is, is);
                icon.paint(&p, ir, Qt::AlignCenter,
                           a->isEnabled() ? QIcon::Normal : QIcon::Disabled);
                box.setTop(box.top() + is + 3.0);
            }
            p.setPen(!a->isEnabled() ? textOff : ((isHover && !onPath) ? textHi : text));
            p.drawText(box, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, labelFor(a));
        }
    }

    // Hub (cancel zone).
    p.setPen(QPen(edge, 1.0));
    p.setBrush(fill);
    p.drawEllipse(centre, kHubRadius - 4.0, kHubRadius - 4.0);
    p.setPen(QPen(textOff, 1.5));
    const double x = 5.0;
    p.drawLine(centre + QPointF(-x, -x), centre + QPointF(x, x));
    p.drawLine(centre + QPointF(-x, x),  centre + QPointF(x, -x));
}
