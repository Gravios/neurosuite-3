/***************************************************************************
 * probepage.cpp
 *
 * Copyright (C) 2025 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "probepage.h"

#include <klustersshared/parameteryamlreader_probes.h>

#include <yaml-cpp/yaml.h>

#include <QFileDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QPixmap>
#ifdef ND_HAVE_QT_SVG
#  include <QSvgRenderer>
#  include <QPainter>
#endif
#include <QDebug>
#include <QRegularExpression>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLabel>

#include "probemakerpage.h"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ProbePage::ProbePage(QWidget* parent)
    : ProbeLayout(parent)
    , m_modified(false)
{
    probeTable->setColumnCount(6);
    probeTable->horizontalHeader()->setSectionResizeMode(ColId,          QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(ColFile,        QHeaderView::Stretch);
    probeTable->horizontalHeader()->setSectionResizeMode(ColLabel,       QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(ColOffset,      QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(ColGroups,      QHeaderView::Stretch);
    probeTable->horizontalHeader()->setSectionResizeMode(ColSpikeGroups, QHeaderView::Stretch);

    probeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    probeTable->setSelectionMode(QAbstractItemView::SingleSelection);

    QStringList dataDirs = QStandardPaths::standardLocations(
        QStandardPaths::GenericDataLocation);
    if (!dataDirs.isEmpty())
        m_libraryPath = dataDirs.first() + QStringLiteral("/neurosuite/probes");

    libraryPathEdit->setPlaceholderText(m_libraryPath + QStringLiteral("  (default)"));

    connect(addProbeButton,      &QPushButton::clicked, this, &ProbePage::addProbe);
    connect(removeProbeButton,   &QPushButton::clicked, this, &ProbePage::removeProbe);
    connect(moveUpButton,        &QPushButton::clicked, this, &ProbePage::moveProbeUp);
    connect(moveDownButton,      &QPushButton::clicked, this, &ProbePage::moveProbeDown);
    connect(browseProbeButton,   &QPushButton::clicked, this, &ProbePage::browseProbeFile);
    connect(browseLibraryButton, &QPushButton::clicked, this, &ProbePage::browseLibraryPath);
    connect(probeTable, &QTableWidget::cellChanged,
            this, &ProbePage::cellEdited);
    connect(probeTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex&, const QModelIndex&) { rowSelected(); });

    // ── Embedded probe-geometry editor ───────────────────────────────────
    // The original layout (probelayout.ui) carried a `diagramGroupBox`
    // containing a static QLabel preview ("diagramLabel").  We keep
    // diagramLabel as the no-selection placeholder, and add a
    // QStackedWidget alongside it so we can swap to an interactive
    // ProbeMakerPage when a row is selected and a probe file is loaded.
    //
    // Page 0 = diagramLabel (placeholder, the original behaviour)
    // Page 1 = m_probeMaker (interactive geometry editor)
    qDebug() << "[ProbeMaker] ctor: diagramGroupBox=" << (void*)diagramGroupBox
             << "diagramLayout=" << (void*)diagramLayout
             << "diagramLabel=" << (void*)diagramLabel;
    if (diagramGroupBox && diagramLayout) {
        m_makerStack = new QStackedWidget(diagramGroupBox);
        m_probeMaker = new ProbeMakerPage(diagramGroupBox);
        qDebug() << "[ProbeMaker] ctor: created stack=" << (void*)m_makerStack
                 << "probeMaker=" << (void*)m_probeMaker;

        // diagramLabel was already added to diagramLayout by the
        // .ui-generated setup; move it into the stack so the existing
        // placeholder text continues to work without rewriting the .ui.
        if (diagramLabel) {
            diagramLayout->removeWidget(diagramLabel);
            m_makerStack->addWidget(diagramLabel);   // index 0
            qDebug() << "[ProbeMaker] ctor: added diagramLabel as stack page 0";
        }
        m_makerStack->addWidget(m_probeMaker);       // index 1
        m_makerStack->setCurrentIndex(0);
        diagramLayout->addWidget(m_makerStack);
        qDebug() << "[ProbeMaker] ctor: stack added to diagramLayout, currentIndex=0"
                 << "stack count=" << m_makerStack->count();

        // Whenever the maker reports an edit, persist it back to the
        // session's <session>.probe.<probeId>.probe file and update
        // the table's File column.
        connect(m_probeMaker, &ProbeMakerPage::modified,
                this, &ProbePage::saveMakerToCurrentRow);
    } else {
        qWarning() << "[ProbeMaker] ctor: diagramGroupBox or diagramLayout missing"
                   << "— maker NOT instantiated!";
    }
}

// ---------------------------------------------------------------------------
// setProbes / getProbes
// ---------------------------------------------------------------------------

void ProbePage::setProbes(const QList<ProbeEntry>& probes)
{
    qDebug() << "[ProbeMaker] setProbes: populating" << probes.size() << "rows";
    probeTable->blockSignals(true);
    probeTable->setRowCount(0);
    for (const ProbeEntry& entry : probes) {
        int row = probeTable->rowCount();
        probeTable->insertRow(row);
        populateRow(row, entry);
        qDebug() << "[ProbeMaker] setProbes: row" << row
                 << "id=" << entry.id
                 << "probeFile=" << entry.probeFile;
    }
    probeTable->blockSignals(false);
    m_modified = false;
}

void ProbePage::getProbes(QList<ProbeEntry>& probes) const
{
    probes.clear();
    for (int row = 0; row < probeTable->rowCount(); ++row)
        probes.append(rowToEntry(row));
}

void ProbePage::setLibraryPath(const QString& path)
{
    m_libraryPath = path;
    libraryPathEdit->setText(path);
}

QString ProbePage::getLibraryPath() const
{
    QString txt = libraryPathEdit->text().trimmed();
    return txt.isEmpty() ? m_libraryPath : txt;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// copyProbeIntoSession
// ---------------------------------------------------------------------------
// Copy the probe file at @p srcPath into the current working directory
// (which, by convention, is the session directory — ndmanager is launched
// from there).  Returns the bare filename the caller should store in the
// probe table's file column, or an empty string on failure / user cancel.
//
// When @p probeId is non-negative, the destination is renamed to
//   <session>.probe.<probeId>.probe
// so the file is unambiguously tied to the row whose ID is probeId.
// When @p probeId is -1, the source basename is preserved (legacy
// behaviour; used only by transitional callers that don't yet know
// the row's ID — currently none).
//
// If the source canonical path equals the destination, nothing is
// copied and the bare destination filename is returned.  If a
// different file already exists at the destination, the user is asked
// whether to overwrite, reuse, or cancel.
QString ProbePage::copyProbeIntoSession(const QString& srcPath, int probeId)
{
    const QFileInfo srcInfo(srcPath);
    const QString   baseName   = (probeId >= 0)
                                  ? sessionProbeFilename(probeId)
                                  : srcInfo.fileName();
    const QDir      sessionDir = QDir::current();
    const QString   localPath  = sessionDir.absoluteFilePath(baseName);

    // Source already at the destination?  Nothing to copy.
    if (QFileInfo(srcPath).canonicalFilePath()
        == QFileInfo(localPath).canonicalFilePath())
    {
        return baseName;
    }

    if (QFile::exists(localPath)) {
        const auto reply = QMessageBox::question(
            this, tr("Probe file exists"),
            tr("A probe file named\n\n    %1\n\nalready exists in the "
               "session directory.  Overwrite it with the selected "
               "file?\n\nChoose \"No\" to reuse the existing local "
               "copy.").arg(baseName),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::No);
        if (reply == QMessageBox::Cancel)
            return QString();
        if (reply == QMessageBox::No)
            return baseName;        // reuse existing local copy
        if (!QFile::remove(localPath)) {
            QMessageBox::warning(this, tr("Copy Failed"),
                tr("Cannot remove existing %1").arg(localPath));
            return QString();
        }
    }

    if (!QFile::copy(srcPath, localPath)) {
        QMessageBox::warning(this, tr("Copy Failed"),
            tr("Cannot copy\n  %1\ninto the session directory"
               "\n  %2").arg(srcPath, sessionDir.absolutePath()));
        return QString();
    }
    return baseName;
}

// ---------------------------------------------------------------------------
// sessionProbeFilename
// ---------------------------------------------------------------------------
// The local filename a probe with @p probeId should have in the
// session directory: <session>.probe.<probeId>.probe where <session>
// is the current working directory's basename.  Used by both Browse
// (to rename imported files) and by the embedded Probe Maker (to
// place its serialised output).
QString ProbePage::sessionProbeFilename(int probeId)
{
    const QString sessionStem = QDir::current().dirName();
    return QStringLiteral("%1.probe.%2.probe")
            .arg(sessionStem)
            .arg(probeId);
}

void ProbePage::addProbe()
{
    // Open a file picker immediately.  The historical flow was:
    //   1. click + → inserts empty row
    //   2. click Browse → opens picker, fills row's ColFile
    // merged here into one step: + opens the picker directly.  On accept
    // the selected probe file is copied into the session directory (cwd,
    // since ndmanager is launched from the session directory) and the
    // new row references the local copy — making the session
    // self-contained and portable to other machines.

    const QString srcPath = QFileDialog::getOpenFileName(
        this,
        tr("Select Probe Configuration File"),
        getLibraryPath(),
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));
    if (srcPath.isEmpty())
        return;     // user cancelled — no new row added

    const int row = probeTable->rowCount();
    // Copy first using the new row's eventual ID so the file gets the
    // canonical <session>.probe.<row>.probe name from the start.
    const QString localName = copyProbeIntoSession(srcPath, row);
    if (localName.isEmpty())
        return;     // copy failed or user cancelled the overwrite dialog

    probeTable->insertRow(row);
    ProbeEntry blank;
    blank.id        = row;
    blank.probeFile = localName;   // relative to session dir
    probeTable->blockSignals(true);
    populateRow(row, blank);
    probeTable->blockSignals(false);
    probeTable->setCurrentCell(row, ColFile);
    m_modified = true;
    recalculateAll();
}

void ProbePage::removeProbe()
{
    int row = probeTable->currentRow();
    if (row < 0) return;
    if (QMessageBox::question(this, tr("Remove Probe"),
            tr("Remove probe at row %1?").arg(row + 1),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    probeTable->removeRow(row);
    renumberIds();
    m_modified = true;
    recalculateAll();
}

void ProbePage::moveProbeUp()
{
    int row = probeTable->currentRow();
    if (row <= 0) return;
    probeTable->blockSignals(true);
    ProbeEntry above = rowToEntry(row - 1);
    ProbeEntry below = rowToEntry(row);
    populateRow(row - 1, below);
    populateRow(row,     above);
    probeTable->blockSignals(false);
    renumberIds();
    probeTable->selectRow(row - 1);
    m_modified = true;
    recalculateAll();
}

void ProbePage::moveProbeDown()
{
    int row = probeTable->currentRow();
    if (row < 0 || row >= probeTable->rowCount() - 1) return;
    probeTable->blockSignals(true);
    ProbeEntry above = rowToEntry(row);
    ProbeEntry below = rowToEntry(row + 1);
    populateRow(row,     below);
    populateRow(row + 1, above);
    probeTable->blockSignals(false);
    renumberIds();
    probeTable->selectRow(row + 1);
    m_modified = true;
    recalculateAll();
}

void ProbePage::rowSelected()
{
    const int row = probeTable->currentRow();
    qDebug() << "[ProbeMaker] rowSelected: row=" << row
             << "rowCount=" << probeTable->rowCount()
             << "m_currentMakerRow=" << m_currentMakerRow;

    // Save any pending edits from the previously-loaded probe before
    // we switch.  saveMakerToCurrentRow is a no-op when m_loadingMaker
    // or when there's no current row.
    saveMakerToCurrentRow();

    if (row < 0) {
        qDebug() << "[ProbeMaker] rowSelected: row<0, showing placeholder";
        if (m_makerStack) m_makerStack->setCurrentIndex(0);   // placeholder
        if (diagramLabel) {
            diagramLabel->setText(tr("Select a row to preview the probe layout"));
            diagramLabel->setPixmap(QPixmap());
        }
        m_currentMakerRow = -1;
        return;
    }

    loadProbeIntoMaker(row);
}

// ---------------------------------------------------------------------------
// loadProbeIntoMaker
// ---------------------------------------------------------------------------
// Read row @p row's probeFile cell, resolve it to an absolute path
// (preferring the session-local copy over the library path), and feed
// it to the embedded ProbeMakerPage.  When the file is missing or
// can't be parsed we fall back to the legacy SVG / text placeholder
// behaviour by leaving the QStackedWidget on page 0.
//
// `m_loadingMaker` is raised across the load so the maker's
// modified() signal — which fires during setConnector() because the
// scene is rebuilt — does not bounce back through saveMakerToCurrentRow.
void ProbePage::loadProbeIntoMaker(int row)
{
    qDebug() << "[ProbeMaker] loadProbeIntoMaker: row=" << row
             << "m_probeMaker=" << (void*)m_probeMaker
             << "m_makerStack=" << (void*)m_makerStack;

    if (!m_probeMaker || !m_makerStack) {
        qWarning() << "[ProbeMaker] loadProbeIntoMaker: maker not instantiated"
                   << "(diagramGroupBox/diagramLayout missing in ctor) — bailing";
        // Constructor wiring failed (UI without diagramGroupBox);
        // nothing more to do.
        return;
    }

    auto* fileItem = probeTable->item(row, ColFile);
    const QString probeFile = fileItem ? fileItem->text().trimmed() : QString();
    qDebug() << "[ProbeMaker] loadProbeIntoMaker: probeFile cell=" << probeFile;

    if (probeFile.isEmpty()) {
        qDebug() << "[ProbeMaker] loadProbeIntoMaker: empty file → page 0 (placeholder)";
        // No probe file → show the placeholder, not the editor.
        m_makerStack->setCurrentIndex(0);
        if (diagramLabel) {
#ifdef ND_HAVE_QT_SVG
            QSvgRenderer renderer(QStringLiteral(":/icons/probe.svg"));
            if (renderer.isValid()) {
                QSize sz = diagramLabel->size().boundedTo(QSize(300, 300));
                if (sz.isEmpty()) sz = QSize(200, 200);
                QPixmap pm(sz);
                pm.fill(Qt::transparent);
                QPainter painter(&pm);
                renderer.render(&painter);
                painter.end();
                diagramLabel->setPixmap(pm);
                diagramLabel->setText(QString());
            } else
#endif
            {
                diagramLabel->setPixmap(QPixmap());
                diagramLabel->setText(tr("No probe file set for this entry"));
            }
        }
        m_currentMakerRow = row;
        return;
    }

    // Resolve the probe file: try the session-local path first, then
    // the library path.  Library files are typically read-only, so
    // editing one in the maker triggers a rename to
    //   <session>.probe.<probeId>.probe
    // on the next save (handled by saveMakerToCurrentRow).
    QString resolved = probeFile;
    qDebug() << "[ProbeMaker] loadProbeIntoMaker: cwd=" << QDir::currentPath();
    qDebug() << "[ProbeMaker] loadProbeIntoMaker: trying" << resolved
             << "exists?" << QFile::exists(resolved);
    if (!QFile::exists(resolved)) {
        resolved = getLibraryPath() + QDir::separator() + probeFile;
        qDebug() << "[ProbeMaker] loadProbeIntoMaker: trying library"
                 << resolved << "exists?" << QFile::exists(resolved);
    }

    if (!QFile::exists(resolved)) {
        qWarning() << "[ProbeMaker] loadProbeIntoMaker: file not found, page 0";
        m_makerStack->setCurrentIndex(0);
        if (diagramLabel) {
            diagramLabel->setPixmap(QPixmap());
            diagramLabel->setText(
                tr("%1\n(file not found)").arg(QFileInfo(probeFile).fileName()));
        }
        m_currentMakerRow = row;
        return;
    }

    // Hand off to the maker.  Block re-entrant save while the scene
    // rebuild emits modified().
    qDebug() << "[ProbeMaker] loadProbeIntoMaker: calling loadFromFile(" << resolved << ")";
    m_loadingMaker = true;
    QString err;
    const bool ok = m_probeMaker->loadFromFile(resolved, &err);
    m_loadingMaker = false;
    qDebug() << "[ProbeMaker] loadProbeIntoMaker: loadFromFile returned" << ok
             << "err=" << err;

    if (!ok) {
        qWarning() << "[ProbeMaker] loadProbeIntoMaker: parse failed → page 0";
        m_makerStack->setCurrentIndex(0);
        if (diagramLabel) {
            diagramLabel->setPixmap(QPixmap());
            diagramLabel->setText(
                tr("%1\n(parse error: %2)")
                    .arg(QFileInfo(probeFile).fileName(),
                         err.isEmpty() ? tr("unknown") : err));
        }
        m_currentMakerRow = -1;
        return;
    }

        qDebug() << "[ProbeMaker] loadProbeIntoMaker: success — switching to page 1";
    m_makerStack->setCurrentIndex(1);
    m_currentMakerRow = row;

    // Visibility/size diagnostics — useful when the maker is created
    // but cramped into a too-narrow column.  Reports the actual
    // rendered size after the layout has settled.  If you see
    // "page 1 size=" as something like 80x300 (very narrow), the
    // issue is layout stretch ratios, not visibility.
    qDebug() << "[ProbeMaker] loadProbeIntoMaker: stack visible="
             << m_makerStack->isVisible()
             << "geometry=" << m_makerStack->geometry()
             << "currentWidget=" << (void*)m_makerStack->currentWidget()
             << "probeMaker visible=" << m_probeMaker->isVisible()
             << "probeMaker size=" << m_probeMaker->size()
             << "probeMaker minimumSize=" << m_probeMaker->minimumSize()
             << "diagramGroupBox size="
             << (diagramGroupBox ? diagramGroupBox->size() : QSize());
}

// ---------------------------------------------------------------------------
// saveMakerToCurrentRow
// ---------------------------------------------------------------------------
// Persist whatever the embedded maker is currently showing back to
// disk under the session-local <session>.probe.<probeId>.probe name,
// then update the table's File column to point at it.
//
// Triggered by:
//   - ProbeMakerPage::modified() during interactive editing
//   - rowSelected() before switching away from the current row
//
// Skips when:
//   - m_loadingMaker is true (the modified() came from setConnector)
//   - no row is currently tracked (m_currentMakerRow < 0)
//   - the maker hasn't been instantiated (no diagramGroupBox in the .ui)
void ProbePage::saveMakerToCurrentRow()
{
    qDebug() << "[ProbeMaker] saveMakerToCurrentRow:"
             << "m_loadingMaker=" << m_loadingMaker
             << "m_probeMaker=" << (void*)m_probeMaker
             << "m_currentMakerRow=" << m_currentMakerRow
             << "rowCount=" << probeTable->rowCount();

    if (m_loadingMaker) {
        qDebug() << "[ProbeMaker] saveMakerToCurrentRow: skipped — load in progress";
        return;
    }
    if (!m_probeMaker) {
        qDebug() << "[ProbeMaker] saveMakerToCurrentRow: skipped — no maker";
        return;
    }
    if (m_currentMakerRow < 0) {
        qDebug() << "[ProbeMaker] saveMakerToCurrentRow: skipped — no current row";
        return;
    }
    if (m_currentMakerRow >= probeTable->rowCount()) {
        qDebug() << "[ProbeMaker] saveMakerToCurrentRow: skipped — row out of range";
        return;
    }

    auto* idItem = probeTable->item(m_currentMakerRow, ColId);
    const int probeId = idItem ? idItem->text().toInt() : m_currentMakerRow;
    if (probeId < 0) {
        qDebug() << "[ProbeMaker] saveMakerToCurrentRow: skipped — negative probeId";
        return;
    }

    const QString fname = sessionProbeFilename(probeId);
    const QString fpath = QDir::current().absoluteFilePath(fname);
    qDebug() << "[ProbeMaker] saveMakerToCurrentRow: writing to" << fpath;

    QString err;
    if (!m_probeMaker->saveToFile(fpath, &err)) {
        qWarning() << "[ProbeMaker] saveMakerToCurrentRow: save failed for"
                   << fpath << ":" << err;
        return;
    }
    qDebug() << "[ProbeMaker] saveMakerToCurrentRow: save OK";

    // Update the row's File column to the new local filename.  Block
    // signals so cellEdited isn't re-triggered (it'd call recalculateAll
    // which re-imports from disk — fine, but redundant).
    probeTable->blockSignals(true);
    if (auto* fileItem = probeTable->item(m_currentMakerRow, ColFile)) {
        if (fileItem->text() != fname) fileItem->setText(fname);
    } else {
        probeTable->setItem(m_currentMakerRow, ColFile,
                            new QTableWidgetItem(fname));
    }
    probeTable->blockSignals(false);

    m_modified = true;
    emit probesModified();
}

void ProbePage::browseProbeFile()
{
    int row = probeTable->currentRow();
    if (row < 0) row = probeTable->rowCount() - 1;
    if (row < 0) {
        QMessageBox::information(this, tr("Import Probe"),
            tr("Click \"+\" to add a probe entry."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select Probe Configuration File"),
        getLibraryPath(),
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));
    if (path.isEmpty()) return;

    // Read the row's probeId from the ID column so the copy is named
    // <session>.probe.<probeId>.probe (consistent with addProbe).
    auto* idItem = probeTable->item(row, ColId);
    const int probeId = idItem ? idItem->text().toInt() : row;

    // Copy the selected probe into the session directory (or reuse the
    // local copy if one already exists), then store its bare filename in
    // the row.  Same policy as addProbe — keeps the session self-contained.
    const QString localName = copyProbeIntoSession(path, probeId);
    if (localName.isEmpty())
        return;     // copy failed or user cancelled

    probeTable->blockSignals(true);
    if (!probeTable->item(row, ColFile))
        probeTable->setItem(row, ColFile, new QTableWidgetItem(localName));
    else
        probeTable->item(row, ColFile)->setText(localName);
    probeTable->blockSignals(false);

    m_modified = true;
    recalculateAll();
}

// ---------------------------------------------------------------------------
// recalculateAll
// ---------------------------------------------------------------------------
// Recomputes channel offsets and anatomy/spike groups for every row in the
// probe table from scratch, in list order.  Called after any structural
// change: Browse, Add, Remove, Move Up/Down, or editing the file column.
//
// Algorithm:
//   - Walk rows top-to-bottom.
//   - Each row's offset = cumulative channel count of all rows above it.
//   - Re-import the probe YAML to derive per-shank channel groups.
//   - Assign group IDs sequentially across all probes (1, 2, ..., N).
//   - Write the new offset back into ColOffset so the table stays in sync.
//   - After processing all rows, compute a leftover group from any channels
//     0..m_nbChannels-1 not claimed by any probe.
//   - Emit probeLayoutImported with the complete unified anatomy map.

void ProbePage::recalculateAll()
{
    QString libPath = getLibraryPath();
    int nRows = probeTable->rowCount();

    QMap<int,QList<int>> outAnatomy;
    QMap<int,QList<int>> outSpike;
    QSet<int> allProbeChannels;
    int nextGroupId = 1;
    int cumOffset   = 0;

    for (int r = 0; r < nRows; ++r) {
        auto* fi = probeTable->item(r, ColFile);
        QString pf = fi ? fi->text().trimmed() : QString();

        // Write the computed offset back to the table
        probeTable->blockSignals(true);
        if (!probeTable->item(r, ColOffset))
            probeTable->setItem(r, ColOffset, new QTableWidgetItem(QString::number(cumOffset)));
        else
            probeTable->item(r, ColOffset)->setText(QString::number(cumOffset));
        probeTable->blockSignals(false);

        if (pf.isEmpty()) continue;

        // Resolve path
        QString resolved = pf;
        if (!QFile::exists(resolved) && !libPath.isEmpty())
            resolved = libPath + QDir::separator() + pf;

        int cnt = probeChannelCount(resolved);
        if (cnt <= 0) {
            // Unknown probe — can't parse.  Advance offset by 0 (leave as-is).
            continue;
        }

        // Re-import at the correct offset to get shank channel maps
        ProbeEntry entry;
        entry.channelOffset = cumOffset;
        // Preserve any label already in the table
        auto* li = probeTable->item(r, ColLabel);
        if (li && !li->text().isEmpty()) entry.label = li->text();

        QMap<int,QList<int>> ra, rs;
        if (importProbeYaml(resolved, entry, nextGroupId, ra, rs)) {
            // Write derived groups back to the table row
            QStringList gstrs;
            for (int gid : ra.keys()) gstrs.append(QString::number(gid));
            probeTable->blockSignals(true);
            if (!probeTable->item(r, ColGroups))
                probeTable->setItem(r, ColGroups, new QTableWidgetItem(gstrs.join(QStringLiteral(", "))));
            else
                probeTable->item(r, ColGroups)->setText(gstrs.join(QStringLiteral(", ")));
            // Fill label from probe file if blank
            if (entry.label != (li ? li->text() : QString())) {
                if (!probeTable->item(r, ColLabel))
                    probeTable->setItem(r, ColLabel, new QTableWidgetItem(entry.label));
                else if (probeTable->item(r, ColLabel)->text().isEmpty())
                    probeTable->item(r, ColLabel)->setText(entry.label);
            }
            probeTable->blockSignals(false);

            for (auto it = ra.constBegin(); it != ra.constEnd(); ++it) {
                outAnatomy[it.key()] = it.value();
                for (int ch : it.value()) allProbeChannels.insert(ch);
                nextGroupId = qMax(nextGroupId, it.key() + 1);
            }
            for (auto it = rs.constBegin(); it != rs.constEnd(); ++it)
                outSpike[it.key()] = it.value();
        }

        cumOffset += cnt;
    }

    // Leftover group: channels 0..m_nbChannels-1 not claimed by any probe
    if (m_nbChannels > 0) {
        QList<int> leftover;
        for (int ch = 0; ch < m_nbChannels; ++ch)
            if (!allProbeChannels.contains(ch))
                leftover.append(ch);
        if (!leftover.isEmpty())
            outAnatomy[nextGroupId] = leftover;
            // leftover not added to outSpike
    }

    emit probesModified();

    if (!outAnatomy.isEmpty()) {
        QList<ProbeEntry> allProbes;
        getProbes(allProbes);
        emit probeLayoutImported(allProbes, outAnatomy, outSpike, 1);
    }
}

void ProbePage::browseLibraryPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Probe Library Directory"), getLibraryPath());
    if (!dir.isEmpty()) {
        m_libraryPath = dir;
        libraryPathEdit->setText(dir);
        m_modified = true;
    }
}

void ProbePage::cellEdited(int /*row*/, int column)
{
    m_modified = true;
    // When the probe file path is edited directly, recalculate offsets and
    // anatomy groups for all rows — same as when Browse is used.
    if (column == ColFile)
        recalculateAll();
    else
        emit probesModified();
}

