/***************************************************************************
 *                        inputdispatcher_test.cpp                         *
 *                                                                         *
 *  Standalone test for the Qt-event glue (plan                            *
 *  claude/input-remapping-plan.md, decision #1): chordFromEvent's mapping  *
 *  of QKeyEvent / QMouseEvent / QWheelEvent to a Chord (triggers vs        *
 *  gesture-body / auto-repeat -> invalid), and dispatch() end-to-end       *
 *  against a BindingRegistry (match -> invoke + true; unbound -> false).   *
 *  Exits 0 on success, non-zero on first failure.                         *
 *                                                                         *
 *  No widgets, no QApplication — event objects construct fine standalone;  *
 *  the view pointer is only stored, never dereferenced.  Links Qt6         *
 *  Core+Gui.  See src/klusters/tests/CMakeLists.txt                        *
 *  (klusters_test_inputdispatcher).                                        *
 ***************************************************************************/
#include "input/bindingregistry.h"
#include "input/inputdispatcher.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPoint>
#include <QPointF>

#include <cstdio>

using namespace input;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    // ── chordFromEvent: keys ─────────────────────────────────────────────
    {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
        const Chord c = chordFromEvent(&press);
        CHECK(c.isValid());
        CHECK(c == Chord::key(Qt::Key_S, Qt::ControlModifier));

        // Auto-repeat is not a fresh trigger -> invalid.
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier,
                         QString(), /*autorep=*/true);
        CHECK(!chordFromEvent(&repeat).isValid());

        // KeyRelease is not a trigger.
        QKeyEvent rel(QEvent::KeyRelease, Qt::Key_S, Qt::NoModifier);
        CHECK(!chordFromEvent(&rel).isValid());
    }

    // ── chordFromEvent: mouse buttons ────────────────────────────────────
    {
        const QPointF p(10, 10), g(10, 10);
        QMouseEvent pressL(QEvent::MouseButtonPress, p, g,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        const Chord cl = chordFromEvent(&pressL);
        CHECK(cl == Chord::button(Qt::LeftButton));
        CHECK(cl.phase == Phase::Press);

        QMouseEvent pressR(QEvent::MouseButtonPress, p, g,
                           Qt::RightButton, Qt::RightButton, Qt::ControlModifier);
        CHECK(chordFromEvent(&pressR) == Chord::button(Qt::RightButton, Qt::ControlModifier));

        QMouseEvent dbl(QEvent::MouseButtonDblClick, p, g,
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        const Chord cd = chordFromEvent(&dbl);
        CHECK(cd.phase == Phase::DoubleClick);
        CHECK(cd == Chord::button(Qt::LeftButton, Qt::NoModifier, Phase::DoubleClick));

        // Release and move are gesture-body, not triggers -> invalid.
        QMouseEvent rel(QEvent::MouseButtonRelease, p, g,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        CHECK(!chordFromEvent(&rel).isValid());
        QMouseEvent move(QEvent::MouseMove, p, g,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        CHECK(!chordFromEvent(&move).isValid());
    }

    // ── chordFromEvent: wheel ────────────────────────────────────────────
    {
        QWheelEvent up(QPointF(0, 0), QPointF(0, 0), QPoint(0, 0), QPoint(0, 120),
                       Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        const Chord cu = chordFromEvent(&up);
        CHECK(cu == Chord::wheel(+1, Qt::ControlModifier));
        CHECK(cu.phase == Phase::Wheel);

        QWheelEvent down(QPointF(0, 0), QPointF(0, 0), QPoint(0, 0), QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        CHECK(chordFromEvent(&down) == Chord::wheel(-1));
    }

    // ── dispatch end-to-end against a registry ───────────────────────────
    {
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"), Layer::App, nullptr });
        int invoked = 0;
        Command c;
        c.id = QStringLiteral("app.doit");
        c.scopeId = QStringLiteral("app");
        c.defaultChord = Chord::key(Qt::Key_D, Qt::ControlModifier);
        c.invoke = [&invoked](const Ctx& ctx){ invoked = (ctx.view == nullptr) ? 1 : 2; };
        reg.addCommand(c);

        // Bound chord -> invoked, handled.
        QKeyEvent hit(QEvent::KeyPress, Qt::Key_D, Qt::ControlModifier);
        CHECK(dispatch(nullptr, &hit, reg) == true);
        CHECK(invoked == 1);                          // invoke ran, saw the (null) view in Ctx

        // Unbound chord -> not handled, caller falls through.
        QKeyEvent miss(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
        CHECK(dispatch(nullptr, &miss, reg) == false);

        // A non-trigger event never dispatches.
        QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(0,0), QPointF(0,0),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        CHECK(dispatch(nullptr, &rel, reg) == false);
    }

    if (g_fail == 0) std::printf("inputdispatcher_test: OK\n");
    return g_fail == 0 ? 0 : 1;
}
