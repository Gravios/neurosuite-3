/***************************************************************************
 * ndmanageryamlwriter.h
 *
 * Drop-in companion to ndmanager's XmlWriter that writes the YAML parameter
 * file.  The public API mirrors XmlWriter exactly so ndmanagerdoc.cpp can
 * call it based on the file extension with no other restructuring.
 *
 * The writer accumulates state through the set*() calls (same as XmlWriter)
 * and serialises everything to disk in a single writeTofile() call.
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include "generalinformation.h"
#include "fileinformation.h"
#include "programinformation.h"
#include "neuroscopevideoinfo.h"
#include "channelcolors.h"

#include <yaml-cpp/yaml.h>

/**
 * @brief Writes the neurosuite YAML parameter file.
 *
 * Mirrors the ndmanager XmlWriter public API exactly.
 * Call the set*() methods in any order, then call writeTofile() once.
 */
class NdManagerYamlWriter
{
public:
    NdManagerYamlWriter();
    ~NdManagerYamlWriter() = default;

    /**
     * @brief Write the accumulated YAML document to @p path.
     * @return true on success, false on IO error.
     */
    bool writeTofile(const QString& path);

    // ---- Content setters (same signatures as XmlWriter) ----

    void setGeneralInformation(GeneralInformation& generalInformation);

    void setAcquisitionSystemInformation(const QMap<QString,double>& acquisitionSystemInfo);

    void setVideoInformation(const QMap<QString,double>& videoInformation);

    void setLfpInformation(double lfpSamplingRate);

    void setFilesInformation(const QList<FileInformation>& files);

    void setAnatomicalDescription(QMap<int,QList<int>>& anatomicalGroups,
                                  QMap<QString,QMap<int,QString>>& attributes);

    void setSpikeDetectionInformation(QMap<int,QList<int>>& spikeGroups,
                                      QMap<int,QMap<QString,QString>>& information);

    void setMiscellaneousInformation(float screenGain,
                                     const QString& traceBackgroundImage);

    void setNeuroscopeVideoInformation(NeuroscopeVideoInfo& videoInfo);

    void setNeuroscopeSpikeInformation(int nbSamples, int peakSampleIndex);

    void setChannelDisplayInformation(const QList<ChannelColors>& colorList,
                                      const QMap<int,int>& channelDefaultOffsets);

    void setProgramsInformation(const QList<ProgramInformation>& programs);

    void setUnitsInformation(const QMap<int,QStringList>& units);

private:
    YAML::Node m_doc;   ///< The document being built
};
