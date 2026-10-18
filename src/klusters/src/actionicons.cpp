/***************************************************************************
 * actionicons.cpp — see actionicons.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "actionicons.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

#include <array>

namespace actionicons {

namespace {
using S = ActionIconSpec::Source;

// Grouped by menu, in menu order.  Standard rows give the freedesktop name first;
// a fallback is listed only where the bundled copy has another name.
constexpr std::array<ActionIconSpec, 25> kTable{{
    // File
    { "file.open",            S::Standard, "document-open" },
    { "file.save",            S::Standard, "document-save" },
    { "file.saveAs",          S::Standard, "document-save-as" },
    { "file.renumberAndSave", S::Standard, "document-save" },
    { "file.print",           S::Standard, "document-print" },
    { "file.close",           S::Standard, "document-close" },
    { "file.quit",            S::Standard, "application-exit", "window-close" },
    // Edit
    { "edit.undo",            S::Standard, "edit-undo" },
    { "edit.redo",            S::Standard, "edit-redo" },
    // Actions
    { "action.deleteArtifact",    S::Bundled, "delete-artifact-clusters" },
    { "action.deleteNoisy",       S::Bundled, "delete-noisy-clusters" },
    { "action.groupClusters",     S::Bundled, "group-clusters" },
    { "action.autoMerge",         S::Bundled, "auto-merge" },
    { "action.updateDisplay",     S::Bundled, "update-display" },
    { "action.updateErrorMatrix", S::Bundled, "update-error-matrix" },
    { "sort.bySimilarity",        S::Bundled, "reorder-by-similarity" },
    // Tools
    { "menu:Tools/New Cluster",            S::Bundled, "new-cluster" },
    { "menu:Tools/Split Clusters",         S::Bundled, "split-clusters" },
    { "menu:Tools/Multi-Lasso",            S::Drawn,   "multi-lasso" },
    { "menu:Tools/Split by Lassos",        S::Drawn,   "split-by-lassos" },
    { "menu:Tools/Delete Artifact Spikes", S::Bundled, "delete-artifact-spikes" },
    { "menu:Tools/Delete Noisy Spikes",    S::Bundled, "delete-noisy-spikes" },
    // Traces
    { "trace.nextSpike",      S::Bundled, "next-spike" },
    { "trace.previousSpike",  S::Bundled, "previous-spike" },
    // Settings
    { "menu:Settings/Preferences", S::Standard, "configure" },
}};

// The two multi-lasso icons: two overlapping loops, dashed for the tool, filled in
// three colours for the split (one per overlap pattern).  The fills are fixed hues
// (they stand for clusters); the outline is the palette's text colour, so the loops
// read on a light and on a dark toolbar.
QIcon paintLassos(bool filled, const QPalette& palette)
{
    QPixmap pm(44, 44);
    pm.fill(Qt::transparent);
    QPainter ip(&pm);
    ip.setRenderHint(QPainter::Antialiasing);
    const QRectF a(4, 8, 24, 22), b(16, 14, 24, 22);
    if (filled) {
        QPainterPath pa; pa.addEllipse(a);
        QPainterPath pb; pb.addEllipse(b);
        ip.setPen(Qt::NoPen);
        ip.setBrush(QColor(255, 200, 40, 200)); ip.drawPath(pa.subtracted(pb));
        ip.setBrush(QColor(60, 220, 255, 200)); ip.drawPath(pb.subtracted(pa));
        ip.setBrush(QColor(255, 90, 200, 220)); ip.drawPath(pa.intersected(pb));
    }
    QColor outline = palette.color(QPalette::Active, QPalette::WindowText);
    outline.setAlpha(190);
    QPen pen(outline);
    pen.setWidthF(2.5);
    pen.setStyle(filled ? Qt::SolidLine : Qt::DashLine);
    ip.setPen(pen);
    ip.setBrush(Qt::NoBrush);
    ip.drawEllipse(a);
    ip.drawEllipse(b);
    return QIcon(pm);
}
} // namespace

std::span<const ActionIconSpec> table()
{
    return kTable;
}

QIcon drawnIcon(const QString& key, const QPalette& palette)
{
    if (key == QLatin1String("multi-lasso"))     return paintLassos(false, palette);
    if (key == QLatin1String("split-by-lassos")) return paintLassos(true, palette);
    return QIcon();
}

QString resourcePath(const ActionIconSpec& spec)
{
    switch (spec.source) {
    case S::Standard:
        return QStringLiteral(":/shared-icons/")
             + QLatin1String(spec.fallback ? spec.fallback : spec.name);
    case S::Bundled:
        return QStringLiteral(":/icons/") + QLatin1String(spec.name);
    case S::Drawn:
        break;
    }
    return QString();
}

QIcon load(const ActionIconSpec& spec, const QPalette& palette)
{
    switch (spec.source) {
    case S::Standard:
        return QIcon::fromTheme(QLatin1String(spec.name), QIcon(resourcePath(spec)));
    case S::Bundled:
        return QIcon(resourcePath(spec));
    case S::Drawn:
        return drawnIcon(QLatin1String(spec.name), palette);
    }
    return QIcon();
}

} // namespace actionicons
