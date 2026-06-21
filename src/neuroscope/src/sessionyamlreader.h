/***************************************************************************
 * sessionyamlreader.h
 *
 * Reads the NeuroScope YAML session file (.nrs) written by SessionYamlWriter.
 * Returns data through the same getter API used by loadSession() /
 * loadDocumentInformation() in NeuroscopeDoc so the call sites are trivial.
 ***************************************************************************/
#pragma once

#include "sessionInformation.h"

#include <QList>
#include <QMap>
#include <QString>

class SessionYamlReader
{
public:
    SessionYamlReader()  = default;
    ~SessionYamlReader() = default;

    /** Parse @p path. Returns true on success. */
    bool parseFile(const QString& path);
    void closeFile() {}

    QString version() const { return m_version; }

    QList<SessionFile>        getFilesToLoad()        const { return files;    }
    QList<DisplayInformation> getDisplayInformation() const { return displays; }

    // Stubs satisfying the loadSession<Reader> interface.
    // Rotation/flip are loaded from the parameter file, not the session file.
    // The 1.2.2 legacy background-image path never applies to YAML sessions.
    QString getVersion()        const { return m_version; }
    QString getBackgroundImage() const { return QString(); }
    int     getRotation()        const { return 0; }
    int     getFlip()            const { return 0; }

private:
    QString                   m_version;
    QList<SessionFile>        files;
    QList<DisplayInformation> displays;
};
