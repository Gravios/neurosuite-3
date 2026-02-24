/***************************************************************************
 * ndmanageryamlwriter.cpp
 ***************************************************************************/

#include "ndmanageryamlwriter.h"

#include <QFile>
#include <QTextStream>
#include <QDebug>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static YAML::Node strNode(const QString& s)
{
    if (s.isEmpty()) return YAML::Node(YAML::NodeType::Null);
    YAML::Node n;
    n = s.toStdString();
    return n;
}

// yaml-cpp 0.8: operator[] on a Map node returns a Node by value when the
// key doesn't yet exist.  Chained writes like m_doc["a"]["b"] = x therefore
// create a dangling temporary.  The safe pattern is:
//
//   YAML::Node sub = m_doc["a"];   // creates/fetches the sub-map in-place
//   sub["b"] = x;                  // now modifies the live node
//
// Single-level writes (m_doc["key"] = x) are fine as-is.

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NdManagerYamlWriter::NdManagerYamlWriter()
{
    m_doc["parameters"]["version"] = "1.0";
    m_doc["parameters"]["creator"] = "ndManager-yaml";
}

// ---------------------------------------------------------------------------
// writeTofile
// ---------------------------------------------------------------------------

bool NdManagerYamlWriter::writeTofile(const QString& path)
{
    YAML::Emitter out;
    out.SetIndent(2);
    out.SetMapFormat(YAML::Block);
    out.SetSeqFormat(YAML::Block);
    out << m_doc;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "NdManagerYamlWriter: cannot open" << path << "for writing";
        return false;
    }
    QTextStream ts(&file);
    ts << QString::fromStdString(out.c_str());
    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// General information
// ---------------------------------------------------------------------------

void NdManagerYamlWriter::setGeneralInformation(GeneralInformation& gi)
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

void NdManagerYamlWriter::setAcquisitionSystemInformation(
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
// Video
// ---------------------------------------------------------------------------

void NdManagerYamlWriter::setVideoInformation(const QMap<QString,double>& info)
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

void NdManagerYamlWriter::setLfpInformation(double lfpSamplingRate)
{
    YAML::Node n = m_doc["fieldPotentials"];
    n["lfpSamplingRate"] = lfpSamplingRate;
}

// ---------------------------------------------------------------------------
// Additional files
// ---------------------------------------------------------------------------

void NdManagerYamlWriter::setFilesInformation(const QList<FileInformation>& files)
{
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& fi : files) {
        YAML::Node entry;
        entry["samplingRate"] = fi.getSamplingRate();
        entry["extension"]    = fi.getExtension().toStdString();
        auto mapping = const_cast<FileInformation&>(fi).getChannelMapping();
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

void NdManagerYamlWriter::setAnatomicalDescription(
        QMap<int,QList<int>>& anatomicalGroups,
        QMap<QString,QMap<int,QString>>& attributes)
{
    const auto& skipAttr = attributes.value(QStringLiteral("skip"));
    YAML::Node groups(YAML::NodeType::Sequence);
    for (int gid = 1; gid <= anatomicalGroups.size(); ++gid) {
        const auto& chList = anatomicalGroups.value(gid);
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

void NdManagerYamlWriter::setSpikeDetectionInformation(
        QMap<int,QList<int>>& spikeGroups,
        QMap<int,QMap<QString,QString>>& information)
{
    YAML::Node groups(YAML::NodeType::Sequence);
    for (int gid = 1; gid <= spikeGroups.size(); ++gid) {
        const auto& chList = spikeGroups.value(gid);
        const auto& info   = information.value(gid);
        YAML::Node grp;
        YAML::Node channels(YAML::NodeType::Sequence);
        for (int ch : chList) channels.push_back(ch);
        grp["channels"]        = channels;
        grp["nSamples"]        = info.value(QStringLiteral("nSamples"),        QStringLiteral("0")).toInt();
        grp["peakSampleIndex"] = info.value(QStringLiteral("peakSampleIndex"), QStringLiteral("0")).toInt();
        grp["nFeatures"]       = info.value(QStringLiteral("nFeatures"),       QStringLiteral("0")).toInt();
        groups.push_back(grp);
    }
    YAML::Node spike = m_doc["spikeDetection"];
    spike["channelGroups"] = groups;
}

// ---------------------------------------------------------------------------
// NeuroScope miscellaneous
// ---------------------------------------------------------------------------

void NdManagerYamlWriter::setMiscellaneousInformation(
        float screenGain, const QString& traceBackgroundImage)
{
    YAML::Node ns = m_doc["neuroscope"];
    YAML::Node misc = ns["miscellaneous"];
    misc["screenGain"]           = static_cast<double>(screenGain);
    misc["traceBackgroundImage"] = strNode(traceBackgroundImage);
}

// ---------------------------------------------------------------------------
// NeuroScope video
// ---------------------------------------------------------------------------

void NdManagerYamlWriter::setNeuroscopeVideoInformation(NeuroscopeVideoInfo& vi)
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

void NdManagerYamlWriter::setNeuroscopeSpikeInformation(int nbSamples,
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

void NdManagerYamlWriter::setChannelDisplayInformation(
        const QList<ChannelColors>& colorList,
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

void NdManagerYamlWriter::setProgramsInformation(
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
                    else if (row[1].isEmpty()) { p["value"] = YAML::Node(YAML::NodeType::Null); }
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

void NdManagerYamlWriter::setUnitsInformation(const QMap<int,QStringList>& units)
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
