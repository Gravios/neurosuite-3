/***************************************************************************
 * actionid.h — stable ids for Klusters' menu actions.
 *
 * One id per action, used wherever an action has to be named outside the
 * code that creates it: the radial menu's stored layout (radiallayout.h)
 * and the action icon table (actionicons.h).
 *
 *   - the input registry's id where the action is mirrored there as an app
 *     command (KlustersApp::registerActionCommand: "sort.bySnr",
 *     "edit.undo", "file.save", ...);
 *   - else its menu path, "menu:Tools/New Cluster" — menu titles and the
 *     action text with mnemonics and a trailing ellipsis removed.
 *
 * KlustersApp::actionCatalog() lists every menu-bar action with its id;
 * actionsById() resolves both forms for every action, so a stored id
 * survives an action gaining a registry id later.
 *
 * Qt Core only (unit-tested in test/radiallayout_test.cpp).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef ACTIONID_H
#define ACTIONID_H

#include <QString>
#include <QStringList>

class QAction;

/** One menu-bar action and its stable id (KlustersApp::actionCatalog()). */
struct ActionCatalogEntry {
    QString     id;
    QStringList path;          ///< the menu titles above the action
    QString     text;          ///< the action's text, mnemonics removed
    QAction*    action = nullptr;
};

namespace actionid {

/** Mnemonics removed ("&Split" -> "Split", "&&" -> "&"), trailing ellipsis dropped. */
QString plainText(const QString& text);

/** A menu path's id: "menu:" + titles and action text joined by '/'. */
QString menuPathId(const QStringList& path, const QString& actionText);

} // namespace actionid

#endif // ACTIONID_H
