/***************************************************************************
 *   Copyright (C) 2004 by Lynn Hazan                                      *
 *   lynn.hazan@myrealbox.com                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include "config-ndmanager.h"
// include files for QT
#include <QDir>
#include <QFileInfo>

#include <QString>

#include <QApplication>
#include <QLocale>
#include <QDebug>
//Application specific include files
#include "ndmanager.h"

#include <klustersshared/theme.h>

QString version;

// Resolve a session directory to its parameter file.  A session directory holds
// its parameter file under the directory's own name (<dir>/<dir>.yaml), so build
// that name directly rather than globbing *.yaml: session directories routinely
// hold unrelated YAML (KlustaKwik priors, tuning files) and opening one of those
// as a parameter file fails with a parse error instead of doing nothing.
// Returns an empty string when the file is not there.
static QString sessionParameterFile(const QDir& dir)
{
    const QString stem = dir.dirName();
    const QStringList exts = { QStringLiteral(".yaml"), QStringLiteral(".yml") };
    for (const QString& ext : exts) {
        const QString candidate = dir.absoluteFilePath(stem + ext);
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return QString();
}

int main(int argc, char **argv)
{
    QApplication::setOrganizationName("sourceforge");
    QApplication::setOrganizationDomain("sourceforge.net");
    QApplication::setApplicationName("ndmanager");

    QApplication app(argc, argv);

    // Apply the suite-wide light/dark/system theme preference.
    neurosuite::initThemeFromSettings();

    // Pin the C locale for all numeric input and formatting.  ndmanager's
    // parameter pages (acquisition system, LFP, clusters, video, probe, ...)
    // edit sampling rates, gains, voltage ranges and offsets through
    // QIntValidator / QDoubleValidator line edits and spin boxes, which
    // otherwise inherit QLocale::system(); under a locale whose decimal
    // separator is not '.' (German uses ','), typing '.' in a double field is
    // rejected and '.' is read as a thousands separator in integer fields, so
    // the user cannot type the values the '.'-decimal, ungrouped Neurosuite
    // parameter files require.  Set after the QApplication is constructed so
    // platform locale initialisation does not overwrite it, and before any
    // page widget is created.  Mirrors the same pin in klusters and neuroscope.
    QLocale::setDefault(QLocale::c());

    QStringList args = QApplication::arguments();
    QStringList argsList;
    for (int i = 1, n = args.size(); i < n; ++i) {
        const QString arg = args.at(i);
        if (arg == "-h" || arg == "--help" || arg == "-help") {
            qWarning() << "Usage: " << qPrintable(args.at(0))
                       << " [file]"
                       << "\n\n"
                       << "Arguments:\n"
                       << "  -h, --help              print this help\n";
            return 1;
        }
        argsList.push_back(QString::fromLocal8Bit(argv[i]));
    }


    ndManager* manager = new ndManager();
    manager->show();
    if(argsList.count()){
        QString file = argsList.at(0);
        QFileInfo fInfo(file);
        if (file.startsWith(QLatin1String("-")) ) {
            qWarning() << "it's not a filename :"<<file;
        } else {
            QString url = fInfo.isRelative()
                        ? QDir::currentPath() + QDir::separator() + file
                        : file;
            // Accept a session directory as well as a parameter file: resolve it
            // to <dir>/<dir>.yaml, the same rule used when no argument is given.
            if (QFileInfo(url).isDir()) {
                const QString resolved = sessionParameterFile(QDir(url));
                if (resolved.isEmpty()) {
                    qWarning() << "ndmanager:" << url
                               << "is a directory with no"
                               << (QDir(url).dirName() + QLatin1String(".yaml"))
                               << "- opening empty.";
                }
                url = resolved;   // empty => open empty, as with no argument
            }
            if (!url.isEmpty())
                manager->openDocumentFile(url);
        }
    } else {
        // No argument given: open the parameter file belonging to the current
        // directory, i.e. exactly <dir>/<dir>.yaml (or .yml).  Any other YAML in
        // the directory is not a parameter file and is never opened.
        const QString url = sessionParameterFile(QDir::current());
        if (!url.isEmpty()) {
            qInfo() << "ndmanager: no argument given, opening" << url;
            manager->openDocumentFile(url);
        } else {
            const QString expected = QDir::current().dirName() + QLatin1String(".yaml");
            qWarning() << "ndmanager: no argument given and no" << expected
                       << "in" << QDir::currentPath() << "- opening empty.";
            qWarning().noquote() << "  a session directory holds its parameter file as <dir>/<dir>.yaml;";
            qWarning().noquote() << "  pass a path explicitly to open anything else.";
        }
    }

    const int ret = app.exec();
    delete manager;
    return ret;
}
