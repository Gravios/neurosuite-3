/***************************************************************************
                          main.cpp  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>

#include "klusters.h"

#ifdef Q_OS_UNIX
#include <sys/resource.h>   // RLIMIT_NOFILE headroom for over-clustered sessions
#endif

#include <klustersshared/theme.h>
#include "timer.h"
#include "config-klusters.h"

int nbUndo;

int main(int argc, char* argv[])
{
#ifdef Q_OS_UNIX
    // Raise the soft file-descriptor limit to the hard limit before any Qt or
    // GLib machinery starts.  Every thread's event dispatcher costs a GWakeup
    // pipe (two descriptors), and a heavily over-clustered session (10k+
    // atoms) drives enough per-cluster machinery that the default soft limit
    // of 1024 is exhausted mid-load -- GLib then hard-aborts ("Creating pipes
    // for GWakeup: Too many open files") instead of failing an open.  The
    // hard limit on a systemd desktop is 2^19 or more, so this is free
    // headroom; raising soft to hard is the standard move for
    // many-descriptor applications.
    struct rlimit nofile;
    if (getrlimit(RLIMIT_NOFILE, &nofile) == 0
            && (nofile.rlim_max == RLIM_INFINITY
                || nofile.rlim_cur < nofile.rlim_max)) {
        nofile.rlim_cur = nofile.rlim_max;
        setrlimit(RLIMIT_NOFILE, &nofile);
    }
#endif
    QApplication::setOrganizationName("sourceforge");
    QApplication::setOrganizationDomain("sourceforge.net");
    QApplication::setApplicationName("klusters");
    QApplication::setApplicationVersion(KLUSTERS_VERSION);

    QApplication app(argc, argv);

    // Apply the suite-wide light/dark/system theme preference.
    neurosuite::initThemeFromSettings();

    // Pin the C locale for all numeric input and formatting.  Without this the
    // spin boxes and the QIntValidator / QDoubleValidator input fields inherit
    // QLocale::system(); in locales whose decimal separator is not '.' (German,
    // for instance, uses ',') typing '.' in a double field is rejected, and '.'
    // is interpreted as a thousands separator in integer fields — so the user
    // "cannot type certain numbers".  The C locale (decimal '.', no group
    // separator) makes numeric entry consistent across every field and matches
    // the '.'-decimal / ungrouped Neurosuite data files.  Set after the
    // QApplication is constructed (so it isn't overwritten by platform locale
    // initialisation) and before the main window — and therefore every widget —
    // is created, so the new default is inherited everywhere.
    QLocale::setDefault(QLocale::c());

    QCommandLineParser parser;
    parser.setApplicationDescription("Klusters - cluster cutting application");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "File to open.");
    parser.process(app);

    KlustersApp* Klusters = new KlustersApp();
    Klusters->show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        const QString file = positional.at(0);
        QFileInfo fInfo(file);
        if (fInfo.isRelative()) {
            Klusters->openDocumentFile(QDir::currentPath() + QDir::separator() + file);
        } else {
            Klusters->openDocumentFile(file);
        }
    }

    int ret = app.exec();
    delete Klusters;
    return ret;
}
