/***************************************************************************
 *  theme.h  -  application-wide light/dark/system theming for the
 *              Neurosuite Qt programs.
 *
 *  A small, self-contained facility shared by every Qt program in the suite
 *  (klusters, neuroscope, ndmanager, ...). It builds a coherent light or dark
 *  QPalette (with sensible text/disabled colours), can follow the operating
 *  system appearance, and persists the user's choice in a single shared
 *  settings location so the whole suite stays consistent.
 *
 *  Typical use, once, right after the QApplication is constructed and the
 *  application/organisation names are set:
 *
 *      QApplication app(argc, argv);
 *      neurosuite::initThemeFromSettings();
 *
 *  A preferences UI can then call saveThemePreference()/applyTheme() when the
 *  user picks a different theme.
 ***************************************************************************/

#ifndef KLUSTERSSHARED_THEME_H
#define KLUSTERSSHARED_THEME_H

#include "libklustersshared_export.h"

#include <QString>

namespace neurosuite {

/** The three user-selectable appearances. System follows the OS where that can
 * be determined, falling back to the desktop's startup palette otherwise. */
enum class Theme { System = 0, Light = 1, Dark = 2 };

/** True when the operating system is currently using a dark appearance
 * (best effort: Qt's colour scheme on Qt >= 6.5, else the startup palette
 * luminance captured by initThemeFromSettings()). */
KLUSTERSSHARED_EXPORT bool systemIsDark();

/** Resolves a Theme to a concrete dark/light decision (System via the OS). */
KLUSTERSSHARED_EXPORT bool isDark(Theme theme);

/** Applies a theme to the running QApplication: installs the Fusion style (so
 * the palette is honoured uniformly across platforms) and a matching palette.
 * No-op when there is no QApplication. Records the choice as currentTheme(). */
KLUSTERSSHARED_EXPORT void applyTheme(Theme theme);

/** The theme last passed to applyTheme() (defaults to System). */
KLUSTERSSHARED_EXPORT Theme currentTheme();

/** Stable tokens ("system"/"light"/"dark") for settings and command lines. */
KLUSTERSSHARED_EXPORT QString themeToString(Theme theme);
KLUSTERSSHARED_EXPORT Theme   themeFromString(const QString& token);

/** A human-readable, translatable label for menus and combo boxes. */
KLUSTERSSHARED_EXPORT QString themeDisplayName(Theme theme);

/** The shared preference, stored under a suite-wide settings scope so every
 * program reads and writes the same value regardless of its own app name.
 * loadThemePreference() defaults to System when nothing has been saved. */
KLUSTERSSHARED_EXPORT Theme loadThemePreference();
KLUSTERSSHARED_EXPORT void  saveThemePreference(Theme theme);

/** Captures the OS appearance, loads the saved preference, applies it, and (on
 * Qt >= 6.5) keeps the application in sync with later OS appearance changes for
 * as long as the preference remains System. Call once after the QApplication
 * exists. Safe to call when no preference has ever been saved. */
KLUSTERSSHARED_EXPORT void initThemeFromSettings();

} // namespace neurosuite

#endif // KLUSTERSSHARED_THEME_H
