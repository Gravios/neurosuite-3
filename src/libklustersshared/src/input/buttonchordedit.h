#ifndef BUTTONCHORDEDIT_H
#define BUTTONCHORDEDIT_H

// buttonchordedit.h — a compact editor for a mouse-button Chord (input-remapping plan §5).
//
// Edits the BUTTON and the MODIFIERS of a Button chord.  The phase (Press / DoubleClick)
// and the modifier-match policy (Exact / AtLeast) are structural to the gesture, not things
// a user picks, so they are carried through from the chord handed to setChord() and preserved
// on chord(): a gesture registered as "any modifier" (AtLeast) stays AtLeast, one registered
// exact stays exact.  This is the first editor that makes the ported mouse gestures
// rebindable — the PrefInput button rows were read-only until it landed.

#include <QWidget>

#include "libklustersshared_export.h"
#include "input/chord.h"

class QComboBox;
class QCheckBox;

class KLUSTERSSHARED_EXPORT ButtonChordEdit : public QWidget {
    Q_OBJECT
public:
    explicit ButtonChordEdit(QWidget* parent = nullptr);

    void         setChord(const input::Chord& c);   ///< load button + modifiers; remember phase + modMatch
    input::Chord chord() const;                      ///< rebuild with the remembered phase + modMatch

    void setConflict(bool on);   ///< tint when this binding clashes within its scope

Q_SIGNALS:
    void chordChanged();         ///< a user edit (not a programmatic setChord)

private:
    QComboBox* button_ = nullptr;
    QCheckBox* ctrl_   = nullptr;
    QCheckBox* alt_    = nullptr;
    QCheckBox* shift_  = nullptr;
    QCheckBox* meta_   = nullptr;
    input::Phase    phase_    = input::Phase::Press;      // carried through from setChord
    input::ModMatch modMatch_ = input::ModMatch::Exact;   // carried through from setChord
};

#endif  // BUTTONCHORDEDIT_H
