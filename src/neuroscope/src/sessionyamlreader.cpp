/***************************************************************************
 * sessionyamlreader.cpp  –  YAML session parser for NeuroScope
 *
 * Uses yaml-cpp (already a project dependency) to read the .nrs session
 * file produced by SessionYamlWriter.
 ***************************************************************************/
#include "sessionyamlreader.h"

#include <yaml-cpp/yaml.h>

#include <QColor>
#include <QDateTime>
#include <QUrl>

// ── helper: safely get a scalar with a default ────────────────────────────
template<typename T>
static T safeGet(const YAML::Node& n, const char* key, T def = T{})
{
    try {
        if (n[key]) return n[key].as<T>();
    } catch (...) {}
    return def;
}

// ── helper: integer list from a YAML sequence node ────────────────────────
static QList<int> intList(const YAML::Node& seq)
{
    QList<int> r;
    if (!seq || !seq.IsSequence()) return r;
    for (const auto& v : seq)
        try { r << v.as<int>(); } catch (...) {}
    return r;
}

// ── helper: map<QString, QList<int>> from a YAML sequence with
//           "fileUrl" / itemTag fields ──────────────────────────────────────
static QMap<QString,QList<int>> fileUrlMap(const YAML::Node& seq,
                                           const char* itemTag)
{
    QMap<QString,QList<int>> m;
    if (!seq || !seq.IsSequence()) return m;
    for (const auto& entry : seq) {
        QString url = QString::fromStdString(safeGet<std::string>(entry,"fileUrl",""));
        if (url.isEmpty()) continue;
        QList<int> ids;
        if (entry[itemTag]) ids = intList(entry[itemTag]);
        m.insert(url, ids);
    }
    return m;
}

// ─────────────────────────────────────────────────────────────────────────
bool SessionYamlReader::parseFile(const QString& path)
{
    files.clear();
    displays.clear();

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.toStdString());
    } catch (...) {
        return false;
    }

    YAML::Node sess = root["neuroscope_session"];
    if (!sess) return false;

    m_version = QString::fromStdString(safeGet<std::string>(sess,"version",""));

    // ── files ─────────────────────────────────────────────────────────────
    YAML::Node filesNode = sess["files"];
    if (filesNode && filesNode.IsSequence()) {
        for (const auto& f : filesNode) {
            SessionFile sf;
            sf.setType(static_cast<SessionFile::type>(safeGet<int>(f,"type",0)));
            sf.setUrl(QUrl(QString::fromStdString(safeGet<std::string>(f,"url",""))));

            QString dateStr = QString::fromStdString(safeGet<std::string>(f,"date",""));
            if (!dateStr.isEmpty())
                sf.setModification(QDateTime::fromString(dateStr, Qt::ISODate));

            YAML::Node items = f["items"];
            if (items && items.IsSequence()) {
                for (const auto& item : items) {
                    QString id    = QString::fromStdString(safeGet<std::string>(item,"id",""));
                    QString color = QString::fromStdString(safeGet<std::string>(item,"color",""));
                    if (!id.isEmpty())
                        sf.setItemColor(id, color);
                }
            }
            files << sf;
        }
    }

    // ── displays ──────────────────────────────────────────────────────────
    YAML::Node disps = sess["displays"];
    if (disps && disps.IsSequence()) {
        for (const auto& d : disps) {
            DisplayInformation di;
            di.setTabLabel(QString::fromStdString(safeGet<std::string>(d,"tabLabel","")));
            di.setLabelStatus(safeGet<int>(d,"showLabels",0));
            di.setStartTime(safeGet<long>(d,"startTime",0));
            di.setTimeWindow(safeGet<long>(d,"duration",50));
            di.setMode(static_cast<DisplayInformation::mode>(safeGet<int>(d,"multipleColumns",0)));
            di.setGreyScale(safeGet<int>(d,"greyScale",0));
            di.setAutocenterChannels(safeGet<int>(d,"autocenterChannels",0) != 0);
            di.setPositionView(safeGet<int>(d,"positionView",0));
            di.setEventsInPositionView(safeGet<int>(d,"showEvents",0));
            di.setRasterHeight(safeGet<int>(d,"rasterHeight",-1));

            YAML::Node spts = d["spikePresentations"];
            if (spts && spts.IsSequence()) {
                for (const auto& v : spts)
                    try { di.addSpikeDisplayType(
                        static_cast<DisplayInformation::spikeDisplayType>(v.as<int>())); }
                    catch (...) {}
            }

            for (auto [file,ids] : fileUrlMap(d["clustersSelected"],"clusters").asKeyValueRange())
                di.setSelectedClusters(file, ids);
            for (auto [file,ids] : fileUrlMap(d["eventsSelected"],"events").asKeyValueRange())
                di.setSelectedEvents(file, ids);
            for (auto [file,ids] : fileUrlMap(d["clustersSkipped"],"clusters").asKeyValueRange())
                di.setSkippedClusters(file, ids);
            for (auto [file,ids] : fileUrlMap(d["eventsSkipped"],"events").asKeyValueRange())
                di.setSkippedEvents(file, ids);

            // spike files (URL-only list)
            YAML::Node spkNode = d["spikesSelected"];
            if (spkNode && spkNode.IsSequence()) {
                QList<QString> spkFiles;
                for (const auto& entry : spkNode)
                    if (entry["fileUrl"])
                        spkFiles << QString::fromStdString(entry["fileUrl"].as<std::string>());
                di.setSelectedSpikeFiles(spkFiles);
            }

            // channel positions
            YAML::Node posNode = d["channelPositions"];
            if (posNode && posNode.IsSequence()) {
                QList<TracePosition> positions;
                for (const auto& p : posNode) {
                    TracePosition tp;
                    tp.setId(safeGet<int>(p,"channel",0));
                    tp.setGain(safeGet<int>(p,"gain",1));
                    tp.setOffset(safeGet<int>(p,"offset",0));
                    positions << tp;
                }
                di.setPositions(positions);
            }

            di.setSelectedChannelIds(intList(d["channelsSelected"]));
            di.setChannelIds(intList(d["channelsShown"]));

            displays << di;
        }
    }

    return true;
}
