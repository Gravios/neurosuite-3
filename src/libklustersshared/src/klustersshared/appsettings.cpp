/***************************************************************************
 *  appsettings.cpp  -  see appsettings.h
 ***************************************************************************/

#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>

namespace neurosuite {

namespace {

// The organization the programs used before neurosuite-3 had its own.
const char *const kLegacyOrganization = "sourceforge";

// The same, as QSettings named their settings with it: on macOS the plist
// was named after the organization domain the programs also set.
QString legacySettingsOrganization()
{
#ifdef Q_OS_DARWIN
    return QStringLiteral("sourceforge.net");
#else
    return QString::fromLatin1(kLegacyOrganization);
#endif
}

// Copies every key of `from` into `to` when `to` holds none.  Fallbacks are
// off on both, so only the user's own file is read and judged empty: a
// system-wide or organization-wide file is neither copied nor mistaken for
// settings already present.
bool copySettings(const QString &fromOrg, const QString &fromApp,
                  const QString &toOrg, const QString &toApp)
{
    QSettings from(QSettings::NativeFormat, QSettings::UserScope, fromOrg, fromApp);
    QSettings to(QSettings::NativeFormat, QSettings::UserScope, toOrg, toApp);
    from.setFallbacksEnabled(false);
    to.setFallbacksEnabled(false);
    const QStringList keys = from.allKeys();
    if (keys.isEmpty() || !to.allKeys().isEmpty())
        return false;
    for (const QString &key : keys)
        to.setValue(key, from.value(key));
    to.sync();
    return to.status() == QSettings::NoError;
}

// Copies the tree under `from` to `to`, which must not exist yet.
bool copyTree(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to))
        return false;
    const QDir src(from);
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    bool ok = true;
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString target = to + QLatin1Char('/') + src.relativeFilePath(path);
        if (info.isDir() && !info.isSymLink())
            ok = QDir().mkpath(target) && ok;
        else
            ok = QFile::copy(path, target) && ok;
    }
    return ok;
}

// The program's AppDataLocation as it is under `organization`.
QString appDataFor(const QString &organization)
{
    const QString current = QCoreApplication::organizationName();
    QCoreApplication::setOrganizationName(organization);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setOrganizationName(current);
    return dir;
}

} // namespace

QString settingsOrganization()
{
    return QStringLiteral("neurosuite-3");
}

QStringList migrateLegacySettings()
{
    QStringList done;
    const QString app = QCoreApplication::applicationName();
    const QString org = settingsOrganization();
    if (app.isEmpty())
        return done;

    if (copySettings(legacySettingsOrganization(), app, org, app))
        done << QStringLiteral("settings of %1 copied from organization \"%2\" to \"%3\"")
                    .arg(app, legacySettingsOrganization(), org);

    if (copySettings(QStringLiteral("neurosuite"), QStringLiteral("appearance"),
                     org, QStringLiteral("appearance")))
        done << QStringLiteral("theme copied from organization \"neurosuite\" to \"%1\"").arg(org);

    const QString oldData = appDataFor(QString::fromLatin1(kLegacyOrganization));
    const QString newData = appDataFor(org);
    if (!oldData.isEmpty() && !newData.isEmpty() && oldData != newData
        && QFileInfo(oldData).isDir() && !QFileInfo::exists(newData)) {
        if (copyTree(oldData, newData))
            done << QStringLiteral("data folder copied from %1 to %2").arg(oldData, newData);
        else
            done << QStringLiteral("data folder only partly copied from %1 to %2").arg(oldData, newData);
    }
    return done;
}

void initApplicationIdentity(const QString &applicationName)
{
    QCoreApplication::setOrganizationName(settingsOrganization());
    QCoreApplication::setApplicationName(applicationName);
    for (const QString &line : migrateLegacySettings())
        qInfo("%s: %s", qPrintable(applicationName), qPrintable(line));
}

} // namespace neurosuite
