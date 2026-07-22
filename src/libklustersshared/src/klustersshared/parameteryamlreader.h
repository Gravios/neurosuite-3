/***************************************************************************
 * parameteryamlreader.h
 *
 * Shared parameter-file reader that understands the YAML format used by
 * neurosuite-3.  All three Qt packages (klusters, neuroscope, ndmanager)
 * include this header directly; the ndmanager-plugins bash scripts have
 * their own shell implementation in ndm_functions.
 *
 * Design
 * ------
 * The class parses the YAML file once (parseFile) and then exposes
 * typed accessors for every section of the parameter schema.  Three
 * Qt packages (klusters, neuroscope, ndmanager) share this reader.
 *
 * YAML schema:
 *
 *   parameters:
 *     version: "1.0"
 *     creator: "ndManager-"
 *   generalInfo:
 *     date: "2012-03-16"
 *     experimenters: "gravio"
 *     description: "hip\nca1"
 *     notes: ...
 *   acquisitionSystem:
 *     nBits: 16
 *     nChannels: 96
 *     samplingRate: 32552
 *     voltageRange: 20
 *     amplification: 1000
 *     offset: 0
 *   fieldPotentials:
 *     lfpSamplingRate: 1250
 *   files:
 *     - samplingRate: 1250
 *       extension: lfp
 *   anatomicalDescription:
 *     channelGroups:
 *       - channels:
 *           - id: 0
 *             skip: 0
 *   spikeDetection:
 *     channelGroups:
 *       - channels: [0,1,2,3]
 *         nSamples: 52
 *         peakSampleIndex: 26
 *         nFeatures: 3
 *   units:
 *     - group: 4
 *       cluster: 2
 *       structure: ~
 *       type: ~
 *       isolationDistance: ~
 *       quality: ~
 *       notes: ~
 *   neuroscope:
 *     version: "1.2.5"
 *     miscellaneous:
 *       screenGain: 0.2
 *       traceBackgroundImage: ~
 *     video:
 *       rotate: 0
 *       flip: 0
 *       videoImage: ~
 *       positionsBackground: 0
 *     spikes:
 *       nSamples: 32
 *       peakSampleIndex: 16
 *     channels:
 *       colors:
 *         - channel: 0
 *           color: "#0080ff"
 *           anatomyColor: "#0080ff"
 *           spikeColor: "#0080ff"
 *       offsets:
 *         - channel: 0
 *           defaultOffset: 0
 *   programs:
 *     - name: ndm_hipass
 *       parameters:
 *         - name: windowHalfLength
 *           value: 16
 *           status: Mandatory
 *       help: |
 *         High-pass filter …
 *
 * Copyright
 * ---------
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once

#include "libklustersshared_export.h"

#include <QList>
#include <QMap>
#include <QString>

// yaml-cpp (libyaml-cpp-dev on Ubuntu/Debian)
#include <yaml-cpp/yaml.h>

#include <generalinformation.h>
#include <fileinformation.h>
#include <programinformation.h>
#include <neuroscopevideoinfo.h>
#include <channelcolorentry.h>

/**
 * @brief Reads the neurosuite YAML parameter file.
 *
 * Used by all three Qt packages (klusters, neuroscope, ndmanager)
 * via their document-open code paths.
 */
class KLUSTERSSHARED_EXPORT ParameterYamlReader
{
public:
    ParameterYamlReader();
    ~ParameterYamlReader();

    // ----------------------------------------------------------------
    // File I/O
    // ----------------------------------------------------------------

    /**
     * @brief Parse the YAML file at @p path.
     * @return true on success, false on any parse/IO error.
     */
    bool parseFile(const QString& path);

    /** Release any resources held by the reader. */
    void closeFile();

    /** Returns true if a file has been successfully parsed. */
    bool isValid() const { return valid; }

    /**
     * Human-readable reason the last parseFile() failed (yaml-cpp message with
     * line/column, or an explanation that the document is not a parameter file).
     * Empty when the last parse succeeded.
     */
    QString lastError() const { return lastErrorMsg; }

