/***************************************************************************
 * neuroscopeyamlreader.h
 *
 * Drop-in companion to NeuroscopeXmlReader that reads the YAML parameter
 * file.  The public API mirrors NeuroscopeXmlReader so callers can choose
 * the reader based on the file extension with no other changes.
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include "sessionInformation.h"
#include "parameteryamlreader.h"

/**
 * @brief Reads neuroscope-relevant fields from a YAML parameter file.
 *
 * Mirrors the NeuroscopeXmlReader public API exactly.
 */
class NeuroscopeYamlReader
{
public:
    enum fileType { PARAMETER = 0, SESSION = 1 };

    NeuroscopeYamlReader() = default;
    ~NeuroscopeYamlReader() = default;

    bool parseFile(const QString& path, fileType type = PARAMETER);
    void closeFile();

    QString getVersion() const { return m_reader.getVersion(); }
    fileType getType() const { return m_type; }

    // ---- Acquisition system ----
    int    getResolution()    const { return m_reader.getResolution(); }
    int    getNbChannels()    const { return m_reader.getNbChannels(); }
    double getSamplingRate()  const { return m_reader.getSamplingRate(); }
    double getUpsamplingRate() const { return 0.0; }   // not in YAML schema yet
    int    getVoltageRange()  const { return m_reader.getVoltageRange(); }
    int    getAmplification() const { return m_reader.getAmplification(); }
    int    getOffset()        const { return m_reader.getOffset(); }

    // ---- Field potentials ----
    double getLfpInformation() const { return m_reader.getLfpSamplingRate(); }

    // ---- NeuroScope display ----
    float   getScreenGain()           const { return m_reader.getScreenGain(); }
    int     getNbSamples()            const { return m_reader.getNbSamplesSpikes(); }
    int     getPeakSampleIndex()      const { return m_reader.getPeakSampleIndexSpikes(); }
    QString getTraceBackgroundImage() const { return m_reader.getTraceBackgroundImage(); }

    float getWaveformLength()    const { return 0.0f; }   // XML-only concept
    float getPeakSampleLength()  const { return 0.0f; }

    // ---- Channel display ----
    QList<ChannelDescription> getChannelDescription() const;
    void getChannelDefaultOffset(QMap<int,int>& channelDefaultOffsets);

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

    // ---- Session file info (not stored in YAML parameter files) ----
    QList<SessionFile>       getFilesToLoad()       { return {}; }
    QList<DisplayInformation> getDisplayInformation() { return {}; }
    QMap<QString,double>     getSampleRateByExtension() { return {}; }

    // ---- Video ----
    int getVideoWidth()  const { return 0; }
    int getVideoHeight() const { return 0; }
    int getRotation()    const { return 0; }
    int getFlip()        const { return 0; }
    int getTrajectory()  const { return 0; }
    QString getBackgroundImage() const { return {}; }

private:
    ParameterYamlReader m_reader;
    fileType            m_type = PARAMETER;
};
