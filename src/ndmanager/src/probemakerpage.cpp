/***************************************************************************
 * probemakerpage.cpp
 *
 * Implementation of the Probe Maker page.  See probemakerpage.h and
 * doc/design/probe-maker.md for design rationale.
 *
 * The page owns a single `ProbeConnector` data instance.  Two
 * QGraphicsView panes (logical and physical) and a shared inspector
 * all observe the same data; edits from any of them flow back into
 * the data model and trigger view refreshes.
 *
 * File I/O uses yaml-cpp (already a hard dep via libklustersshared)
 * and writes the canonical `probeFile:` schema described in
 * doc/design/probe-maker.md.  The format is identical to the library
 * files in src/nphys-data/src/probes/.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "probemakerpage.h"
#include "probemakeritems.h"
#include "probemakerviews.h"

#include <algorithm>

#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QVBoxLayout>

#include <yaml-cpp/yaml.h>

using probemaker::ChannelItem;
using probemaker::ConnectorItem;
using probemaker::ItemTypeChannel;
using probemaker::ItemTypeConnector;
using probemaker::ItemTypeLogicalNode;
using probemaker::ItemTypeShank;
using probemaker::LogicalEdgeItem;
using probemaker::LogicalNodeItem;
using probemaker::ProbeLogicalView;
using probemaker::ProbePhysicalView;
using probemaker::RoleItemKind;
using probemaker::RoleLogicalKey;
using probemaker::RoleModelPtr;
using probemaker::RoleObjectId;
using probemaker::ShankItem;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers — local to the translation unit
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Stable composite-key generators for ProbeMakerPage::logicalState.
// See probemakerpage.h for the rationale (QList reallocation can
// invalidate raw model pointers; stable string keys avoid that).
//
// Format:
//   connector       → "C"
//   shank   <sid>   → "S:<sid>"
//   channel <sid> <hw> → "H:<sid>:<hw>"
//
// Different prefixes ensure no collisions across kinds.  Shank id
// strings are auto-generated as "shank<N>" and not user-editable, so
// they're stable for the session.

inline QString keyForConnector()
{
    return QStringLiteral("C");
}
inline QString keyForShank(const ProbeShank& s)
{
    return QStringLiteral("S:%1").arg(s.id);
}
inline QString keyForChannel(const ProbeShank& s, const ProbeChannel& c)
{
    return QStringLiteral("H:%1:%2").arg(s.id).arg(c.hardwareId);
}

// Quote a string for safe inclusion as a YAML scalar value.  Empty
// strings, strings containing special characters (anything not in a
// conservative plain-scalar safe set), and strings starting with a
// reserved prefix get double-quoted with backslash-escaped " and \.
// Strings that are clearly safe pass through unchanged so the output
// stays readable.
//
// Conservative on purpose: when in doubt, quote.  False-positive
// quoting just adds two chars; false-negatives produce malformed YAML.
inline QString yamlEscape(const QString& s)
{
    if (s.isEmpty()) return QStringLiteral("\"\"");

    // Plain-safe iff every char is alnum, '-', '_', '.', or space —
    // AND the value is not one of YAML's reserved literals (true,
    // false, null, ~, yes, no, on, off — all case-insensitive) — AND
    // it doesn't start with whitespace, '-', '?', or a digit-leading
    // sequence that could be parsed as a number.
    bool plain = true;
    for (QChar ch : s) {
        if (!(ch.isLetterOrNumber() || ch == '-' || ch == '_'
              || ch == '.' || ch == ' ')) {
            plain = false; break;
        }
    }
    if (plain) {
        if (s.front().isSpace() || s.back().isSpace()) plain = false;
        const QString lower = s.toLower();
        if (lower == QLatin1String("true")  || lower == QLatin1String("false")
         || lower == QLatin1String("null")  || lower == QLatin1String("~")
         || lower == QLatin1String("yes")   || lower == QLatin1String("no")
         || lower == QLatin1String("on")    || lower == QLatin1String("off"))
            plain = false;
    }
    if (plain) return s;

    QString out = QStringLiteral("\"");
    for (QChar ch : s) {
        if (ch == '\\')      out += QLatin1String("\\\\");
        else if (ch == '"')  out += QLatin1String("\\\"");
        else if (ch == '\n') out += QLatin1String("\\n");
        else if (ch == '\t') out += QLatin1String("\\t");
        else                 out += ch;
    }
    out += '"';
    return out;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════

ProbeMakerPage::ProbeMakerPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outerVbox = new QVBoxLayout(this);
    outerVbox->setContentsMargins(0, 0, 0, 0);
    outerVbox->setSpacing(0);

    // ── Outer toolbar ────────────────────────────────────────────────────
    auto* toolbar = new QWidget;
    toolbar->setFixedHeight(40);
    toolbar->setStyleSheet(
        "background:#0d1117;border-bottom:1px solid #1e2736;");
    auto* tbox = new QHBoxLayout(toolbar);
    tbox->setContentsMargins(8, 0, 8, 0);
    tbox->setSpacing(8);

    saveBtn = new QPushButton(tr("💾 Save"));
    saveBtn->setToolTip(tr("Save probe as <session>.ndm.default.probe"));
    saveAsBtn = new QPushButton(tr("Save As…"));
    saveAsBtn->setToolTip(tr("Save probe under a custom name"));

    const QString tealBtn =
        "QPushButton{background:#102a18;border:1px solid #2dd4bf;color:#2dd4bf;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#163d20;}";
    saveBtn->setStyleSheet(tealBtn);
    saveAsBtn->setStyleSheet(tealBtn);

    const QString neutralBtn =
        "QPushButton{background:#0d1117;border:1px solid #2a3650;color:#8fa8c8;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#141c2a;color:#bcd0e8;}";

    addShankBtn   = new QPushButton(tr("+ Shank"));
    addChannelBtn = new QPushButton(tr("+ Channel"));
    addArrayBtn   = new QPushButton(tr("+ Array…"));
    deleteBtn     = new QPushButton(tr("Delete"));
    addShankBtn->setStyleSheet(neutralBtn);
    addChannelBtn->setStyleSheet(neutralBtn);
    addArrayBtn->setStyleSheet(neutralBtn);
    addArrayBtn->setToolTip(tr(
        "Add a linear array of channels to the selected shank.  Asks "
        "for site count, pitch, and lateral offset."));
    deleteBtn->setStyleSheet(
        "QPushButton{background:#180404;border:1px solid #f87171;color:#f87171;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#2d1010;}");

    connect(saveBtn,       &QPushButton::clicked,
            this, &ProbeMakerPage::savePipelineRequested);
    connect(saveAsBtn,     &QPushButton::clicked,
            this, &ProbeMakerPage::saveAsPipelineRequested);
    connect(addShankBtn,   &QPushButton::clicked,
            this, &ProbeMakerPage::onAddShankClicked);
    connect(addChannelBtn, &QPushButton::clicked,
            this, &ProbeMakerPage::onAddChannelClicked);
    connect(addArrayBtn,   &QPushButton::clicked,
            this, &ProbeMakerPage::onAddArrayClicked);
    connect(deleteBtn,     &QPushButton::clicked,
            this, &ProbeMakerPage::onDeleteSelectedClicked);

    // ── Undo / Redo ────────────────────────────────────────────────────
    // Buttons in the toolbar plus widget-scoped keyboard shortcuts so
    // they don't compete with any global Ctrl+Z/Ctrl+Y bindings in
    // ndmanager's other pages.  The shortcut context is
    // WidgetWithChildrenShortcut: active only while focus is inside
    // the Probe Maker page (i.e. when the user is actually editing a
    // probe, not flipping through other tabs).
    undoBtn = new QPushButton(QStringLiteral("↶"));
    redoBtn = new QPushButton(QStringLiteral("↷"));
    undoBtn->setStyleSheet(neutralBtn);
    redoBtn->setStyleSheet(neutralBtn);
    undoBtn->setMaximumWidth(36);
    redoBtn->setMaximumWidth(36);
    undoBtn->setToolTip(tr("Undo (Ctrl+Z)"));
    redoBtn->setToolTip(tr("Redo (Ctrl+Shift+Z, Ctrl+Y)"));
    connect(undoBtn, &QPushButton::clicked,
            this, &ProbeMakerPage::onUndoClicked);
    connect(redoBtn, &QPushButton::clicked,
            this, &ProbeMakerPage::onRedoClicked);

    auto mkShortcut = [this](QKeySequence seq, void (ProbeMakerPage::*slot)()) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    mkShortcut(QKeySequence(QKeySequence::Undo), &ProbeMakerPage::onUndoClicked);
    mkShortcut(QKeySequence(QKeySequence::Redo), &ProbeMakerPage::onRedoClicked);
    // QKeySequence::Redo is platform-dependent (Ctrl+Y on Windows/Linux,
    // Cmd+Shift+Z on macOS).  Add the alternate explicitly so both
    // bindings work everywhere — some users expect Ctrl+Shift+Z, others
    // expect Ctrl+Y, and the canonical sequence covers only one.
    mkShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
               &ProbeMakerPage::onRedoClicked);
    mkShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y),
               &ProbeMakerPage::onRedoClicked);

    auto* fitBtn = new QPushButton(tr("Fit"));
    fitBtn->setStyleSheet(neutralBtn);
    fitBtn->setToolTip(tr(
        "Reset zoom and pan to fit the entire probe in view.  "
        "Auto-fit also runs on every fresh load until you start "
        "panning or zooming."));
    connect(fitBtn, &QPushButton::clicked, this, [this]{
        // Explicit re-fit: clear the user-touched flag so subsequent
        // rebuilds will once again auto-fit.  The user's intent here
        // is "go back to the default view".
        userZoomedOrPanned = false;
        physicalView->fitAll();
    });

    tbox->addWidget(saveBtn);
    tbox->addWidget(saveAsBtn);
    tbox->addSpacing(16);
    tbox->addWidget(undoBtn);
    tbox->addWidget(redoBtn);
    tbox->addSpacing(16);
    tbox->addWidget(addShankBtn);
    tbox->addWidget(addChannelBtn);
    tbox->addWidget(addArrayBtn);
    tbox->addWidget(deleteBtn);
    tbox->addSpacing(16);
    tbox->addWidget(fitBtn);
    tbox->addStretch();

    auto* helpLabel = new QLabel(
        tr("Wheel to zoom · middle-drag to pan · drag pads to reposition"));
    helpLabel->setStyleSheet("color:#2a3a50;font-size:10px;");
    tbox->addWidget(helpLabel);

    // Stacks start empty; both buttons are disabled until the user
    // does something undoable.
    updateUndoRedoButtons();

    outerVbox->addWidget(toolbar);

    // ── Layout: logical view on left, physical view + inspector
    //    stacked vertically on the right.  Putting the inspector under
    //    the physical view (instead of beside it) gives the canvas
    //    much more horizontal room to lay out wide multi-shank probes,
    //    and matches the user's expected reading order:
    //      1. SEE the geometry (large, top-right)
    //      2. EDIT the selected item's fields (compact, bottom-right)
    //
    //    The outer horizontal split is logical | rightSide.  The inner
    //    vertical split on the right is physical-view | inspector.
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(2);
    splitter->setStyleSheet("QSplitter::handle{background:#1e2736;}");

    buildLogicalPane(splitter);

    auto* rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->setHandleWidth(2);
    rightSplit->setStyleSheet("QSplitter::handle{background:#1e2736;}");
    rightSplit->setChildrenCollapsible(false);

    buildPhysicalPane(rightSplit);
    buildInspector(rightSplit);

    // Right-split sizing: physical view dominates (looking at the
    // probe is the main task); inspector takes just enough for its
    // five form rows + a stretch.
    rightSplit->setStretchFactor(0, 4);
    rightSplit->setStretchFactor(1, 1);
    rightSplit->setSizes({600, 220});

    splitter->addWidget(rightSplit);

    // Outer split: logical narrower than the right column.
    splitter->setStretchFactor(0, 2);    // logical
    splitter->setStretchFactor(1, 5);    // right column (physical + inspector)

    outerVbox->addWidget(splitter, /*stretch=*/1);

    // Start with an empty connector — no shanks, ready for the user to
    // drag in or click Add Shank.
    clearToConnector();
}

