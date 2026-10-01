#ifndef PLUGINDIALOG_H
#define PLUGINDIALOG_H

// Parameter form generated from a plugin descriptor (see docs/PLUGIN_API.md).
// Pure UI: one editable field per descriptor parameter, prefilled with the value
// Klusters resolved from the open document (e.g. nsamp / nchan) when one matches
// the parameter name, else the descriptor default.  No process machinery — the
// caller takes values() and builds the invocation (PluginRegistry::buildArgv).

#include <QDialog>
#include <QMap>
#include "pluginregistry.h"

class QLineEdit;

class PluginDialog : public QDialog {
    Q_OBJECT
public:
    /** @p context maps resolved tokens (base/group/variant/tag/nsamp/nchan) to
     *  values; a parameter whose name matches a non-empty context key is prefilled
     *  from it rather than from the descriptor default. */
    explicit PluginDialog(const KlustersPlugin& plugin,
                          const QMap<QString, QString>& context = {},
                          QWidget* parent = nullptr);
    /** Parameter name -> the value the user entered (defaults preserved). */
    QMap<QString, QString> values() const;

private:
    QMap<QString, QLineEdit*> mEdits;
};

#endif // PLUGINDIALOG_H
