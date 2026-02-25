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
    m_valid = false;
    try {
        m_root = YAML::LoadFile(path.toStdString());
        m_valid = m_root.IsDefined() && m_root.IsMap();
    } catch (const YAML::Exception& e) {
        qWarning() << "ParameterYamlReader: failed to parse" << path
                   << ":" << QString::fromStdString(e.what());
        return false;
    }
    return m_valid;
}

void ParameterYamlReader::closeFile()
{
    m_valid = false;
    m_root.reset();
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

const YAML::Node ParameterYamlReader::spikeGroup(int electrodeGroupID) const
{
    // electrodeGroupID is 1-based
    auto groups = m_root["spikeDetection"]["channelGroups"];
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
    return nodeStr(m_root["parameters"]["version"]);
}

QString ParameterYamlReader::getDate() const
{
    return nodeStr(m_root["generalInfo"]["date"]);
}

QString ParameterYamlReader::getExperimenters() const
{
    return nodeStr(m_root["generalInfo"]["experimenters"]);
}

QString ParameterYamlReader::getDescription() const
{
    return nodeStr(m_root["generalInfo"]["description"]);
}

QString ParameterYamlReader::getNotes() const
{
    return nodeStr(m_root["generalInfo"]["notes"]);
}

// ---------------------------------------------------------------------------
// Acquisition system
// ---------------------------------------------------------------------------

int ParameterYamlReader::getResolution() const
{
    return nodeAs<int>(m_root["acquisitionSystem"]["nBits"], 0);
}

int ParameterYamlReader::getNbChannels() const
{
    return nodeAs<int>(m_root["acquisitionSystem"]["nChannels"], 0);
}

double ParameterYamlReader::getSamplingRate() const
{
    return nodeAs<double>(m_root["acquisitionSystem"]["samplingRate"], 0.0);
}

int ParameterYamlReader::getVoltageRange() const
{
    return nodeAs<int>(m_root["acquisitionSystem"]["voltageRange"], 0);
}

int ParameterYamlReader::getAmplification() const
{
    return nodeAs<int>(m_root["acquisitionSystem"]["amplification"], 0);
}

int ParameterYamlReader::getOffset() const
{
    return nodeAs<int>(m_root["acquisitionSystem"]["offset"], 0);
}

// ---------------------------------------------------------------------------
// Field potentials
// ---------------------------------------------------------------------------

double ParameterYamlReader::getLfpSamplingRate() const
{
    return nodeAs<double>(m_root["fieldPotentials"]["lfpSamplingRate"], 0.0);
}

void ParameterYamlReader::getSampleRateByExtension(QMap<QString,double>& result) const
{
    // "files" is a sequence of {samplingRate: N, extension: "ext"}
    auto files = m_root["files"];
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

// NeuroscopeXmlReader variant
void ParameterYamlReader::getAnatomicalDescription(
        int /*nbChannels*/,
        QMap<int,int>&        displayChannelsGroups,
        QMap<int,QList<int>>& displayGroupsChannels,
        QMap<int,bool>&       skipStatus) const
{
    auto groups = m_root["anatomicalDescription"]["channelGroups"];
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

// ndmanager XmlReader variant
void ParameterYamlReader::getAnatomicalDescription(
        int /*nbChannels*/,
        QMap<int,QList<int>>&              anatomicalGroups,
        QMap<QString,QMap<int,QString>>&   attributes) const
{
    auto groups = m_root["anatomicalDescription"]["channelGroups"];
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

// NeuroscopeXmlReader variant
void ParameterYamlReader::getSpikeDescription(
        int nbChannels,
        QMap<int,int>&        spikeChannelsGroups,
        QMap<int,QList<int>>& spikeGroupsChannels) const
{
    // Mirror the XML reader behaviour: every channel starts in the spike
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

    auto groups = m_root["spikeDetection"]["channelGroups"];
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

// ndmanager XmlReader variant
void ParameterYamlReader::getSpikeDescription(
        int /*nbChannels*/,
        QMap<int,QList<int>>&            spikeGroups,
        QMap<int,QMap<QString,QString>>& information) const
{
    auto groups = m_root["spikeDetection"]["channelGroups"];
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
        information[groupId] = info;
        ++groupId;
    }
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

void ParameterYamlReader::getUnits(QMap<int,QStringList>& units) const
{
    auto unitsList = m_root["units"];
    if (!unitsList || !unitsList.IsSequence()) return;

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
        units.insert(cluster, info);
    }
}

// ---------------------------------------------------------------------------
// NeuroScope display
// ---------------------------------------------------------------------------

float ParameterYamlReader::getScreenGain() const
{
    return nodeAs<float>(m_root["neuroscope"]["miscellaneous"]["screenGain"], 0.0f);
}

int ParameterYamlReader::getNbSamplesSpikes() const
{
    return nodeAs<int>(m_root["neuroscope"]["spikes"]["nSamples"], 0);
}

int ParameterYamlReader::getPeakSampleIndexSpikes() const
{
    return nodeAs<int>(m_root["neuroscope"]["spikes"]["peakSampleIndex"], 0);
}

QString ParameterYamlReader::getTraceBackgroundImage() const
{
    return nodeStr(m_root["neuroscope"]["miscellaneous"]["traceBackgroundImage"]);
}

void ParameterYamlReader::getChannelDisplayInfo(
        QList<QMap<QString,QString>>& colors,
        QMap<int,int>&                offsets) const
{
    auto chNode = m_root["neuroscope"]["channels"];
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
    auto programs = m_root["programs"];
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
    auto programs = m_root["programs"];
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
