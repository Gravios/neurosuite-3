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
#include <QRegularExpression>

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

// A probes: entry's id.  `probeId` is the canonical key -- ndm_setupgroups
// writes it, and every reader (the ndm_* scripts, KlustaKwik) looks for it
// first -- and `id` is the legacy key older files and ndmanager before this
// change wrote.  Same order everywhere, so a file holding both reads the same
// in every tool.  Reading only `id` gave a setupgroups-written probe its list
// position instead of its id, so a group's probeId pointed at the wrong probe.
static int probeEntryId(const YAML::Node& p, int position)
{
    if (p["probeId"]) return yi(p["probeId"], position);
    if (p["id"])      return yi(p["id"],      position);
    return position;
}

// ---------------------------------------------------------------------------
// readProbesSection
// ---------------------------------------------------------------------------
// Reads:
//   probes:
//     - probeId: 0                # legacy files: id: 0
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
        entry.id            = probeEntryId(p, static_cast<int>(i));
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
// stampGroupProbeMeta  (writeProbesSection's group pass)
// ---------------------------------------------------------------------------
// Channel groups (anatomy and spike) get probeId / shankIndex from the probe
// whose layout holds all their channels.  ParameterYamlWriter carries a
// group's keys across by pairing it with an old group (same channel set,
// else position); without this pass a group the probe tab generated in a
// removed probe's place inherited that probe's probeId.

static QList<int> groupChannels(const YAML::Node& g)
{
    QList<int> out;
    const YAML::Node chs = g["channels"];
    if (!chs || !chs.IsSequence()) return out;
    for (std::size_t i = 0; i < chs.size(); ++i) {
        const YAML::Node c = chs[i];
        const int id = c.IsMap() ? yi(c["id"], -1) : yi(c, -1);
        if (id >= 0) out.append(id);
    }
    return out;
}

static void stampGroups(YAML::Node groups, const QList<ProbeEntry>& probes,
                        bool spikeGroups)
{
    if (!groups || !groups.IsSequence()) return;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        YAML::Node g = groups[i];
        if (!g.IsMap()) continue;
        const YAML::Node& cg = g;      // const lookups never add keys
        const QList<int> chs = groupChannels(cg);
        if (chs.isEmpty()) continue;

        const ProbeEntry* owner = nullptr;
        int shank = -1;
        for (const ProbeEntry& e : probes) {
            if (!e.layoutKnown) continue;
            bool all = true;
            for (int ch : chs) {
                bool found = false;
                for (const QList<int>& s : e.shankChannels)
                    if (s.contains(ch)) { found = true; break; }
                if (!found) { all = false; break; }
            }
            if (!all) continue;
            owner = &e;
            for (int s = 0; s < e.shankChannels.size(); ++s) {
                bool inShank = true;
                for (int ch : chs)
                    if (!e.shankChannels[s].contains(ch)) { inShank = false; break; }
                if (inShank) { shank = s; break; }
            }
            break;
        }

        if (owner) {
            g["probeId"] = owner->id;
            if (shank >= 0) g["shankIndex"] = shank;
            else            g.remove("shankIndex");
            if (spikeGroups && cg["sitePositions_um"]) {
                YAML::Node pos(YAML::NodeType::Sequence);
                for (int ch : chs) {
                    if (owner->channelPositions.contains(ch)) {
                        const QPointF p = owner->channelPositions.value(ch);
                        YAML::Node xy(YAML::NodeType::Sequence);
                        xy.push_back(p.x());
                        xy.push_back(p.y());
                        xy.SetStyle(YAML::EmitterStyle::Flow);
                        pos.push_back(xy);
                    } else {
                        pos.push_back(YAML::Node(YAML::NodeType::Null));
                    }
                }
                g["sitePositions_um"] = pos;
            }
            continue;
        }

        if (!cg["probeId"]) continue;
        const int pid = yi(cg["probeId"], -1);
        bool listed = false, known = false;
        for (const ProbeEntry& e : probes) {
            if (e.id != pid) continue;
            listed = true;
            known  = e.layoutKnown;
        }
        if (listed && !known) continue;   // that probe's layout is unknown: leave it
        g.remove("probeId");
        g.remove("shankIndex");
        g.remove("sitePositions_um");
    }
}

static void stampGroupProbeMeta(YAML::Node& root, const QList<ProbeEntry>& probes)
{
    bool anyKnown = false;
    for (const ProbeEntry& e : probes) anyKnown = anyKnown || e.layoutKnown;
    if (!anyKnown) return;
    const YAML::Node& croot = root;
    for (const char* section : { "anatomicalDescription", "spikeDetection" }) {
        const YAML::Node sec = croot.IsMap() ? croot[section] : YAML::Node();
        if (!sec || !sec.IsMap()) continue;
        const YAML::Node groups = sec["channelGroups"];
        stampGroups(groups, probes, std::string(section) == "spikeDetection");
    }
}

// ---------------------------------------------------------------------------
// writeProbesSection  (for ParameterYamlWriter)
// ---------------------------------------------------------------------------

void writeProbesSection(YAML::Node& root,
                        const QList<ProbeEntry>& probes,
                        const QString& libraryPath)
{
    // An empty library path means the caller has none to offer (ndmanager's
    // probe tab always has one, its default included), so the file's is kept.
    if (!libraryPath.isEmpty())
        root["probeLibraryPath"] = libraryPath.toStdString();

    // The caller (ndmanager's probe tab) owns the probe list: an empty list is
    // every probe removed there.  On a fresh document this is a no-op.
    if (probes.isEmpty()) {
        root.remove("probes");
        return;
    }

    // Entries already in the document, by id, so a rewrite keeps any key the
    // caller does not model.
    QMap<int, YAML::Node> old;
    const YAML::Node& croot = root;
    const YAML::Node existing = croot.IsMap() ? croot["probes"] : YAML::Node();
    if (existing && existing.IsSequence()) {
        for (std::size_t i = 0; i < existing.size(); ++i) {
            const YAML::Node p = existing[i];
            if (!p.IsMap()) continue;
            const int id = probeEntryId(p, static_cast<int>(i));
            if (!old.contains(id)) old.insert(id, YAML::Clone(p));
        }
    }

    YAML::Node seq(YAML::NodeType::Sequence);
    for (const ProbeEntry& e : probes) {
        // probeId is the canonical key and comes first.  A legacy `id` the
        // entry carried is dropped, so the file holds one id per probe.
        YAML::Node entry(YAML::NodeType::Map);
        entry["probeId"]       = e.id;
        if (old.contains(e.id)) {
            for (const auto& kv : old.take(e.id)) {
                const std::string key = kv.first.as<std::string>("");
                if (key != "id" && key != "probeId") entry[kv.first] = kv.second;
            }
        }
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
        } else {
            entry.remove("spikeGroups");
        }

        seq.push_back(entry);
    }
    root["probes"] = seq;

    stampGroupProbeMeta(root, probes);
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
