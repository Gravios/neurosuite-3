/***************************************************************************
 * parameteryamlwriter.cpp
 ***************************************************************************/

#include "parameteryamlwriter.h"
#include "parameteryamlmodifier.h"       // matchExistingGroups
#include "parameteryamlreader_probes.h"  // writeProbesSection, ProbeEntry
#include "yamlformat.h"                  // emitWithLayout

#include <QDebug>
#include <QFile>
#include <QDir>
#include <QPair>

#include <cmath>
#include <fstream>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

YAML::Node ParameterYamlWriter::strNode(const QString& s)
{
    // An empty field is written as a YAML null.  It must be a Null node: a
    // std::string("~") is a string whose content happens to be a tilde, and
    // yaml-cpp emits it quoted ("~"), so every other reader (the ndm_* scripts,
    // PyYAML) gets the text "~" instead of "unset".  A Null node is emitted
    // as a plain ~ in block maps, block sequences and as a final value, and
    // reads back as null in yaml-cpp and PyYAML (checked with yaml-cpp 0.7.0,
    // Debian 12, and 0.8.0, Ubuntu 24.04).
    if (s.isEmpty()) return YAML::Node(YAML::NodeType::Null);
    return YAML::Node(s.toStdString());
}

// parent[key] as a map, replacing whatever non-map the base document held there
// (e.g. a null "spikes: ~").  The returned node is live: keys set on it land in
// parent.
static YAML::Node mapAt(YAML::Node parent, const char* key)
{
    if (!parent[key] || !parent[key].IsMap())
        parent[key] = YAML::Node(YAML::NodeType::Map);
    return parent[key];
}

// Existing map entries of the sequence @p seq, keyed by what @p keyOf returns
// for them (entries it cannot key, ok = false, are left out).  Deep copies, so
// the caller can rebuild the sequence from them.  The first entry with a given
// key wins; later duplicates are dropped with the rest of the old sequence.
template<typename K, typename F>
static QMap<K,YAML::Node> entriesBy(const YAML::Node& seq, F keyOf)
{
    QMap<K,YAML::Node> m;
    if (!seq || !seq.IsSequence()) return m;
    for (const auto& e : seq) {
        if (!e.IsMap()) continue;
        bool ok = false;
        const K k = keyOf(e, ok);
        if (ok && !m.contains(k)) m.insert(k, YAML::Clone(e));
    }
    return m;
}

// n[key] read through the const operator[], so looking does not touch the
// document: an absent key, or a parent that is not a map, gives an undefined
// node instead of inserting one (or throwing on a scalar).
static YAML::Node child(const YAML::Node& n, const char* key)
{
    if (!n || !n.IsMap()) return YAML::Node();
    return n[key];
}

