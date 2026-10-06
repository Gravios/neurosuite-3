// input/chord.cpp — see input/chord.h.

#include "chord.h"

#include <QKeyCombination>
#include <QKeySequence>
#include <QStringList>

namespace input {

QString Chord::toString() const
{
    // Exact integer encoding for persistence; see the header.
    return QStringLiteral("%1/%2/%3/%4")
        .arg(int(device))
        .arg(code)
        .arg(int(modifiers))
        .arg(int(phase));
}

Chord Chord::fromString(const QString& s)
{
    const QStringList parts = s.split(QLatin1Char('/'));
    if (parts.size() != 4) return {};            // malformed -> invalid chord

    bool ok0 = false, ok1 = false, ok2 = false, ok3 = false;
    const int dev   = parts[0].toInt(&ok0);
    const int code  = parts[1].toInt(&ok1);
    const int mods  = parts[2].toInt(&ok2);
    const int phase = parts[3].toInt(&ok3);
    if (!(ok0 && ok1 && ok2 && ok3)) return {};

    if (dev < int(Device::None) || dev > int(Device::Wheel)) return {};
    if (phase < int(Phase::Press) || phase > int(Phase::Wheel)) return {};

    Chord c;
    c.device    = static_cast<Device>(dev);
    c.code      = code;
    c.modifiers = normalize(static_cast<Qt::KeyboardModifiers>(mods));
    c.phase     = static_cast<Phase>(phase);
    return c;
}

static QString modsText(Qt::KeyboardModifiers m)
{
    QStringList out;
    if (m & Qt::ControlModifier) out << QStringLiteral("Ctrl");
    if (m & Qt::AltModifier)     out << QStringLiteral("Alt");
    if (m & Qt::ShiftModifier)   out << QStringLiteral("Shift");
    if (m & Qt::MetaModifier)    out << QStringLiteral("Meta");
    return out.isEmpty() ? QString() : (out.join(QLatin1Char('+')) + QLatin1Char('+'));
}

static QString buttonName(int code)
{
    switch (static_cast<Qt::MouseButton>(code)) {
    case Qt::LeftButton:    return QStringLiteral("Left-button");
    case Qt::RightButton:   return QStringLiteral("Right-button");
    case Qt::MiddleButton:  return QStringLiteral("Middle-button");
    case Qt::BackButton:    return QStringLiteral("Back-button");
    case Qt::ForwardButton: return QStringLiteral("Forward-button");
    default:                return QStringLiteral("Button-%1").arg(code);
    }
}

QString Chord::displayString() const
{
    switch (device) {
    case Device::None:
        return QStringLiteral("(unbound)");
    case Device::Key: {
        // QKeySequence renders the modifier combo + key natively ("Ctrl+Shift+S").
        const QKeySequence seq(static_cast<int>(modifiers) | code);
        const QString txt = seq.toString(QKeySequence::NativeText);
        return txt.isEmpty() ? QStringLiteral("(unbound)") : txt;
    }
    case Device::Button: {
        QString b = modsText(modifiers) + buttonName(code);
        if (phase == Phase::DoubleClick) b += QStringLiteral(" (double)");
        return b;
    }
    case Device::Wheel:
        return modsText(modifiers)
             + (code < 0 ? QStringLiteral("Wheel down") : QStringLiteral("Wheel up"));
    }
    return QStringLiteral("(unbound)");
}

Chord chordFromKeySequence(const QKeySequence& seq)
{
    if (seq.isEmpty()) return {};
    const QKeyCombination comb = seq[0];           // first (and, for a shortcut, only) combo
    const int key = comb.key();
    if (key == 0 || key == Qt::Key_unknown) return {};
    return Chord::key(key, comb.keyboardModifiers());
}

QKeySequence keySequenceFromChord(const Chord& c)
{
    if (c.device != Device::Key) return {};        // only key chords map to a QAction shortcut
    return QKeySequence(QKeyCombination(c.modifiers, static_cast<Qt::Key>(c.code)));
}

}  // namespace input
