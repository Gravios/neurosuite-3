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
#include <QSignalBlocker>

#include <exception>

namespace {

// ── .probe parsing, the rule every reader shares ───────────────────────────
// (scripts/probe_geometry.py, doc/ndmanager-plugins/formats/probe.md):
// sites.geometry lists the sites shank by shank; count_per_shank is one
// integer for every shank, or a list with one entry per shank (which then
// also fixes the number of shanks); site j is channel map[j] when
// channelMap.map is given -- a flat list, or one list per shank, flattened
// -- and channel j otherwise, both relative to the session's channelOffset.

struct ProbeLayout {
    QList<QList<int>>     shankChannels;   ///< per shank, file order, relative channels
    QList<QList<QPointF>> shankPositions;  ///< [x, y above tip] per site, parallel
    QList<QList<bool>>    shankHasPos;     ///< false where geometry is missing
    int                   nSites = 0;
    int                   span   = 0;      ///< channels used from the offset: max(sites, max(map) + 1)
    QString               model;
};

int yamlInt(const YAML::Node& n, int def)
{
    if (!n || !n.IsScalar()) return def;
    try { return n.as<int>(); } catch (...) { return def; }
}

bool yamlDouble(const YAML::Node& n, double& out)
{
    if (!n || !n.IsScalar()) return false;
    try { out = n.as<double>(); return true; } catch (...) { return false; }
}

// Fills @p out from the .probe file at @p path.  Never throws: a file that
// cannot be parsed, or a map that does not cover every site, returns false
// with the reason in @p err.
bool readProbeLayout(const QString& path, ProbeLayout& out, QString* err)
{
    out = ProbeLayout{};
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        if (err) *err = QStringLiteral("no such file");
        return false;
    }
    try {
        const YAML::Node root = YAML::LoadFile(path.toStdString());
        const YAML::Node pf = root.IsMap() ? root["probeFile"] : YAML::Node();
        if (!pf || !pf.IsMap()) {
            if (err) *err = QStringLiteral("no probeFile: section");
            return false;
        }
        if (pf["model"] && pf["model"].IsScalar())
            out.model = QString::fromStdString(pf["model"].Scalar());

        const YAML::Node shanks = pf["shanks"];
        const YAML::Node sites  = pf["sites"];
        int nShanks = 1;
        if (shanks && shanks.IsMap())
            nShanks = qMax(1, yamlInt(shanks["count"], 1));
        const YAML::Node cps  = (sites && sites.IsMap()) ? sites["count_per_shank"] : YAML::Node();
        const YAML::Node geom = (sites && sites.IsMap()) ? sites["geometry"] : YAML::Node();
        QList<int> counts;
        if (cps && cps.IsSequence()) {
            for (std::size_t i = 0; i < cps.size(); ++i)
                counts.append(qMax(0, yamlInt(cps[i], 0)));
        } else {
            const int n = qMax(0, yamlInt(cps, 0));
            for (int i = 0; i < nShanks; ++i) counts.append(n);
        }
        for (int n : counts) out.nSites += n;

        QList<int> chans;
        const YAML::Node cm = pf["channelMap"];
        const YAML::Node m  = (cm && cm.IsMap()) ? cm["map"] : YAML::Node();
        if (m && m.IsSequence() && m.size() > 0) {
            for (std::size_t i = 0; i < m.size(); ++i) {
                const YAML::Node v = m[i];
                if (v.IsSequence()) {
                    for (std::size_t k = 0; k < v.size(); ++k)
                        chans.append(yamlInt(v[k], -1));
                } else {
                    chans.append(yamlInt(v, -1));
                }
            }
            if (chans.size() != out.nSites) {
                if (err) *err = QStringLiteral("channelMap.map has %1 entries, the probe has %2 sites")
                                    .arg(chans.size()).arg(out.nSites);
                return false;
            }
            if (chans.contains(-1)) {
                if (err) *err = QStringLiteral("channelMap.map holds a non-channel entry");
                return false;
            }
        } else {
            for (int j = 0; j < out.nSites; ++j) chans.append(j);
        }

        out.span = out.nSites;
        for (int c : chans) out.span = qMax(out.span, c + 1);

        const int nGeom = (geom && geom.IsSequence()) ? static_cast<int>(geom.size()) : 0;
        int j = 0;
        for (int n : counts) {
            QList<int> sc; QList<QPointF> sp; QList<bool> sh;
            for (int k = 0; k < n; ++k, ++j) {
                sc.append(chans[j]);
                double x = 0.0, y = 0.0;
                bool known = false;
                if (j < nGeom) {
                    const YAML::Node p = geom[static_cast<std::size_t>(j)];
                    known = p.IsSequence() && p.size() >= 2
                            && yamlDouble(p[0], x) && yamlDouble(p[1], y);
                }
                sp.append(known ? QPointF(x, y) : QPointF());
                sh.append(known);
            }
            out.shankChannels.append(sc);
            out.shankPositions.append(sp);
            out.shankHasPos.append(sh);
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = QString::fromUtf8(e.what());
        out = ProbeLayout{};
        return false;
    }
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════

ProbePage::ProbePage(QWidget* parent)
    : QWidget(parent)
{
    // libraryPath stays empty unless the session names one
    // (probeLibraryPath): the installed libraries are found at run time by
    // librarySearchDirs(), so one machine's install prefix is never
    // written into the session.
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

    // ── Outer two-pane splitter ─────────────────────────────────────────
    // Left pane stacks the probe list above the inspector vertically;
    // packing the inspector under the list (instead of beside it) gives
    // the geometry editor on the right ~70% of the page width to work
    // in.  The outer splitter lets the user resize the left vs right
    // proportion; an inner vertical splitter on the left lets them
    // resize list vs inspector.
    split = new QSplitter(Qt::Horizontal, this);
    split->setHandleWidth(2);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, /*stretch=*/1);

    // ── Left column: list (top) above inspector (bottom) ────────────────
    auto* leftSplit = new QSplitter(Qt::Vertical);
    leftSplit->setHandleWidth(2);
    leftSplit->setChildrenCollapsible(false);

    // ── List pane ──────────────────────────────────────────────────────
    auto* listPane = new QWidget;
    auto* listVbox = new QVBoxLayout(listPane);
    listVbox->setContentsMargins(8, 8, 8, 4);
    listVbox->setSpacing(6);

    auto* listHdr = new QLabel(tr("PROBES"));
    listHdr->setStyleSheet(
        "color:#4a5568;font-size:10px;font-weight:bold;letter-spacing:2px;");
    listVbox->addWidget(listHdr);

    list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setAlternatingRowColors(true);
    listVbox->addWidget(list, /*stretch=*/1);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);

