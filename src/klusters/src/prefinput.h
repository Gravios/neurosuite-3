#ifndef PREFINPUT_H
#define PREFINPUT_H

// prefinput.h — the generated "Input" Preferences page (input-remapping plan §5).
//
// Built by ITERATING input::registry(): one row per Command, grouped scope -> category,
// a QKeySequenceEdit for key commands.  A new Command or Scope therefore appears here
// automatically, with no edits to this file — the extensibility the plan is built for.
// Editing a binding writes an OVERRIDE (a diff from the shipped default) to the registry
// and to Configuration (persisted); clearing an edit back to the default drops the
// override.  Per-row Reset and the dialog's global Default restore shipped bindings.
// Within-scope conflicts are tinted live.
//
// Button / wheel commands (none exist until the mouse gestures land) are shown
// read-only for now; a button-combo editor is a later patch.

#include <QWidget>
#include <QList>
#include <QString>

#include "input/chord.h"

class QKeySequenceEdit;
class QLabel;
class QToolButton;

class PrefInput : public QWidget {
    Q_OBJECT
public:
    explicit PrefInput(QWidget* parent = nullptr);

    void updateFromRegistry();   ///< effective chords -> editors (on dialog open)
    void commitToRegistry();     ///< editors -> registry overrides + Configuration map
    void restoreDefaults();      ///< every editor -> its command's shipped default

Q_SIGNALS:
    void changed();              ///< a binding edit — lights the dialog's Apply button

private:
    struct Row {
        QString           commandId;
        QString           scopeId;
        input::Chord      defaultChord;
        bool              keyEditable = false;
        QKeySequenceEdit* edit  = nullptr;   // key commands (editable)
        QLabel*           fixed = nullptr;   // non-key commands (read-only for now)
    };

    void build();                ///< construct the UI from the registry (once, in the ctor)
    void recomputeConflicts();   ///< tint editors that collide within a scope
    input::Chord rowChord(const Row& r) const;   ///< the row's current chord from its editor

    QList<Row> rows_;
};

#endif  // PREFINPUT_H