ProbeMakerPage::~ProbeMakerPage() = default;

// ═══════════════════════════════════════════════════════════════════════════
// Layout — pane builders
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::buildLogicalPane(QSplitter* inner)
{
    auto* wrap = new QWidget;
    wrap->setStyleSheet("background:#0a0f18;");
    auto* v = new QVBoxLayout(wrap);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* hdr = new QLabel(tr("  LOGICAL GRAPH"));
    hdr->setStyleSheet(
        "background:#0d1117;color:#4a5568;font-size:10px;font-weight:bold;"
        "letter-spacing:2px;padding:8px 0 6px;border-bottom:1px solid #1e2736;");
    v->addWidget(hdr);

    logicalView = new ProbeLogicalView;
    auto* logicalScene = new QGraphicsScene(logicalView);
    logicalView->setScene(logicalScene);
    connect(logicalScene, &QGraphicsScene::selectionChanged,
            this, &ProbeMakerPage::onLogicalSelectionChanged);
    // Drag-tracking event filter: mouse press/release on the
    // viewport reaches eventFilter() before being routed into items
    // for drag handling, giving us a clean before/after hook to
    // snapshot pre-drag state and verify a drag actually happened on
    // release.  See eventFilter() for the protocol.
    logicalView->viewport()->installEventFilter(this);
    v->addWidget(logicalView, /*stretch=*/1);

    inner->addWidget(wrap);
}

void ProbeMakerPage::buildPhysicalPane(QSplitter* inner)
{
    auto* wrap = new QWidget;
    wrap->setStyleSheet("background:#12141c;");
    auto* v = new QVBoxLayout(wrap);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* hdr = new QLabel(tr("  PHYSICAL LAYOUT (µm)"));
    hdr->setStyleSheet(
        "background:#0d1117;color:#4a5568;font-size:10px;font-weight:bold;"
        "letter-spacing:2px;padding:8px 0 6px;border-bottom:1px solid #1e2736;");
    v->addWidget(hdr);

    physicalView = new ProbePhysicalView;
    auto* physicalScene = new QGraphicsScene(physicalView);
    physicalView->setScene(physicalScene);
    connect(physicalScene, &QGraphicsScene::selectionChanged,
            this, &ProbeMakerPage::onPhysicalSelectionChanged);

    // Live inspector polling: QGraphicsScene::changed fires whenever
    // any item's geometry changes (e.g. while a channel pad is being
    // dragged).  We refresh the inspector spinboxes on every fire so
    // the X/Y values track the drag in real time.  refreshInspector
    // is cheap (≤10 spinboxes) and idempotent.
    //
    // Skip the refresh while the user is actively typing in an
    // inspector field — otherwise QSignalBlocker-wrapped setValue()
    // calls would clobber whatever they're entering, since the scene
    // also fires `changed` on selection updates and paints.
    connect(physicalScene, &QGraphicsScene::changed,
            this, [this](const QList<QRectF>&){
                if (inspWidget && inspWidget->isAncestorOf(QApplication::focusWidget()))
                    return;
                refreshInspector();
            });

    // Stop auto-fitting on every rebuild once the user has manually
    // zoomed or panned.  fitAll() (button + initial load) clears the
    // flag again so users can opt back into auto-fit at any time.
    connect(physicalView, &ProbePhysicalView::userInteracted,
            this, [this]{ userZoomedOrPanned = true; });

    // Drag-tracking event filter — see buildLogicalPane for protocol.
    physicalView->viewport()->installEventFilter(this);

    v->addWidget(physicalView, /*stretch=*/1);

    inner->addWidget(wrap);
}

