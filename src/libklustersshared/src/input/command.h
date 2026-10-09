#ifndef KLUSTERS_INPUT_COMMAND_H
#define KLUSTERS_INPUT_COMMAND_H

// input/command.h — a Command is the unit of behavior (plan §1).
//
// A keystroke, a toolbar button and a mouse button all resolve to the SAME Command,
// so there is one definition of "what this does" and one place it can be rebound.
// A Command is declared next to the code it drives (a view's / mode's registerInput),
// never in a central table — that locality is the whole point of the architecture.

#include "chord.h"
#include "ctx.h"

#include <QString>
#include <functional>

namespace input {

enum class Kind : unsigned char {
    Action,    // atomic: invoke() performs it (a menu action, a key shortcut)
    Gesture,   // stateful: invoke() only BEGINS it; the drag/commit body lives in the view
    Locked     // modal, shown in the UI but not rebindable (Enter / Esc / D confirmations)
};

// Whether a command may fire while a text / key-capture field holds focus.  The app-level
// key dispatch already yields to a focused field for Default commands (so a letter typed
// into a spin box is text, not a shortcut).  Always is for the structural navigation keys
// — Tab, PageUp/Down, the focus-ring cycle — that must work FROM those toolbar fields: the
// focus ring includes them, so the key has to move focus out rather than be swallowed as
// text.  Either way the key is still gated to this window and blocked while a modal dialog
// is up (so a Preferences binding editor records the key instead of firing it).
enum class Focus : unsigned char {
    Default,   // yield to a focused text / key-capture field (the common case)
    Always     // fire even in a text field (still window-scoped, never through a modal)
};

// Whether a command fires again on a held-key auto-repeat.  FallThrough — the default and
// today's behavior for every command — does NOT re-fire: the auto-repeat is let through
// (the dispatcher treats a held key as not a fresh trigger).  Fire re-invokes on each
// auto-repeat, for the keys whose whole point is to repeat while held (t-SNE perplexity
// step, a focus-ring cycle).  A command that does re-fire also consumes the repeat, so it
// never leaks to the widget beneath.
enum class Repeat : unsigned char {
    FallThrough,   // auto-repeat neither re-fires nor is consumed (default)
    Fire           // auto-repeat re-fires (and is consumed)
};

struct Command {
    QString id;          // globally unique, e.g. "cluster.lasso", "app.prefs"
    QString scopeId;     // the InputScope that owns it (see inputscope.h)
    QString label;       // shown in Preferences + cheat-sheet (already tr()'d by the caller)
    QString category;    // grouping within the page

    Kind    kind = Kind::Action;
    Focus   focus = Focus::Default;     // may it fire while a text field has focus? (keys only)
    Repeat  repeat = Repeat::FallThrough; // does it re-fire on a held-key auto-repeat? (keys only)
    Chord   defaultChord;

    // A mirror of something Qt already dispatches (a menu/toolbar QAction, whose
    // shortcut Qt fires directly).  It belongs in the registry so it shows in
    // Preferences + the cheat-sheet and is rebindable (the override is pushed back
    // onto the QAction), but it must NOT be resolver-dispatched — resolve() skips it,
    // so the event path never double-fires it alongside Qt.
    bool    external = false;

    std::function<bool(const Ctx&)> enabled;   // "when" predicate; null == always enabled
    std::function<void(const Ctx&)> invoke;    // Action: perform; Gesture: begin
};

}  // namespace input

#endif  // KLUSTERS_INPUT_COMMAND_H
