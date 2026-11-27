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
#include <QMap>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>

#include <algorithm>

namespace {
/** The selection Mark Collisions and its companions work on: the child palette's while the child
 *  view is active, the parent clusters shown otherwise; never the artefact and noise bins. */
QList<int> collisionSelection(KlustersDoc* doc, KlustersView* view, ClusterPalette* childPalette, bool& childScope)
{
    childScope = doc->isChildClusteringActive();
    QList<int> selected = (childScope && childPalette) ? childPalette->selectedClusters() : view->clusters();
    selected.removeAll(0);
    selected.removeAll(1);
    return selected;
}
} // namespace

void KlustersApp::slotMarkCollisions()
{
    if (!doc || doc->url().isEmpty()) return;
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish before marking collisions."), 5000);
        return;
    }
    KlustersView* view = activeView();
    if (!view) return;
    bool childScope = false;
    QList<int> selected = collisionSelection(doc, view, childPaletteA, childScope);
    if (!childScope) doc->leaveOutReserved(selected, tr("Mark collisions"));
    if (selected.isEmpty()) {
        slotStatusMsg(tr("Mark collisions: select one or more clusters first."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    slotStatusMsg(tr("Searching the selection for collisions…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const KlustersDoc::CollisionScan R = doc->scanCollisions(selected, childScope);
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
    for (int i = 0; i < partners.size() && i < 8; ++i)
        who << tr("%1: %2 (%3)").arg(partners[i]).arg(byPartner.value(partners[i]))
                   .arg(R.expectedOf.value(partners[i]), 0, 'f', R.expectedOf.value(partners[i]) < 10.0 ? 1 : 0);
    QStringList notes;
    for (auto it = R.lookAlikes.constBegin(); it != R.lookAlikes.constEnd(); ++it) {
        QStringList alike;
        for (const int c : it.value()) alike << QString::number(c);
        notes << tr("Not tried as partners of cluster %1, too like it: %2.").arg(it.key()).arg(alike.join(QStringLiteral(", ")));
    }
    if (R.notPresent > 0)
        notes << tr("A cluster is tried as a partner only where it has spikes of its own within 5 minutes of the row.");
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
           "Undo does not take them back.\n\n%1\n\nPartners (cluster: rows, and in brackets the rows two independently firing units "
           "would share by chance): %2.  A partner named many times more often than chance is more "
           "likely a misfit than a partner.\n\n"
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

QList<int> KlustersApp::collisionRowsToChildren(const QList<int>& ids, bool children, QString& summary)
{
    QList<int> created;
    QString err;
    if (!doc->loadChildClustering(err)) {
        summary = tr("Collision rows to a child: the session has no child layer (%1). "
                     "The rows stay in their clusters; Hierarchy > Promote to Hierarchical Session gives it one.").arg(err);
        return created;
    }
    const std::vector<qint64> rows = doc->collisionRowsOf(ids, children);
    if (rows.empty()) { summary = tr("Collision rows to a child: the selection holds no row that names a partner."); return created; }
    // One new child per parent, holding that parent's collision rows whichever of its children
    // they were in.  A row never leaves its parent.
    const QVector<dataType> parentOfRow = doc->parentData().labelByFeatureRow();
    QMap<int, QSet<dataType>> byParent;
    for (const qint64 r : rows) {
        const int row = static_cast<int>(r) + 1;                       // 0-based .spk -> 1-based feature row
        if (row <= 0 || row >= parentOfRow.size()) continue;
        const int parent = static_cast<int>(parentOfRow.at(row));
        if (parent > 1) byParent[parent].insert(static_cast<dataType>(row));
    }
    int moved = 0, already = 0;
    for (auto it = byParent.constBegin(); it != byParent.constEnd(); ++it) {
        // Already a child of their own: every row of the children they are in is a collision row.
        doc->setActiveClustering(true);
        const QVector<dataType> childOfRow = doc->data().labelByFeatureRow();
        QSet<int> kids;
        for (const dataType row : it.value()) kids.insert(static_cast<int>(childOfRow.at(static_cast<int>(row))));
        qint64 held = 0;
        for (const int k : kids) held += static_cast<qint64>(doc->childClusterData().nbOfSpikes(static_cast<dataType>(k)));
        if (kids.size() == 1 && held == it.value().size()) { already += it.value().size(); continue; }
        const int made = splitOffBurstFollowers(it.value(), /*children*/ true, QStringLiteral("collision_rows"));
        if (made > 0) { created.append(made); moved += it.value().size(); }
    }
    summary = tr("Collision rows to a child: %1 rows of %2 cluster(s) are now %3 child(ren) of their own%4.")
                  .arg(moved).arg(byParent.size()).arg(created.size())
                  .arg(already > 0 ? tr(" (%1 were one already)").arg(already) : QString());
    return created;
}

void KlustersApp::slotCollisionRowsToChild()
{
    if (!doc || doc->url().isEmpty()) return;
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish first."), 5000);
        return;
    }
    KlustersView* view = activeView();
    if (!view) return;
    bool childScope = false;
    const QList<int> selected = collisionSelection(doc, view, childPaletteA, childScope);
    if (selected.isEmpty()) { slotStatusMsg(tr("Collision rows to a child: select one or more clusters first.")); return; }
    QString summary;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    collisionRowsToChildren(selected, childScope, summary);
    QApplication::restoreOverrideCursor();
    slotStatusMsg(summary);
}

void KlustersApp::slotRemoveCollisionMarks()
{
    if (!doc || doc->url().isEmpty()) return;
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish first."), 5000);
        return;
    }
    KlustersView* view = activeView();
    if (!view) return;
    bool childScope = false;
    const QList<int> selected = collisionSelection(doc, view, childPaletteA, childScope);
    if (selected.isEmpty()) { slotStatusMsg(tr("Remove collision marks: select one or more clusters first.")); return; }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<qint64> rows = doc->collisionRowsOf(selected, childScope);
    QApplication::restoreOverrideCursor();
    if (rows.empty()) { slotStatusMsg(tr("Remove collision marks: the selection holds no row that names a partner.")); return; }
    QMessageBox box(QMessageBox::Question, tr("Remove Collision Marks"),
        tr("%1 row(s) of the selected %2 name a partner.  Take the partners out?")
            .arg(rows.size()).arg(childScope ? tr("children") : tr("clusters")),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(
        tr("Each row keeps its own cluster's class and is shown as the .spk holds it again.  This "
           "takes out every partner these rows name, whoever named it -- Mark Collisions or the sort.  "
           "It is written at Save and dropped if the session is closed without saving; Undo does not "
           "bring the marks back (Mark Collisions finds them again)."));
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const int changed = doc->removeCollisionMarks(rows);
    QApplication::restoreOverrideCursor();
    slotStatusMsg(tr("Removed the partners of %1 row(s).  Written at Save.").arg(changed));
}

void KlustersApp::slotResidualWaveforms()
{
    if (!doc || doc->url().isEmpty()) { if (mResidualWaveforms) mResidualWaveforms->setChecked(false); return; }
    const bool on = mResidualWaveforms && mResidualWaveforms->isChecked();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const KlustersDoc::ResidualReport R = doc->showResiduals(on);
    QApplication::restoreOverrideCursor();
    if (!on) { slotStatusMsg(tr("Residual display off: every row is shown as it is.")); return; }
    if (!R.ok || doc->residualCount() == 0) {
        mResidualWaveforms->setChecked(false);
        doc->showResiduals(false);
        slotStatusMsg(tr("Residual display: %1").arg(!R.reason.isEmpty() ? R.reason
                         : tr("no row has a fitted partner (Clusters > Mark Collisions finds them).")));
        return;
    }
    slotStatusMsg(tr("Residual display on: %1 collision rows shown as their partner's spike (%2).  %3 are spikes no row "
                     "holds, %4 the partner's cluster already has, %5 repeat another row's.%6")
                      .arg(doc->residualCount())
                      .arg(R.fromRecording ? tr("cut from %1, centred on the partner").arg(R.source)
                                           : tr("from %1, the row's own window: no recording to cut").arg(R.source))
                      .arg(R.recovered).arg(R.detected).arg(R.repeated)
                      .arg(R.withoutNeighbours > 0 ? tr("  %1 rows have too few clean neighbours.").arg(R.withoutNeighbours) : QString()));
}

void KlustersApp::slotRemoveDuplicateSpikes()
{
    if (!doc || doc->url().isEmpty()) return;
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish first."), 5000);
        return;
    }
    KlustersView* view = activeView();
    if (!view) return;
    bool childScope = false;
    QList<int> selected = collisionSelection(doc, view, childPaletteA, childScope);
    if (!childScope) doc->leaveOutReserved(selected, tr("Remove duplicate spikes"));
    if (selected.isEmpty()) { slotStatusMsg(tr("Remove duplicate spikes: select one or more clusters first.")); return; }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const KlustersDoc::DuplicateScan R = doc->scanDuplicateSpikes(selected, childScope);
    QApplication::restoreOverrideCursor();
    if (!R.ok) { slotStatusMsg(tr("Remove duplicate spikes: %1").arg(R.reason)); return; }
    QStringList across;
    {
        QStringList keys = R.acrossPairs.keys();
        std::sort(keys.begin(), keys.end(), [&](const QString& x, const QString& y) { return R.acrossPairs.value(x) > R.acrossPairs.value(y); });
        for (int i = 0; i < keys.size() && i < 6; ++i) across << tr("%1: %2").arg(keys[i]).arg(R.acrossPairs.value(keys[i]));
    }
    const QString acrossNote = R.acrossClusters > 0
        ? tr("%1 pair(s) of rows this near in time are in two DIFFERENT clusters (%2).  They are one spike "
             "sorted twice or a collision of two units, and are left as they are.").arg(R.acrossClusters).arg(across.join(QStringLiteral(", ")))
        : QString();
    if (R.extras.isEmpty()) {
        slotStatusMsg(tr("Remove duplicate spikes: none in the %1 rows of the selection (within %2 samples).%3")
                          .arg(R.scanned).arg(R.toleranceSamples).arg(acrossNote.isEmpty() ? QString() : QStringLiteral("  ") + acrossNote));
        return;
    }
    QStringList lines;
    QList<int> ids = R.extrasOf.keys();
    std::sort(ids.begin(), ids.end(), [&](int x, int y) { return R.extrasOf.value(x) > R.extrasOf.value(y); });
    for (int i = 0; i < ids.size() && i < 12; ++i)
        lines << tr("%1 %2: %3 row(s) too many").arg(childScope ? tr("child") : tr("cluster")).arg(ids[i]).arg(R.extrasOf.value(ids[i]));
    if (ids.size() > 12) lines << tr("… and %1 more").arg(ids.size() - 12);
    QMessageBox box(QMessageBox::Question, tr("Remove Duplicate Spikes"),
        tr("%1 spike(s) are held by more than one row.  Move the %2 row(s) too many to the artefact cluster?")
            .arg(R.runs).arg(R.extras.size()),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(
        tr("Rows of one cluster within %1 samples (0.2 ms) of each other are one spike detected more than "
           "once; %2 of the %3 have the very same time.  Of each, the row most like the cluster's template "
           "is kept.  The others go to cluster 0 and can be brought back with Undo.\n\n%4%5")
            .arg(R.toleranceSamples).arg(R.atOneTime).arg(R.runs).arg(lines.join(QLatin1Char('\n')))
            .arg(acrossNote.isEmpty() ? QString() : QStringLiteral("\n\n") + acrossNote));
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;
    doc->deleteArtifact(SpikeSelection(R.extras, QStringLiteral("duplicate_spikes")), selected);
    slotStatusMsg(tr("Moved %1 duplicate row(s) of %2 spike(s) to the artefact cluster.").arg(R.extras.size()).arg(R.runs));
}
