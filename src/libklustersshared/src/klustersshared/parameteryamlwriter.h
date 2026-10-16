/***************************************************************************
 * parameteryamlwriter.h
 *
 * YAML parameter file writer for ndmanager's model of a session.
 *
 * Used by ndmanagerdoc.cpp for every save.  Shared across the Qt packages
 * so improvements land in one place.
 *
 * Notes:
 *   - The document starts empty (a brand-new file) or, after
 *     setBaseDocument(), as a copy of an existing parameter file.  The
 *     set*() methods then write only what ndmanager models over it: keys,
 *     group entries and top-level blocks other tools write (probeId,
 *     sitePositions_um, klustakwik:, bitResolution_uV, sourceRecording, ...)
 *     are carried across.  Groups are paired with the existing ones the way
 *     ParameterYamlModifier pairs them; a group ndmanager deleted is removed.
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
 * @brief Writes the neurosuite YAML parameter file.
 *
 * Optionally call setBaseDocument() first, then the set*() methods in any
 * order, then writeTofile() once.
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

    /**
     * @brief Start from the parameter file at @p path instead of an empty document.
     *
     * Call before any set*() method.  Every key the set*() calls do not write
     * is kept as the file has it.  Returns false, leaving the writer on its
     * fresh document, when @p path is missing, unparsable or not a YAML map.
     */
    bool setBaseDocument(const QString& path);

    // ---- Content setters ----

    void setGeneralInformation(GeneralInformation& gi);

    void setAcquisitionSystemInformation(const QMap<QString,double>& info);

    /** Top-level video (width/height from acquisition system). */
    void setVideoInformation(const QMap<QString,double>& info);

    void setLfpInformation(double lfpSamplingRate);

    /** An empty list removes the files section. */
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

    /** A value <= 0 (a blank field) leaves that key as the document has it. */
    void setNeuroscopeSpikeInformation(int nbSamples, int peakSampleIndex);

    void setChannelDisplayInformation(const QList<ChannelColorEntry>& colorList,
                                      const QMap<int,int>& channelDefaultOffsets);

    /** An empty list removes the programs section. */
    void setProgramsInformation(const QList<ProgramInformation>& programs);

    /**
     * @brief Write the `probes` sequence and optional `probeLibraryPath` field.
     *
     * Should be called after setAnatomicalDescription() so the probe entries
     * can reference anatomical group IDs that are already in the document.
     * An empty list removes any `probes` section the base document has.
     *
     * @param probes       List of ProbeEntry structs from ProbePage.
     * @param libraryPath  Absolute path to the probe library root, or empty.
     */
    void setProbesInformation(const QList<ProbeEntry>& probes,
                              const QString& libraryPath);

    void setUnitsInformation(const QMap<int,QStringList>& units);

private:
    YAML::Node doc;
    std::string baseText;   ///< text of the base document: its comments and layout are kept

    static YAML::Node strNode(const QString& s);
};
