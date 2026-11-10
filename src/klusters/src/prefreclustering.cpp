#include "prefreclustering.h"
#include "pluginsettings.h"
#include "configuration.h"

#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const char* kKlustaKwik = "klustakwik";
const char* kFiberKit   = "fiber-kit";
const char* kFiberKitPlugin = "fiber-kit-recluster";
}

PrefReclustering::PrefReclustering(QWidget* parent)
    : PrefReclusteringLayout(parent)
{
    connect(reclusteringExecutableButton, &QAbstractButton::clicked,
            this, &PrefReclustering::updateReclusteringExecutable);
    reclusteringExecutableButton->setIcon(QIcon(":/shared-icons/folder-open"));

    // ── The engine, at the top ──
    engineBox = new QGroupBox(tr("Engine — what Recluster (Shift+R) runs"), this);
    QVBoxLayout* el = new QVBoxLayout(engineBox);
    QHBoxLayout* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Recluster the selection with:"), engineBox));
    engineCombo = new QComboBox(engineBox);
    engineCombo->setToolTip(tr("The same choice as Actions > Recluster > Engine and the radial menu's Recluster > Plugin ring.  "
                               "Shift+M / Shift+C always run KlustaKwik on other features; Shift+Q always runs fiber-kit."));
    row->addWidget(engineCombo, 1);
    engineConfigure = new QPushButton(tr("Parameters…"), engineBox);
    engineConfigure->setToolTip(tr("Open this engine's page under Preferences > Plugins"));
    row->addWidget(engineConfigure);
    el->addLayout(row);
    engineInfo = new QLabel(engineBox);
    engineInfo->setWordWrap(true);
    engineInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    engineInfo->setStyleSheet(QStringLiteral("color: palette(mid);"));
    el->addWidget(engineInfo);
    topLayout->insertWidget(0, engineBox);

    // ── The KlustaKwik group is the layout's: retitle it as the engine's parameters ──
    groupBox2_2->setTitle(tr("KlustaKwik engine"));

    // ── fiber-kit without its descriptor: the fallback command ──
    fiberKitBox = new QGroupBox(tr("fiber-kit engine"), this);
    QVBoxLayout* fl = new QVBoxLayout(fiberKitBox);
    fiberKitNote = new QLabel(fiberKitBox);
    fiberKitNote->setWordWrap(true);
    fiberKitNote->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fl->addWidget(fiberKitNote);
    QHBoxLayout* frow = new QHBoxLayout();
    frow->addWidget(new QLabel(tr("Command"), fiberKitBox));
    fiberKitCommandEdit = new QLineEdit(fiberKitBox);
    fiberKitCommandEdit->setToolTip(tr("Run on the selection's sub-session when the fiber-kit-recluster plugin is not installed.  "
                                       "%fileBaseName = the sub-session's base name, %electrodeGroupID = the group; the final .clu "
                                       "must be written as %fileBaseName.clu.klusters.%electrodeGroupID."));
    frow->addWidget(fiberKitCommandEdit, 1);
    fiberKitDefault = new QPushButton(tr("Default"), fiberKitBox);
    frow->addWidget(fiberKitDefault);
    fl->addLayout(frow);
    topLayout->insertWidget(2, fiberKitBox);

    connect(engineCombo, &QComboBox::currentIndexChanged, this, [this](int) { refreshEngineView(); emit changed(); });
    connect(engineConfigure, &QPushButton::clicked, this, [this]() {
        const QString e = getReclusterEngine();
        if (e == QLatin1String(kFiberKit)) emit openPluginPage(QLatin1String(kFiberKitPlugin));
        else if (e.startsWith(QLatin1String("plugin:"))) emit openPluginPage(e.mid(7));
    });
    connect(fiberKitCommandEdit, &QLineEdit::textChanged, this, &PrefReclustering::changed);
    connect(fiberKitDefault, &QPushButton::clicked, this, [this]() {
        fiberKitCommandEdit->setText(configuration().getFiberKitReclusterCommandDefault());
    });

    setPlugins({});
}