static int scalarInt(const YAML::Node& n, bool& ok)
{
    ok = false;
    if (!n || !n.IsScalar()) return 0;
    try { const int v = n.as<int>(); ok = true; return v; } catch (...) {}
    return 0;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ParameterYamlWriter::ParameterYamlWriter()
{
    // yaml-cpp 0.8: chained operator[] on a new map creates sub-maps in-place
    // only when the intermediate node is fetched as a live reference first.
    YAML::Node params = doc["parameters"];
    params["version"] = "1.0";
    params["version"].SetTag("!");   // a string: written '1.0', not the number 1.0
    params["creator"] = "ndManager-yaml";
}

// ---------------------------------------------------------------------------
// setBaseDocument
// ---------------------------------------------------------------------------

bool ParameterYamlWriter::setBaseDocument(const QString& path)
{
    if (path.isEmpty() || !QFile::exists(path)) return false;
    YAML::Node base;
    try {
        base = YAML::LoadFile(path.toStdString());
    } catch (const YAML::Exception& e) {
        qWarning() << "ParameterYamlWriter: cannot use" << path << "as the base document:"
                   << QString::fromStdString(e.what());
        return false;
    }
    // A file that is not a mapping (an XML session, an empty file) has nothing
    // to carry; the writer keeps its fresh document.
    if (!base.IsMap()) return false;

    doc = base;
    baseText = yamlformat::readText(path.toStdString());
    // The version/creator header is only filled in where the file lacks it:
    // like ParameterYamlModifier, a save does not restamp what it did not model.
    YAML::Node params = mapAt(doc, "parameters");
    if (!params["version"]) { params["version"] = "1.0"; params["version"].SetTag("!"); }
    if (!params["creator"]) params["creator"] = "ndManager-yaml";
    return true;
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
        // The base document's comments, inline lists and quoting are kept
        // (yamlformat.h); a new file gets the same block layout as before,
        // with lists of scalars written inline.
        out << yamlformat::emitWithLayout(doc, baseText);
        out.close();
        if (!out) {
            qWarning() << "ParameterYamlWriter: write error on" << tmpPath;
            QFile::remove(tmpPath);
            return false;
        }
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
    YAML::Node n = mapAt(doc, "generalInfo");
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
    // Only the keys ndmanager models are set; any other key the base document
    // holds here (bitResolution_uV, nSamples, duration_s from ndm_plx2dat, ...)
    // is left in place.
    YAML::Node n = mapAt(doc, "acquisitionSystem");
    // ndmanager models these as ints read through a rounding reader, so a value
    // the file already holds that denotes the same number -- 1000.0 for 1000, a
    // fractional amplification rounded on load -- is left as written rather
    // than replaced by the coarser copy (the rule ParameterYamlModifier uses).
    auto sameNumber = [&n](const char* key, double value, bool rounded) {
        const YAML::Node old = child(n, key);
        if (!old || !old.IsScalar()) return false;
        try {
            const double d = old.as<double>();
            return rounded ? std::lround(d) == std::lround(value) : d == value;
        } catch (...) { return false; }
    };
    auto setInt = [&](const char* key, const QString& mapKey) {
        if (!info.contains(mapKey) || sameNumber(key, info[mapKey], true)) return;
        n[key] = static_cast<int>(info[mapKey]);
    };
    setInt("nBits",         QStringLiteral("nBits"));
    setInt("nChannels",     QStringLiteral("nChannels"));
    setInt("voltageRange",  QStringLiteral("voltageRange"));
    setInt("amplification", QStringLiteral("amplification"));
    setInt("offset",        QStringLiteral("offset"));
    if (info.contains(QStringLiteral("samplingRate")) &&
        !sameNumber("samplingRate", info[QStringLiteral("samplingRate")], false))
        n["samplingRate"] = info[QStringLiteral("samplingRate")];
}

// ---------------------------------------------------------------------------
// Video (top-level)
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setVideoInformation(const QMap<QString,double>& info)
{
    YAML::Node n = mapAt(doc, "video");
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
    YAML::Node n = mapAt(doc, "fieldPotentials");
    n["lfpSamplingRate"] = lfpSamplingRate;
}

// ---------------------------------------------------------------------------
// Additional files
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setFilesInformation(const QList<FileInformation>& files)
{
    // ndmanager owns this list: a file it no longer lists was deleted there.
    // An entry it still lists keeps any key ndmanager does not model.
    if (files.isEmpty()) {
        doc.remove("files");
        return;
    }
    QMap<QString,YAML::Node> old = entriesBy<QString>(child(doc, "files"),
        [](const YAML::Node& e, bool& ok) {
            const YAML::Node x = child(e, "extension");
            ok = x && x.IsScalar();
            return ok ? QString::fromStdString(x.Scalar()) : QString();
        });
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& fi : files) {
        YAML::Node entry = old.contains(fi.getExtension())
                           ? old.take(fi.getExtension())
                           : YAML::Node(YAML::NodeType::Map);
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
        } else {
            entry.remove("channelMapping");
        }
        seq.push_back(entry);
    }
    doc["files"] = seq;
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

    // Groups and channel entries already in the base document are carried
    // across whole (probeId, shankIndex, a channel's depth, ...), with only the
    // channel list and each channel's skip flag set from ndmanager's model.
    // Pairing is ParameterYamlModifier's: same channel set first, then position.
    const YAML::Node existing = child(child(doc, "anatomicalDescription"), "channelGroups");
    const QMap<int,YAML::Node> carried =
        ParameterYamlModifier::matchExistingGroups(existing, anatomicalGroups);
    QMap<int,YAML::Node> oldChannels;            // channel id -> its entry
    if (existing && existing.IsSequence()) {
        for (const auto& grp : existing) {
            if (!grp.IsMap()) continue;
            const YAML::Node chs = child(grp, "channels");
            if (!chs || !chs.IsSequence()) continue;
            for (const auto& ch : chs) {
                bool ok = false;
                const int id = scalarInt(child(ch, "id"), ok);
                if (ok && !oldChannels.contains(id)) oldChannels.insert(id, YAML::Clone(ch));
            }
        }
    }

    // Use iterator so non-contiguous keys (after group deletion) are handled
    // correctly — the original NdManagerYamlWriter used a 1..size() index
    // which would silently drop or corrupt groups when keys had gaps.
    for (auto it = anatomicalGroups.constBegin();
         it != anatomicalGroups.constEnd(); ++it) {
        const auto& chList = it.value();
        YAML::Node grp = carried.value(it.key(), YAML::Node(YAML::NodeType::Map));
        YAML::Node channels(YAML::NodeType::Sequence);
        for (int ch : chList) {
            YAML::Node chNode = oldChannels.value(ch, YAML::Node(YAML::NodeType::Map));
            chNode["id"]   = ch;
            chNode["skip"] = skipAttr.value(ch, QStringLiteral("0")).toInt();
            channels.push_back(chNode);
        }
        grp["channels"] = channels;
        groups.push_back(grp);
    }
    YAML::Node anat = mapAt(doc, "anatomicalDescription");
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

    // Groups already in the base document are carried across whole -- probeId,
    // shankIndex, sitePositions_um, a klustakwik: block, nCells, any key a later
    // tool adds -- and only what ndmanager models is set on them.  Pairing is
    // ParameterYamlModifier's (same channel set first, then position), so a
    // group ndmanager renumbered keeps its own keys; a group it deleted is gone.
    const YAML::Node existing = child(child(doc, "spikeDetection"), "channelGroups");
    const QMap<int,YAML::Node> carried =
        ParameterYamlModifier::matchExistingGroups(existing, spikeGroups);

    // Global nSamples/peakSampleIndex — take from first real group
    int globalNSamples = 0;
    int globalPeakSampleIndex = 0;

    // Use iterator; skip trash (key <= 0) and non-contiguous keys are fine
    for (auto it = spikeGroups.constBegin(); it != spikeGroups.constEnd(); ++it) {
        if (it.key() <= 0) continue;   // skip trash / display-only groups
        const auto& chList = it.value();
        const auto& info   = information.value(it.key());
        YAML::Node grp = carried.value(it.key(), YAML::Node(YAML::NodeType::Map));
        YAML::Node channels(YAML::NodeType::Sequence);
        for (int ch : chList) channels.push_back(ch);
        grp["channels"]        = channels;
        // A value ndmanager has (its table only accepts digits) is written.  A
        // cell left empty keeps the group's existing value rather than writing
        // 0 over it -- 0 is never a usable waveform geometry -- and a group
        // with no value at all gets 0, as before.
        auto putInt = [&grp, &info](const char* key) {
            bool ok = false;
            const int v = info.value(QString::fromLatin1(key)).toInt(&ok);
            if (ok) { grp[key] = v; return v; }
            if (!child(grp, key)) { grp[key] = 0; return 0; }
            bool had = false;
            const int kept = scalarInt(child(grp, key), had);
            return had ? kept : 0;
        };
        const int nSamples   = putInt("nSamples");
        const int peakSample = putInt("peakSampleIndex");
        putInt("nFeatures");
        // Emit the custom difference pattern only for groups that carry one, so
        // standard sessions stay free of an empty sdiffPairs key.  ndmanager
        // shows the pattern, so a pattern cleared there is removed.
        if (info.contains(QStringLiteral("sdiffPairs")) &&
            !info.value(QStringLiteral("sdiffPairs")).isEmpty())
            grp["sdiffPairs"]  = info.value(QStringLiteral("sdiffPairs")).toStdString();
        else
            grp.remove("sdiffPairs");
        groups.push_back(grp);

        if (globalNSamples == 0 && nSamples > 0) {
            globalNSamples       = nSamples;
            globalPeakSampleIndex = peakSample;
        }
    }

    YAML::Node spike = mapAt(doc, "spikeDetection");
    spike["channelGroups"] = groups;

    // Also keep neuroscope/spikes in sync so neuroscope reads the right values
    if (globalNSamples > 0) {
        YAML::Node ns  = mapAt(doc, "neuroscope");
        YAML::Node spk = mapAt(ns, "spikes");
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
    YAML::Node ns   = mapAt(doc, "neuroscope");
    YAML::Node misc = mapAt(ns, "miscellaneous");
    misc["screenGain"]           = static_cast<double>(screenGain);
    misc["traceBackgroundImage"] = strNode(traceBackgroundImage);
}

