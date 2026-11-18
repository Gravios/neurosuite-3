// radialmenu_test.cpp — the radial menu's response to cursor MOVEMENT (radialmenu.h).
//
// A child ring is wider than its parent wedge, so the straight path from a parent
// wedge to most of its children crosses a neighbouring parent wedge.  A move that
// would close an open ring is held until the cursor settles; it is dropped when the
// cursor reaches the open ring first.  Checked here, on the offscreen platform:
//   - a continuous move from a parent across its neighbour into the parent's own
//     child ring never retargets to the neighbour, and lands on the child aimed at;
//   - stopping on the neighbour retargets to it after the dwell (which also proves
//     the path above really crosses the neighbour);
//   - passing through the hub does not collapse the rings, stopping there does;
//   - a press + release acts on the wedge under the cursor without waiting.
// Own main; run via ctest with QT_QPA_PLATFORM=offscreen.

#include "radialmenu.h"

#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <string>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static void waitMs(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

namespace {
const double kPi = 3.14159265358979323846;

// The geometry of radialmenu.cpp: hub 30 px, rings 82 px thick with a 3 px gap; the
// root's first wedge is centred at 12 o'clock and the wedges read clockwise.
struct Rig {
    QMenu root, subA, subB;
    QAction *a = nullptr, *b = nullptr, *c = nullptr;
    QList<QAction*> aKids;
    RadialMenu menu;
    QPointF centre;
    QList<QAction*> log;        // every hovered() emission
    QAction* picked = nullptr;

    explicit Rig(RadialMenu::Mode mode) : menu(nullptr, mode) {
        for (int i = 0; i < 5; ++i) aKids.append(subA.addAction(QStringLiteral("a%1").arg(i + 1)));
        subB.addAction(QStringLiteral("b1"));
        subB.addAction(QStringLiteral("b2"));
        a = root.addMenu(&subA); a->setText(QStringLiteral("A"));   // 12 o'clock: 60..120 deg
        b = root.addMenu(&subB); b->setText(QStringLiteral("B"));   // clockwise of A: 0..60 deg
        c = root.addAction(QStringLiteral("C"));                    // 300..360 deg
        root.addAction(QStringLiteral("D"));
        root.addAction(QStringLiteral("E"));
        root.addAction(QStringLiteral("F"));
        menu.setMenu(&root);
        QObject::connect(&menu, &RadialMenu::hovered, [this](QAction* x){ log.append(x); });
        QObject::connect(&menu, &RadialMenu::triggered, [this](QAction* x){ picked = x; });
        centre = QPointF(menu.width() / 2.0, menu.height() / 2.0);
    }
    QPointF polar(double radius, double deg) const {
        return centre + QPointF(radius * std::cos(deg * kPi / 180.0), -radius * std::sin(deg * kPi / 180.0));
    }
    void send(QEvent::Type type, const QPointF& p, Qt::MouseButton button = Qt::NoButton) {
        const Qt::MouseButtons held = (type == QEvent::MouseButtonPress) ? Qt::MouseButtons(button) : Qt::NoButton;
        QMouseEvent ev(type, p, menu.mapToGlobal(p), button, held, Qt::NoModifier);
        QApplication::sendEvent(&menu, &ev);
    }
    void moveTo(const QPointF& p) { send(QEvent::MouseMove, p); }
    // A continuous move: `steps` samples `gapMs` apart, each far more than a still cursor's jitter.
    void glide(const QPointF& from, const QPointF& to, int steps, int gapMs) {
        for (int i = 1; i <= steps; ++i) {
            moveTo(from + (to - from) * (static_cast<double>(i) / steps));
            waitMs(gapMs);
        }
    }
    QAction* last() const { return log.isEmpty() ? nullptr : log.last(); }
};
} // namespace

