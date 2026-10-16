/* plugincategory.h -- the purpose a plugin is listed under.
 *
 * The Plugins entry of the left-hand tree and the Pipeline palette both list
 * plugins grouped by purpose (conversion, detection, sorting, ...), as
 * collapsible branches.  A plugin's purpose comes from, in order:
 *
 *   1. its description: `program.category` in its .yaml or in what it prints
 *      for `--ndm-describe` (kept in the session file's programs: entry);
 *   2. the built-in table of the plugins this repository ships
 *      (tools/pipeline_script_defs.py -> pipelinescriptdefs.inc);
 *   3. otherwise "other".
 *
 * Branches appear in the built-in table's order, then any other purpose a
 * description names (alphabetically), then Other.  No Qt widgets here, so the
 * ordering is tested without a GUI.
 */
#ifndef NDMANAGER_PLUGINCATEGORY_H
#define NDMANAGER_PLUGINCATEGORY_H

#include <QString>
#include <QStringList>
#include <algorithm>

namespace plugincategory {

inline QString other() { return QStringLiteral("other"); }

/** The key a category is compared by: trimmed, lower case; empty is "other". */
inline QString normalize(const QString& category)
{
    const QString c = category.trimmed().toLower();
    return c.isEmpty() ? other() : c;
}

/** The category of a plugin: its described one, else its built-in one, else "other". */
inline QString resolve(const QString& described, const QString& builtin)
{
    if (!described.trimmed().isEmpty()) return normalize(described);
    return normalize(builtin);
}

/** @p present in display order: the ones in @p known in that order, then the
 *  rest alphabetically, then "other".  Duplicates are listed once. */
inline QStringList order(const QStringList& known, const QStringList& present)
{
    QStringList wanted;
    for (const QString& p : present) {
        const QString c = normalize(p);
        if (!wanted.contains(c)) wanted << c;
    }
    QStringList out, rest;
    for (const QString& k : known) {
        const QString c = normalize(k);
        if (c != other() && wanted.contains(c) && !out.contains(c)) out << c;
    }
    for (const QString& c : wanted)
        if (c != other() && !out.contains(c)) rest << c;
    std::sort(rest.begin(), rest.end());
    for (const QString& c : rest) out << c;
    if (wanted.contains(other())) out << other();
    return out;
}

/** A label for a category the built-in table has no label for:
 *  "spike-quality" -> "Spike quality", "other" -> "Other". */
inline QString fallbackLabel(const QString& category)
{
    QString c = normalize(category);
    for (int i = 0; i < c.size(); ++i)
        if (c[i] == QLatin1Char('-') || c[i] == QLatin1Char('_')) c[i] = QLatin1Char(' ');
    return c.left(1).toUpper() + c.mid(1);
}

} // namespace plugincategory

#endif // NDMANAGER_PLUGINCATEGORY_H
