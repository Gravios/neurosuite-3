/***************************************************************************
 * actionicons_test.cpp — the action icon table (actionicons.h) against the
 * bundled resources.
 *
 * Built with klusters-icons.qrc and libklustersshared-icons.qrc compiled in,
 * so it checks the files the application actually ships:
 *
 *   - every row has an action id, a name, and a unique id;
 *   - every Bundled row's :/icons/<alias> and every Standard row's fallback
 *     :/shared-icons/<alias> exists and decodes to a non-null image;
 *   - every Drawn row has a painter, and its outline follows the palette
 *     (a light and a dark palette give different pixels);
 *   - an unknown drawn key gives a null icon.
 *
 * Bundled icons that no row uses are listed for information (preference-page
 * icons and retired tools are expected there), not failed.
 * Exits 0 on success, 1 on the first failed check.  Qt Gui, offscreen.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "actionicons.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPalette>
#include <QSet>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s [%s]\n", \
    __FILE__, __LINE__, #cond, qPrintable(what)); g_fail = 1; } } while (0)

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    using S = ActionIconSpec::Source;
    const auto rows = actionicons::table();
    CHECK(!rows.empty(), QStringLiteral("table"));

    QSet<QString> ids, usedAliases;
    for (const ActionIconSpec& r : rows) {
        const QString id = QLatin1String(r.actionId ? r.actionId : "");
        CHECK(!id.isEmpty() && r.name && *r.name, id);
        CHECK(!ids.contains(id), QStringLiteral("duplicate id ") + id);
        ids.insert(id);

        if (r.source == S::Drawn) {
            QPalette light; light.setColor(QPalette::WindowText, Qt::black);
            QPalette dark;  dark.setColor(QPalette::WindowText, Qt::white);
            const QIcon a = actionicons::drawnIcon(QLatin1String(r.name), light);
            const QIcon b = actionicons::drawnIcon(QLatin1String(r.name), dark);
            CHECK(!a.isNull(), QStringLiteral("no painter for ") + QLatin1String(r.name));
            CHECK(a.pixmap(44, 44).toImage() != b.pixmap(44, 44).toImage(),
                  QStringLiteral("drawn icon ignores the palette: ") + QLatin1String(r.name));
            CHECK(!actionicons::load(r, light).isNull(), id);
            continue;
        }
        const QString path = actionicons::resourcePath(r);
        CHECK(QFile::exists(path), id + QStringLiteral(" -> ") + path + QStringLiteral(" (not bundled)"));
        CHECK(!QImage(path).isNull(), id + QStringLiteral(" -> ") + path + QStringLiteral(" (does not decode)"));
        if (r.source == S::Bundled) usedAliases.insert(QLatin1String(r.name));
        // The loaded icon is never empty: the theme or the bundled fallback.
        CHECK(!actionicons::load(r, QPalette()).isNull(), id);
    }
    CHECK(actionicons::drawnIcon(QStringLiteral("no-such-icon"), QPalette()).isNull(),
          QStringLiteral("unknown drawn key"));

    // Information: bundled icons no action row uses.
    QStringList unused;
    for (const QString& f : QDir(QStringLiteral(":/icons")).entryList(QDir::Files))
        if (!usedAliases.contains(f)) unused << f;
    if (!unused.isEmpty())
        std::printf("actionicons_test: bundled icons used by no action row (info): %s\n",
                    qPrintable(unused.join(QStringLiteral(", "))));

    if (!g_fail) std::printf("actionicons_test: %d rows, all checks passed\n", int(rows.size()));
    return g_fail;
}
