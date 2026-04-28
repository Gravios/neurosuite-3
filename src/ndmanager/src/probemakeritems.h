/***************************************************************************
 * probemakeritems.h
 *
 * QGraphicsItem subclasses used by the Probe Maker page.  The
 * physical view uses ConnectorItem / ShankItem / ChannelItem to
 * render the actual probe geometry at micron scale; the logical view
 * uses LogicalNodeItem + LogicalEdgeItem as a draggable DAG editor
 * for the connector → shanks → channels tree.
 *
 * Each item carries a back-reference to its data-model sibling
 * (ProbeConnector / ProbeShank / ProbeChannel) so edits in the view
 * mutate the model in place.  The page emits modified() afterwards
 * to flag the document as dirty.
 *
 * The two QGraphicsView subclasses (ProbeLogicalView, ProbePhysicalView)
 * own these items.  Items in the physical view are positioned in
 * micron coords (with channels parented to their owning shank for
 * cheap drag-the-shank-and-channels-follow behaviour); items in the
 * logical view are positioned in arbitrary scene coords with their
 * positions persisted across rebuilds via the page's m_logicalState
 * cache (see probemakerpage.h).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QPainter>
#include <QPolygonF>
#include <QSet>

// Forward-declare the data-model types — defined in
// libklustersshared/.../parameteryamlreader_probes.h.
struct ProbeConnector;
struct ProbeShank;
struct ProbeChannel;

namespace probemaker {

/** QGraphicsItem::data() role for back-references to the data model. */
enum DataRole {
    RoleItemKind   = 0,   ///< QString: "connector" / "shank" / "channel"
    RoleObjectId   = 1,   ///< QString: shank id, or channel hardwareId as string;
                          ///< used for cross-scene selection mirroring (must
                          ///< match the value set by the physical-scene items).
    RoleModelPtr   = 2,   ///< QVariant::fromValue(void*): pointer into the
                          ///< owning ProbeConnector.  Caller owns lifetime;
                          ///< the item never deletes through this pointer.
                          ///< NOT stable across QList reallocation — see
                          ///< RoleLogicalKey for a position-cache key that is.
    RoleLogicalKey = 3    ///< QString: composite stable key used by the
                          ///< logical-graph view's position cache.  Format:
                          ///<   "C"           — connector
                          ///<   "S:<shankId>" — shank
                          ///<   "H:<shankId>:<hwId>" — channel
                          ///< Set only on LogicalNodeItem; resilient to
                          ///< QList reallocation that invalidates RoleModelPtr.
};

/** Item kinds — used for type() and for filtering selection. */
constexpr int ItemTypeConnector   = QGraphicsItem::UserType + 1;
constexpr int ItemTypeShank       = QGraphicsItem::UserType + 2;
constexpr int ItemTypeChannel     = QGraphicsItem::UserType + 3;
constexpr int ItemTypeLogicalNode = QGraphicsItem::UserType + 4;
constexpr int ItemTypeLogicalEdge = QGraphicsItem::UserType + 5;

// ─────────────────────────────────────────────────────────────────────────────
// ConnectorItem
// ─────────────────────────────────────────────────────────────────────────────

/** Gold "★ ROOT" header drawn at the top of either view's scene.
 *
 *  The connector item is non-movable and non-deletable — it represents
 *  the device itself.  Width is fixed at construction; height is fixed
 *  at NodeHeight.  Position is set by the owning view (logical view
 *  pins it at scene-origin, physical view positions it above the
 *  shank silhouettes). */
class ConnectorItem : public QGraphicsRectItem
{
public:
    explicit ConnectorItem(ProbeConnector* model,
                           qreal width = 240.0,
                           QGraphicsItem* parent = nullptr);

    int type() const override { return ItemTypeConnector; }

    /** Width in scene units; height is fixed at NodeHeight. */
    static constexpr qreal NodeHeight = 56.0;

