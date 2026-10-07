// wheelchordedit.cpp — see wheelchordedit.h.  Parallel to buttonchordedit.cpp.

#include "wheelchordedit.h"

#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QSignalBlocker>

WheelChordEdit::WheelChordEdit(QWidget* parent)
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

    // The wheel direction; item data is the signed direction (+1 up / -1 down).
    dir_ = new QComboBox(this);
    dir_->addItem(tr("Wheel up"),   +1);
    dir_->addItem(tr("Wheel down"), -1);
    h->addWidget(dir_);
    connect(dir_, &QComboBox::currentIndexChanged, this, [this](int){ Q_EMIT chordChanged(); });
}

void WheelChordEdit::setChord(const input::Chord& c)
{
    // Programmatic load: block the children so no chordChanged escapes (mirrors the
    // QSignalBlocker the key rows use around a programmatic setKeySequence).
    const QSignalBlocker b0(this),  b1(dir_), b2(ctrl_),
                         b3(alt_),  b4(shift_), b5(meta_);

    phase_    = c.phase;
    modMatch_ = c.modMatch;

    const int wantDir = (c.code < 0) ? -1 : +1;
    int idx = dir_->findData(wantDir);
    dir_->setCurrentIndex(idx < 0 ? 0 : idx);

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

input::Chord WheelChordEdit::chord() const
{
    Qt::KeyboardModifiers m = Qt::NoModifier;
    if (ctrl_->isChecked())  m |= Qt::ControlModifier;
    if (alt_->isChecked())   m |= Qt::AltModifier;
    if (shift_->isChecked()) m |= Qt::ShiftModifier;
    if (meta_->isChecked())  m |= Qt::MetaModifier;
    return input::Chord::wheel(dir_->currentData().toInt(), m, modMatch_);
}

void WheelChordEdit::setConflict(bool on)
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
