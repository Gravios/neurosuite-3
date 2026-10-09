// input/inputdispatcher.cpp — see input/inputdispatcher.h.

#include "inputdispatcher.h"

#include "bindingregistry.h"
#include "command.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

namespace input {

BindingRegistry& registry()
{
    static BindingRegistry r;   // the one app-wide instance (Meyers singleton)
    return r;
}

Chord chordFromEvent(const QEvent* ev, bool allowAutoRepeat)
{
    if (!ev) return {};
    switch (ev->type()) {
    case QEvent::KeyPress:
    case QEvent::ShortcutOverride: {
        // ShortcutOverride carries the same key+modifiers as the KeyPress that
        // follows it; mapping it lets a caller claim the override for a key it will
        // handle on KeyPress (so QAction shortcuts / list type-ahead do not eat it).
        const auto* k = static_cast<const QKeyEvent*>(ev);
        if (k->isAutoRepeat() && !allowAutoRepeat) return {};   // a held key is not a fresh trigger (unless the caller opts in)
        const int key = k->key();
        if (key == 0 || key == Qt::Key_unknown) return {};
        return Chord::key(key, k->modifiers());
    }
    case QEvent::MouseButtonPress: {
        const auto* m = static_cast<const QMouseEvent*>(ev);
        return Chord::button(m->button(), m->modifiers(), Phase::Press);
    }
    case QEvent::MouseButtonDblClick: {
        const auto* m = static_cast<const QMouseEvent*>(ev);
        return Chord::button(m->button(), m->modifiers(), Phase::DoubleClick);
    }
    case QEvent::Wheel: {
        const auto* w = static_cast<const QWheelEvent*>(ev);
        const int dy = w->angleDelta().y();
        if (dy == 0) return {};                           // horizontal-only wheel is not bound
        return Chord::wheel(dy, w->modifiers());
    }
    default:
        // MouseButtonRelease / MouseMove / everything else: gesture body or non-input,
        // not a trigger — fall through.
        return {};
    }
}

bool dispatch(QWidget* view, QEvent* ev, const BindingRegistry& reg)
{
    const Chord c = chordFromEvent(ev);
    if (!c.isValid()) return false;
    Ctx ctx;
    ctx.view  = view;
    ctx.event = ev;
    const Command* cmd = reg.resolve(c, ctx);             // scopes/enabled gate on this event's ctx
    if (!cmd) return false;                               // nothing bound -> caller falls through
    if (cmd->invoke) cmd->invoke(ctx);
    return true;
}

bool dispatch(QWidget* view, QEvent* ev)
{
    return dispatch(view, ev, registry());
}

}  // namespace input
