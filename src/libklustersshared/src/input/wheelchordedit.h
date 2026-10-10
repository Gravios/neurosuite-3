#ifndef WHEELCHORDEDIT_H
#define WHEELCHORDEDIT_H

// wheelchordedit.h — a compact editor for a mouse-WHEEL Chord (input-remapping plan §5).
//
// The wheel counterpart of ButtonChordEdit: edits the wheel DIRECTION (up / down) and the
// MODIFIERS.  The phase (always Wheel) and the modifier-match policy (Exact / AtLeast) are
// carried through from the chord handed to setChord() and preserved on chord(), so a
// gesture registered "any modifier" (AtLeast) stays AtLeast.  Makes the ported Ctrl+wheel
// zoom commands rebindable — the PrefInput wheel rows were read-only until it landed.

#include <QWidget>

#include "libklustersshared_export.h"
#include "input/chord.h"

class QComboBox;
class QCheckBox;

class KLUSTERSSHARED_EXPORT WheelChordEdit : public QWidget {
    Q_OBJECT
public:
    explicit WheelChordEdit(QWidget* parent = nullptr);

    void         setChord(const input::Chord& c);   ///< load direction + modifiers; remember phase + modMatch
    input::Chord chord() const;                      ///< rebuild with the remembered phase + modMatch

    void setConflict(bool on);   ///< tint when this binding clashes within its scope

Q_SIGNALS:
    void chordChanged();         ///< a user edit (not a programmatic setChord)

private:
    QComboBox* dir_   = nullptr;   // item data: +1 (up) / -1 (down)
    QCheckBox* ctrl_  = nullptr;
    QCheckBox* alt_   = nullptr;
    QCheckBox* shift_ = nullptr;
    QCheckBox* meta_  = nullptr;
    input::Phase    phase_    = input::Phase::Wheel;     // carried through from setChord
    input::ModMatch modMatch_ = input::ModMatch::Exact;  // carried through from setChord
};

#endif  // WHEELCHORDEDIT_H
