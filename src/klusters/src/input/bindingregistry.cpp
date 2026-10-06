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

const Command* BindingRegistry::resolve(const Chord& chord, const Ctx& ctx) const
{
    if (!chord.isValid()) return nullptr;

    // Collect the indices of every active scope.  A null active() predicate means
    // "always on" (the App scope); otherwise it is asked about this event's ctx.
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

    for (int si : active) {
        const QString& sid = scopes_[si].id;
        for (const Command& c : commands_) {
            if (c.scopeId != sid) continue;
            if (effectiveChord(c.id) != chord) continue;
            if (c.enabled && !c.enabled(ctx)) continue;   // "when" predicate gates the match
            return &c;
        }
    }
    return nullptr;
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