    // The + button uses QToolButton with MenuButtonPopup so the icon
    // area and the drop-down arrow are separate hit targets:
    //   click "+"      → primary action (new empty probe)
    //   click  arrow   → menu with library / file alternatives
    auto* addToolBtn = new QToolButton;
    addToolBtn->setText(QStringLiteral("+"));
    addToolBtn->setPopupMode(QToolButton::MenuButtonPopup);
    addToolBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addToolBtn->setToolTip(tr(
        "Add a new probe.  Click + for the empty template, or use the "
        "drop-down arrow for library/file imports."));
    addToolBtn->setMaximumWidth(56);

    removeBtn = new QPushButton(QStringLiteral("−"));
    browseBtn = new QPushButton(tr("Replace…"));
    removeBtn->setToolTip(tr("Remove the selected probe"));
    browseBtn->setToolTip(tr(
        "Replace the current probe's geometry with a .probe file you choose.  "
        "Asks for confirmation if you've edited the existing geometry."));
    removeBtn->setMaximumWidth(32);

    auto* addMenu = new QMenu(addToolBtn);
    QAction* addEmptyAct   = addMenu->addAction(tr("New (empty template)"));
    QAction* addLibraryAct = addMenu->addAction(tr("From library…"));
    QAction* addFileAct    = addMenu->addAction(tr("From file…"));
    addToolBtn->setMenu(addMenu);

