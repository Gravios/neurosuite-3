/***************************************************************************
 * generalinformation.h
 *
 * Moved from ndmanager/src to libklustersshared so it can be used by both
 * ndmanager and the shared ParameterYamlReader/Writer.
 *
 * Copyright (C) 2004 Lynn Hazan
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "libklustersshared_export.h"

#include <QDate>
#include <QString>

/**
 * @brief Stores the information in the generalInfo section of a parameter file.
 */
class KLUSTERSSHARED_EXPORT GeneralInformation
{
public:
    GeneralInformation() : date(QDate::currentDate()) {}
    ~GeneralInformation() = default;

    void setDate(const QDate& d)               { date = d; }
    void setExperimenters(const QString& s)    { experimenters = s; }
    void setDescription(const QString& s)      { description = s; }
    void setNotes(const QString& s)            { notes = s; }

    QDate   getDate()          const { return date; }
    QString getExperimenters() const { return experimenters; }
    QString getDescription()   const { return description; }
    QString getNotes()         const { return notes; }

private:
    QDate   date;
    QString experimenters;
    QString description;
    QString notes;
};
