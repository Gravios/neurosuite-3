#ifndef PLUGINDIALOG_H
#define PLUGINDIALOG_H

// Parameter form generated from a plugin descriptor (see docs/PLUGIN_API.md).
// Pure UI: one editable field per descriptor parameter, prefilled with the
// descriptor default.  No process machinery — the caller takes values() and
// builds the invocation (PluginRegistry::buildArgv).

#include <QDialog>
#include <QMap>
#include "pluginregistry.h"

class QLineEdit;

class PluginDialog : public QDialog {
    Q_OBJECT
public:
    explicit PluginDialog(const KlustersPlugin& plugin, QWidget* parent = nullptr);
    /** Parameter name -> the value the user entered (defaults preserved). */
    QMap<QString, QString> values() const;

private:
    QMap<QString, QLineEdit*> mEdits;
};

#endif // PLUGINDIALOG_H
