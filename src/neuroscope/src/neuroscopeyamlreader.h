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
#include <klustersshared/parameteryamlreader.h>
#include <klustersshared/neuroscopevideoinfo.h>

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
        this->type = type;
        bool ok = reader.parseFile(path);
        if (ok) {
            // Pre-load video info so all getters below are O(1).
            reader.getNeuroscopeVideoInfo(videoInfo);
        }
        return ok;
    }
    void closeFile() { reader.closeFile(); }

    QString  getVersion() const { return reader.getVersion(); }
    fileType getType()    const { return type; }

    // ---- Acquisition system ----
    int    getResolution()     const { return reader.getResolution(); }
    int    getNbChannels()     const { return reader.getNbChannels(); }
    double getSamplingRate()   const { return reader.getSamplingRate(); }
    double getUpsamplingRate() const { return 0.0; }
    int    getVoltageRange()   const { return reader.getVoltageRange(); }
    int    getAmplification()  const { return reader.getAmplification(); }
    int    getOffset()         const { return reader.getOffset(); }

    // ---- Field potentials ----
    double getLfpInformation() const { return reader.getLfpSamplingRate(); }

    // ---- NeuroScope display ----
    float   getScreenGain()           const { return reader.getScreenGain(); }
    int     getNbSamples()            const { return reader.getNbSamplesSpikes(); }
    int     getPeakSampleIndex()      const { return reader.getPeakSampleIndexSpikes(); }
    QString getTraceBackgroundImage() const { return reader.getTraceBackgroundImage(); }

    float getWaveformLength()   const { return 0.0f; }
    float getPeakSampleLength() const { return 0.0f; }

    // ---- Channel display ----
    // Returns ChannelDescription (= ChannelColorEntry) list directly from
    // ParameterYamlReader::getChannelColors()
    QList<ChannelDescription> getChannelDescription() const
    {
        QList<ChannelDescription> result;
        reader.getChannelColors(result);
        return result;
    }

    void getChannelDefaultOffset(QMap<int,int>& offsets)
    { reader.getChannelDefaultOffset(offsets); }

    // ---- Anatomical / spike description ----
    void getAnatomicalDescription(int nbChannels,
                                  QMap<int,int>&         displayChannelsGroups,
                                  QMap<int,QList<int>>&  displayGroupsChannels,
                                  QMap<int,bool>&        skipStatus)
    { reader.getAnatomicalDescription(nbChannels, displayChannelsGroups,
                                        displayGroupsChannels, skipStatus); }

    void getSpikeDescription(int nbChannels,
                             QMap<int,int>&         spikeChannelsGroups,
                             QMap<int,QList<int>>&  spikeGroupsChannels)
    { reader.getSpikeDescription(nbChannels, spikeChannelsGroups,
                                   spikeGroupsChannels); }

    // ---- Session fields (not in YAML parameter files) ----
    QList<SessionFile>        getFilesToLoad()        { return {}; }
    QList<DisplayInformation> getDisplayInformation() { return {}; }
    QMap<QString,double>      getSampleRateByExtension()
    {
        QMap<QString,double> result;
        reader.getSampleRateByExtension(result);
        return result;
    }

    // ---- Video ----
    // YAML parameter files store video dimensions in the top-level "video"
    // section (written by ndmanager).  Neuroscope-specific fields (rotation,
    // flip, trajectory, background image) live in neuroscope/video and are
    // pre-loaded by parseFile into videoInfo.
    int     getVideoWidth()      const { return 0; }   // top-level video not read by neuroscope
    int     getVideoHeight()     const { return 0; }
    int     getRotation()        const { return videoInfo.getRotation(); }
    int     getFlip()            const { return videoInfo.getFlip(); }
    int     getTrajectory()      const { return videoInfo.getTrajectory(); }
    QString getBackgroundImage() const { return videoInfo.getBackgroundImage(); }

private:
    ParameterYamlReader reader;
    NeuroscopeVideoInfo videoInfo;
    fileType            type = PARAMETER;
};
