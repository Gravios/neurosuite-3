/***************************************************************************
 * parameteryamlmodifier.cpp
 ***************************************************************************/

#include "parameteryamlmodifier.h"
#include "channelcolors.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ParameterYamlModifier::ParameterYamlModifier()
{
    // Start with a null node so we can detect "never parsed"
    m_root = YAML::Node(YAML::NodeType::Map);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::parseFile(const QString& path)
{
    try {
        m_root = YAML::LoadFile(path.toStdString());
        if (!m_root.IsDefined() || !m_root.IsMap()) {
            qWarning() << "ParameterYamlModifier: root is not a YAML map in" << path;
            m_root = YAML::Node(YAML::NodeType::Map);
            return false;
        }
        return true;
    } catch (const YAML::Exception& e) {
        qWarning() << "ParameterYamlModifier: parse error in" << path
                   << ":" << QString::fromStdString(e.what());
        m_root = YAML::Node(YAML::NodeType::Map);
        return false;
    }
}

bool ParameterYamlModifier::writeToFile(const QString& path)
{
    // Ensure the version/creator header is present (needed for new files)
    if (!m_root["parameters"]) {
        m_root["parameters"]["version"] = "1.0";
        m_root["parameters"]["creator"] = "neuroscope-3";
    }

    QString tmpPath = path + QLatin1String(".nstmp");
    try {
        std::ofstream out(tmpPath.toStdString());
        if (!out) {
            qWarning() << "ParameterYamlModifier: cannot open" << tmpPath << "for writing";
            return false;
        }
        YAML::Emitter emitter(out);
        emitter << m_root;
        out.close();
    } catch (const std::exception& e) {
        qWarning() << "ParameterYamlModifier: write error:" << e.what();
        QFile::remove(tmpPath);
        return false;
    }

    // Atomic replace
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "ParameterYamlModifier: cannot remove" << path;
        QFile::remove(tmpPath);
        return false;
    }
    if (!QFile::rename(tmpPath, path)) {
        qWarning() << "ParameterYamlModifier: cannot rename" << tmpPath << "to" << path;
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

YAML::Node ParameterYamlModifier::ensureMap(const std::string& key)
{
    if (!m_root[key] || !m_root[key].IsMap())
        m_root[key] = YAML::Node(YAML::NodeType::Map);
    return m_root[key];
}

template<typename T>
void ParameterYamlModifier::setScalar(YAML::Node node,
                                       const std::string& key,
                                       const T& value)
{
    node[key] = value;
}

// ---------------------------------------------------------------------------
// Acquisition system
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setAcquisitionSystemInformation(
        int resolution, int nbChannels, double samplingRate,
        int voltageRange, int amplification, int offset)
{
    auto acq = ensureMap("acquisitionSystem");
    acq["nBits"]         = resolution;
    acq["nChannels"]     = nbChannels;
    acq["samplingRate"]  = samplingRate;
    acq["voltageRange"]  = voltageRange;
    acq["amplification"] = amplification;
    acq["offset"]        = offset;
    return true;
}

// ---------------------------------------------------------------------------
// Field potentials
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setLfpInformation(double lfpSamplingRate)
{
    auto fp = ensureMap("fieldPotentials");
    fp["lfpSamplingRate"] = lfpSamplingRate;
    return true;
}

// ---------------------------------------------------------------------------
// Video (top-level)
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setVideoInformation(int width, int height)
{
    auto vid = ensureMap("video");
    vid["width"]  = width;
    vid["height"] = height;
    return true;
}

// ---------------------------------------------------------------------------
// File extension → sampling rate
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setSampleRateByExtension(
        const QMap<QString,double>& extensionSamplingRates)
{
    // files: is a sequence of { samplingRate: N, extension: "ext" }
    YAML::Node seq(YAML::NodeType::Sequence);
    for (auto it = extensionSamplingRates.constBegin();
         it != extensionSamplingRates.constEnd(); ++it) {
        YAML::Node entry;
        entry["samplingRate"] = it.value();
        entry["extension"]    = it.key().toStdString();
        seq.push_back(entry);
    }
    m_root["files"] = seq;
    return true;
}

// ---------------------------------------------------------------------------
// Spike detection
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setSpikeDetectionInformation(
        int nbSamples, int peakSampleIndex,
        QMap<int,QList<int>>& spikeGroups)
{
    // Preserve per-group nFeatures from any existing groups
    QMap<int,int> existingNFeatures;
    auto existing = m_root["spikeDetection"]["channelGroups"];
    if (existing && existing.IsSequence()) {
        int idx = 1;
        for (auto grp : existing) {
            if (grp["nFeatures"])
                existingNFeatures[idx] = grp["nFeatures"].as<int>(3);
            ++idx;
        }
    }

    // neuroscope/spikes also carries nSamples and peakSampleIndex
    if (!m_root["neuroscope"] || !m_root["neuroscope"].IsMap())
        m_root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    if (!m_root["neuroscope"]["spikes"] || !m_root["neuroscope"]["spikes"].IsMap())
        m_root["neuroscope"]["spikes"] = YAML::Node(YAML::NodeType::Map);
    m_root["neuroscope"]["spikes"]["nSamples"]       = nbSamples;
    m_root["neuroscope"]["spikes"]["peakSampleIndex"] = peakSampleIndex;

    auto sd = ensureMap("spikeDetection");
    YAML::Node seq(YAML::NodeType::Sequence);

    // spikeGroups keys: -1 = trash (skip), 0 = display-only (skip), ≥1 = real
    for (auto it = spikeGroups.constBegin(); it != spikeGroups.constEnd(); ++it) {
        if (it.key() <= 0) continue;   // skip trash / display-only groups
        YAML::Node grp;
        YAML::Node chSeq(YAML::NodeType::Sequence);
        for (int ch : it.value()) chSeq.push_back(ch);
        grp["channels"]        = chSeq;
        grp["nSamples"]        = nbSamples;
        grp["peakSampleIndex"] = peakSampleIndex;
        grp["nFeatures"]       = existingNFeatures.value(it.key(), 3);
        seq.push_back(grp);
    }
    sd["channelGroups"] = seq;
    return true;
}

bool ParameterYamlModifier::setSpikeDetectionInformation(
        QMap<int,QList<int>>& spikeGroups)
{
    // Preserve existing nSamples/peakSampleIndex
    int nbSamples       = 32;
    int peakSampleIndex = 16;
    if (m_root["neuroscope"]["spikes"]) {
        auto spk = m_root["neuroscope"]["spikes"];
        if (spk["nSamples"])       nbSamples       = spk["nSamples"].as<int>(32);
        if (spk["peakSampleIndex"]) peakSampleIndex = spk["peakSampleIndex"].as<int>(16);
    }
    return setSpikeDetectionInformation(nbSamples, peakSampleIndex, spikeGroups);
}

// ---------------------------------------------------------------------------
// Anatomical description
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setAnatomicalDescription(
        QMap<int,QList<int>>& anatomicalGroups,
        const QMap<int,bool>& skipStatus)
{
    auto ad = ensureMap("anatomicalDescription");
    YAML::Node seq(YAML::NodeType::Sequence);

    for (auto it = anatomicalGroups.constBegin();
         it != anatomicalGroups.constEnd(); ++it) {
        YAML::Node grp;
        YAML::Node chSeq(YAML::NodeType::Sequence);
        for (int ch : it.value()) {
            YAML::Node chNode;
            chNode["id"]   = ch;
            chNode["skip"] = skipStatus.value(ch, false) ? 1 : 0;
            chSeq.push_back(chNode);
        }
        grp["channels"] = chSeq;
        seq.push_back(grp);
    }
    ad["channelGroups"] = seq;
    return true;
}

// ---------------------------------------------------------------------------
// NeuroScope miscellaneous
// ---------------------------------------------------------------------------

void ParameterYamlModifier::setMiscellaneousInformation(
        float screenGain, const QString& traceBackgroundImage)
{
    if (!m_root["neuroscope"] || !m_root["neuroscope"].IsMap())
        m_root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    auto misc = m_root["neuroscope"]["miscellaneous"];
    if (!misc || !misc.IsMap()) {
        m_root["neuroscope"]["miscellaneous"] = YAML::Node(YAML::NodeType::Map);
        misc = m_root["neuroscope"]["miscellaneous"];
    }
    misc["screenGain"] = static_cast<double>(screenGain);
    if (traceBackgroundImage.isEmpty())
        misc["traceBackgroundImage"] = YAML::Node(YAML::NodeType::Null);
    else
        misc["traceBackgroundImage"] = traceBackgroundImage.toStdString();
}

// ---------------------------------------------------------------------------
// NeuroScope video
// ---------------------------------------------------------------------------

void ParameterYamlModifier::setNeuroscopeVideoInformation(
        int rotation, int flip,
        const QString& backgroundPath, int drawTrajectory)
{
    if (!m_root["neuroscope"] || !m_root["neuroscope"].IsMap())
        m_root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    auto vid = m_root["neuroscope"]["video"];
    if (!vid || !vid.IsMap()) {
        m_root["neuroscope"]["video"] = YAML::Node(YAML::NodeType::Map);
        vid = m_root["neuroscope"]["video"];
    }
    vid["rotate"]             = rotation;
    vid["flip"]               = flip;
    if (backgroundPath.isEmpty())
        vid["videoImage"] = YAML::Node(YAML::NodeType::Null);
    else
        vid["videoImage"] = backgroundPath.toStdString();
    vid["positionsBackground"] = drawTrajectory;
}

// ---------------------------------------------------------------------------
// NeuroScope channel display (colors + offsets)
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setChannelDisplayInformation(
        ChannelColors*  channelColors,
        QMap<int,int>&  channelsGroups,
        QMap<int,int>&  channelDefaultOffsets)
{
    if (!channelColors) return false;

    if (!m_root["neuroscope"] || !m_root["neuroscope"].IsMap())
        m_root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    if (!m_root["neuroscope"]["channels"] || !m_root["neuroscope"]["channels"].IsMap())
        m_root["neuroscope"]["channels"] = YAML::Node(YAML::NodeType::Map);

    auto channels = m_root["neuroscope"]["channels"];

    YAML::Node colorSeq(YAML::NodeType::Sequence);
    YAML::Node offsetSeq(YAML::NodeType::Sequence);

    // Use nChannels from acquisitionSystem as the authoritative count.
    // channelColors may contain fewer entries than nChannels (e.g. the first
    // time a session is opened), so iterate 0..nChannels-1 and fall back to
    // a default blue for any channel not present in channelColors.
    int nChannels = 0;
    if (m_root["acquisitionSystem"] && m_root["acquisitionSystem"]["nChannels"])
        nChannels = m_root["acquisitionSystem"]["nChannels"].as<int>(0);
    if (nChannels <= 0)
        nChannels = static_cast<int>(channelColors->numberOfChannels());

    const std::string defaultColor = "#0080ff";

    for (int chId = 0; chId < nChannels; ++chId) {
        YAML::Node cc;
        cc["channel"] = chId;
        if (channelColors->contains(chId)) {
            cc["color"]        = channelColors->color(chId).name().toStdString();
            cc["anatomyColor"] = channelColors->groupColor(chId).name().toStdString();
            cc["spikeColor"]   = channelColors->spikeGroupColor(chId).name().toStdString();
        } else {
            cc["color"]        = defaultColor;
            cc["anatomyColor"] = defaultColor;
            cc["spikeColor"]   = defaultColor;
        }
        colorSeq.push_back(cc);

        YAML::Node co;
        co["channel"]       = chId;
        co["defaultOffset"] = channelDefaultOffsets.value(chId, 0);
        offsetSeq.push_back(co);
    }

    channels["colors"]  = colorSeq;
    channels["offsets"] = offsetSeq;

    // channelsGroups is channel→anatomicalGroupId; not stored in YAML
    // (anatomical groups ARE already in anatomicalDescription); suppress
    // unused-parameter warning by referencing it
    (void)channelsGroups;

    return true;
}
