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
}

// ---------------------------------------------------------------------------
// setProbes / getProbes
// ---------------------------------------------------------------------------

void ProbePage::setProbes(const QList<ProbeEntry>& probes)
{
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
    emit probesModified();
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
    emit probesModified();
}

void ProbePage::rowSelected()
{
    int row = probeTable->currentRow();
    if (row < 0) {
        diagramLabel->setText(tr("Select a row to preview the probe layout"));
        diagramLabel->setPixmap(QPixmap());
        return;
    }

    auto* fileItem = probeTable->item(row, ColFile);
    QString probeFile = fileItem ? fileItem->text().trimmed() : QString();

    if (probeFile.isEmpty()) {
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
            return;
        }
#endif
        diagramLabel->setPixmap(QPixmap());
        diagramLabel->setText(tr("No probe file set for this entry"));
        return;
    }

    QString resolved = probeFile;
    if (!QFile::exists(resolved))
        resolved = getLibraryPath() + QDir::separator() + probeFile;

#ifdef ND_HAVE_QT_SVG
    if (resolved.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) &&
        QFile::exists(resolved))
    {
        QSvgRenderer renderer(resolved);
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
        QString shortName = QFileInfo(probeFile).fileName();
        diagramLabel->setPixmap(QPixmap());
        diagramLabel->setText(QFile::exists(resolved)
            ? tr("%1\n(no diagram available)").arg(shortName)
            : tr("%1\n(file not found)").arg(shortName));
    }
}

void ProbePage::browseProbeFile()
{
    int row = probeTable->currentRow();
    if (row < 0) row = probeTable->rowCount() - 1;
    if (row < 0) {
        // No rows yet — ask user to add a probe first
        QMessageBox::information(this, tr("Import Probe"),
            tr("Click \"+\" to add a probe entry first, then use Browse to assign a file."));
        return;
    }

    QString startDir = getLibraryPath();
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select Probe Configuration File"),
        startDir,
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));
    if (path.isEmpty()) return;

    // Compute next group ID = 1 + max of all existing group IDs across all rows
    int nextGroupId = 1;
    for (int r = 0; r < probeTable->rowCount(); ++r) {
        ProbeEntry e = rowToEntry(r);
        for (int g : e.anatomicalGroups)
            nextGroupId = qMax(nextGroupId, g + 1);
    }

    // Use the channel offset already set in this row (user may have filled it)
    auto* offsetItem = probeTable->item(row, ColOffset);
    int channelOffset = offsetItem ? offsetItem->text().toInt() : 0;

    ProbeEntry entry = rowToEntry(row);
    entry.channelOffset = channelOffset;

    QMap<int,QList<int>> outAnatomy;
    QMap<int,QList<int>> outSpike;

    if (!importProbeYaml(path, entry, nextGroupId, outAnatomy, outSpike)) {
        // Parse failed — fall back to just recording the file path
        QString libPath = getLibraryPath();
        if (!libPath.isEmpty() && path.startsWith(libPath))
            path = path.mid(libPath.length()).remove(0, 1);
        probeTable->blockSignals(true);
        if (!probeTable->item(row, ColFile))
            probeTable->setItem(row, ColFile, new QTableWidgetItem(path));
        else
            probeTable->item(row, ColFile)->setText(path);
        probeTable->blockSignals(false);
        m_modified = true;
        emit probesModified();
        return;
    }

    // Write relative path when inside library
    QString storedPath = path;
    QString libPath = getLibraryPath();
    if (!libPath.isEmpty() && storedPath.startsWith(libPath))
        storedPath = storedPath.mid(libPath.length()).remove(0, 1);
    entry.probeFile = storedPath;

    // Update the table row
    probeTable->blockSignals(true);
    populateRow(row, entry);
    probeTable->blockSignals(false);

    m_modified = true;
    emit probesModified();

    // Collect full probe list and emit layout signal
    QList<ProbeEntry> allProbes;
    getProbes(allProbes);
    emit probeLayoutImported(allProbes, outAnatomy, outSpike, nextGroupId);
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

void ProbePage::renumberIds()
{
    probeTable->blockSignals(true);
    for (int row = 0; row < probeTable->rowCount(); ++row) {
        auto* item = probeTable->item(row, ColId);
        if (item) item->setText(QString::number(row));
    }
    probeTable->blockSignals(false);
}
