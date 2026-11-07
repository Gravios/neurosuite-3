// pluginsettings.h -- the per-plugin parameter defaults a user sets in Preferences > Plugins
// (QtCore only, tested).  A descriptor (PluginRegistry) declares its parameters with static
// defaults; this store overlays the user's values on them, per plugin name, in the application's
// QSettings under Plugins/<name>/<parameter>.  The run dialog (PluginDialog) prefills from the
// overlay; a parameter a descriptor no longer declares is dropped on the next save.
#pragma once

#include "pluginregistry.h"

#include <QMap>
#include <QString>

class QSettings;

class PluginSettings {
public:
    /** The effective defaults of @p plugin: the descriptor's values, overlaid with the stored ones
     *  (parameters in descriptor order; only declared parameters). */
    static QMap<QString, QString> values(const KlustersPlugin& plugin);
    /** Only what is stored for @p pluginName (no descriptor overlay). */
    static QMap<QString, QString> stored(const QString& pluginName);
    /** Store @p values for @p plugin: a value equal to the descriptor's default is NOT stored (so a
     *  later change of the descriptor's default shows through), any other key of the plugin's group
     *  is removed. */
    static void store(const KlustersPlugin& plugin, const QMap<QString, QString>& values);
    /** Forget every stored value of @p pluginName (back to the descriptor's defaults). */
    static void reset(const QString& pluginName);
    /** True when at least one parameter of @p plugin is overridden. */
    static bool overridden(const KlustersPlugin& plugin);

    /** The settings group of a plugin; the name is escaped so that '/' in it cannot nest groups. */
    static QString groupOf(const QString& pluginName);

    /** For tests: point the store at a settings object instead of the application's. */
    static void setSettingsForTests(QSettings* s);

private:
    static QSettings* settings();
};