const KlustersPlugin* PrefReclustering::pluginNamed(const QString& name) const
{
    for (const KlustersPlugin& p : mPlugins)
        if (p.name == name) return &p;
    return nullptr;
}

void PrefReclustering::setPlugins(const QList<KlustersPlugin>& plugins)
{
    mPlugins = plugins;
    const QString keep = engineCombo->count() ? getReclusterEngine() : QString();
    const QSignalBlocker block(engineCombo);
    engineCombo->clear();
    engineCombo->addItem(tr("KlustaKwik  (built in)"), QLatin1String(kKlustaKwik));
    engineCombo->addItem(tr("fiber-kit  (the plan on the selection's sub-session)"), QLatin1String(kFiberKit));
    for (const KlustersPlugin& p : mPlugins)
        if (p.runsOnSubsession() && p.name != QLatin1String(kFiberKitPlugin))
            engineCombo->addItem(p.refines() ? tr("%1  (refine: children of the selected units, parents kept)").arg(p.name)
                                             : tr("%1  (plugin)").arg(p.name), QStringLiteral("plugin:") + p.name);
    if (!keep.isEmpty()) setReclusterEngine(keep);
    refreshEngineView();
}

void PrefReclustering::setReclusterEngine(const QString& engine)
{
    int i = engineCombo->findData(engine);
    if (i < 0) i = 0;                                   // a plugin that is gone: KlustaKwik
    const QSignalBlocker block(engineCombo);
    engineCombo->setCurrentIndex(i);
    refreshEngineView();
}

QString PrefReclustering::getReclusterEngine() const
{
    return engineCombo->currentData().toString();
}

void PrefReclustering::refreshEngineView()
{
    const QString e = getReclusterEngine();
    const bool kk = (e == QLatin1String(kKlustaKwik));
    const bool fk = (e == QLatin1String(kFiberKit));
    const KlustersPlugin* fkPlugin = pluginNamed(QLatin1String(kFiberKitPlugin));
    const KlustersPlugin* plugin = e.startsWith(QLatin1String("plugin:")) ? pluginNamed(e.mid(7)) : (fk ? fkPlugin : nullptr);

    // what the chosen engine runs, and where its parameters are
    QString info;
    if (kk) {
        info = tr("KiloKlustaKwik on the selection's features (the temp .fet); its executable, arguments and the "
                  "feature-selection options are below.");
    } else if (plugin) {
        const QMap<QString, QString> vals = PluginSettings::values(*plugin);
        QStringList shown;
        for (const PluginParameter& p : plugin->parameters) {
            const QString v = vals.value(p.name);
            if (!v.isEmpty()) shown << QStringLiteral("%1 = %2").arg(p.name, v);
        }
        info = tr("Plugin \"%1\" on the selection's sub-session (%2).  Parameters%3: %4 — edit them on its page under "
                  "Preferences > Plugins (Parameters…).")
                   .arg(plugin->name,
                        plugin->command.isEmpty() ? plugin->name : plugin->command,
                        PluginSettings::overridden(*plugin) ? tr(" (stored defaults)") : tr(" (descriptor defaults)"),
                        shown.isEmpty() ? tr("none") : shown.join(QStringLiteral(", ")));
    } else if (fk) {
        info = tr("The fiber-kit-recluster plugin is NOT installed (fk install klusters-plugin), so Recluster runs the "
                  "fallback command below on the selection's sub-session.  With the plugin installed, its page under "
                  "Preferences > Plugins holds the configuration (the installed fiber-kit-klusters.yaml).");
    } else {
        info = tr("This plugin is not installed any more (Plugins > Reload Plugins); Recluster will run KlustaKwik.");
    }
    engineInfo->setText(info);
    engineConfigure->setEnabled(plugin != nullptr);
    engineConfigure->setText(plugin ? tr("Parameters of %1…").arg(plugin->name) : tr("Parameters…"));

    // the engine groups: the active one plain, the others marked inactive (still editable)
    groupBox2_2->setTitle(kk ? tr("KlustaKwik engine  (active)") : tr("KlustaKwik engine  (not the active engine; Shift+M / Shift+C still use it)"));
    fiberKitBox->setTitle(fk ? tr("fiber-kit engine  (active)") : tr("fiber-kit engine"));
    fiberKitBox->setVisible(fkPlugin == nullptr || fk);
    fiberKitCommandEdit->setEnabled(fkPlugin == nullptr);
    fiberKitDefault->setEnabled(fkPlugin == nullptr);
    fiberKitNote->setText(fkPlugin
        ? tr("Configured by the fiber-kit-recluster plugin (Preferences > Plugins): %1.  The command below is unused while the plugin is installed.")
              .arg(PluginSettings::values(*fkPlugin).value(QStringLiteral("config")).isEmpty()
                   ? tr("the session folder's fiber-kit.yaml")
                   : PluginSettings::values(*fkPlugin).value(QStringLiteral("config")))
        : tr("Without the plugin descriptor, the fallback command runs the plan of the session folder's fiber-kit.yaml."));
}