    /** Re-read label/subtitle text from the data model.  Called by the
     *  page after any edit that might affect the connector's display
     *  (rename, channel-count change, etc.). */
    void refreshFromModel();

protected:
    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* opt,
               QWidget* widget) override;

private:
    ProbeConnector* m_model;   ///< non-owning, lifetime managed by the page
    qreal           m_width;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShankItem
// ─────────────────────────────────────────────────────────────────────────────

/** Shank silhouette — a rectangle with a triangular tip wedge.
 *
 *  Used by the physical view; the logical view uses LogicalNodeItem
 *  for its shank representation (see below).  The polygon is drawn
 *  at the shank's actual micron dimensions (lengthUm × widthUm) with
 *  channels as direct children.  Movable so the user can drag a
 *  shank to reposition it relative to the connector. */
class ShankItem : public QGraphicsPolygonItem
{
public:
    explicit ShankItem(ProbeShank* model,
                       QGraphicsItem* parent = nullptr);

    int type() const override { return ItemTypeShank; }

    /** Re-read shape from the model — call after the user edits length,
     *  width, or tip angle in the inspector. */
    void refreshFromModel();

protected:
    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* opt,
               QWidget* widget) override;

    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    /** Build the silhouette polygon from the model's lengthUm /
     *  widthUm / tipAngle.  The polygon is centred on local x=0 (the
     *  shank's centreline) so a child ChannelItem with posUm.x()=0
     *  lands on the centreline.  Tip wedge sits at y = lengthUm; head
     *  sits at y = 0. */
    void rebuildPolygon();

    ProbeShank* m_model;        ///< non-owning
};

// ─────────────────────────────────────────────────────────────────────────────
// ChannelItem
// ─────────────────────────────────────────────────────────────────────────────

/** Single recording site — a filled circle at the channel's micron
 *  coordinates relative to its parent shank.  Item parenting is what
 *  makes "select shank → moves channels" work; never re-parent at
 *  runtime, only when the channel is reassigned to a different shank
 *  (which is a model-level edit, not a view-level one). */
class ChannelItem : public QGraphicsEllipseItem
{
public:
    explicit ChannelItem(ProbeChannel* model,
                         QGraphicsItem* parent = nullptr);

    int type() const override { return ItemTypeChannel; }

    /** Re-read position and area from the model. */
    void refreshFromModel();

    /** Pad radius in scene units (independent of model area; the model's
     *  area_um² is for export-only display and doesn't drive the
     *  visual size — pads remain readable at any zoom). */
    static constexpr qreal Radius = 5.0;

protected:
    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* opt,
               QWidget* widget) override;

    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    ProbeChannel* m_model;      ///< non-owning
};

// ─────────────────────────────────────────────────────────────────────────────
// LogicalNodeItem  /  LogicalEdgeItem
// ─────────────────────────────────────────────────────────────────────────────
//
// The logical-view scene used to be a static auto-laid-out diagram —
// every rebuild called setPos() with computed coordinates and the user
// couldn't drag anything; the connector→shank and shank→channel edges
// were drawn as fixed paths between those static positions.  These two
// classes turn it into a real DAG editor:
//
//   * LogicalNodeItem  — draggable, selectable rectangle that can act
//                        as either a connector header, a shank, or a
//                        channel.  Holds a back-reference to the model
//                        item it represents and a registry of edges
//                        so it can notify them whenever it moves.
//
//   * LogicalEdgeItem  — a polyline between two LogicalNodeItem
//                        endpoints.  Endpoints are pointers, not
//                        baked-in positions; recalcGeometry() refreshes
//                        the path from the current endpoint scene
//                        positions.  Nodes call this on every movement.
//
// Positions persist across rebuilds via a QHash<void*, QPointF> on the
// page (keyed by the model pointer).  First time an item is laid out
// the page assigns an auto-layout position; user drags update the hash;
// subsequent rebuilds restore the stored position rather than
// re-running the auto-layout for that item.

