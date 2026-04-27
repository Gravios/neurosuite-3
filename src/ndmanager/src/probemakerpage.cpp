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

#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QVBoxLayout>

#include <yaml-cpp/yaml.h>

using probemaker::ChannelItem;
using probemaker::ConnectorItem;
using probemaker::ItemTypeChannel;
using probemaker::ItemTypeConnector;
using probemaker::ItemTypeShank;
using probemaker::ProbeLogicalView;
using probemaker::ProbePhysicalView;
using probemaker::RoleItemKind;
using probemaker::RoleModelPtr;
using probemaker::RoleObjectId;
using probemaker::ShankItem;

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

    m_loadBtn = new QPushButton(tr("📂 Load…"));
    m_saveBtn = new QPushButton(tr("💾 Save"));
    m_saveBtn->setToolTip(tr("Save probe as <session>.ndm.default.probe (Ctrl+Alt+R)"));
    m_saveAsBtn = new QPushButton(tr("Save As…"));
    m_saveAsBtn->setToolTip(tr("Save probe under a custom name (Ctrl+Alt+Shift+R)"));

    const QString tealBtn =
        "QPushButton{background:#102a18;border:1px solid #2dd4bf;color:#2dd4bf;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#163d20;}";
    m_saveBtn->setStyleSheet(tealBtn);
    m_saveAsBtn->setStyleSheet(tealBtn);

    const QString neutralBtn =
        "QPushButton{background:#0d1117;border:1px solid #2a3650;color:#8fa8c8;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#141c2a;color:#bcd0e8;}";
    m_loadBtn->setStyleSheet(neutralBtn);

    m_addShankBtn   = new QPushButton(tr("+ Shank"));
    m_addChannelBtn = new QPushButton(tr("+ Channel"));
    m_deleteBtn     = new QPushButton(tr("Delete"));
    m_addShankBtn->setStyleSheet(neutralBtn);
    m_addChannelBtn->setStyleSheet(neutralBtn);
    m_deleteBtn->setStyleSheet(
        "QPushButton{background:#180404;border:1px solid #f87171;color:#f87171;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#2d1010;}");

    connect(m_loadBtn,       &QPushButton::clicked,
            this, [this]{ /* handled by ndManager menu via dialog;
                           we just emit nothing here.  TODO: optional
                           in-page Load dialog. */ });
    connect(m_saveBtn,       &QPushButton::clicked,
            this, &ProbeMakerPage::savePipelineRequested);
    connect(m_saveAsBtn,     &QPushButton::clicked,
            this, &ProbeMakerPage::saveAsPipelineRequested);
    connect(m_addShankBtn,   &QPushButton::clicked,
            this, &ProbeMakerPage::onAddShankClicked);
    connect(m_addChannelBtn, &QPushButton::clicked,
            this, &ProbeMakerPage::onAddChannelClicked);
    connect(m_deleteBtn,     &QPushButton::clicked,
            this, &ProbeMakerPage::onDeleteSelectedClicked);

    tbox->addWidget(m_loadBtn);
    tbox->addWidget(m_saveBtn);
    tbox->addWidget(m_saveAsBtn);
    tbox->addSpacing(16);
    tbox->addWidget(m_addShankBtn);
    tbox->addWidget(m_addChannelBtn);
    tbox->addWidget(m_deleteBtn);
    tbox->addStretch();

    auto* helpLabel = new QLabel(
        tr("Ctrl+wheel to zoom · middle-drag to pan · drag pads to reposition"));
    helpLabel->setStyleSheet("color:#2a3a50;font-size:10px;");
    tbox->addWidget(helpLabel);

    outerVbox->addWidget(toolbar);

    // ── Splitter: logical | physical | inspector ─────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(2);
    splitter->setStyleSheet("QSplitter::handle{background:#1e2736;}");

    buildLogicalPane(splitter);
    buildPhysicalPane(splitter);
    buildInspector(splitter);

    splitter->setStretchFactor(0, 2);    // logical
    splitter->setStretchFactor(1, 4);    // physical (largest)
    splitter->setStretchFactor(2, 2);    // inspector

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

    m_logicalView = new ProbeLogicalView;
    auto* logicalScene = new QGraphicsScene(m_logicalView);
    m_logicalView->setScene(logicalScene);
    connect(logicalScene, &QGraphicsScene::selectionChanged,
            this, &ProbeMakerPage::onLogicalSelectionChanged);
    v->addWidget(m_logicalView, /*stretch=*/1);

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

    m_physicalView = new ProbePhysicalView;
    auto* physicalScene = new QGraphicsScene(m_physicalView);
    m_physicalView->setScene(physicalScene);
    connect(physicalScene, &QGraphicsScene::selectionChanged,
            this, &ProbeMakerPage::onPhysicalSelectionChanged);
    v->addWidget(m_physicalView, /*stretch=*/1);

    inner->addWidget(wrap);
}

