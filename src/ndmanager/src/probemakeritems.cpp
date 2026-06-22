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
    , model(model)
    , width(width)
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
    const QRectF r(0.0, 0.0, width, NodeHeight);
    const bool   selected = (opt && (opt->state & QStyle::State_Selected));

    // Body
    p->setRenderHint(QPainter::Antialiasing);
    p->setBrush(connectorBg());
    p->setPen(QPen(selected ? selectionStroke() : connectorAccent().darker(160),
                   selected ? 2.0 : 0.5));
    p->drawRoundedRect(r, 8.0, 8.0);

    // Header stripe
    QPainterPath stripe;
    stripe.addRect(QRectF(0.0, 0.0, width, 6.0));
    p->setPen(Qt::NoPen);
    p->setBrush(connectorAccent());
    p->drawPath(stripe);

    // Title + subtitle
    QFont f = p->font();
    f.setPointSizeF(10.0);
    f.setWeight(QFont::Bold);
    p->setFont(f);
    p->setPen(connectorAccent().lighter(130));

    // Title shows the probe's model name when set, falling back to
    // a generic "Connector ★ ROOT" placeholder.  model is non-owning
    // and is set at construction; it can only be null in the unlikely
    // case that ConnectorItem is constructed without a model (which
    // we don't currently do, but defensive programming is cheap).
    QString title = QStringLiteral("Connector ★ ROOT");
    if (model && !model->model.isEmpty()) {
        title = model->model;
        if (!model->vendor.isEmpty())
            title = model->vendor + QStringLiteral(" · ") + title;
    }
    p->drawText(QRectF(12.0, 10.0, width - 24.0, 22.0),
                Qt::AlignLeft | Qt::AlignVCenter, title);

    // Subtitle: total channel count.
    QString subtitle;
    if (model)
        subtitle = QStringLiteral("%1 channels").arg(model->totalChannels);
    f.setPointSizeF(8.0);
    f.setWeight(QFont::Normal);
    p->setFont(f);
    p->setPen(connectorAccent());
    p->drawText(QRectF(12.0, 32.0, width - 24.0, 18.0),
                Qt::AlignLeft | Qt::AlignVCenter, subtitle);
}

// ═══════════════════════════════════════════════════════════════════════════
// ShankItem
// ═══════════════════════════════════════════════════════════════════════════

ShankItem::ShankItem(ProbeShank* model, QGraphicsItem* parent)
    : QGraphicsPolygonItem(parent)
    , model(model)
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

void ShankItem::rebuildPolygon()
{
    QPolygonF poly;
    if (!model) {
        // Defensive — we never construct a ShankItem without a model
        // in normal flow, but if one slips through, draw a minimal
        // placeholder polygon so the scene doesn't crash on an empty
        // QGraphicsPolygonItem.
        poly << QPointF(0.0, 0.0)
             << QPointF(70.0, 0.0)
             << QPointF(70.0, 1500.0)
             << QPointF(35.0, 1530.0)
             << QPointF(0.0, 1500.0);
        setPolygon(poly);
        return;
    }
    // Rect body (width × length) with a triangular tip wedge at the
    // bottom (y = lengthUm).  The polygon is centred on x=0 — i.e.
    // the local origin is at the top of the shank's CENTRELINE — so
    // that a child ChannelItem with posUm.x() == 0 lands on the
    // centreline.  Sites at ±halfW sit on the edges.  This matches
    // the canonical .probe file convention where each shank's
    // geometry is referenced to its own centreline (e.g. Buzsaki
    // sites at x=±11 µm).
    //
    // Tip half-angle is (180° − tipAngle) / 2; at 90° the tip is
    // flat (no wedge).  Lower angles produce sharp points.
    const qreal w        = model->widthUm;
    const qreal h        = model->lengthUm;
    const qreal halfW    = w * 0.5;
    const qreal tipDeg   = model->tipAngle;
    const qreal halfApex = (180.0 - tipDeg) * 0.5 * M_PI / 180.0;
    const qreal wedge    = std::tan(halfApex) * halfW;
    poly << QPointF(-halfW, 0.0)
         << QPointF( halfW, 0.0)
         << QPointF( halfW, h)
         << QPointF( 0.0,   h + wedge)
         << QPointF(-halfW, h);
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
    // Physical view: just the silhouette polygon.  Labels are shown
    // by the inspector when the shank is selected.
    p->drawPolygon(polygon());
}

