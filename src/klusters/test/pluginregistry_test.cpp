// pluginregistry_test.cpp -- the plugin descriptor contract: typed parameters, the <command>
// template and <input>, buildArgv, and the per-plugin defaults store (PluginSettings).
//
//   * parseDescriptor reads <type>/<min>/<max>/<step>/<choices>/<help> under a parameter, and
//     <input>/<command> under <klusters>; a `recluster` kind without <input> runs on the sub-session;
//   * effectiveType: declared wins, else inferred (true/false -> Bool, 12 -> Int, 0.125 -> Real with
//     3 decimals, *_path / a/b.c -> Path, choices -> Choice, else Text);
//   * buildArgv with a template: %tokens substituted, %selection one argument per id, %params where
//     placed (appended when absent), a Bool true = bare flag / false = nothing, an empty Optional
//     omitted, a list value split, %tokens in a value substituted; without a template: the positional form;
//   * PluginSettings: values() overlays stored on descriptor defaults, store() keeps only what differs
//     and drops stale keys, reset(), overridden(); the group name escapes '/'.
// QtCore only, own main, run via ctest.
#include "pluginregistry.h"
#include "pluginsettings.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>
#include <string>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    std::printf("%s %s\n", ok ? "  ok:" : "FAIL:", what.c_str());
    g_fail += !ok;
}
static std::string S(const QStringList& l) { return l.join(QLatin1Char(' ')).toStdString(); }

static const char* kDescriptor = R"(<?xml version="1.0"?>
<program>
  <n>fiber-kit-recluster</n>
  <parameters>
    <parameter><n>profile</n><value></value><status>Optional</status><type>choice</type>
      <choices>|base|exp|best</choices><help>config profile</help></parameter>
    <parameter><n>chunks</n><value>all</value><status>Optional</status></parameter>
    <parameter><n>theta</n><value>0.125</value><status>Optional</status><min>0</min><max>1</max></parameter>
    <parameter><n>knn</n><value>12</value><status>Mandatory</status><type>int</type><min>1</min></parameter>
    <parameter><n>verbose</n><value>false</value><status>Optional</status></parameter>
    <parameter><n>plan-file</n><value></value><status>Optional</status><type>path</type></parameter>
    <parameter><n>methods</n><value>standard stderiv</value><status>Optional</status></parameter>
  </parameters>
  <help>Run a fiber-kit plan on the selection.</help>
  <klusters>
    <kind>recluster</kind>
    <produces>triple</produces>
    <integration>recluster-integrate</integration>
    <selection>clusters</selection>
    <command>fk run %group --session %base --final-clu %final %params</command>
  </klusters>