class LogicalEdgeItem;

/** Logical-view node — connector / shank / channel as a rectangle that
 *  the user can drag and select.  Edges register with their endpoints
 *  here so the node can call recalcGeometry() on each one whenever it
 *  moves, keeping the lines glued to the box. */
class LogicalNodeItem : public QGraphicsItem
{
public:
    enum class Kind { Connector, Shank, Channel };

    LogicalNodeItem(Kind          kind,
                    void*         modelPtr,
                    const QString& label,
                    QSizeF        size,
                    QGraphicsItem* parent = nullptr);

    /** Notifies all attached edges that this endpoint is going away,
     *  so they can null out their pointer and skip a UAF on their
     *  own destructor.  This matters because QGraphicsScene::clear()
     *  destroys items in arbitrary order — without this, an edge
     *  destroyed AFTER one of its endpoints would call removeEdge()
     *  on freed memory. */
    ~LogicalNodeItem() override;

    /** Register an edge to be redrawn when this node moves.  Both
     *  endpoints typically register the same edge; either can drive
     *  recalcGeometry() on every movement. */
    void addEdge(LogicalEdgeItem* edge);
    void removeEdge(LogicalEdgeItem* edge);

    /** Scene-coordinate centre of this node's top edge — used as the
     *  attachment point for an incoming edge from the parent. */
    QPointF topAnchor() const;
    /** Scene-coordinate centre of the bottom edge — attachment for
     *  outgoing edges to children. */
    QPointF bottomAnchor() const;

    Kind  kind()     const { return m_kind; }
    void* modelPtr() const { return m_modelPtr; }

    int type() const override;
    QRectF boundingRect() const override;
    void   paint(QPainter* p,
                 const QStyleOptionGraphicsItem* opt,
                 QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    Kind     m_kind;
    void*    m_modelPtr;       ///< back-reference; not owning
    QString  m_label;
    QSizeF   m_size;
    QSet<LogicalEdgeItem*> m_edges;
};

/** Polyline between two LogicalNodeItem endpoints.  Endpoints are
 *  stored by pointer; the edge subscribes to both via addEdge() so it
 *  receives a recalcGeometry() callback whenever either moves.
 *
 *  The path is a 4-point polyline: nodeA-anchor → nodeA-stub →
 *  nodeB-stub → nodeB-anchor, with a small vertical offset on each
 *  stub.  This visually emphasises the attachment points and avoids
 *  a single straight line that can ambiguously cross node bodies. */
class LogicalEdgeItem : public QGraphicsItem
{
public:
    LogicalEdgeItem(LogicalNodeItem* src, LogicalNodeItem* dst);
    ~LogicalEdgeItem() override;

    /** Recompute the polyline from the current scene positions of
     *  src and dst.  Calls prepareGeometryChange() so QGraphicsScene
     *  invalidates the old bounding rect.  Cheap (4 QPointF ops). */
    void recalcGeometry();

    /** Called by a LogicalNodeItem from its destructor when this edge
     *  was registered with that endpoint.  Nulls the matching pointer
     *  so the edge's own destructor doesn't dereference freed memory.
     *  Order of destruction in QGraphicsScene::clear() is arbitrary,
     *  so either endpoint or this edge may be destroyed first; this
     *  callback handles the case where the endpoint dies first. */
    void endpointDetached(LogicalNodeItem* who);

    int type() const override;
    QRectF boundingRect() const override;
    void   paint(QPainter* p,
                 const QStyleOptionGraphicsItem* opt,
                 QWidget* widget) override;

private:
    LogicalNodeItem* m_src;
    LogicalNodeItem* m_dst;
    QPolygonF        m_path;       ///< 4 points, scene coords
    QRectF           m_bounding;   ///< cached bounding rect for boundingRect()
};

}  // namespace probemaker
