/***************************************************************************
 * probemakeritems.cpp
 *
 * Implementations for the QGraphicsItem subclasses declared in
 * probemakeritems.h.  See that file for design rationale.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "probemakeritems.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QVariant>
#include <cmath>

#include "klustersshared/parameteryamlreader_probes.h"

namespace probemaker {

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette — matches the Pipeline Designer's idiom so the two pages
// feel like the same product.
// ─────────────────────────────────────────────────────────────────────────────
//   Connector (gold)   — orchestrator/root accent in pipelinedesignerpage.cpp
//   Shank     (teal)   — peer to "grouping" category
//   Channel   (blue)   — peer to "preparation" category
//
// We hold these as inline constants rather than constexpr because QColor's
// constructors aren't constexpr in Qt6.  The shape of the values mirrors
// pipelinedesignerpage.cpp's CategoryStyle table.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

inline QColor connectorBg()      { return QColor(0x2a, 0x1d, 0x05); }
inline QColor connectorAccent()  { return QColor(0xfa, 0xc1, 0x5c); }
inline QColor shankBg()          { return QColor(0x0a, 0x28, 0x26); }
inline QColor shankAccent()      { return QColor(0x2d, 0xd4, 0xbf); }
inline QColor channelFill()      { return QColor(0x85, 0xb7, 0xeb); }
inline QColor channelStroke()    { return QColor(0x37, 0x8a, 0xdd); }
inline QColor selectionStroke()  { return QColor(0x4a, 0x9e, 0xff); }

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// ConnectorItem
// ═══════════════════════════════════════════════════════════════════════════

ConnectorItem::ConnectorItem(ProbeConnector* model,
                             qreal width,
                             QGraphicsItem* parent)
    : QGraphicsRectItem(0.0, 0.0, width, NodeHeight, parent)
    , m_model(model)
    , m_width(width)
{
    // The connector is selectable (so the inspector can show its props)
    // but not movable — it's pinned by the view at a known location.
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable,    false);
    setData(RoleItemKind, QStringLiteral("connector"));
    setData(RoleModelPtr, QVariant::fromValue(static_cast<void*>(model)));
    // Z=10 — connector always above shank silhouettes.
    setZValue(10.0);
    // Suppress the default rect rendering; we paint our own.
    setPen(Qt::NoPen);
    setBrush(Qt::NoBrush);
}

void ConnectorItem::refreshFromModel()
{
    update();
}

void ConnectorItem::paint(QPainter* p,
                          const QStyleOptionGraphicsItem* opt,
                          QWidget* /*widget*/)
{
    Q_UNUSED(opt);
    const QRectF r(0.0, 0.0, m_width, NodeHeight);
    const bool   selected = (opt && (opt->state & QStyle::State_Selected));

    // Body
    p->setRenderHint(QPainter::Antialiasing);
    p->setBrush(connectorBg());
    p->setPen(QPen(selected ? selectionStroke() : connectorAccent().darker(160),
                   selected ? 2.0 : 0.5));
    p->drawRoundedRect(r, 8.0, 8.0);

    // Header stripe
    QPainterPath stripe;
    stripe.addRect(QRectF(0.0, 0.0, m_width, 6.0));
    p->setPen(Qt::NoPen);
    p->setBrush(connectorAccent());
    p->drawPath(stripe);

    // Title + subtitle
    QFont f = p->font();
    f.setPointSizeF(10.0);
    f.setWeight(QFont::Bold);
    p->setFont(f);
    p->setPen(connectorAccent().lighter(130));

    const QString label = m_model
        ? QStringLiteral("Connector ★ ROOT")
        : QStringLiteral("Connector ★ ROOT");  // model is non-owning, kept always
    p->drawText(QRectF(12.0, 10.0, m_width - 24.0, 22.0),
                Qt::AlignLeft | Qt::AlignVCenter, label);

    // Subtitle: channel count + vendor/model
    QString subtitle;
    if (m_model) {
        subtitle = QStringLiteral("%1 channels").arg(m_model->totalChannels);
        if (!m_model->model.isEmpty()) {
            subtitle += QStringLiteral(" · ");
            if (!m_model->vendor.isEmpty()) {
                subtitle += m_model->vendor + QStringLiteral(" ");
            }
            subtitle += m_model->model;
        }
    }
    f.setPointSizeF(8.0);
    f.setWeight(QFont::Normal);
    p->setFont(f);
    p->setPen(connectorAccent());
    p->drawText(QRectF(12.0, 32.0, m_width - 24.0, 18.0),
                Qt::AlignLeft | Qt::AlignVCenter, subtitle);
}