void ProbeMakerPage::buildInspector(QSplitter* inner)
{
    inspScroll = new QScrollArea;
    inspScroll->setWidgetResizable(true);
    inspScroll->setStyleSheet("QScrollArea{background:#0d1117;border:none;}");

    inspWidget = new QWidget;
    inspWidget->setStyleSheet("background:#0d1117;");
    auto* iv = new QVBoxLayout(inspWidget);
    iv->setContentsMargins(12, 12, 12, 12);
    iv->setSpacing(12);

    auto* title = new QLabel(tr("INSPECTOR"));
    title->setStyleSheet(
        "color:#4a5568;font-size:10px;font-weight:bold;letter-spacing:2px;"
        "padding-bottom:4px;border-bottom:1px solid #1e2736;");
    iv->addWidget(title);

    const QString fldStyle =
        "QLineEdit, QSpinBox, QDoubleSpinBox{"
        "background:#0a0f18;border:1px solid #1e2736;color:#8fa8c8;"
        "padding:3px 6px;font-size:11px;font-family:monospace;"
        "border-radius:3px;}"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus{"
        "border-color:#4a9eff;}";
    const QString lblStyle = "color:#4a6080;font-size:9px;letter-spacing:1px;";

    // Connector form
    connGroup = new QWidget;
    auto* cf = new QFormLayout(connGroup);
    cf->setContentsMargins(0, 0, 0, 0);
    cf->setSpacing(6);
    auto mkLbl = [&](const QString& s) {
        auto* l = new QLabel(s.toUpper());
        l->setStyleSheet(lblStyle);
        return l;
    };
    connVendor    = new QLineEdit;
    connModel     = new QLineEdit;
    connTotalChan = new QSpinBox;
    connTotalChan->setRange(0, 100000);
    connNotes     = new QLineEdit;
    for (auto* w : { static_cast<QWidget*>(connVendor),
                     static_cast<QWidget*>(connModel),
                     static_cast<QWidget*>(connTotalChan),
                     static_cast<QWidget*>(connNotes) }) {
        w->setStyleSheet(fldStyle);
    }
    cf->addRow(mkLbl(tr("Vendor")),     connVendor);
    cf->addRow(mkLbl(tr("Model")),      connModel);
    cf->addRow(mkLbl(tr("Channels")),   connTotalChan);
    cf->addRow(mkLbl(tr("Notes")),      connNotes);
    for (QLineEdit* le : { connVendor, connModel, connNotes }) {
        connect(le, &QLineEdit::editingFinished,
                this, &ProbeMakerPage::onConnectorFieldEdited);
    }
    connect(connTotalChan, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProbeMakerPage::onConnectorFieldEdited);

    // Shank form
    shankGroup = new QWidget;
    auto* sf = new QFormLayout(shankGroup);
    sf->setContentsMargins(0, 0, 0, 0);
    sf->setSpacing(6);
    shankLabel    = new QLineEdit;
    shankLength   = new QDoubleSpinBox;  shankLength->setRange(1, 50000);   shankLength->setSuffix(" µm");
    shankWidth    = new QDoubleSpinBox;  shankWidth->setRange(1, 5000);     shankWidth->setSuffix(" µm");
    shankTipAngle = new QDoubleSpinBox;  shankTipAngle->setRange(30, 180);  shankTipAngle->setSuffix("°");
    shankOriginX  = new QDoubleSpinBox;  shankOriginX->setRange(-50000, 50000); shankOriginX->setSuffix(" µm");
    shankOriginY  = new QDoubleSpinBox;  shankOriginY->setRange(-50000, 50000); shankOriginY->setSuffix(" µm");
    shankOriginX->setToolTip(tr("Lateral position of this shank's centreline "
                                   "in the global probe frame (e.g. shank N at "
                                   "N × spacing_um for evenly-spaced multi-"
                                   "shank arrays)."));
    shankOriginY->setToolTip(tr("Vertical offset of this shank's head in the "
                                   "global probe frame.  Usually 0; non-zero only "
                                   "for staggered designs."));
    shankLayout   = new QLineEdit;
    for (auto* w : { static_cast<QWidget*>(shankLabel),
                     static_cast<QWidget*>(shankLength),
                     static_cast<QWidget*>(shankWidth),
                     static_cast<QWidget*>(shankTipAngle),
                     static_cast<QWidget*>(shankOriginX),
                     static_cast<QWidget*>(shankOriginY),
                     static_cast<QWidget*>(shankLayout) }) {
        w->setStyleSheet(fldStyle);
    }
    sf->addRow(mkLbl(tr("Label")),      shankLabel);
    sf->addRow(mkLbl(tr("Length")),     shankLength);
    sf->addRow(mkLbl(tr("Width")),      shankWidth);
    sf->addRow(mkLbl(tr("Tip angle")),  shankTipAngle);
    sf->addRow(mkLbl(tr("Origin x")),   shankOriginX);
    sf->addRow(mkLbl(tr("Origin y")),   shankOriginY);
    sf->addRow(mkLbl(tr("Layout")),     shankLayout);
    connect(shankLabel,    &QLineEdit::editingFinished,
            this, &ProbeMakerPage::onShankFieldEdited);
    connect(shankLayout,   &QLineEdit::editingFinished,
            this, &ProbeMakerPage::onShankFieldEdited);
    for (QDoubleSpinBox* sb : { shankLength, shankWidth, shankTipAngle,
                                 shankOriginX, shankOriginY }) {
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &ProbeMakerPage::onShankFieldEdited);
    }

    // Channel form
    chanGroup = new QWidget;
    auto* chf = new QFormLayout(chanGroup);
    chf->setContentsMargins(0, 0, 0, 0);
    chf->setSpacing(6);
    chanHwId    = new QSpinBox;       chanHwId->setRange(0, 99999);
    chanX       = new QDoubleSpinBox; chanX->setRange(-50000, 50000); chanX->setSuffix(" µm");
    chanY       = new QDoubleSpinBox; chanY->setRange(-50000, 50000); chanY->setSuffix(" µm");
    chanSiteIdx = new QSpinBox;       chanSiteIdx->setRange(-1, 99999);
    chanArea    = new QDoubleSpinBox; chanArea->setRange(0, 100000);  chanArea->setSuffix(" µm²");
    for (auto* w : { static_cast<QWidget*>(chanHwId),
                     static_cast<QWidget*>(chanX),
                     static_cast<QWidget*>(chanY),
                     static_cast<QWidget*>(chanSiteIdx),
                     static_cast<QWidget*>(chanArea) }) {
        w->setStyleSheet(fldStyle);
    }
    chf->addRow(mkLbl(tr("Hw channel")),  chanHwId);
    chf->addRow(mkLbl(tr("Pos x")),       chanX);
    chf->addRow(mkLbl(tr("Pos y")),       chanY);
    chf->addRow(mkLbl(tr("Site index")),  chanSiteIdx);
    chf->addRow(mkLbl(tr("Area")),        chanArea);
    for (QSpinBox* sb : { chanHwId, chanSiteIdx }) {
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &ProbeMakerPage::onChannelFieldEdited);
    }
    for (QDoubleSpinBox* sb : { chanX, chanY, chanArea }) {
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &ProbeMakerPage::onChannelFieldEdited);
    }

    iv->addWidget(connGroup);
    iv->addWidget(shankGroup);
    iv->addWidget(chanGroup);
    iv->addStretch();

    inspScroll->setWidget(inspWidget);
    inner->addWidget(inspScroll);

    refreshInspector();   // hide all groups initially (no selection)
}

// ═══════════════════════════════════════════════════════════════════════════
// Scene rebuild
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::rebuildLogicalScene()
{
    if (!logicalView) return;
    QGraphicsScene* s = logicalView->scene();

    // ── Capture pre-rebuild positions and prune dead keys ──────────────
    //
    // Each LogicalNodeItem stores its stable composite key in
    // RoleLogicalKey at scene-build time.  Reading it back here gives
    // us a per-item identifier that survives both the upcoming
    // s->clear() and any QList reallocation that invalidated the raw
    // model pointers.  Building the captured hash from the live
    // scene (rather than augmenting logicalState in place) also
    // implicitly prunes any keys whose model items no longer exist:
    // they're not in the scene, so they're not in `captured`.
    QHash<QString, QPointF> captured;
    for (QGraphicsItem* it : s->items()) {
        if (it->type() != ItemTypeLogicalNode) continue;
        const QString key = it->data(RoleLogicalKey).toString();
        if (!key.isEmpty())
            captured.insert(key, it->pos());
    }
    logicalState = std::move(captured);

    s->clear();   // deletes all items, including children and edges

    // ── Layout constants ───────────────────────────────────────────────
    constexpr qreal connW    = 240.0;
    constexpr qreal connH    = 56.0;
    constexpr qreal yConn    = 0.0;
    constexpr qreal yShank   = yConn + connH + 80.0;
    constexpr qreal shankW   = 200.0;
    constexpr qreal shankH   = 56.0;
    constexpr qreal shankGap = 60.0;
    constexpr qreal chanW    = 40.0;
    constexpr qreal chanH    = 26.0;
    constexpr qreal chanGap  = 6.0;
    constexpr int   chansPerRow = 4;
    constexpr qreal chanRowGap  = 4.0;
    const     qreal chanFirstY  = yShank + shankH + 24.0;

    const int   nShanks         = data.shanks.size();
    const qreal totalShankRowW  = nShanks > 0
        ? nShanks * shankW + (nShanks - 1) * shankGap
        : shankW;
    const qreal sceneW = qMax(connW, totalShankRowW);

    // ── Connector node ─────────────────────────────────────────────────
    // Auto-layout position = horizontally centred over the shank row.
    // Persisted user position (if any) wins.
    auto* connNode = new LogicalNodeItem(
        LogicalNodeItem::Kind::Connector,
        static_cast<void*>(&data),
        data.model.isEmpty() ? tr("★ ROOT") : data.model,
        QSizeF(connW, connH));
    connNode->setData(RoleObjectId,   QStringLiteral("connector"));
    connNode->setData(RoleLogicalKey, keyForConnector());
    {
        const QString k = keyForConnector();
        connNode->setPos(logicalState.contains(k)
            ? logicalState.value(k)
            : QPointF((sceneW - connW) * 0.5, yConn));
    }
    s->addItem(connNode);

    // ── Shank nodes (one per ProbeShank) ───────────────────────────────
    qreal autoShankX = (sceneW - totalShankRowW) * 0.5;
    QList<LogicalNodeItem*> shankNodes;
    shankNodes.reserve(nShanks);
    for (int si = 0; si < nShanks; ++si) {
        ProbeShank& sh = data.shanks[si];
        auto* shankNode = new LogicalNodeItem(
            LogicalNodeItem::Kind::Shank,
            static_cast<void*>(&sh),
            sh.label.isEmpty() ? tr("Shank %1").arg(si + 1) : sh.label,
            QSizeF(shankW, shankH));
        shankNode->setData(RoleObjectId,   sh.id);
        shankNode->setData(RoleLogicalKey, keyForShank(sh));
        const QString shankKey = keyForShank(sh);
        shankNode->setPos(logicalState.contains(shankKey)
            ? logicalState.value(shankKey)
            : QPointF(autoShankX, yShank));
        s->addItem(shankNode);
        shankNodes.append(shankNode);
        autoShankX += shankW + shankGap;

        // Connector → shank edge.  LogicalEdgeItem subscribes to both
        // endpoints in its ctor; subsequent moves of either node call
        // edge->recalcGeometry() automatically.
        auto* edge = new LogicalEdgeItem(connNode, shankNode);
        s->addItem(edge);
    }

    // ── Channel nodes (one per ProbeChannel) ───────────────────────────
    for (int si = 0; si < nShanks; ++si) {
        ProbeShank& sh = data.shanks[si];
        const int nch = sh.channels.size();
        // Auto-layout origin: directly under the shank's auto position
        // (we don't read the shank's *current* pos here because user-
        // positioned shanks shouldn't drag their channels into wherever
        // they were dropped — channels independently auto-lay-out the
        // first time, then persist their user positions just like
        // shanks do).
        const qreal autoOrigin = (sceneW - totalShankRowW) * 0.5
                               + si * (shankW + shankGap);

        for (int ci = 0; ci < nch; ++ci) {
            ProbeChannel& ch = sh.channels[ci];
            const int row = ci / chansPerRow;
            const int col = ci % chansPerRow;
            const int colsThisRow = qMin(chansPerRow, nch - row * chansPerRow);
            const qreal rowW = colsThisRow * chanW
                             + (colsThisRow - 1) * chanGap;
            const qreal rowX0 = autoOrigin + (shankW - rowW) * 0.5;
            const qreal autoX = rowX0 + col * (chanW + chanGap);
            const qreal autoY = chanFirstY + row * (chanH + chanRowGap);

            auto* chanNode = new LogicalNodeItem(
                LogicalNodeItem::Kind::Channel,
                static_cast<void*>(&ch),
                QStringLiteral("ch %1").arg(ch.hardwareId),
                QSizeF(chanW, chanH));
            chanNode->setData(RoleObjectId,   QString::number(ch.hardwareId));
            chanNode->setData(RoleLogicalKey, keyForChannel(sh, ch));
            const QString chanKey = keyForChannel(sh, ch);
            chanNode->setPos(logicalState.contains(chanKey)
                ? logicalState.value(chanKey)
                : QPointF(autoX, autoY));
            s->addItem(chanNode);

            // Shank → channel edge.
            auto* edge = new LogicalEdgeItem(shankNodes[si], chanNode);
            s->addItem(edge);
        }
    }

    s->setSceneRect(s->itemsBoundingRect().marginsAdded(QMarginsF(40, 40, 40, 40)));
}

