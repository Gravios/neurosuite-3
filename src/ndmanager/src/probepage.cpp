/***************************************************************************
 * probepage.cpp
 *
 * Implementation of the redesigned Probe tab.  See probepage.h for the
 * page-level design (three-pane single-page layout: probe list,
 * inspector, embedded ProbeMakerPage).
 *
 * Copyright (C) 2025–2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "probepage.h"
#include "probemakerpage.h"

#include <klustersshared/parameteryamlreader_probes.h>

#include <yaml-cpp/yaml.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

// ═══════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════

ProbePage::ProbePage(QWidget* parent)
    : QWidget(parent)
{
    // Default library path: /usr/local/share/neurosuite/probes (or the
    // platform equivalent via QStandardPaths).  empty.probe and the
    // canonical library files all live under this prefix.
    const QStringList dataDirs = QStandardPaths::standardLocations(
        QStandardPaths::GenericDataLocation);
    if (!dataDirs.isEmpty())
        m_libraryPath = dataDirs.first() + QStringLiteral("/neurosuite/probes");

    buildUi();
    refreshInspector();   // disables fields when no selection
}

ProbePage::~ProbePage() = default;

// ═══════════════════════════════════════════════════════════════════════════
// UI construction
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Three-pane splitter ─────────────────────────────────────────────
    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->setHandleWidth(2);
    m_split->setChildrenCollapsible(false);
    outer->addWidget(m_split, /*stretch=*/1);

    // ── Pane 1: probe list + add/remove buttons ─────────────────────────
    auto* listPane = new QWidget;
    auto* listVbox = new QVBoxLayout(listPane);
    listVbox->setContentsMargins(8, 8, 4, 8);
    listVbox->setSpacing(6);

    auto* listHdr = new QLabel(tr("PROBES"));
    listHdr->setStyleSheet(
        "color:#4a5568;font-size:10px;font-weight:bold;letter-spacing:2px;");
    listVbox->addWidget(listHdr);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);
    listVbox->addWidget(m_list, /*stretch=*/1);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);
    m_addBtn    = new QPushButton(QStringLiteral("+"));
    m_removeBtn = new QPushButton(QStringLiteral("−"));
    m_browseBtn = new QPushButton(tr("Browse Library…"));
    m_addBtn   ->setToolTip(tr("Add a new probe (clones empty.probe template)"));
    m_removeBtn->setToolTip(tr("Remove the selected probe"));
    m_browseBtn->setToolTip(tr("Import a .probe file from the library"));
    m_addBtn   ->setMaximumWidth(32);
    m_removeBtn->setMaximumWidth(32);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addWidget(m_browseBtn);
    btnRow->addStretch();
    listVbox->addLayout(btnRow);

    m_split->addWidget(listPane);

    // ── Pane 2: inspector form ──────────────────────────────────────────
    auto* inspPane = new QWidget;
    auto* inspVbox = new QVBoxLayout(inspPane);
    inspVbox->setContentsMargins(8, 8, 8, 8);
    inspVbox->setSpacing(8);

    auto* inspHdr = new QLabel(tr("INSPECTOR"));
    inspHdr->setStyleSheet(
        "color:#4a5568;font-size:10px;font-weight:bold;letter-spacing:2px;");
    inspVbox->addWidget(inspHdr);

    auto* inspForm = new QFormLayout;
    inspForm->setLabelAlignment(Qt::AlignRight);
    inspForm->setFormAlignment(Qt::AlignTop);
    inspForm->setSpacing(6);

    m_inspLabel  = new QLineEdit;
    m_inspFile   = new QLineEdit;
    m_inspOffset = new QSpinBox;
    m_inspOffset->setRange(0, 100000);
    m_inspOffset->setToolTip(tr(
        "First ADC channel from this probe.  "
        "Auto-recomputed by Recalculate; manual edits override the "
        "stack-from-zero default."));

    inspForm->addRow(tr("Label:"),          m_inspLabel);
    inspForm->addRow(tr("Probe file:"),     m_inspFile);
    inspForm->addRow(tr("Channel offset:"), m_inspOffset);

    m_inspAnatomy = new QLabel;
    m_inspSpike   = new QLabel;
    m_inspAnatomy->setTextFormat(Qt::PlainText);
    m_inspSpike  ->setTextFormat(Qt::PlainText);
    inspForm->addRow(tr("Anatomical groups:"), m_inspAnatomy);
    inspForm->addRow(tr("Spike groups:"),      m_inspSpike);

    inspVbox->addLayout(inspForm);
    inspVbox->addStretch(1);

    m_split->addWidget(inspPane);

    // ── Pane 3: embedded ProbeMakerPage ─────────────────────────────────
    m_maker = new ProbeMakerPage;
    m_split->addWidget(m_maker);

    // Stretch ratios — list narrow, inspector mid, geometry wide.  These
    // are initial sizes; QSplitter handles user resize.
    m_split->setStretchFactor(0, 1);   // list
    m_split->setStretchFactor(1, 2);   // inspector
    m_split->setStretchFactor(2, 5);   // geometry
    m_split->setSizes({140, 280, 800});

    // ── Signals ────────────────────────────────────────────────────────
    connect(m_list,     &QListWidget::currentRowChanged,
            this,       [this](int){ onListSelectionChanged(); });
    connect(m_addBtn,   &QPushButton::clicked, this, &ProbePage::onAddClicked);
    connect(m_removeBtn,&QPushButton::clicked, this, &ProbePage::onRemoveClicked);
    connect(m_browseBtn,&QPushButton::clicked, this, &ProbePage::onBrowseLibraryClicked);

    connect(m_inspLabel,  &QLineEdit::editingFinished,
            this, &ProbePage::onLabelEdited);
    connect(m_inspFile,   &QLineEdit::editingFinished,
            this, &ProbePage::onFileEdited);
    connect(m_inspOffset, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int){ onOffsetEdited(); });

    connect(m_maker, &ProbeMakerPage::modified,
            this, &ProbePage::onMakerModified);
}

