// input/bindingregistry.cpp — see input/bindingregistry.h.

#include "bindingregistry.h"

#include <algorithm>

namespace input {

void BindingRegistry::addScope(const InputScope& s)
{
    auto it = scopeIndex_.constFind(s.id);
    if (it != scopeIndex_.constEnd()) { scopes_[it.value()] = s; return; }
    scopeIndex_.insert(s.id, scopes_.size());
    scopes_.append(s);
}

void BindingRegistry::addCommand(const Command& c)
{
    auto it = commandIndex_.constFind(c.id);
    if (it != commandIndex_.constEnd()) { commands_[it.value()] = c; return; }
    commandIndex_.insert(c.id, commands_.size());
    commands_.append(c);
}

Chord BindingRegistry::effectiveChord(const QString& commandId) const
{
    auto o = overrides_.constFind(commandId);
    if (o != overrides_.constEnd()) return o.value();
    auto ci = commandIndex_.constFind(commandId);
    if (ci != commandIndex_.constEnd()) return commands_[ci.value()].defaultChord;
    return {};   // unknown command -> invalid chord
}

bool BindingRegistry::hasOverride(const QString& commandId) const
{
    return overrides_.contains(commandId);
}

void BindingRegistry::setOverride(const QString& commandId, const Chord& c)
{
    overrides_.insert(commandId, c);
}

void BindingRegistry::clearOverride(const QString& commandId)
{
    overrides_.remove(commandId);
}

void BindingRegistry::clearAllOverrides()
{
    overrides_.clear();
}

QList<QPair<QString, Chord>> BindingRegistry::overrides() const
{
    QList<QPair<QString, Chord>> out;
    out.reserve(overrides_.size());
    for (auto it = overrides_.constBegin(); it != overrides_.constEnd(); ++it)
        out.append({ it.key(), it.value() });
    return out;
}

BindingRegistry::Resolution BindingRegistry::resolveEx(const Chord& chord, const Ctx& ctx) const
{
    // Collect the indices of every active scope.  A null active() predicate means
    // "always on" (the App scope); otherwise it is asked about this event's ctx.  Note
    // the chord may be INVALID here (a held-key auto-repeat the dispatcher deliberately
    // does not treat as a fresh trigger); we still evaluate scopes, so an Exclusive scope
    // can swallow that repeat instead of letting it leak to a lower scope / the palette.
    QList<int> active;
    active.reserve(scopes_.size());
    for (int i = 0; i < scopes_.size(); ++i) {
        const InputScope& s = scopes_[i];
        if (!s.active || s.active(ctx)) active.append(i);
    }

    // Highest layer first; ties resolved so a later-registered scope shadows an
    // earlier one in the same layer.  stable_sort keeps the comparator total/stable.
    std::stable_sort(active.begin(), active.end(), [this](int a, int b) {
        const int la = int(scopes_[a].layer);
        const int lb = int(scopes_[b].layer);
        if (la != lb) return la > lb;   // inner layer wins
        return a > b;                   // later registration shadows earlier
    });

    const bool validChord = chord.isValid();
    for (int si : active) {
        const InputScope& s = scopes_[si];
        if (validChord) {
            for (const Command& c : commands_) {
                if (c.scopeId != s.id) continue;
                if (c.external) continue;                       // Qt dispatches it; never resolve it here
                if (!effectiveChord(c.id).matches(chord)) continue;  // binding vs event, per modMatch policy
                if (c.enabled && !c.enabled(ctx)) continue;     // "when" predicate gates the match
                return { &c, true };                            // matched -> invoke + consume
            }
        }
        // An active Exclusive scope owns the keyboard: once its own commands have been
        // consulted without a match (or the chord was not resolvable at all), the event is
        // swallowed here rather than offered to a lower scope or to Qt.
        if (s.capture == Capture::Exclusive)
            return { nullptr, true };
    }
    return { nullptr, false };   // nothing matched, nothing captured -> fall through
}

const Command* BindingRegistry::resolve(const Chord& chord, const Ctx& ctx) const
{
    // Convenience form: the command only.  An invalid chord with no Exclusive scope
    // active resolves to {nullptr,false} -> nullptr, exactly as the old resolve() did;
    // capture only ever changes the consume flag, which this form discards.
    return resolveEx(chord, ctx).command;
}

bool BindingRegistry::hasActiveCapture(const Ctx& ctx) const
{
    for (const InputScope& s : scopes_)
        if (s.capture == Capture::Exclusive && (!s.active || s.active(ctx)))
            return true;
    return false;
}

const Command* BindingRegistry::command(const QString& id) const
{
    auto it = commandIndex_.constFind(id);
    return it != commandIndex_.constEnd() ? &commands_[it.value()] : nullptr;
}

const InputScope* BindingRegistry::scope(const QString& id) const
{
    auto it = scopeIndex_.constFind(id);
    return it != scopeIndex_.constEnd() ? &scopes_[it.value()] : nullptr;
}

QList<BindingRegistry::Conflict> BindingRegistry::conflicts() const
{
    QList<Conflict> out;
    for (const InputScope& s : scopes_) {
        QHash<QString, QStringList> byChord;   // chord.toString() -> command ids in this scope
        QHash<QString, Chord>       chordOf;   // chord.toString() -> the chord itself
        for (const Command& c : commands_) {
            if (c.scopeId != s.id) continue;
            const Chord ch = effectiveChord(c.id);
            if (!ch.isValid()) continue;       // an unbound command never conflicts
            const QString k = ch.toString();
            byChord[k].append(c.id);
            chordOf[k] = ch;
        }
        for (auto it = byChord.constBegin(); it != byChord.constEnd(); ++it)
            if (it.value().size() > 1)
                out.append({ s.id, chordOf[it.key()], it.value() });
    }
    return out;
}

}  // namespace input
