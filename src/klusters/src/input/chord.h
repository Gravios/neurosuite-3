#ifndef KLUSTERS_INPUT_CHORD_H
#define KLUSTERS_INPUT_CHORD_H

// input/chord.h — a device-agnostic input trigger (plan claude/input-remapping-plan.md §3).
//
// A Chord is "what the user did to begin a command": a key press, a mouse-button
// press / double-click, or a wheel notch, together with the active keyboard
// modifiers.  Keyboard and mouse are ONE mechanism — a mouse button is just a Chord
// with device == Button — so binding, resolving, persistence and conflict-checking
// treat them identically.  Multi-phase gesture bodies (drag / move / release) are
// NOT chords; a Chord names only the PRESS that begins them (the plan's "seam": the
// registry owns which chord in which scope begins a gesture; the gesture body stays
// in the view).
//
//   toString() / fromString()  exact, compact round-trip for persistence.  Only user
//                              overrides are stored (see BindingRegistry), as these.
//   displayString()            one-way, human-readable label for the Preferences page
//                              and cheat-sheet.  Does NOT round-trip.

#include <QString>
#include <Qt>

class QKeySequence;

namespace input {

enum class Device : unsigned char { None = 0, Key, Button, Wheel };

// The trigger phase a Chord fires on.  Press covers key-down and button-down (the
// common case); DoubleClick is Qt's synthesised second click; Wheel is a notch.
// Release and Move are deliberately absent — they belong to the gesture body the
// Command implements, not to the trigger the registry binds.
enum class Phase : unsigned char { Press = 0, DoubleClick, Wheel };

struct Chord {
    Device                device    = Device::None;
    int                   code      = 0;            // Qt::Key_* | one Qt::MouseButton | wheel dir (+1 up / -1 down)
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    Phase                 phase     = Phase::Press;

    bool isValid() const { return device != Device::None; }

    bool operator==(const Chord& o) const {
        return device == o.device && code == o.code
            && modifiers == o.modifiers && phase == o.phase;
    }
    bool operator!=(const Chord& o) const { return !(*this == o); }

    // Only the four "interesting" modifiers (Shift / Ctrl / Alt / Meta) take part in
    // a binding; Keypad / GroupSwitch and friends are masked so a numpad Enter or an
    // X11 group-switch never changes which command a chord resolves to.
    static Qt::KeyboardModifiers normalize(Qt::KeyboardModifiers m) {
        return m & (Qt::ShiftModifier | Qt::ControlModifier
                    | Qt::AltModifier | Qt::MetaModifier);
    }

    // Exact round-trip for persistence: "<device>/<code>/<modifiers>/<phase>" as
    // integers.  Stable across runs; parsed back by fromString.  (Human-readable
    // rendering is displayString(), which does NOT round-trip.)
    QString       toString() const;
    static Chord  fromString(const QString& s);   // invalid Chord if malformed

    // One-way, human-readable label for the UI / cheat-sheet (e.g. "Ctrl+Shift+S",
    // "Right-button", "Ctrl+Wheel up").  Never parsed back.
    QString displayString() const;

    // Convenience builders so default-chord registration sites read well.
    static Chord key(int qtKey, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        return { Device::Key, qtKey, normalize(mods), Phase::Press };
    }
    static Chord button(Qt::MouseButton b, Qt::KeyboardModifiers mods = Qt::NoModifier,
                        Phase ph = Phase::Press) {
        return { Device::Button, int(b), normalize(mods), ph };
    }
    static Chord wheel(int dir, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        return { Device::Wheel, dir < 0 ? -1 : 1, normalize(mods), Phase::Wheel };
    }
};

// Bridge to Qt's QKeySequence, for mirroring the menu/toolbar QActions (which Qt
// dispatches by shortcut) into the registry.  Only a single key-combo is handled: a
// one-element QKeySequence <-> a Key Chord.  An empty or multi-element sequence maps
// to an invalid Chord; a non-Key Chord maps to an empty QKeySequence.
Chord        chordFromKeySequence(const QKeySequence& seq);
QKeySequence keySequenceFromChord(const Chord& c);

}  // namespace input

#endif  // KLUSTERS_INPUT_CHORD_H
