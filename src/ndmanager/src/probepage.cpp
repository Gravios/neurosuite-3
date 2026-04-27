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
#include <QAction>
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
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
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

    // The + button uses QToolButton with MenuButtonPopup so the icon
    // area and the drop-down arrow are separate hit targets:
    //   click "+"      → primary action (new empty probe)
    //   click  arrow   → menu with library / file alternatives
    // QPushButton::setMenu() would route both clicks to the menu,
    // making the primary action unreachable in one click.
    auto* addToolBtn = new QToolButton;
    addToolBtn->setText(QStringLiteral("+"));
    addToolBtn->setPopupMode(QToolButton::MenuButtonPopup);
    addToolBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addToolBtn->setToolTip(tr(
        "Add a new probe.  Click + for the empty template, or use the "
        "drop-down arrow for library/file imports."));
    addToolBtn->setMaximumWidth(56);

    m_removeBtn = new QPushButton(QStringLiteral("−"));
    m_browseBtn = new QPushButton(tr("Replace Geometry…"));
    m_removeBtn->setToolTip(tr("Remove the selected probe"));
    m_browseBtn->setToolTip(tr(
        "Replace the current probe's geometry with a .probe file you choose.  "
        "Asks for confirmation if you've edited the existing geometry."));
    m_removeBtn->setMaximumWidth(32);

    auto* addMenu = new QMenu(addToolBtn);
    QAction* addEmptyAct   = addMenu->addAction(tr("New (empty template)"));
    QAction* addLibraryAct = addMenu->addAction(tr("From library…"));
    QAction* addFileAct    = addMenu->addAction(tr("From file…"));
    addToolBtn->setMenu(addMenu);
    // The default (primary-button-click) action is "new empty probe",
    // matching the menu's first entry.  We don't use setDefaultAction
    // because that would make the button text track the action text;
    // the static "+" label is more recognisable.

    btnRow->addWidget(addToolBtn);
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
    m_inspFile->setReadOnly(true);
    m_inspFile->setToolTip(tr(
        "Set automatically by Add / Browse Library… / Load from file….  "
        "Cannot be edited directly — change the path via those actions."));
    m_inspFile->setStyleSheet(
        "QLineEdit:read-only{color:#9ca3af;background:#0d1117;}");
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

    // Status line — transient feedback from Browse / Add / Save.
    m_status = new QLabel;
    m_status->setStyleSheet("color:#9ca3af;font-size:11px;");
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(28);
    inspVbox->addWidget(m_status);

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

    // QToolButton::clicked fires only when the icon-area is clicked
    // (not when the drop-down arrow is clicked — that opens the menu).
    // So routing both the button click and the "New (empty template)"
    // menu action to onAddClicked gives a consistent default.
    connect(addToolBtn,    &QToolButton::clicked,
            this, &ProbePage::onAddClicked);
    connect(addEmptyAct,   &QAction::triggered,
            this, &ProbePage::onAddClicked);
    connect(addLibraryAct, &QAction::triggered,
            this, &ProbePage::onAddFromLibraryClicked);
    connect(addFileAct,    &QAction::triggered,
            this, &ProbePage::onAddFromFileClicked);
    connect(m_removeBtn,&QPushButton::clicked, this, &ProbePage::onRemoveClicked);
    connect(m_browseBtn,&QPushButton::clicked, this, &ProbePage::onBrowseLibraryClicked);

    connect(m_inspLabel,  &QLineEdit::editingFinished,
            this, &ProbePage::onLabelEdited);
    // m_inspFile is read-only — no editingFinished connection.
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
    // Default + button click — create from empty template.  Status
    // line confirms the action so the user sees that something happened
    // even when the geometry editor is at its default zoom.
    const int newId = appendProbe(QString());
    if (newId < 0) return;
    setStatus(tr("Added probe %1 (empty template).  "
                 "Edit geometry on the right, or use the + drop-down "
                 "to import a different probe instead.")
                .arg(newId));
}

