/***************************************************************************
 * sessionyamlwriter.h
 *
 * Writes the NeuroScope session state to a YAML file (.nrs).
 * Replaces the old SessionXmlWriter (QDom-based).
 *
 * YAML layout
 * ──────────────────────────────────────────────────────────────────────────
 * neuroscope_session:
 *   version: "3.0.0"
 *   files:
 *     - type: 0          # SessionFile::type int
 *       url: "/abs/path"
 *       date: "ISO-8601"
 *       items:
 *         - id: "label"
 *           color: "#rrggbb"
 *   displays:
 *     - tabLabel: "Display 1"
 *       showLabels: 0
 *       startTime: 0
 *       duration: 1000
 *       multipleColumns: 0
 *       greyScale: 0
 *       autocenterChannels: 0
 *       positionView: 0
 *       showEvents: 0
 *       rasterHeight: -1
 *       spikePresentations: [0, 1]
 *       clustersSelected:
 *         - fileUrl: "/abs/path/base.clu.1"
 *           clusters: [1, 2, 3]
 *       eventsSelected: []
 *       spikesSelected: []
 *       clustersSkipped: []
 *       eventsSkipped: []
 *       channelPositions:
 *         - channel: 0
 *           gain: 1
 *           offset: 0
 *       channelsSelected: [0, 1]
 *       channelsShown:    [0, 1, 2, 3]
 ***************************************************************************/
#pragma once

#include "sessionInformation.h"
#include <QList>

class SessionYamlWriter
{
public:
    SessionYamlWriter()  = default;
    ~SessionYamlWriter() = default;

    /** Store the list of files that were loaded during the session. */
    void setLoadedFilesInformation(const QList<SessionFile>& fileList);

    /** Store the per-display state. */
    void setDisplayInformation(const QList<DisplayInformation>& displayList);

    /** Serialise and write to @p url. Returns true on success. */
    bool writeTofile(const QString& url);

private:
    QList<SessionFile>        m_files;
    QList<DisplayInformation> m_displays;
};
