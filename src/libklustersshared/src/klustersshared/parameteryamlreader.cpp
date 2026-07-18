/***************************************************************************
 * parameteryamlreader.cpp
 ***************************************************************************/

#include "parameteryamlreader.h"

#include <QDebug>
#include <QStringList>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ParameterYamlReader::ParameterYamlReader() = default;
ParameterYamlReader::~ParameterYamlReader() = default;

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

bool ParameterYamlReader::parseFile(const QString& path)
{
    valid = false;
    try {
        root = YAML::LoadFile(path.toStdString());
        valid = root.IsDefined() && root.IsMap();
    } catch (const YAML::Exception& e) {
        qWarning() << "ParameterYamlReader: failed to parse" << path
                   << ":" << QString::fromStdString(e.what());
        return false;
    }
    return valid;
}

void ParameterYamlReader::closeFile()
{
    valid = false;
    root.reset();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

QString ParameterYamlReader::nodeStr(const YAML::Node& n) const
{
    if (!n || !n.IsDefined() || n.IsNull()) return {};
    try { return QString::fromStdString(n.as<std::string>()); }
    catch (...) { return {}; }
}

template<typename T>
T ParameterYamlReader::nodeAs(const YAML::Node& n, const T& fallback) const
{
    if (!n || !n.IsDefined() || n.IsNull()) return fallback;
    try { return n.as<T>(); }
    catch (...) { return fallback; }
}

// ---------------------------------------------------------------------------
// Safe chained-key helpers
// ---------------------------------------------------------------------------
// yaml-cpp 0.8: in a const method, the member YAML::Node is accessed via
// its const operator[], which throws YAML::InvalidNode on any chained access
// through an absent intermediate key — BEFORE the caller's !node guard can
// run.  All chained accesses in const methods must go through these helpers.

// Returns root[k1][k2] safely. Returns an empty (Null) Node if k1 is absent.
static YAML::Node safeGet2(const YAML::Node& root,
                           const char* k1, const char* k2)
{
    YAML::Node s1 = root[k1];
    if (!s1 || !s1.IsDefined()) return YAML::Node{};
    return s1[k2];
}

// (safeGet3 used to live here; it was a 3-key chained-access helper that
// never had any callers — `safeGet2` plus a one-line follow-up handles
// every case currently used.  Dead since written; removed in libshared
// audit.  Restore from git if a 3-key chained safe lookup becomes
// needed.)

const YAML::Node ParameterYamlReader::spikeGroup(int electrodeGroupID) const
{
    // electrodeGroupID is 1-based
    auto groups = safeGet2(root, "spikeDetection", "channelGroups");
    if (!groups || !groups.IsSequence()) return YAML::Node{};
    int idx = electrodeGroupID - 1;
    if (idx < 0 || idx >= static_cast<int>(groups.size())) return YAML::Node{};
    return groups[idx];
}

// ---------------------------------------------------------------------------
// Version / general info
// ---------------------------------------------------------------------------

QString ParameterYamlReader::getVersion() const
{
    return nodeStr(safeGet2(root, "parameters", "version"));
}

QString ParameterYamlReader::getDate() const
{
    return nodeStr(safeGet2(root, "generalInfo", "date"));
}

QString ParameterYamlReader::getExperimenters() const
{
    return nodeStr(safeGet2(root, "generalInfo", "experimenters"));
}

QString ParameterYamlReader::getDescription() const
{
    return nodeStr(safeGet2(root, "generalInfo", "description"));
}

QString ParameterYamlReader::getNotes() const
{
    return nodeStr(safeGet2(root, "generalInfo", "notes"));
}

// ---------------------------------------------------------------------------
// Acquisition system
// ---------------------------------------------------------------------------

int ParameterYamlReader::getResolution() const
{
    return nodeAs<int>(safeGet2(root, "acquisitionSystem", "nBits"), 0);
}

int ParameterYamlReader::getNbChannels() const
{
    return nodeAs<int>(safeGet2(root, "acquisitionSystem", "nChannels"), 0);
}

double ParameterYamlReader::getSamplingRate() const
{
    return nodeAs<double>(safeGet2(root, "acquisitionSystem", "samplingRate"), 0.0);
}

int ParameterYamlReader::getVoltageRange() const
{
    return nodeAs<int>(safeGet2(root, "acquisitionSystem", "voltageRange"), 0);
}

int ParameterYamlReader::getAmplification() const
{
    return nodeAs<int>(safeGet2(root, "acquisitionSystem", "amplification"), 0);
}

int ParameterYamlReader::getOffset() const
{
    return nodeAs<int>(safeGet2(root, "acquisitionSystem", "offset"), 0);
}

// ---------------------------------------------------------------------------
// Field potentials
// ---------------------------------------------------------------------------

double ParameterYamlReader::getLfpSamplingRate() const
{
    return nodeAs<double>(safeGet2(root, "fieldPotentials", "lfpSamplingRate"), 0.0);
}

void ParameterYamlReader::getSampleRateByExtension(QMap<QString,double>& result) const
{
    // "files" is a sequence of {samplingRate: N, extension: "ext"}
    auto files = root["files"];
    if (!files || !files.IsSequence()) return;
    for (auto entry : files) {
        double rate = nodeAs<double>(entry["samplingRate"], 0.0);
        QString ext = nodeStr(entry["extension"]);
        if (rate > 0 && !ext.isEmpty())
            result.insert(ext, rate);
    }
}

// ---------------------------------------------------------------------------
// Anatomical description
// ---------------------------------------------------------------------------

// neuroscope variant — fills per-channel group + per-group channel maps
void ParameterYamlReader::getAnatomicalDescription(
        int /*nbChannels*/,
        QMap<int,int>&        displayChannelsGroups,
        QMap<int,QList<int>>& displayGroupsChannels,
        QMap<int,bool>&       skipStatus) const
{
    auto groups = safeGet2(root, "anatomicalDescription", "channelGroups");
    if (!groups || !groups.IsSequence()) return;

    int groupId = 1;
    for (auto grp : groups) {
        auto channels = grp["channels"];
        if (!channels || !channels.IsSequence()) { ++groupId; continue; }
        QList<int> chList;
        for (auto ch : channels) {
            int id   = nodeAs<int>(ch["id"],   -1);
            int skip = nodeAs<int>(ch["skip"],  0);
            if (id < 0) continue;
            chList.append(id);
            displayChannelsGroups[id] = groupId;
            skipStatus[id] = (skip != 0);
        }
        displayGroupsChannels[groupId] = chList;
        ++groupId;
    }
}

// ndmanager variant — fills anatomical groups + per-channel attributes
void ParameterYamlReader::getAnatomicalDescription(
        int /*nbChannels*/,
        QMap<int,QList<int>>&              anatomicalGroups,
        QMap<QString,QMap<int,QString>>&   attributes) const
{
    auto groups = safeGet2(root, "anatomicalDescription", "channelGroups");
    if (!groups || !groups.IsSequence()) return;

    int groupId = 1;
    for (auto grp : groups) {
        auto channels = grp["channels"];
        if (!channels || !channels.IsSequence()) { ++groupId; continue; }
        QList<int> chList;
        for (auto ch : channels) {
            int id   = nodeAs<int>(ch["id"],  -1);
            int skip = nodeAs<int>(ch["skip"], 0);
            if (id < 0) continue;
            chList.append(id);
            attributes[QStringLiteral("skip")][id] = skip ? QStringLiteral("1") : QStringLiteral("0");
        }
        anatomicalGroups[groupId] = chList;
        ++groupId;
    }
}

// ---------------------------------------------------------------------------
// Spike detection
// ---------------------------------------------------------------------------

QList<int> ParameterYamlReader::getChannelsByGroup(int electrodeGroupID) const
{
    QList<int> result;
    auto grp = spikeGroup(electrodeGroupID);
    if (!grp) return result;
    auto channels = grp["channels"];
    if (!channels || !channels.IsSequence()) return result;
    for (auto ch : channels)
        result.append(nodeAs<int>(ch, -1));
    result.removeAll(-1);
    return result;
}

int ParameterYamlReader::getProbeId(int electrodeGroupID) const
{
    auto grp = spikeGroup(electrodeGroupID);
    if (!grp) return 0;
    // probeId is optional; default 0 (all groups on one probe)
    return nodeAs<int>(grp["probeId"], 0);
}

int ParameterYamlReader::getShankIndex(int electrodeGroupID) const
{
    auto grp = spikeGroup(electrodeGroupID);
    if (!grp) return electrodeGroupID - 1;
    // shankIndex is optional; default to group-1 (0-based)
    return nodeAs<int>(grp["shankIndex"], electrodeGroupID - 1);
}

QList<int> ParameterYamlReader::getSiblingElectrodeGroups(int electrodeGroupID) const
{
    QList<int> siblings;
    const int myProbeId = getProbeId(electrodeGroupID);

    auto groups = safeGet2(root, "spikeDetection", "channelGroups");
    if (!groups || !groups.IsSequence()) return siblings;

    int gnum = 1;
    for (const auto& grp : groups) {
        if (gnum != electrodeGroupID) {
            const int pid = nodeAs<int>(grp["probeId"], 0);
            if (pid == myProbeId)
                siblings.append(gnum);
        }
        ++gnum;
    }
    return siblings;
}

int ParameterYamlReader::getNbSamples(int electrodeGroupID) const
{
    return nodeAs<int>(spikeGroup(electrodeGroupID)["nSamples"], 0);
}

int ParameterYamlReader::getPeakSampleIndex(int electrodeGroupID) const
{
    return nodeAs<int>(spikeGroup(electrodeGroupID)["peakSampleIndex"], 0);
}

int ParameterYamlReader::getNbFeatures(int electrodeGroupID) const
{
    return nodeAs<int>(spikeGroup(electrodeGroupID)["nFeatures"], 0);
}

// neuroscope variant — fills per-channel spike group + per-group channel maps
void ParameterYamlReader::getSpikeDescription(
        int nbChannels,
        QMap<int,int>&        spikeChannelsGroups,
        QMap<int,QList<int>>& spikeGroupsChannels) const
{
    // Convention: every channel starts in the spike
    // trash group (-1).  Channels that appear in spikeDetection groups get
    // reassigned.  Channels that are in the anatomical trash group (0) keep
    // group 0.  This ensures channelsSpikeGroups has an entry for every
    // channel so the ChannelPalette iconviewDict lookup never gets a
    // default-constructed 0 for an unassigned channel.
    QList<int> trashList;
    if (spikeGroupsChannels.contains(0))
        trashList = spikeGroupsChannels[0];

    QList<int> spikeTrashList;
    for (int i = 0; i < nbChannels; ++i) {
        if (!trashList.contains(i)) {
            spikeTrashList.append(i);
            spikeChannelsGroups.insert(i, -1);
        } else {
            spikeChannelsGroups.insert(i, 0);
        }
    }

    auto groups = safeGet2(root, "spikeDetection", "channelGroups");
    if (groups && groups.IsSequence()) {
        int groupId = 1;
        for (auto grp : groups) {
            auto channels = grp["channels"];
            if (!channels || !channels.IsSequence()) { ++groupId; continue; }
            QList<int> chList;
            for (auto ch : channels) {
                int id = nodeAs<int>(ch, -1);
                if (id < 0) continue;
                chList.append(id);
                spikeChannelsGroups[id] = groupId;  // overwrite -1
                spikeTrashList.removeAll(id);
            }
            spikeGroupsChannels[groupId] = chList;
            ++groupId;
        }
    }

    if (!spikeTrashList.isEmpty())
        spikeGroupsChannels.insert(-1, spikeTrashList);
}

// ndmanager variant — fills spike groups + per-group attribute maps
void ParameterYamlReader::getSpikeDescription(
        int /*nbChannels*/,
        QMap<int,QList<int>>&            spikeGroups,
        QMap<int,QMap<QString,QString>>& information) const
{
    auto groups = safeGet2(root, "spikeDetection", "channelGroups");
    if (!groups || !groups.IsSequence()) return;

    int groupId = 1;
    for (auto grp : groups) {
        auto channels = grp["channels"];
        if (!channels || !channels.IsSequence()) { ++groupId; continue; }
        QList<int> chList;
        for (auto ch : channels) {
            int id = nodeAs<int>(ch, -1);
            if (id >= 0) chList.append(id);
        }
        spikeGroups[groupId] = chList;

        QMap<QString,QString> info;
        info[QStringLiteral("nSamples")]       = QString::number(nodeAs<int>(grp["nSamples"],       0));
        info[QStringLiteral("peakSampleIndex")]= QString::number(nodeAs<int>(grp["peakSampleIndex"],0));
        info[QStringLiteral("nFeatures")]      = QString::number(nodeAs<int>(grp["nFeatures"],      0));
        // Optional per-group custom difference pattern for the stderiv pipeline.
        // Recorded only when present, so sessions that do not use it round-trip
        // unchanged (no empty sdiffPairs is written back).
        if (grp["sdiffPairs"] && grp["sdiffPairs"].IsDefined())
            info[QStringLiteral("sdiffPairs")] = nodeStr(grp["sdiffPairs"]);
        information[groupId] = info;
        ++groupId;
    }
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

void ParameterYamlReader::getUnits(QMap<int,QStringList>& units) const
{
    auto unitsList = root["units"];
    if (!unitsList || !unitsList.IsSequence()) return;

    // Key is a sequential document-order index (0, 1, 2, …) matching the
    // contract of ndmanager's getUnits().  Using cluster id as
    // the key is WRONG: every electrode group has its own cluster 1, 2, 3…
    // so entries from different groups would collide and one would be silently
    // dropped.  setUnitsInformation ignores the key entirely (it only uses the
    // row values), so the index choice is irrelevant to writers.
    int i = 0;
    for (auto u : unitsList) {
        int group   = nodeAs<int>(u["group"],   -1);
        int cluster = nodeAs<int>(u["cluster"], -1);
        if (group < 0 || cluster < 0) continue;

        QStringList info;
        info << QString::number(group)
             << QString::number(cluster)
             << nodeStr(u["structure"])
             << nodeStr(u["type"])
             << nodeStr(u["isolationDistance"])
             << nodeStr(u["quality"])
             << nodeStr(u["notes"]);
        units.insert(i++, info);
    }
}

// ---------------------------------------------------------------------------
// NeuroScope display
// ---------------------------------------------------------------------------

float ParameterYamlReader::getScreenGain() const
{
    // Break three-level chain: yaml-cpp throws InvalidNode on chained const access
    // through an absent intermediate key.
    auto ns = root["neuroscope"];
    if (!ns || !ns.IsDefined()) return 0.0f;
    auto misc = ns["miscellaneous"];
    return nodeAs<float>(misc["screenGain"], 0.0f);
}

int ParameterYamlReader::getNbSamplesSpikes() const
{
    auto ns = root["neuroscope"];
    if (!ns || !ns.IsDefined()) return 0;
    auto spk = ns["spikes"];
    return nodeAs<int>(spk["nSamples"], 0);
}

int ParameterYamlReader::getPeakSampleIndexSpikes() const
{
    auto ns = root["neuroscope"];
    if (!ns || !ns.IsDefined()) return 0;
    auto spk = ns["spikes"];
    return nodeAs<int>(spk["peakSampleIndex"], 0);
}

QString ParameterYamlReader::getTraceBackgroundImage() const
{
    auto ns = root["neuroscope"];
    if (!ns || !ns.IsDefined()) return {};
    auto misc = ns["miscellaneous"];
    return nodeStr(misc["traceBackgroundImage"]);
}

void ParameterYamlReader::getChannelDisplayInfo(
        QList<QMap<QString,QString>>& colors,
        QMap<int,int>&                offsets) const
{
    auto chNode = safeGet2(root, "neuroscope", "channels");
    if (!chNode) return;

    auto colorsNode = chNode["colors"];
    if (colorsNode && colorsNode.IsSequence()) {
        for (auto cc : colorsNode) {
            QMap<QString,QString> entry;
            entry[QStringLiteral("channel")]      = nodeStr(cc["channel"]);
            entry[QStringLiteral("color")]        = nodeStr(cc["color"]);
            entry[QStringLiteral("anatomyColor")] = nodeStr(cc["anatomyColor"]);
            entry[QStringLiteral("spikeColor")]   = nodeStr(cc["spikeColor"]);
            colors.append(entry);
        }
    }

    auto offsetsNode = chNode["offsets"];
    if (offsetsNode && offsetsNode.IsSequence()) {
        for (auto co : offsetsNode) {
            int ch  = nodeAs<int>(co["channel"],       -1);
            int off = nodeAs<int>(co["defaultOffset"],  0);
            if (ch >= 0) offsets[ch] = off;
        }
    }
}

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

QList<ParameterYamlReader::ProgramEntry> ParameterYamlReader::getPrograms() const
{
    QList<ProgramEntry> result;
    auto programs = root["programs"];
    if (!programs || !programs.IsSequence()) return result;

    for (auto prog : programs) {
        ProgramEntry entry;
        entry.name = nodeStr(prog["name"]);
        entry.help = nodeStr(prog["help"]);

        auto params = prog["parameters"];
        if (params && params.IsSequence()) {
            for (auto p : params) {
                ParameterEntry pe;
                pe.name   = nodeStr(p["name"]);
                pe.value  = nodeStr(p["value"]);
                pe.status = nodeStr(p["status"]);
                entry.parameters.append(pe);
            }
        }
        result.append(entry);
    }
    return result;
}

QString ParameterYamlReader::getProgramParameter(
        const QString& programName,
        const QString& paramName) const
{
    auto programs = root["programs"];
    if (!programs || !programs.IsSequence()) return {};

    for (auto prog : programs) {
        if (nodeStr(prog["name"]) != programName) continue;
        auto params = prog["parameters"];
        if (!params || !params.IsSequence()) return {};
        for (auto p : params) {
            if (nodeStr(p["name"]) == paramName)
                return nodeStr(p["value"]);
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// High-level getters (return application-ready types)
// ---------------------------------------------------------------------------

void ParameterYamlReader::getGeneralInformation(GeneralInformation& gi) const
{
    const QString dateStr = getDate();
    if (!dateStr.isEmpty()) {
        QDate d = QDate::fromString(dateStr, Qt::ISODate);
        if (d.isValid()) gi.setDate(d);
    }
    gi.setExperimenters(getExperimenters());
    gi.setDescription(getDescription());
    gi.setNotes(getNotes());
}

void ParameterYamlReader::getFilesInformation(QList<FileInformation>& files) const
{
    files.clear();
    const auto list = root["files"];
    if (!list || !list.IsSequence()) return;
    for (const auto& entry : list) {
        FileInformation fi;
        fi.setSamplingRate(nodeAs<double>(entry["samplingRate"], 0.0));
        fi.setExtension(nodeStr(entry["extension"]));
        // channelMapping is written by ndmanager but rarely present
        const auto mapping = entry["channelMapping"];
        if (mapping && mapping.IsSequence()) {
            QMap<int,QList<int>> cm;
            for (const auto& m : mapping) {
                int orig = nodeAs<int>(m["original"], -1);
                if (orig < 0) continue;
                QList<int> targets;
                const auto mapped = m["mapped"];
                if (mapped && mapped.IsSequence())
                    for (const auto& t : mapped)
                        targets.append(nodeAs<int>(t, -1));
                targets.removeAll(-1);
                cm[orig] = targets;
            }
            fi.setChannelMapping(cm);
        }
        files.append(fi);
    }
}

void ParameterYamlReader::getChannelColors(QList<ChannelColorEntry>& list) const
{
    list.clear();
    // Break three-level chain: const access through absent "channels" key throws
    // YAML::InvalidNode in yaml-cpp 0.8.
    auto ns = root["neuroscope"];
    if (!ns || !ns.IsDefined()) return;
    auto chNode = ns["channels"];
    if (!chNode || !chNode.IsDefined() || chNode.IsNull()) return;
    const auto colors = chNode["colors"];
    if (!colors || !colors.IsSequence()) return;
    for (const auto& entry : colors) {
        int ch = nodeAs<int>(entry["channel"], -1);
        if (ch < 0) continue;
        ChannelColorEntry cc;
        cc.setId(ch);
        cc.setColor(nodeStr(entry["color"]).isEmpty()
                    ? QStringLiteral("#0080ff") : nodeStr(entry["color"]));
        cc.setGroupColor(nodeStr(entry["anatomyColor"]).isEmpty()
                         ? QStringLiteral("#0080ff") : nodeStr(entry["anatomyColor"]));
        cc.setSpikeGroupColor(nodeStr(entry["spikeColor"]).isEmpty()
                              ? QStringLiteral("#0080ff") : nodeStr(entry["spikeColor"]));
        list.append(cc);
    }
}

void ParameterYamlReader::getChannelDefaultOffset(QMap<int,int>& offsets) const
{
    offsets.clear();
    // Break three-level chain (same InvalidNode issue as getChannelColors).
    auto ns = root["neuroscope"];
    if (!ns || !ns.IsDefined()) return;
    auto chNode = ns["channels"];
    if (!chNode || !chNode.IsDefined() || chNode.IsNull()) return;
    const auto list = chNode["offsets"];
    if (!list || !list.IsSequence()) return;
    for (const auto& entry : list) {
        int ch  = nodeAs<int>(entry["channel"],      -1);
        int off = nodeAs<int>(entry["defaultOffset"],  0);
        if (ch >= 0) offsets[ch] = off;
    }
}

void ParameterYamlReader::getNeuroscopeVideoInfo(NeuroscopeVideoInfo& videoInfo) const
{
    const auto v = safeGet2(root, "neuroscope", "video");
    if (!v || !v.IsMap()) return;
    videoInfo.setRotation(nodeAs<int>(v["rotate"], 0));
    videoInfo.setFlip(nodeAs<int>(v["flip"], 0));
    videoInfo.setTrajectory(nodeAs<int>(v["positionsBackground"], 0));
    const QString img = nodeStr(v["videoImage"]);
    if (!img.isEmpty()) videoInfo.setBackgroundImage(img);
}

void ParameterYamlReader::getTopLevelVideoInfo(QMap<QString,double>& info) const
{
    // Top-level "video" section written by ndmanager (width/height/samplingRate).
    // Keys: "samplingRate", "width", "height".  Consumed by ndmanager's VideoPage.
    const auto v = root["video"];
    if (!v || !v.IsMap()) return;
    if (v["samplingRate"])
        info.insert(QStringLiteral("samplingRate"), nodeAs<double>(v["samplingRate"], 0.0));
    if (v["width"])
        info.insert(QStringLiteral("width"),  static_cast<double>(nodeAs<int>(v["width"],  0)));
    if (v["height"])
        info.insert(QStringLiteral("height"), static_cast<double>(nodeAs<int>(v["height"], 0)));
}

void ParameterYamlReader::getProgramsInformation(QList<ProgramInformation>& programs) const
{
    programs.clear();
    const auto entries = getPrograms();
    for (const auto& entry : entries) {
        ProgramInformation pi;
        pi.setProgramName(entry.name);
        pi.setHelp(entry.help);
        QMap<int,QStringList> params;
        int idx = 0;
        for (const auto& pe : entry.parameters) {
            QStringList row;
            row << pe.name << pe.value << pe.status;
            params.insert(idx++, row);
        }
        pi.setParameterInformation(params);
        programs.append(pi);
    }
}