// ═══════════════════════════════════════════════════════════════════════════
// Data setters / getters
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::setProbes(const QList<ProbeEntry>& probes)
{
    m_probes = probes;
    rebuildList();
    if (!m_probes.isEmpty()) {
        m_list->setCurrentRow(0);
    } else {
        m_currentIndex = -1;
        refreshInspector();
    }
    m_modified = false;
}

void ProbePage::getProbes(QList<ProbeEntry>& probes) const
{
    probes = m_probes;
}

void ProbePage::setLibraryPath(const QString& path)
{
    if (!path.isEmpty()) m_libraryPath = path;
}

QString ProbePage::getLibraryPath() const
{
    return m_libraryPath;
}

// ═══════════════════════════════════════════════════════════════════════════
// List management
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::rebuildList()
{
    QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const ProbeEntry& e : m_probes) {
        const QString label = e.label.isEmpty()
            ? tr("Probe %1").arg(e.id)
            : tr("Probe %1 — %2").arg(e.id).arg(e.label);
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, e.id);
    }
}

void ProbePage::onListSelectionChanged()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_probes.size()) {
        m_currentIndex = -1;
        refreshInspector();
        m_maker->clearToConnector();
        return;
    }
    m_currentIndex = row;
    refreshInspector();
    loadProbeIntoMaker(row);
}

