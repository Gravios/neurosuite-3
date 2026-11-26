/***************************************************************************
    klusters_collisions.cpp
    -----------------------
    KlustersApp "Mark Collisions": search the selected clusters (or the
    children selected in the child view) for rows that hold a second unit's
    spike, show what was found, and on confirmation name the partners in the
    stage's membership (KlustersDoc::scanCollisions / markCollisions).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "clusterpalette.h"
#include "configuration.h"

#include <QApplication>
#include <QComboBox>
#include <QMessageBox>
#include <QStatusBar>

#include <algorithm>

void KlustersApp::slotMarkCollisions()
{
    if (!doc || doc->url().isEmpty()) return;
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish before marking collisions."), 5000);
        return;
    }
    KlustersView* view = activeView();
    if (!view) return;
    // The selection, in the layer on screen: the child palette's while the child view is
    // active, the parent clusters shown otherwise.
    const bool childScope = doc->isChildClusteringActive();
    QList<int> selected = (childScope && childPaletteA) ? childPaletteA->selectedClusters() : view->clusters();
    selected.removeAll(0);
    selected.removeAll(1);
    if (!childScope) doc->leaveOutReserved(selected, tr("Mark collisions"));
    if (selected.isEmpty()) {
        slotStatusMsg(tr("Mark collisions: select one or more clusters first."));
        return;
    }

    constexpr double kFalseRate = 0.01;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    slotStatusMsg(tr("Searching the selection for collisions…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const KlustersDoc::CollisionScan R = doc->scanCollisions(selected, childScope, kFalseRate);
    QApplication::restoreOverrideCursor();
    if (!R.ok) {
        slotStatusMsg(tr("Mark collisions: %1").arg(R.reason));
        return;
    }
    if (R.hits.empty()) {
        slotStatusMsg(tr("Mark collisions: none found in the %1 rows searched (%2 were over the threshold, "
                         "the control %3).").arg(R.scanned).arg(R.candidates).arg(R.controlOver));
        return;
    }

    // What was found, by selected cluster and by partner.
    QStringList lines;
    QList<int> ids = R.rowsOf.keys();
    std::sort(ids.begin(), ids.end(), [&](int a, int b) { return R.hitsOf.value(a) > R.hitsOf.value(b); });
    for (int i = 0; i < ids.size() && i < 12; ++i) {
        const qint64 n = R.rowsOf.value(ids[i]);
        if (n == 0) continue;
        lines << tr("%1 %2: %3 of %4 rows (%5 %), control %6")
                     .arg(childScope ? tr("child") : tr("cluster")).arg(ids[i]).arg(R.hitsOf.value(ids[i])).arg(n)
                     .arg(100.0 * static_cast<double>(R.hitsOf.value(ids[i])) / static_cast<double>(n), 0, 'f', 1)
                     .arg(R.controlOf.value(ids[i]));
    }
    if (ids.size() > 12) lines << tr("… and %1 more").arg(ids.size() - 12);
    QHash<int, qint64> byPartner;
    for (const KlustersDoc::CollisionHit& h : R.hits) ++byPartner[h.partner];
    QList<int> partners = byPartner.keys();
    std::sort(partners.begin(), partners.end(), [&](int a, int b) { return byPartner.value(a) > byPartner.value(b); });
    QStringList who;
    for (int i = 0; i < partners.size() && i < 8; ++i) who << tr("%1: %2").arg(partners[i]).arg(byPartner.value(partners[i]));
    QStringList notes;
    for (auto it = R.lookAlikes.constBegin(); it != R.lookAlikes.constEnd(); ++it) {
        QStringList alike;
        for (const int c : it.value()) alike << QString::number(c);
        notes << tr("Not tried as partners of cluster %1, too like it: %2.").arg(it.key()).arg(alike.join(QStringLiteral(", ")));
    }
    if (R.alreadyNamed > 0) notes << tr("%1 rows already name a partner and were left out.").arg(R.alreadyNamed);
    if (!configuration().getCorrectCollisionRows())
        notes << tr("Preferences > Cluster view > Subtract the partner from collision rows is off: the rows "
                    "are marked but shown as they are until it is turned on.");

    QMessageBox box(QMessageBox::Question, tr("Mark Collisions"),
        tr("%1 of the %2 rows searched hold a second cluster's spike.  Mark them?")
            .arg(R.hits.size()).arg(R.scanned),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(
        tr("A marked row names its partner in the stage's membership (.eap).  It is then shown, averaged "
           "and realigned with the partner's template subtracted; the .spk and the clustering are not "
           "changed.  The marks are written at Save and dropped if the session is closed without saving; "
           "Undo does not take them back.\n\n%1\n\nPartners (cluster: rows): %2.\n\n"
           "The control is the same search with the partners' templates negated: rows it puts over the "
           "threshold (%3 of %4 in all) are what noise and misfit alone give.%5")
            .arg(lines.join(QLatin1Char('\n')), who.join(QStringLiteral(", ")))
            .arg(R.controlOver).arg(R.scanned)
            .arg(notes.isEmpty() ? QString() : QStringLiteral("\n\n") + notes.join(QLatin1Char('\n'))));
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const KlustersDoc::CollisionMarks M = doc->markCollisions(R.hits);
    QApplication::restoreOverrideCursor();
    if (!M.ok) {
        QMessageBox::warning(this, tr("Mark Collisions"), tr("Nothing was marked: %1").arg(M.error));
        return;
    }
    QString left;
    if (M.strayRows > 0) left += tr("  %1 of them named another cluster's class and now name their own.").arg(M.strayRows);
    if (M.withoutClass > 0) left += tr("  %1 had a cluster that could be given no class.").arg(M.withoutClass);
    slotStatusMsg(tr("Marked %1 collision rows (%2 template classes made).  Written at Save.%3")
                      .arg(M.marked).arg(M.classesCreated).arg(left));
}
