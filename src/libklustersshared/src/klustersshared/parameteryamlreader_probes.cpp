/***************************************************************************
 * parameteryamlreader_probes.cpp
 *
 * Extension methods for ParameterYamlReader: read the new `probes` section
 * and the `probeId`/`shankIndex` fields in anatomicalDescription.
 *
 * These are implemented as free functions that take the root YAML node
 * rather than as members of ParameterYamlReader, so the extension can be
 * dropped into the libklustersshared source tree without patching the
 * existing header/implementation pair.
 *
 * Usage (from ndmanager's ndmanagerdoc.cpp loadFromReader template):
 *
 *   QList<ProbeEntry>        probes;
 *   QMap<int,ProbeGroupMeta> groupMeta;
 *   QString                  libraryPath;
 *   readProbesSection(yamlRoot, probes, libraryPath);
 *   readAnatomyGroupMeta(yamlRoot, groupMeta);
 *
 * Copyright (C) 2025 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "parameteryamlreader_probes.h"
#include <yaml-cpp/yaml.h>
#include <QDebug>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString qs(const YAML::Node& n)
{
    if (!n || !n.IsScalar()) return {};
    return QString::fromStdString(n.Scalar());
}

static int yi(const YAML::Node& n, int fallback = 0)
{
    if (!n || !n.IsScalar()) return fallback;
    try { return n.as<int>(); } catch (...) { return fallback; }
}

// ---------------------------------------------------------------------------
// readProbesSection
// ---------------------------------------------------------------------------
// Reads:
//   probes:
//     - id: 0
//       probeFile: neuronexus/Buzsaki64.probe
//       label: "left CA1"
//       channelOffset: 0
//       anatomicalGroups: [1,2,3,4,5,6,7,8]
// ---------------------------------------------------------------------------

void readProbesSection(const YAML::Node& root,
                       QList<ProbeEntry>& out,
                       QString& libraryPathOut)
{
    out.clear();
    libraryPathOut.clear();

    const YAML::Node probes = root["probes"];
    if (!probes || !probes.IsSequence()) return;

    // Optional global library path
    if (root["probeLibraryPath"])
        libraryPathOut = qs(root["probeLibraryPath"]);

    for (std::size_t i = 0; i < probes.size(); ++i) {
        const YAML::Node& p = probes[i];
        if (!p.IsMap()) continue;

        ProbeEntry entry;
        entry.id            = yi(p["id"], static_cast<int>(i));
        entry.probeFile     = qs(p["probeFile"]);
        entry.label         = qs(p["label"]);
        entry.channelOffset = yi(p["channelOffset"], 0);

        // anatomicalGroups may be a sequence [1,2,3] or a scalar "1,2,3"
        const YAML::Node ag = p["anatomicalGroups"];
        if (ag) {
            if (ag.IsSequence()) {
                for (std::size_t j = 0; j < ag.size(); ++j)
                    entry.anatomicalGroups.append(yi(ag[j]));
            } else if (ag.IsScalar()) {
                // comma-separated string fallback
                const QString raw = qs(ag);
                for (const QString& tok : raw.split(
                         QRegularExpression(QStringLiteral("[,\\s]+")),
                         Qt::SkipEmptyParts)) {
                    bool ok = false;
                    int g = tok.toInt(&ok);
                    if (ok && g > 0) entry.anatomicalGroups.append(g);
                }
            }
        }

        // spikeGroups — optional; when absent defaults to anatomicalGroups at use-site
        const YAML::Node sg = p["spikeGroups"];
        if (sg) {
            if (sg.IsSequence()) {
                for (std::size_t j = 0; j < sg.size(); ++j)
                    entry.spikeGroups.append(yi(sg[j]));
            } else if (sg.IsScalar()) {
                const QString raw = qs(sg);
                for (const QString& tok : raw.split(
                         QRegularExpression(QStringLiteral("[,\\s]+")),
                         Qt::SkipEmptyParts)) {
                    bool ok = false;
                    int g = tok.toInt(&ok);
                    if (ok && g > 0) entry.spikeGroups.append(g);
                }
            }
        }

        out.append(entry);
    }
}

// ---------------------------------------------------------------------------
// readAnatomyGroupMeta
// ---------------------------------------------------------------------------
// Reads probeId and shankIndex from anatomicalDescription.channelGroups
// entries.  Fills a map keyed by the explicit `id` field (or 1-based list
// position when `id` is absent).
// ---------------------------------------------------------------------------

void readAnatomyGroupMeta(const YAML::Node& root,
                          QMap<int, ProbeGroupMeta>& out)
{
    out.clear();

    const YAML::Node cg = root["anatomicalDescription"]["channelGroups"];
    if (!cg || !cg.IsSequence()) return;

    for (std::size_t i = 0; i < cg.size(); ++i) {
        const YAML::Node& g = cg[i];
        if (!g.IsMap()) continue;

        // group id: explicit or 1-based position
        int groupId = g["id"] ? yi(g["id"]) : static_cast<int>(i + 1);

        ProbeGroupMeta meta;
        meta.groupId    = groupId;
        meta.probeId    = g["probeId"]    ? yi(g["probeId"],    -1) : -1;
        meta.shankIndex = g["shankIndex"] ? yi(g["shankIndex"], -1) : -1;

        out.insert(groupId, meta);
    }
}

// ---------------------------------------------------------------------------
// writeProbesSection  (for ParameterYamlWriter)
// ---------------------------------------------------------------------------

void writeProbesSection(YAML::Node& root,
                        const QList<ProbeEntry>& probes,
                        const QString& libraryPath)
{
    if (!libraryPath.isEmpty())
        root["probeLibraryPath"] = libraryPath.toStdString();

    if (probes.isEmpty()) return;

    YAML::Node seq(YAML::NodeType::Sequence);
    for (const ProbeEntry& e : probes) {
        YAML::Node entry(YAML::NodeType::Map);
        entry["id"]            = e.id;
        entry["probeFile"]     = e.probeFile.toStdString();
        entry["label"]         = e.label.toStdString();
        entry["channelOffset"] = e.channelOffset;

        YAML::Node ag(YAML::NodeType::Sequence);
        for (int g : e.anatomicalGroups)
            ag.push_back(g);
        entry["anatomicalGroups"] = ag;

        // spikeGroups — only written when different from anatomicalGroups
        if (!e.spikeGroups.isEmpty() && e.spikeGroups != e.anatomicalGroups) {
            YAML::Node sg(YAML::NodeType::Sequence);
            for (int g : e.spikeGroups)
                sg.push_back(g);
            entry["spikeGroups"] = sg;
        }

        seq.push_back(entry);
    }
    root["probes"] = seq;
}

// ---------------------------------------------------------------------------
// writeAnatomyGroupMeta  (for ParameterYamlWriter)
// ---------------------------------------------------------------------------
// Writes probeId and shankIndex back into anatomicalDescription.channelGroups
// entries that already exist in root.  Caller must have set up the
// channelGroups sequence first (via ParameterYamlWriter::setAnatomicalDescription).

void writeAnatomyGroupMeta(YAML::Node& root,
                           const QMap<int, ProbeGroupMeta>& meta)
{
    YAML::Node cg = root["anatomicalDescription"]["channelGroups"];
    if (!cg || !cg.IsSequence()) return;

    for (std::size_t i = 0; i < cg.size(); ++i) {
        YAML::Node g = cg[i];  // value, not ref — yaml-cpp 0.8 operator[] returns rvalue

        // Determine which 1-based group id this entry has
        int groupId = g["id"] ? yi(g["id"]) : static_cast<int>(i + 1);

        if (!meta.contains(groupId)) continue;
        const ProbeGroupMeta& m = meta[groupId];

        if (m.probeId >= 0)    g["probeId"]    = m.probeId;
        if (m.shankIndex >= 0) g["shankIndex"] = m.shankIndex;
    }
}
