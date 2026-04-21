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

#include <QString>

#include <QApplication>
#include <QDebug>
//Application specific include files
#include "ndmanager.h"

QString version;

int main(int argc, char **argv)
{
    QApplication::setOrganizationName("sourceforge");
    QApplication::setOrganizationDomain("sourceforge.net");
    QApplication::setApplicationName("ndmanager");

    QApplication app(argc, argv);
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
        } else if(fInfo.isRelative()) {
            QString url;
            url = QDir::currentPath()+ QDir::separator() + file;
            manager->openDocumentFile(url);
        } else {
            manager->openDocumentFile(file);
        }
    } else {
        // No argument given: auto-discover a single *.yaml parameter file
        // in the current working directory.  This covers the common case
        // where the user cd's into a session directory and types plain
        // `ndmanager`.  Behaviour:
        //   0 yaml files : open the empty ndmanager as before
        //   1 yaml file  : open it
        //   N yaml files : open empty, print the candidate list so the
        //                  user can pick one explicitly
        QStringList yamls = QDir::current().entryList(
            QStringList() << QLatin1String("*.yaml") << QLatin1String("*.yml"),
            QDir::Files, QDir::Name);
        if (yamls.size() == 1) {
            const QString url = QDir::currentPath() + QDir::separator() + yamls.first();
            qInfo() << "ndmanager: no argument given, opening" << url;
            manager->openDocumentFile(url);
        } else if (yamls.size() > 1) {
            qWarning() << "ndmanager: no argument given and"
                       << yamls.size()
                       << "candidate .yaml files in" << QDir::currentPath();
            qWarning() << "  pass one explicitly:";
            for (const QString& y : yamls) {
                qWarning().noquote() << "    ndmanager " << y;
            }
        }
    }

    const int ret = app.exec();
    delete manager;
    return ret;
}
