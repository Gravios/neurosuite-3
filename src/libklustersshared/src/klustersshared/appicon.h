/***************************************************************************
 *  appicon.h  -  the window and Dock icon of a Neurosuite Qt program.
 *
 *  Each program compiles its icon into its resources as
 *  ":/app-icon/<size>" (PNG, one entry per size; doc/images/README.md) and
 *  calls, once, right after the QApplication is constructed:
 *
 *      neurosuite::installApplicationIcon(QStringLiteral("klusters"));
 *
 *  This sets the icon of every window (title bar, Windows taskbar, the macOS
 *  Dock and app switcher for a program not run from a bundle) and names the
 *  program's desktop file, through which Wayland compositors find the icon
 *  the hicolor theme installs.
 ***************************************************************************/

#ifndef KLUSTERSSHARED_APPICON_H
#define KLUSTERSSHARED_APPICON_H

#include "libklustersshared_export.h"

#include <QString>

namespace neurosuite {

/** Sets the application icon from the ":/app-icon/<size>" resources and the
 * desktop file name (desktopFileName, without ".desktop").  Returns false,
 * changing nothing, when the program has no such resource. */
KLUSTERSSHARED_EXPORT bool installApplicationIcon(const QString &desktopFileName);

} // namespace neurosuite

#endif // KLUSTERSSHARED_APPICON_H
