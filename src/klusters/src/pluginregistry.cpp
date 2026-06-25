#include "pluginregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <algorithm>

QStringList PluginRegistry::discoveryDirs()
{
    QStringList dirs;
    const QString rel = QStringLiteral("klusters/plugins/descriptions");

    // Standard per-user app data (covers $XDG_DATA_HOME/klusters/...).
    for (const QString& base : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation))
        dirs << base + QStringLiteral("/plugins/descriptions");
    // Generic data dirs (covers the install share/klusters/... on most layouts).
    for (const QString& base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        dirs << base + QLatin1Char('/') + rel;
    // App-relative install fallback (<bindir>/../share/klusters/...).
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/") + rel;
    // Explicit override(s): $KLUSTERS_PLUGIN_PATH, listSeparator-delimited.
    const QString env = qEnvironmentVariable("KLUSTERS_PLUGIN_PATH");
    for (const QString& d : env.split(QDir::listSeparator(), Qt::SkipEmptyParts))
        dirs << d;

    dirs.removeDuplicates();
    return dirs;
}

KlustersPlugin PluginRegistry::parseDescriptor(const QString& path)
{
    KlustersPlugin p;
    p.descriptorPath = path;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return p;

    QXmlStreamReader xml(&f);
    bool inParameters = false;
    bool inParam = false;
    PluginParameter cur;

    while (!xml.atEnd() && !xml.hasError()) {
        const QXmlStreamReader::TokenType tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QStringView nm = xml.name();
            if (nm == u"parameters") {
                inParameters = true;
            } else if (nm == u"parameter" && inParameters) {
                inParam = true;
                cur = PluginParameter();
            } else if (nm == u"n") {
                const QString t = xml.readElementText();
                if (inParam)             cur.name = t;
                else if (p.name.isEmpty()) p.name = t;   // program-level name (first <n> before <parameters>)
            } else if (nm == u"value" && inParam) {
                cur.value = xml.readElementText();
            } else if (nm == u"status" && inParam) {
                cur.status = xml.readElementText();
            } else if (nm == u"help") {
                p.help = xml.readElementText().trimmed();
            } else if (nm == u"kind") {
                p.kind = xml.readElementText().trimmed();
            } else if (nm == u"consumes") {
                p.consumes = xml.readElementText().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            } else if (nm == u"produces") {
                p.produces = xml.readElementText().trimmed();
            } else if (nm == u"integration") {
                p.integration = xml.readElementText().trimmed();
            } else if (nm == u"selection") {
                p.selection = xml.readElementText().trimmed();
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const QStringView nm = xml.name();
            if (nm == u"parameters") {
                inParameters = false;
            } else if (nm == u"parameter" && inParam) {
                inParam = false;
                if (!cur.name.isEmpty())
                    p.parameters.append(cur);
            }
        }
    }

    p.valid = !xml.hasError() && !p.name.isEmpty();
    return p;
}

void PluginRegistry::reload()
{
    mPlugins.clear();
    const QStringList dirs = discoveryDirs();
    for (const QString& d : dirs) {
        QDir dir(d);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList(QStringList{QStringLiteral("*.xml")},
                                                QDir::Files, QDir::Name);
        for (const QString& f : files) {
            const KlustersPlugin p = parseDescriptor(dir.filePath(f));
            if (!p.valid)
                continue;
            // Later dir wins on a name collision.
            for (int i = mPlugins.size() - 1; i >= 0; --i)
                if (mPlugins[i].name == p.name)
                    mPlugins.removeAt(i);
            mPlugins.append(p);
        }
    }
    std::sort(mPlugins.begin(), mPlugins.end(),
              [](const KlustersPlugin& a, const KlustersPlugin& b) { return a.name < b.name; });
}
