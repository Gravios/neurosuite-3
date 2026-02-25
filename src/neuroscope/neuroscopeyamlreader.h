/***************************************************************************
 * neuroscopeyamlreader.h
 *
 * Drop-in companion to NeuroscopeXmlReader that reads the YAML parameter
 * file.  Now a thin delegator to ParameterYamlReader (libklustersshared).
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include "sessionInformation.h"   // ChannelDescription = ChannelColorEntry
#include "parameteryamlreader.h"
#include "neuroscopevideoinfo.h"

/**
 * @brief Reads neuroscope-relevant fields from a YAML parameter file.
 *
 * Mirrors NeuroscopeXmlReader's public API exactly.
 * All methods delegate to ParameterYamlReader.
 */
class NeuroscopeYamlReader
{
public:
    enum fileType { PARAMETER = 0, SESSION = 1 };

    NeuroscopeYamlReader()  = default;
    ~NeuroscopeYamlReader() = default;

    bool parseFile(const QString& path, fileType type = PARAMETER)
    {
        m_type = type;
        bool ok = m_reader.parseFile(path);
        if (ok) {
            // Pre-load video info so all getters below are O(1).
            m_reader.getNeuroscopeVideoInfo(m_videoInfo);
        }
        return ok;
    }
    void closeFile() { m_reader.closeFile(); }

    QString  getVersion() const { return m_reader.getVersion(); }
    fileType getType()    const { return m_type; }

    // ---- Acquisition system ----
    int    getResolution()     const { return m_reader.getResolution(); }
    int    getNbChannels()     const { return m_reader.getNbChannels(); }
    double getSamplingRate()   const { return m_reader.getSamplingRate(); }
    double getUpsamplingRate() const { return 0.0; }
    int    getVoltageRange()   const { return m_reader.getVoltageRange(); }
    int    getAmplification()  const { return m_reader.getAmplification(); }
    int    getOffset()         const { return m_reader.getOffset(); }

    // ---- Field potentials ----
    double getLfpInformation() const { return m_reader.getLfpSamplingRate(); }

    // ---- NeuroScope display ----
    float   getScreenGain()           const { return m_reader.getScreenGain(); }
    int     getNbSamples()            const { return m_reader.getNbSamplesSpikes(); }
    int     getPeakSampleIndex()      const { return m_reader.getPeakSampleIndexSpikes(); }
    QString getTraceBackgroundImage() const { return m_reader.getTraceBackgroundImage(); }

    float getWaveformLength()   const { return 0.0f; }
    float getPeakSampleLength() const { return 0.0f; }

    // ---- Channel display ----
    // Returns ChannelDescription (= ChannelColorEntry) list directly from
    // ParameterYamlReader::getChannelColors()
    QList<ChannelDescription> getChannelDescription() const
    {
        QList<ChannelDescription> result;
        m_reader.getChannelColors(result);
        return result;
    }

    void getChannelDefaultOffset(QMap<int,int>& offsets)
    { m_reader.getChannelDefaultOffset(offsets); }

    // ---- Anatomical / spike description ----
    void getAnatomicalDescription(int nbChannels,
                                  QMap<int,int>&         displayChannelsGroups,
                                  QMap<int,QList<int>>&  displayGroupsChannels,
                                  QMap<int,bool>&        skipStatus)
    { m_reader.getAnatomicalDescription(nbChannels, displayChannelsGroups,
                                        displayGroupsChannels, skipStatus); }

    void getSpikeDescription(int nbChannels,
                             QMap<int,int>&         spikeChannelsGroups,
                             QMap<int,QList<int>>&  spikeGroupsChannels)
    { m_reader.getSpikeDescription(nbChannels, spikeChannelsGroups,
                                   spikeGroupsChannels); }

    // ---- Session fields (not in YAML parameter files) ----
    QList<SessionFile>        getFilesToLoad()        { return {}; }
    QList<DisplayInformation> getDisplayInformation() { return {}; }
    QMap<QString,double>      getSampleRateByExtension()
    {
        QMap<QString,double> result;
        m_reader.getSampleRateByExtension(result);
        return result;
    }

    // ---- Video ----
    // YAML parameter files store video dimensions in the top-level "video"
    // section (written by ndmanager).  Neuroscope-specific fields (rotation,
    // flip, trajectory, background image) live in neuroscope/video and are
    // pre-loaded by parseFile into m_videoInfo.
    int     getVideoWidth()      const { return 0; }   // top-level video not read by neuroscope
    int     getVideoHeight()     const { return 0; }
    int     getRotation()        const { return m_videoInfo.getRotation(); }
    int     getFlip()            const { return m_videoInfo.getFlip(); }
    int     getTrajectory()      const { return m_videoInfo.getTrajectory(); }
    QString getBackgroundImage() const { return m_videoInfo.getBackgroundImage(); }

private:
    ParameterYamlReader m_reader;
    NeuroscopeVideoInfo m_videoInfo;
    fileType            m_type = PARAMETER;
};