    btnRow->addWidget(addToolBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addWidget(browseBtn);
    btnRow->addStretch();
    listVbox->addLayout(btnRow);

    leftSplit->addWidget(listPane);

    // ── Inspector pane (under the list) ────────────────────────────────
    auto* inspPane = new QWidget;
    auto* inspVbox = new QVBoxLayout(inspPane);
    inspVbox->setContentsMargins(8, 4, 8, 8);
    inspVbox->setSpacing(8);

    auto* inspHdr = new QLabel(tr("INSPECTOR"));
    inspHdr->setStyleSheet(
        "color:#4a5568;font-size:10px;font-weight:bold;letter-spacing:2px;");
    inspVbox->addWidget(inspHdr);

    auto* inspForm = new QFormLayout;
    inspForm->setLabelAlignment(Qt::AlignRight);
    inspForm->setFormAlignment(Qt::AlignTop);
    inspForm->setSpacing(6);

    inspLabel  = new QLineEdit;
    inspFile   = new QLineEdit;
    inspFile->setReadOnly(true);
    inspFile->setToolTip(tr(
        "Set automatically by Add / Replace….  "
        "Cannot be edited directly — change the path via those actions."));
    inspFile->setStyleSheet(
        "QLineEdit:read-only{color:#9ca3af;background:#0d1117;}");
    inspOffset = new QSpinBox;
    inspOffset->setRange(0, 100000);
    inspOffset->setToolTip(tr(
        "First recording channel of this probe (absolute).  By default it "
        "follows the previous probe (its offset + its channel count); a "
        "value you type is kept.  Type the automatic value to make it "
        "follow again."));

    inspForm->addRow(tr("Label:"),          inspLabel);
    inspForm->addRow(tr("Probe file:"),     inspFile);
    inspForm->addRow(tr("Channel offset:"), inspOffset);

    inspAnatomy = new QLabel;
    inspSpike   = new QLabel;
    inspAnatomy->setTextFormat(Qt::PlainText);
    inspSpike  ->setTextFormat(Qt::PlainText);
    inspForm->addRow(tr("Anatomical groups:"), inspAnatomy);
    inspForm->addRow(tr("Spike groups:"),      inspSpike);

    inspVbox->addLayout(inspForm);
    inspVbox->addStretch(1);

    // Status line — transient feedback from Browse / Add / Save.
    status = new QLabel;
    status->setStyleSheet("color:#9ca3af;font-size:11px;");
    status->setWordWrap(true);
    status->setMinimumHeight(28);
    inspVbox->addWidget(status);

    leftSplit->addWidget(inspPane);

    // Inner-split sizing: the list takes the bulk; inspector is just
    // tall enough for its 5 rows + status line (~190px).  User can
    // drag the handle to redistribute.
    leftSplit->setStretchFactor(0, 3);
    leftSplit->setStretchFactor(1, 2);
    leftSplit->setSizes({280, 200});

    split->addWidget(leftSplit);

    // ── Right column: embedded ProbeMakerPage ───────────────────────────
    maker = new ProbeMakerPage;
    split->addWidget(maker);

    // Outer split: narrow left column (~25%), wide geometry editor (~75%).
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    split->setSizes({280, 900});

    // ── Signals ────────────────────────────────────────────────────────
    connect(list,     &QListWidget::currentRowChanged,
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
    connect(removeBtn,&QPushButton::clicked, this, &ProbePage::onRemoveClicked);
    connect(browseBtn,&QPushButton::clicked, this, &ProbePage::onBrowseLibraryClicked);

    connect(inspLabel,  &QLineEdit::editingFinished,
            this, &ProbePage::onLabelEdited);
    // inspFile is read-only — no editingFinished connection.
    connect(inspOffset, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int){ onOffsetEdited(); });

    // Every maker edit is written to the session's probe file at once;
    // the maker's Save button writes it again on demand.
    connect(maker, &ProbeMakerPage::edited,
            this, &ProbePage::onMakerEdited);
    connect(maker, &ProbeMakerPage::saveRequested,
            this, &ProbePage::saveCurrentProbeFile);
    connect(maker, &ProbeMakerPage::statusMessage,
            this, &ProbePage::setStatus);
}

// ═══════════════════════════════════════════════════════════════════════════
// Data setters / getters
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::setProbes(const QList<ProbeEntry>& probes)
{
    this->probes = probes;
    probeFilesDir = sessionDir();

    // Which offsets the user set (kept by recalculateAll), and the layout
    // the session already has: recalculateAll rebuilds the anatomy and
    // spike pages only when the probe-derived groups differ from it.
    classifyOffsets();
    lastAnatomy.clear();
    lastSpike.clear();
    {
        QList<ProbeEntry> copy = this->probes;
        QMap<int,QList<int>> a, s;
        QStringList warnings;
        if (computeLayout(copy, a, s, warnings)) {
            lastAnatomy = a;
            lastSpike   = s;
        }
    }

    rebuildList();
    if (!probes.isEmpty()) {
        list->setCurrentRow(0);
    } else {
        currentIndex = -1;
        refreshInspector();
    }
    modified = false;
}

void ProbePage::getProbes(QList<ProbeEntry>& probes) const
{
    probes = this->probes;
}

void ProbePage::setLibraryPath(const QString& path)
{
    libraryPath = path;
}

QString ProbePage::getLibraryPath() const
{
    return libraryPath;
}

// ═══════════════════════════════════════════════════════════════════════════
// List management
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::rebuildList()
{
    QSignalBlocker blocker(list);
    list->clear();
    for (const ProbeEntry& e : probes) {
        const QString label = e.label.isEmpty()
            ? tr("Probe %1").arg(e.id)
            : tr("Probe %1 — %2").arg(e.id).arg(e.label);
        auto* item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, e.id);
    }
}

