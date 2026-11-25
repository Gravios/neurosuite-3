/***************************************************************************
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/
#ifndef COMMANDSPEC_H
#define COMMANDSPEC_H

#include <QKeySequence>
#include <QString>

#include <functional>

class QAction;

/** One menu command, described once: KlustersApp::commandTable() (klusters_commands.cpp)
 *  lists them, createMenus() places them (`addCommand(menu, id)`), and the binding
 *  registry and the edit lock are filled from the same rows.
 *
 *  A row is written as a constructor and the properties that apply:
 *
 *      CommandSpec("action.groupClusters", "Actions", tr("&Group Clusters"), ON(triggered, slotGroup))
 *          .shortcut(Qt::Key_G).editing()
 */
struct CommandSpec {
    QString id;            ///< Unique.  With a category, also the id in the binding registry.
    QString category;      ///< The registry's category; empty: the command is not mirrored there.
    QString text;          ///< The menu text, mnemonic and ellipsis included.
    QKeySequence key;
    QString status;        ///< The status-bar tip.
    QString tip;           ///< The tool tip.
    bool isCheckable = false;
    bool isChecked = false;            ///< The initial state of a checkable command (set before
                                       ///< its slot is connected: the slot does not run for it).
    bool isEnabled = true;             ///< The initial state.
    /// It changes the clustering, its files or the cluster ids, or starts a job that
    /// does: the edit lock holds it off while a job runs (KlustersApp::refreshEditLock).
    bool edits = false;
    /// Connects the action to its slot.  Empty: createMenus() wires it where it adds it
    /// (a slot that takes an argument, or no slot of its own).
    std::function<void(QAction*)> wire;

    CommandSpec() = default;
    CommandSpec(const char* commandId, const char* registryCategory, const QString& menuText,
                std::function<void(QAction*)> wiring = {})
        : id(QString::fromLatin1(commandId)), category(QString::fromLatin1(registryCategory)),
          text(menuText), wire(std::move(wiring)) {}

    CommandSpec& shortcut(const QKeySequence& k) { key = k; return *this; }
    CommandSpec& statusTip(const QString& s)     { status = s; return *this; }
    CommandSpec& toolTip(const QString& s)       { tip = s; return *this; }
    CommandSpec& checkable()                     { isCheckable = true; return *this; }
    CommandSpec& checked(bool on = true)         { isChecked = on; return *this; }
    CommandSpec& disabled()                      { isEnabled = false; return *this; }
    CommandSpec& enabledIf(bool on)              { isEnabled = on; return *this; }
    CommandSpec& editing()                       { edits = true; return *this; }
};

#endif // COMMANDSPEC_H
