/***************************************************************************
 *   prefreclustering.h  -  Preferences > Curation > Reclustering
 *
 *   The page is built around the recluster ENGINE -- what Recluster (Shift+R)
 *   runs: KlustaKwik (built in), fiber-kit (built in: the plan on the
 *   selection's sub-session), or any installed `recluster` plugin.  The engine
 *   is chosen at the top; its configuration follows:
 *     - KlustaKwik: the executable, its arguments and the feature-selection
 *       options (the layout's group box);
 *     - a plugin (fiber-kit's included, when its descriptor is installed): the
 *       parameters live on the plugin's own page under Preferences > Plugins,
 *       reached from here with one button; what the plugin runs is summarised;
 *     - fiber-kit without its descriptor: the fallback command.
 *   The group boxes of the engines not chosen stay editable but are marked
 *   inactive, so a user sees what Shift+R will actually run.
 ***************************************************************************/
#ifndef PREFRECLUSTERING_H
#define PREFRECLUSTERING_H

#include "prefreclusteringlayout.h"
#include "pluginregistry.h"

#include <QList>

class QComboBox;
class QLabel;
class QPushButton;
class QGroupBox;
class QLineEdit;

class PrefReclustering : public PrefReclusteringLayout
{
    Q_OBJECT
public:
    explicit PrefReclustering(QWidget* parent = nullptr);
    ~PrefReclustering() override = default;

    /** The installed plugins (for the engine list and the plugin summaries). */
    void setPlugins(const QList<KlustersPlugin>& plugins);

    // the engine
    void setReclusterEngine(const QString& engine);         // klustakwik | fiber-kit | plugin:<name>
    QString getReclusterEngine() const;
    void setFiberKitReclusterCommand(const QString& command);
    QString getFiberKitReclusterCommand() const;

    // KlustaKwik
    void setReclusteringExecutable(const QString& executable);
    void setReclusteringArguments(const QString& arguments);
    void setAutoSelectFeatures(bool checked);
    void setAutoSelectNFeatures(int n);
    void setReclusterMeanSubtractedSubdim(bool checked);
    void setReclusterChannelVariance(bool checked);
    void setReclusterMedianWaveformResidual(bool checked);
    void setSuppressReclusterOutputTab(bool checked);
    QString getReclusteringExecutable() const;
    QString getReclusteringArguments() const;
    bool    getAutoSelectFeatures() const;
    int     getAutoSelectNFeatures() const;
    bool    getReclusterMeanSubtractedSubdim() const;
    bool    getReclusterChannelVariance() const;
    bool    getReclusterMedianWaveformResidual() const;
    bool    getSuppressReclusterOutputTab() const;

Q_SIGNALS:
    /** The user wants the Preferences page of this plugin (its parameters). */
    void openPluginPage(const QString& pluginName);
    void changed();

private slots:
    void updateReclusteringExecutable();
    void refreshEngineView();

private:
    const KlustersPlugin* pluginNamed(const QString& name) const;
    QList<KlustersPlugin> mPlugins;
    // engine group
    QGroupBox*   engineBox = nullptr;
    QComboBox*   engineCombo = nullptr;
    QLabel*      engineInfo = nullptr;
    QPushButton* engineConfigure = nullptr;
    // fiber-kit fallback
    QGroupBox*   fiberKitBox = nullptr;
    QLineEdit*   fiberKitCommandEdit = nullptr;
    QLabel*      fiberKitNote = nullptr;
    QPushButton* fiberKitDefault = nullptr;
};

#endif
