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
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QRegularExpression>

// yaml-cpp forward
namespace YAML { class Node; }

#include "libklustersshared_export.h"

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/**
 * @brief One probe entry as stored in the session YAML `probes` section.
 */
struct ProbeEntry {
    /// The probe's id: YAML key `probeId` (canonical, the key spike and
    /// anatomical groups use to refer to the probe); the legacy key `id` is
    /// still read.  Falls back to the entry's 0-based position.
    int        id            = 0;
    QString    probeFile;           ///< relative path inside probe library
    QString    label;               ///< user-visible label
    int        channelOffset = 0;   ///< first ADC channel from this probe
    QList<int> anatomicalGroups;    ///< 1-based group IDs belonging to this probe
    QList<int> spikeGroups;         ///< spike group IDs; empty = same as anatomicalGroups

    // Layout of the probe's .probe file, filled by ndmanager's Probes tab
    // when it can read the file; never read from or written to the session
    // YAML.  writeProbesSection uses it to give each channel group the
    // probeId / shankIndex of the probe shank its channels are on.
    bool                layoutKnown = false;  ///< the fields below are valid
    QList<QList<int>>   shankChannels;        ///< absolute channels, per shank
    QMap<int, QPointF>  channelPositions;     ///< absolute channel -> (x, y above tip) µm
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
// Probe geometry data — used by ndmanager's Probe Maker page.
// These types describe the *contents* of a `.probe` file (one connector,
// N shanks, M channels per shank).  ProbeEntry above describes how a
// probe is *referenced* from a session YAML; the two are complementary.
// See doc/design/probe-maker.md for the design.
// ---------------------------------------------------------------------------

/**
 * @brief A single recording site — one electrode pad on a shank.
 *
 *  Coordinates are µm in the parent shank's local frame: x is lateral
 *  across the shank face, y is depth from the shank head (y = 0 at the
 *  top, y = lengthUm at the tip).  The `.probe` files measure y the other
 *  way, as the height above the tip (doc/ndmanager-plugins/formats/
 *  probe.md); the Probe Maker converts on load and save.
 */
struct ProbeChannel {
    /// Channel of this site relative to the probe's channelOffset: the
    /// site's channelMap.map entry, or its index in the file without a map.
    int     hardwareId    = 0;
    QPointF posUm;                 ///< (x, y) on the shank, µm
    int     siteIndex     = -1;    ///< probe-numbered site (display only)
    qreal   areaUm2       = 177.0; ///< pad area, µm²
    bool    enabled       = true;
};

/**
 * @brief A single shank — substrate carrying recording sites.
 *
 *  originUm is the shank's origin in connector coordinates (where the
 *  shank head sits relative to the connector).  Multi-shank probes
 *  (Neuropixels 2.0, Buzsáki silicon arrays) store inter-shank pitch
 *  via originUm.x() differences.
 */
struct ProbeShank {
    QString id;                    ///< "shank1" — local identifier
    QString label;                 ///< "Shank A" — user-facing
    QPointF originUm;              ///< origin in connector coords, µm
    qreal   lengthUm  = 1500.0;    ///< shank length, µm
    qreal   widthUm   =   70.0;    ///< shank width, µm
    qreal   tipAngle  =   90.0;    ///< degrees; 90° = blunt, 60° = sharp
    QString layout    = QStringLiteral("linear");  ///< "linear", "tetrode", "poly2", ...
    QList<ProbeChannel> channels;
};

/**
 * @brief A whole probe — connector header plus shanks plus channel map.
 *
 *  Mirrors the canonical `.probe` file's `probeFile:` schema 1:1.
 *  The Probe Maker page edits one of these in place; round-tripping
 *  through saveToFile() / loadFromFile() preserves all fields.
 */
struct ProbeConnector {
    QString version              = QStringLiteral("1.0");
    QString vendor;
    QString model;
    QString catalogPage;
    int     totalChannels        = 0;
    QString substrateMaterial    = QStringLiteral("silicon");
    qreal   substrateThicknessUm = 0.0;
    QList<ProbeShank> shanks;
    QString channelMapDescription;
    /// Unused by the Probe Maker, which keeps each site's channel in
    /// ProbeChannel::hardwareId and rebuilds the file's map from those.
    QList<int> channelMap;
    QString notes;
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
KLUSTERSSHARED_EXPORT void readProbesSection(const YAML::Node& root,
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
 *
 * The caller owns the probe list: an empty @p probes removes `probes`.  An
 * empty @p libraryPath leaves `probeLibraryPath` as it is.  A probe entry
 * @p root already holds (matched by id) is updated in place, so keys the
 * caller does not model survive.
 *
 * When entries carry their layout (ProbeEntry::layoutKnown), the channel
 * groups already in @p root (anatomicalDescription and spikeDetection --
 * write those first) are matched to it: a group whose channels all lie on
 * one probe gets that probe's probeId, and the shankIndex of the shank
 * holding them all (removed when they span shanks); an existing
 * sitePositions_um is rewritten from the probe file.  A group that lies on
 * no probe loses a probeId that names a probe with a known layout or no
 * probe at all, with its shankIndex and sitePositions_um.  Entries without
 * a layout leave every group as it is.
 */
KLUSTERSSHARED_EXPORT void writeProbesSection(YAML::Node& root,
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
