#include "plugindialog.h"
#include "pluginparamform.h"
#include "pluginsettings.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>

PluginDialog::PluginDialog(const KlustersPlugin& plugin,
                           const QMap<QString, QString>& context, QWidget* parent)
    : QDialog(parent), mPlugin(plugin)
{
    setWindowTitle(tr("Run plugin: %1").arg(plugin.name));

    QVBoxLayout* top = new QVBoxLayout(this);

    if (!plugin.help.isEmpty()) {
        QLabel* help = new QLabel(plugin.help.section(QLatin1Char('\n'), 0, 2), this);
        help->setWordWrap(true);
        help->setStyleSheet(QStringLiteral("color: palette(mid);"));
        top->addWidget(help);
    }
    if (plugin.runsOnSubsession()) {
        QLabel* note = new QLabel(tr("Runs on the selected clusters' spikes (a sub-session); its clusters "
                                     "replace the selection, as a recluster does."), this);
        note->setWordWrap(true);
        top->addWidget(note);
    }

    // Prefill: the document's resolved value (keyed by the parameter name) wins over
    // the stored default, which wins over the descriptor's static default, so
    // geometry-dependent fields (nsamp / nchan) come in correct and editable.
    QMap<QString, QString> init = PluginSettings::values(plugin);
    for (const PluginParameter& p : plugin.parameters) {
        const QString ctxVal = context.value(p.name);
        if (!ctxVal.isEmpty()) init.insert(p.name, ctxVal);
    }
    mForm = new PluginParamForm(plugin, this);
    mForm->setValues(init);
    top->addWidget(mForm);

    mRemember = new QCheckBox(tr("Remember these values as the plugin's defaults (Preferences → Plugins)"), this);
    top->addWidget(mRemember);

    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(tr("Run"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    top->addWidget(buttons);
}

QMap<QString, QString> PluginDialog::values() const
{
    return mForm->values();
}

bool PluginDialog::rememberAsDefaults() const
{
    return mRemember && mRemember->isChecked();
}