</program>
)";

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir td;
    const QString path = td.filePath("fiber-kit-recluster.xml");
    { QFile f(path); f.open(QIODevice::WriteOnly); f.write(kDescriptor); }

    const KlustersPlugin p = PluginRegistry::parseDescriptor(path);
    check(p.valid && p.name == "fiber-kit-recluster" && p.parameters.size() == 7, "descriptor parsed, 7 parameters");
    check(p.kind == "recluster" && p.input.isEmpty() && p.runsOnSubsession(), "a recluster kind runs on the sub-session by default");
    check(p.command == "fk run %group --session %base --final-clu %final %params", "the <command> template");
    const PluginParameter& prof = p.parameters[0];
    check(prof.effectiveType() == PluginParameter::Type::Choice && prof.choices == QStringList{"", "base", "exp", "best"}
          && prof.help == "config profile", "choice parameter with its choices and help");
    check(p.parameters[1].effectiveType() == PluginParameter::Type::Text, "'all' is Text");
    check(p.parameters[2].effectiveType() == PluginParameter::Type::Real && p.parameters[2].decimals() == 3
          && p.parameters[2].min == "0" && p.parameters[2].max == "1", "0.125 is Real with 3 decimals, range read");
    check(p.parameters[3].effectiveType() == PluginParameter::Type::Int && p.parameters[3].mandatory(), "declared int, mandatory");
    check(p.parameters[4].effectiveType() == PluginParameter::Type::Bool, "false is Bool");
    check(p.parameters[5].effectiveType() == PluginParameter::Type::Path, "declared path");
    PluginParameter inferredPath; inferredPath.name = "out_dir"; inferredPath.value = "";
    check(inferredPath.effectiveType() == PluginParameter::Type::Path, "a name ending in dir is a Path");
    PluginParameter slash; slash.name = "x"; slash.value = "a/b.c";
    check(slash.effectiveType() == PluginParameter::Type::Path, "a value with slash and dot is a Path");

    // buildArgv with the template
    QMap<QString, QString> ctx{{"base", "rat--12-15--t"}, {"group", "6"}, {"final", "rat--12-15--t.clu.klusters.6"},
                               {"variant", "stderiv"}, {"tag", "cur"}};
    QMap<QString, QString> params{{"profile", "best"}, {"knn", "8"}, {"verbose", "true"}, {"methods", "standard stderiv_C5"}};
    QStringList argv1 = PluginRegistry::buildArgv(p, params, ctx, QList<int>{12, 15});
    check(argv1 == QStringList{"fk", "run", "6", "--session", "rat--12-15--t", "--final-clu", "rat--12-15--t.clu.klusters.6",
                               "--profile", "best", "--chunks", "all", "--theta", "0.125", "--knn", "8", "--verbose",
                               "--methods", "standard", "stderiv_C5"},
          "template argv: tokens, %params in place, bool flag, defaults kept, empty optional omitted, list split: " + S(argv1));
    params["verbose"] = "false";
    argv1 = PluginRegistry::buildArgv(p, params, ctx, {});
    check(!argv1.contains("--verbose"), "a false Bool adds nothing");
    params["chunks"] = "%variant-%tag";
    argv1 = PluginRegistry::buildArgv(p, params, ctx, {});
    check(argv1.contains("stderiv-cur"), "%tokens in a parameter VALUE are substituted from the context: " + S(argv1));
    KlustersPlugin q = p; q.command = "engine %selection --n %nchan";
    ctx.insert("nchan", "8");
    QStringList argv2 = PluginRegistry::buildArgv(q, {}, ctx, QList<int>{3, 4});
    check(argv2.mid(0, 5) == QStringList{"engine", "3", "4", "--n", "8"} && argv2.contains("--knn") && argv2.last() == "stderiv",
          "%selection expands per id; %params appended when the template has none: " + S(argv2));
    KlustersPlugin r = p; r.command.clear(); r.consumes = QStringList{"base", "group", "selection"};
    QStringList argv3 = PluginRegistry::buildArgv(r, {}, ctx, QList<int>{3});
    check(argv3.mid(0, 4) == QStringList{"fiber-kit-recluster", "rat--12-15--t", "6", "3"}, "positional form unchanged: " + S(argv3));

    // PluginSettings on a private ini
    QSettings ini(td.filePath("klusters.ini"), QSettings::IniFormat);
    PluginSettings::setSettingsForTests(&ini);
    check(PluginSettings::values(p).value("theta") == "0.125" && !PluginSettings::overridden(p), "values() = descriptor defaults when nothing stored");
    QMap<QString, QString> v = PluginSettings::values(p);
    v["theta"] = "0.5"; v["knn"] = "12"; v["gone"] = "x";
    PluginSettings::store(p, v);
    QMap<QString, QString> st = PluginSettings::stored(p.name);
    check(st.size() == 1 && st.value("theta") == "0.5", "store keeps only what differs from the descriptor (knn 12 = default, 'gone' undeclared): "
                                                        + std::to_string(st.size()));
    check(PluginSettings::values(p).value("theta") == "0.5" && PluginSettings::values(p).value("knn") == "12" && PluginSettings::overridden(p),
          "values() overlays the stored theta; overridden()");
    v["theta"] = "0.125"; v["chunks"] = "1-3";
    PluginSettings::store(p, v);
    st = PluginSettings::stored(p.name);
    check(st.size() == 1 && st.value("chunks") == "1-3", "a value set back to the default is dropped, a new difference stored");
    PluginSettings::reset(p.name);
    check(PluginSettings::stored(p.name).isEmpty() && !PluginSettings::overridden(p), "reset forgets everything");
    check(PluginSettings::groupOf("a/b") == "Plugins/a%2Fb", "a '/' in the name does not nest settings groups");

    std::printf("%s (%d checks, %d failed)\n", g_fail ? "FAILED" : "OK", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
