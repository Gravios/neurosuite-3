#ifndef PLUGINREGISTRY_H
#define PLUGINREGISTRY_H

// Read-only discovery + parse of Klusters plugin descriptors (see
// docs/PLUGIN_API.md).  Descriptors use the ndmanager-plugins <program> schema
// plus an optional <klusters> block (kind / consumes / produces / integration /
// selection).  Phase 1: parse XML descriptors and list them; the dialog + runner
// arrive in later phases.  (YAML descriptors are deferred to the shared
// DescriptionYamlReader consolidation noted in the spec.)

#include <QString>
#include <QStringList>
#include <QList>

struct PluginParameter {
    QString name;
    QString value;
    QString status;        // "Optional" | "Mandatory"
};

struct KlustersPlugin {
    QString name;                       // <program><n>
    QList<PluginParameter> parameters;  // <parameters><parameter>...
    QString help;                       // <help>
    // <klusters> extension (empty for a plain ndManager descriptor):
    QString kind;                       // recluster | refiber | analysis | export
    QStringList consumes;               // base group variant tag selection children
    QString produces;                   // clu | triple | report | none
    QString integration;                // recluster-integrate | hierarchy-reload | none
    QString selection;                  // none | clusters | children
    QString descriptorPath;
    bool valid = false;
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

private:
    QList<KlustersPlugin> mPlugins;
};

#endif // PLUGINREGISTRY_H
