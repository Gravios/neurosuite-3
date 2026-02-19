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

#include "klusters.h"
#include "timer.h"
#include "config-klusters.h"

int nbUndo;

int main(int argc, char* argv[])
{
    QApplication::setOrganizationName("sourceforge");
    QApplication::setOrganizationDomain("sourceforge.net");
    QApplication::setApplicationName("klusters");
    QApplication::setApplicationVersion(KLUSTERS_VERSION);

    QApplication app(argc, argv);

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