void ProbeMakerPage::rebuildPhysicalScene()
{
    if (!physicalView) return;
    QGraphicsScene* s = physicalView->scene();
    s->clear();

    // Connector ribbon — horizontal bar above the shanks at scene-Y
    // negative-something so it doesn't overlap the shank polygons.
    constexpr qreal connBarH = 40.0;
    auto* connItem = new ConnectorItem(&data, 280.0);
    connItem->setPos(0.0, -connBarH - 80.0);
    s->addItem(connItem);

    // Shanks at their stored originUm.  When originUm is (0, 0) for
    // every shank (as on a fresh probe), spread them along the X axis
    // 200 µm apart so they're individually selectable.  Once the user
    // drags one or sets an explicit origin, that auto-spread stops.
    bool needsAutoSpread = true;
    for (const ProbeShank& sh : data.shanks) {
        if (sh.originUm != QPointF(0, 0)) {
            needsAutoSpread = false;
            break;
        }
    }
    qreal autoX = 0.0;
    for (int si = 0; si < data.shanks.size(); ++si) {
        ProbeShank& sh = data.shanks[si];
        auto* shankItem = new ShankItem(&sh);
        const QPointF origin = needsAutoSpread
            ? QPointF(autoX, 0.0)
            : sh.originUm;
        if (needsAutoSpread) {
            sh.originUm = origin;
            autoX += sh.widthUm + 200.0;
        }
        shankItem->setPos(origin);
        s->addItem(shankItem);

        // Channels parented to the shank item — pos in shank-local
        // coords means they move with the shank automatically.
        for (ProbeChannel& ch : sh.channels) {
            auto* chItem = new ChannelItem(&ch, shankItem);
            chItem->refreshFromModel();
        }
    }

    s->setSceneRect(s->itemsBoundingRect().marginsAdded(QMarginsF(40, 40, 40, 40)));
    // Auto-fit only on the first rebuild after a load (when no user
    // transform has been applied yet); subsequent rebuilds (from
    // adding a channel, editing a field, etc.) preserve the user's
    // current zoom/pan so they don't have to re-orient after every
    // edit.  Manual fit is available via the "Fit" toolbar button.
    if (!userZoomedOrPanned)
        physicalView->fitAll();
}

// ═══════════════════════════════════════════════════════════════════════════
// Selection mirroring
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::onLogicalSelectionChanged()
{
    mirrorSelection(logicalView->scene(), physicalView->scene());
    refreshInspector();
}

void ProbeMakerPage::onPhysicalSelectionChanged()
{
    mirrorSelection(physicalView->scene(), logicalView->scene());
    refreshInspector();
}

void ProbeMakerPage::mirrorSelection(const QGraphicsScene* sourceScene,
                                     QGraphicsScene*       targetScene)
{
    if (!sourceScene || !targetScene) return;

    // Resolve the selected (kind, id) pair on the source side.  We
    // prefer items with a non-empty RoleItemKind (connector / shank /
    // channel); edges and other auxiliary graphics never have a kind
    // set, so they get skipped naturally.  If nothing kindful is
    // selected, fall through to clearing the target scene's selection.
    QString kind, id;
    void*   modelPtr = nullptr;
    for (QGraphicsItem* it : sourceScene->selectedItems()) {
        const QString k = it->data(RoleItemKind).toString();
        if (k.isEmpty()) continue;
        kind     = k;
        id       = it->data(RoleObjectId).toString();
        modelPtr = it->data(RoleModelPtr).value<void*>();
        break;
    }

    // Cache the page-level selection for inspector refresh.
    connectorSelected = false;
    selectedShank     = nullptr;
    selectedChannel   = nullptr;
    if (kind == QLatin1String("connector")) {
        connectorSelected = true;
    } else if (kind == QLatin1String("shank")) {
        selectedShank   = static_cast<ProbeShank*>(modelPtr);
    } else if (kind == QLatin1String("channel")) {
        selectedChannel = static_cast<ProbeChannel*>(modelPtr);
    }

    // Apply the same selection in the target scene.  Block signals
    // while we do so to avoid bouncing back and triggering this slot
    // again.
    //
    // Connector matching is by kind alone: there's only ever one
    // connector per scene, and the older ConnectorItem class doesn't
    // set RoleObjectId, so an id-based match would never succeed.
    // Shank and channel matching uses both kind and RoleObjectId.
    QSignalBlocker blocker(targetScene);
    for (QGraphicsItem* it : targetScene->items()) {
        const QString itKind = it->data(RoleItemKind).toString();
        bool match;
        if (kind == QLatin1String("connector")) {
            match = (itKind == QLatin1String("connector"));
        } else if (kind.isEmpty()) {
            match = false;   // nothing selected on source — clear target
        } else {
            match = (itKind == kind &&
                     it->data(RoleObjectId).toString() == id);
        }
        it->setSelected(match);
    }
    targetScene->update();
}

// ═══════════════════════════════════════════════════════════════════════════
// Toolbar actions
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::onAddShankClicked()
{
    pushUndoSnapshot(tr("Add shank"));

    ProbeShank sh;
    // Generate a fresh, unique id even if intermediate shanks were
    // deleted.  Using `size() + 1` would collide: add A, B → [shank1,
    // shank2]; delete A → [shank2]; add → "shank2" (duplicate of the
    // surviving B).  Walk the existing ids and pick the next free
    // suffix, which guarantees uniqueness within the session.
    int suffix = data.shanks.size() + 1;
    QString candidate = QStringLiteral("shank%1").arg(suffix);
    auto idTaken = [this](const QString& id) {
        for (const ProbeShank& s : data.shanks)
            if (s.id == id) return true;
        return false;
    };
    while (idTaken(candidate))
        candidate = QStringLiteral("shank%1").arg(++suffix);
    sh.id    = candidate;
    sh.label = QStringLiteral("Shank %1")
                   .arg(QChar(static_cast<char>('A' + static_cast<int>(data.shanks.size()))));
    sh.lengthUm = 1500.0;
    sh.widthUm  = 70.0;
    sh.tipAngle = 90.0;
    sh.layout   = QStringLiteral("linear");
    // Place the new shank to the right of any existing ones, 200 µm
    // apart on its own centreline, so it shows up off-screen of any
    // existing shanks rather than overlapping them.
    qreal originX = 0.0;
    if (!data.shanks.isEmpty())
        originX = data.shanks.last().originUm.x() + 200.0;
    sh.originUm = QPointF(originX, 0.0);
    data.shanks.append(sh);

    // Select the new shank so subsequent + Channel / Delete / inspector
    // actions target it without an extra click.
    selectedShank   = &data.shanks.last();
    selectedChannel = nullptr;

    rebuildLogicalScene();
    rebuildPhysicalScene();
    refreshInspector();
    setModified(true);
}

