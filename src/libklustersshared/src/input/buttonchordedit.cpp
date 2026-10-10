// buttonchordedit.cpp — see buttonchordedit.h.

#include "buttonchordedit.h"

#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QSignalBlocker>

ButtonChordEdit::ButtonChordEdit(QWidget* parent)
    : QWidget(parent)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);

    // Modifier toggles, in displayString order (Ctrl / Alt / Shift / Meta).
    ctrl_  = new QCheckBox(tr("Ctrl"),  this);
    alt_   = new QCheckBox(tr("Alt"),   this);
    shift_ = new QCheckBox(tr("Shift"), this);
    meta_  = new QCheckBox(tr("Meta"),  this);
    for (QCheckBox* cb : { ctrl_, alt_, shift_, meta_ }) {
        h->addWidget(cb);
        connect(cb, &QCheckBox::toggled, this, [this]{ Q_EMIT chordChanged(); });
    }

    // The button itself; item data is the Qt::MouseButton value.
    button_ = new QComboBox(this);
    button_->addItem(tr("Left-button"),    int(Qt::LeftButton));
    button_->addItem(tr("Right-button"),   int(Qt::RightButton));
    button_->addItem(tr("Middle-button"),  int(Qt::MiddleButton));
    button_->addItem(tr("Back-button"),    int(Qt::BackButton));
    button_->addItem(tr("Forward-button"), int(Qt::ForwardButton));
    h->addWidget(button_);
    connect(button_, &QComboBox::currentIndexChanged, this, [this](int){ Q_EMIT chordChanged(); });
}

void ButtonChordEdit::setChord(const input::Chord& c)
{
    // Programmatic load: block the children so no chordChanged escapes (mirrors the
    // QSignalBlocker the key rows use around a programmatic setKeySequence).
    const QSignalBlocker b0(this),  b1(button_), b2(ctrl_),
                         b3(alt_),  b4(shift_),  b5(meta_);

    phase_    = c.phase;
    modMatch_ = c.modMatch;

    int idx = button_->findData(c.code);
    if (idx < 0) {                                   // an unknown button value: show it anyway
        button_->addItem(tr("Button-%1").arg(c.code), c.code);
        idx = button_->count() - 1;
    }
    button_->setCurrentIndex(idx);

    ctrl_->setChecked (c.modifiers & Qt::ControlModifier);
    alt_->setChecked  (c.modifiers & Qt::AltModifier);
    shift_->setChecked(c.modifiers & Qt::ShiftModifier);
    meta_->setChecked (c.modifiers & Qt::MetaModifier);

    // Make the "extras allowed" semantics of an AtLeast gesture legible.
    setToolTip(modMatch_ == input::ModMatch::AtLeast
               ? tr("Extra modifiers are allowed: the gesture fires whenever at least the "
                    "checked modifiers are held.")
               : QString());
}

input::Chord ButtonChordEdit::chord() const
{
    Qt::KeyboardModifiers m = Qt::NoModifier;
    if (ctrl_->isChecked())  m |= Qt::ControlModifier;
    if (alt_->isChecked())   m |= Qt::AltModifier;
    if (shift_->isChecked()) m |= Qt::ShiftModifier;
    if (meta_->isChecked())  m |= Qt::MetaModifier;
    const auto b = static_cast<Qt::MouseButton>(button_->currentData().toInt());
    return input::Chord::button(b, m, phase_, modMatch_);
}

void ButtonChordEdit::setConflict(bool on)
{
    setStyleSheet(on ? QStringLiteral("QComboBox{background:#e2524a;color:white;}") : QString());
    if (on)
        setToolTip(tr("This combination is already used by another command in the same context."));
    else
        setToolTip(modMatch_ == input::ModMatch::AtLeast
                   ? tr("Extra modifiers are allowed: the gesture fires whenever at least the "
                        "checked modifiers are held.")
                   : QString());
}
