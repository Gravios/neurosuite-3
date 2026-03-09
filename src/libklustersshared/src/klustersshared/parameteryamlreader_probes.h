/***************************************************************************
 * parameteryamlreader_probes.h
 *
 * Free-function extensions to ParameterYamlReader / ParameterYamlWriter
 * that handle the `probes` section and the `probeId`/`shankIndex` fields
 * in `anatomicalDescription.channelGroups`.
 *
 * Copyright (C) 2025 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QRegularExpression>

// yaml-cpp forward
namespace YAML { class Node; }

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/**
 * @brief One probe entry as stored in the session YAML `probes` section.
 */
struct ProbeEntry {
    int        id            = 0;
    QString    probeFile;           ///< relative path inside probe library
    QString    label;               ///< user-visible label
    int        channelOffset = 0;   ///< first ADC channel from this probe
    QList<int> anatomicalGroups;    ///< 1-based anatomical group IDs on this probe
    QList<int> spikeGroups;         ///< 1-based spike-sorting group IDs; empty = same as anatomicalGroups
};

/**
 * @brief Optional probe/shank metadata attached to an anatomical group.
 */
struct ProbeGroupMeta {
    int groupId    = -1;
    int probeId    = -1;   ///< index into probes[], -1 = unset
    int shankIndex = -1;   ///< 0-based shank within probe, -1 = unset
};

// ---------------------------------------------------------------------------
// Reader helpers
// ---------------------------------------------------------------------------

/**
 * @brief Parse the top-level `probes` section of the YAML root node.
 *
 * @param root          Parsed YAML document root (output of YAML::LoadFile).
 * @param out           Filled with one ProbeEntry per probe.
 * @param libraryPathOut  Set to probeLibraryPath if present, empty otherwise.
 */
void readProbesSection(const YAML::Node& root,
                       QList<ProbeEntry>& out,
                       QString& libraryPathOut);

/**
 * @brief Parse probeId / shankIndex from anatomicalDescription.channelGroups.
 *
 * @param root  Parsed YAML root.
 * @param out   Keyed by 1-based group ID.
 */
void readAnatomyGroupMeta(const YAML::Node& root,
                          QMap<int, ProbeGroupMeta>& out);

// ---------------------------------------------------------------------------
// Writer helpers (called by ParameterYamlWriter after document is built)
// ---------------------------------------------------------------------------

/**
 * @brief Emit the `probes` sequence into a YAML document node.
 */
void writeProbesSection(YAML::Node& root,
                        const QList<ProbeEntry>& probes,
                        const QString& libraryPath);

/**
 * @brief Write probeId / shankIndex back into existing channelGroups entries.
 *
 * The channelGroups sequence must already exist in @p root before this is
 * called (written first by ParameterYamlWriter::setAnatomicalDescription).
 */
void writeAnatomyGroupMeta(YAML::Node& root,
                           const QMap<int, ProbeGroupMeta>& meta);
