#ifndef KLUSTERS_INPUT_BINDINGREGISTRY_H
#define KLUSTERS_INPUT_BINDINGREGISTRY_H

// input/bindingregistry.h — the one source of truth for input (plan §4).
//
// Holds every registered Command and InputScope, the shipped default chord per
// command, and a thin OVERRIDE layer (user edits, persisted as diffs-from-default so
// that changing a shipped default still propagates to commands the user never
// touched).  The Resolver turns a Chord into the Command it should invoke by composing
// the currently-active scopes in layer order.  Preferences, the cheat-sheet and the
// conflict-checker all derive from this registry, so a new Command or Scope surfaces
// everywhere with no edits elsewhere — that is the extensibility the plan is built for.
//
// No Qt-Widgets dependency: this is the Qt-light core, unit-tested standalone
// (klusters_test_bindingregistry).  The Qt-event glue lives in InputDispatcher.

#include "chord.h"
#include "command.h"
#include "inputscope.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace input {

class BindingRegistry {
public:
    // ── registration (co-located with the view / mode that owns the behavior) ──
    // Re-adding an id replaces the previous entry (so a view can re-register idempotently).
    void addScope(const InputScope& s);
    void addCommand(const Command& c);

    // ── effective binding = user override if set, else the shipped default ──
    Chord effectiveChord(const QString& commandId) const;   // invalid Chord if command unknown
    bool  hasOverride(const QString& commandId) const;
    void  setOverride(const QString& commandId, const Chord& c);
    void  clearOverride(const QString& commandId);          // "restore default" for one command
    void  clearAllOverrides();
    // Only the user's diffs from default, for persistence (Configuration stores these).
    QList<QPair<QString, Chord>> overrides() const;

    // ── resolution ──
    // Among every scope whose active(ctx) reports true, consult them highest-layer
    // first (Transient → … → App; ties broken so a later-registered scope shadows an
    // earlier one in the same layer).  Within a scope, the first command whose effective
    // chord equals `chord` AND whose enabled(ctx) is not false wins.  `ctx` carries the
    // view the event arrived on + the event, so a shared command (e.g. the base zoom)
    // can gate on the pressed frame's state; it is empty for non-event queries.  Returns
    // nullptr when nothing matches, so the caller falls through to Qt / the base handler
    // — this fall-through is what lets un-ported views keep their current behavior during
    // the incremental migration.  The returned pointer is owned by the registry and valid
    // until the next addCommand / override change.
    const Command* resolve(const Chord& chord, const Ctx& ctx = {}) const;

    // ── introspection (Preferences / cheat-sheet / conflict UI) ──
    const QList<Command>&    commands() const { return commands_; }
    const QList<InputScope>& scopes()   const { return scopes_; }
    const Command*    command(const QString& id) const;
    const InputScope* scope(const QString& id)   const;

    // Two commands in the SAME scope bound to the same effective chord: always a real
    // conflict, since one shadows the other whenever that scope is active.  Cross-scope
    // repeats are legal by design (Left means different things in different modes) and
    // are not reported here.
    struct Conflict { QString scopeId; Chord chord; QStringList commandIds; };
    QList<Conflict> conflicts() const;

private:
    QList<InputScope>   scopes_;
    QList<Command>      commands_;
    QHash<QString, int> scopeIndex_;     // scope id   -> index in scopes_
    QHash<QString, int> commandIndex_;   // command id -> index in commands_
    QHash<QString, Chord> overrides_;    // command id -> override chord
};

}  // namespace input

#endif  // KLUSTERS_INPUT_BINDINGREGISTRY_H
