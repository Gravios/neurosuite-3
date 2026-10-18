/***************************************************************************
 * radiallayout.h — the radial context menu's layout as data.
 *
 * The wheel (radialmenu.h, klusters_radialmenu.cpp) is a tree of
 *
 *   - groups: a named wedge that opens its children as the next ring out;
 *   - actions: one of Klusters' menu actions, by a stable id, optionally with
 *     a short wedge label of its own (the menu bar keeps the full text);
 *   - value wedges: "dimX" / "dimY", the feature axes stepped by the mouse
 *     wheel (optional label = the prefix shown before the number).
 *
 * Root order is ring order, clockwise from 12 o'clock.  The layout is stored
 * in the Klusters settings as JSON (Configuration::radialMenuLayout; empty =
 * the shipped defaults()) and edited in Preferences ▸ Radial menu.
 *
 * Action ids: the input registry's id where the action has one (e.g.
 * "sort.bySnr", "edit.undo"), else its menu path, "menu:Tools/New Cluster"
 * (mnemonics and a trailing ellipsis removed).  An id that no longer
 * resolves is skipped when the wheel is built and shown as unavailable in
 * the editor.
 *
 * Qt Core only (QString / QJson), so the model is unit-tested without
 * widgets (test/radiallayout_test.cpp).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef RADIALLAYOUT_H
#define RADIALLAYOUT_H

#include <QList>
#include <QString>
#include <QStringList>

class QAction;

struct RadialLayoutNode {
    enum class Kind { Group, Action, Value };
    Kind kind = Kind::Action;
    QString id;                         ///< Action: catalogue id; Value: "dimX" / "dimY"
    QString label;                      ///< Group: its name; else an optional short label
    QList<RadialLayoutNode> children;   ///< Group only

    static RadialLayoutNode group(const QString& name, const QList<RadialLayoutNode>& kids)
    { RadialLayoutNode n; n.kind = Kind::Group; n.label = name; n.children = kids; return n; }
    static RadialLayoutNode action(const QString& actionId, const QString& shortLabel = QString())
    { RadialLayoutNode n; n.kind = Kind::Action; n.id = actionId; n.label = shortLabel; return n; }
    static RadialLayoutNode value(const QString& valueId, const QString& prefix = QString())
    { RadialLayoutNode n; n.kind = Kind::Value; n.id = valueId; n.label = prefix; return n; }

    bool operator==(const RadialLayoutNode& o) const
    { return kind == o.kind && id == o.id && label == o.label && children == o.children; }
    bool operator!=(const RadialLayoutNode& o) const { return !(*this == o); }
};

using RadialLayout = QList<RadialLayoutNode>;

/** One action the wheel can hold: the editor's catalogue, built by KlustersApp
 *  from its menu bar.  @p path is the menu titles above the action. */
struct RadialCatalogEntry {
    QString     id;
    QStringList path;
    QString     text;          ///< the action's text, mnemonics removed
    QAction*    action = nullptr;
};

namespace radiallayout {

constexpr int kMaxRings = 4;    ///< RadialMenu shows at most this many rings

/** The shipped wheel: Tools, Clusters, Recluster, Undo, Redo, Hierarchy, Sort, X, Y. */
RadialLayout defaults();

/** JSON text for the settings file ({"version":1,"items":[...]}). */
QString toJson(const RadialLayout& layout);

/** Parse toJson() text.  Returns false (and leaves @p out untouched) on
 *  malformed text or an unknown version; unknown node kinds are skipped. */
bool fromJson(const QString& text, RadialLayout* out, QString* error = nullptr);

/** The stored layout, or defaults() when @p text is empty or unreadable. */
RadialLayout fromSettings(const QString& text);

/** Rings the layout needs (1 for a flat root; a group adds one). */
int ringsNeeded(const RadialLayout& layout);

/** A menu path's id ("menu:Tools/New Cluster"). */
QString menuPathId(const QStringList& path, const QString& actionText);

/** Mnemonics removed ("&Split" -> "Split", "&&" -> "&"), trailing ellipsis dropped. */
QString plainActionText(const QString& text);

} // namespace radiallayout

#endif // RADIALLAYOUT_H