// ═══════════════════════════════════════════════════════════════════════════
// Add / remove / browse
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::onAddClicked()
{
    // Allocate a fresh probe id: max existing + 1 (or 0 if list empty).
    int newId = 0;
    for (const ProbeEntry& e : m_probes) newId = qMax(newId, e.id + 1);

    // Resolve the empty template path.  If absent, fall back to a tiny
    // hardcoded skeleton string so the user still ends up with a usable
    // probe file even when the system-wide template hasn't been
    // installed (CI builds, dev checkouts).
    const QString templatePath = emptyTemplatePath();
    QString srcContent;
    if (QFile::exists(templatePath)) {
        QFile f(templatePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            srcContent = QString::fromUtf8(f.readAll());
    }
    if (srcContent.isEmpty()) {
        // Hardcoded minimal fallback: 1 shank, 1 site.
        srcContent = QStringLiteral(
            "probeFile:\n"
            "  version: '1.0'\n"
            "  vendor: ''\n"
            "  model: ''\n"
            "  totalChannels: 1\n"
            "  substrate: { material: silicon, thickness_um: null }\n"
            "  shanks:\n"
            "    count: 1\n"
            "    spacing_um: null\n"
            "    length_mm: null\n"
            "  sites:\n"
            "    count_per_shank: 1\n"
            "    layout: linear\n"
            "    area_um2: 100\n"
            "    spacing_um: null\n"
            "    geometry:\n"
            "      - [0, 100]\n"
            "  channelMap:\n"
            "    description: 'Sequential.'\n"
            "    map: null\n"
            "  notes: 'Empty starter probe — edit before use.'\n"
        );
    }

    // Write to <session>.probe.<newId>.probe
    const QString fname = sessionProbeFilename(newId);
    const QString fpath = QDir::current().absoluteFilePath(fname);
    QFile out(fpath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Add Probe"),
            tr("Cannot create %1: %2").arg(fpath, out.errorString()));
        return;
    }
    out.write(srcContent.toUtf8());
    out.close();

    ProbeEntry e;
    e.id            = newId;
    e.probeFile     = fname;
    e.label         = QString();   // user fills via inspector
    e.channelOffset = 0;            // recalculateAll will fix
    m_probes.append(e);

    rebuildList();
    m_list->setCurrentRow(m_probes.size() - 1);
    m_modified = true;
    recalculateAll();
}

void ProbePage::onRemoveClicked()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_probes.size()) return;
    const ProbeEntry victim = m_probes[m_currentIndex];

    const auto reply = QMessageBox::question(
        this, tr("Remove Probe"),
        tr("Remove probe %1 (%2)?\n\n"
           "The probe file will be deleted from the session directory if it "
           "is a session-local copy (matches <session>.probe.%1.probe).")
            .arg(victim.id)
            .arg(victim.label.isEmpty() ? tr("unlabeled") : victim.label),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    // Delete session-local copy if its name matches the canonical pattern
    // for this id.  Library files (other paths) are left alone.
    const QString sessionName = sessionProbeFilename(victim.id);
    if (victim.probeFile == sessionName) {
        const QString fpath = QDir::current().absoluteFilePath(victim.probeFile);
        if (QFile::exists(fpath))
            QFile::remove(fpath);
    }

    m_probes.removeAt(m_currentIndex);
    if (m_probes.isEmpty()) {
        m_currentIndex = -1;
    } else {
        m_currentIndex = qMin(m_currentIndex, m_probes.size() - 1);
    }
    rebuildList();
    if (m_currentIndex >= 0)
        m_list->setCurrentRow(m_currentIndex);
    else
        refreshInspector();
    m_modified = true;
    recalculateAll();
}

void ProbePage::onBrowseLibraryClicked()
{
    if (m_currentIndex < 0) {
        QMessageBox::information(this, tr("Browse Library"),
            tr("Select a probe in the list (or click \"+\" to add one) "
               "before importing from the library."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select Probe Configuration File"),
        getLibraryPath(),
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));
    if (path.isEmpty()) return;

    ProbeEntry& e = m_probes[m_currentIndex];
    const QString localName = copyProbeIntoSession(path, e.id);
    if (localName.isEmpty()) return;

    e.probeFile = localName;
    rebuildList();
    m_list->setCurrentRow(m_currentIndex);
    m_modified = true;
    recalculateAll();
}

// ═══════════════════════════════════════════════════════════════════════════
// Inspector wiring
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::refreshInspector()
{
    const bool hasSel = (m_currentIndex >= 0 && m_currentIndex < m_probes.size());

    m_inspLabel ->setEnabled(hasSel);
    m_inspFile  ->setEnabled(hasSel);
    m_inspOffset->setEnabled(hasSel);
    m_browseBtn ->setEnabled(hasSel);
    m_removeBtn ->setEnabled(hasSel);

    QSignalBlocker bL(m_inspLabel);
    QSignalBlocker bF(m_inspFile);
    QSignalBlocker bO(m_inspOffset);

    if (!hasSel) {
        m_inspLabel ->clear();
        m_inspFile  ->clear();
        m_inspOffset->setValue(0);
        m_inspAnatomy->setText(QStringLiteral("—"));
        m_inspSpike  ->setText(QStringLiteral("—"));
        return;
    }

    const ProbeEntry& e = m_probes[m_currentIndex];
    m_inspLabel ->setText(e.label);
    m_inspFile  ->setText(e.probeFile);
    m_inspOffset->setValue(e.channelOffset);
    updateGroupsLabels();
}

void ProbePage::updateGroupsLabels()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_probes.size()) {
        m_inspAnatomy->setText(QStringLiteral("—"));
        m_inspSpike  ->setText(QStringLiteral("—"));
        return;
    }
    const ProbeEntry& e = m_probes[m_currentIndex];
    QStringList anatStrs;  for (int g : e.anatomicalGroups) anatStrs.append(QString::number(g));
    QStringList spikeStrs; for (int g : e.spikeGroups)      spikeStrs.append(QString::number(g));
    m_inspAnatomy->setText(anatStrs.isEmpty()  ? tr("—") : anatStrs.join(QStringLiteral(", ")));
    m_inspSpike  ->setText(spikeStrs.isEmpty() ? tr("(same as anatomical)")
                                               : spikeStrs.join(QStringLiteral(", ")));
}

