/***************************************************************************
 * sessionyamlwriter.cpp  –  YAML session serialiser for NeuroScope
 ***************************************************************************/
#include "sessionyamlwriter.h"

#include <QFile>
#include <QTextStream>

// ── helpers ──────────────────────────────────────────────────────────────
static QString escape(const QString& s)
{
    // Wrap in double quotes and escape embedded double quotes.
    QString e = s;
    e.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + e + QLatin1Char('"');
}

static void writeIntList(QTextStream& o, const QList<int>& ids, int indent)
{
    QString pad(indent, QLatin1Char(' '));
    if (ids.isEmpty()) { o << " []\n"; return; }
    o << '\n';
    for (int id : ids)
        o << pad << "- " << id << '\n';
}

static void writeFileUrlMap(QTextStream& o,
                            const QMap<QString, QList<int>>& m,
                            const QString& tag,
                            const QString& itemTag,
                            int indent)
{
    QString pad(indent, QLatin1Char(' '));
    if (m.isEmpty()) { o << pad << tag << ": []\n"; return; }
    o << pad << tag << ":\n";
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        o << pad << "  - fileUrl: " << escape(it.key()) << '\n';
        o << pad << "    " << itemTag << ":";
        writeIntList(o, it.value(), indent + 4);
    }
}

// ── public API ────────────────────────────────────────────────────────────
void SessionYamlWriter::setLoadedFilesInformation(const QList<SessionFile>& fileList)
{
    m_files = fileList;
}

void SessionYamlWriter::setDisplayInformation(const QList<DisplayInformation>& displayList)
{
    m_displays = displayList;
}

bool SessionYamlWriter::writeTofile(const QString& url)
{
    QFile f(url);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream o(&f);

    o << "neuroscope_session:\n";
    o << "  version: \"3.0.0\"\n";

    // ── files ─────────────────────────────────────────────────────────────
    if (m_files.isEmpty()) {
        o << "  files: []\n";
    } else {
        o << "  files:\n";
        for (const SessionFile& sf : m_files) {
            o << "    - type: "  << static_cast<int>(sf.getType()) << '\n';
            o << "      url: "   << escape(sf.getUrl().toString())  << '\n';
            o << "      date: "  << escape(sf.getModification().toString(Qt::ISODate)) << '\n';

            const auto& colors = sf.getItemColors();
            if (colors.isEmpty()) {
                o << "      items: []\n";
            } else {
                o << "      items:\n";
                for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
                    o << "        - id: "    << escape(it.key())           << '\n';
                    o << "          color: " << escape(it.value().name()) << '\n';
                }
            }
        }
    }

    // ── displays ──────────────────────────────────────────────────────────
    if (m_displays.isEmpty()) {
        o << "  displays: []\n";
    } else {
        o << "  displays:\n";
        for (const DisplayInformation& di : m_displays) {
            o << "    - tabLabel: "           << escape(di.getTabLabel()) << '\n';
            o << "      showLabels: "         << di.getLabelStatus()      << '\n';
            o << "      startTime: "          << di.getStartTime()        << '\n';
            o << "      duration: "           << di.getTimeWindow()       << '\n';
            o << "      multipleColumns: "    << static_cast<int>(di.getMode()) << '\n';
            o << "      greyScale: "          << di.getGreyScale()        << '\n';
            o << "      autocenterChannels: " << (di.getAutocenterChannels() ? 1 : 0) << '\n';
            o << "      positionView: "       << di.isAPositionView()     << '\n';
            o << "      showEvents: "         << di.isEventsDisplayedInPositionView() << '\n';
            o << "      rasterHeight: "       << di.getRasterHeight()     << '\n';

            // spike presentation types
            const auto& spts = di.getSpikeDisplayTypes();
            o << "      spikePresentations:";
            if (spts.isEmpty()) { o << " []\n"; }
            else {
                o << " [";
                for (int i = 0; i < spts.size(); ++i) {
                    if (i) o << ", ";
                    o << static_cast<int>(spts[i]);
                }
                o << "]\n";
            }

            writeFileUrlMap(o, di.getSelectedClusters(), "      clustersSelected", "clusters",  6);
            writeFileUrlMap(o, di.getSelectedEvents(),   "      eventsSelected",   "events",    6);
            writeFileUrlMap(o, di.getSkippedClusters(),  "      clustersSkipped",  "clusters",  6);
            writeFileUrlMap(o, di.getSkippedEvents(),    "      eventsSkipped",    "events",    6);

            // spike files (just URLs)
            const auto& spkFiles = di.getSelectedSpikeFiles();
            if (spkFiles.isEmpty()) {
                o << "      spikesSelected: []\n";
            } else {
                o << "      spikesSelected:\n";
                for (const QString& u : spkFiles)
                    o << "        - fileUrl: " << escape(u) << '\n';
            }

            // channel positions (gain + offset per channel)
            const auto& positions = di.getPositions();
            if (positions.isEmpty()) {
                o << "      channelPositions: []\n";
            } else {
                o << "      channelPositions:\n";
                for (const TracePosition& tp : positions) {
                    o << "        - channel: " << tp.getId()     << '\n';
                    o << "          gain: "    << tp.getGain()   << '\n';
                    o << "          offset: "  << tp.getOffset() << '\n';
                }
            }

            // channel id lists
            o << "      channelsSelected:";
            writeIntList(o, di.getSelectedChannelIds(), 8);
            o << "      channelsShown:";
            writeIntList(o, di.getChannelIds(), 8);
        }
    }

    f.close();
    return true;
}