void ProbePage::onListSelectionChanged()
{
    const int row = list->currentRow();
    if (row < 0 || row >= probes.size()) {
        currentIndex = -1;
        makerHasProbe = false;
        refreshInspector();
        maker->clearToConnector();
        return;
    }
    currentIndex = row;
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
        defaultLibraryDir(),
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
    // Session-local probe files live next to the session file, so a
    // session that has never been saved has nowhere to put one.
    if (sessionDir().isEmpty()) {
        setStatus(tr("Save the session first: its probe files are stored "
                     "next to the session file."), /*err=*/true);
        return -1;
    }

    int newId = 0;
    for (const ProbeEntry& e : probes) newId = qMax(newId, e.id + 1);

    const QString fname = sessionProbeFilename(newId);
    const QString fpath = sessionProbePath(newId);

    QString source = sourcePath;
    if (source.isEmpty()) source = emptyTemplatePath();

    if (!source.isEmpty() && QFile::exists(source)) {
        // Copy the chosen file, or the installed empty template.
        // copyProbeIntoSession asks before replacing an existing file.
        const QString localName = copyProbeIntoSession(source, newId);
        if (localName.isEmpty()) {
            setStatus(tr("Import cancelled or failed."), /*err=*/true);
            return -1;
        }
    } else if (!sourcePath.isEmpty()) {
        setStatus(tr("%1 does not exist.").arg(sourcePath), /*err=*/true);
        return -1;
    } else {
        // No installed template: write a minimal valid probe so the user
        // always ends up with a file to edit -- unless one is already
        // there and the user wants to keep it.
        const ExistingFile choice = askAboutExistingFile(fpath);
        if (choice == ExistingFile::Cancel) {
            setStatus(tr("Add cancelled."));
            return -1;
        }
        if (choice == ExistingFile::Write) {
            const QString srcContent = QStringLiteral(
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
                "    geometry:            # [x_um, y_um], y = height above the tip\n"
                "      - [0, 100]\n"
                "  channelMap:\n"
                "    description: 'Sequential.'\n"
                "    map: null\n"
                "  notes: 'Empty starter probe — edit before use.'\n");
            QFile out(fpath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QMessageBox::warning(this, tr("Add Probe"),
                    tr("Cannot create %1: %2").arg(fpath, out.errorString()));
                setStatus(tr("Failed to create %1").arg(fname), /*err=*/true);
                return -1;
            }
            out.write(srcContent.toUtf8());
            out.close();
        }
    }

    ProbeEntry e;
    e.id            = newId;
    e.probeFile     = fname;
    e.label         = QString();
    e.channelOffset = 0;
    probes.append(e);

    rebuildList();
    list->setCurrentRow(probes.size() - 1);
    modified = true;
    recalculateAll();
    return newId;
}

void ProbePage::onRemoveClicked()
{
    if (currentIndex < 0 || currentIndex >= probes.size()) return;
    const ProbeEntry victim = probes[currentIndex];

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
        const QString fpath = sessionDir().isEmpty() ? QString()
            : QDir(sessionDir()).absoluteFilePath(victim.probeFile);
        if (QFile::exists(fpath))
            QFile::remove(fpath);
    }

    manualOffsetIds.remove(victim.id);
    knownSpan.remove(victim.id);
    probes.removeAt(currentIndex);
    if (probes.isEmpty()) {
        currentIndex = -1;
    } else {
        currentIndex = qMin(currentIndex, probes.size() - 1);
    }
    rebuildList();
    if (currentIndex >= 0)
        list->setCurrentRow(currentIndex);
    else
        refreshInspector();
    modified = true;
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
    if (currentIndex < 0) {
        setStatus(tr("Select a probe first, or use + to add one."), /*err=*/true);
        return;
    }

    ProbeEntry& e = probes[currentIndex];

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
        defaultLibraryDir(),
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
    list->setCurrentRow(currentIndex);
    modified = true;
    recalculateAll();
    loadProbeIntoMaker(currentIndex);

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
    const bool hasSel = (currentIndex >= 0 && currentIndex < probes.size());

    inspLabel ->setEnabled(hasSel);
    // With no probe selected, or a probe file that could not be read, an
    // edit in the geometry editor would have no file to go to -- or would
    // overwrite the unreadable one with an empty probe.
    maker     ->setEnabled(hasSel && makerHasProbe);
    inspFile  ->setEnabled(hasSel);
    inspOffset->setEnabled(hasSel);
    browseBtn ->setEnabled(hasSel);
    removeBtn ->setEnabled(hasSel);

    QSignalBlocker bL(inspLabel);
    QSignalBlocker bF(inspFile);
    QSignalBlocker bO(inspOffset);

    if (!hasSel) {
        inspLabel ->clear();
        inspFile  ->clear();
        inspOffset->setValue(0);
        inspAnatomy->setText(QStringLiteral("—"));
        inspSpike  ->setText(QStringLiteral("—"));
        return;
    }

    const ProbeEntry& e = probes[currentIndex];
    inspLabel ->setText(e.label);
    inspFile  ->setText(e.probeFile);
    inspOffset->setValue(e.channelOffset);
    updateGroupsLabels();
}

