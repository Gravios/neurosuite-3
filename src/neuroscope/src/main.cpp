/***************************************************************************
                          main.cpp  -  description
                             -------------------
    begin                : Wed Feb 25 19:05:25 EST 2004
    copyright            : (C) 2004 by Lynn Hazan
    email                : lynn.hazan.myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// Qt include files
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QDebug>

// Application-specific include files
#include "neuroscope.h"

#include <klustersshared/theme.h>

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(QStringLiteral("sourceforge"));
    QApplication::setOrganizationDomain(QStringLiteral("sourceforge.net"));
    QApplication::setApplicationName(QStringLiteral("neuroscope"));
    QApplication::setApplicationVersion(QStringLiteral("2.0.0"));

    QApplication app(argc, argv);

    // Apply the suite-wide light/dark/system theme preference.
    neurosuite::initThemeFromSettings();

    // ---- Command-line parsing with QCommandLineParser (Qt5+) ----
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Neuroscope — neural data viewer"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("File to open."));

    QCommandLineOption resolutionOpt({QStringLiteral("r"), QStringLiteral("resolution")},
        QStringLiteral("Resolution of the acquisition system."), QStringLiteral("value"));
    QCommandLineOption channelNbOpt({QStringLiteral("c"), QStringLiteral("nbChannels")},
        QStringLiteral("Number of channels."), QStringLiteral("value"));
    QCommandLineOption offsetOpt({QStringLiteral("o"), QStringLiteral("offset")},
        QStringLiteral("Initial offset."), QStringLiteral("value"));
    QCommandLineOption voltageRangeOpt({QStringLiteral("m"), QStringLiteral("voltageRange")},
        QStringLiteral("Voltage range."), QStringLiteral("value"));
    QCommandLineOption amplificationOpt({QStringLiteral("a"), QStringLiteral("amplification")},
        QStringLiteral("Amplification."), QStringLiteral("value"));
    QCommandLineOption screenGainOpt({QStringLiteral("g"), QStringLiteral("screenGain")},
        QStringLiteral("Screen gain."), QStringLiteral("value"));
    QCommandLineOption samplingRateOpt({QStringLiteral("s"), QStringLiteral("samplingRate")},
        QStringLiteral("Sampling rate."), QStringLiteral("value"));
    QCommandLineOption timeWindowOpt({QStringLiteral("t"), QStringLiteral("timeWindow")},
        QStringLiteral("Initial time window (milliseconds)."), QStringLiteral("value"));

    parser.addOption(resolutionOpt);
    parser.addOption(channelNbOpt);
    parser.addOption(offsetOpt);
    parser.addOption(voltageRangeOpt);
    parser.addOption(amplificationOpt);
    parser.addOption(screenGainOpt);
    parser.addOption(samplingRateOpt);
    parser.addOption(timeWindowOpt);

    parser.process(app);

    const QString channelNb    = parser.value(channelNbOpt);
    const QString SR           = parser.value(samplingRateOpt);
    const QString resolution   = parser.value(resolutionOpt);
    const QString offset       = parser.value(offsetOpt);
    const QString voltageRange = parser.value(voltageRangeOpt);
    const QString amplification= parser.value(amplificationOpt);
    const QString screenGain   = parser.value(screenGainOpt);
    const QString timeWindow   = parser.value(timeWindowOpt);

    const QStringList positional = parser.positionalArguments();
    const QString file = positional.isEmpty() ? QString() : positional.first();

    auto *neuroscope = new NeuroscopeApp();
    neuroscope->setFileProperties(channelNb, SR, resolution,
                                  offset, voltageRange, amplification,
                                  screenGain, timeWindow);
    neuroscope->show();

    if (!file.isEmpty()) {
        const QFileInfo fInfo(file);
        if (fInfo.isRelative()) {
            neuroscope->openDocumentFile(QDir::currentPath() + QLatin1Char('/') + file);
        } else {
            neuroscope->openDocumentFile(file);
        }
    }

    const int ret = app.exec();
    delete neuroscope;
    return ret;
}
