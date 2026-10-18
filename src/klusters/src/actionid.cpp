/***************************************************************************
 * actionid.cpp — see actionid.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "actionid.h"

namespace actionid {

QString plainText(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('&')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&')) { out.append(QLatin1Char('&')); ++i; }
            continue;
        }
        out.append(text.at(i));
    }
    out = out.trimmed();
    if (out.endsWith(QStringLiteral("..."))) out.chop(3);
    else if (out.endsWith(QChar(0x2026))) out.chop(1);
    return out.trimmed();
}

QString menuPathId(const QStringList& path, const QString& actionText)
{
    QStringList parts;
    for (const QString& p : path) parts << plainText(p);
    parts << plainText(actionText);
    return QStringLiteral("menu:") + parts.join(QLatin1Char('/'));
}

} // namespace actionid
