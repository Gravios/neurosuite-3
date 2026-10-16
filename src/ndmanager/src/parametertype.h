/* parametertype.h -- the declared type of a plugin parameter and the values
 * it accepts.
 *
 * A program description (a plugin's .yaml, or its `--ndm-describe` output)
 * may give each parameter three optional keys after name/value/status:
 *
 *   type:     how the value is edited and checked (below); absent = text
 *   default:  the value the plugin uses when the field is left empty
 *   hidden:   true/yes/1 hides the row unless ndmanager runs in expert mode
 *
 * Recognised types (case does not matter):
 *
 *   bool, boolean               true/false (also yes/no, on/off, 1/0)
 *   int, integer                a whole number, optionally signed
 *   float, double, number       a decimal number, "1e-3" included
 *   choice:a|b|c, enum:a|b|c    exactly one of the listed words
 *   anything else (string, path, ...)   free text
 *
 * An empty value is always accepted: it means "not set", and the plugin
 * falls back to its default.  The parameter table keeps type, default and
 * hidden on the name cell (the roles below), so they survive an edit and are
 * written back when the description is saved.
 */
#ifndef NDMANAGER_PARAMETERTYPE_H
#define NDMANAGER_PARAMETERTYPE_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>

struct ParameterType {
    enum Kind { Text, Bool, Int, Float, Choice };

    /** Item-data roles on the name cell of a parameter row. */
    enum Role { TypeRole = Qt::UserRole + 1, DefaultRole, HiddenRole };

    Kind        kind = Text;
    QStringList choices;          ///< Choice: the accepted words; Bool: unused

    static ParameterType parse(const QString& declared)
    {
        ParameterType t;
        const QString s = declared.trimmed();
        const QString lower = s.toLower();
        if (lower == QLatin1String("bool") || lower == QLatin1String("boolean"))
            t.kind = Bool;
        else if (lower == QLatin1String("int") || lower == QLatin1String("integer"))
            t.kind = Int;
        else if (lower == QLatin1String("float") || lower == QLatin1String("double")
                 || lower == QLatin1String("number"))
            t.kind = Float;
        else if (lower.startsWith(QLatin1String("choice:")) || lower.startsWith(QLatin1String("enum:"))) {
            const QString list = s.mid(s.indexOf(QLatin1String(":")) + 1);
            for (const QString& c : list.split(QStringLiteral("|"))) {
                const QString w = c.trimmed();
                if (!w.isEmpty() && !t.choices.contains(w)) t.choices << w;
            }
            if (!t.choices.isEmpty()) t.kind = Choice;
        }
        return t;
    }

    /** True for "true"/"false" and the other boolean spellings. */
    static bool isBoolWord(const QString& v)
    {
        const QString l = v.trimmed().toLower();
        return l == QLatin1String("true") || l == QLatin1String("false")
            || l == QLatin1String("yes")  || l == QLatin1String("no")
            || l == QLatin1String("on")   || l == QLatin1String("off")
            || l == QLatin1String("1")    || l == QLatin1String("0");
    }

    /** True for a "hidden" value that hides the row. */
    static bool isTrue(const QString& v)
    {
        const QString l = v.trimmed().toLower();
        return l == QLatin1String("true") || l == QLatin1String("yes")
            || l == QLatin1String("on")   || l == QLatin1String("1");
    }

    /** The two words a boolean editor offers: the pair @p current is spelled
     *  in (so a plugin reading 1/0 keeps getting 1/0), else true/false. */
    static QStringList boolWords(const QString& current)
    {
        const QString l = current.trimmed().toLower();
        if (l == QLatin1String("1") || l == QLatin1String("0"))
            return {QStringLiteral("1"), QStringLiteral("0")};
        if (l == QLatin1String("yes") || l == QLatin1String("no"))
            return {QStringLiteral("yes"), QStringLiteral("no")};
        if (l == QLatin1String("on") || l == QLatin1String("off"))
            return {QStringLiteral("on"), QStringLiteral("off")};
        return {QStringLiteral("true"), QStringLiteral("false")};
    }

    /** Empty when @p value is acceptable, else why not. */
    QString check(const QString& value) const
    {
        const QString v = value.trimmed();
        if (v.isEmpty()) return QString();
        bool ok = true;
        switch (kind) {
        case Text:
            return QString();
        case Bool:
            if (isBoolWord(v)) return QString();
            return tr("Expected true or false.");
        case Int:
            (void)v.toLongLong(&ok);
            if (ok) return QString();
            return tr("Expected a whole number.");
        case Float:
            (void)v.toDouble(&ok);
            if (ok) return QString();
            return tr("Expected a number.");
        case Choice:
            if (choices.contains(v)) return QString();
            return tr("Expected one of: %1.").arg(choices.join(QStringLiteral(", ")));
        }
        return QString();
    }

    /** A short description for tooltips ("whole number", "one of a, b"). */
    QString describe() const
    {
        switch (kind) {
        case Text:   return QString();
        case Bool:   return tr("true or false");
        case Int:    return tr("whole number");
        case Float:  return tr("number");
        case Choice: return tr("one of: %1").arg(choices.join(QStringLiteral(", ")));
        }
        return QString();
    }

private:
    static QString tr(const char* s) { return QCoreApplication::translate("ParameterType", s); }
};

#endif // NDMANAGER_PARAMETERTYPE_H
