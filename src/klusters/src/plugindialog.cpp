#include "plugindialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

PluginDialog::PluginDialog(const KlustersPlugin& plugin, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Run plugin: %1").arg(plugin.name));

    QVBoxLayout* top = new QVBoxLayout(this);

    if (!plugin.help.isEmpty()) {
        QLabel* help = new QLabel(plugin.help.section(QLatin1Char('\n'), 0, 2), this);
        help->setWordWrap(true);
        help->setStyleSheet(QStringLiteral("color: palette(mid);"));
        top->addWidget(help);
    }

    QFormLayout* form = new QFormLayout();
    for (const PluginParameter& p : plugin.parameters) {
        QLineEdit* edit = new QLineEdit(p.value, this);
        const bool mandatory =
            (p.status.compare(QStringLiteral("Mandatory"), Qt::CaseInsensitive) == 0);
        edit->setToolTip(mandatory ? tr("required") : tr("optional"));
        mEdits.insert(p.name, edit);
        form->addRow(mandatory ? (p.name + QStringLiteral(" *")) : p.name, edit);
    }
    top->addLayout(form);

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
    QMap<QString, QString> out;
    for (auto it = mEdits.constBegin(); it != mEdits.constEnd(); ++it)
        out.insert(it.key(), it.value()->text());
    return out;
}