void ProbePage::onLabelEdited()
{
    if (m_currentIndex < 0) return;
    m_probes[m_currentIndex].label = m_inspLabel->text();
    rebuildList();
    m_list->setCurrentRow(m_currentIndex);
    m_modified = true;
    emit probesModified();
}

void ProbePage::onFileEdited()
{
    if (m_currentIndex < 0) return;
    m_probes[m_currentIndex].probeFile = m_inspFile->text().trimmed();
    m_modified = true;
    recalculateAll();
    // Reload geometry too — the file may have changed.
    loadProbeIntoMaker(m_currentIndex);
}

void ProbePage::onOffsetEdited()
{
    if (m_currentIndex < 0) return;
    m_probes[m_currentIndex].channelOffset = m_inspOffset->value();
    m_modified = true;
    recalculateAll();
}

// ═══════════════════════════════════════════════════════════════════════════
// Geometry editor wiring
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::loadProbeIntoMaker(int probeIndex)
{
    if (!m_maker || probeIndex < 0 || probeIndex >= m_probes.size()) return;

    const ProbeEntry& e = m_probes[probeIndex];
    const QString resolved = resolveProbePath(e.probeFile);
    if (resolved.isEmpty()) {
        m_maker->clearToConnector();
        return;
    }

    m_loadingMaker = true;
    QString err;
    const bool ok = m_maker->loadFromFile(resolved, &err);
    m_loadingMaker = false;

    if (!ok) {
        qWarning() << "ProbePage: maker failed to load" << resolved << ":" << err;
        m_maker->clearToConnector();
    }
}

void ProbePage::onMakerModified()
{
    if (m_loadingMaker) return;
    if (m_currentIndex < 0 || m_currentIndex >= m_probes.size()) return;
    saveCurrentProbeFile();
}

void ProbePage::saveCurrentProbeFile()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_probes.size()) return;
    ProbeEntry& e = m_probes[m_currentIndex];
    if (e.id < 0) return;

    const QString fname = sessionProbeFilename(e.id);
    const QString fpath = QDir::current().absoluteFilePath(fname);
    QString err;
    if (!m_maker->saveToFile(fpath, &err)) {
        qWarning() << "ProbePage: maker save failed for" << fpath << ":" << err;
        return;
    }

    if (e.probeFile != fname) {
        e.probeFile = fname;
        QSignalBlocker blocker(m_inspFile);
        m_inspFile->setText(fname);
    }
    m_modified = true;
    recalculateAll();   // geometry changed → recompute group memberships
}

// ═══════════════════════════════════════════════════════════════════════════
// Path / filename helpers
// ═══════════════════════════════════════════════════════════════════════════

QString ProbePage::sessionProbeFilename(int probeId)
{
    const QString sessionStem = QDir::current().dirName();
    return QStringLiteral("%1.probe.%2.probe")
            .arg(sessionStem)
            .arg(probeId);
}

QString ProbePage::resolveProbePath(const QString& cellValue) const
{
    if (cellValue.isEmpty()) return QString();
    if (QFile::exists(cellValue)) return cellValue;
    const QString libCandidate =
        getLibraryPath() + QDir::separator() + cellValue;
    if (QFile::exists(libCandidate)) return libCandidate;
    return QString();
}

