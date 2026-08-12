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
    root = YAML::Node(YAML::NodeType::Map);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::parseFile(const QString& path)
{
    try {
        root = YAML::LoadFile(path.toStdString());
        if (!root.IsDefined() || !root.IsMap()) {
            qWarning() << "ParameterYamlModifier: root is not a YAML map in" << path;
            root = YAML::Node(YAML::NodeType::Map);
            return false;
        }
        return true;
    } catch (const YAML::Exception& e) {
        qWarning() << "ParameterYamlModifier: parse error in" << path
                   << ":" << QString::fromStdString(e.what());
        root = YAML::Node(YAML::NodeType::Map);
        return false;
    }
}

bool ParameterYamlModifier::writeToFile(const QString& path)
{
    // Ensure the version/creator header is present (needed for new files)
    if (!root["parameters"]) {
        root["parameters"]["version"] = "1.0";
        root["parameters"]["creator"] = "neuroscope-3";
    }

    QString tmpPath = path + QLatin1String(".nstmp");
    try {
        std::ofstream out(tmpPath.toStdString());
        if (!out) {
            qWarning() << "ParameterYamlModifier: cannot open" << tmpPath << "for writing";
            return false;
        }
        YAML::Emitter emitter(out);
        emitter.SetIndent(2);
        emitter.SetMapFormat(YAML::Block);
        emitter.SetSeqFormat(YAML::Block);
        emitter << root;
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
    if (!root[key] || !root[key].IsMap())
        root[key] = YAML::Node(YAML::NodeType::Map);
    return root[key];
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
    root["files"] = seq;
    return true;
}

// ---------------------------------------------------------------------------
// Spike detection
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setSpikeDetectionInformation(
        int nbSamples, int peakSampleIndex,
        QMap<int,QList<int>>& spikeGroups)
{
    // Preserve per-group nFeatures, nSamples AND peakSampleIndex from any existing
    // groups.
    //
    // nSamples/peakSampleIndex are PER GROUP in this format, and klusters reads them
    // that way (ParameterYamlReader::getNbSamples(electrodeGroupID)).  Neuroscope,
    // the only caller of this overload, models exactly one of each for the whole
    // session -- "Neuroscope uses the same values for all the groups" -- so the
    // values it passes in are not per-group knowledge, they are the single value it
    // happened to load.  Writing them across every group therefore destroys geometry
    // the caller never modelled, and it is destroyed on ANY neuroscope save: opening
    // a session to look at traces and hitting Save is enough.
    //
    // The damage is silent and downstream: klusters derives the spike count as
    // spkFileLength / (nChannels * nSamples * sampleSize), so a wrong nSamples makes
    // a perfectly good .clu look like it has the wrong number of entries and the
    // session stops loading, pointing at the cluster files rather than at the .yaml.
    //
    // So an existing per-group value always wins.  The caller's value is used only
    // for groups that do not have one -- a group neuroscope just created, where its
    // session-wide value is the only information available.
    QMap<int,int> existingNFeatures, existingNSamples, existingPeak, existingFeatureLag;
    auto existing = root["spikeDetection"]["channelGroups"];
    if (existing && existing.IsSequence()) {
        int idx = 1;
        for (auto grp : existing) {
            if (grp["nFeatures"])
                existingNFeatures[idx] = grp["nFeatures"].as<int>(3);
            if (grp["nSamples"])
                existingNSamples[idx] = grp["nSamples"].as<int>(0);
            if (grp["peakSampleIndex"])
                existingPeak[idx] = grp["peakSampleIndex"].as<int>(0);
            // featureLag: 0 = the classic PC1,PC2,PC3 space.  Non-zero selects the
            // lag feature space fiber-session builds under --feat-lag N: PC1 sampled
            // at -N/0/+N samples plus PC2, four columns per channel instead of three.
            //
            // It is per GROUP because nFeatures already is, and the two describe the
            // same thing -- the width and the meaning of a channel's feature block.
            // A group can be re-featurised on its own, so a session-wide value would
            // be wrong for every group that had not been.
            //
            // Absent means 0, and 0 means classic.  Preserved on save like the other
            // three: this writer turns an absent spike-group field into its default,
            // so a field it does not know about is silently zeroed -- which is how a
            // key-name typo once became nSamples: 0.
            if (grp["featureLag"])
                existingFeatureLag[idx] = grp["featureLag"].as<int>(0);
            ++idx;
        }
    }

    // neuroscope/spikes also carries nSamples and peakSampleIndex
    if (!root["neuroscope"] || !root["neuroscope"].IsMap())
        root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    if (!root["neuroscope"]["spikes"] || !root["neuroscope"]["spikes"].IsMap())
        root["neuroscope"]["spikes"] = YAML::Node(YAML::NodeType::Map);
    root["neuroscope"]["spikes"]["nSamples"]       = nbSamples;
    root["neuroscope"]["spikes"]["peakSampleIndex"] = peakSampleIndex;

    auto sd = ensureMap("spikeDetection");
    YAML::Node seq(YAML::NodeType::Sequence);

    // spikeGroups keys: -1 = trash (skip), 0 = display-only (skip), ≥1 = real
    for (auto it = spikeGroups.constBegin(); it != spikeGroups.constEnd(); ++it) {
        if (it.key() <= 0) continue;   // skip trash / display-only groups
        YAML::Node grp;
        YAML::Node chSeq(YAML::NodeType::Sequence);
        for (int ch : it.value()) chSeq.push_back(ch);
        grp["channels"]        = chSeq;
        grp["nSamples"]        = existingNSamples.value(it.key(), nbSamples);
        grp["peakSampleIndex"] = existingPeak.value(it.key(), peakSampleIndex);
        grp["nFeatures"]       = existingNFeatures.value(it.key(), 3);
        // Written only when non-zero, so a classic session's YAML is unchanged and
        // an older ndmanager reading it sees exactly what it saw before.
        if (existingFeatureLag.value(it.key(), 0) != 0)
            grp["featureLag"]  = existingFeatureLag.value(it.key(), 0);
        seq.push_back(grp);
    }
    sd["channelGroups"] = seq;
    return true;
}

bool ParameterYamlModifier::setSpikeDetectionInformation(
        QMap<int,QList<int>>& spikeGroups)
{
    // Fallbacks for groups that have no per-group value of their own.  These are
    // NOT defaults to invent: 32/16 used to be hardcoded here, and 32/16 is a real
    // waveform geometry, so inventing it produced a file that reads as valid and
    // silently mis-sizes every spike.  Prefer the neuroscope tag, then any existing
    // per-group value, and refuse rather than guess when the file carries neither.
    int nbSamples = 0, peakSampleIndex = 0;
    if (root["neuroscope"] && root["neuroscope"]["spikes"]) {
        auto spk = root["neuroscope"]["spikes"];
        if (spk["nSamples"])        nbSamples       = spk["nSamples"].as<int>(0);
        if (spk["peakSampleIndex"]) peakSampleIndex = spk["peakSampleIndex"].as<int>(0);
    }
    if (nbSamples <= 0 || peakSampleIndex <= 0) {
        auto groups = root["spikeDetection"]["channelGroups"];
        if (groups && groups.IsSequence()) {
            for (auto grp : groups) {
                if (nbSamples <= 0 && grp["nSamples"])
                    nbSamples = grp["nSamples"].as<int>(0);
                if (peakSampleIndex <= 0 && grp["peakSampleIndex"])
                    peakSampleIndex = grp["peakSampleIndex"].as<int>(0);
                if (nbSamples > 0 && peakSampleIndex > 0) break;
            }
        }
    }
    if (nbSamples <= 0 || peakSampleIndex <= 0)
        return false;      // nothing to preserve and nothing safe to invent
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
    if (!root["neuroscope"] || !root["neuroscope"].IsMap())
        root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    auto misc = root["neuroscope"]["miscellaneous"];
    if (!misc || !misc.IsMap()) {
        root["neuroscope"]["miscellaneous"] = YAML::Node(YAML::NodeType::Map);
        misc = root["neuroscope"]["miscellaneous"];
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
    if (!root["neuroscope"] || !root["neuroscope"].IsMap())
        root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    auto vid = root["neuroscope"]["video"];
    if (!vid || !vid.IsMap()) {
        root["neuroscope"]["video"] = YAML::Node(YAML::NodeType::Map);
        vid = root["neuroscope"]["video"];
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

    if (!root["neuroscope"] || !root["neuroscope"].IsMap())
        root["neuroscope"] = YAML::Node(YAML::NodeType::Map);
    if (!root["neuroscope"]["channels"] || !root["neuroscope"]["channels"].IsMap())
        root["neuroscope"]["channels"] = YAML::Node(YAML::NodeType::Map);

    auto channels = root["neuroscope"]["channels"];

    YAML::Node colorSeq(YAML::NodeType::Sequence);
    YAML::Node offsetSeq(YAML::NodeType::Sequence);

    // Use nChannels from acquisitionSystem as the authoritative count.
    // channelColors may contain fewer entries than nChannels (e.g. the first
    // time a session is opened), so iterate 0..nChannels-1 and fall back to
    // a default blue for any channel not present in channelColors.
    int nChannels = 0;
    if (root["acquisitionSystem"] && root["acquisitionSystem"]["nChannels"])
        nChannels = root["acquisitionSystem"]["nChannels"].as<int>(0);
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

// ---------------------------------------------------------------------------
// Units (cluster user information)
// ---------------------------------------------------------------------------

bool ParameterYamlModifier::setUnitsInformation(const QMap<int,QStringList>& units)
{
    YAML::Node seq(YAML::NodeType::Sequence);
    for (auto it = units.cbegin(); it != units.cend(); ++it) {
        const QStringList& row = it.value();
        if (row.size() < 7) continue;
        YAML::Node u;
        u["group"]             = row[0].toInt();
        u["cluster"]           = row[1].toInt();
        auto strOrNull = [](const QString& s) -> YAML::Node {
            if (s.isEmpty()) return YAML::Node(YAML::NodeType::Null);
            YAML::Node n; n = s.toStdString(); return n;
        };
        u["structure"]         = strOrNull(row[2]);
        u["type"]              = strOrNull(row[3]);
        u["isolationDistance"] = strOrNull(row[4]);
        u["quality"]           = strOrNull(row[5]);
        u["notes"]             = strOrNull(row[6]);
        seq.push_back(u);
    }
    root["units"] = seq;
    return true;
}