// ---------------------------------------------------------------------------
// NeuroScope video
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setNeuroscopeVideoInformation(NeuroscopeVideoInfo& vi)
{
    YAML::Node ns = mapAt(doc, "neuroscope");
    YAML::Node v  = mapAt(ns, "video");
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
    // A value <= 0 is ndmanager's blank field (or an unset value it read as
    // 0): leave the key as the document has it -- the base file's value, or
    // the one setSpikeDetectionInformation() derived -- instead of writing 0.
    if (nbSamples <= 0 && peakSampleIndex <= 0)
        return;
    YAML::Node ns  = mapAt(doc, "neuroscope");
    YAML::Node spk = mapAt(ns, "spikes");
    if (nbSamples > 0)
        spk["nSamples"]        = nbSamples;
    if (peakSampleIndex > 0)
        spk["peakSampleIndex"] = peakSampleIndex;
}

// ---------------------------------------------------------------------------
// Channel display
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setChannelDisplayInformation(
        const QList<ChannelColorEntry>& colorList,
        const QMap<int,int>& channelDefaultOffsets)
{
    YAML::Node ns = mapAt(doc, "neuroscope");
    YAML::Node ch = mapAt(ns, "channels");

    // Existing per-channel entries are updated rather than rebuilt, so keys
    // other tools add to them (a label, say) survive.
    auto byChannel = [](const YAML::Node& e, bool& ok) { return scalarInt(child(e, "channel"), ok); };
    QMap<int,YAML::Node> oldColors  = entriesBy<int>(child(ch, "colors"),  byChannel);
    QMap<int,YAML::Node> oldOffsets = entriesBy<int>(child(ch, "offsets"), byChannel);

    YAML::Node colors(YAML::NodeType::Sequence);
    YAML::Node offsets(YAML::NodeType::Sequence);

    for (const auto& cc : colorList) {
        YAML::Node c = oldColors.contains(cc.getId()) ? oldColors.take(cc.getId())
                                                      : YAML::Node(YAML::NodeType::Map);
        c["channel"]      = cc.getId();
        c["color"]        = cc.getColor().name().toStdString();
        c["anatomyColor"] = cc.getGroupColor().name().toStdString();
        c["spikeColor"]   = cc.getSpikeGroupColor().name().toStdString();
        colors.push_back(c);

        YAML::Node o = oldOffsets.contains(cc.getId()) ? oldOffsets.take(cc.getId())
                                                       : YAML::Node(YAML::NodeType::Map);
        o["channel"]       = cc.getId();
        o["defaultOffset"] = channelDefaultOffsets.value(cc.getId(), 0);
        offsets.push_back(o);
    }

    ch["colors"]  = colors;
    ch["offsets"] = offsets;
}

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setProgramsInformation(
        const QList<ProgramInformation>& programs)
{
    // ndmanager owns the program list (a program it no longer lists was
    // removed there); a program or parameter it still lists keeps any key
    // ndmanager does not model.
    if (programs.isEmpty()) {
        doc.remove("programs");
        return;
    }
    auto byName = [](const YAML::Node& e, bool& ok) {
        const YAML::Node x = child(e, "name");
        ok = x && x.IsScalar();
        return ok ? QString::fromStdString(x.Scalar()) : QString();
    };
    QMap<QString,YAML::Node> oldPrograms = entriesBy<QString>(child(doc, "programs"), byName);

    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& pi : programs) {
        const QString name = pi.getProgramName();
        YAML::Node prog = oldPrograms.contains(name) ? oldPrograms.take(name)
                                                     : YAML::Node(YAML::NodeType::Map);
        prog["name"] = name.toStdString();
        QMap<QString,YAML::Node> oldParams = entriesBy<QString>(child(prog, "parameters"), byName);

        const auto& params = pi.getParameterInformation();
        if (!params.isEmpty()) {
            YAML::Node paramSeq(YAML::NodeType::Sequence);
            for (auto it = params.cbegin(); it != params.cend(); ++it) {
                const QStringList& row = it.value();
                if (row.size() < 3) continue;
                YAML::Node p = oldParams.contains(row[0]) ? oldParams.take(row[0])
                                                          : YAML::Node(YAML::NodeType::Map);
                p["name"] = row[0].toStdString();
                // An empty value is left out rather than written as null: an
                // absent key is "unset" to every reader (ParameterYamlReader,
                // the ndm_* scripts' yaml_read, PyYAML) without depending on
                // each one treating ~ as unset, and the file says nothing it
                // does not mean.
                bool ok;
                int iv = row[1].toInt(&ok);
                if (ok) { p["value"] = iv; }
                else {
                    double dv = row[1].toDouble(&ok);
                    if (ok) { p["value"] = dv; }
                    else if (!row[1].isEmpty()) { p["value"] = row[1].toStdString(); }
                    else { p.remove("value"); }
                }
                p["status"] = row[2].toStdString();
                paramSeq.push_back(p);
            }
            prog["parameters"] = paramSeq;
        } else {
            prog.remove("parameters");
        }

        const QString help = pi.getHelp();
        if (!help.isEmpty())
            prog["help"] = help.toStdString();
        else
            prog.remove("help");

        seq.push_back(prog);
    }
    doc["programs"] = seq;
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setUnitsInformation(const QMap<int,QStringList>& units)
{
    // A unit ndmanager still lists keeps any key it does not model; units are
    // identified by (group, cluster).
    QMap<QPair<int,int>,YAML::Node> old = entriesBy<QPair<int,int>>(child(doc, "units"),
        [](const YAML::Node& e, bool& ok) {
            bool okG = false, okC = false;
            const int g = scalarInt(child(e, "group"), okG);
            const int c = scalarInt(child(e, "cluster"), okC);
            ok = okG && okC;
            return qMakePair(g, c);
        });
    YAML::Node seq(YAML::NodeType::Sequence);
    for (auto it = units.cbegin(); it != units.cend(); ++it) {
        const QStringList& row = it.value();
        if (row.size() < 7) continue;
        const QPair<int,int> id = qMakePair(row[0].toInt(), row[1].toInt());
        YAML::Node u = old.contains(id) ? old.take(id) : YAML::Node(YAML::NodeType::Map);
        u["group"]             = row[0].toInt();
        u["cluster"]           = row[1].toInt();
        u["structure"]         = strNode(row[2]);
        u["type"]              = strNode(row[3]);
        u["isolationDistance"] = strNode(row[4]);
        u["quality"]           = strNode(row[5]);
        u["notes"]             = strNode(row[6]);
        seq.push_back(u);
    }
    doc["units"] = seq;
}

// ---------------------------------------------------------------------------
// setProbesInformation
// ---------------------------------------------------------------------------

void ParameterYamlWriter::setProbesInformation(const QList<ProbeEntry>& probes,
                                               const QString& libraryPath)
{
    // writeProbesSection handles the empty list, the libraryPath field and
    // carrying the base document's probe entries.
    writeProbesSection(doc, probes, libraryPath);
}