void ProbeMakerPage::onAddChannelClicked()
{
    pushUndoSnapshot(tr("Add channel"));

    // Without an explicit selection, target the last (most recently
    // added) shank so the user can append channels with a single click.
    // Silent no-op was the previous behaviour and was too easy to mistake
    // for a broken button when the user had just clicked + Channel right
    // after Add Probe — the new probe's lone shank wasn't yet selected.
    ProbeShank* targetShank = selectedShank;
    if (!targetShank && !data.shanks.isEmpty())
        targetShank = &data.shanks.last();
    if (!targetShank) {
        // No shanks at all — nothing to attach a channel to.  Add a
        // shank first; the user gets visible feedback (the new shank
        // appears) and can click + Channel again.  Mark the nested
        // shank-add so its own pushUndoSnapshot is suppressed: the
        // user's "Add channel" action becomes one atomic undo step,
        // not two.
        inNestedMutation = true;
        onAddShankClicked();
        inNestedMutation = false;
        targetShank = &data.shanks.last();
    }

    ProbeChannel ch;
    // Hardware id: highest existing + 1, across all shanks.
    int maxHw = -1;
    for (const ProbeShank& s : data.shanks)
        for (const ProbeChannel& c : s.channels)
            maxHw = qMax(maxHw, c.hardwareId);
    ch.hardwareId = maxHw + 1;
    ch.areaUm2    = 177.0;
    ch.siteIndex  = targetShank->channels.size();

    // Default position: centreline (x = 0 in shank-local coords) near
    // the tip, stacking upward as more channels are added.  Most real
    // probes have sites concentrated near the tip; this matches user
    // expectations and keeps the new site on-shank regardless of width.
    constexpr qreal pitch    = 50.0;     // µm between successive defaults
    constexpr qreal tipMargin = 25.0;    // µm above the tip wedge
    const qreal idx = static_cast<qreal>(targetShank->channels.size());
    const qreal y   = targetShank->lengthUm - (tipMargin + pitch * idx);
    ch.posUm = QPointF(0.0, qMax<qreal>(tipMargin, y));   // clamp away from head

    targetShank->channels.append(ch);
    int totalSites = 0;
    for (const ProbeShank& s : data.shanks) totalSites += s.channels.size();
    data.totalChannels = qMax(data.totalChannels, totalSites);

    // Update selection so the inspector immediately shows the new
    // channel and Delete acts on it.  Using the model pointer directly
    // is safe here because the rebuilds below recreate all graphics
    // items from scratch using fresh pointers from the same QVector.
    selectedShank   = nullptr;
    selectedChannel = &targetShank->channels.last();

    rebuildLogicalScene();
    rebuildPhysicalScene();
    refreshInspector();
    setModified(true);
}

void ProbeMakerPage::onAddArrayClicked()
{
    // Three quick prompts BEFORE any model mutation, so a user cancel
    // doesn't leave behind a phantom shank or a no-op undo entry.
    // QInputDialog is intentional — it keeps the tarball small (no
    // extra .ui file) and the parameters are independent enough that
    // a single multi-field dialog wouldn't add much.
    bool ok = false;
    const int count = QInputDialog::getInt(
        this, tr("Add Linear Array"),
        tr("Number of sites:"),
        /*default*/ 16, /*min*/ 1, /*max*/ 1024, /*step*/ 1, &ok);
    if (!ok) return;

    const double pitchUm = QInputDialog::getDouble(
        this, tr("Add Linear Array"),
        tr("Pitch between sites (µm):"),
        /*default*/ 50.0, /*min*/ 1.0, /*max*/ 10000.0, /*decimals*/ 2, &ok);
    if (!ok) return;

    const double xOffsetUm = QInputDialog::getDouble(
        this, tr("Add Linear Array"),
        tr("Lateral offset from centreline (µm):  (negative = left, positive = right)"),
        /*default*/ 0.0, /*min*/ -5000.0, /*max*/ 5000.0, /*decimals*/ 2, &ok);
    if (!ok) return;

    // All inputs collected → snapshot once for the entire op.
    pushUndoSnapshot(tr("Add channel array (%1 sites)").arg(count));

    // Same shank-targeting logic as + Channel: prefer the explicitly
    // selected shank, fall back to the most recently added one, create
    // a new shank if there are none yet.  Nested shank-add suppressed
    // so the array op is a single atomic undo step.
    ProbeShank* targetShank = selectedShank;
    if (!targetShank && !data.shanks.isEmpty())
        targetShank = &data.shanks.last();
    if (!targetShank) {
        inNestedMutation = true;
        onAddShankClicked();
        inNestedMutation = false;
        targetShank = &data.shanks.last();
    }

    // Highest hardware id so far, for sequential numbering.
    int maxHw = -1;
    for (const ProbeShank& s : data.shanks)
        for (const ProbeChannel& c : s.channels)
            maxHw = qMax(maxHw, c.hardwareId);

    // Lay out tip-up: the first new site goes 25 µm above the tip
    // wedge, subsequent sites march toward the head with the requested
    // pitch.  This matches the convention in the canonical probe
    // library (sites concentrate near the tip).
    constexpr qreal tipMargin = 25.0;
    const qreal lengthUm = targetShank->lengthUm;
    int siteIdxBase = targetShank->channels.size();
    for (int i = 0; i < count; ++i) {
        ProbeChannel ch;
        ch.hardwareId = ++maxHw;
        ch.areaUm2    = 177.0;
        ch.siteIndex  = siteIdxBase + i;
        const qreal y = lengthUm - tipMargin - i * pitchUm;
        ch.posUm      = QPointF(xOffsetUm, qMax<qreal>(tipMargin, y));
        targetShank->channels.append(ch);
    }

    int totalSites = 0;
    for (const ProbeShank& s : data.shanks) totalSites += s.channels.size();
    data.totalChannels = qMax(data.totalChannels, totalSites);

    // Select the last channel of the new array so the inspector shows
    // a representative member; user can navigate from there.
    selectedShank   = nullptr;
    selectedChannel = &targetShank->channels.last();

    rebuildLogicalScene();
    rebuildPhysicalScene();
    refreshInspector();
    setModified(true);
}

void ProbeMakerPage::onDeleteSelectedClicked()
{
    if (selectedChannel) {
        // Find which shank owns it and remove via pointer match.
        for (ProbeShank& s : data.shanks) {
            for (int i = s.channels.size() - 1; i >= 0; --i) {
                if (&s.channels[i] == selectedChannel) {
                    pushUndoSnapshot(tr("Delete channel"));
                    s.channels.removeAt(i);
                    selectedChannel = nullptr;
                    rebuildLogicalScene();
                    rebuildPhysicalScene();
                    setModified(true);
                    return;
                }
            }
        }
    } else if (selectedShank) {
        for (int i = data.shanks.size() - 1; i >= 0; --i) {
            if (&data.shanks[i] == selectedShank) {
                pushUndoSnapshot(tr("Delete shank"));
                data.shanks.removeAt(i);
                selectedShank = nullptr;
                rebuildLogicalScene();
                rebuildPhysicalScene();
                setModified(true);
                return;
            }
        }
    }
    // Connector deletion is intentionally not supported; if the user
    // wants to start over, they use clearToConnector() (no UI for that
    // in v1 — it's invoked by Load on success or by the Apply path).
}

// ═══════════════════════════════════════════════════════════════════════════
// Inspector commit handlers
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::refreshInspector()
{
    connGroup->setVisible(connectorSelected);
    shankGroup->setVisible(selectedShank   != nullptr);
    chanGroup->setVisible(selectedChannel != nullptr);

    QSignalBlocker bC(connVendor); QSignalBlocker bM(connModel);
    QSignalBlocker bT(connTotalChan); QSignalBlocker bN(connNotes);
    if (connectorSelected) {
        connVendor->setText(data.vendor);
        connModel->setText(data.model);
        connTotalChan->setValue(data.totalChannels);
        connNotes->setText(data.notes);
    }

    QSignalBlocker bSL(shankLabel);    QSignalBlocker bSLn(shankLength);
    QSignalBlocker bSW(shankWidth);    QSignalBlocker bST(shankTipAngle);
    QSignalBlocker bSY(shankLayout);
    QSignalBlocker bSOX(shankOriginX); QSignalBlocker bSOY(shankOriginY);
    if (selectedShank) {
        shankLabel->setText(selectedShank->label);
        shankLength->setValue(selectedShank->lengthUm);
        shankWidth->setValue(selectedShank->widthUm);
        shankTipAngle->setValue(selectedShank->tipAngle);
        shankOriginX->setValue(selectedShank->originUm.x());
        shankOriginY->setValue(selectedShank->originUm.y());
        shankLayout->setText(selectedShank->layout);
    }

    QSignalBlocker bCH(chanHwId); QSignalBlocker bCX(chanX);
    QSignalBlocker bCY(chanY);    QSignalBlocker bCS(chanSiteIdx);
    QSignalBlocker bCA(chanArea);
    if (selectedChannel) {
        chanHwId->setValue(selectedChannel->hardwareId);
        chanX->setValue(selectedChannel->posUm.x());
        chanY->setValue(selectedChannel->posUm.y());
        chanSiteIdx->setValue(selectedChannel->siteIndex);
        chanArea->setValue(selectedChannel->areaUm2);
    }
}

void ProbeMakerPage::onConnectorFieldEdited()
{
    if (!connectorSelected) return;
    pushUndoSnapshotCoalesced(tr("Edit connector"),
                              QStringLiteral("connector"),
                              static_cast<void*>(&data));
    data.vendor        = connVendor->text();
    data.model         = connModel->text();
    data.totalChannels = connTotalChan->value();
    data.notes         = connNotes->text();
    // Refresh just the connector header — no need to rebuild scenes.
    for (QGraphicsItem* it : logicalView->scene()->items()) {
        if (it->type() == ItemTypeConnector)
            static_cast<ConnectorItem*>(it)->refreshFromModel();
    }
    for (QGraphicsItem* it : physicalView->scene()->items()) {
        if (it->type() == ItemTypeConnector)
            static_cast<ConnectorItem*>(it)->refreshFromModel();
    }
    setModified(true);
}

