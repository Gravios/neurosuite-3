/***************************************************************************
 * ndmanageryamlreader.h
 *
 * Drop-in companion to ndmanager's XmlReader that reads the YAML parameter
 * file.  The public API mirrors XmlReader so ndmanagerdoc.cpp can choose
 * the reader based on the file extension without further restructuring.
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include "generalinformation.h"
#include "programinformation.h"
#include "fileinformation.h"
#include "channelcolors.h"
#include "neuroscopevideoinfo.h"
#include "parameteryamlreader.h"

#include <yaml-cpp/yaml.h>

/**
 * @brief Reads ndmanager-relevant fields from a YAML parameter file.
 *
 * Mirrors the ndmanager XmlReader public API exactly.
 */
class NdManagerYamlReader
{
public:
    NdManagerYamlReader() = default;
    ~NdManagerYamlReader() = default;

    bool parseFile(const QString& path);
    void closeFile();

    // ---- Acquisition system ----
    void getAcquisitionSystemInfo(QMap<QString,double>& info) const;

    // ---- General information ----
    void getGeneralInformation(GeneralInformation& generalInformation) const;

    // ---- Field potentials ----
    double getLfpInformation() const { return m_reader.getLfpSamplingRate(); }

    // ---- Files ----
    void getFilesInformation(QList<FileInformation>& files) const;

    // ---- Anatomical description ----
    void getAnatomicalDescription(int nbChannels,
                                  QMap<int,QList<int>>&              anatomicalGroups,
                                  QMap<QString,QMap<int,QString>>&   attributes)
    { m_reader.getAnatomicalDescription(nbChannels, anatomicalGroups, attributes); }

    // ---- Spike description ----
    void getSpikeDescription(int nbChannels,
                             QMap<int,QList<int>>&            spikeGroups,
                             QMap<int,QMap<QString,QString>>& information)
    { m_reader.getSpikeDescription(nbChannels, spikeGroups, information); }

    // ---- Units ----
    void getUnits(QMap<int,QStringList>& units) const
    { m_reader.getUnits(units); }

    // ---- Neuroscope display ----
    float   getScreenGain()            const { return m_reader.getScreenGain(); }
    QString getTraceBackgroundImage()  const { return m_reader.getTraceBackgroundImage(); }
    int     getNbSamples()             const { return m_reader.getNbSamplesSpikes(); }
    int     getPeakSampleIndex()       const { return m_reader.getPeakSampleIndexSpikes(); }
    void    getChannelColors(QList<ChannelColors>& list) const;
    void    getChannelDefaultOffset(QMap<int,int>& offsets) const;

    // ---- Neuroscope video ----
    void getVideoInfo(QMap<QString,double>& videoInformation) const;
    void getNeuroscopeVideoInfo(NeuroscopeVideoInfo& videoInfo) const;

    // ---- Programs ----
    void getProgramsInformation(QList<ProgramInformation>& programs) const;
    void getProgramInformation(ProgramInformation& programInformation) const;

private:
    ParameterYamlReader m_reader;
    YAML::Node          m_root;   ///< kept for fields not exposed by ParameterYamlReader

    template<typename T>
    T nodeAs(const YAML::Node& n, const T& fallback) const {
        try { if (n && n.IsDefined() && !n.IsNull()) return n.as<T>(); }
        catch (...) {}
        return fallback;
    }
};
