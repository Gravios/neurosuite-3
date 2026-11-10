#include "prefplugins.h"
#include "pluginparamform.h"
#include "pluginsettings.h"

#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

PrefPluginPage::PrefPluginPage(const KlustersPlugin& plugin, QWidget* parent)
    : QWidget(parent), mPlugin(plugin)
{
    QVBoxLayout* top = new QVBoxLayout(this);

    // what the descriptor says
    QString facts;
    if (!mPlugin.kind.isEmpty()) facts += tr("<b>Kind:</b> %1 &nbsp; ").arg(mPlugin.kind.toHtmlEscaped());
    facts += tr("<b>Runs on:</b> %1 &nbsp; ").arg(mPlugin.runsOnSubsession()
              ? tr("the selection, as a sub-session (temp files; the result replaces the selection)")
              : tr("the session files"));
    if (!mPlugin.integration.isEmpty()) facts += tr("<b>Integration:</b> %1").arg(mPlugin.integration.toHtmlEscaped());
    QLabel* kind = new QLabel(facts, this);
    kind->setWordWrap(true);
    top->addWidget(kind);
    if (!mPlugin.help.isEmpty()) {
        QLabel* help = new QLabel(mPlugin.help.section(QLatin1Char('\n'), 0, 3), this);
        help->setWordWrap(true);
        help->setStyleSheet(QStringLiteral("color: palette(mid);"));
        top->addWidget(help);
    }
    const QString cmd = mPlugin.command.isEmpty()
        ? (mPlugin.name + (mPlugin.consumes.isEmpty() ? QString() : QLatin1Char(' ') + mPlugin.consumes.join(QLatin1Char(' '))) + QStringLiteral(" --<parameter> <value> …"))
        : mPlugin.command;
    QLabel* command = new QLabel(tr("<b>Command:</b> <code>%1</code><br><b>Descriptor:</b> %2")
                                 .arg(cmd.toHtmlEscaped(), QDir::toNativeSeparators(mPlugin.descriptorPath).toHtmlEscaped()), this);
    command->setWordWrap(true);
    command->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(command);

    QGroupBox* box = new QGroupBox(tr("Parameter defaults (what the run dialog opens with)"), this);
    QVBoxLayout* bl = new QVBoxLayout(box);
    if (mPlugin.parameters.isEmpty()) {
        bl->addWidget(new QLabel(tr("This plugin declares no parameters."), box));
    } else {
        mForm = new PluginParamForm(mPlugin, box);
        connect(mForm, &PluginParamForm::changed, this, &PrefPluginPage::changed);
        bl->addWidget(mForm);
    }
    QHBoxLayout* foot = new QHBoxLayout();
    mState = new QLabel(box);
    mState->setStyleSheet(QStringLiteral("color: palette(mid);"));
    foot->addWidget(mState, 1);
    QPushButton* reset = new QPushButton(tr("Reset to descriptor defaults"), box);
    reset->setEnabled(mForm != nullptr);
    connect(reset, &QPushButton::clicked, this, [this]() { restoreDescriptorDefaults(); emit changed(); });
    foot->addWidget(reset);
    bl->addLayout(foot);
    top->addWidget(box);
    top->addStretch(1);
}

void PrefPluginPage::updateFromSettings()
{
    if (mForm) mForm->setValues(PluginSettings::values(mPlugin));
    if (mState)
        mState->setText(PluginSettings::overridden(mPlugin) ? tr("Stored defaults differ from the descriptor's.")
                                                             : tr("Descriptor defaults."));
}

void PrefPluginPage::commitToSettings() const
{
    if (mForm) PluginSettings::store(mPlugin, mForm->values());
}

void PrefPluginPage::restoreDescriptorDefaults()
{
    if (mForm) mForm->setValues(QMap<QString, QString>());
    if (mState) mState->setText(tr("Descriptor defaults (not stored until Apply)."));
}

QWidget* makePluginsOverviewPage(const QList<KlustersPlugin>& plugins, QWidget* parent,
                                 const QList<KlustersPlugin>& broken)
{
    QString text = QObject::tr(
        "Plugins are external programs described by an XML descriptor (src/klusters/docs/PLUGIN_API.md). "
        "Each installed plugin has a page here with the DEFAULTS its parameters open with in the run dialog "
        "(Plugins menu); a <i>recluster</i> plugin runs on the selected clusters' spikes exported as a "
        "sub-session and its result replaces the selection, like a KlustaKwik recluster.<br><br>"
        "<b>%n plugin(s) found</b> in:<br>", "", plugins.size());
    for (const QString& d : PluginRegistry::discoveryDirs())
        text += QStringLiteral("&nbsp;&nbsp;%1%2<br>").arg(QDir::toNativeSeparators(d).toHtmlEscaped(),
                                                             QDir(d).exists() ? QString() : QObject::tr(" (absent)"));
    if (!broken.isEmpty()) {
        text += QObject::tr("<br><b>%n descriptor(s) NOT loaded</b> (fix the file, then <i>Plugins → Reload Plugins</i>):<br>", "", broken.size());
        for (const KlustersPlugin& b : broken)
            text += QStringLiteral("&nbsp;&nbsp;%1 — %2<br>").arg(QDir::toNativeSeparators(b.descriptorPath).toHtmlEscaped(),
                                                                 b.error.toHtmlEscaped());
    }
    text += QObject::tr("<br>$KLUSTERS_PLUGIN_PATH adds directories.  <i>Plugins → Reload Plugins</i> rescans; "
                        "reopen Preferences to see new pages.");
    QLabel* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