    /**
     * Returns the raw parsed YAML document root.
     * Intended for callers that need to pass it to free-function extensions
     * such as readProbesSection() without adding new member functions here.
     * The returned reference is only valid while this reader is alive and a
     * file is loaded (isValid() == true).
     */
    const YAML::Node& getRawRoot() const { return root; }

    /** Returns the file format version string (e.g. "1.0"). */
    QString getVersion() const;

    // ----------------------------------------------------------------
    // Acquisition system
    // ----------------------------------------------------------------
    int    getResolution()   const;   ///< nBits
    int    getNbChannels()   const;
    double getSamplingRate() const;
    int    getVoltageRange() const;
    int    getAmplification() const;
    int    getOffset()       const;

    // ----------------------------------------------------------------
    // Field potentials
    // ----------------------------------------------------------------
    double getLfpSamplingRate() const;

    // ----------------------------------------------------------------
    // File extension → sampling rate  (from "files" list)
    // ----------------------------------------------------------------
    /**
     * @brief Populate @p result with extension→samplingRate pairs from
     *        the YAML "files" list.  Does NOT add dat/fil — the caller
     *        is responsible for those.
     */
    void getSampleRateByExtension(QMap<QString,double>& result) const;

    // ----------------------------------------------------------------
    // Anatomical description
    // ----------------------------------------------------------------

    /**
     * @brief Fills @p displayChannelsGroups and @p displayGroupsChannels.
     *
     * The neuroscope variant: returns per-channel group assignment and
     * per-group channel lists for the anatomical display.  Used by
     * neuroscopedoc.cpp.
     */
    void getAnatomicalDescription(int nbChannels,
                                  QMap<int,int>&         displayChannelsGroups,
                                  QMap<int,QList<int>>&  displayGroupsChannels,
                                  QMap<int,bool>&        skipStatus) const;

    /**
     * @brief Fills @p anatomicalGroups and @p attributes.
     *
     * The ndmanager variant: returns anatomical groups plus per-channel
     * attribute maps (skip status, colour, etc.).
     */
    void getAnatomicalDescription(int nbChannels,
                                  QMap<int,QList<int>>&              anatomicalGroups,
                                  QMap<QString,QMap<int,QString>>&   attributes) const;

    // ----------------------------------------------------------------
    // Spike detection
    // ----------------------------------------------------------------

    /** Returns the channel list for spike group @p electrodeGroupID (1-based). */
    QList<int> getChannelsByGroup(int electrodeGroupID) const;

    // -----------------------------------------------------------------------
    // Probe / shank metadata (optional fields in spikeDetection.channelGroups)
    //
    // These fields allow multi-shank probes to be represented explicitly:
    //
    //   spikeDetection:
    //     channelGroups:
    //       - channels: [0,1,2,3]
    //         probeId: 0
    //         shankIndex: 0
    //         ...
    //       - channels: [4,5,6,7]
    //         probeId: 0
    //         shankIndex: 1
    //         ...
    //
    // When the fields are absent:
    //   getProbeId()    returns 0 for all groups (backward compatible).
    //   getShankIndex() returns electrodeGroupID - 1 for all groups.
    //
    // getSiblingElectrodeGroups() returns all spike groups that share the
    // same probeId as electrodeGroupID, excluding electrodeGroupID itself.
    // This is the list of groups whose drift is driven by the same probe.
    // -----------------------------------------------------------------------
    int        getProbeId(int electrodeGroupID)                  const;
    int        getShankIndex(int electrodeGroupID)               const;
    QList<int> getSiblingElectrodeGroups(int electrodeGroupID)  const;

    int getNbSamples(int electrodeGroupID)      const;
    int getPeakSampleIndex(int electrodeGroupID) const;
    int getNbFeatures(int electrodeGroupID)      const;

