/* parametercatalog.cpp -- see parametercatalog.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "parametercatalog.h"

namespace {
struct RawEntry {
    const char* program;
    const char* name;
    const char* value;
    const char* status;
    bool inTemplate;
    const char* help;
};

const RawEntry kEntries[] = {
#include "parametercatalog.inc"
};
}  // namespace

QList<ParameterCatalogEntry> ParameterCatalog::forProgram(const QString& program)
{
    QList<ParameterCatalogEntry> out;
    const QByteArray key = program.toUtf8();
    for (const RawEntry& e : kEntries) {
        if (key != e.program) continue;
        ParameterCatalogEntry entry;
        entry.name = QString::fromUtf8(e.name);
        entry.value = QString::fromUtf8(e.value);
        entry.status = QString::fromUtf8(e.status);
        entry.inTemplate = e.inTemplate;
        entry.help = QString::fromUtf8(e.help);
        out << entry;
    }
    return out;
}

QString ParameterCatalog::help(const QString& program, const QString& name)
{
    const QByteArray p = program.toUtf8();
    const QByteArray n = name.toUtf8();
    for (const RawEntry& e : kEntries)
        if (p == e.program && n == e.name) return QString::fromUtf8(e.help);
    return QString();
}
