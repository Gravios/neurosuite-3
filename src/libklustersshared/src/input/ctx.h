#ifndef KLUSTERS_INPUT_CTX_H
#define KLUSTERS_INPUT_CTX_H

// input/ctx.h — the resolution context passed to a Command's invoke() and to the
// scope active() / command enabled() predicates (plan §1, §2).
//
// A scope or command often gates on which view received the input and on the event
// itself (e.g. the base zoom is active only when THE PRESSED frame is in ZOOM mode;
// a gesture's invoke() needs the press position).  A parameterless predicate cannot
// see that, so every predicate and invoke() takes a Ctx.  It is empty ({nullptr,
// nullptr}) when resolution is driven by something other than a live event — the
// Preferences page or cheat-sheet querying enabled(), say — so predicates must treat
// a null view as "no live context" rather than dereferencing it.

class QWidget;
class QEvent;

namespace input {

struct Ctx {
    QWidget* view  = nullptr;   // the view the trigger arrived on (null outside live dispatch)
    QEvent*  event = nullptr;   // the originating QEvent (for position / phase details)
};

}  // namespace input

#endif  // KLUSTERS_INPUT_CTX_H
