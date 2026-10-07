// input/keymapprofile.cpp — see input/keymapprofile.h.

#include "keymapprofile.h"
#include "bindingregistry.h"

#include <QStringList>

namespace input {

QString serializeKeymap(const KeymapProfile& profile)
{
    QString out;
    out += QStringLiteral("# Klusters keymap profile\n");
    out += QStringLiteral("name = ") + profile.name + QLatin1Char('\n');
    if (!profile.description.isEmpty())
        out += QStringLiteral("description = ") + profile.description + QLatin1Char('\n');
    out += QLatin1Char('\n');
    // QMap iterates sorted by key, so the binding block is stable and diffable.
    for (auto it = profile.bindings.constBegin(); it != profile.bindings.constEnd(); ++it)
        out += it.key() + QStringLiteral(" = ") + it.value().toString() + QLatin1Char('\n');
    return out;
}

KeymapProfile parseKeymap(const QString& text, bool* ok)
{
    KeymapProfile profile;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;                                   // blank line / comment
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;                                   // not a `key = value` line
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();
        if (key.isEmpty())
            continue;
        if (key == QLatin1String("name"))        { profile.name = val;        continue; }
        if (key == QLatin1String("description")) { profile.description = val; continue; }
        // Any other key is a command id; its value is a Chord::toString().  An
        // unparseable chord (a command this build lacks, or an encoding a newer build
        // changed) is skipped so one bad line never discards the whole profile.
        const Chord c = Chord::fromString(val);
        if (c.isValid())
            profile.bindings.insert(key, c);
    }
    if (ok)
        *ok = !profile.name.isEmpty();
    return profile;
}

KeymapProfile captureKeymap(const BindingRegistry& reg, const QString& name,
                            const QString& description)
{
    KeymapProfile profile;
    profile.name        = name;
    profile.description = description;
    const QList<QPair<QString, Chord>> ov = reg.overrides();
    for (const QPair<QString, Chord>& e : ov)
        profile.bindings.insert(e.first, e.second);
    return profile;
}

void applyKeymap(BindingRegistry& reg, const KeymapProfile& profile)
{
    reg.clearAllOverrides();
    for (auto it = profile.bindings.constBegin(); it != profile.bindings.constEnd(); ++it)
        reg.setOverride(it.key(), it.value());
}

}  // namespace input
