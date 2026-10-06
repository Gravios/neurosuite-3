#ifndef KLUSTERS_INPUT_INPUTSCOPE_H
#define KLUSTERS_INPUT_INPUTSCOPE_H

// input/inputscope.h — an InputScope is a view or mode as a context layer (plan §2).
//
// A scope is SELF-DESCRIBING: it is live when its active() predicate reports true
// (e.g. []{ return view.mode() == NEW_CLUSTER; }).  The resolver never hard-codes
// knowledge of any specific view or mode — it just composes whichever scopes report
// active and consults them in layer order.  Adding a view or mode is therefore a
// self-contained unit: declare a scope + its commands, and the resolver, Preferences,
// persistence, conflict-checker and cheat-sheet all discover them.

#include "ctx.h"

#include <QString>
#include <functional>

namespace input {

// Priority layers, lowest to highest.  When several scopes are active at once the
// resolver consults them highest-first, so a Transient (an in-progress lasso) shadows
// a ToolMode, which shadows the Presentation, which shadows the ViewType, which
// shadows App.  The same Left-press thus resolves to zoom / polygon-vertex / node-mark
// / pan purely by which innermost scope binds it — no pile of ifs.
enum class Layer : unsigned char {
    App          = 0,   // always active; global actions
    ViewType     = 1,   // the focused view's type (cluster / waveform / …)
    Presentation = 2,   // a view's alternate presentation (feature / tsne / oblique)
    ToolMode     = 3,   // the active tool (zoom / newCluster / …)
    Transient    = 4    // an in-progress interaction (pendingLasso / boundaryDrag / …)
};

struct InputScope {
    QString id;                            // "view.cluster", "mode.newCluster", "transient.pendingLasso", …
    Layer   layer = Layer::App;
    std::function<bool(const Ctx&)> active; // is this scope in effect for this event?  null == always (App)
};

}  // namespace input

#endif  // KLUSTERS_INPUT_INPUTSCOPE_H