QString ProbePage::emptyTemplatePath() const
{
    // Honour an optional environment override so test rigs can point at
    // a custom-installed library without root.
    const QByteArray envOverride = qgetenv("NS3_PROBE_LIBRARY_PATH");
    if (!envOverride.isEmpty()) {
        const QString p = QString::fromLocal8Bit(envOverride)
                            + QStringLiteral("/general/empty.probe");
        if (QFile::exists(p)) return p;
    }
    // Default: <library>/general/empty.probe.
    return getLibraryPath() + QStringLiteral("/general/empty.probe");
}

QString ProbePage::copyProbeIntoSession(const QString& srcPath, int probeId)
{
    const QString baseName  = sessionProbeFilename(probeId);
    const QDir    sessionDir = QDir::current();
    const QString localPath = sessionDir.absoluteFilePath(baseName);

    if (QFileInfo(srcPath).canonicalFilePath()
        == QFileInfo(localPath).canonicalFilePath())
        return baseName;

    if (QFile::exists(localPath)) {
        const auto reply = QMessageBox::question(
            this, tr("Probe file exists"),
            tr("A probe file named\n\n    %1\n\nalready exists.  Overwrite "
               "with the selected file?  Choose No to reuse the existing "
               "local copy.").arg(baseName),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::No);
        if (reply == QMessageBox::Cancel) return QString();
        if (reply == QMessageBox::No)     return baseName;
        if (!QFile::remove(localPath)) {
            QMessageBox::warning(this, tr("Copy Failed"),
                tr("Cannot remove existing %1").arg(localPath));
            return QString();
        }
    }

    if (!QFile::copy(srcPath, localPath)) {
        QMessageBox::warning(this, tr("Copy Failed"),
            tr("Cannot copy\n  %1\nto\n  %2")
                .arg(srcPath, sessionDir.absolutePath()));
        return QString();
    }
    return baseName;
}

// ═══════════════════════════════════════════════════════════════════════════
// recalculateAll  +  importProbeYaml  +  probeChannelCount
// ═══════════════════════════════════════════════════════════════════════════
//
// Walk the probe list top-to-bottom assigning channel offsets cumulatively
// and parsing each probe's geometry to derive anatomical / spike groups.
// Mirrors the original algorithm but works against the m_probes vector
// instead of a QTableWidget.

