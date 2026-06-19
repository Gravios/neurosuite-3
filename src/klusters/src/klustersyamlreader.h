/***************************************************************************
 * klustersyamlreader.h
 *
 * Reads klusters-relevant fields from the YAML parameter file.
 *
 * Usage:
 *   if (fileName.endsWith(".yaml") || fileName.endsWith(".yml")) {
 *       KlustersYamlReader reader;
 *       if (!reader.parseFile(fileName)) { // error /// }
 *       int nch = reader.getNbChannels();
 *       // ...
 *   }
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once

#include <QList>
#include <QFile>
#include <QMap>

#include <klustersshared/parameteryamlreader.h>

class ClusterUserInformation;

/**
 * @brief Reads klusters-relevant fields from a YAML parameter file.
 */
class KlustersYamlReader
{
public:
    enum fileType { PARAMETER = 0 };

    KlustersYamlReader() = default;
    ~KlustersYamlReader() = default;

    /** Parse @p file.  @p type must be PARAMETER (reserved for future use). */
    bool parseFile(const QFile& file, fileType type = PARAMETER);

    /** Parse from a plain path string (convenience overload). */
    bool parseFile(const QString& path, fileType type = PARAMETER);

    void closeFile();

    // ---- Acquisition system ----
    int    getResolution()   const { return reader.getResolution(); }
    int    getNbChannels()   const { return reader.getNbChannels(); }
    double getSamplingRate() const { return reader.getSamplingRate(); }
    int    getVoltageRange() const { return reader.getVoltageRange(); }
    int    getAmplification() const { return reader.getAmplification(); }
    int    getOffset()       const { return reader.getOffset(); }

    // ---- Spike detection ----
    QList<int> getNbChannelsByGroup(int electrodeGroupID) const
        { return reader.getChannelsByGroup(electrodeGroupID); }

    int getNbSamples(int electrodeGroupID) const
        { return reader.getNbSamples(electrodeGroupID); }

    int getPeakSampleIndex(int electrodeGroupID) const
        { return reader.getPeakSampleIndex(electrodeGroupID); }

    int getNbFeatures(int electrodeGroupID) const
        { return reader.getNbFeatures(electrodeGroupID); }

    // ---- Units ----
    void getClusterUserInformation(int pGroup,
                                   QMap<int,ClusterUserInformation>& map) const;

private:
    ParameterYamlReader reader;
};
