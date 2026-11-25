// pluginparamform.h -- a form of typed editors for a plugin descriptor's parameters, generated
// from the descriptor (PluginParameter::effectiveType picks the widget: check box, spin box, double
// spin box, combo box, path with Browse, line edit).  Used by Preferences > Plugins (the stored
// defaults) and by the run dialog (PluginDialog), so both show the same controls.
#pragma once

#include "pluginregistry.h"

#include <QWidget>
#include <QMap>

class PluginParamForm : public QWidget {
    Q_OBJECT
public:
    explicit PluginParamForm(const KlustersPlugin& plugin, QWidget* parent = nullptr);
    /** Set every editor from @p values (parameter name -> text); unknown names are ignored. */
    void setValues(const QMap<QString, QString>& values);
    /** Every parameter's current text (a Bool as true/false). */
    QMap<QString, QString> values() const;
    const KlustersPlugin& plugin() const { return mPlugin; }

Q_SIGNALS:
    void changed();

private:
    KlustersPlugin mPlugin;
    QMap<QString, QWidget*> mEditors;
};
