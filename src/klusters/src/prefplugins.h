// prefplugins.h -- Preferences > Plugins: one page per installed plugin descriptor, generated from
// it (PluginParamForm), holding the DEFAULTS the run dialog opens with (PluginSettings); and the
// group's overview page.  PrefDialog builds the pages from the registry at construction.
#pragma once

#include "pluginregistry.h"

#include <QWidget>
#include <QList>

class PluginParamForm;
class QLabel;

/** One plugin's page: descriptor facts, the parameter form, "Reset to descriptor defaults". */
class PrefPluginPage : public QWidget {
    Q_OBJECT
public:
    explicit PrefPluginPage(const KlustersPlugin& plugin, QWidget* parent = nullptr);
    const KlustersPlugin& plugin() const { return mPlugin; }
    /** Load the stored defaults (PluginSettings::values). */
    void updateFromSettings();
    /** Store the form (PluginSettings::store). */
    void commitToSettings() const;
    /** Back to the descriptor's values (in the form only; commit stores). */
    void restoreDescriptorDefaults();

Q_SIGNALS:
    void changed();

private:
    KlustersPlugin mPlugin;
    PluginParamForm* mForm = nullptr;
    QLabel* mState = nullptr;
};

/** The group's overview: where descriptors are looked for, how many were found. */
QWidget* makePluginsOverviewPage(const QList<KlustersPlugin>& plugins, QWidget* parent);
