#include "pluginparamform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

#include <cmath>
#include <limits>

PluginParamForm::PluginParamForm(const KlustersPlugin& plugin, QWidget* parent)
    : QWidget(parent), mPlugin(plugin)
{
    QFormLayout* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    for (const PluginParameter& p : mPlugin.parameters) {
        QWidget* editor = nullptr;
        switch (p.effectiveType()) {
        case PluginParameter::Type::Bool: {
            QCheckBox* cb = new QCheckBox(this);
            connect(cb, &QCheckBox::toggled, this, &PluginParamForm::changed);
            editor = cb; break;
        }
        case PluginParameter::Type::Int: {
            QSpinBox* sb = new QSpinBox(this);
            bool okMin = false, okMax = false;
            const int lo = p.min.toInt(&okMin), hi = p.max.toInt(&okMax);
            sb->setRange(okMin ? lo : std::numeric_limits<int>::min(), okMax ? hi : std::numeric_limits<int>::max());
            if (!p.step.isEmpty()) sb->setSingleStep(qMax(1, p.step.toInt()));
            connect(sb, &QSpinBox::valueChanged, this, &PluginParamForm::changed);
            editor = sb; break;
        }
        case PluginParameter::Type::Real: {
            QDoubleSpinBox* sb = new QDoubleSpinBox(this);
            bool okMin = false, okMax = false;
            const double lo = p.min.toDouble(&okMin), hi = p.max.toDouble(&okMax);
            sb->setRange(okMin ? lo : -1e12, okMax ? hi : 1e12);
            sb->setDecimals(qMax(p.decimals(), p.step.isEmpty() ? 1 : PluginParameter{QString(), p.step}.decimals()));
            if (!p.step.isEmpty()) sb->setSingleStep(p.step.toDouble());
            else sb->setSingleStep(std::pow(10.0, -p.decimals()));
            connect(sb, &QDoubleSpinBox::valueChanged, this, &PluginParamForm::changed);
            editor = sb; break;
        }
        case PluginParameter::Type::Choice: {
            QComboBox* cb = new QComboBox(this);
            for (const QString& c : p.choices) cb->addItem(c.isEmpty() ? tr("(unset)") : c, c);
            if (cb->findData(p.value) < 0) cb->addItem(p.value.isEmpty() ? tr("(unset)") : p.value, p.value);
            connect(cb, &QComboBox::currentIndexChanged, this, &PluginParamForm::changed);
            editor = cb; break;
        }
        case PluginParameter::Type::Path: {
            QWidget* row = new QWidget(this);
            QHBoxLayout* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            QLineEdit* le = new QLineEdit(row);
            le->setObjectName(QStringLiteral("path"));
            QPushButton* browse = new QPushButton(tr("Browse…"), row);
            h->addWidget(le, 1); h->addWidget(browse);
            const bool dir = p.name.toLower().endsWith(QLatin1String("dir")) || p.name.toLower().endsWith(QLatin1String("directory"))
                             || p.name.toLower().endsWith(QLatin1String("folder"));
            connect(browse, &QPushButton::clicked, this, [this, le, dir, p]() {
                const QString f = dir ? QFileDialog::getExistingDirectory(this, p.name, le->text())
                                      : QFileDialog::getOpenFileName(this, p.name, le->text());
                if (!f.isEmpty()) le->setText(f);
            });
            connect(le, &QLineEdit::textChanged, this, &PluginParamForm::changed);
            editor = row; break;
        }
        case PluginParameter::Type::Text: {
            QLineEdit* le = new QLineEdit(this);
            connect(le, &QLineEdit::textChanged, this, &PluginParamForm::changed);
            editor = le; break;
        }
        }
        QString tip = p.help;
        if (!tip.isEmpty()) tip += QLatin1Char('\n');
        tip += p.mandatory() ? tr("required") : tr("optional");
        if (!p.value.isEmpty()) tip += tr("; descriptor default: %1").arg(p.value);
        editor->setToolTip(tip);
        QLabel* label = new QLabel(p.mandatory() ? (p.name + QStringLiteral(" *")) : p.name, this);
        label->setToolTip(tip);
        form->addRow(label, editor);
        mEditors.insert(p.name, editor);
    }
    setValues(QMap<QString, QString>());        // the descriptor defaults
}

static bool isTrueText(const QString& v)
{
    const QString l = v.trimmed().toLower();
    return l == QLatin1String("true") || l == QLatin1String("on") || l == QLatin1String("yes") || l == QLatin1String("1");
}

void PluginParamForm::setValues(const QMap<QString, QString>& values)
{
    for (const PluginParameter& p : mPlugin.parameters) {
        QWidget* w = mEditors.value(p.name);
        if (!w) continue;
        const QString v = values.value(p.name, p.value);
        const QSignalBlocker block(w);
        if (auto* cb = qobject_cast<QCheckBox*>(w)) cb->setChecked(isTrueText(v));
        else if (auto* sb = qobject_cast<QSpinBox*>(w)) sb->setValue(v.toInt());
        else if (auto* db = qobject_cast<QDoubleSpinBox*>(w)) db->setValue(v.toDouble());
        else if (auto* co = qobject_cast<QComboBox*>(w)) {
            int i = co->findData(v);
            if (i < 0) { co->addItem(v.isEmpty() ? tr("(unset)") : v, v); i = co->count() - 1; }
            co->setCurrentIndex(i);
        }
        else if (auto* le = qobject_cast<QLineEdit*>(w)) le->setText(v);
        else if (auto* pl = w->findChild<QLineEdit*>(QStringLiteral("path"))) { const QSignalBlocker b2(pl); pl->setText(v); }
    }
}

QMap<QString, QString> PluginParamForm::values() const
{
    QMap<QString, QString> out;
    for (const PluginParameter& p : mPlugin.parameters) {
        QWidget* w = mEditors.value(p.name);
        if (!w) continue;
        QString v;
        if (auto* cb = qobject_cast<QCheckBox*>(w)) v = cb->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
        else if (auto* sb = qobject_cast<QSpinBox*>(w)) v = QString::number(sb->value());
        else if (auto* db = qobject_cast<QDoubleSpinBox*>(w)) v = QString::number(db->value(), 'g', 12);
        else if (auto* co = qobject_cast<QComboBox*>(w)) v = co->currentData().toString();
        else if (auto* le = qobject_cast<QLineEdit*>(w)) v = le->text();
        else if (auto* pl = w->findChild<QLineEdit*>(QStringLiteral("path"))) v = pl->text();
        out.insert(p.name, v);
    }
    return out;
}