void PrefReclustering::setFiberKitReclusterCommand(const QString& command) { fiberKitCommandEdit->setText(command); }
QString PrefReclustering::getFiberKitReclusterCommand() const { return fiberKitCommandEdit->text(); }

void PrefReclustering::setReclusteringExecutable(const QString& e)  { reclusteringExecutableLineEdit->setText(e); }
void PrefReclustering::setReclusteringArguments(const QString& a)   { reclusteringArgsLineEdit->setText(a); }
void PrefReclustering::setAutoSelectFeatures(bool c)                { autoSelectFeaturesCheckBox->setChecked(c); }
void PrefReclustering::setAutoSelectNFeatures(int n)                { autoSelectNFeaturesSpinBox->setValue(n); }
void PrefReclustering::setReclusterMeanSubtractedSubdim(bool c)     { reclusterMeanSubtractedSubdimCheckBox->setChecked(c); }
void PrefReclustering::setReclusterChannelVariance(bool c)         { reclusterChannelVarianceCheckBox->setChecked(c); }
void PrefReclustering::setReclusterMedianWaveformResidual(bool c)  { reclusterMedianWaveformResidualCheckBox->setChecked(c); }
void PrefReclustering::setSuppressReclusterOutputTab(bool c)       { suppressReclusterOutputTabCheckBox->setChecked(c); }

QString PrefReclustering::getReclusteringExecutable() const    { return reclusteringExecutableLineEdit->text(); }
QString PrefReclustering::getReclusteringArguments() const     { return reclusteringArgsLineEdit->text(); }
bool    PrefReclustering::getAutoSelectFeatures() const        { return autoSelectFeaturesCheckBox->isChecked(); }
int     PrefReclustering::getAutoSelectNFeatures() const       { return autoSelectNFeaturesSpinBox->value(); }
bool    PrefReclustering::getReclusterMeanSubtractedSubdim() const { return reclusterMeanSubtractedSubdimCheckBox->isChecked(); }
bool    PrefReclustering::getReclusterChannelVariance() const     { return reclusterChannelVarianceCheckBox->isChecked(); }
bool    PrefReclustering::getReclusterMedianWaveformResidual() const { return reclusterMedianWaveformResidualCheckBox->isChecked(); }
bool    PrefReclustering::getSuppressReclusterOutputTab() const   { return suppressReclusterOutputTabCheckBox->isChecked(); }

void PrefReclustering::updateReclusteringExecutable()
{
    const QString executable = QFileDialog::getOpenFileName(this, tr("Select the Reclustering executable…"));
    if (!executable.isEmpty()) setReclusteringExecutable(executable);
}
