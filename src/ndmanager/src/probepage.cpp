/***************************************************************************
 * probepage.cpp
 *
 * Copyright (C) 2025 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "probepage.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ProbePage::ProbePage(QWidget* parent)
    : ProbeLayout(parent)
    , m_modified(false)
{
    // Configure table columns
    probeTable->setColumnCount(5);
    probeTable->horizontalHeader()->setSectionResizeMode(ColFile,   QHeaderView::Stretch);
    probeTable->horizontalHeader()->setSectionResizeMode(ColLabel,  QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(ColId,     QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(ColOffset, QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(ColGroups, QHeaderView::Stretch);

    probeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    probeTable->setSelectionMode(QAbstractItemView::SingleSelection);

    // Default library path
    QStringList dataDirs = QStandardPaths::standardLocations(
        QStandardPaths::GenericDataLocation);
    if (!dataDirs.isEmpty())
        m_libraryPath = dataDirs.first() + QStringLiteral("/neurosuite/probes");

    libraryPathEdit->setPlaceholderText(m_libraryPath + QStringLiteral("  (default)"));

    // Wire buttons
    connect(addProbeButton,    &QPushButton::clicked, this, &ProbePage::addProbe);
    connect(removeProbeButton, &QPushButton::clicked, this, &ProbePage::removeProbe);
    connect(browseProbeButton, &QPushButton::clicked, this, &ProbePage::browseProbeFile);
    connect(browseLibraryButton, &QPushButton::clicked, this, &ProbePage::browseLibraryPath);
    connect(probeTable, &QTableWidget::cellChanged, this, &ProbePage::cellEdited);
}

// ---------------------------------------------------------------------------
// setProbes / getProbes
// ---------------------------------------------------------------------------

void ProbePage::setProbes(const QList<ProbeEntry>& probes)
{
    // Block signals while populating to avoid marking as modified
    probeTable->blockSignals(true);
    probeTable->setRowCount(0);

    for (const ProbeEntry& entry : probes) {
        int row = probeTable->rowCount();
        probeTable->insertRow(row);
        populateRow(row, entry);
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

void ProbePage::addProbe()
{
    int row = probeTable->rowCount();
    probeTable->insertRow(row);

    ProbeEntry blank;
    blank.id = row;
    // Block signals so inserting the blank row doesn't trip cellChanged
    probeTable->blockSignals(true);
    populateRow(row, blank);
    probeTable->blockSignals(false);

    probeTable->setCurrentCell(row, ColFile);
    m_modified = true;
    emit probesModified();
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
    emit probesModified();
}

void ProbePage::browseProbeFile()
{
    int row = probeTable->currentRow();
    if (row < 0) row = probeTable->rowCount() - 1;
    if (row < 0) return;

    QString startDir = getLibraryPath();
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select Probe Configuration File"),
        startDir,
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));

    if (path.isEmpty()) return;

    // Store relative path if inside library
    QString libPath = getLibraryPath();
    if (!libPath.isEmpty() && path.startsWith(libPath))
        path = path.mid(libPath.length()).remove(0, 1); // strip leading slash

    probeTable->blockSignals(true);
    probeTable->item(row, ColFile)->setText(path);
    probeTable->blockSignals(false);
    m_modified = true;
    emit probesModified();
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

void ProbePage::cellEdited(int /*row*/, int /*column*/)
{
    m_modified = true;
    emit probesModified();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ProbePage::populateRow(int row, const ProbeEntry& entry)
{
    // ID (read-only display)
    auto* idItem = new QTableWidgetItem(QString::number(entry.id));
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    probeTable->setItem(row, ColId, idItem);

    probeTable->setItem(row, ColFile,
        new QTableWidgetItem(entry.probeFile));
    probeTable->setItem(row, ColLabel,
        new QTableWidgetItem(entry.label));
    probeTable->setItem(row, ColOffset,
        new QTableWidgetItem(QString::number(entry.channelOffset)));

    // Anatomical groups: comma-separated list
    QStringList groupStrs;
    for (int g : entry.anatomicalGroups)
        groupStrs.append(QString::number(g));
    probeTable->setItem(row, ColGroups,
        new QTableWidgetItem(groupStrs.join(QStringLiteral(", "))));
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
    return e;
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
