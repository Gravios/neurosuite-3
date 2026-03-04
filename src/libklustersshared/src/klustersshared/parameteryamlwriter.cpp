/***************************************************************************
 * parameteryamlwriter.cpp
 ***************************************************************************/

#include "parameteryamlwriter.h"

#include <QDebug>
#include <QFile>
#include <QDir>

#include <fstream>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

YAML::Node ParameterYamlWriter::strNode(const QString& s)
{
    // Write empty optional fields as the explicit YAML null scalar "~".
    // YAML::NodeType::Null is correct per-spec but yaml-cpp's emitter can
    // produce a bare "key:" (no value token) in certain block-mapping contexts
    // on some builds, which causes "illegal map value" errors on re-read.
    // Using "~" as a plain scalar is unambiguous across all yaml-cpp versions.
    YAML::Node n;
    n = s.isEmpty() ? std::string("~") : s.toStdString();
    return n;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ParameterYamlWriter::ParameterYamlWriter()
{
    // yaml-cpp 0.8: chained operator[] on a new map creates sub-maps in-place
    // only when the intermediate node is fetched as a live reference first.
    YAML::Node params = m_doc["parameters"];
    params["version"] = "1.0";
    params["creator"] = "ndManager-yaml";
}

// ---------------------------------------------------------------------------
// writeTofile  (atomic: write to .nstmp then rename)
// ---------------------------------------------------------------------------

bool ParameterYamlWriter::writeTofile(const QString& path)
{
    const QString tmpPath = path + QLatin1String(".nstmp");

    try {
        std::ofstream out(tmpPath.toStdString());
        if (!out) {
            qWarning() << "ParameterYamlWriter: cannot open" << tmpPath << "for writing";
            return false;
        }
        YAML::Emitter emitter(out);
        emitter.SetIndent(2);
        emitter.SetMapFormat(YAML::Block);
        emitter.SetSeqFormat(YAML::Block);
        emitter << m_doc;
        out.close();
    } catch (const std::exception& e) {
        qWarning() << "ParameterYamlWriter: write error:" << e.what();
        QFile::remove(tmpPath);
        return false;
    }

    // Atomic replace
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "ParameterYamlWriter: cannot remove" << path;
        QFile::remove(tmpPath);
        return false;
    }
    if (!QFile::rename(tmpPath, path)) {
        qWarning() << "ParameterYamlWriter: cannot rename" << tmpPath << "to" << path;
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// General information
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setGeneralInformation(GeneralInformation& gi)
{
    YAML::Node n = m_doc["generalInfo"];
    n["date"]          = gi.getDate().toString(Qt::ISODate).toStdString();
    n["experimenters"] = strNode(gi.getExperimenters());
    n["description"]   = strNode(gi.getDescription());
    n["notes"]         = strNode(gi.getNotes());
}

// ---------------------------------------------------------------------------
// Acquisition system
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setAcquisitionSystemInformation(
        const QMap<QString,double>& info)
{
    YAML::Node n = m_doc["acquisitionSystem"];
    auto setInt = [&](const char* key, const QString& mapKey) {
        if (info.contains(mapKey))
            n[key] = static_cast<int>(info[mapKey]);
    };
    setInt("nBits",         QStringLiteral("nBits"));
    setInt("nChannels",     QStringLiteral("nChannels"));
    setInt("voltageRange",  QStringLiteral("voltageRange"));
    setInt("amplification", QStringLiteral("amplification"));
    setInt("offset",        QStringLiteral("offset"));
    if (info.contains(QStringLiteral("samplingRate")))
        n["samplingRate"] = info[QStringLiteral("samplingRate")];
}

// ---------------------------------------------------------------------------
// Video (top-level)
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setVideoInformation(const QMap<QString,double>& info)
{
    YAML::Node n = m_doc["video"];
    if (info.contains(QStringLiteral("width")))
        n["width"]  = static_cast<int>(info[QStringLiteral("width")]);
    if (info.contains(QStringLiteral("height")))
        n["height"] = static_cast<int>(info[QStringLiteral("height")]);
    if (info.contains(QStringLiteral("samplingRate")))
        n["samplingRate"] = info[QStringLiteral("samplingRate")];
    if (info.contains(QStringLiteral("nSamples")))
        n["nSamples"] = static_cast<int>(info[QStringLiteral("nSamples")]);
}

// ---------------------------------------------------------------------------
// Field potentials
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setLfpInformation(double lfpSamplingRate)
{
    YAML::Node n = m_doc["fieldPotentials"];
    n["lfpSamplingRate"] = lfpSamplingRate;
}

// ---------------------------------------------------------------------------
// Additional files
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setFilesInformation(const QList<FileInformation>& files)
{
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& fi : files) {
        YAML::Node entry;
        entry["samplingRate"] = fi.getSamplingRate();
        entry["extension"]    = fi.getExtension().toStdString();
        const auto mapping = fi.getChannelMapping();
        if (!mapping.isEmpty()) {
            YAML::Node mapNode(YAML::NodeType::Sequence);
            for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
                YAML::Node m;
                m["original"] = it.key();
                YAML::Node targets(YAML::NodeType::Sequence);
                for (int ch : it.value()) targets.push_back(ch);
                m["mapped"] = targets;
                mapNode.push_back(m);
            }
            entry["channelMapping"] = mapNode;
        }
        seq.push_back(entry);
    }
    m_doc["files"] = seq;
}