void ProbeMakerPage::onShankFieldEdited()
{
    if (!selectedShank) return;
    pushUndoSnapshotCoalesced(tr("Edit shank"),
                              QStringLiteral("shank"),
                              static_cast<void*>(selectedShank));
    selectedShank->label    = shankLabel->text();
    selectedShank->lengthUm = shankLength->value();
    selectedShank->widthUm  = shankWidth->value();
    selectedShank->tipAngle = shankTipAngle->value();
    selectedShank->originUm = QPointF(shankOriginX->value(),
                                         shankOriginY->value());
    selectedShank->layout   = shankLayout->text();
    // Geometry change → rebuild physical scene; logical labels need
    // a refresh (channel-count subtitle) too.
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(true);
}

void ProbeMakerPage::onChannelFieldEdited()
{
    if (!selectedChannel) return;
    pushUndoSnapshotCoalesced(tr("Edit channel"),
                              QStringLiteral("channel"),
                              static_cast<void*>(selectedChannel));
    selectedChannel->hardwareId = chanHwId->value();
    selectedChannel->posUm      = QPointF(chanX->value(), chanY->value());
    selectedChannel->siteIndex  = chanSiteIdx->value();
    selectedChannel->areaUm2    = chanArea->value();
    // Channel pad position lives in the physical scene.  Refresh the
    // ChannelItem in place rather than rebuilding everything.
    for (QGraphicsItem* it : physicalView->scene()->items()) {
        if (it->type() == ItemTypeChannel) {
            void* ptr = it->data(RoleModelPtr).value<void*>();
            if (ptr == selectedChannel) {
                static_cast<ChannelItem*>(it)->refreshFromModel();
            }
        }
    }
    rebuildLogicalScene();   // hw-id label might have changed
    setModified(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public data API
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::setConnector(const ProbeConnector& connector)
{
    data = connector;
    // New connector data → re-enable auto-fit on the next rebuild.
    // This is the "load a probe file" path; user expects to see the
    // whole new probe centred and scaled regardless of any prior
    // manual zoom.
    userZoomedOrPanned = false;
    // Discard cached logical-graph positions: model pointers are now
    // different addresses (we reassigned data wholesale) so any
    // stale entries would never match anyway, but explicit clear is
    // tidier and bounds memory growth across many loads.
    logicalState.clear();
    // Loading a different probe is a hard boundary for the undo
    // history — the user can't sensibly undo "across" a load.
    undoStack.clear();
    redoStack.clear();
    resetEditIdentity();
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(false);
    updateUndoRedoButtons();
}

ProbeConnector ProbeMakerPage::connector() const
{
    return data;
}

void ProbeMakerPage::clearToConnector()
{
    data = ProbeConnector{};
    data.version = QStringLiteral("1.0");
    // New content → re-enable auto-fit on the next rebuild so the
    // user sees the fresh probe centred and scaled appropriately.
    userZoomedOrPanned = false;
    // Discard cached logical-graph positions; the new probe gets a
    // fresh auto-layout.
    logicalState.clear();
    // Hard boundary for undo history.
    undoStack.clear();
    redoStack.clear();
    resetEditIdentity();
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(false);
    updateUndoRedoButtons();
}

void ProbeMakerPage::setModified(bool b)
{
    if (m_modified == b) return;
    m_modified = b;
    if (b) emit modified();
}

// ═══════════════════════════════════════════════════════════════════════════
// Undo / redo
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::pushUndoSnapshot(const QString& description)
{
    // Three reasons to skip the push:
    //   1. undoInProgress: a restore is in flight — touching history
    //      during restore would mean the rebuild could blow it up.
    //   2. inNestedMutation: a higher-level mutator already pushed
    //      its own snapshot and is now invoking a sub-mutator (e.g.
    //      Add Array creating a shank); the user expects ONE undo
    //      entry for their conceptual action, not two.
    if (undoInProgress || inNestedMutation) return;

    // The logical-view position cache is only fresh after a rebuild's
    // capture pass — drags update LogicalNodeItem positions live but
    // logicalState catches up only on the next rebuild.  Capture
    // current scene positions into logicalState before snapshotting
    // so the snapshot reflects the user-visible state.
    if (logicalView && logicalView->scene()) {
        for (QGraphicsItem* it : logicalView->scene()->items()) {
            if (it->type() != ItemTypeLogicalNode) continue;
            const QString key = it->data(RoleLogicalKey).toString();
            if (!key.isEmpty()) logicalState[key] = it->pos();
        }
    }

    UndoSnapshot snap;
    snap.data         = data;
    snap.logicalState = logicalState;
    snap.description  = description;
    undoStack.append(std::move(snap));

    // A new edit invalidates any pending redo history — once you
    // diverge from the redo timeline you can't get back without
    // undoing the new edit.
    redoStack.clear();

    // Bound stack growth.  200 deep is more than enough for an
    // editing session (and the data is small) but we don't want it to
    // grow unbounded on long sessions.
    while (undoStack.size() > kMaxUndoDepth)
        undoStack.removeFirst();

    // Any non-coalesced push starts a fresh edit identity — the next
    // spinbox tick won't be merged with whatever came before.
    resetEditIdentity();

    updateUndoRedoButtons();
}

void ProbeMakerPage::pushUndoSnapshotCoalesced(const QString& description,
                                                const QString& editKind,
                                                void*          modelPtr)
{
    if (undoInProgress || inNestedMutation) return;

    // If this edit is on the same field of the same item as the
    // previous edit, the snapshot already on top of the undo stack
    // already represents the pre-edit state.  Skip the push; the
    // user will undo through one logical edit, not N individual
    // spinbox ticks.
    if (lastEditKind == editKind
        && lastEditModelPtr == modelPtr
        && !lastEditKind.isEmpty()
        && !undoStack.isEmpty()) {
        return;
    }

    // Capture logical positions like pushUndoSnapshot does.
    if (logicalView && logicalView->scene()) {
        for (QGraphicsItem* it : logicalView->scene()->items()) {
            if (it->type() != ItemTypeLogicalNode) continue;
            const QString key = it->data(RoleLogicalKey).toString();
            if (!key.isEmpty()) logicalState[key] = it->pos();
        }
    }

    // First tick of a new edit: push normally, then remember the
    // identity so subsequent ticks coalesce.
    UndoSnapshot snap;
    snap.data         = data;
    snap.logicalState = logicalState;
    snap.description  = description;
    undoStack.append(std::move(snap));
    redoStack.clear();
    while (undoStack.size() > kMaxUndoDepth)
        undoStack.removeFirst();

    lastEditKind     = editKind;
    lastEditModelPtr = modelPtr;

    updateUndoRedoButtons();
}

void ProbeMakerPage::onUndoClicked()
{
    if (undoStack.isEmpty()) return;

    // Capture any drag-since-last-snapshot from the live scene into
    // logicalState before snapshotting it onto the redo stack.
    // Without this, an undone drag-then-undo round-trip would lose
    // the post-drag state that the user is undoing from.
    if (logicalView && logicalView->scene()) {
        for (QGraphicsItem* it : logicalView->scene()->items()) {
            if (it->type() != ItemTypeLogicalNode) continue;
            const QString key = it->data(RoleLogicalKey).toString();
            if (!key.isEmpty()) logicalState[key] = it->pos();
        }
    }
    UndoSnapshot redo;
    redo.data         = data;
    redo.logicalState = logicalState;
    redo.description  = undoStack.last().description;
    redoStack.append(std::move(redo));

    // Restore previous state.  undoInProgress guards pushUndoSnapshot
    // against re-entering during the rebuilds below.
    UndoSnapshot prev = undoStack.takeLast();
    undoInProgress = true;
    data         = std::move(prev.data);
    logicalState = std::move(prev.logicalState);

    // Selection pointers are stale after restore (data has new
    // shank/channel addresses post-assignment).  Null them so the
    // inspector hides its sub-forms; user can re-select in either
    // scene to pick up where they were.
    selectedShank   = nullptr;
    selectedChannel = nullptr;
    connectorSelected = false;

    rebuildLogicalScene();
    rebuildPhysicalScene();
    refreshInspector();
    setModified(true);
    undoInProgress = false;

    resetEditIdentity();
    updateUndoRedoButtons();
}

void ProbeMakerPage::onRedoClicked()
{
    if (redoStack.isEmpty()) return;

    // Symmetric to onUndoClicked.
    if (logicalView && logicalView->scene()) {
        for (QGraphicsItem* it : logicalView->scene()->items()) {
            if (it->type() != ItemTypeLogicalNode) continue;
            const QString key = it->data(RoleLogicalKey).toString();
            if (!key.isEmpty()) logicalState[key] = it->pos();
        }
    }
    UndoSnapshot undo;
    undo.data         = data;
    undo.logicalState = logicalState;
    undo.description  = redoStack.last().description;
    undoStack.append(std::move(undo));

    UndoSnapshot next = redoStack.takeLast();
    undoInProgress = true;
    data         = std::move(next.data);
    logicalState = std::move(next.logicalState);

    selectedShank   = nullptr;
    selectedChannel = nullptr;
    connectorSelected = false;

    rebuildLogicalScene();
    rebuildPhysicalScene();
    refreshInspector();
    setModified(true);
    undoInProgress = false;

    resetEditIdentity();
    updateUndoRedoButtons();
}

void ProbeMakerPage::updateUndoRedoButtons()
{
    if (undoBtn) {
        undoBtn->setEnabled(!undoStack.isEmpty());
        undoBtn->setToolTip(undoStack.isEmpty()
            ? tr("Undo (Ctrl+Z)")
            : tr("Undo %1 (Ctrl+Z)").arg(undoStack.last().description));
    }
    if (redoBtn) {
        redoBtn->setEnabled(!redoStack.isEmpty());
        redoBtn->setToolTip(redoStack.isEmpty()
            ? tr("Redo (Ctrl+Shift+Z, Ctrl+Y)")
            : tr("Redo %1 (Ctrl+Shift+Z, Ctrl+Y)")
                .arg(redoStack.last().description));
    }
}

bool ProbeMakerPage::eventFilter(QObject* obj, QEvent* ev)
{
    // Drag-tracking protocol: mouse-down over a movable item snapshots
    // the pre-drag state; mouse-up checks whether the item actually
    // moved.  If it did, the snapshot stays on the undo stack; if it
    // was just a click-select with no movement, we pop the snapshot
    // to keep the history free of no-op entries.
    //
    // We filter both viewports — the logical view's viewport for
    // LogicalNodeItem drags (which mutate logicalState) and the
    // physical view's viewport for ChannelItem / ShankItem drags
    // (which mutate posUm / originUm in data).
    //
    // Determining "is the cursor over a movable item" uses the view's
    // itemAt(): we identify which view owns the viewport, map the
    // mouse position into the view's coords, and ask what's there.

    // Identify the owning view (if any).
    QGraphicsView* view = nullptr;
    if (physicalView && obj == physicalView->viewport()) view = physicalView;
    else if (logicalView && obj == logicalView->viewport()) view = logicalView;
    if (!view) return QWidget::eventFilter(obj, ev);

    if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() != Qt::LeftButton)
            return QWidget::eventFilter(obj, ev);   // only left-button drags

        // Look up the item under the cursor.  itemAt() takes view coords,
        // which is exactly what me->pos() gives us.
        QGraphicsItem* hit = view->itemAt(me->pos());
        if (!hit || !(hit->flags() & QGraphicsItem::ItemIsMovable))
            return QWidget::eventFilter(obj, ev);

        // Snapshot the pre-drag state.  Description is generic — we
        // don't know yet whether the user is dragging a channel, a
        // shank, or a logical-graph node, but the snapshot is the
        // same shape regardless.
        pushUndoSnapshot(tr("Move"));
        dragSnapshotPushed = true;
        dragItem           = hit;
        dragStartPos       = hit->pos();
        // Don't accept the event — let Qt route it normally so the
        // built-in drag machinery still works.
        return QWidget::eventFilter(obj, ev);
    }

    if (ev->type() == QEvent::MouseButtonRelease) {
        if (!dragSnapshotPushed)
            return QWidget::eventFilter(obj, ev);

        // Did the item actually move?  If yes, the snapshot stays on
        // the undo stack — the drag is undoable.  If no (just a
        // click-select), pop the snapshot so the user's undo history
        // doesn't fill with no-op entries from selecting items.
        //
        // Defensive: confirm the dragged item is still in its scene.
        // If a structural rebuild fired mid-drag (it shouldn't, but)
        // dragItem could be a dangling pointer; skip the position
        // read and just pop in that case.  view->scene()->items()
        // is O(n) but only runs on mouse release, not per move.
        bool moved = false;
        if (dragItem && view->scene()
            && view->scene()->items().contains(dragItem)) {
            moved = (dragItem->pos() != dragStartPos);
        }
        if (!moved && !undoStack.isEmpty()) {
            // Drop the just-pushed entry.  The matching redo stack
            // was cleared by the push; nothing else to clean up.
            undoStack.removeLast();
            updateUndoRedoButtons();
        }
        dragSnapshotPushed = false;
        dragItem           = nullptr;
        // dragStartPos doesn't need clearing; only valid while
        // dragSnapshotPushed is true.
    }

    return QWidget::eventFilter(obj, ev);
}

