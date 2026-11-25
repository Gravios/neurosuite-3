/***************************************************************************
    klusters_actionids.cpp
    ----------------------
    KlustersApp's catalogue of menu-bar actions with their stable ids
    (actionid.h): the registry id where the action is mirrored as an app
    command, else the id of its row in the command table, else its menu path
    (the entries plugins add).  Used by the radial menu's layout and its
    Preferences editor, and by the action icon table (applyActionIcons()).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "actionid.h"
#include "actionicons.h"

#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QToolBar>
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

    // Else the id of its row in the command table (klusters_commands.cpp): an id that does
    // not change when the command moves to another menu.
    QHash<const QAction*, QString> tableId;
    for (auto it = commandActions.constBegin(); it != commandActions.constEnd(); ++it)
        if (it.value()) tableId.insert(it.value(), it.key());

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
            e.id = registryId.value(a, tableId.value(a, actionid::menuPathId(path, a->text())));
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
    const QString enginePath = QStringLiteral("menu:Recluster/Engine/");
    for (const ActionCatalogEntry& e : actionCatalog()) {
        map.insert(e.id, e.action);
        const QString pathId = actionid::menuPathId(e.path, e.action->text());
        map.insert(pathId, e.action);
        // The engines of the installed plugins have no row: their id is their path, which
        // was under Actions until 2026-11-24.
        if (pathId.startsWith(enginePath))
            map.insert(QStringLiteral("menu:Actions/Recluster/Engine/") + pathId.mid(enginePath.size()), e.action);
    }
    // The menu-path ids of the commands that had no other id when the menus were
    // redistributed (2026-11-24): a radial layout stored before still names them so.
    static const struct { const char* was; const char* id; } movedSince20261124[] = {
        { "menu:Settings/Show Main Toolbar",   "view.showMainToolbar" },
        { "menu:Settings/Show Actions",        "view.showActionsToolbar" },
        { "menu:Settings/Show Tools",          "view.showToolsToolbar" },
        { "menu:Settings/Show Parameters",     "view.showParametersToolbar" },
        { "menu:Settings/Show Cluster Info",   "view.showClusterInfo" },
        { "menu:Settings/Show StatusBar",      "view.showStatusBar" },
        { "menu:Settings/Increase Point Size", "view.increasePointSize" },
        { "menu:Settings/Decrease Point Size", "view.decreasePointSize" },
        { "menu:Settings/Immediate Update",    "view.immediateUpdate" },
        { "menu:Settings/Delayed Update",      "view.delayedUpdate" },
        { "menu:Settings/Preferences",         "edit.preferences" },
        { "menu:Actions/Sync Feature Spaces",  "action.syncFeatureSpaces" },
        { "menu:Actions/Sort Spikes by Time",  "action.sortSpikesByTime" },
        { "menu:Actions/Sort Clusters/Sort Clusters by Largest Amplitude by Channel", "sort.byAmplitudeByChannel" },
    };
    for (const auto& m : movedSince20261124)
        if (QAction* a = map.value(QLatin1String(m.id))) map.insert(QLatin1String(m.was), a);
    return map;
}


void KlustersApp::applyActionIcons()
{
    const QHash<QString, QAction*> byId = actionsById();
    // The application palette, not this widget's copy: right after a theme change
    // (Preferences ▸ Apply) the widget has not been told yet.
    const QPalette pal = QApplication::palette();
    for (const ActionIconSpec& spec : actionicons::table()) {
        QAction* a = byId.value(QLatin1String(spec.actionId));
        if (!a) {
            NS3_DIAG() << "action icons: no action with id" << spec.actionId;
            continue;
        }
        a->setIcon(actionicons::load(spec, pal));
    }

    if (NS3_VERBOSE) {
        // A toolbar button with no icon shows its text: almost always a missing row.
        for (const QToolBar* bar : findChildren<QToolBar*>()) {
            for (const QAction* a : bar->actions()) {
                if (!a || a->isSeparator() || qobject_cast<const QWidgetAction*>(a)) continue;
                if (a->icon().isNull())
                    qDebug() << "action icons: toolbar" << bar->objectName()
                             << "has no icon for" << actionid::plainText(a->text());
            }
        }
    }
}

void KlustersApp::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange)
        applyActionIcons();
    QMainWindow::changeEvent(event);
}