// ---------------------------------------------------------------------------
// Anatomical description
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setAnatomicalDescription(
        QMap<int,QList<int>>& anatomicalGroups,
        QMap<QString,QMap<int,QString>>& attributes)
{
    const auto& skipAttr = attributes.value(QStringLiteral("skip"));
    YAML::Node groups(YAML::NodeType::Sequence);

    // Use iterator so non-contiguous keys (after group deletion) are handled
    // correctly — the original NdManagerYamlWriter used a 1..size() index
    // which would silently drop or corrupt groups when keys had gaps.
    for (auto it = anatomicalGroups.constBegin();
         it != anatomicalGroups.constEnd(); ++it) {
        const auto& chList = it.value();
        YAML::Node grp;
        YAML::Node channels(YAML::NodeType::Sequence);
        for (int ch : chList) {
            YAML::Node chNode;
            chNode["id"]   = ch;
            chNode["skip"] = skipAttr.value(ch, QStringLiteral("0")).toInt();
            channels.push_back(chNode);
        }
        grp["channels"] = channels;
        groups.push_back(grp);
    }
    YAML::Node anat = m_doc["anatomicalDescription"];
    anat["channelGroups"] = groups;
}

// ---------------------------------------------------------------------------
// Spike detection
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setSpikeDetectionInformation(
        QMap<int,QList<int>>& spikeGroups,
        QMap<int,QMap<QString,QString>>& information)
{
    YAML::Node groups(YAML::NodeType::Sequence);

    // Global nSamples/peakSampleIndex — take from first real group
    int globalNSamples = 0;
    int globalPeakSampleIndex = 0;

    // Use iterator; skip trash (key <= 0) and non-contiguous keys are fine
    for (auto it = spikeGroups.constBegin(); it != spikeGroups.constEnd(); ++it) {
        if (it.key() <= 0) continue;   // skip trash / display-only groups
        const auto& chList = it.value();
        const auto& info   = information.value(it.key());
        YAML::Node grp;
        YAML::Node channels(YAML::NodeType::Sequence);
        for (int ch : chList) channels.push_back(ch);
        grp["channels"]        = channels;
        const int nSamples      = info.value(QStringLiteral("nSamples"),       QStringLiteral("0")).toInt();
        const int peakSample    = info.value(QStringLiteral("peakSampleIndex"),QStringLiteral("0")).toInt();
        grp["nSamples"]        = nSamples;
        grp["peakSampleIndex"] = peakSample;
        grp["nFeatures"]       = info.value(QStringLiteral("nFeatures"),       QStringLiteral("0")).toInt();
        groups.push_back(grp);

        if (globalNSamples == 0 && nSamples > 0) {
            globalNSamples       = nSamples;
            globalPeakSampleIndex = peakSample;
        }
    }

    YAML::Node spike = m_doc["spikeDetection"];
    spike["channelGroups"] = groups;

    // Also keep neuroscope/spikes in sync so neuroscope reads the right values
    if (globalNSamples > 0) {
        YAML::Node ns  = m_doc["neuroscope"];
        YAML::Node spk = ns["spikes"];
        // Only write if not already set by setNeuroscopeSpikeInformation
        if (!spk["nSamples"]) {
            spk["nSamples"]        = globalNSamples;
            spk["peakSampleIndex"] = globalPeakSampleIndex;
        }
    }
}

