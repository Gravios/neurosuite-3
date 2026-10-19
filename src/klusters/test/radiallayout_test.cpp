/***************************************************************************
 * radiallayout_test.cpp — the radial menu's layout model (radiallayout.h).
 *
 * JSON round-trip of the shipped and of an edited layout, the settings
 * fallback to the defaults, rejection of malformed / wrong-version text,
 * skipping of unknown node kinds, ring counting, and the menu-path id rules.
 * Exits 0 on success, 1 on the first failed check.  Qt Core only.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "radiallayout.h"

#include <QSet>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

using N = RadialLayoutNode;

static void collectIds(const RadialLayout& l, QList<QString>& ids)
{
    for (const N& n : l) {
        if (n.kind == N::Kind::Group) collectIds(n.children, ids);
        else ids << n.id;
    }
}

int main()
{
    // ---- the shipped layout -------------------------------------------------
    const RadialLayout def = radiallayout::defaults();
    CHECK(def.size() == 10);                                  // 7 groups/actions + X + Y + N
    CHECK(def.first().kind == N::Kind::Group && def.first().label == QStringLiteral("Tools"));
    CHECK(def.at(7).kind == N::Kind::Value && def.at(7).id == QStringLiteral("dimX"));
    CHECK(def.at(8).kind == N::Kind::Value && def.at(8).id == QStringLiteral("dimY"));
    CHECK(def.last().kind == N::Kind::Value && def.last().id == QStringLiteral("nFeat"));
    CHECK(radiallayout::ringsNeeded(def) == 2);
    {
        QList<QString> ids; collectIds(def, ids);
        CHECK(QSet<QString>(ids.begin(), ids.end()).size() == ids.size());   // no duplicates
    }

    // ---- JSON round trip ----------------------------------------------------
    {
        RadialLayout back;
        QString err;
        CHECK(radiallayout::fromJson(radiallayout::toJson(def), &back, &err));
        CHECK(back == def);
    }
    {
        // An edited layout: nested groups, a short label, a value prefix.
        const RadialLayout edited = {
            N::group(QStringLiteral("A"), {
                N::group(QStringLiteral("B"), {
                    N::group(QStringLiteral("C"), { N::action(QStringLiteral("edit.undo"), QStringLiteral("U")) }),
                }),
            }),
            N::value(QStringLiteral("dimX"), QStringLiteral("Abscissa")),
            N::action(QStringLiteral("menu:Tools/New Cluster")),
        };
        RadialLayout back;
        CHECK(radiallayout::fromJson(radiallayout::toJson(edited), &back));
        CHECK(back == edited);
        CHECK(radiallayout::ringsNeeded(edited) == 4);
        CHECK(radiallayout::ringsNeeded({}) == 0);
        CHECK(radiallayout::ringsNeeded({ N::group(QStringLiteral("empty"), {}) }) == 1);
    }

    // ---- rejection, fallback, unknown kinds -----------------------------------
    {
        RadialLayout keep = { N::action(QStringLiteral("x")) };
        QString err;
        CHECK(!radiallayout::fromJson(QStringLiteral("{not json"), &keep, &err) && !err.isEmpty());
        CHECK(keep.size() == 1);                                  // untouched on failure
        CHECK(!radiallayout::fromJson(QStringLiteral("[1,2]"), &keep));
        CHECK(!radiallayout::fromJson(QStringLiteral("{\"version\":2,\"items\":[]}"), &keep));
        CHECK(radiallayout::fromSettings(QString()) == def);
        CHECK(radiallayout::fromSettings(QStringLiteral("garbage")) == def);

        RadialLayout l;
        CHECK(radiallayout::fromJson(QStringLiteral(
            "{\"version\":1,\"items\":[{\"separator\":true},{\"action\":\"edit.redo\"},{\"action\":\"\"}]}"), &l));
        CHECK(l.size() == 1 && l.first().id == QStringLiteral("edit.redo"));

        // An empty, valid layout is kept as empty (the user removed everything).
        CHECK(radiallayout::fromSettings(QStringLiteral("{\"version\":1,\"items\":[]}")).isEmpty());
    }

    // ---- ids -----------------------------------------------------------------
    CHECK(actionid::plainText(QStringLiteral("&Split Clusters")) == QStringLiteral("Split Clusters"));
    CHECK(actionid::plainText(QStringLiteral("R&&D...")) == QStringLiteral("R&D"));
    CHECK(actionid::plainText(QString::fromUtf8("Save As…")) == QStringLiteral("Save As"));
    CHECK(actionid::menuPathId({ QStringLiteral("&Actions"), QStringLiteral("Sort Clusters") },
                                   QStringLiteral("Sort Clusters by Largest Amplitude by C&hannel"))
          == QStringLiteral("menu:Actions/Sort Clusters/Sort Clusters by Largest Amplitude by Channel"));

    if (!g_fail) std::printf("radiallayout_test: all checks passed\n");
    return g_fail;
}
