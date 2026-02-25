/***************************************************************************
 * programinformation.h
 *
 * Moved from ndmanager/src to libklustersshared.
 *
 * Copyright (C) 2004 Lynn Hazan
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "libklustersshared_export.h"

#include <QMap>
#include <QString>
#include <QStringList>

/**
 * @brief Stores the information describing one ndmanager plugin program.
 *
 * Each program has a name, optional help text, and a list of parameters.
 * Each parameter row is a QStringList of {name, value, status}.
 */
class KLUSTERSSHARED_EXPORT ProgramInformation
{
public:
    ProgramInformation()  = default;
    ~ProgramInformation() = default;

    void setProgramName(const QString& n)                        { name = n; }
    void setHelp(const QString& h)                               { help = h; }
    void setParameterInformation(const QMap<int,QStringList>& p) { parameters = p; }

    QString              getProgramName()         const { return name; }
    QString              getHelp()                const { return help; }
    QMap<int,QStringList> getParameterInformation() const { return parameters; }

private:
    QString              name;
    QString              help;
    QMap<int,QStringList> parameters;
};
