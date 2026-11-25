/***************************************************************************
 * actionicons.h — the one table of Klusters' action icons.
 *
 * The standard (doc/dev/STANDARDIZATION.md §2.11):
 *
 *   1. One table.  Every action icon is a row here, keyed by the action's
 *      stable id (actionid.h: the input registry's id, else its menu path).
 *      KlustersApp::applyActionIcons() sets them all in one pass, after the
 *      actions exist; no setIcon() at the action's creation site.
 *   2. One loading form per kind of icon:
 *        Standard — a generic action (open, save, undo, ...): the
 *                   freedesktop icon name from the desktop theme, falling
 *                   back to the bundled copy :/shared-icons/<fallback>
 *                   (libklustersshared-icons.qrc), so the suite looks native
 *                   where a theme provides the icon and identical elsewhere;
 *        Bundled  — a Klusters-specific action: QIcon(":/icons/<name>"),
 *                   <name> a lower-case, hyphenated alias in
 *                   klusters-icons.qrc (no extension in code);
 *        Drawn    — painted by drawnIcon(<name>, palette): no file, outline
 *                   colours taken from the palette so it follows the
 *                   light / dark theme (re-applied on every palette
 *                   change: KlustersApp::changeEvent()).
 *   3. Checks.  test/actionicons_test.cpp fails when a row names a resource
 *      that is not bundled or a drawn icon that has no painter; in a build
 *      with -DNS3_VERBOSE=1, applyActionIcons() logs rows whose action does
 *      not exist and toolbar actions left without an icon.
 *
 * To give an action an icon: add the PNG under src/icons/, list it in
 * klusters-icons.qrc with an alias, and add a row below (RECIPES.md).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef ACTIONICONS_H
#define ACTIONICONS_H

#include <QIcon>
#include <QString>

#include <span>

class QPalette;

struct ActionIconSpec {
    enum class Source { Standard, Bundled, Drawn };
    const char* actionId;      ///< actionid.h id
    Source      source;
    const char* name;          ///< Standard: theme name; Bundled: :/icons alias; Drawn: painter key
    const char* fallback = nullptr;   ///< Standard only: :/shared-icons alias (default: name)
};

namespace actionicons {

/** Every action icon, one row per action. */
std::span<const ActionIconSpec> table();

/** The icon for @p spec; @p palette colours a drawn icon. */
QIcon load(const ActionIconSpec& spec, const QPalette& palette);

/** The bundled resource a row relies on (":/icons/x", or ":/shared-icons/x"
 *  for a Standard row's fallback); empty for a drawn icon. */
QString resourcePath(const ActionIconSpec& spec);

/** The painted icons ("multi-lasso", "split-by-lassos"); a null icon for an
 *  unknown key. */
QIcon drawnIcon(const QString& key, const QPalette& palette);

} // namespace actionicons

#endif // ACTIONICONS_H