// ═══════════════════════════════════════════════════════════════════════════
// ShankItem
// ═══════════════════════════════════════════════════════════════════════════

ShankItem::ShankItem(ProbeShank* model, QGraphicsItem* parent)
    : QGraphicsPolygonItem(parent)
    , m_model(model)
{
    setFlag(QGraphicsItem::ItemIsSelectable,       true);
    setFlag(QGraphicsItem::ItemIsMovable,          true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setData(RoleItemKind, QStringLiteral("shank"));
    if (model) {
        setData(RoleObjectId, model->id);
        setData(RoleModelPtr, QVariant::fromValue(static_cast<void*>(model)));
    }
    setZValue(5.0);
    setPen(Qt::NoPen);     // we paint our own
    setBrush(Qt::NoBrush);
    rebuildPolygon();
}

void ShankItem::refreshFromModel()
{
    rebuildPolygon();
    update();
}

void ShankItem::setLogicalMode(bool on)
{
    if (m_logicalMode == on) return;
    m_logicalMode = on;
    rebuildPolygon();
    update();
}

void ShankItem::rebuildPolygon()
{
    QPolygonF poly;
    if (m_logicalMode || !m_model) {
        // Logical view: 200×56 rounded rect, identical to the
        // Pipeline Designer's node footprint.  Origin at top-left
        // because logical-view positions are arbitrary anyway.
        poly << QPointF(0.0,    0.0)
             << QPointF(200.0,  0.0)
             << QPointF(200.0, 56.0)
             << QPointF(0.0,   56.0);
    } else {
        // Physical view: rect body (width × length) with a triangular
        // tip wedge at the bottom (y = lengthUm).  The polygon is
        // centred on x=0 — i.e. the local origin is at the top of the
        // shank's CENTRELINE — so that a child ChannelItem with
        // posUm.x() == 0 lands on the centreline.  Sites at ±halfW
        // sit on the edges.  This matches the canonical .probe file
        // convention where each shank's geometry is referenced to its
        // own centreline (e.g. Buzsaki sites at x=±11 µm).
        //
        // Tip half-angle is (180° − tipAngle) / 2; at 90° the tip is
        // flat (no wedge).  Lower angles produce sharp points.
        const qreal w     = m_model->widthUm;
        const qreal h     = m_model->lengthUm;
        const qreal halfW = w * 0.5;
        const qreal tipDeg = m_model->tipAngle;
        const qreal halfApex = (180.0 - tipDeg) * 0.5 * M_PI / 180.0;
        const qreal wedge = std::tan(halfApex) * halfW;
        poly << QPointF(-halfW, 0.0)
             << QPointF( halfW, 0.0)
             << QPointF( halfW, h)
             << QPointF( 0.0,   h + wedge)
             << QPointF(-halfW, h);
    }
    setPolygon(poly);
}

void ShankItem::paint(QPainter* p,
                      const QStyleOptionGraphicsItem* opt,
                      QWidget* /*widget*/)
{
    p->setRenderHint(QPainter::Antialiasing);

    const bool selected = (opt && (opt->state & QStyle::State_Selected));
    p->setBrush(shankBg());
    p->setPen(QPen(selected ? selectionStroke() : shankAccent().darker(160),
                   selected ? 2.0 : 0.5));

    if (m_logicalMode) {
        p->drawRoundedRect(boundingRect(), 8.0, 8.0);
        // Header stripe
        QPainterPath stripe;
        stripe.addRect(QRectF(0.0, 0.0, 200.0, 6.0));
        p->setPen(Qt::NoPen);
        p->setBrush(shankAccent());
        p->drawPath(stripe);
        // Label
        if (m_model) {
            QFont f = p->font();
            f.setPointSizeF(10.0);
            f.setWeight(QFont::Bold);
            p->setFont(f);
            p->setPen(shankAccent().lighter(130));
            p->drawText(QRectF(12.0, 10.0, 176.0, 22.0),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        m_model->label.isEmpty() ? m_model->id
                                                 : m_model->label);
            f.setPointSizeF(8.0);
            f.setWeight(QFont::Normal);
            p->setFont(f);
            p->setPen(shankAccent());
            const QString sub =
                QStringLiteral("%1 channels · %2 µm")
                    .arg(m_model->channels.size())
                    .arg(static_cast<int>(m_model->lengthUm));
            p->drawText(QRectF(12.0, 32.0, 176.0, 18.0),
                        Qt::AlignLeft | Qt::AlignVCenter, sub);
        }
    } else {
        // Physical view: just the silhouette polygon, no labels.
        p->drawPolygon(polygon());
    }
}

QVariant ShankItem::itemChange(GraphicsItemChange change,
                               const QVariant& value)
{
    if (change == ItemPositionHasChanged && m_model && !m_logicalMode) {
        // User dragged the shank; mirror the new position into the
        // model's originUm.  scene() emits selectionChanged signals
        // separately; the page connects to those for modelChanged.
        const QPointF p = value.toPointF();
        m_model->originUm = p;
    }
    return QGraphicsPolygonItem::itemChange(change, value);
}

// ═══════════════════════════════════════════════════════════════════════════
// ChannelItem
// ═══════════════════════════════════════════════════════════════════════════

ChannelItem::ChannelItem(ProbeChannel* model, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-Radius, -Radius, 2.0 * Radius, 2.0 * Radius, parent)
    , m_model(model)
{
    setFlag(QGraphicsItem::ItemIsSelectable,         true);
    setFlag(QGraphicsItem::ItemIsMovable,            true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setData(RoleItemKind, QStringLiteral("channel"));
    if (model) {
        setData(RoleObjectId, QString::number(model->hardwareId));
        setData(RoleModelPtr, QVariant::fromValue(static_cast<void*>(model)));
        setPos(model->posUm);
    }
    setZValue(20.0);                  // pads always above the shank silhouette
    setBrush(channelFill());
    setPen(QPen(channelStroke(), 0.5));
}

void ChannelItem::refreshFromModel()
{
    if (!m_model) return;
    setPos(m_model->posUm);
    update();
}

void ChannelItem::paint(QPainter* p,
                        const QStyleOptionGraphicsItem* opt,
                        QWidget* /*widget*/)
{
    p->setRenderHint(QPainter::Antialiasing);

    const bool selected = (opt && (opt->state & QStyle::State_Selected));
    p->setBrush(channelFill());
    p->setPen(QPen(selected ? selectionStroke() : channelStroke(),
                   selected ? 1.5 : 0.5));
    p->drawEllipse(rect());

    // Disabled overlay — semi-transparent black wash on the pad.
    if (m_model && !m_model->enabled) {
        p->setBrush(QColor(0, 0, 0, 100));
        p->setPen(Qt::NoPen);
        p->drawEllipse(rect());
    }
}

QVariant ChannelItem::itemChange(GraphicsItemChange change,
                                 const QVariant& value)
{
    if (change == ItemPositionHasChanged && m_model) {
        // Mirror dragged position into the model.  Note pos() is in the
        // shank's local coordinate frame thanks to item parenting,
        // which is exactly the µm system the model uses.
        m_model->posUm = value.toPointF();
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

}  // namespace probemaker