void ProbePage::updateGroupsLabels()
{
    if (currentIndex < 0 || currentIndex >= probes.size()) {
        inspAnatomy->setText(QStringLiteral("—"));
        inspSpike  ->setText(QStringLiteral("—"));
        return;
    }
    const ProbeEntry& e = probes[currentIndex];
    QStringList anatStrs;  for (int g : e.anatomicalGroups) anatStrs.append(QString::number(g));
    QStringList spikeStrs; for (int g : e.spikeGroups)      spikeStrs.append(QString::number(g));
    inspAnatomy->setText(anatStrs.isEmpty()  ? tr("—") : anatStrs.join(QStringLiteral(", ")));
    inspSpike  ->setText(spikeStrs.isEmpty() ? tr("(same as anatomical)")
                                               : spikeStrs.join(QStringLiteral(", ")));
}

void ProbePage::onLabelEdited()
{
    if (currentIndex < 0) return;
    probes[currentIndex].label = inspLabel->text();
    rebuildList();
    list->setCurrentRow(currentIndex);
    modified = true;
    emit probesModified();
}

void ProbePage::onOffsetEdited()
{
    if (currentIndex < 0) return;
    // A typed offset is kept from now on; typing the automatic value
    // (previous probe's offset + its channel count) makes it follow the
    // previous probe again.
    ProbeEntry& e = probes[currentIndex];
    const int value = inspOffset->value();
    if (value == automaticOffset(currentIndex))
        manualOffsetIds.remove(e.id);
    else
        manualOffsetIds.insert(e.id);
    e.channelOffset = value;
    modified = true;
    recalculateAll();
}

// ═══════════════════════════════════════════════════════════════════════════
// Geometry editor wiring
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::loadProbeIntoMaker(int probeIndex)
{
    if (!maker || probeIndex < 0 || probeIndex >= probes.size()) return;

    const ProbeEntry& e = probes[probeIndex];
    const QString resolved = resolveProbePath(e.probeFile);
    makerHasProbe = false;
    if (resolved.isEmpty()) {
        maker->clearToConnector();
        maker->setEnabled(false);
        setStatus(tr("Probe %1: file %2 not found — geometry editor disabled.")
                    .arg(e.id).arg(e.probeFile), /*err=*/true);
        return;
    }

    loadingMaker = true;
    QString err;
    const bool ok = maker->loadFromFile(resolved, &err);
    loadingMaker = false;

    if (!ok) {
        qWarning() << "ProbePage: maker failed to load" << resolved << ":" << err;
        maker->clearToConnector();
        maker->setEnabled(false);
        setStatus(tr("Probe %1: cannot read %2: %3 — geometry editor disabled.")
                    .arg(e.id).arg(resolved, err), /*err=*/true);
        return;
    }
    makerHasProbe = true;
    maker->setEnabled(true);
}

void ProbePage::onMakerEdited()
{
    if (loadingMaker || !makerHasProbe) return;
    if (currentIndex < 0 || currentIndex >= probes.size()) return;
    saveCurrentProbeFile();
}

