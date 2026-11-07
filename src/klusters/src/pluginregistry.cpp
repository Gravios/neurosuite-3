#include "pluginregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <algorithm>

QStringList PluginRegistry::discoveryDirs()
{
    QStringList dirs;
    const QString rel = QStringLiteral("klusters/plugins/descriptions");

    // Standard per-user app data (covers $XDG_DATA_HOME/klusters/...).
    for (const QString& base : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation))
        dirs << base + QStringLiteral("/plugins/descriptions");
    // Generic data dirs (covers the install share/klusters/... on most layouts).
    for (const QString& base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        dirs << base + QLatin1Char('/') + rel;
    // App-relative install fallback (<bindir>/../share/klusters/...).
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/") + rel;
    // Explicit override(s): $KLUSTERS_PLUGIN_PATH, listSeparator-delimited.
    const QString env = qEnvironmentVariable("KLUSTERS_PLUGIN_PATH");
    for (const QString& d : env.split(QDir::listSeparator(), Qt::SkipEmptyParts))
        dirs << d;

    dirs.removeDuplicates();
    return dirs;
}

KlustersPlugin PluginRegistry::parseDescriptor(const QString& path)
{
    KlustersPlugin p;
    p.descriptorPath = path;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return p;

    QXmlStreamReader xml(&f);
    bool inParameters = false;
    bool inParam = false;
    PluginParameter cur;

    while (!xml.atEnd() && !xml.hasError()) {
        const QXmlStreamReader::TokenType tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QStringView nm = xml.name();
            if (nm == u"parameters") {
                inParameters = true;
            } else if (nm == u"parameter" && inParameters) {
                inParam = true;
                cur = PluginParameter();
            } else if (nm == u"n") {
                const QString t = xml.readElementText();
                if (inParam)             cur.name = t;
                else if (p.name.isEmpty()) p.name = t;   // program-level name (first <n> before <parameters>)
            } else if (nm == u"value" && inParam) {
                cur.value = xml.readElementText();
            } else if (nm == u"status" && inParam) {
                cur.status = xml.readElementText();
            } else if (nm == u"type" && inParam) {
                cur.type = xml.readElementText().trimmed().toLower();
            } else if (nm == u"min" && inParam) {
                cur.min = xml.readElementText().trimmed();
            } else if (nm == u"max" && inParam) {
                cur.max = xml.readElementText().trimmed();
            } else if (nm == u"step" && inParam) {
                cur.step = xml.readElementText().trimmed();
            } else if (nm == u"choices" && inParam) {
                cur.choices = xml.readElementText().split(QLatin1Char('|'));
                for (QString& c : cur.choices) c = c.trimmed();
            } else if (nm == u"help" && inParam) {
                cur.help = xml.readElementText().trimmed();
            } else if (nm == u"help") {
                p.help = xml.readElementText().trimmed();
            } else if (nm == u"input") {
                p.input = xml.readElementText().trimmed().toLower();
            } else if (nm == u"command") {
                p.command = xml.readElementText().simplified();
            } else if (nm == u"kind") {
                p.kind = xml.readElementText().trimmed();
            } else if (nm == u"consumes") {
                p.consumes = xml.readElementText().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            } else if (nm == u"produces") {
                p.produces = xml.readElementText().trimmed();
            } else if (nm == u"integration") {
                p.integration = xml.readElementText().trimmed();
            } else if (nm == u"selection") {
                p.selection = xml.readElementText().trimmed();
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const QStringView nm = xml.name();
            if (nm == u"parameters") {
                inParameters = false;
            } else if (nm == u"parameter" && inParam) {
                inParam = false;
                if (!cur.name.isEmpty())
                    p.parameters.append(cur);
            }
        }
    }

    p.valid = !xml.hasError() && !p.name.isEmpty();
    return p;
}

PluginParameter::Type PluginParameter::effectiveType() const
{
    const QString t = type.trimmed().toLower();
    if (t == QLatin1String("bool"))   return Type::Bool;
    if (t == QLatin1String("int"))    return Type::Int;
    if (t == QLatin1String("real") || t == QLatin1String("float") || t == QLatin1String("double")) return Type::Real;
    if (t == QLatin1String("path") || t == QLatin1String("file") || t == QLatin1String("dir")) return Type::Path;
    if (t == QLatin1String("choice") || t == QLatin1String("enum") || !choices.isEmpty()) return Type::Choice;
    if (t == QLatin1String("text") || t == QLatin1String("string")) return Type::Text;
    // inferred from the default
    const QString v = value.trimmed();
    const QString lv = v.toLower();
    const QString ln = name.toLower();
    if (lv == QLatin1String("true") || lv == QLatin1String("false") || lv == QLatin1String("on")
        || lv == QLatin1String("off") || lv == QLatin1String("yes") || lv == QLatin1String("no"))
        return Type::Bool;
    if (ln.endsWith(QLatin1String("path")) || ln.endsWith(QLatin1String("file")) || ln.endsWith(QLatin1String("dir"))
        || ln.endsWith(QLatin1String("directory")) || ln.endsWith(QLatin1String("folder")))
        return Type::Path;
    static const QRegularExpression reInt(QStringLiteral("^[+-]?\\d+$"));
    static const QRegularExpression reReal(QStringLiteral("^[+-]?(\\d+\\.\\d*|\\.\\d+|\\d+)([eE][+-]?\\d+)?$"));
    if (reInt.match(v).hasMatch()) return Type::Int;
    if (!v.isEmpty() && reReal.match(v).hasMatch()) return Type::Real;
    if (v.contains(QLatin1Char('/')) && v.contains(QLatin1Char('.'))) return Type::Path;
    return Type::Text;
}

