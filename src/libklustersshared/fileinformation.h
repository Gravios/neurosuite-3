/***************************************************************************
 * fileinformation.h
 *
 * Moved from ndmanager/src to libklustersshared.
 *
 * Copyright (C) 2004 Lynn Hazan
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "libklustersshared_export.h"

#include <QList>
#include <QMap>
#include <QString>

/**
 * @brief Stores information about one additional derived file (e.g. .lfp).
 */
class KLUSTERSSHARED_EXPORT FileInformation
{
public:
    FileInformation() : samplingRate(0.0) {}
    ~FileInformation() = default;

    double  getSamplingRate()  const { return samplingRate; }
    QString getExtension()     const { return extension; }
    QMap<int,QList<int>> getChannelMapping() const { return channelMapping; }

    void setSamplingRate(double r)                        { samplingRate = r; }
    void setExtension(const QString& e)                  { extension = e; }
    void setChannelMapping(const QMap<int,QList<int>>& m) { channelMapping = m; }

private:
    double              samplingRate;
    QString             extension;
    QMap<int,QList<int>> channelMapping;
};
