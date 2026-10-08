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
// Key commands use a QKeySequenceEdit; button commands use a ButtonChordEdit (the
// button-combo editor); wheel commands use a WheelChordEdit (direction + modifiers).  The
// read-only label remains only as a fallback for any other device.

#include <QWidget>
#include <QList>
#include <QMap>
#include <QString>

#include "input/chord.h"
#include "input/keymapprofile.h"   // KeymapProfile: the saved / bundled keymap layouts

class QKeySequenceEdit;
class QLabel;
class QToolButton;
class QComboBox;
class ButtonChordEdit;
class WheelChordEdit;

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
        QKeySequenceEdit* edit       = nullptr;   // key / unbound commands
        ButtonChordEdit*  buttonEdit = nullptr;   // button commands
        WheelChordEdit*   wheelEdit  = nullptr;   // wheel commands
        QLabel*           fixed      = nullptr;   // any other device (read-only fallback)
    };

    void build();                ///< construct the UI from the registry (once, in the ctor)
    void recomputeConflicts();   ///< tint editors that collide within a scope
    input::Chord rowChord(const Row& r) const;   ///< the row's current chord from its editor

    // ── Keymap layouts (profiles) ────────────────────────────────────────────
    // A layout is a named set of binding overrides (input/keymapprofile.h).  The bar at
    // the top of the page loads one into the editor rows (Apply), or captures the current
    // rows as a new user layout (Save As).  Bundled presets (compiled-in, :/keymaps) are
    // read-only; user layouts persist in Configuration (committed with the bindings on
    // the dialog's Apply/OK, so the whole page stays transactional).
    void loadProfiles();                 ///< bundled_ from :/keymaps + userProfiles_ from Configuration
    void rebuildProfileCombo(const QString& select = QString());
    void updateProfileButtons();         ///< Rename/Delete enabled only for a user layout
    void applyProfileToRows(const input::KeymapProfile& p);   ///< load a layout into the editors
    input::KeymapProfile captureRowsAsProfile(const QString& name) const;  ///< editors -> diff
    void commitProfiles();               ///< userProfiles_ -> Configuration (serialized)

    QList<Row> rows_;

    QComboBox*  profileCombo_   = nullptr;
    QToolButton* applyProfileBtn_ = nullptr;
    QToolButton* saveAsBtn_     = nullptr;
    QToolButton* renameBtn_     = nullptr;
    QToolButton* deleteBtn_     = nullptr;
    QToolButton* resetAllBtn_   = nullptr;   ///< clear every override -> shipped defaults
    QList<input::KeymapProfile>         bundled_;       ///< read-only presets (:/keymaps)
    QMap<QString, input::KeymapProfile> userProfiles_;  ///< name -> user layout
};

#endif  // PREFINPUT_H