void ProbePage::onAddFromLibraryClicked()
{
    // Filtered to the library directory so the dialog opens in the
    // right place; user can still navigate elsewhere.
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Add Probe — Select File from Library"),
        getLibraryPath(),
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));
    if (path.isEmpty()) return;

    const int newId = appendProbe(path);
    if (newId < 0) return;
    setStatus(tr("Added probe %1 from library: %2 → %3")
                .arg(newId)
                .arg(QFileInfo(path).fileName())
                .arg(sessionProbeFilename(newId)));
}

void ProbePage::onAddFromFileClicked()
{
    // Like Add From Library but opens at the user's home directory by
    // default (or wherever they last browsed) — for one-off probes
    // that aren't in the canonical library tree.
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Add Probe — Select File"),
        QString(),     // platform default location
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));
    if (path.isEmpty()) return;

    const int newId = appendProbe(path);
    if (newId < 0) return;
    setStatus(tr("Added probe %1 from file: %2 → %3")
                .arg(newId)
                .arg(QFileInfo(path).fileName())
                .arg(sessionProbeFilename(newId)));
}

// ---------------------------------------------------------------------------
// appendProbe — common path for + / +-from-library / +-from-file
// ---------------------------------------------------------------------------
// Allocates a fresh probe id, materialises the new <session>.probe.<id>.probe
// file (either by copying the empty template's content or copying @p sourcePath),
// appends a ProbeEntry, selects it in the list, and runs recalculateAll.
// Returns the new id, or -1 on failure.
int ProbePage::appendProbe(const QString& sourcePath)
{
    int newId = 0;
    for (const ProbeEntry& e : m_probes) newId = qMax(newId, e.id + 1);

    const QString fname = sessionProbeFilename(newId);
    const QString fpath = QDir::current().absoluteFilePath(fname);

    if (sourcePath.isEmpty()) {
        // Empty-template path.  Read content once; if installed
        // template is missing, fall back to a hardcoded skeleton so
        // the user always ends up with a valid file.
        QString srcContent;
        const QString templatePath = emptyTemplatePath();
        if (QFile::exists(templatePath)) {
            QFile f(templatePath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                srcContent = QString::fromUtf8(f.readAll());
        }
        if (srcContent.isEmpty()) {
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
                "  notes: 'Empty starter probe — edit before use.'\n");
        }
        QFile out(fpath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Add Probe"),
                tr("Cannot create %1: %2").arg(fpath, out.errorString()));
            setStatus(tr("Failed to create %1").arg(fname), /*err=*/true);
            return -1;
        }
        out.write(srcContent.toUtf8());
        out.close();
    } else {
        // Copy a chosen file.  copyProbeIntoSession handles the
        // "destination already exists" overwrite/cancel prompt; for a
        // brand-new id the destination won't exist so the prompt is
        // dormant.
        const QString localName = copyProbeIntoSession(sourcePath, newId);
        if (localName.isEmpty()) {
            setStatus(tr("Import cancelled or failed."), /*err=*/true);
            return -1;
        }
    }

    ProbeEntry e;
    e.id            = newId;
    e.probeFile     = fname;
    e.label         = QString();
    e.channelOffset = 0;
    m_probes.append(e);

    rebuildList();
    m_list->setCurrentRow(m_probes.size() - 1);
    m_modified = true;
    recalculateAll();
    return newId;
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
    setStatus(tr("Removed probe %1.").arg(victim.id));
}

