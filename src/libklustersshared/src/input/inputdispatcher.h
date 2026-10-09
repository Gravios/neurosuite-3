#ifndef KLUSTERS_INPUT_INPUTDISPATCHER_H
#define KLUSTERS_INPUT_INPUTDISPATCHER_H

// input/inputdispatcher.h — the Qt-event glue (plan claude/input-remapping-plan.md,
// structural decision #1).
//
// Turns a QEvent on a focused view into a Chord, resolves it against the one app-wide
// BindingRegistry, and invokes the matched Command.  This is the ONE place raw Qt
// events become commands: a view funnels its handlers through a single entrypoint
// (BaseFrame::dispatchInput) rather than hand-decoding events in scattered if-piles,
// which is the core maintainability property the plan is built on.
//
// Qt-light: this header pulls no Qt widget headers (QWidget / QEvent appear only as
// forward-declared pointers), so the binding core stays testable without a GUI.

#include "chord.h"
#include "libklustersshared_export.h"

class QEvent;
class QWidget;

namespace input {

class BindingRegistry;

// The single app-wide registry every view registers into and every dispatch resolves
// against — the "one source of truth" (plan §4).  Populated by the per-view
// registerInput() calls as views are ported; read by dispatch().  A function-local
// static, so it is constructed on first use and shared process-wide.
KLUSTERSSHARED_EXPORT BindingRegistry& registry();

// Build a Chord from a Qt input event.  Returns an invalid Chord for anything that is
// not a trigger — mouse move / release, auto-repeat keys, non-input events — so the
// caller falls through to its existing handler.  Only the PRESS (or double-click, or
// wheel notch) that BEGINS an interaction is a trigger; the drag / release body stays
// in the view (the plan's "seam").
// Map a Qt event to the Chord it would trigger.  A held-key auto-repeat is normally NOT a
// fresh trigger, so by default it maps to an invalid Chord (callers fall through).  Pass
// allowAutoRepeat=true to map it anyway — for the app key dispatch, which then consults the
// matched command's Repeat policy to decide whether a held key re-fires.
KLUSTERSSHARED_EXPORT Chord chordFromEvent(const QEvent* ev, bool allowAutoRepeat = false);

// Resolve `ev` for `view` against `reg`; if a command matches, invoke it (Action:
// perform; Gesture: begin; Locked: perform) and return true (handled).  Otherwise
// return false so the caller runs its existing handler — the fall-through that lets
// un-ported views keep working while the migration lands view by view.
KLUSTERSSHARED_EXPORT bool dispatch(QWidget* view, QEvent* ev, const BindingRegistry& reg);
KLUSTERSSHARED_EXPORT bool dispatch(QWidget* view, QEvent* ev);   // uses registry()

}  // namespace input

#endif  // KLUSTERS_INPUT_INPUTDISPATCHER_H