void ProbeMakerPage::buildInspector(QSplitter* inner)
{
    m_inspScroll = new QScrollArea;
    m_inspScroll->setWidgetResizable(true);
    m_inspScroll->setStyleSheet("QScrollArea{background:#0d1117;border:none;}");

    m_inspWidget = new QWidget;
    m_inspWidget->setStyleSheet("background:#0d1117;");
    auto* iv = new QVBoxLayout(m_inspWidget);
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
    m_connGroup = new QWidget;
    auto* cf = new QFormLayout(m_connGroup);
    cf->setContentsMargins(0, 0, 0, 0);
    cf->setSpacing(6);
    auto mkLbl = [&](const QString& s) {
        auto* l = new QLabel(s.toUpper());
        l->setStyleSheet(lblStyle);
        return l;
    };
    m_connVendor    = new QLineEdit;
    m_connModel     = new QLineEdit;
    m_connTotalChan = new QSpinBox;
    m_connTotalChan->setRange(0, 100000);
    m_connNotes     = new QLineEdit;
    for (auto* w : { static_cast<QWidget*>(m_connVendor),
                     static_cast<QWidget*>(m_connModel),
                     static_cast<QWidget*>(m_connTotalChan),
                     static_cast<QWidget*>(m_connNotes) }) {
        w->setStyleSheet(fldStyle);
    }
    cf->addRow(mkLbl(tr("Vendor")),     m_connVendor);
    cf->addRow(mkLbl(tr("Model")),      m_connModel);
    cf->addRow(mkLbl(tr("Channels")),   m_connTotalChan);
    cf->addRow(mkLbl(tr("Notes")),      m_connNotes);
    for (QLineEdit* le : { m_connVendor, m_connModel, m_connNotes }) {
        connect(le, &QLineEdit::editingFinished,
                this, &ProbeMakerPage::onConnectorFieldEdited);
    }
    connect(m_connTotalChan, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProbeMakerPage::onConnectorFieldEdited);

    // Shank form
    m_shankGroup = new QWidget;
    auto* sf = new QFormLayout(m_shankGroup);
    sf->setContentsMargins(0, 0, 0, 0);
    sf->setSpacing(6);
    m_shankLabel    = new QLineEdit;
    m_shankLength   = new QDoubleSpinBox;  m_shankLength->setRange(1, 50000);   m_shankLength->setSuffix(" µm");
    m_shankWidth    = new QDoubleSpinBox;  m_shankWidth->setRange(1, 5000);     m_shankWidth->setSuffix(" µm");
    m_shankTipAngle = new QDoubleSpinBox;  m_shankTipAngle->setRange(30, 180);  m_shankTipAngle->setSuffix("°");
    m_shankLayout   = new QLineEdit;
    for (auto* w : { static_cast<QWidget*>(m_shankLabel),
                     static_cast<QWidget*>(m_shankLength),
                     static_cast<QWidget*>(m_shankWidth),
                     static_cast<QWidget*>(m_shankTipAngle),
                     static_cast<QWidget*>(m_shankLayout) }) {
        w->setStyleSheet(fldStyle);
    }
    sf->addRow(mkLbl(tr("Label")),      m_shankLabel);
    sf->addRow(mkLbl(tr("Length")),     m_shankLength);
    sf->addRow(mkLbl(tr("Width")),      m_shankWidth);
    sf->addRow(mkLbl(tr("Tip angle")),  m_shankTipAngle);
    sf->addRow(mkLbl(tr("Layout")),     m_shankLayout);
    connect(m_shankLabel,    &QLineEdit::editingFinished,
            this, &ProbeMakerPage::onShankFieldEdited);
    connect(m_shankLayout,   &QLineEdit::editingFinished,
            this, &ProbeMakerPage::onShankFieldEdited);
    for (QDoubleSpinBox* sb : { m_shankLength, m_shankWidth, m_shankTipAngle }) {
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &ProbeMakerPage::onShankFieldEdited);
    }

    // Channel form
    m_chanGroup = new QWidget;
    auto* chf = new QFormLayout(m_chanGroup);
    chf->setContentsMargins(0, 0, 0, 0);
    chf->setSpacing(6);
    m_chanHwId    = new QSpinBox;       m_chanHwId->setRange(0, 99999);
    m_chanX       = new QDoubleSpinBox; m_chanX->setRange(-50000, 50000); m_chanX->setSuffix(" µm");
    m_chanY       = new QDoubleSpinBox; m_chanY->setRange(-50000, 50000); m_chanY->setSuffix(" µm");
    m_chanSiteIdx = new QSpinBox;       m_chanSiteIdx->setRange(-1, 99999);
    m_chanArea    = new QDoubleSpinBox; m_chanArea->setRange(0, 100000);  m_chanArea->setSuffix(" µm²");
    for (auto* w : { static_cast<QWidget*>(m_chanHwId),
                     static_cast<QWidget*>(m_chanX),
                     static_cast<QWidget*>(m_chanY),
                     static_cast<QWidget*>(m_chanSiteIdx),
                     static_cast<QWidget*>(m_chanArea) }) {
        w->setStyleSheet(fldStyle);
    }
    chf->addRow(mkLbl(tr("Hw channel")),  m_chanHwId);
    chf->addRow(mkLbl(tr("Pos x")),       m_chanX);
    chf->addRow(mkLbl(tr("Pos y")),       m_chanY);
    chf->addRow(mkLbl(tr("Site index")),  m_chanSiteIdx);
    chf->addRow(mkLbl(tr("Area")),        m_chanArea);
    for (QSpinBox* sb : { m_chanHwId, m_chanSiteIdx }) {
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &ProbeMakerPage::onChannelFieldEdited);
    }
    for (QDoubleSpinBox* sb : { m_chanX, m_chanY, m_chanArea }) {
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &ProbeMakerPage::onChannelFieldEdited);
    }

    iv->addWidget(m_connGroup);
    iv->addWidget(m_shankGroup);
    iv->addWidget(m_chanGroup);
    iv->addStretch();

    m_inspScroll->setWidget(m_inspWidget);
    inner->addWidget(m_inspScroll);

    refreshInspector();   // hide all groups initially (no selection)
}