void ProbePage::saveCurrentProbeFile()
{
    if (currentIndex < 0 || currentIndex >= probes.size() || !makerHasProbe) return;
    ProbeEntry& e = probes[currentIndex];
    if (e.id < 0) return;

    const QString dir = sessionDir();
    if (dir.isEmpty()) {
        setStatus(tr("Not saved — save the session first: its probe files "
                     "are stored next to the session file."), /*err=*/true);
        return;
    }
    // A probe whose file already sits in the session directory is written
    // in place (whatever its name).  One that uses a library or other file
    // gets its own session copy, <session>.probe.<id>.probe, and is
    // pointed at it; a file of that name is only replaced if the user
    // agrees.
    QString fname = e.probeFile;
    QString fpath;
    if (!fname.isEmpty() && !QDir::isAbsolutePath(fname)
        && QFileInfo(QDir(dir).absoluteFilePath(fname)).isFile()) {
        fpath = QDir(dir).absoluteFilePath(fname);
    } else {
        fname = sessionProbeFilename(e.id);
        fpath = sessionProbePath(e.id);
        if (QFile::exists(fpath)) {
            const auto reply = QMessageBox::question(
                this, tr("Probe file exists"),
                tr("Probe %1 uses %2, so its edits go to a session copy, but\n\n"
                   "    %3\n\nalready exists.  Overwrite it with this probe's "
                   "geometry?").arg(e.id).arg(e.probeFile, fname),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                setStatus(tr("Not saved — %1 exists.").arg(fname), /*err=*/true);
                return;
            }
        }
    }
    QString err;
    if (!maker->saveToFile(fpath, &err)) {
        qWarning() << "ProbePage: maker save failed for" << fpath << ":" << err;
        setStatus(tr("Not saved — %1: %2").arg(fname, err), /*err=*/true);
        return;
    }
    maker->markSaved();

    if (e.probeFile != fname) {
        e.probeFile = fname;
        QSignalBlocker blocker(inspFile);
        inspFile->setText(fname);
    }
    modified = true;
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
    if (!status) return;
    status->setText(msg);
    status->setStyleSheet(isError
        ? QStringLiteral("color:#f87171;font-size:11px;")
        : QStringLiteral("color:#9ca3af;font-size:11px;"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Path / filename helpers
// ═══════════════════════════════════════════════════════════════════════════

void ProbePage::setSessionFileProvider(std::function<QString()> provider)
{
    sessionFileProvider = std::move(provider);
    probeFilesDir = sessionDir();
}

QString ProbePage::sessionDir() const
{
    // The directory of the session file as it is on disk.  A document that
    // has no file yet (File > New, before the first save) has none.
    const QString file = sessionFileProvider ? sessionFileProvider() : QString();
    if (file.isEmpty()) return QString();
    const QFileInfo fi(file);
    if (!fi.isFile()) return QString();
    return fi.absolutePath();
}

QString ProbePage::sessionStem() const
{
    const QString file = sessionFileProvider ? sessionFileProvider() : QString();
    if (file.isEmpty()) return QString();
    const QFileInfo fi(file);
    // session.yaml -> session; a name without a parameter-file extension
    // is used whole (as ParameterView::sessionBasePath does).
    const QString suffix = fi.suffix().toLower();
    if (suffix == QLatin1String("yaml") || suffix == QLatin1String("yml")
        || suffix == QLatin1String("xml"))
        return fi.completeBaseName();
    return fi.fileName();
}

QString ProbePage::sessionProbeFilename(int probeId) const
{
    return QStringLiteral("%1.probe.%2.probe")
            .arg(sessionStem())
            .arg(probeId);
}

QString ProbePage::sessionProbePath(int probeId) const
{
    const QString dir = sessionDir();
    if (dir.isEmpty()) return QString();
    return QDir(dir).absoluteFilePath(sessionProbeFilename(probeId));
}

ProbePage::ExistingFile ProbePage::askAboutExistingFile(const QString& path)
{
    if (!QFile::exists(path)) return ExistingFile::Write;
    const auto reply = QMessageBox::question(
        this, tr("Probe file exists"),
        tr("A probe file named\n\n    %1\n\nalready exists.  Overwrite it?  "
           "Choose No to use the existing file as it is.")
            .arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::No);
    if (reply == QMessageBox::Yes) return ExistingFile::Write;
    if (reply == QMessageBox::No)  return ExistingFile::Keep;
    return ExistingFile::Cancel;
}

QStringList ProbePage::librarySearchDirs() const
{
    // Probe libraries, first match wins (doc/ndmanager/README.md,
    // "Probe library"; ndm_setupgroups searches in the same order):
    //   1. the session's probeLibraryPath
    //   2. $NEUROSUITE_PROBE_PATH, then the legacy $NS3_PROBE_LIBRARY_PATH
    //      (each a list separated like PATH)
    //   3. neurosuite/probes under every generic data directory:
    //      ~/.local/share, then $XDG_DATA_DIRS (/usr/local/share,
    //      /usr/share) on Linux
    QStringList dirs;
    auto add = [&dirs](const QString& d) {
        if (d.isEmpty()) return;
        const QString clean = QDir::cleanPath(d);
        if (!dirs.contains(clean)) dirs.append(clean);
    };
    add(libraryPath);
    for (const char* var : { "NEUROSUITE_PROBE_PATH", "NS3_PROBE_LIBRARY_PATH" }) {
        const QString value = QString::fromLocal8Bit(qgetenv(var));
        for (const QString& d : value.split(QDir::listSeparator(), Qt::SkipEmptyParts))
            add(d);
    }
    for (const QString& d : QStandardPaths::locateAll(
             QStandardPaths::GenericDataLocation, QStringLiteral("neurosuite/probes"),
             QStandardPaths::LocateDirectory))
        add(d);
    return dirs;
}

QString ProbePage::defaultLibraryDir() const
{
    for (const QString& d : librarySearchDirs())
        if (QFileInfo(d).isDir()) return d;
    return QString();
}

QString ProbePage::resolveProbePath(const QString& cellValue) const
{
    // Search order: an absolute path as is; then relative to the session
    // directory, its probes/ subdirectory, and each library directory of
    // librarySearchDirs() in turn.
    if (cellValue.isEmpty()) return QString();
    if (QDir::isAbsolutePath(cellValue))
        return QFileInfo(cellValue).isFile() ? cellValue : QString();
    QStringList bases;
    const QString dir = sessionDir();
    if (!dir.isEmpty()) {
        bases.append(dir);
        bases.append(QDir(dir).absoluteFilePath(QStringLiteral("probes")));
    }
    bases += librarySearchDirs();
    for (const QString& base : bases) {
        const QString candidate = QDir(base).absoluteFilePath(cellValue);
        if (QFileInfo(candidate).isFile()) return candidate;
    }
    return QString();
}

QString ProbePage::emptyTemplatePath() const
{
    // <library>/general/empty.probe from the first library that has one.
    for (const QString& d : librarySearchDirs()) {
        const QString p = QDir(d).absoluteFilePath(QStringLiteral("general/empty.probe"));
        if (QFileInfo(p).isFile()) return p;
    }
    return QString();
}

QString ProbePage::copyProbeIntoSession(const QString& srcPath, int probeId)
{
    const QString baseName  = sessionProbeFilename(probeId);
    const QString localPath = sessionProbePath(probeId);
    if (localPath.isEmpty()) {
        setStatus(tr("Save the session first: its probe files are stored "
                     "next to the session file."), /*err=*/true);
        return QString();
    }

    if (QFileInfo(srcPath).canonicalFilePath()
        == QFileInfo(localPath).canonicalFilePath())
        return baseName;

    switch (askAboutExistingFile(localPath)) {
    case ExistingFile::Cancel: return QString();
    case ExistingFile::Keep:   return baseName;
    case ExistingFile::Write:  break;
    }
    if (QFile::exists(localPath) && !QFile::remove(localPath)) {
        QMessageBox::warning(this, tr("Copy Failed"),
            tr("Cannot remove existing %1").arg(localPath));
        return QString();
    }

    if (!QFile::copy(srcPath, localPath)) {
        QMessageBox::warning(this, tr("Copy Failed"),
            tr("Cannot copy\n  %1\nto\n  %2")
                .arg(srcPath, QFileInfo(localPath).absolutePath()));
        return QString();
    }
    // QFile::copy keeps the source's permissions; a read-only library file
    // must give a session copy the Probe Maker can write.
    QFile::setPermissions(localPath, QFile::permissions(localPath)
                          | QFileDevice::WriteOwner | QFileDevice::ReadOwner);
    return baseName;
}

void ProbePage::resetModificationStatus()
{
    modified = false;

    // After Save As into another directory the session's own probe files
    // are still next to the old session file, but the saved document
    // names them relative to the new one.  Copy them across (never over
    // an existing file) so the new session opens with its geometry.
    const QString newDir = sessionDir();
    if (newDir.isEmpty()) return;
    if (!probeFilesDir.isEmpty()
        && QDir(probeFilesDir).canonicalPath() != QDir(newDir).canonicalPath()) {
        QStringList copied, kept;
        for (const ProbeEntry& e : probes) {
            if (e.probeFile.isEmpty() || QDir::isAbsolutePath(e.probeFile))
                continue;
            const QString from = QDir(probeFilesDir).absoluteFilePath(e.probeFile);
            if (!QFileInfo(from).isFile()) continue;   // a library file
            const QString to = QDir(newDir).absoluteFilePath(e.probeFile);
            if (QFile::exists(to)) { kept.append(e.probeFile); continue; }
            QDir().mkpath(QFileInfo(to).absolutePath());
            if (QFile::copy(from, to)) copied.append(e.probeFile);
        }
        if (!kept.isEmpty())
            setStatus(tr("Saved to a new directory; it already had %1, kept "
                         "as is.").arg(kept.join(QStringLiteral(", "))), /*err=*/true);
        else if (!copied.isEmpty())
            setStatus(tr("Copied %1 next to the saved session.")
                        .arg(copied.join(QStringLiteral(", "))));
    }
    probeFilesDir = newDir;
}

// ═══════════════════════════════════════════════════════════════════════════
// recalculateAll  +  computeLayout  +  probeChannelCount
// ═══════════════════════════════════════════════════════════════════════════
//
// Channel offsets (probes[].channelOffset) are absolute ADC channels.  A
// probe's offset is "automatic" -- it follows the previous probe, i.e. the
// previous offset plus that probe's span -- unless the user set it: an
// offset typed in the inspector, or one that already differed from the
// automatic value when the session was loaded, is kept as it is.  A probe
// whose file cannot be read keeps the span it had (from the next probe's
// offset at load, or from its file when last read), so the probes after it
// do not move; the anatomy and spike pages are then left alone, since
// that probe's groups cannot be rebuilt.

int ProbePage::probeSpan(const ProbeEntry& e) const
{
    const int n = probeChannelCount(resolveProbePath(e.probeFile));
    return n > 0 ? n : knownSpan.value(e.id, 0);
}

int ProbePage::automaticOffset(int index) const
{
    if (index <= 0 || index > probes.size()) return 0;
    const ProbeEntry& prev = probes[index - 1];
    return prev.channelOffset + probeSpan(prev);
}

void ProbePage::classifyOffsets()
{
    manualOffsetIds.clear();
    knownSpan.clear();
    for (int i = 0; i < probes.size(); ++i) {
        const ProbeEntry& e = probes[i];
        const int n = probeChannelCount(resolveProbePath(e.probeFile));
        if (n > 0) {
            knownSpan.insert(e.id, n);
        } else if (i + 1 < probes.size()
                   && probes[i + 1].channelOffset > e.channelOffset) {
            // Unreadable file: assume it covers the channels up to the
            // next probe, as the stored offsets say.
            knownSpan.insert(e.id, probes[i + 1].channelOffset - e.channelOffset);
        }
        if (e.channelOffset != automaticOffset(i))
            manualOffsetIds.insert(e.id);
    }
}

bool ProbePage::computeLayout(QList<ProbeEntry>&    list,
                              QMap<int,QList<int>>& outAnatomy,
                              QMap<int,QList<int>>& outSpike,
                              QStringList&          warnings)
{
    bool complete = true;
    QMap<int,int> claimedBy;          // channel -> probe id
    int nextGroupId = 1;
    int prevEnd     = 0;

    for (ProbeEntry& e : list) {
        if (!manualOffsetIds.contains(e.id))
            e.channelOffset = prevEnd;
        e.anatomicalGroups.clear();
        e.spikeGroups.clear();

        const QString resolved = resolveProbePath(e.probeFile);
        ProbeLayout layout;
        QString err;
        int span = 0;
        if (resolved.isEmpty()) {
            err = tr("%1 not found").arg(e.probeFile);
        } else if (readProbeLayout(resolved, layout, &err) && layout.nSites > 0) {
            span = layout.span;
            knownSpan.insert(e.id, span);
            if (e.label.isEmpty() && !layout.model.isEmpty())
                e.label = layout.model;
            for (int s = 0; s < layout.shankChannels.size(); ++s) {
                const int gid = nextGroupId++;
                e.anatomicalGroups.append(gid);
                QList<int> chs;
                for (int rel : layout.shankChannels[s]) {
                    const int ch = e.channelOffset + rel;
                    chs.append(ch);
                    if (claimedBy.contains(ch) && claimedBy.value(ch) != e.id)
                        warnings.append(tr("channel %1 is on probe %2 and probe %3")
                                          .arg(ch).arg(claimedBy.value(ch)).arg(e.id));
                    claimedBy.insert(ch, e.id);
                    if (nbChannels > 0 && ch >= nbChannels)
                        warnings.append(tr("probe %1 uses channel %2, the recording has %3")
                                          .arg(e.id).arg(ch).arg(nbChannels));
                }
                outAnatomy[gid] = chs;
                outSpike[gid]   = chs;
            }
        } else if (err.isEmpty()) {
            err = tr("%1 has no sites").arg(e.probeFile);
        }

        if (span == 0) {
            // Keep the channels this probe had, so later probes stay put.
            complete = false;
            span = knownSpan.value(e.id, 0);
            warnings.append(span > 0
                ? tr("probe %1: %2; keeping channels %3–%4")
                      .arg(e.id).arg(err).arg(e.channelOffset).arg(e.channelOffset + span - 1)
                : tr("probe %1: %2; its channel count is unknown").arg(e.id).arg(err));
        }
        prevEnd = e.channelOffset + span;
    }

    // Leftover anatomical group: any acquisition channel not claimed by a
    // probe.  Not a spike group.
    if (nbChannels > 0) {
        QList<int> leftover;
        for (int ch = 0; ch < nbChannels; ++ch)
            if (!claimedBy.contains(ch))
                leftover.append(ch);
        if (!leftover.isEmpty())
            outAnatomy[nextGroupId] = leftover;
    }
    return complete;
}

void ProbePage::recalculateAll()
{
    QMap<int,QList<int>> outAnatomy;
    QMap<int,QList<int>> outSpike;
    QStringList warnings;
    const bool complete = computeLayout(probes, outAnatomy, outSpike, warnings);

    // Refresh inspector readouts after group recomputation.
    refreshInspector();
    warnings.removeDuplicates();
    if (!warnings.isEmpty())
        setStatus(warnings.join(QStringLiteral("\n")), /*err=*/true);

    emit probesModified();
    // The anatomy and spike pages are rebuilt only when the probe-derived
    // groups changed (a pad drag or a label edit leaves them as they are,
    // with any hand edits), and never from an incomplete layout.
    if (!complete || outAnatomy.isEmpty()) return;
    if (outAnatomy == lastAnatomy && outSpike == lastSpike) return;
    lastAnatomy = outAnatomy;
    lastSpike   = outSpike;
    emit probeLayoutImported(probes, outAnatomy, outSpike, 1);
}

int ProbePage::probeChannelCount(const QString& probePath)
{
    ProbeLayout layout;
    if (!readProbeLayout(probePath, layout, nullptr)) return 0;
    return layout.span;
}
