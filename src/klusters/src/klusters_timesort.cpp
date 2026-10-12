/***************************************************************************
    klusters_timesort.cpp
    ---------------------
    KlustersApp "Sort Spikes by Time": confirm, rewrite the group's row-indexed
    files in time order (KlustersDoc::planTimeSort / applyTimeSort) and reopen
    the document so every in-memory table is rebuilt in the new row order.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"

#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

void KlustersApp::slotSortSpikesByTime()
{
    if (!doc || doc->url().isEmpty()) return;
    // The feature-space combo is locked during save / realign / recluster.
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish before sorting "
                                    "spikes by time."), 5000);
        return;
    }
    // The sort rewrites the saved files; unsaved edits (a pending realignment,
    // synced feature spaces, cluster changes) live elsewhere and would be lost.
    if (doc->isModified()) {
        QMessageBox::information(this, tr("Sort Spikes by Time"),
            tr("Save the document first: the sort rewrites the saved files and "
               "then reopens them."));
        return;
    }

    KlustersDoc::TimeSortPlan plan;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool planned = doc->planTimeSort(plan, err);
    QApplication::restoreOverrideCursor();
    if (!planned) {
        QMessageBox::warning(this, tr("Sort Spikes by Time"), err);
        return;
    }
    if (plan.nOutOfOrder == 0) {
        QMessageBox::information(this, tr("Sort Spikes by Time"),
                                 tr("The spikes of this group are already in time order."));
        return;
    }

    QStringList names;
    for (const KlustersDoc::RowFile& f : plan.files) names << QFileInfo(f.path).fileName();
    QString details = tr("Rewritten in time order:\n%1").arg(names.join(QLatin1Char('\n')));
    if (!plan.skipped.isEmpty())
        details += tr("\n\nLeft as is (not this group's spike count):\n%1")
                       .arg(plan.skipped.join(QLatin1Char('\n')));
    if (!plan.otherGroupFiles.isEmpty())
        details += tr("\n\nOther files of this group, not reordered (check none holds "
                      "one entry per spike):\n%1")
                       .arg(plan.otherGroupFiles.join(QLatin1Char('\n')));
    QString text = tr("%1 spike(s) are out of time order.  Sorting moves the same rows in "
                      "%2 file(s) of this group, then reopens the document; undo history "
                      "is cleared.")
                       .arg(plan.nOutOfOrder).arg(plan.files.size());
    if (!plan.notReordered.isEmpty())
        text += tr("\n\nNot reordered (regenerate them after sorting): %1")
                    .arg(plan.notReordered.join(QLatin1String(", ")));

    QMessageBox box(QMessageBox::Question, tr("Sort Spikes by Time"), text,
                    QMessageBox::Ok | QMessageBox::Cancel, this);
    box.setDetailedText(details);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Ok) return;

    // Close first: nothing then reads the files while they are swapped, and no
    // open document is left holding the old row order (whose working copies a
    // later Save would copy back over the sorted files).  A close that does
    // not complete (a job still stopping) sorts nothing.
    const QString url = doc->url();
    slotFileClose();
    if (mainDock) {
        statusBar()->showMessage(tr("The document could not be closed yet, so nothing was "
                                    "sorted.  Try again in a moment."), 8000);
        return;
    }

    QString log;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    slotStatusMsg(tr("Sorting spikes by time…"));
    const bool ok = doc->applyTimeSort(plan, log);   // works on the plan's files only
    QApplication::restoreOverrideCursor();

    openDocumentFile(url);
    if (!ok)
        QMessageBox::warning(this, tr("Sort Spikes by Time"), log.trimmed());
    else
        statusBar()->showMessage(tr("Spikes sorted by time; \"%1\" reopened.")
                                     .arg(QFileInfo(url).fileName()), 6000);
}