// ═══════════════════════════════════════════════════════════════════════════
// Scene rebuild
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::rebuildLogicalScene()
{
    if (!m_logicalView) return;
    QGraphicsScene* s = m_logicalView->scene();
    s->clear();   // deletes all items, including children

    constexpr qreal connW = 240.0;
    constexpr qreal yConn = 0.0;
    constexpr qreal yShank = yConn + ConnectorItem::NodeHeight + 60.0;
    constexpr qreal shankW = 200.0;
    constexpr qreal shankH = 56.0;
    constexpr qreal shankGap = 60.0;
    constexpr qreal chanW = 40.0;
    constexpr qreal chanH = 32.0;
    constexpr qreal chanGap = 8.0;

    // Connector at top, centred horizontally over the shank row
    const int nShanks = m_data.shanks.size();
    const qreal totalShankRowW = nShanks > 0
        ? nShanks * shankW + (nShanks - 1) * shankGap
        : shankW;
    const qreal sceneW = qMax(connW, totalShankRowW);
    auto* connItem = new ConnectorItem(&m_data, connW);
    connItem->setPos((sceneW - connW) * 0.5, yConn);
    s->addItem(connItem);

    qreal x = (sceneW - totalShankRowW) * 0.5;
    for (int si = 0; si < nShanks; ++si) {
        ProbeShank& sh = m_data.shanks[si];
        auto* shankItem = new ShankItem(&sh);
        shankItem->setLogicalMode(true);
        shankItem->setPos(x, yShank);
        s->addItem(shankItem);

        // Channels as a horizontal row underneath (capped at 6 visible
        // pills + ellipsis, since the logical view is for structure
        // not for showing every site).
        const int nch = sh.channels.size();
        const int maxVis = 5;
        const int nVis = qMin(nch, maxVis);
        const qreal chanRowW = nVis * chanW + qMax(0, nVis - 1) * chanGap;
        qreal cx = x + (shankW - chanRowW) * 0.5;
        const qreal cy = yShank + shankH + 16.0;
        for (int ci = 0; ci < nVis; ++ci) {
            ProbeChannel& ch = sh.channels[ci];
            auto* chItem = new QGraphicsRectItem(0, 0, chanW, chanH);
            chItem->setBrush(QColor(0x07, 0x12, 0x22));
            chItem->setPen(QPen(QColor(0x37, 0x8a, 0xdd), 0.5));
            chItem->setData(RoleItemKind, QStringLiteral("channel"));
            chItem->setData(RoleObjectId,
                            QString::number(ch.hardwareId));
            chItem->setData(RoleModelPtr,
                            QVariant::fromValue(static_cast<void*>(&ch)));
            chItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
            chItem->setPos(cx + ci * (chanW + chanGap), cy);
            s->addItem(chItem);
            // Label is added as a child text item — small enough to
            // ignore the no-rotated-text rule.  We use a plain label.
            auto* txt = new QGraphicsSimpleTextItem(
                QStringLiteral("ch %1").arg(ch.hardwareId), chItem);
            txt->setBrush(QColor(0x85, 0xb7, 0xeb));
            QFont f = txt->font();
            f.setPointSizeF(8.0);
            txt->setFont(f);
            const QRectF tb = txt->boundingRect();
            txt->setPos((chanW - tb.width()) * 0.5, (chanH - tb.height()) * 0.5);
        }
        if (nch > maxVis) {
            // "…" trailing pill
            auto* dots = new QGraphicsSimpleTextItem(
                QStringLiteral("+%1 more").arg(nch - maxVis));
            dots->setBrush(QColor(0x4a, 0x60, 0x80));
            QFont f = dots->font();
            f.setPointSizeF(8.0);
            dots->setFont(f);
            dots->setPos(cx + nVis * (chanW + chanGap), cy + 8.0);
            s->addItem(dots);
        }

        // Connector → shank line
        auto* line = new QGraphicsLineItem(
            connW * 0.5 + (sceneW - connW) * 0.5,
            yConn + ConnectorItem::NodeHeight,
            x + shankW * 0.5,
            yShank);
        line->setPen(QPen(QColor(0xfa, 0xc1, 0x5c), 0.5));
        line->setZValue(-1.0);
        s->addItem(line);

        x += shankW + shankGap;
    }

    s->setSceneRect(s->itemsBoundingRect().marginsAdded(QMarginsF(40, 40, 40, 40)));
}

