/***************************************************************************
 * parameteryamlmodifier.h
 *
 * In-place modifier for the neurosuite YAML parameter file.
 *
 * Used by NeuroscopeDoc::saveSession().  The full YAML tree is read
 * into yaml-cpp, the relevant nodes are patched, and the file is
 * written atomically (tmp file + rename) to avoid corruption on crash.
 *
 * The class handles both the "modify existing file" (parseFile + set* +
 * writeToFile) and the "create new file" (default-construct + set* +
 * writeToFile) workflows.  In the latter case, missing top-level keys
 * are created on demand.
 *
 * Copyright (C) 2025  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once

#include "libklustersshared_export.h"

#include <QList>
#include <QMap>
#include <QString>

#include <yaml-cpp/yaml.h>

// Forward declarations — avoid pulling in the full headers into every TU
// that includes this header.
class ChannelColors;

/**
 * @brief Reads, patches, and atomically re-writes a neurosuite YAML
 *        parameter file.
 *
 * Typical "modify" workflow:
 * @code
 *   ParameterYamlModifier m;
 *   if (!m.parseFile(path)) return error;
 *   m.setAcquisitionSystemInformation(...);
 *   m.setChannelDisplayInformation(...);
 *   // ...
 *   if (!m.writeToFile(path)) return error;
 * @endcode
 *
 * Typical "create" workflow (no existing file):
 * @code
 *   ParameterYamlModifier m;
 *   m.setAcquisitionSystemInformation(...);
 *   // ...
 *   if (!m.writeToFile(path)) return error;
 * @endcode
 */
class KLUSTERSSHARED_EXPORT ParameterYamlModifier
{
public:
    ParameterYamlModifier();
    ~ParameterYamlModifier() = default;

    // ----------------------------------------------------------------
    // File I/O
    // ----------------------------------------------------------------

    /**
     * @brief Load an existing YAML parameter file.
     * @return true on success.  On failure the modifier still works —
     *         it will create a new document from scratch.
     */
    bool parseFile(const QString& path);

    /**
     * @brief Atomically write the (possibly modified) YAML tree to
     *        @p path (writes to path+".tmp" then renames).
     * @return true on success.
     */
    bool writeToFile(const QString& path);

    // ----------------------------------------------------------------
    // Acquisition system
    // ----------------------------------------------------------------
    bool setAcquisitionSystemInformation(int resolution, int nbChannels,
                                         double samplingRate, int voltageRange,
                                         int amplification, int offset);

    // ----------------------------------------------------------------
    // Field potentials
    // ----------------------------------------------------------------
    bool setLfpInformation(double lfpSamplingRate);

    // ----------------------------------------------------------------
    // Video (top-level, shared with other tools)
    // ----------------------------------------------------------------
    bool setVideoInformation(int width, int height);

    // ----------------------------------------------------------------
    // File extension → sampling rate map
    // ----------------------------------------------------------------
    bool setSampleRateByExtension(const QMap<QString,double>& extensionSamplingRates);

    // ----------------------------------------------------------------
    // Spike detection
    // ----------------------------------------------------------------

    /**
     * @brief Set nSamples, peakSampleIndex, and the channel groups.
     *
     * Preserves nFeatures of each existing group if possible.
     */
    bool setSpikeDetectionInformation(int nbSamples, int peakSampleIndex,
                                      QMap<int,QList<int>>& spikeGroups);

    /** @brief Set only the channel groups (nSamples/peakSampleIndex unchanged). */
    bool setSpikeDetectionInformation(QMap<int,QList<int>>& spikeGroups);

    // ----------------------------------------------------------------
    // Anatomical description
    // ----------------------------------------------------------------
    bool setAnatomicalDescription(QMap<int,QList<int>>& anatomicalGroups,
                                  const QMap<int,bool>& skipStatus);

    // ----------------------------------------------------------------
    // NeuroScope-specific display information
    // ----------------------------------------------------------------

    /**
     * @brief Patch neuroscope/miscellaneous: screenGain, traceBackgroundImage.
     */
    void setMiscellaneousInformation(float screenGain,
                                     const QString& traceBackgroundImage);

    /**
     * @brief Patch neuroscope/video: rotate, flip, videoImage, positionsBackground.
     */
    void setNeuroscopeVideoInformation(int rotation, int flip,
                                       const QString& backgroundPath,
                                       int drawTrajectory);

    /**
     * @brief Patch neuroscope/channels: colors and offsets arrays.
     *
     * Iterates over @p channelColors (nbChannels entries) and writes
     *   color / anatomyColor / spikeColor  and  defaultOffset
     * for each channel.
     */
    bool setChannelDisplayInformation(ChannelColors*     channelColors,
                                      QMap<int,int>&     channelsGroups,
                                      QMap<int,int>&     channelDefaultOffsets);

    // ----------------------------------------------------------------
    // Units (cluster user information)
    // ----------------------------------------------------------------

    /**
     * @brief Replace the entire "units" sequence.
     *
     * @p units maps cluster-id → QStringList of seven fields:
     *   [0] group, [1] cluster, [2] structure, [3] type,
     *   [4] isolationDistance, [5] quality, [6] notes.
     *
     * Any existing units for electrode groups NOT in @p touchedGroups
     * are preserved.  Pass the merged old+new map so that other
     * groups' data is retained.
     */
    bool setUnitsInformation(const QMap<int,QStringList>& units);

private:
    YAML::Node root;   ///< live YAML tree (Map or Null if never parsed)

    // Helpers
    /** Ensure root is a Map and has the given top-level key as a Map. */
    YAML::Node ensureMap(const std::string& key);

    /** Assign a scalar, creating intermediate maps as needed. */
    template<typename T>
    void setScalar(YAML::Node node, const std::string& key, const T& value);
};
