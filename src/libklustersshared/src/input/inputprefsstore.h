#ifndef INPUT_INPUTPREFSSTORE_H
#define INPUT_INPUTPREFSSTORE_H

// inputprefsstore.h — the persistence seam for the shared Input Preferences page (PrefInput).
//
// PrefInput lives in libklustersshared and is reused by every neurosuite app (klusters,
// neuroscope, …).  The page itself is app-agnostic — it iterates the process-wide
// input::registry() — but it must read and write two things that live in each app's own
// Configuration: the per-command binding overrides and the user's saved keymap layouts.  Rather
// than couple PrefInput to any one app's Configuration class, each app's Configuration implements
// this tiny interface and hands it to the page.
//
// Both payloads are plain string maps so the store stays trivial: the Chord<->string and
// KeymapProfile<->text serialization is done by the shared input/keymapprofile.h + chord bridges,
// and the store only has to persist the resulting strings (typically via QSettings).
//   - binding overrides: commandId  -> Chord::toString()
//   - keymap profiles:   profileName -> serializeKeymap(profile)

#include "libklustersshared_export.h"

#include <QMap>
#include <QString>

namespace input {

struct KLUSTERSSHARED_EXPORT InputPrefsStore {
    virtual ~InputPrefsStore();

    /// The per-command binding overrides (commandId -> Chord::toString()); empty when none.
    virtual const QMap<QString, QString>& getInputBindingOverrides() const = 0;
    virtual void setInputBindingOverrides(const QMap<QString, QString>& overrides) = 0;

    /// The user's saved keymap layouts (profileName -> serialized keymap text); empty when none.
    virtual const QMap<QString, QString>& getInputProfiles() const = 0;
    virtual void setInputProfiles(const QMap<QString, QString>& profiles) = 0;
};

}  // namespace input

#endif  // INPUT_INPUTPREFSSTORE_H