void ProbeMakerPage::rebuildPhysicalScene()
{
    if (!m_physicalView) return;
    QGraphicsScene* s = m_physicalView->scene();
    s->clear();

    // Connector ribbon — horizontal bar at the top of the scene
    constexpr qreal connBarH = 40.0;
    constexpr qreal connBarMargin = 40.0;
    auto* connItem = new ConnectorItem(&m_data, 280.0);
    connItem->setPos(0.0, -connBarH - 80.0);
    s->addItem(connItem);

    // Shanks at their stored originUm.  When originUm is (0, 0) for
    // every shank (as on a fresh probe), spread them along the X axis
    // 200 µm apart so they're individually selectable.  Once the user
    // drags one or sets an explicit origin, that auto-spread stops.
    bool needsAutoSpread = true;
    for (const ProbeShank& sh : m_data.shanks) {
        if (sh.originUm != QPointF(0, 0)) {
            needsAutoSpread = false;
            break;
        }
    }
    qreal autoX = 0.0;
    for (int si = 0; si < m_data.shanks.size(); ++si) {
        ProbeShank& sh = m_data.shanks[si];
        auto* shankItem = new ShankItem(&sh);
        shankItem->setLogicalMode(false);
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
    m_physicalView->fitAll();
}

// ═══════════════════════════════════════════════════════════════════════════
// Selection mirroring
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::onLogicalSelectionChanged()
{
    mirrorSelection(m_logicalView->scene(), m_physicalView->scene());
    refreshInspector();
}

void ProbeMakerPage::onPhysicalSelectionChanged()
{
    mirrorSelection(m_physicalView->scene(), m_logicalView->scene());
    refreshInspector();
}

void ProbeMakerPage::mirrorSelection(const QGraphicsScene* sourceScene,
                                     QGraphicsScene*       targetScene)
{
    if (!sourceScene || !targetScene) return;

    // Resolve the selected (kind, id) pair on the source side.
    QString kind, id;
    void*   modelPtr = nullptr;
    for (QGraphicsItem* it : sourceScene->selectedItems()) {
        kind     = it->data(RoleItemKind).toString();
        id       = it->data(RoleObjectId).toString();
        modelPtr = it->data(RoleModelPtr).value<void*>();
        break;   // we only mirror the first selection (no multi-select v1)
    }

    // Cache the page-level selection for inspector refresh.
    m_connectorSelected = false;
    m_selectedShank     = nullptr;
    m_selectedChannel   = nullptr;
    if (kind == QLatin1String("connector")) {
        m_connectorSelected = true;
    } else if (kind == QLatin1String("shank")) {
        m_selectedShank   = static_cast<ProbeShank*>(modelPtr);
    } else if (kind == QLatin1String("channel")) {
        m_selectedChannel = static_cast<ProbeChannel*>(modelPtr);
    }

    // Apply the same selection in the target scene.  Block signals
    // while we do so to avoid bouncing back and triggering this slot
    // again.
    QSignalBlocker blocker(targetScene);
    for (QGraphicsItem* it : targetScene->items()) {
        const bool match =
            it->data(RoleItemKind).toString() == kind &&
            it->data(RoleObjectId).toString() == id;
        it->setSelected(match);
    }
    targetScene->update();
}

// ═══════════════════════════════════════════════════════════════════════════
// Toolbar actions
// ═══════════════════════════════════════════════════════════════════════════

void ProbeMakerPage::onAddShankClicked()
{
    ProbeShank sh;
    sh.id    = QStringLiteral("shank%1").arg(m_data.shanks.size() + 1);
    sh.label = QStringLiteral("Shank %1")
                   .arg(QChar('A' + m_data.shanks.size()));
    sh.lengthUm = 1500.0;
    sh.widthUm  = 70.0;
    sh.tipAngle = 90.0;
    sh.layout   = QStringLiteral("linear");
    m_data.shanks.append(sh);
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(true);
}

void ProbeMakerPage::onAddChannelClicked()
{
    if (!m_selectedShank) return;   // need a shank context
    ProbeChannel ch;
    // Hardware id: highest existing + 1, across all shanks
    int maxHw = -1;
    for (const ProbeShank& s : m_data.shanks)
        for (const ProbeChannel& c : s.channels)
            maxHw = qMax(maxHw, c.hardwareId);
    ch.hardwareId = maxHw + 1;
    // Default position: stack down the shank centre line at 50 µm pitch
    const qreal y = 25.0 + 50.0 * m_selectedShank->channels.size();
    ch.posUm = QPointF(m_selectedShank->widthUm * 0.5, y);
    ch.areaUm2 = 177.0;
    m_selectedShank->channels.append(ch);
    m_data.totalChannels = qMax(m_data.totalChannels, ch.hardwareId + 1);
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(true);
}

void ProbeMakerPage::onDeleteSelectedClicked()
{
    if (m_selectedChannel) {
        // Find which shank owns it and remove via pointer match.
        for (ProbeShank& s : m_data.shanks) {
            for (int i = s.channels.size() - 1; i >= 0; --i) {
                if (&s.channels[i] == m_selectedChannel) {
                    s.channels.removeAt(i);
                    m_selectedChannel = nullptr;
                    rebuildLogicalScene();
                    rebuildPhysicalScene();
                    setModified(true);
                    return;
                }
            }
        }
    } else if (m_selectedShank) {
        for (int i = m_data.shanks.size() - 1; i >= 0; --i) {
            if (&m_data.shanks[i] == m_selectedShank) {
                m_data.shanks.removeAt(i);
                m_selectedShank = nullptr;
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
    m_connGroup->setVisible(m_connectorSelected);
    m_shankGroup->setVisible(m_selectedShank   != nullptr);
    m_chanGroup->setVisible(m_selectedChannel != nullptr);

    QSignalBlocker bC(m_connVendor); QSignalBlocker bM(m_connModel);
    QSignalBlocker bT(m_connTotalChan); QSignalBlocker bN(m_connNotes);
    if (m_connectorSelected) {
        m_connVendor->setText(m_data.vendor);
        m_connModel->setText(m_data.model);
        m_connTotalChan->setValue(m_data.totalChannels);
        m_connNotes->setText(m_data.notes);
    }

    QSignalBlocker bSL(m_shankLabel);    QSignalBlocker bSLn(m_shankLength);
    QSignalBlocker bSW(m_shankWidth);    QSignalBlocker bST(m_shankTipAngle);
    QSignalBlocker bSY(m_shankLayout);
    if (m_selectedShank) {
        m_shankLabel->setText(m_selectedShank->label);
        m_shankLength->setValue(m_selectedShank->lengthUm);
        m_shankWidth->setValue(m_selectedShank->widthUm);
        m_shankTipAngle->setValue(m_selectedShank->tipAngle);
        m_shankLayout->setText(m_selectedShank->layout);
    }

    QSignalBlocker bCH(m_chanHwId); QSignalBlocker bCX(m_chanX);
    QSignalBlocker bCY(m_chanY);    QSignalBlocker bCS(m_chanSiteIdx);
    QSignalBlocker bCA(m_chanArea);
    if (m_selectedChannel) {
        m_chanHwId->setValue(m_selectedChannel->hardwareId);
        m_chanX->setValue(m_selectedChannel->posUm.x());
        m_chanY->setValue(m_selectedChannel->posUm.y());
        m_chanSiteIdx->setValue(m_selectedChannel->siteIndex);
        m_chanArea->setValue(m_selectedChannel->areaUm2);
    }
}

void ProbeMakerPage::onConnectorFieldEdited()
{
    if (!m_connectorSelected) return;
    m_data.vendor        = m_connVendor->text();
    m_data.model         = m_connModel->text();
    m_data.totalChannels = m_connTotalChan->value();
    m_data.notes         = m_connNotes->text();
    // Refresh just the connector header — no need to rebuild scenes.
    for (QGraphicsItem* it : m_logicalView->scene()->items()) {
        if (it->type() == ItemTypeConnector)
            static_cast<ConnectorItem*>(it)->refreshFromModel();
    }
    for (QGraphicsItem* it : m_physicalView->scene()->items()) {
        if (it->type() == ItemTypeConnector)
            static_cast<ConnectorItem*>(it)->refreshFromModel();
    }
    setModified(true);
}

void ProbeMakerPage::onShankFieldEdited()
{
    if (!m_selectedShank) return;
    m_selectedShank->label    = m_shankLabel->text();
    m_selectedShank->lengthUm = m_shankLength->value();
    m_selectedShank->widthUm  = m_shankWidth->value();
    m_selectedShank->tipAngle = m_shankTipAngle->value();
    m_selectedShank->layout   = m_shankLayout->text();
    // Geometry change → rebuild physical scene; logical labels need
    // a refresh (channel-count subtitle) too.
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(true);
}

void ProbeMakerPage::onChannelFieldEdited()
{
    if (!m_selectedChannel) return;
    m_selectedChannel->hardwareId = m_chanHwId->value();
    m_selectedChannel->posUm      = QPointF(m_chanX->value(), m_chanY->value());
    m_selectedChannel->siteIndex  = m_chanSiteIdx->value();
    m_selectedChannel->areaUm2    = m_chanArea->value();
    // Channel pad position lives in the physical scene.  Refresh the
    // ChannelItem in place rather than rebuilding everything.
    for (QGraphicsItem* it : m_physicalView->scene()->items()) {
        if (it->type() == ItemTypeChannel) {
            void* ptr = it->data(RoleModelPtr).value<void*>();
            if (ptr == m_selectedChannel) {
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
    m_data = connector;
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(false);
}

ProbeConnector ProbeMakerPage::connector() const
{
    return m_data;
}

void ProbeMakerPage::clearToConnector()
{
    m_data = ProbeConnector{};
    m_data.version = QStringLiteral("1.0");
    rebuildLogicalScene();
    rebuildPhysicalScene();
    setModified(false);
}

void ProbeMakerPage::setModified(bool b)
{
    if (m_modified == b) return;
    m_modified = b;
    if (b) emit modified();
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
    if (pf["sites"] && pf["sites"]["geometry"] && pf["sites"]["geometry"].IsSequence()) {
        int nextHw = 0;
        int siteCursor = 0;
        int shankIdx = 0;
        const qreal area = pf["sites"]["area_um2"]
            ? pf["sites"]["area_um2"].as<double>()
            : 177.0;
        for (auto it = pf["sites"]["geometry"].begin();
             it != pf["sites"]["geometry"].end(); ++it) {
            const YAML::Node& pair = *it;
            if (!pair.IsSequence() || pair.size() < 2) continue;
            ProbeChannel ch;
            ch.hardwareId = nextHw++;
            ch.posUm   = QPointF(pair[0].as<double>(), pair[1].as<double>());
            ch.areaUm2 = area;
            ch.siteIndex = siteCursor;
            // Advance shank cursor when we've filled the current one.
            while (shankIdx < c.shanks.size()
                   && c.shanks[shankIdx].channels.size() >= perShank.value(shankIdx, 0)) {
                shankIdx++;
                siteCursor = 0;
            }
            if (shankIdx >= c.shanks.size()) {
                // overflow — append to last shank
                shankIdx = c.shanks.size() - 1;
            }
            c.shanks[shankIdx].channels.append(ch);
            siteCursor++;
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
    // round-trip cleanly only with hand-formatting.
    out << "# neurosuite-3 probe file — generated by ndmanager Probe Maker\n";
    out << "# Canonical schema: matches src/nphys-data/src/probes/*.probe.\n";
    out << "probeFile:\n";
    out << "  version: '" << m_data.version << "'\n";
    if (!m_data.vendor.isEmpty())
        out << "  vendor: " << m_data.vendor << "\n";
    if (!m_data.model.isEmpty())
        out << "  model: " << m_data.model << "\n";
    if (!m_data.catalogPage.isEmpty())
        out << "  catalogPage: " << m_data.catalogPage << "\n";

    // Compute totalChannels from the model in case the field is stale.
    int total = 0;
    for (const ProbeShank& s : m_data.shanks) total += s.channels.size();
    out << "  totalChannels: " << qMax(m_data.totalChannels, total) << "\n";

    out << "  substrate:\n";
    out << "    material: " << (m_data.substrateMaterial.isEmpty()
                                  ? QStringLiteral("silicon")
                                  : m_data.substrateMaterial) << "\n";
    if (m_data.substrateThicknessUm > 0)
        out << "    thickness_um: " << m_data.substrateThicknessUm << "\n";
    else
        out << "    thickness_um: null\n";

    out << "  shanks:\n";
    out << "    count: " << m_data.shanks.size() << "\n";
    // spacing_um: derive from originUm differences if uniform.
    qreal spacing = 0.0;
    bool spacingUniform = true;
    if (m_data.shanks.size() >= 2) {
        spacing = m_data.shanks[1].originUm.x() - m_data.shanks[0].originUm.x();
        for (int i = 2; i < m_data.shanks.size(); ++i) {
            const qreal d = m_data.shanks[i].originUm.x()
                          - m_data.shanks[i-1].originUm.x();
            if (qFuzzyCompare(d + 1.0, spacing + 1.0) == false) {
                spacingUniform = false;
                break;
            }
        }
    }
    if (m_data.shanks.size() >= 2 && spacingUniform)
        out << "    spacing_um: " << spacing << "\n";
    else
        out << "    spacing_um: null\n";
    if (!m_data.shanks.isEmpty()) {
        out << "    length_mm: "
            << (m_data.shanks.first().lengthUm / 1000.0) << "\n";
    } else {
        out << "    length_mm: null\n";
    }

    // Sites
    out << "  sites:\n";
    bool uniformPerShank = true;
    int firstCount = m_data.shanks.isEmpty() ? 0 : m_data.shanks.first().channels.size();
    for (const ProbeShank& s : m_data.shanks) {
        if (s.channels.size() != firstCount) { uniformPerShank = false; break; }
    }
    if (uniformPerShank) {
        out << "    count_per_shank: " << firstCount << "\n";
    } else {
        out << "    count_per_shank: [";
        for (int i = 0; i < m_data.shanks.size(); ++i) {
            out << m_data.shanks[i].channels.size();
            if (i + 1 < m_data.shanks.size()) out << ", ";
        }
        out << "]\n";
    }
    if (!m_data.shanks.isEmpty())
        out << "    layout: " << m_data.shanks.first().layout << "\n";
    qreal area = 177.0;
    if (!m_data.shanks.isEmpty() && !m_data.shanks.first().channels.isEmpty())
        area = m_data.shanks.first().channels.first().areaUm2;
    out << "    area_um2: " << area << "\n";
    out << "    spacing_um: null\n";
    out << "    geometry:\n";
    for (const ProbeShank& s : m_data.shanks) {
        for (const ProbeChannel& ch : s.channels) {
            out << "      - [" << ch.posUm.x() << ", " << ch.posUm.y() << "]\n";
        }
    }

    out << "  channelMap:\n";
    if (!m_data.channelMapDescription.isEmpty())
        out << "    description: \"" << m_data.channelMapDescription << "\"\n";
    else
        out << "    description: \"Sequential — site index = hardware channel.\"\n";
    if (m_data.channelMap.isEmpty()) {
        out << "    map: null\n";
    } else {
        out << "    map: [";
        for (int i = 0; i < m_data.channelMap.size(); ++i) {
            out << m_data.channelMap[i];
            if (i + 1 < m_data.channelMap.size()) out << ", ";
        }
        out << "]\n";
    }

    if (!m_data.notes.isEmpty()) {
        out << "  notes: \"" << m_data.notes << "\"\n";
    }

    out.flush();
    if (f.error() != QFile::NoError) {
        if (error) *error = QStringLiteral("Write failed: %1").arg(f.errorString());
        return false;
    }
    return true;
}
