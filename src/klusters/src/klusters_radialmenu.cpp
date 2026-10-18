/***************************************************************************
    klusters_radialmenu.cpp
    -----------------------
    KlustersApp radial context menu (proof of concept).  Right-click in any
    view opens a RadialMenu (radialmenu.h) centred on the cursor; KlustersView's
    event filter dispatches the press, and the lasso undo-vertex and the
    lineage overlay keep their own right-click.  The command is
    app.radialMenu, so it is listed and rebindable in Preferences ▸ Input and
    shown in Help ▸ Shortcuts like every other binding.

    The menu is a tree of the actions the menu bar already owns — nothing is
    duplicated: enabled / checked state, icons and shortcuts are the live
    ones, and a wedge simply triggers its QAction.  Hidden actions (and
    submenus left empty by them) drop out of the rings.  Which actions, in
    which groups and order, is data (radiallayout.h): the shipped default or
    the layout edited in Preferences ▸ Radial menu, resolved here against the
    catalogue of menu-bar actions (radialCatalog()).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "configuration.h"
#include "radiallayout.h"
#include "radialmenu.h"

#include <QHash>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QStatusBar>
#include <QWidgetAction>

#include <functional>

namespace {
// Menus whose entries are not commands (recent files): kept out of the catalogue.
bool isCatalogueMenu(const QAction* menuAction)
{
    return menuAction && !menuAction->inherits("QRecentFileAction");
}
} // namespace

QList<RadialCatalogEntry> KlustersApp::radialCatalog() const
{
    // Registry id per action, where the action is mirrored as an app command.
    QHash<const QAction*, QString> registryId;
    for (auto it = appActionCommands_.constBegin(); it != appActionCommands_.constEnd(); ++it)
        if (it.value() && !registryId.contains(it.value())) registryId.insert(it.value(), it.key());

    QList<RadialCatalogEntry> out;
    QSet<QString> seen;
    std::function<void(const QMenu*, const QStringList&)> walk =
        [&](const QMenu* m, const QStringList& path) {
        for (QAction* a : m->actions()) {
            if (!a || a->isSeparator() || qobject_cast<QWidgetAction*>(a)) continue;
            const QString text = radiallayout::plainActionText(a->text());
            if (text.isEmpty()) continue;
            if (a->menu()) {
                if (isCatalogueMenu(a)) walk(a->menu(), path + QStringList{ text });
                continue;
            }
            RadialCatalogEntry e;
            e.id = registryId.value(a, radiallayout::menuPathId(path, a->text()));
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
            walk(top->menu(), QStringList{ radiallayout::plainActionText(top->text()) });
    return out;
}

QHash<QString, QAction*> KlustersApp::radialActionsById() const
{
    // Both ids resolve for every action: the registry id the catalogue prefers and
    // the menu path, so a stored layout survives an action gaining a registry id.
    QHash<QString, QAction*> map;
    for (const RadialCatalogEntry& e : radialCatalog()) {
        map.insert(e.id, e.action);
        map.insert(radiallayout::menuPathId(e.path, e.action->text()), e.action);
    }
    return map;
}

void KlustersApp::buildRadialMenuTree()
{
    // A container only: QMenu::addAction(QAction*) does not take ownership, so every
    // action wedge is the very QAction the menu bar shows.  Groups are new QMenus
    // owned by the container.
    mRadialMenuTree = new QMenu(this);

    // The feature-space axes as value wedges: hover one and turn the wheel.  These
    // two actions exist only here (the toolbar spinboxes are the menu-bar-side
    // control); triggering them does nothing, and RadialMenu never picks them.
    mRadialDimX = new QAction(tr("X axis feature dimension"), mRadialMenuTree);
    mRadialDimY = new QAction(tr("Y axis feature dimension"), mRadialMenuTree);
    mRadialMenu->setAdjustable(mRadialDimX);
    mRadialMenu->setAdjustable(mRadialDimY);
    mRadialDimXPrefix = tr("X");
    mRadialDimYPrefix = tr("Y");

    const QHash<QString, QAction*> byId = radialActionsById();
    std::function<void(QMenu*, const RadialLayout&)> fill =
        [&](QMenu* m, const RadialLayout& nodes) {
        for (const RadialLayoutNode& n : nodes) {
            switch (n.kind) {
            case RadialLayoutNode::Kind::Group:
                fill(m->addMenu(n.label), n.children);
                break;
            case RadialLayoutNode::Kind::Action:
                if (QAction* a = byId.value(n.id)) {
                    m->addAction(a);
                    mRadialMenu->setLabel(a, n.label);   // empty clears: full text
                } else {
                    NS3_DIAG() << "radial menu: no action for id" << n.id;
                }
                break;
            case RadialLayoutNode::Kind::Value:
                if (n.id == QLatin1String("dimX")) {
                    m->addAction(mRadialDimX);
                    if (!n.label.isEmpty()) mRadialDimXPrefix = n.label;
                } else if (n.id == QLatin1String("dimY")) {
                    m->addAction(mRadialDimY);
                    if (!n.label.isEmpty()) mRadialDimYPrefix = n.label;
                }
                break;
            }
        }
    };
    fill(mRadialMenuTree, radiallayout::fromSettings(configuration().getRadialMenuLayout()));
}

void KlustersApp::rebuildRadialMenu()
{
    // The layout changed (Preferences ▸ Radial menu): drop the wheel; the next
    // right-click builds it again from the stored layout.
    delete mRadialMenu;   mRadialMenu = nullptr;
    delete mRadialMenuTree; mRadialMenuTree = nullptr;   // owns the groups and X / Y
    mRadialDimX = nullptr;
    mRadialDimY = nullptr;
}

void KlustersApp::refreshRadialDimensions()
{
    if (!mRadialDimX || !dimensionX || !dimensionY) return;
    // Live only where the toolbar shows the spinboxes (a display with a cluster view).
    const bool live = dimensionXAction && dimensionXAction->isVisible()
                   && dimensionX->isEnabled() && dimensionX->maximum() > 1;
    mRadialDimX->setEnabled(live);
    mRadialDimY->setEnabled(live);
    mRadialMenu->setLabel(mRadialDimX, tr("%1: %2").arg(mRadialDimXPrefix).arg(dimensionX->value()));
    mRadialMenu->setLabel(mRadialDimY, tr("%1: %2").arg(mRadialDimYPrefix).arg(dimensionY->value()));
}

void KlustersApp::showRadialStatus(QAction* a)
{
    // The wedge labels are short; the status bar carries the full name + shortcut.
    if (!a) { statusBar()->clearMessage(); return; }
    if (a == mRadialDimX || a == mRadialDimY) {
        const SpinBox* spin = (a == mRadialDimX) ? dimensionX : dimensionY;
        statusBar()->showMessage(a->isEnabled()
            ? tr("%1: %2 of %3 — scroll to change")
                  .arg(a == mRadialDimX ? tr("X axis") : tr("Y axis"))
                  .arg(spin->value()).arg(spin->maximum())
            : tr("Feature dimensions — no cluster view in this display"));
        return;
    }
    QString msg = a->text().remove(QLatin1Char('&'));
    if (!a->shortcut().isEmpty())
        msg += QStringLiteral("    (%1)").arg(a->shortcut().toString(QKeySequence::NativeText));
    if (!a->isEnabled()) msg += tr("  — not available now");
    statusBar()->showMessage(msg);
}

void KlustersApp::showRadialMenu(const QPoint& globalPos)
{
    if (!mRadialMenu) {
        mRadialMenu = new RadialMenu(this);
        buildRadialMenuTree();               // also sets the wedges' short labels
        mRadialMenu->setMenu(mRadialMenuTree);
        connect(mRadialMenu, &RadialMenu::hovered, this, &KlustersApp::showRadialStatus);
        // Wheel over X / Y: step the toolbar spinbox (it wraps, like the spinbox's own
        // wheel); its valueChanged runs the usual slotUpdateDimensionX/Y, so the
        // active cluster view redraws behind the menu.
        connect(mRadialMenu, &RadialMenu::adjusted, this, [this](QAction* a, int steps) {
            SpinBox* spin = (a == mRadialDimX) ? dimensionX
                          : (a == mRadialDimY) ? dimensionY : nullptr;
            if (!spin) return;
            spin->stepBy(steps);
            refreshRadialDimensions();
            showRadialStatus(a);
        });
    }
    refreshRadialDimensions();
    mRadialMenu->popup(globalPos);
}
