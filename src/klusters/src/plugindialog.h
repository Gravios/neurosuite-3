#ifndef PLUGINDIALOG_H
#define PLUGINDIALOG_H

// Parameter form generated from a plugin descriptor (see docs/PLUGIN_API.md).
// Pure UI: one typed editor per descriptor parameter (PluginParamForm), prefilled
// with -- in this order -- the value Klusters resolved from the open document (e.g.
// nsamp / nchan) when one matches the parameter name, the user's stored default
// (Preferences > Plugins, PluginSettings), else the descriptor default.  No process
// machinery -- the caller takes values() and builds the invocation
// (PluginRegistry::buildArgv).  "Remember as defaults" stores the values.

#include <QDialog>
#include <QMap>
#include "pluginregistry.h"

class PluginParamForm;
class QCheckBox;

class PluginDialog : public QDialog {
    Q_OBJECT
public:
    /** @p context maps resolved tokens (base/group/variant/tag/nsamp/nchan) to
     *  values; a parameter whose name matches a non-empty context key is prefilled
     *  from it rather than from the stored or descriptor default. */
    explicit PluginDialog(const KlustersPlugin& plugin,
                          const QMap<QString, QString>& context = {},
                          QWidget* parent = nullptr);
    /** Parameter name -> the value the user entered (defaults preserved). */
    QMap<QString, QString> values() const;
    /** True when the user asked to keep these values as the plugin's defaults. */
    bool rememberAsDefaults() const;

private:
    PluginParamForm* mForm = nullptr;
    QCheckBox* mRemember = nullptr;
    KlustersPlugin mPlugin;
};

#endif // PLUGINDIALOG_H