// ═══════════════════════════════════════════════════════════════════════════
// File I/O — canonical `probeFile:` schema
// ═══════════════════════════════════════════════════════════════════════════

bool ProbeMakerPage::loadFromFile(const QString& path, QString* error)
{
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.toStdString());
    } catch (const YAML::Exception& e) {
        if (error) *error = QStringLiteral("Parse error: %1").arg(e.what());
        return false;
    }

    if (!root["probeFile"]) {
        if (error) {
            *error = QStringLiteral("Missing top-level 'probeFile:' key in %1")
                        .arg(path);
        }
        return false;
    }
    YAML::Node pf = root["probeFile"];

    ProbeConnector c;
    if (pf["version"])       c.version = QString::fromStdString(pf["version"].as<std::string>());
    if (pf["vendor"])        c.vendor  = QString::fromStdString(pf["vendor"].as<std::string>());
    if (pf["model"])         c.model   = QString::fromStdString(pf["model"].as<std::string>());
    if (pf["catalogPage"])   c.catalogPage = QString::fromStdString(pf["catalogPage"].as<std::string>());
    if (pf["totalChannels"]) c.totalChannels = pf["totalChannels"].as<int>();
    if (pf["substrate"]) {
        if (pf["substrate"]["material"])
            c.substrateMaterial = QString::fromStdString(pf["substrate"]["material"].as<std::string>());
        if (pf["substrate"]["thickness_um"] && !pf["substrate"]["thickness_um"].IsNull())
            c.substrateThicknessUm = pf["substrate"]["thickness_um"].as<double>();
    }
    if (pf["notes"]) c.notes = QString::fromStdString(pf["notes"].as<std::string>());

    // Shanks: build N empty shanks from `shanks.count`, then fill in
    // sites from `sites.geometry`.  count_per_shank tells us how to
    // distribute geometry entries across shanks.
    int nShanks = 1;
    if (pf["shanks"] && pf["shanks"]["count"])
        nShanks = pf["shanks"]["count"].as<int>();

    QList<int> perShank;
    if (pf["sites"] && pf["sites"]["count_per_shank"]) {
        YAML::Node cps = pf["sites"]["count_per_shank"];
        if (cps.IsScalar()) {
            const int n = cps.as<int>();
            for (int i = 0; i < nShanks; ++i) perShank.append(n);
        } else if (cps.IsSequence()) {
            for (auto it = cps.begin(); it != cps.end(); ++it)
                perShank.append(it->as<int>());
        }
    }
    while (perShank.size() < nShanks) perShank.append(0);

    qreal lengthMm = 0.0;
    if (pf["shanks"] && pf["shanks"]["length_mm"]
        && !pf["shanks"]["length_mm"].IsNull()) {
        lengthMm = pf["shanks"]["length_mm"].as<double>();
    }
    qreal spacingUm = 0.0;
    if (pf["shanks"] && pf["shanks"]["spacing_um"]
        && !pf["shanks"]["spacing_um"].IsNull()) {
        spacingUm = pf["shanks"]["spacing_um"].as<double>();
    }

    for (int si = 0; si < nShanks; ++si) {
        ProbeShank s;
        s.id    = QStringLiteral("shank%1").arg(si + 1);
        s.label = QStringLiteral("Shank %1").arg(QChar('A' + si));
        s.lengthUm = lengthMm > 0 ? lengthMm * 1000.0 : 1500.0;
        s.widthUm  = 70.0;
        s.originUm = QPointF(spacingUm * si, 0.0);
        if (pf["sites"] && pf["sites"]["layout"])
            s.layout = QString::fromStdString(pf["sites"]["layout"].as<std::string>());
        c.shanks.append(s);
    }

    // Sites: walk the geometry, distributing into shanks per perShank.
    //
    // Schema conventions (see canonical files in nphys-data/.../probes):
    //   x = lateral position from FIRST shank's centreline, in a single
    //       global frame across all shanks.  Multi-shank probes have
    //       sites at x = k * spacing_um ± (within-shank lateral offset)
    //       for shank k.  Single-shank probes have x in a small range
    //       (typically -W/2 to +W/2) around the shank's centreline.
    //   y = depth from the TIP of the shank, growing toward the head.
    //       Sites are typically listed tip-to-base (y ascending).
    //
    // Internal model conventions (see ProbeShank/ProbeChannel docs):
    //   Each ProbeChannel::posUm is in the shank's LOCAL frame, with
    //   x = 0 at the centreline and y = 0 at the head growing toward
    //   the tip (y = lengthUm at the tip).  This matches Qt's natural
    //   "y grows downward in screen coords" so the polygon renders
    //   head-up and tip-down without any inversion.
    //
    // The translation is therefore:
    //   localX = schemaX - shankOriginX   (shank k centreline is at k * spacing_um)
    //   localY = lengthUm - schemaY       (flip "depth from tip" → "depth from head")
    //
    // When no spacing_um is given (single-shank probes, or files that
    // omit the field), we derive each shank's origin from the median
    // x of its assigned sites — that's robust to both centred and
    // off-centre lateral layouts.
    if (pf["sites"] && pf["sites"]["geometry"] && pf["sites"]["geometry"].IsSequence()) {
        // First pass: distribute raw [x, y] pairs into per-shank buckets
        // by simple cursor advance using count_per_shank.
        struct RawSite { double x; double y; };
        QVector<QVector<RawSite>> rawByShank(nShanks);
        {
            int shankIdx = 0;
            for (auto it = pf["sites"]["geometry"].begin();
                 it != pf["sites"]["geometry"].end(); ++it) {
                const YAML::Node& pair = *it;
                if (!pair.IsSequence() || pair.size() < 2) continue;
                while (shankIdx < nShanks
                       && rawByShank[shankIdx].size() >= perShank.value(shankIdx, 0))
                    shankIdx++;
                if (shankIdx >= nShanks) shankIdx = nShanks - 1;   // overflow
                rawByShank[shankIdx].append(
                    { pair[0].as<double>(), pair[1].as<double>() });
            }
        }

        // Second pass: derive shank origins.  spacing_um wins if set;
        // otherwise we use the median x of each shank's sites as its
        // centreline (robust against ±-staggered designs).
        for (int si = 0; si < nShanks; ++si) {
            qreal originX;
            if (spacingUm > 0.0) {
                originX = spacingUm * si;
            } else if (!rawByShank[si].isEmpty()) {
                QVector<double> xs;
                xs.reserve(rawByShank[si].size());
                for (const auto& r : rawByShank[si]) xs.append(r.x);
                std::sort(xs.begin(), xs.end());
                // Median: average of the two middle values (or the
                // single middle value for odd-sized lists).
                const int n = xs.size();
                originX = (n % 2 == 0) ? 0.5 * (xs[n/2 - 1] + xs[n/2])
                                       : xs[n/2];
            } else {
                originX = 0.0;
            }
            c.shanks[si].originUm = QPointF(originX, 0.0);
        }

        // Third pass: emit ProbeChannel objects in shank-local coords,
        // flipping y from depth-from-tip to depth-from-head.
        const qreal area = pf["sites"]["area_um2"]
            ? pf["sites"]["area_um2"].as<double>()
            : 177.0;
        int nextHw = 0;
        for (int si = 0; si < nShanks; ++si) {
            const qreal originX = c.shanks[si].originUm.x();
            const qreal lengthUm = c.shanks[si].lengthUm;
            int siteCursor = 0;
            for (const auto& r : rawByShank[si]) {
                ProbeChannel ch;
                ch.hardwareId = nextHw++;
                ch.posUm = QPointF(r.x - originX, lengthUm - r.y);
                ch.areaUm2 = area;
                ch.siteIndex = siteCursor++;
                c.shanks[si].channels.append(ch);
            }
        }
    }

    // Channel map (optional)
    if (pf["channelMap"]) {
        if (pf["channelMap"]["description"])
            c.channelMapDescription = QString::fromStdString(
                pf["channelMap"]["description"].as<std::string>());
        if (pf["channelMap"]["map"] && pf["channelMap"]["map"].IsSequence()) {
            for (auto it = pf["channelMap"]["map"].begin();
                 it != pf["channelMap"]["map"].end(); ++it) {
                c.channelMap.append(it->as<int>());
            }
        }
    }

    setConnector(c);
    return true;
}

