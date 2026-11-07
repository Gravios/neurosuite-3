#ifndef PLUGINREGISTRY_H
#define PLUGINREGISTRY_H

// Discovery + parse of Klusters plugin descriptors (see docs/PLUGIN_API.md).
// Descriptors use the ndmanager-plugins <program> schema plus an optional
// <klusters> block (kind / consumes / produces / integration / selection).  This
// class parses XML descriptors, lists them, and assembles the invocation
// (buildArgv); the parameter dialog (PluginDialog) and the execute + integrate
// runner (KlustersApp::runPlugin) consume it.  (YAML descriptors are deferred to
// the shared DescriptionYamlReader consolidation noted in the spec.)

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

struct PluginParameter {
    QString name;
    QString value;
    QString status;        // "Optional" | "Mandatory"
    // Optional typing (docs/PLUGIN_API.md "Parameter types"): how Preferences > Plugins and the run
    // dialog edit the parameter.  Empty type = inferred from the default (effectiveType()).
    QString type;          // bool | int | real | path | text | choice
    QString min, max, step;
    QStringList choices;   // <choices>a|b|c</choices> (an empty entry = "unset")
    QString help;          // <help> under the parameter

    enum class Type { Bool, Int, Real, Path, Text, Choice };
    /** The declared type, or the one inferred from the default: true/false/on/off/yes/no -> Bool, an
     *  integer literal -> Int, a real literal -> Real, a name ending in path/file/dir(ectory)/folder or a
     *  value with a slash and a dot -> Path, choices declared -> Choice, else Text. */
    Type effectiveType() const;
    /** The decimals a Real default is written with ("0.125" -> 3), at least 1. */
    int decimals() const;
    bool mandatory() const { return status.compare(QStringLiteral("Mandatory"), Qt::CaseInsensitive) == 0; }
};

struct KlustersPlugin {
    QString name;                       // <program><n>
    QList<PluginParameter> parameters;  // <parameters><parameter>...
    QString help;                       // <help>
    // <klusters> extension (empty for a plain ndManager descriptor):
    QString kind;                       // recluster | repair | analysis | export
    QStringList consumes;               // base group variant tag selection children (positional form)
    QString produces;                   // clu | triple | report | none
    QString integration;                // recluster-integrate | hierarchy-reload | none
    QString selection;                  // none | clusters | children
    /** <input>: what the engine runs on.  `session` (default): the session files on disk.
     *  `subsession`: the selected clusters' spikes exported as a session of their own (the temp-file
     *  method: KlustersDoc::createReclusterSubsession); %base then names the sub-session and %final the
     *  .clu Klusters reads back.  A `recluster` kind defaults to `subsession`. */
    QString input;
    /** <command>: the invocation as a template, instead of the positional <consumes>: tokens %base
     *  %group %variant %tag %dir %final %nsamp %nchan %rate (the context), %selection / %children (the
     *  selected ids, one argument each) and %params (where the "--name value" parameters go; appended
     *  when absent).  The first word is the program. */
    QString command;
    QString descriptorPath;
    bool valid = false;

    bool runsOnSubsession() const {
        return input == QLatin1String("subsession") || (input.isEmpty() && kind == QLatin1String("recluster"));
    }
};

class PluginRegistry {
public:
    /** Scan all discovery dirs and (re)parse every *.xml descriptor.  Idempotent;
     *  later dirs override earlier ones on a name collision. */
    void reload();
    const QList<KlustersPlugin>& plugins() const { return mPlugins; }

    /** Discovery dirs, in increasing priority: standard app/data locations,
     *  the app-relative install share, then $KLUSTERS_PLUGIN_PATH entries. */
    static QStringList discoveryDirs();
    /** Parse one descriptor file.  Returns a plugin with valid==false on error. */
    static KlustersPlugin parseDescriptor(const QString& path);

    /** Assemble the program invocation.  With a <command> template: its words with the %tokens
     *  substituted from @p context (%selection / %children expand to one argument per id, %params to
     *  the parameters).  Without: the program name, then the resolved <consumes> tokens (in declared
     *  order; 'selection'/'children' expand to the selected ids), then the parameters.  A parameter
     *  is "--name value" (a Bool: the bare flag when true, nothing when false; an Optional parameter
     *  left empty is omitted).  The first element is the program. */
    static QStringList buildArgv(const KlustersPlugin& plugin,
                                 const QMap<QString, QString>& params,
                                 const QMap<QString, QString>& context,
                                 const QList<int>& selection);
    /** The "--name value" arguments alone (as buildArgv appends them). */
    static QStringList paramArgs(const KlustersPlugin& plugin, const QMap<QString, QString>& params);

private:
    QList<KlustersPlugin> mPlugins;
};

#endif // PLUGINREGISTRY_H
