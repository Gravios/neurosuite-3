// prefbinder.cpp -- see prefbinder.h.

#include "prefbinder.h"

#include "configuration.h"

#include <qcolorbutton.h>

#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>

namespace {

bool isFlag(const PrefSpec& s)   { return s.def.typeId() == QMetaType::Bool; }
bool isNumber(const PrefSpec& s) { return s.def.typeId() == QMetaType::Int || s.def.typeId() == QMetaType::Double; }

// Show @p v in the control(s) @p ws.  @p s gives the kind of value and what to fall back
// on when the value has no entry.
void show(const QList<QWidget*>& ws, const PrefSpec& s, const QVariant& v)
{
    QWidget* w = ws.first();
    if (ws.size() > 1) {                                             // radio buttons, in value order
        int index = v.toInt();
        if (index < 0 || index >= ws.size()) index = 0;
        if (QAbstractButton* b = qobject_cast<QAbstractButton*>(ws.at(index))) b->setChecked(true);
    }
    else if (QColorButton* col = qobject_cast<QColorButton*>(w)) col->setColor(v.value<QColor>());
    else if (QAbstractButton* b = qobject_cast<QAbstractButton*>(w)) b->setChecked(v.toBool());
    else if (QSpinBox* i = qobject_cast<QSpinBox*>(w)) i->setValue(v.toInt());
    else if (QDoubleSpinBox* d = qobject_cast<QDoubleSpinBox*>(w)) d->setValue(v.toDouble());
    else if (QComboBox* c = qobject_cast<QComboBox*>(w)) {
        int index = !s.choices.isEmpty() ? static_cast<int>(s.choices.indexOf(v))
                  : isFlag(s) ? (v.toBool() ? 1 : 0) : v.toInt();
        if (index < 0 || index >= c->count())
            index = !s.choices.isEmpty() ? qMax(0, static_cast<int>(s.choices.indexOf(s.def))) : isFlag(s) ? 0 : s.def.toInt();
        c->setCurrentIndex(index);
    }
    else if (QLineEdit* e = qobject_cast<QLineEdit*>(w)) {
        if (s.def.typeId() == QMetaType::Int)         e->setText(QString::number(v.toInt()));
        else if (s.def.typeId() == QMetaType::Double) e->setText(QString::number(v.toDouble()));
        else                                          e->setText(v.toString());
    }
}

QVariant shown(const QList<QWidget*>& ws, const PrefSpec& s)
{
    const QWidget* w = ws.first();
    if (ws.size() > 1) {
        for (qsizetype i = 0; i < ws.size(); ++i)
            if (const QAbstractButton* b = qobject_cast<const QAbstractButton*>(ws.at(i)); b && b->isChecked()) return static_cast<int>(i);
        return 0;
    }
    if (const QColorButton* col = qobject_cast<const QColorButton*>(w)) return col->color();
    if (const QAbstractButton* b = qobject_cast<const QAbstractButton*>(w)) return b->isChecked();
    if (const QSpinBox* i = qobject_cast<const QSpinBox*>(w)) return i->value();
    if (const QDoubleSpinBox* d = qobject_cast<const QDoubleSpinBox*>(w)) return d->value();
    if (const QComboBox* c = qobject_cast<const QComboBox*>(w)) {
        if (!s.choices.isEmpty()) return s.choices.value(c->currentIndex(), s.def);
        return isFlag(s) ? QVariant(c->currentIndex() == 1) : QVariant(c->currentIndex());
    }
    if (const QLineEdit* e = qobject_cast<const QLineEdit*>(w)) {
        if (!isNumber(s)) return e->text();
        // A number typed into a line edit: what cannot be read as one is the default.
        bool ok = false;
        const QVariant v = s.def.typeId() == QMetaType::Int ? QVariant(e->text().toInt(&ok)) : QVariant(e->text().toDouble(&ok));
        return ok ? v : s.def;
    }
    return {};
}

bool canHold(const QWidget* w)
{
    return qobject_cast<const QAbstractButton*>(w) || qobject_cast<const QSpinBox*>(w)
        || qobject_cast<const QDoubleSpinBox*>(w) || qobject_cast<const QComboBox*>(w)
        || qobject_cast<const QLineEdit*>(w);
}

} // namespace

PrefBinder::PrefBinder(QWidget* root, QObject* parent)
    : QObject(parent)
{
    for (const PrefSpec& spec : Configuration::prefTable()) {
        if (spec.widget.isEmpty()) continue;
        QList<QWidget*> ws;
        bool found = true;
        const QStringList names = spec.widget.split(QLatin1Char('|'));
        for (const QString& name : names) {
            QWidget* w = root ? root->findChild<QWidget*>(name) : nullptr;
            found = found && w && canHold(w) && (names.size() == 1 || qobject_cast<QAbstractButton*>(w));
            ws.append(w);
        }
        if (!found) { missing.append(spec.key + QLatin1String(" -> ") + spec.widget); continue; }
        bindings.append({ spec, ws });
        for (QWidget* w : std::as_const(ws)) {
            if (QColorButton* col = qobject_cast<QColorButton*>(w)) connect(col, &QColorButton::colorChanged, this, &PrefBinder::changed);
            else if (QAbstractButton* b = qobject_cast<QAbstractButton*>(w)) connect(b, &QAbstractButton::toggled, this, &PrefBinder::changed);
            else if (QSpinBox* i = qobject_cast<QSpinBox*>(w)) connect(i, &QSpinBox::valueChanged, this, &PrefBinder::changed);
            else if (QDoubleSpinBox* d = qobject_cast<QDoubleSpinBox*>(w)) connect(d, &QDoubleSpinBox::valueChanged, this, &PrefBinder::changed);
            else if (QComboBox* c = qobject_cast<QComboBox*>(w)) connect(c, &QComboBox::currentIndexChanged, this, &PrefBinder::changed);
            else if (QLineEdit* e = qobject_cast<QLineEdit*>(w)) connect(e, &QLineEdit::textChanged, this, &PrefBinder::changed);
        }
    }
}

void PrefBinder::load(const Configuration& config)
{
    for (const Binding& b : bindings) show(b.widgets, b.spec, b.spec.get(config));
}

void PrefBinder::apply(Configuration& config) const
{
    for (const Binding& b : bindings) b.spec.set(config, shown(b.widgets, b.spec));
}

void PrefBinder::restoreDefaults()
{
    for (const Binding& b : bindings) show(b.widgets, b.spec, b.spec.def);
}

QStringList PrefBinder::boundKeys() const
{
    QStringList keys;
    for (const Binding& b : bindings) keys.append(b.spec.key);
    return keys;
}