bool ProbeMakerPage::saveToFile(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Could not open '%1' for writing: %2")
                               .arg(path, f.errorString());
        return false;
    }
    QTextStream out(&f);

    // Write header comment + canonical schema.  We hand-format rather
    // than going through yaml-cpp::Emitter because the canonical
    // library files have specific comment/whitespace conventions
    // (block separators, inline comments on key lines) that
    // round-trip cleanly only with hand-formatting.  All user-
    // controlled strings (vendor, model, notes, catalogPage,
    // descriptions) are passed through yamlEscape() to handle special
    // characters that would otherwise produce malformed YAML.
    out << "# neurosuite-3 probe file — generated by ndmanager Probe Maker\n";
    out << "# Canonical schema: matches src/nphys-data/src/probes/*.probe.\n";
    out << "probeFile:\n";
    out << "  version: " << yamlEscape(data.version) << "\n";
    if (!data.vendor.isEmpty())
        out << "  vendor: "      << yamlEscape(data.vendor) << "\n";
    if (!data.model.isEmpty())
        out << "  model: "       << yamlEscape(data.model) << "\n";
    if (!data.catalogPage.isEmpty())
        out << "  catalogPage: " << yamlEscape(data.catalogPage) << "\n";

    // Compute totalChannels from the model in case the field is stale.
    int total = 0;
    for (const ProbeShank& s : data.shanks) total += s.channels.size();
    out << "  totalChannels: " << qMax(data.totalChannels, total) << "\n";

    out << "  substrate:\n";
    out << "    material: "
        << yamlEscape(data.substrateMaterial.isEmpty()
                       ? QStringLiteral("silicon")
                       : data.substrateMaterial) << "\n";
    if (data.substrateThicknessUm > 0)
        out << "    thickness_um: " << data.substrateThicknessUm << "\n";
    else
        out << "    thickness_um: null\n";

    out << "  shanks:\n";
    out << "    count: " << data.shanks.size() << "\n";
    // spacing_um: derive from originUm differences iff uniform across
    // all gaps.  qFuzzyCompare's documented zero-safety trick (adding
    // 1.0 to both sides) is unnecessary here — spacings are always
    // non-zero for multi-shank probes — so use the direct comparison.
    qreal spacing = 0.0;
    bool spacingUniform = true;
    if (data.shanks.size() >= 2) {
        spacing = data.shanks[1].originUm.x() - data.shanks[0].originUm.x();
        for (int i = 2; i < data.shanks.size(); ++i) {
            const qreal d = data.shanks[i].originUm.x()
                          - data.shanks[i-1].originUm.x();
            if (!qFuzzyCompare(d, spacing)) {
                spacingUniform = false;
                break;
            }
        }
    }
    if (data.shanks.size() >= 2 && spacingUniform)
        out << "    spacing_um: " << spacing << "\n";
    else
        out << "    spacing_um: null\n";
    if (!data.shanks.isEmpty()) {
        out << "    length_mm: "
            << (data.shanks.first().lengthUm / 1000.0) << "\n";
    } else {
        out << "    length_mm: null\n";
    }

    // Sites
    out << "  sites:\n";
    bool uniformPerShank = true;
    int firstCount = data.shanks.isEmpty() ? 0 : data.shanks.first().channels.size();
    for (const ProbeShank& s : data.shanks) {
        if (s.channels.size() != firstCount) { uniformPerShank = false; break; }
    }
    if (uniformPerShank) {
        out << "    count_per_shank: " << firstCount << "\n";
    } else {
        out << "    count_per_shank: [";
        for (int i = 0; i < data.shanks.size(); ++i) {
            out << data.shanks[i].channels.size();
            if (i + 1 < data.shanks.size()) out << ", ";
        }
        out << "]\n";
    }
    // SCHEMA NOTE: `sites.layout` and `sites.area_um2` are GLOBAL
    // fields in the canonical schema, but the in-memory model carries
    // them per-shank (layout) and per-channel (area_um2).  We write
    // the FIRST value we see and silently drop divergent entries.  In
    // practice this is fine: probes have one substrate fab, so all
    // sites of one probe share area, and all shanks of one probe share
    // layout.  If a future schema revision adds per-shank/per-channel
    // overrides, the loader and saver will both need updating.
    if (!data.shanks.isEmpty())
        out << "    layout: " << yamlEscape(data.shanks.first().layout) << "\n";
    qreal area = 177.0;
    if (!data.shanks.isEmpty() && !data.shanks.first().channels.isEmpty())
        area = data.shanks.first().channels.first().areaUm2;
    out << "    area_um2: " << area << "\n";
    out << "    spacing_um: null\n";
    out << "    geometry:\n";
    // Convert each site from internal shank-local (x = lateral from
    // centreline, y = depth from head) back to the schema's global
    // frame (x = lateral from first-shank centreline, y = depth from
    // tip).  This is the inverse of the loadFromFile transformation.
    for (const ProbeShank& s : data.shanks) {
        const qreal originX = s.originUm.x();
        const qreal lengthUm = s.lengthUm;
        for (const ProbeChannel& ch : s.channels) {
            const qreal globalX = ch.posUm.x() + originX;
            const qreal schemaY = lengthUm - ch.posUm.y();
            out << "      - [" << globalX << ", " << schemaY << "]\n";
        }
    }

    out << "  channelMap:\n";
    out << "    description: "
        << yamlEscape(data.channelMapDescription.isEmpty()
                       ? QStringLiteral("Sequential — site index = hardware channel.")
                       : data.channelMapDescription)
        << "\n";
    if (data.channelMap.isEmpty()) {
        out << "    map: null\n";
    } else {
        out << "    map: [";
        for (int i = 0; i < data.channelMap.size(); ++i) {
            out << data.channelMap[i];
            if (i + 1 < data.channelMap.size()) out << ", ";
        }
        out << "]\n";
    }

    if (!data.notes.isEmpty()) {
        out << "  notes: " << yamlEscape(data.notes) << "\n";
    }

    out.flush();
    if (f.error() != QFile::NoError) {
        if (error) *error = QStringLiteral("Write failed: %1").arg(f.errorString());
        return false;
    }
    return true;
}