// ---------------------------------------------------------------------------
// importProbeYaml
// ---------------------------------------------------------------------------

bool ProbePage::importProbeYaml(const QString&        path,
                                ProbeEntry&           entry,
                                int                   nextGroupId,
                                QMap<int,QList<int>>& outAnatomy,
                                QMap<int,QList<int>>& outSpike)
{
    YAML::Node doc;
    try {
        doc = YAML::LoadFile(path.toStdString());
    } catch (const YAML::Exception& e) {
        qWarning() << "ProbePage: YAML parse error in" << path
                   << ":" << e.what();
        return false;
    }

    // All content is under the probeFile: key
    const YAML::Node pf = doc["probeFile"];
    if (!pf || !pf.IsMap()) {
        qWarning() << "ProbePage: no probeFile root in" << path;
        return false;
    }

    // Basic metadata
    if (pf["model"] && entry.label.isEmpty())
        entry.label = QString::fromStdString(pf["model"].as<std::string>());

    const YAML::Node shanksNode = pf["shanks"];
    const YAML::Node sitesNode  = pf["sites"];
    if (!shanksNode || !sitesNode) {
        qWarning() << "ProbePage: missing shanks/sites in" << path;
        return false;
    }

    int nShanks       = shanksNode["count"] ? shanksNode["count"].as<int>() : 1;
    int sitesPerShank = sitesNode["count_per_shank"]
                            ? sitesNode["count_per_shank"].as<int>() : 0;

    if (nShanks < 1 || sitesPerShank < 1) {
        qWarning() << "ProbePage: invalid shank/site counts in" << path;
        return false;
    }

    // Channel map: optional explicit assignment
    // Format: list of lists  [[ch0_shank0, ch1_shank0, ...], [ch0_shank1, ...], ...]
    // OR flat list ordered shank-major: [ch0_s0, ch1_s0, ..., ch0_s1, ...]
    // When null, use sequential assignment starting at channelOffset.
    QVector<QVector<int>> channelMap(nShanks, QVector<int>(sitesPerShank, -1));
    bool hasMap = false;

    const YAML::Node cmNode = pf["channelMap"];
    if (cmNode && cmNode.IsMap()) {
        const YAML::Node mapSeq = cmNode["map"];
        if (mapSeq && mapSeq.IsSequence() && static_cast<int>(mapSeq.size()) > 0) {
            hasMap = true;
            // Detect nested vs flat
            if (mapSeq[0].IsSequence()) {
                // [[...], [...], ...]  one inner list per shank
                for (int s = 0; s < nShanks && s < static_cast<int>(mapSeq.size()); ++s) {
                    const YAML::Node row = mapSeq[static_cast<std::size_t>(s)];
                    for (int c = 0; c < sitesPerShank && c < static_cast<int>(row.size()); ++c)
                        channelMap[s][c] = row[static_cast<std::size_t>(c)].as<int>();
                }
            } else {
                // Flat shank-major list
                int idx = 0;
                for (int s = 0; s < nShanks; ++s)
                    for (int c = 0; c < sitesPerShank; ++c, ++idx)
                        if (idx < static_cast<int>(mapSeq.size()))
                            channelMap[s][c] = mapSeq[static_cast<std::size_t>(idx)].as<int>();
            }
        }
    }

    if (!hasMap) {
        // Sequential: shank s gets channels [offset + s*sps ... offset + (s+1)*sps - 1]
        for (int s = 0; s < nShanks; ++s)
            for (int c = 0; c < sitesPerShank; ++c)
                channelMap[s][c] = entry.channelOffset + s * sitesPerShank + c;
    }

    // Assign one anatomical group per shank, group IDs start at nextGroupId
    entry.anatomicalGroups.clear();
    entry.spikeGroups.clear();

    for (int s = 0; s < nShanks; ++s) {
        int gid = nextGroupId + s;
        entry.anatomicalGroups.append(gid);

        QList<int> channels;
        for (int ch : channelMap[s])
            channels.append(ch);

        outAnatomy[gid] = channels;
        outSpike[gid]   = channels;   // same by default; user can refine
    }

    return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ProbePage::populateRow(int row, const ProbeEntry& entry)
{
    auto* idItem = new QTableWidgetItem(QString::number(entry.id));
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    probeTable->setItem(row, ColId, idItem);

    probeTable->setItem(row, ColFile,
        new QTableWidgetItem(entry.probeFile));
    probeTable->setItem(row, ColLabel,
        new QTableWidgetItem(entry.label));
    probeTable->setItem(row, ColOffset,
        new QTableWidgetItem(QString::number(entry.channelOffset)));

    QStringList groupStrs;
    for (int g : entry.anatomicalGroups)
        groupStrs.append(QString::number(g));
    probeTable->setItem(row, ColGroups,
        new QTableWidgetItem(groupStrs.join(QStringLiteral(", "))));

    QStringList spikeStrs;
    for (int g : entry.spikeGroups)
        spikeStrs.append(QString::number(g));
    probeTable->setItem(row, ColSpikeGroups,
        new QTableWidgetItem(spikeStrs.join(QStringLiteral(", "))));
}

ProbeEntry ProbePage::rowToEntry(int row) const
{
    ProbeEntry e;

    auto* idItem = probeTable->item(row, ColId);
    e.id = idItem ? idItem->text().toInt() : row;

    auto* fileItem = probeTable->item(row, ColFile);
    e.probeFile = fileItem ? fileItem->text().trimmed() : QString();

    auto* labelItem = probeTable->item(row, ColLabel);
    e.label = labelItem ? labelItem->text().trimmed() : QString();

    auto* offsetItem = probeTable->item(row, ColOffset);
    e.channelOffset = offsetItem ? offsetItem->text().toInt() : 0;

    auto* groupItem = probeTable->item(row, ColGroups);
    if (groupItem) {
        const QStringList parts = groupItem->text().split(
            QRegularExpression(QStringLiteral("[,\\s]+")),
            Qt::SkipEmptyParts);
        for (const QString& s : parts) {
            bool ok = false;
            int g = s.toInt(&ok);
            if (ok && g > 0) e.anatomicalGroups.append(g);
        }
    }

    auto* spikeItem = probeTable->item(row, ColSpikeGroups);
    if (spikeItem && !spikeItem->text().trimmed().isEmpty()) {
        const QStringList parts = spikeItem->text().split(
            QRegularExpression(QStringLiteral("[,\\s]+")),
            Qt::SkipEmptyParts);
        for (const QString& s : parts) {
            bool ok = false;
            int g = s.toInt(&ok);
            if (ok && g > 0) e.spikeGroups.append(g);
        }
        if (e.spikeGroups == e.anatomicalGroups)
            e.spikeGroups.clear();
    }

    return e;
}

// ---------------------------------------------------------------------------
// probeChannelCount
// ---------------------------------------------------------------------------

int ProbePage::probeChannelCount(const QString& probePath)
{
    if (probePath.isEmpty() || !QFile::exists(probePath)) return 0;
    try {
        YAML::Node doc = YAML::LoadFile(probePath.toStdString());
        const YAML::Node pf = doc["probeFile"];
        if (!pf || !pf.IsMap()) return 0;
        if (pf["totalChannels"] && pf["totalChannels"].IsScalar())
            return pf["totalChannels"].as<int>();
        // Fallback: nShanks × count_per_shank
        const YAML::Node shanks = pf["shanks"];
        const YAML::Node sites  = pf["sites"];
        if (!shanks || !sites) return 0;
        int nShanks = shanks["count"] ? shanks["count"].as<int>() : 1;
        const YAML::Node cps = sites["count_per_shank"];
        if (!cps) return 0;
        if (cps.IsSequence()) {
            int total = 0;
            for (std::size_t i = 0; i < cps.size(); ++i) total += cps[i].as<int>();
            return total;
        }
        return nShanks * cps.as<int>();
    } catch (...) { return 0; }
}

void ProbePage::renumberIds()
{
    probeTable->blockSignals(true);
    for (int row = 0; row < probeTable->rowCount(); ++row) {
        auto* item = probeTable->item(row, ColId);
        if (item) item->setText(QString::number(row));
    }
    probeTable->blockSignals(false);
}
