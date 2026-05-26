/***************************************************************************
 * parameteryamlwriter.h
 *
 * Write-from-scratch YAML parameter file writer.
 *
 * Used by ndmanagerdoc.cpp when saving a new session.  Shared across
 * the Qt packages so improvements land in one place.
 *
 * Notes:
 *   - setAnatomicalDescription and setSpikeDetectionInformation iterate
 *     the QMap with an iterator instead of a 1..size() index loop, so
 *     groups are written correctly even when keys are non-contiguous.
 *   - writeTofile uses atomic tmp-file + rename (crash-safe).
 *   - YAML emitter uses SetIndent(2) / SetMapFormat(Block) / SetSeqFormat(Block).
 *   - setSpikeDetectionInformation also writes neuroscope/spikes/{nSamples,
 *     peakSampleIndex} to keep both locations in sync.
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "libklustersshared_export.h"

#include <QList>
#include <QMap>
#include <QString>

#include <generalinformation.h>
#include <fileinformation.h>
#include <programinformation.h>
#include <neuroscopevideoinfo.h>
#include <channelcolorentry.h>
#include "parameteryamlreader_probes.h"  // ProbeEntry

#include <yaml-cpp/yaml.h>

/**
 * @brief Writes the neurosuite YAML parameter file from scratch.
 *
 * Call the set*() methods in any order, then call writeTofile() once.
 * The writer holds an in-memory YAML tree (doc) that is serialised
 * atomically on writeTofile().
 */
class KLUSTERSSHARED_EXPORT ParameterYamlWriter
{
public:
    ParameterYamlWriter();
    ~ParameterYamlWriter() = default;

    /**
     * @brief Serialise the accumulated document to @p path.
     *
     * Writes to path+".nstmp" then renames, so the original file is
     * never left in a partial state on crash.
     *
     * @return true on success.
     */
    bool writeTofile(const QString& path);

    // ---- Content setters ----

    void setGeneralInformation(GeneralInformation& gi);

    void setAcquisitionSystemInformation(const QMap<QString,double>& info);

    /** Top-level video (width/height from acquisition system). */
    void setVideoInformation(const QMap<QString,double>& info);

    void setLfpInformation(double lfpSamplingRate);

    void setFilesInformation(const QList<FileInformation>& files);

    void setAnatomicalDescription(QMap<int,QList<int>>& anatomicalGroups,
                                  QMap<QString,QMap<int,QString>>& attributes);

    /**
     * @brief Set spike groups and per-group metadata.
     *
     * Also writes neuroscope/spikes/nSamples and peakSampleIndex so both
     * schema locations stay in sync.  Group keys ≤ 0 (trash/display-only)
     * are skipped.
     */
    void setSpikeDetectionInformation(QMap<int,QList<int>>& spikeGroups,
                                      QMap<int,QMap<QString,QString>>& information);

    void setMiscellaneousInformation(float screenGain,
                                     const QString& traceBackgroundImage);

    void setNeuroscopeVideoInformation(NeuroscopeVideoInfo& videoInfo);

    void setNeuroscopeSpikeInformation(int nbSamples, int peakSampleIndex);

    void setChannelDisplayInformation(const QList<ChannelColorEntry>& colorList,
                                      const QMap<int,int>& channelDefaultOffsets);

    void setProgramsInformation(const QList<ProgramInformation>& programs);

    /**
     * @brief Write the `probes` sequence and optional `probeLibraryPath` field.
     *
     * Should be called after setAnatomicalDescription() so the probe entries
     * can reference anatomical group IDs that are already in the document.
     * Calling with an empty list writes nothing (no-op, backward compatible).
     *
     * @param probes       List of ProbeEntry structs from ProbePage.
     * @param libraryPath  Absolute path to the probe library root, or empty.
     */
    void setProbesInformation(const QList<ProbeEntry>& probes,
                              const QString& libraryPath);

    void setUnitsInformation(const QMap<int,QStringList>& units);

private:
    YAML::Node doc;

    static YAML::Node strNode(const QString& s);
};
