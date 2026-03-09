/***************************************************************************
 * ndmanageryamlreader.h
 *
 * Drop-in companion to ndmanager's XmlReader.  Now a thin delegator to
 * ParameterYamlReader — all the actual parsing logic lives in libklustersshared.
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <QList>
#include <QMap>
#include <QString>

// The data types now live in libklustersshared
#include <klustersshared/generalinformation.h>
#include <klustersshared/fileinformation.h>
#include <klustersshared/programinformation.h>
#include <klustersshared/neuroscopevideoinfo.h>
#include <klustersshared/parameteryamlreader.h>

// ndmanager's local per-channel value type (distinct from libklustersshared's
// ChannelColors container).  It is now a typedef for ChannelColorEntry.
#include "channelcolors.h"  // ChannelColors = ChannelColorEntry (ndmanager shim)

// Probe section extension — free functions + ProbeEntry/ProbeGroupMeta structs
#include <klustersshared/parameteryamlreader_probes.h>

/**
 * @brief Reads ndmanager-relevant fields from a YAML parameter file.
 *
 * Mirrors the ndmanager XmlReader public API.  All methods delegate to
 * ParameterYamlReader; no parsing logic lives here.
 */
class NdManagerYamlReader
{
public:
    NdManagerYamlReader()  = default;
    ~NdManagerYamlReader() = default;

    bool parseFile(const QString& path) { return m_reader.parseFile(path); }
    void closeFile()                    { m_reader.closeFile(); }

    // ---- Acquisition system ----
    void getAcquisitionSystemInfo(QMap<QString,double>& info) const;

    // ---- General information ----
    void getGeneralInformation(GeneralInformation& gi) const
    { m_reader.getGeneralInformation(gi); }

    // ---- Field potentials ----
    double getLfpInformation() const { return m_reader.getLfpSamplingRate(); }

    // ---- Files ----
    void getFilesInformation(QList<FileInformation>& files) const
    { m_reader.getFilesInformation(files); }

    // ---- Anatomical description ----
    void getAnatomicalDescription(int nbChannels,
                                  QMap<int,QList<int>>& anatomicalGroups,
                                  QMap<QString,QMap<int,QString>>& attributes)
    { m_reader.getAnatomicalDescription(nbChannels, anatomicalGroups, attributes); }

    // ---- Spike description ----
    void getSpikeDescription(int nbChannels,
                             QMap<int,QList<int>>& spikeGroups,
                             QMap<int,QMap<QString,QString>>& information)
    { m_reader.getSpikeDescription(nbChannels, spikeGroups, information); }

    // ---- Units ----
    void getUnits(QMap<int,QStringList>& units) const { m_reader.getUnits(units); }

    // ---- NeuroScope display ----
    float   getScreenGain()           const { return m_reader.getScreenGain(); }
    QString getTraceBackgroundImage() const { return m_reader.getTraceBackgroundImage(); }
    int     getNbSamples()            const { return m_reader.getNbSamplesSpikes(); }
    int     getPeakSampleIndex()      const { return m_reader.getPeakSampleIndexSpikes(); }

    void getChannelColors(QList<ChannelColors>& list) const;

    void getChannelDefaultOffset(QMap<int,int>& offsets) const
    { m_reader.getChannelDefaultOffset(offsets); }

    // ---- Video ----
    // Reads width, height, samplingRate from the top-level "video" section,
    // using the same map keys as XmlReader::getVideoInfo().
    void getVideoInfo(QMap<QString,double>& videoInformation) const
    { m_reader.getTopLevelVideoInfo(videoInformation); }
    void getNeuroscopeVideoInfo(NeuroscopeVideoInfo& videoInfo) const
    { m_reader.getNeuroscopeVideoInfo(videoInfo); }

    // ---- Programs ----
    void getProgramsInformation(QList<ProgramInformation>& programs) const
    { m_reader.getProgramsInformation(programs); }

    void getProgramInformation(ProgramInformation& pi) const;

    // ---- Probes ----
    /**
     * @brief Read the top-level `probes` section and optional `probeLibraryPath`.
     *
     * Delegates to readProbesSection() using the raw YAML root exposed by
     * ParameterYamlReader::getRawRoot().  Returns quietly if the section is
     * absent (backward-compatible: files without a `probes` section yield an
     * empty list and empty library path).
     *
     * @param probes       Filled with one ProbeEntry per probe in the file.
     * @param libraryPath  Set to probeLibraryPath field if present, else empty.
     */
    void getProbesInformation(QList<ProbeEntry>& probes,
                              QString& libraryPath) const
    {
        readProbesSection(m_reader.getRawRoot(), probes, libraryPath);
    }

private:
    ParameterYamlReader m_reader;
};
