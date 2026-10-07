#ifndef KLUSTERS_INPUT_KEYMAPPROFILE_H
#define KLUSTERS_INPUT_KEYMAPPROFILE_H

// input/keymapprofile.h — a named set of input-binding overrides (plan
// claude/input-remapping-plan.md, "Keymap profiles").
//
// A keymap profile is a *named layout* of the keyboard + mouse bindings: the set of
// per-command chord overrides that reproduces one configuration.  It is exactly the
// override layer the BindingRegistry already carries (BindingRegistry::overrides /
// setOverride), given a name so it can be saved, shipped as a preset, and swapped in.
//
//   - A profile stores only the commands it REBINDS (the diff from the shipped
//     defaults).  A command the profile does not mention follows its registered
//     default, so a build that adds a new command never leaves it unbound.
//   - The empty-bindings profile is therefore "Default": applying it clears every
//     override and restores the shipped bindings.
//
// This module is Qt-light (QString / QMap / Chord only, no widgets), so it is covered
// by klusters_test_keymapprofile the same way the binding core is.  Persisting user
// profiles (Configuration / QSettings), loading bundled presets from :/keymaps/, and
// the Preferences UI are layered on top in later patches and are NOT part of this core.

#include "chord.h"

#include <QString>
#include <QMap>

namespace input {

class BindingRegistry;

struct KeymapProfile {
    QString              name;         // display name ("Default", "AZERTY", "Left-hand", …)
    QString              description;  // optional one-line description
    QMap<QString, Chord> bindings;     // commandId -> overriding chord (the diff from defaults)

    bool isValid() const { return !name.isEmpty(); }
};

// Serialize to a line-based, hand-authorable text form (also what a bundled :/keymaps/
// *.keymap file contains):
//
//     # Klusters keymap profile
//     name = AZERTY
//     description = French AZERTY layout
//
//     <commandId> = <chord>          # chord is Chord::toString() — the exact,
//     cluster.pan = 2/1/67108864/0/1 # round-trippable persistence form
//
// Reserved keys `name` and `description` set those fields; every other `key = value`
// line is a binding (command ids are namespaced — "cluster.pan", "app.prefs" — so they
// never collide with the reserved keys).  Bindings are written sorted by command id
// (QMap order) for a stable, diffable file.
QString serializeKeymap(const KeymapProfile& profile);

// Parse the text form.  Robust by design: blank lines and `#` comments are skipped; a
// binding line whose value is not a valid Chord::toString(), or that references nothing
// meaningful, is skipped rather than failing the whole profile (so a preset mentioning a
// command this build lacks, or an encoding a newer build changed, is ignored).  If
// `ok` is non-null it is set false when the text carried no `name`.
KeymapProfile parseKeymap(const QString& text, bool* ok = nullptr);

// Snapshot the registry's CURRENT override set into a profile — the user's changes from
// the shipped defaults.  Commands left at their default are not included.
KeymapProfile captureKeymap(const BindingRegistry& reg, const QString& name,
                            const QString& description = QString());

// Replace the registry's overrides with the profile's: clearAllOverrides(), then
// setOverride() for each binding.  Applying a profile with no bindings ("Default")
// therefore restores every command to its shipped default.  This touches only the
// registry's override layer; pushing the result to QAction shortcuts / refreshing the
// Preferences page is the caller's job (KlustersApp::applyInputOverridesToActions).
void applyKeymap(BindingRegistry& reg, const KeymapProfile& profile);

}  // namespace input

#endif  // KLUSTERS_INPUT_KEYMAPPROFILE_H
