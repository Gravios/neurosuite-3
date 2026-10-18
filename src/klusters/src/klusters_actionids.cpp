/***************************************************************************
    klusters_actionids.cpp
    ----------------------
    KlustersApp's catalogue of menu-bar actions with their stable ids
    (actionid.h): the registry id where the action is mirrored as an app
    command, else its menu path.  Used by the radial menu's layout and its
    Preferences editor, and by the action icon table.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "actionid.h"

#include <QHash>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QWidgetAction>

#include <functional>

namespace {
// Menus whose entries are not commands (recent files): kept out of the catalogue.
bool isCatalogueMenu(const QAction* menuAction)
{
    return menuAction && !menuAction->inherits("QRecentFileAction");
}
} // namespace

QList<ActionCatalogEntry> KlustersApp::actionCatalog() const
{
    // Registry id per action, where the action is mirrored as an app command.
    QHash<const QAction*, QString> registryId;
    for (auto it = appActionCommands_.constBegin(); it != appActionCommands_.constEnd(); ++it)
        if (it.value() && !registryId.contains(it.value())) registryId.insert(it.value(), it.key());

    QList<ActionCatalogEntry> out;
    QSet<QString> seen;
    std::function<void(const QMenu*, const QStringList&)> walk =
        [&](const QMenu* m, const QStringList& path) {
        for (QAction* a : m->actions()) {
            if (!a || a->isSeparator() || qobject_cast<QWidgetAction*>(a)) continue;
            const QString text = actionid::plainText(a->text());
            if (text.isEmpty()) continue;
            if (a->menu()) {
                if (isCatalogueMenu(a)) walk(a->menu(), path + QStringList{ text });
                continue;
            }
            ActionCatalogEntry e;
            e.id = registryId.value(a, actionid::menuPathId(path, a->text()));
            e.path = path;
            e.text = text;
            e.action = a;
            if (seen.contains(e.id)) continue;      // the same action under two menus
            seen.insert(e.id);
            out.append(e);
        }
    };
    for (QAction* top : menuBar()->actions())
        if (top && top->menu() && isCatalogueMenu(top))
            walk(top->menu(), QStringList{ actionid::plainText(top->text()) });
    return out;
}

QHash<QString, QAction*> KlustersApp::actionsById() const
{
    // Both ids resolve for every action: the registry id the catalogue prefers and
    // the menu path, so a stored id survives an action gaining a registry id.
    QHash<QString, QAction*> map;
    for (const ActionCatalogEntry& e : actionCatalog()) {
        map.insert(e.id, e.action);
        map.insert(actionid::menuPathId(e.path, e.action->text()), e.action);
    }
    return map;
}