int PluginParameter::decimals() const
{
    const int dot = value.indexOf(QLatin1Char('.'));
    if (dot < 0) return 1;
    int n = 0;
    for (int i = dot + 1; i < value.size() && value[i].isDigit(); ++i) ++n;
    return qMax(1, n);
}

static bool isTrue(const QString& v)
{
    const QString l = v.trimmed().toLower();
    return l == QLatin1String("true") || l == QLatin1String("on") || l == QLatin1String("yes") || l == QLatin1String("1");
}

QStringList PluginRegistry::paramArgs(const KlustersPlugin& plugin, const QMap<QString, QString>& params)
{
    QStringList argv;
    // Descriptor parameters as "--name value".  An Optional parameter left empty
    // is omitted; a Bool is the bare flag when true and nothing when false.
    for (const PluginParameter& p : plugin.parameters) {
        const QString v = params.value(p.name, p.value).trimmed();
        if (p.effectiveType() == PluginParameter::Type::Bool) {
            if (isTrue(v)) argv << (QStringLiteral("--") + p.name);
            continue;
        }
        if (v.isEmpty() && !p.mandatory())
            continue;
        argv << (QStringLiteral("--") + p.name);
        // A field may hold several whitespace-separated tokens (a list option, e.g.
        // "standard stderiv_C5_D34" or "drift adapt") that an nargs>=1 CLI needs as
        // DISTINCT arguments, not one glued string.  Split shell-style so a value
        // that genuinely contains a space can still be protected with quotes.
        if (!v.isEmpty())
            argv << QProcess::splitCommand(v);
    }
    return argv;
}

QStringList PluginRegistry::buildArgv(const KlustersPlugin& plugin,
                                     const QMap<QString, QString>& params,
                                     const QMap<QString, QString>& context,
                                     const QList<int>& selection)
{
    QStringList argv;
    const QStringList pargs = paramArgs(plugin, params);

    if (!plugin.command.isEmpty()) {
        // The <command> template: %tokens from the context, %selection/%children as one argument per
        // id, %params where the parameters go (appended when the template has none).
        bool placedParams = false;
        for (const QString& word : QProcess::splitCommand(plugin.command)) {
            if (word == QLatin1String("%selection") || word == QLatin1String("%children")) {
                for (int c : selection) argv << QString::number(c);
            } else if (word == QLatin1String("%params")) {
                argv << pargs; placedParams = true;
            } else {
                QString w = word;
                for (auto it = context.constBegin(); it != context.constEnd(); ++it)
                    w.replace(QLatin1Char('%') + it.key(), it.value());
                argv << w;
            }
        }
        if (!placedParams) argv << pargs;
        return argv;
    }

    argv << plugin.name;
    // Resolved <consumes> tokens, positional, in declared order.
    for (const QString& tok : plugin.consumes) {
        if (tok == QLatin1String("selection") || tok == QLatin1String("children")) {
            for (int c : selection)
                argv << QString::number(c);
        } else {
            argv << context.value(tok);     // base / group / variant / tag
        }
    }
    argv << pargs;
    return argv;
}

void PluginRegistry::reload()
{
    mPlugins.clear();
    const QStringList dirs = discoveryDirs();
    for (const QString& d : dirs) {
        QDir dir(d);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList(QStringList{QStringLiteral("*.xml")},
                                                QDir::Files, QDir::Name);
        for (const QString& f : files) {
            const KlustersPlugin p = parseDescriptor(dir.filePath(f));
            if (!p.valid)
                continue;
            // Later dir wins on a name collision.
            for (int i = mPlugins.size() - 1; i >= 0; --i)
                if (mPlugins[i].name == p.name)
                    mPlugins.removeAt(i);
            mPlugins.append(p);
        }
    }
    std::sort(mPlugins.begin(), mPlugins.end(),
              [](const KlustersPlugin& a, const KlustersPlugin& b) { return a.name < b.name; });
}