// ---------------------------------------------------------------------------
// NeuroScope miscellaneous
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setMiscellaneousInformation(
        float screenGain, const QString& traceBackgroundImage)
{
    YAML::Node ns   = m_doc["neuroscope"];
    YAML::Node misc = ns["miscellaneous"];
    misc["screenGain"]           = static_cast<double>(screenGain);
    misc["traceBackgroundImage"] = strNode(traceBackgroundImage);
}

// ---------------------------------------------------------------------------
// NeuroScope video
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setNeuroscopeVideoInformation(NeuroscopeVideoInfo& vi)
{
    YAML::Node ns = m_doc["neuroscope"];
    YAML::Node v  = ns["video"];
    v["rotate"]              = vi.getRotation();
    v["flip"]                = vi.getFlip();
    v["videoImage"]          = strNode(vi.getBackgroundImage());
    v["positionsBackground"] = vi.getTrajectory();
}

// ---------------------------------------------------------------------------
// NeuroScope spike display
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setNeuroscopeSpikeInformation(int nbSamples,
                                                         int peakSampleIndex)
{
    YAML::Node ns  = m_doc["neuroscope"];
    YAML::Node spk = ns["spikes"];
    spk["nSamples"]        = nbSamples;
    spk["peakSampleIndex"] = peakSampleIndex;
}

// ---------------------------------------------------------------------------
// Channel display
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setChannelDisplayInformation(
        const QList<ChannelColorEntry>& colorList,
        const QMap<int,int>& channelDefaultOffsets)
{
    YAML::Node colors(YAML::NodeType::Sequence);
    YAML::Node offsets(YAML::NodeType::Sequence);

    for (const auto& cc : colorList) {
        YAML::Node c;
        c["channel"]      = cc.getId();
        c["color"]        = cc.getColor().name().toStdString();
        c["anatomyColor"] = cc.getGroupColor().name().toStdString();
        c["spikeColor"]   = cc.getSpikeGroupColor().name().toStdString();
        colors.push_back(c);

        YAML::Node o;
        o["channel"]       = cc.getId();
        o["defaultOffset"] = channelDefaultOffsets.value(cc.getId(), 0);
        offsets.push_back(o);
    }

    YAML::Node ns = m_doc["neuroscope"];
    YAML::Node ch = ns["channels"];
    ch["colors"]  = colors;
    ch["offsets"] = offsets;
}

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setProgramsInformation(
        const QList<ProgramInformation>& programs)
{
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& pi : programs) {
        YAML::Node prog;
        prog["name"] = pi.getProgramName().toStdString();

        const auto& params = pi.getParameterInformation();
        if (!params.isEmpty()) {
            YAML::Node paramSeq(YAML::NodeType::Sequence);
            for (auto it = params.cbegin(); it != params.cend(); ++it) {
                const QStringList& row = it.value();
                if (row.size() < 3) continue;
                YAML::Node p;
                p["name"] = row[0].toStdString();
                bool ok;
                int iv = row[1].toInt(&ok);
                if (ok) { p["value"] = iv; }
                else {
                    double dv = row[1].toDouble(&ok);
                    if (ok) { p["value"] = dv; }
                    else if (row[1].isEmpty()) { p["value"] = std::string("~"); }
                    else { p["value"] = row[1].toStdString(); }
                }
                p["status"] = row[2].toStdString();
                paramSeq.push_back(p);
            }
            prog["parameters"] = paramSeq;
        }

        const QString help = pi.getHelp();
        if (!help.isEmpty())
            prog["help"] = help.toStdString();

        seq.push_back(prog);
    }
    m_doc["programs"] = seq;
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setUnitsInformation(const QMap<int,QStringList>& units)
{
    YAML::Node seq(YAML::NodeType::Sequence);
    for (auto it = units.cbegin(); it != units.cend(); ++it) {
        const QStringList& row = it.value();
        if (row.size() < 7) continue;
        YAML::Node u;
        u["group"]             = row[0].toInt();
        u["cluster"]           = row[1].toInt();
        u["structure"]         = strNode(row[2]);
        u["type"]              = strNode(row[3]);
        u["isolationDistance"] = strNode(row[4]);
        u["quality"]           = strNode(row[5]);
        u["notes"]             = strNode(row[6]);
        seq.push_back(u);
    }
    m_doc["units"] = seq;
}