    /**
     * @brief Fills spikeChannelsGroups and spikeGroupsChannels.
     *
     * The neuroscope variant: returns per-channel spike group assignment
     * and per-group channel lists for the spike display.
     */
    void getSpikeDescription(int nbChannels,
                             QMap<int,int>&        spikeChannelsGroups,
                             QMap<int,QList<int>>& spikeGroupsChannels) const;

    /**
     * @brief Fills spikeGroups and information.
     *
     * The ndmanager variant: returns spike groups and a per-group
     * attribute map (nSamples, peakSampleIndex, nFeatures).
     */
    void getSpikeDescription(int nbChannels,
                             QMap<int,QList<int>>&            spikeGroups,
                             QMap<int,QMap<QString,QString>>& information) const;

    // ----------------------------------------------------------------
    // Units (cluster user information)
    // ----------------------------------------------------------------

    /** Fills @p units with per-group cluster metadata. */
    void getUnits(QMap<int,QStringList>& units) const;

    // ----------------------------------------------------------------
    // NeuroScope display
    // ----------------------------------------------------------------
    float   getScreenGain()        const;
    int     getNbSamplesSpikes()   const;   ///< neuroscope/spikes/nSamples
    int     getPeakSampleIndexSpikes() const;
    QString getTraceBackgroundImage() const;

    /** Channel colour/offset lists for neuroscope display.
     *  Each entry in @p colors is { channel, color, anatomyColor, spikeColor }.
     *  Each entry in @p offsets is { channel, defaultOffset }.
     */
    void getChannelDisplayInfo(QList<QMap<QString,QString>>& colors,
                               QMap<int,int>&                offsets) const;

    // ----------------------------------------------------------------
    // Programs (ndmanager plugin parameters)
    // ----------------------------------------------------------------

    struct ParameterEntry {
        QString name;
        QString value;
        QString status;   ///< "Mandatory" | "Optional" | "Dynamic"
    };

    struct ProgramEntry {
        QString name;
        QList<ParameterEntry> parameters;
        QString help;
    };

    /** Returns all program entries in document order. */
    QList<ProgramEntry> getPrograms() const;

    /**
     * @brief Returns the value of parameter @p paramName for program
     *        @p programName, or an empty string if not found.
     */
    QString getProgramParameter(const QString& programName,
                                const QString& paramName) const;

    // ----------------------------------------------------------------
    // General information
    // ----------------------------------------------------------------
    QString getDate()          const;
    QString getExperimenters() const;
    QString getDescription()   const;
    QString getNotes()         const;

    // ----------------------------------------------------------------
    // High-level getters (return application-ready types)
    // ----------------------------------------------------------------

    /** Fills @p gi from generalInfo. */
    void getGeneralInformation(GeneralInformation& gi) const;

    /** Fills @p files from the YAML "files" sequence. */
    void getFilesInformation(QList<FileInformation>& files) const;

    /** Fills @p list with one ChannelColorEntry per channel. */
    void getChannelColors(QList<ChannelColorEntry>& list) const;

    /** Fills @p offsets from neuroscope/channels/offsets. */
    void getChannelDefaultOffset(QMap<int,int>& offsets) const;

    /** Fills @p videoInfo from neuroscope/video. */
    void getNeuroscopeVideoInfo(NeuroscopeVideoInfo& videoInfo) const;

    /**
     * @brief Fills @p info from the top-level \"video\" section.
     *
     * Keys written: \"samplingRate\", \"width\", \"height\".
     * Used by ndmanager's VideoPage.
     */
    void getTopLevelVideoInfo(QMap<QString,double>& info) const;

    /** Fills @p programs from the YAML "programs" sequence. */
    void getProgramsInformation(QList<ProgramInformation>& programs) const;

private:
    bool        valid = false;
    QString     lastErrorMsg;
    YAML::Node  root;

    // Helpers
    template<typename T>
    T nodeAs(const YAML::Node& n, const T& fallback) const;

    QString   nodeStr(const YAML::Node& n) const;
    const YAML::Node spikeGroup(int electrodeGroupID) const; ///< 1-based
};
