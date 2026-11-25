#include "pluginsettings.h"

#include <QSettings>

static QSettings* g_testSettings = nullptr;

void PluginSettings::setSettingsForTests(QSettings* s) { g_testSettings = s; }

QSettings* PluginSettings::settings()
{
    static QSettings* app = nullptr;
    if (g_testSettings) return g_testSettings;
    if (!app) app = new QSettings();
    return app;
}

QString PluginSettings::groupOf(const QString& pluginName)
{
    QString n = pluginName;
    n.replace(QLatin1Char('/'), QLatin1String("%2F"));
    return QStringLiteral("Plugins/") + n;
}

QMap<QString, QString> PluginSettings::stored(const QString& pluginName)
{
    QMap<QString, QString> out;
    QSettings* s = settings();
    s->beginGroup(groupOf(pluginName));
    for (const QString& k : s->childKeys())
        out.insert(k, s->value(k).toString());
    s->endGroup();
    return out;
}

QMap<QString, QString> PluginSettings::values(const KlustersPlugin& plugin)
{
    const QMap<QString, QString> over = stored(plugin.name);
    QMap<QString, QString> out;
    for (const PluginParameter& p : plugin.parameters)
        out.insert(p.name, over.contains(p.name) ? over.value(p.name) : p.value);
    return out;
}

void PluginSettings::store(const KlustersPlugin& plugin, const QMap<QString, QString>& values)
{
    QSettings* s = settings();
    s->beginGroup(groupOf(plugin.name));
    s->remove(QString());                       // the whole group: stale keys go
    for (const PluginParameter& p : plugin.parameters) {
        if (!values.contains(p.name)) continue;
        const QString v = values.value(p.name);
        if (v != p.value) s->setValue(p.name, v);
    }
    s->endGroup();
    s->sync();
}

void PluginSettings::reset(const QString& pluginName)
{
    QSettings* s = settings();
    s->beginGroup(groupOf(pluginName));
    s->remove(QString());
    s->endGroup();
    s->sync();
}

bool PluginSettings::overridden(const KlustersPlugin& plugin)
{
    const QMap<QString, QString> over = stored(plugin.name);
    for (const PluginParameter& p : plugin.parameters)
        if (over.contains(p.name) && over.value(p.name) != p.value) return true;
    return false;
}