void ProbePage::recalculateAll()
{
    QMap<int,QList<int>> outAnatomy;
    QMap<int,QList<int>> outSpike;
    QSet<int> claimedChannels;
    int nextGroupId = 1;
    int cumOffset   = 0;

    for (ProbeEntry& e : m_probes) {
        e.channelOffset = cumOffset;
        e.anatomicalGroups.clear();
        e.spikeGroups.clear();

        const QString resolved = resolveProbePath(e.probeFile);
        if (resolved.isEmpty()) continue;

        const int cnt = probeChannelCount(resolved);
        if (cnt <= 0) continue;

        QMap<int,QList<int>> ra, rs;
        if (importProbeYaml(resolved, e, nextGroupId, ra, rs)) {
            for (auto it = ra.constBegin(); it != ra.constEnd(); ++it) {
                outAnatomy[it.key()] = it.value();
                for (int ch : it.value()) claimedChannels.insert(ch);
                nextGroupId = qMax(nextGroupId, it.key() + 1);
            }
            for (auto it = rs.constBegin(); it != rs.constEnd(); ++it)
                outSpike[it.key()] = it.value();
        }

        cumOffset += cnt;
    }

    // Leftover anatomical group: any acquisition channel not claimed by a probe.
    if (m_nbChannels > 0) {
        QList<int> leftover;
        for (int ch = 0; ch < m_nbChannels; ++ch)
            if (!claimedChannels.contains(ch))
                leftover.append(ch);
        if (!leftover.isEmpty())
            outAnatomy[nextGroupId] = leftover;
            // leftover NOT added to outSpike — it isn't a spike group.
    }

    // Refresh inspector readouts after group recomputation.
    refreshInspector();

    emit probesModified();
    if (!outAnatomy.isEmpty())
        emit probeLayoutImported(m_probes, outAnatomy, outSpike, 1);
}

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
        qWarning() << "ProbePage: YAML parse error in" << path << ":" << e.what();
        return false;
    }

    const YAML::Node pf = doc["probeFile"];
    if (!pf || !pf.IsMap()) {
        qWarning() << "ProbePage: no probeFile root in" << path;
        return false;
    }

    if (pf["model"] && entry.label.isEmpty())
        entry.label = QString::fromStdString(pf["model"].as<std::string>());

    const YAML::Node shanksNode = pf["shanks"];
    const YAML::Node sitesNode  = pf["sites"];
    if (!shanksNode || !sitesNode) {
        qWarning() << "ProbePage: missing shanks/sites in" << path;
        return false;
    }

    const int nShanks = shanksNode["count"] ? shanksNode["count"].as<int>() : 1;

    // sites.count_per_shank may be a scalar (uniform) or sequence (per-shank).
    QVector<int> sitesPerShank(nShanks, 0);
    const YAML::Node cps = sitesNode["count_per_shank"];
    if (cps) {
        if (cps.IsSequence()) {
            for (int s = 0; s < nShanks && s < (int)cps.size(); ++s)
                sitesPerShank[s] = cps[(std::size_t)s].as<int>(0);
        } else {
            const int v = cps.as<int>(0);
            for (int s = 0; s < nShanks; ++s) sitesPerShank[s] = v;
        }
    }
    int totalSites = 0;
    for (int v : sitesPerShank) totalSites += v;
    if (nShanks < 1 || totalSites < 1) {
        qWarning() << "ProbePage: invalid shank/site counts in" << path;
        return false;
    }

    // Channel map: optional explicit assignment.
    // Format: list of lists [[ch_s0_0, ch_s0_1, ...], [ch_s1_0, ...], ...]
    // OR flat list [ch_0, ch_1, ..., ch_total-1] (shank-major).
    // When null, use sequential assignment starting at channelOffset.
    QVector<QVector<int>> channelMap(nShanks);
    for (int s = 0; s < nShanks; ++s)
        channelMap[s].resize(sitesPerShank[s], -1);
    bool hasMap = false;

    const YAML::Node cmNode = pf["channelMap"];
    if (cmNode && cmNode.IsMap()) {
        const YAML::Node mapSeq = cmNode["map"];
        if (mapSeq && mapSeq.IsSequence() && (int)mapSeq.size() > 0) {
            hasMap = true;
            if (mapSeq[0].IsSequence()) {
                for (int s = 0; s < nShanks && s < (int)mapSeq.size(); ++s) {
                    const YAML::Node row = mapSeq[(std::size_t)s];
                    for (int c = 0; c < sitesPerShank[s] && c < (int)row.size(); ++c)
                        channelMap[s][c] = row[(std::size_t)c].as<int>();
                }
            } else {
                int idx = 0;
                for (int s = 0; s < nShanks; ++s)
                    for (int c = 0; c < sitesPerShank[s]; ++c, ++idx)
                        if (idx < (int)mapSeq.size())
                            channelMap[s][c] = mapSeq[(std::size_t)idx].as<int>();
            }
        }
    }

    if (!hasMap) {
        int siteCursor = 0;
        for (int s = 0; s < nShanks; ++s)
            for (int c = 0; c < sitesPerShank[s]; ++c, ++siteCursor)
                channelMap[s][c] = entry.channelOffset + siteCursor;
    }

    entry.anatomicalGroups.clear();
    entry.spikeGroups.clear();

    for (int s = 0; s < nShanks; ++s) {
        const int gid = nextGroupId + s;
        entry.anatomicalGroups.append(gid);
        QList<int> chs;
        for (int ch : channelMap[s]) chs.append(ch);
        outAnatomy[gid] = chs;
        outSpike[gid]   = chs;
    }
    return true;
}

int ProbePage::probeChannelCount(const QString& probePath)
{
    if (probePath.isEmpty() || !QFile::exists(probePath)) return 0;
    try {
        YAML::Node doc = YAML::LoadFile(probePath.toStdString());
        const YAML::Node pf = doc["probeFile"];
        if (!pf || !pf.IsMap()) return 0;
        if (pf["totalChannels"] && pf["totalChannels"].IsScalar())
            return pf["totalChannels"].as<int>();
        const YAML::Node shanks = pf["shanks"];
        const YAML::Node sites  = pf["sites"];
        if (!shanks || !sites) return 0;
        const int nShanks = shanks["count"] ? shanks["count"].as<int>() : 1;
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