// ---------------------------------------------------------------------------
// onBrowseLibraryClicked — Replace Geometry…
// ---------------------------------------------------------------------------
// Replaces the *currently selected* probe's geometry with a chosen
// .probe file.  Confirms first when the existing geometry is non-
// trivial (more than just the empty template), so a careless click
// can't wipe out hand-edited probe layouts.
void ProbePage::onBrowseLibraryClicked()
{
    if (m_currentIndex < 0) {
        setStatus(tr("Select a probe first, or use + to add one."), /*err=*/true);
        return;
    }

    ProbeEntry& e = m_probes[m_currentIndex];

    // Confirm if the current probe is non-empty.  We use a content
    // heuristic rather than a "saved-to-file" flag so the check
    // survives session reloads and works across runs.
    const QString currentResolved = resolveProbePath(e.probeFile);
    if (!currentResolved.isEmpty() && !probeIsUntouched(currentResolved)) {
        const auto reply = QMessageBox::question(
            this, tr("Replace Geometry"),
            tr("Probe %1 already has non-template geometry.  Replacing "
               "it will discard the current shanks/channels and load the "
               "new file in their place.\n\nContinue?").arg(e.id),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            setStatus(tr("Replace cancelled — current geometry kept."));
            return;
        }
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select Probe Configuration File"),
        getLibraryPath(),
        tr("Probe files (*.probe *.yaml *.yml);;All files (*)"));
    if (path.isEmpty()) {
        setStatus(tr("Replace cancelled."));
        return;
    }

    const QString localName = copyProbeIntoSession(path, e.id);
    if (localName.isEmpty()) {
        setStatus(tr("Replace failed during file copy."), /*err=*/true);
        return;
    }

    e.probeFile = localName;
    rebuildList();
    m_list->setCurrentRow(m_currentIndex);
    m_modified = true;
    recalculateAll();
    loadProbeIntoMaker(m_currentIndex);

    setStatus(tr("Imported %1 → probe %2 (%3).")
                .arg(QFileInfo(path).fileName())
                .arg(e.id)
                .arg(localName));
}

// ---------------------------------------------------------------------------
// probeIsUntouched
// ---------------------------------------------------------------------------
// Heuristic: a probe is considered "untouched" if it byte-matches the
// installed empty.probe template (or, when that's missing, has the same
// totalChannels/shanks/sites layout the hardcoded fallback produces).
//
// The byte comparison is the cheap, exact path that catches the common
// case ("user just clicked + and hasn't edited yet").  For the fallback,
// we look for telltale signs of the 1-shank/1-site skeleton.  This is
// only used to decide whether to *prompt* — false negatives just mean
// the user gets a confirmation they could have skipped.
bool ProbePage::probeIsUntouched(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray content = f.readAll();
    f.close();

    const QString templatePath = emptyTemplatePath();
    if (QFile::exists(templatePath)) {
        QFile t(templatePath);
        if (t.open(QIODevice::ReadOnly)) {
            const QByteArray templ = t.readAll();
            if (content == templ) return true;
        }
    }

    // Fallback heuristic: 1 shank, 1 site at [0, 100].  Exact match of
    // the hardcoded skeleton's distinguishing strings (totalChannels: 1
    // AND a single [0, 100] geometry entry).
    if (content.contains("totalChannels: 1") &&
        content.contains("- [0, 100]") &&
        !content.contains("- [0, 0]") &&    // not a real probe with extra sites
        content.size() < 1024)              // template is ~700 bytes
    {
        return true;
    }
    return false;
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
        setStatus(tr("Save failed for %1: %2").arg(fname, err), /*err=*/true);
        return;
    }

    if (e.probeFile != fname) {
        e.probeFile = fname;
        QSignalBlocker blocker(m_inspFile);
        m_inspFile->setText(fname);
    }
    m_modified = true;
    recalculateAll();   // geometry changed → recompute group memberships
    setStatus(tr("Saved geometry → %1").arg(fname));
}

// ---------------------------------------------------------------------------
// setStatus
// ---------------------------------------------------------------------------
// Updates the inspector's bottom status line.  Errors get red text;
// neutral messages get the default grey.  Pass an empty string to clear.
void ProbePage::setStatus(const QString& msg, bool isError)
{
    if (!m_status) return;
    m_status->setText(msg);
    m_status->setStyleSheet(isError
        ? QStringLiteral("color:#f87171;font-size:11px;")
        : QStringLiteral("color:#9ca3af;font-size:11px;"));
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
