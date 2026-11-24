// prefbinder.cpp -- see prefbinder.h.

#include "prefbinder.h"

#include "configuration.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>

namespace {

bool isFlag(const PrefSpec& s) { return s.def.typeId() == QMetaType::Bool; }

// Show @p v in @p w.  @p s gives the kind of value and, for an index out of the combo
// box's range, the entry to fall back on.
void show(QWidget* w, const PrefSpec& s, const QVariant& v)
{
    if (QAbstractButton* b = qobject_cast<QAbstractButton*>(w)) b->setChecked(v.toBool());
    else if (QSpinBox* i = qobject_cast<QSpinBox*>(w)) i->setValue(v.toInt());
    else if (QDoubleSpinBox* d = qobject_cast<QDoubleSpinBox*>(w)) d->setValue(v.toDouble());
    else if (QComboBox* c = qobject_cast<QComboBox*>(w)) {
        int index = isFlag(s) ? (v.toBool() ? 1 : 0) : v.toInt();
        if (index < 0 || index >= c->count()) index = isFlag(s) ? 0 : s.def.toInt();
        c->setCurrentIndex(index);
    }
    else if (QLineEdit* e = qobject_cast<QLineEdit*>(w)) e->setText(v.toString());
}

QVariant shown(const QWidget* w, const PrefSpec& s)
{
    if (const QAbstractButton* b = qobject_cast<const QAbstractButton*>(w)) return b->isChecked();
    if (const QSpinBox* i = qobject_cast<const QSpinBox*>(w)) return i->value();
    if (const QDoubleSpinBox* d = qobject_cast<const QDoubleSpinBox*>(w)) return d->value();
    if (const QComboBox* c = qobject_cast<const QComboBox*>(w))
        return isFlag(s) ? QVariant(c->currentIndex() == 1) : QVariant(c->currentIndex());
    if (const QLineEdit* e = qobject_cast<const QLineEdit*>(w)) return e->text();
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
        QWidget* w = root ? root->findChild<QWidget*>(spec.widget) : nullptr;
        if (!w || !canHold(w)) { missing.append(spec.key + QLatin1String(" -> ") + spec.widget); continue; }
        bindings.append({ spec, w });
        if (QAbstractButton* b = qobject_cast<QAbstractButton*>(w)) connect(b, &QAbstractButton::toggled, this, &PrefBinder::changed);
        else if (QSpinBox* i = qobject_cast<QSpinBox*>(w)) connect(i, &QSpinBox::valueChanged, this, &PrefBinder::changed);
        else if (QDoubleSpinBox* d = qobject_cast<QDoubleSpinBox*>(w)) connect(d, &QDoubleSpinBox::valueChanged, this, &PrefBinder::changed);
        else if (QComboBox* c = qobject_cast<QComboBox*>(w)) connect(c, &QComboBox::currentIndexChanged, this, &PrefBinder::changed);
        else if (QLineEdit* e = qobject_cast<QLineEdit*>(w)) connect(e, &QLineEdit::textChanged, this, &PrefBinder::changed);
    }
}

void PrefBinder::load(const Configuration& config)
{
    for (const Binding& b : bindings) show(b.widget, b.spec, b.spec.get(config));
}

void PrefBinder::apply(Configuration& config) const
{
    for (const Binding& b : bindings) b.spec.set(config, shown(b.widget, b.spec));
}

void PrefBinder::restoreDefaults()
{
    for (const Binding& b : bindings) show(b.widget, b.spec, b.spec.def);
}

QStringList PrefBinder::boundKeys() const
{
    QStringList keys;
    for (const Binding& b : bindings) keys.append(b.spec.key);
    return keys;
}