QVariant ShankItem::itemChange(GraphicsItemChange change,
                               const QVariant& value)
{
    if (change == ItemPositionHasChanged && model) {
        // User dragged the shank in the physical scene; mirror the new
        // origin into the model so the inspector and serializer see
        // the updated position.
        model->originUm = value.toPointF();
    }
    return QGraphicsPolygonItem::itemChange(change, value);
}

// ═══════════════════════════════════════════════════════════════════════════
// ChannelItem
// ═══════════════════════════════════════════════════════════════════════════

ChannelItem::ChannelItem(ProbeChannel* model, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-Radius, -Radius, 2.0 * Radius, 2.0 * Radius, parent)
    , model(model)
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
    if (!model) return;
    setPos(model->posUm);
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
    if (model && !model->enabled) {
        p->setBrush(QColor(0, 0, 0, 100));
        p->setPen(Qt::NoPen);
        p->drawEllipse(rect());
    }
}

QVariant ChannelItem::itemChange(GraphicsItemChange change,
                                 const QVariant& value)
{
    if (change == ItemPositionHasChanged && model) {
        // Mirror dragged position into the model.  Note pos() is in the
        // shank's local coordinate frame thanks to item parenting,
        // which is exactly the µm system the model uses.
        model->posUm = value.toPointF();
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

// ═══════════════════════════════════════════════════════════════════════════
// LogicalNodeItem
// ═══════════════════════════════════════════════════════════════════════════

LogicalNodeItem::LogicalNodeItem(Kind          kind,
                                  void*         modelPtr,
                                  const QString& label,
                                  QSizeF        size,
                                  QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , kind(kind)
    , modelPtr(modelPtr)
    , label(label)
    , size(size)
{
    setFlag(QGraphicsItem::ItemIsMovable,            true);
    setFlag(QGraphicsItem::ItemIsSelectable,         true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setData(RoleItemKind,
            kind == Kind::Connector ? QStringLiteral("connector")
          : kind == Kind::Shank     ? QStringLiteral("shank")
                                     : QStringLiteral("channel"));
    setData(RoleModelPtr, QVariant::fromValue(modelPtr));
}

LogicalNodeItem::~LogicalNodeItem()
{
    // Notify each registered edge that we're going away.  The edge
    // nulls out whichever of its endpoint pointers we matched, so its
    // own dtor (which may run before or after ours) doesn't call
    // removeEdge() on this freed object.  We deliberately don't call
    // removeEdge() ourselves here — edges is about to be destroyed.
    for (LogicalEdgeItem* edge : edges)
        edge->endpointDetached(this);
}

void LogicalNodeItem::addEdge(LogicalEdgeItem* edge)
{
    if (edge) edges.insert(edge);
}

void LogicalNodeItem::removeEdge(LogicalEdgeItem* edge)
{
    edges.remove(edge);
}

QPointF LogicalNodeItem::topAnchor() const
{
    // The node is drawn from (0,0) to (size.w, size.h) in local coords;
    // top-centre in scene coords is mapToScene(size.w/2, 0).
    return mapToScene(QPointF(size.width() * 0.5, 0.0));
}

QPointF LogicalNodeItem::bottomAnchor() const
{
    return mapToScene(QPointF(size.width() * 0.5, size.height()));
}

int LogicalNodeItem::type() const
{
    return ItemTypeLogicalNode;
}

QRectF LogicalNodeItem::boundingRect() const
{
    // Slightly larger than the visible rect so the selection halo and
    // the focus border don't get clipped at the edge.
    constexpr qreal pad = 1.0;
    return QRectF(-pad, -pad,
                  size.width()  + 2.0 * pad,
                  size.height() + 2.0 * pad);
}

void LogicalNodeItem::paint(QPainter* p,
                             const QStyleOptionGraphicsItem* opt,
                             QWidget* /*widget*/)
{
    p->setRenderHint(QPainter::Antialiasing);
    const bool selected = (opt && (opt->state & QStyle::State_Selected));

    QColor bg, accent, txtCol;
    switch (kind) {
    case Kind::Connector:
        bg = QColor(0x1a, 0x1a, 0x0d);
        accent = QColor(0xfa, 0xc1, 0x5c);
        txtCol = QColor(0xff, 0xd9, 0x7e);
        break;
    case Kind::Shank:
        bg = QColor(0x0a, 0x12, 0x1c);
        accent = QColor(0x4a, 0x9e, 0xff);
        txtCol = QColor(0xbc, 0xd0, 0xe8);
        break;
    case Kind::Channel:
        bg = QColor(0x07, 0x12, 0x22);
        accent = QColor(0x37, 0x8a, 0xdd);
        txtCol = QColor(0x85, 0xb7, 0xeb);
        break;
    }

    const QRectF r(0.0, 0.0, size.width(), size.height());
    p->setBrush(bg);
    p->setPen(QPen(selected ? QColor(0xff, 0xd9, 0x7e) : accent.darker(160),
                   selected ? 2.0 : 0.5));
    if (kind == Kind::Connector || kind == Kind::Shank) {
        p->drawRoundedRect(r, 6.0, 6.0);
        // Top accent stripe (matches the auto-laid-out look).
        p->setPen(Qt::NoPen);
        p->setBrush(accent);
        p->drawRect(QRectF(0.0, 0.0, size.width(), 4.0));
    } else {
        p->drawRoundedRect(r, 3.0, 3.0);
    }

    // Label (centred).
    p->setPen(txtCol);
    QFont f = p->font();
    f.setPointSizeF(kind == Kind::Channel ? 8.0 : 10.0);
    if (kind != Kind::Channel) f.setBold(true);
    p->setFont(f);
    p->drawText(r, Qt::AlignCenter, label);
}

QVariant LogicalNodeItem::itemChange(GraphicsItemChange change,
                                      const QVariant& value)
{
    if (change == ItemPositionHasChanged) {
        // Notify all attached edges so they re-route their polylines
        // to follow this node's new scene position.
        for (LogicalEdgeItem* edge : edges)
            edge->recalcGeometry();
    }
    return QGraphicsItem::itemChange(change, value);
}

// ═══════════════════════════════════════════════════════════════════════════
// LogicalEdgeItem
// ═══════════════════════════════════════════════════════════════════════════

LogicalEdgeItem::LogicalEdgeItem(LogicalNodeItem* src, LogicalNodeItem* dst)
    : src(src)
    , dst(dst)
{
    setZValue(-1.0);   // edges render under nodes
    if (src) src->addEdge(this);
    if (dst) dst->addEdge(this);
    recalcGeometry();
}

LogicalEdgeItem::~LogicalEdgeItem()
{
    // Either pointer may already be null if the corresponding endpoint
    // was destroyed first (it called endpointDetached on us).  Only
    // call removeEdge on the still-live ones.
    if (src) src->removeEdge(this);
    if (dst) dst->removeEdge(this);
}

void LogicalEdgeItem::endpointDetached(LogicalNodeItem* who)
{
    // The endpoint is being destroyed; null our pointer so our own
    // dtor doesn't dereference it.  After detachment we can no longer
    // recompute geometry (recalcGeometry guards against null), but
    // that's fine — we'll be deleted in the same s->clear() pass.
    if (src == who) src = nullptr;
    if (dst == who) dst = nullptr;
}

void LogicalEdgeItem::recalcGeometry()
{
    if (!src || !dst) return;
    prepareGeometryChange();

    const QPointF a = src->bottomAnchor();
    const QPointF b = dst->topAnchor();
    // Stub offsets: emphasise the attachment by extending vertically
    // away from each anchor before bending toward the other endpoint.
    constexpr qreal stub = 12.0;
    const QPointF aStub(a.x(), a.y() + stub);
    const QPointF bStub(b.x(), b.y() - stub);

    path.clear();
    path << a << aStub << bStub << b;

    // Cache bounding rect with a 4 px pad for the pen width.
    bounding = path.boundingRect().adjusted(-4.0, -4.0, 4.0, 4.0);
    update();
}

int LogicalEdgeItem::type() const
{
    return ItemTypeLogicalEdge;
}

QRectF LogicalEdgeItem::boundingRect() const
{
    return bounding;
}

void LogicalEdgeItem::paint(QPainter* p,
                             const QStyleOptionGraphicsItem* /*opt*/,
                             QWidget* /*widget*/)
{
    if (path.size() < 2) return;
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(QColor(0xfa, 0xc1, 0x5c), 1.4));
    p->drawPolyline(path);
}

}  // namespace probemaker
