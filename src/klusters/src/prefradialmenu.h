/***************************************************************************
 * prefradialmenu.h — Preferences ▸ Radial menu: the context wheel editor.
 *
 * Three parts, top to bottom:
 *
 *   - Available actions: every menu-bar action (the catalogue KlustersApp
 *     builds, by menu), plus the two feature-axis value wedges; a filter box
 *     narrows it.  Add (or double-click) puts the action into the wheel.
 *   - Wheel: the layout as a tree.  Top-level rows are the root ring,
 *     clockwise from 12 o'clock; a group is a wedge that opens its rows as the
 *     next ring out.  Drag rows to reorder or move them between groups (an
 *     action cannot take children), or use Up / Down.  Double-click a group
 *     to rename it, or an action's "Wedge label" cell to give it a short
 *     label (empty = the action's own text).  Rows whose action no longer
 *     exists are shown in red and are skipped by the wheel.
 *   - Preview: the wheel as it will look (root ring and one ring out).
 *     Selecting a group in the tree opens its ring; hovering works as in the
 *     real menu, but nothing is triggered.
 *
 * The page edits a RadialLayout (radiallayout.h).  PrefDialog loads it from,
 * and commits it to, Configuration::radialMenuLayout; KlustersApp rebuilds
 * the wheel on Apply.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef PREFRADIALMENU_H
#define PREFRADIALMENU_H

#include "radiallayout.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class RadialMenu;

class PrefRadialMenu : public QWidget
{
    Q_OBJECT

public:
    explicit PrefRadialMenu(QWidget* parent = nullptr);

    /** The actions that can be placed (call before setLayout()). */
    void setCatalog(const QList<RadialCatalogEntry>& entries);

    /** Show @p layout in the editor (no changed() signal). */
    void setLayout(const RadialLayout& layout);
    /** The layout as currently edited. */
    RadialLayout layout() const;

    /** Back to the shipped wheel (emits changed()). */
    void restoreDefaults();

Q_SIGNALS:
    /** A user edit (not a programmatic setLayout()). */
    void changed();

private:
    enum Role { KindRole = Qt::UserRole + 1, IdRole };
    enum Column { NameColumn = 0, LabelColumn = 1 };

    // catalogue side
    void fillCatalog();
    void applyFilter(const QString& text);
    void addSelectedCatalogItems();

    // wheel side
    QTreeWidgetItem* makeWheelItem(const RadialLayoutNode& node) const;
    void decorate(QTreeWidgetItem* item) const;
    RadialLayout readItems(const QTreeWidgetItem* parent) const;
    void insertIntoWheel(QTreeWidgetItem* item);
    void addGroup();
    void removeSelected();
    void moveSelected(int delta);
    void edited();                          ///< any user change: schedule refresh + changed()
    void refresh();                         ///< preview, warnings, button states
    void updateButtons();

    QString catalogText(const QString& id) const;

    QList<RadialCatalogEntry> catalog;
    QHash<QString, int> catalogIndex;       ///< id -> catalog row (both id forms)

    QLineEdit*   filterEdit = nullptr;
    QTreeWidget* catalogTree = nullptr;
    QTreeWidget* wheelTree = nullptr;
    QPushButton* addButton = nullptr;
    QPushButton* groupButton = nullptr;
    QPushButton* removeButton = nullptr;
    QPushButton* upButton = nullptr;
    QPushButton* downButton = nullptr;
    QPushButton* defaultsButton = nullptr;
    QLabel*      statusLabel = nullptr;
    RadialMenu*  preview = nullptr;
    QMenu*       previewTree = nullptr;     ///< rebuilt on every refresh
    QHash<const QTreeWidgetItem*, QAction*> previewGroupAction;
    bool loading = false;                   ///< setLayout() in progress: no changed()
    bool refreshPending = false;
};

#endif // PREFRADIALMENU_H
