/***************************************************************************
 * probemakeritems.h
 *
 * QGraphicsItem subclasses used by the Probe Maker page.  These are the
 * visual representations of the probe data model:
 *
 *   ConnectorItem     — gold "★ ROOT" header bar at the top of the scene
 *   ShankItem         — polygon silhouette (rect body + tip wedge);
 *                       carries channels as QGraphicsItem children, so
 *                       moving the shank moves them automatically
 *   ChannelItem       — small filled circle representing a recording site
 *
 * Each item carries a back-reference to its data-model sibling
 * (ProbeConnector / ProbeShank / ProbeChannel) so edits in the view
 * mutate the model in place.  The page emits modelChanged() afterwards.
 *
 * The two QGraphicsView subclasses (ProbeLogicalView, ProbePhysicalView)
 * share these items but render them differently — the logical view
 * arranges them in tree layout, the physical view places them at their
 * actual µm coordinates.  We keep the items "pure" (no per-view logic)
 * and let each view set positions via setPos() when populating its scene.
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

// Forward-declare the data-model types — defined in
// libklustersshared/.../parameteryamlreader_probes.h.
struct ProbeConnector;
struct ProbeShank;
struct ProbeChannel;

namespace probemaker {

/** QGraphicsItem::data() role for back-references to the data model. */
enum DataRole {
    RoleItemKind   = 0,   ///< QString: "connector" / "shank" / "channel"
    RoleObjectId   = 1,   ///< QString: shank id, or channel hardwareId as string
    RoleModelPtr   = 2    ///< QVariant::fromValue(void*): pointer into the
                          ///< owning ProbeConnector.  Caller owns lifetime;
                          ///< the item never deletes through this pointer.
};

/** Item kinds — used for type() and for filtering selection. */
constexpr int ItemTypeConnector = QGraphicsItem::UserType + 1;
constexpr int ItemTypeShank     = QGraphicsItem::UserType + 2;
constexpr int ItemTypeChannel   = QGraphicsItem::UserType + 3;

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
 *  In the physical view the polygon is drawn at the shank's actual
 *  micron dimensions (lengthUm × widthUm), with channels as direct
 *  children.  In the logical view the polygon is replaced by a
 *  uniform-size rectangle; channels still parent to it.
 *
 *  Movable in the physical view (drag to reposition relative to the
 *  connector); fixed in the logical view (laid out by the view). */
class ShankItem : public QGraphicsPolygonItem
{
public:
    explicit ShankItem(ProbeShank* model,
                       QGraphicsItem* parent = nullptr);

    int type() const override { return ItemTypeShank; }

    /** Re-read shape from the model — call after the user edits length,
     *  width, or tip angle in the inspector. */
    void refreshFromModel();

    /** Logical-view rendering mode: replace the silhouette polygon with
     *  a uniform 200×56 rounded rect.  Set false to draw the actual
     *  micron-dimensioned silhouette (physical view). */
    void setLogicalMode(bool on);

protected:
    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* opt,
               QWidget* widget) override;

    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    /** Build the silhouette polygon from the model's lengthUm /
     *  widthUm / tipAngle.  Called by the constructor and by
     *  refreshFromModel().  Tip wedge sits at the bottom of the
     *  polygon (y = lengthUm); the head sits at y = 0. */
    void rebuildPolygon();

    ProbeShank* m_model;        ///< non-owning
    bool        m_logicalMode = false;
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

}  // namespace probemaker