// A's child ring is 5 wedges of 34 deg centred on 90: a5, its clockwise end, sits at
// 22 deg -- radially beyond B.  The straight line from A to a5 runs over B in ring 0.
static void testMovingKeepsTheRing() {
    Rig rig(RadialMenu::Mode::Embedded);
    const QPointF onA  = rig.polar(70.0, 90.0);
    const QPointF onA5 = rig.polar(155.0, 22.0);

    rig.moveTo(onA);
    check(rig.last() == rig.a, "hovering A opens it");
    rig.log.clear();
    rig.glide(onA, onA5, 12, 8);                         // ~12 px per sample, ~1500 px/s
    check(!rig.log.contains(rig.b), "moving across B toward A's ring never retargets to B");
    check(rig.last() == rig.aKids.at(4), "the move lands on the child of A it was aimed at");
    waitMs(250);
    check(rig.last() == rig.aKids.at(4), "and stays there once the cursor rests");
}

static void testStoppingRetargets() {
    Rig rig(RadialMenu::Mode::Embedded);
    const QPointF onA = rig.polar(70.0, 90.0);
    const QPointF mid = onA + (rig.polar(155.0, 22.0) - onA) * 0.5;   // on the same line, over B

    rig.moveTo(onA);
    rig.log.clear();
    rig.glide(onA, mid, 6, 8);
    check(!rig.log.contains(rig.b), "still moving: B has not taken over");
    waitMs(250);
    check(rig.last() == rig.b, "stopped on B: B takes over (the line to a5 does cross B)");
    rig.moveTo(rig.polar(155.0, 22.0));
    check(rig.last() != rig.aKids.at(4) && rig.last() != rig.a && rig.last() != nullptr,
          "A's ring is closed: that spot now belongs to B's ring");

    // A slow creep counts as stopped: 1 px every 40 ms never leaves the still radius in time.
    Rig creep(RadialMenu::Mode::Embedded);
    creep.moveTo(creep.polar(70.0, 90.0));
    creep.log.clear();
    const QPointF onB = creep.polar(70.0, 30.0);
    creep.moveTo(onB);
    for (int i = 1; i <= 5; ++i) { creep.moveTo(onB + QPointF(0.5 * i, 0.0)); waitMs(40); }
    check(creep.last() == creep.b, "a creeping cursor on B is a stopped one");
}

static void testHub() {
    Rig rig(RadialMenu::Mode::Embedded);
    rig.moveTo(rig.polar(70.0, 90.0));
    rig.moveTo(rig.polar(155.0, 90.0));                  // onto a3, in A's ring
    check(rig.last() == rig.aKids.at(2), "on a3");
    rig.log.clear();
    rig.glide(rig.polar(155.0, 90.0), rig.polar(155.0, 110.0), 4, 8);   // sideways within A's ring
    rig.log.clear();
    rig.moveTo(rig.centre);                              // through the hub, not stopping
    rig.moveTo(rig.polar(155.0, 56.0));                  // and back out to a4
    check(!rig.log.contains(nullptr), "passing through the hub does not collapse the rings");
    check(rig.last() == rig.aKids.at(3), "A's ring is still there on the way out");
    rig.moveTo(rig.centre);
    waitMs(250);
    check(rig.last() == nullptr, "stopping in the hub collapses to the root ring");
}

static void testClickDoesNotWait() {
    Rig rig(RadialMenu::Mode::Popup);
    rig.menu.popup(QPoint(400, 400));
    rig.centre = rig.menu.mapFromGlobal(QPoint(400, 400));
    rig.moveTo(rig.polar(70.0, 90.0));
    check(rig.last() == rig.a, "popup: hovering A opens it");
    const QPointF onC = rig.polar(70.0, -30.0);
    rig.moveTo(onC);                                     // held: A's ring would close
    check(rig.last() == rig.a, "popup: the move onto C is held");
    rig.send(QEvent::MouseButtonPress, onC, Qt::LeftButton);
    rig.send(QEvent::MouseButtonRelease, onC, Qt::LeftButton);
    check(rig.picked == rig.c, "a click on C picks C without waiting for the dwell");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    testMovingKeepsTheRing();
    testStoppingRetargets();
    testHub();
    testClickDoesNotWait();
    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
