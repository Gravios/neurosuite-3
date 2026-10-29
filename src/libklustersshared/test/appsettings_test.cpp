// appsettings_test.cpp — the programs' settings live under the organization
// "neurosuite-3", and the first start copies what is still under the old
// organizations ("sourceforge" for each program, "neurosuite" for the theme).
//
// Runs with XDG_CONFIG_HOME / XDG_DATA_HOME in a temporary folder, so the
// user's own settings are never touched.  Linux/XDG paths are checked
// literally; elsewhere only the QSettings / QStandardPaths round trip is.
// Self-contained (own main), run via ctest.

#include "appsettings.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>
#include <string>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string &what)
{
    ++g_ran;
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

static bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QString legacySettingsOrg()
{
#ifdef Q_OS_DARWIN
    return QStringLiteral("sourceforge.net");
#else
    return QStringLiteral("sourceforge");
#endif
}

static QString appData(const QString &org, const QString &app)
{
    QCoreApplication::setOrganizationName(org);
    QCoreApplication::setApplicationName(app);
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

int main(int argc, char **argv)
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::printf("FAIL: no temporary folder\n"); return 1; }
    const QString config = tmp.path() + QStringLiteral("/config");
    const QString data   = tmp.path() + QStringLiteral("/data");
    // Before anything reads a path: QSettings and QStandardPaths cache them.
    qputenv("XDG_CONFIG_HOME", config.toLocal8Bit());
    qputenv("XDG_DATA_HOME", data.toLocal8Bit());

    // ---- the old state: a Klusters and an NDManager that ran before -------
    {
        QSettings old(QSettings::NativeFormat, QSettings::UserScope, legacySettingsOrg(), QStringLiteral("klusters"));
        old.setValue(QStringLiteral("autoShowMatricesOnOpen"), true);
        old.setValue(QStringLiteral("nbUndo"), 7);
        old.setValue(QStringLiteral("recent"), QStringList{QStringLiteral("a.clu.standard.1"), QStringLiteral("b.clu.standard.2")});
        old.setValue(QStringLiteral("Group/inner"), QStringLiteral("x"));
        old.sync();
        // An organization-wide file (shared by every "sourceforge" program):
        // a fallback, not the program's own settings.
        QSettings orgWide(QSettings::NativeFormat, QSettings::UserScope, legacySettingsOrg());
        orgWide.setValue(QStringLiteral("orgOnly"), 1);
        orgWide.sync();
        QSettings theme(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("neurosuite"), QStringLiteral("appearance"));
        theme.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
        theme.sync();
        QSettings nd(QSettings::NativeFormat, QSettings::UserScope, legacySettingsOrg(), QStringLiteral("ndmanager"));
        nd.setValue(QStringLiteral("lastDir"), QStringLiteral("/data"));
        nd.sync();
    }
    const QString oldKlustersData = appData(QStringLiteral("sourceforge"), QStringLiteral("klusters"));
    const QString oldNdData       = appData(QStringLiteral("sourceforge"), QStringLiteral("ndmanager"));
    check(writeFile(oldKlustersData + QStringLiteral("/plugins/descriptions/p.xml"), "<plugin/>"),
          "setup: an old Klusters plugin description");
    check(writeFile(oldNdData + QStringLiteral("/templates/rat.template.yaml"), "acquisitionSystem: {}\n"),
          "setup: an old NDManager session template");

    // ---- first start of the new Klusters -----------------------------------
    neurosuite::initApplicationIdentity(QStringLiteral("klusters"));
    QCoreApplication app(argc, argv);

    check(QCoreApplication::organizationName() == QStringLiteral("neurosuite-3"),
          "the organization is neurosuite-3");
    check(QCoreApplication::applicationName() == QStringLiteral("klusters"),
          "the application name is klusters");
    {
        QSettings s;   // what the program reads
        s.setFallbacksEnabled(false);
        check(s.value(QStringLiteral("autoShowMatricesOnOpen")).toBool(), "a bool setting came over");
        check(s.value(QStringLiteral("nbUndo")).toInt() == 7, "an int setting came over");
        check(s.value(QStringLiteral("recent")).toStringList().size() == 2, "a list setting came over");
        check(s.value(QStringLiteral("Group/inner")).toString() == QStringLiteral("x"), "a grouped setting came over");
        check(!s.contains(QStringLiteral("orgOnly")), "the old organization-wide file was not copied");
#ifdef Q_OS_LINUX
        check(s.fileName() == config + QStringLiteral("/neurosuite-3/klusters.conf"),
              "Klusters' file is ~/.config/neurosuite-3/klusters.conf (" + s.fileName().toStdString() + ")");
#endif
    }
    check(neurosuite::loadThemePreference() == neurosuite::Theme::Dark, "the theme came over");
#ifdef Q_OS_LINUX
    check(QFileInfo::exists(config + QStringLiteral("/neurosuite-3/appearance.conf")),
          "the theme is in ~/.config/neurosuite-3/appearance.conf");
#endif
    const QString newKlustersData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    check(readFile(newKlustersData + QStringLiteral("/plugins/descriptions/p.xml")) == "<plugin/>",
          "the plugin description came over");
#ifdef Q_OS_LINUX
    check(newKlustersData == data + QStringLiteral("/neurosuite-3/klusters"),
          "Klusters' data folder is ~/.local/share/neurosuite-3/klusters");
#endif
    {
        QSettings old(QSettings::NativeFormat, QSettings::UserScope, legacySettingsOrg(), QStringLiteral("klusters"));
        check(old.value(QStringLiteral("nbUndo")).toInt() == 7, "the old settings file is left in place");
        check(QFileInfo::exists(oldKlustersData + QStringLiteral("/plugins/descriptions/p.xml")),
              "the old data folder is left in place");
    }

    // ---- nothing in the new place is overwritten ----------------------------
    {
        QSettings s;
        s.setValue(QStringLiteral("nbUndo"), 3);           // changed in the new program
        s.sync();
        QSettings old(QSettings::NativeFormat, QSettings::UserScope, legacySettingsOrg(), QStringLiteral("klusters"));
        old.setValue(QStringLiteral("nbUndo"), 9);         // an old build still running
        old.setValue(QStringLiteral("newInOld"), 1);
        old.sync();
        writeFile(oldKlustersData + QStringLiteral("/plugins/descriptions/q.xml"), "<plugin2/>");
        const QStringList again = neurosuite::migrateLegacySettings();
        QSettings s2;
        s2.setFallbacksEnabled(false);
        check(again.isEmpty(), "a second start copies nothing");
        check(s2.value(QStringLiteral("nbUndo")).toInt() == 3, "a setting in the new place is not overwritten");
        check(!s2.contains(QStringLiteral("newInOld")), "settings are copied once, not merged later");
        check(!QFileInfo::exists(newKlustersData + QStringLiteral("/plugins/descriptions/q.xml")),
              "an existing data folder is not added to");
    }

    // ---- NDManager: its settings and its session templates -----------------
    neurosuite::initApplicationIdentity(QStringLiteral("ndmanager"));
    {
        QSettings s;
        s.setFallbacksEnabled(false);
        check(s.value(QStringLiteral("lastDir")).toString() == QStringLiteral("/data"),
              "NDManager's settings came over");
        check(!s.contains(QStringLiteral("nbUndo")), "NDManager did not get Klusters' settings");
        const QString newNdData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        check(readFile(newNdData + QStringLiteral("/templates/rat.template.yaml")) == "acquisitionSystem: {}\n",
              "NDManager's session template came over");
    }

    // ---- a program that never had settings: nothing to copy, no folder -----
    {
        const QStringList none = (neurosuite::initApplicationIdentity(QStringLiteral("neuroscope")),
                                  neurosuite::migrateLegacySettings());
        QSettings s;
        s.setFallbacksEnabled(false);
        check(none.isEmpty() && s.allKeys().isEmpty(), "NeuroScope with no old settings gets none");
        check(!QFileInfo::exists(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)),
              "and no data folder is created for it");
    }

    std::printf("\n%s (%d of %d checks failed)\n", g_fail ? "FAILED" : "ALL PASS", g_fail, g_ran);
    return g_fail ? 1 : 0;
}
