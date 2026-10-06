#ifndef KLUSTERS_INPUT_COMMAND_H
#define KLUSTERS_INPUT_COMMAND_H

// input/command.h — a Command is the unit of behavior (plan §1).
//
// A keystroke, a toolbar button and a mouse button all resolve to the SAME Command,
// so there is one definition of "what this does" and one place it can be rebound.
// A Command is declared next to the code it drives (a view's / mode's registerInput),
// never in a central table — that locality is the whole point of the architecture.

#include "chord.h"

#include <QString>
#include <functional>

class QWidget;
class QEvent;

namespace input {

// Everything a command needs to run.  Deliberately small for P0; it grows as ports
// need more (world coordinates, the resolved chord, …) without touching callers that
// ignore the new fields.
struct Ctx {
    QWidget* view  = nullptr;   // the focused view the trigger arrived on
    QEvent*  event = nullptr;   // the originating QEvent (for position / phase details)
};

enum class Kind : unsigned char {
    Action,    // atomic: invoke() performs it (a menu action, a key shortcut)
    Gesture,   // stateful: invoke() only BEGINS it; the drag/commit body lives in the view
    Locked     // modal, shown in the UI but not rebindable (Enter / Esc / D confirmations)
};

struct Command {
    QString id;          // globally unique, e.g. "cluster.lasso", "app.prefs"
    QString scopeId;     // the InputScope that owns it (see inputscope.h)
    QString label;       // shown in Preferences + cheat-sheet (already tr()'d by the caller)
    QString category;    // grouping within the page

    Kind    kind = Kind::Action;
    Chord   defaultChord;

    std::function<bool()>           enabled;   // "when" predicate; null == always enabled
    std::function<void(const Ctx&)> invoke;    // Action: perform; Gesture: begin
};

}  // namespace input

#endif  // KLUSTERS_INPUT_COMMAND_H
