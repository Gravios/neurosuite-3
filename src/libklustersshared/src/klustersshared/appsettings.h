/***************************************************************************
 *  appsettings.h  -  where the Neurosuite Qt programs keep their settings.
 *
 *  Each program calls, once, before its QApplication is constructed:
 *
 *      neurosuite::initApplicationIdentity(QStringLiteral("klusters"));
 *
 *  which sets the organization name every program shares, "neurosuite-3",
 *  and the program's own name.  QSettings and QStandardPaths derive their
 *  locations from the two, so on Linux:
 *
 *      ~/.config/neurosuite-3/<program>.conf       the program's preferences
 *      ~/.config/neurosuite-3/appearance.conf      the suite-wide theme
 *      ~/.local/share/neurosuite-3/<program>/      its data (NDManager's
 *                                                  session templates, Klusters'
 *                                                  plugin descriptions)
 *
 *  (macOS: ~/Library/Preferences/com.neurosuite-3.<program>.plist;
 *  Windows: HKEY_CURRENT_USER\Software\neurosuite-3\<program>.)
 *
 *  Before 2026-10-29 the programs used the organization "sourceforge"
 *  (~/.config/sourceforge/<program>.conf) and the theme "neurosuite"
 *  (~/.config/neurosuite/appearance.conf).  On the first start without
 *  settings in the new place, initApplicationIdentity() copies them over,
 *  with the program's data folder.  The old files are left where they were.
 ***************************************************************************/

#ifndef KLUSTERSSHARED_APPSETTINGS_H
#define KLUSTERSSHARED_APPSETTINGS_H

#include "libklustersshared_export.h"

#include <QString>
#include <QStringList>

namespace neurosuite {

/** The organization name of every program's settings and data. */
KLUSTERSSHARED_EXPORT QString settingsOrganization();

/** Sets the organization ("neurosuite-3") and application names, then
 * copies the settings and data the program had in the old locations, if any
 * (migrateLegacySettings).  Call before the QApplication is constructed and
 * before anything reads QSettings. */
KLUSTERSSHARED_EXPORT void initApplicationIdentity(const QString &applicationName);

/** Copies, for this program, whatever is still only in the old locations:
 *   - its settings from organization "sourceforge" (macOS: "sourceforge.net"),
 *     when it has none under the new organization;
 *   - the suite theme from ("neurosuite", "appearance"), when the new one is
 *     not set;
 *   - its data folder (QStandardPaths::AppDataLocation), when the new folder
 *     does not exist.
 * Only the user's own settings are read (no system-wide fallbacks), nothing
 * is deleted, and settings already in the new place are never overwritten.
 * Returns one line per copy made, for a log.  The organization and
 * application names must already be set. */
KLUSTERSSHARED_EXPORT QStringList migrateLegacySettings();

} // namespace neurosuite

#endif // KLUSTERSSHARED_APPSETTINGS_H
