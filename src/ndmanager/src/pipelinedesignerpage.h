/***************************************************************************
 * pipelinedesignerpage.h
 *
 * Node-based graphical pipeline designer for ndmanager.
 * Appears as the "Pipeline" entry in the parameter tree, alongside Plugins.
 *
 * Architecture
 * ────────────
 *   PipelineDesignerPage (QWidget)
 *   ├─ toolbar: preset combo, apply, clear
 *   ├─ QSplitter
 *   │   ├─ PaletteSidebar  — QListWidget of all ndm_ scripts, drag-enabled
 *   │   ├─ PipelineCanvas  — custom QPainter widget; nodes + bezier edges
 *   │   └─ InspectorPanel  — QFormLayout parameter editor for selected node
 *   └─ (Apply sends QList<ProgramInformation> back to ParameterView)
 *
 * copyright  (C) 2026 Gravios / NeuroSuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <QWidget>
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <QVector>
#include <QMap>
#include <QList>
#include <QPair>
#include <QString>

#include <klustersshared/programinformation.h>

// ── Forward declarations ─────────────────────────────────────────────────────
class QListWidget;
class QListWidgetItem;
class QScrollArea;
class QFormLayout;
class QLabel;
class QCheckBox;
class QLineEdit;
class QComboBox;
class QPushButton;
class QSplitter;

// ── Script metadata ──────────────────────────────────────────────────────────

struct NdmParamDef {
    QString name;
    QString defaultValue;
    QString status;       ///< "Mandatory" or "Optional"
};

struct NdmScriptDef {
    QString              type;       ///< "ndm_extractspikes"
    QString              label;      ///< "Extract Spikes"
    QString              brief;      ///< one-line tooltip
    QString              category;   ///< "detection"
    QString              startFlag;  ///< "spikes" / "wideband" / ""
    QVector<NdmParamDef> params;
};

/** Returns the full list of known ndm_ script definitions (static). */
const QVector<NdmScriptDef>& ndmScriptDefs();

/** Returns the definition for @p type, or nullptr if unknown. */
const NdmScriptDef* ndmScriptDef(const QString& type);

// ── Category visual style ─────────────────────────────────────────────────────

struct CategoryStyle {
    QString label;
    QColor  bg;      ///< node body fill
    QColor  accent;  ///< header stripe, port, border when selected
};

const QMap<QString, CategoryStyle>& categoryStyles();

// ── Pipeline data model ────────────────────────────────────────────────────────

struct PipelineNode {
    QString                    id;
    QString                    type;         ///< ndm_ program name
    QPointF                    pos;          ///< world coords (top-left)
    bool                       enabled = true;
    /** name → value, ordered. Index in vector = row in ProgramInformation. */
    QVector<QPair<QString,QString>> params;
};

struct PipelineEdge {
    QString from;  ///< source node id
    QString to;    ///< destination node id
};

// ── PipelineCanvas ────────────────────────────────────────────────────────────

/** Custom-painted canvas for the node graph.
 *
 *  Coordinate systems
 *  ──────────────────
 *    World:  m_nodes[i].pos is in world coords.
 *    Screen: screen = world + m_pan.
 *
 *  Interactions
 *  ────────────
 *    Left-drag node body     → move node
 *    Left-drag output port   → draw pending edge; release on input port = connect
 *    Left-click empty space  → deselect
 *    Right-drag / mid-drag   → pan
 *    Left-click edge stripe  → select edge (shown highlighted; Delete removes it)
 *    Delete / Backspace      → remove selected node (and its edges) or edge
 *    Double-click empty      → (no-op; add via palette)
 *    Drop from palette       → create node at drop position
 */
class PipelineCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit PipelineCanvas(QWidget* parent = nullptr);

    // ── Data binding ──────────────────────────────────────────────────────────
    /** Bind to the page's node/edge lists (non-owning). */
    void bind(QList<PipelineNode>* nodes, QList<PipelineEdge>* edges);

    /** Clear and lay out @p types as a horizontal chain. */
    void loadPreset(const QVector<QString>& types);

    void clearAll();
    void deleteSelected();

    QString selectedNodeId()  const { return m_selNodeId; }
    QString selectedEdgeKey() const { return m_selEdgeKey; }

signals:
    void nodeSelected(const QString& id);
    void selectionCleared();
    void graphModified();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void wheelEvent(QWheelEvent*) override;

public:
    // Node geometry — public so PipelineDesignerPage can use them
    static constexpr float NW    = 200.f;
    static constexpr float NH    = 56.f;
    static constexpr float PR    = 6.f;
    static constexpr float CR    = 8.f;   // corner radius
    static constexpr float HBAR  = 4.f;   // header stripe height

    /** Allocate the next unique node id (e.g. "n42"). */
    QString allocateId() { return QStringLiteral("n%1").arg(++m_idSeq); }

    /** Programmatically select a node by id and update the display. */
    void selectNode(const QString& id) {
        m_selNodeId  = id;
        m_selEdgeKey.clear();
        update();
        emit nodeSelected(id);
    }

private:

    QList<PipelineNode>* m_nodes = nullptr;
    QList<PipelineEdge>* m_edges = nullptr;

    QString m_selNodeId;
    QString m_selEdgeKey;   // "fromId→toId"

    // drag state
    QString  m_dragId;
    QPointF  m_dragOffset;
    bool     m_panActive = false;
    QPointF  m_panStart;
    QPointF  m_pan = {40, 40};

    // connection state
    bool    m_connecting = false;
    QString m_connectFrom;
    QPointF m_connectCursor;

    int m_idSeq = 0;
    QString newId() { return QStringLiteral("n%1").arg(++m_idSeq); }

    // coord helpers
    QPointF w2s(const QPointF& w) const { return w + m_pan; }
    QPointF s2w(const QPointF& s) const { return s - m_pan; }

    QRectF  nodeRect(const PipelineNode& n) const;
    QPointF outPort(const PipelineNode& n)  const;
    QPointF inPort(const PipelineNode& n)   const;

    // hit testing
    PipelineNode* nodeAt(const QPointF& world);
    bool          hitOut(const PipelineNode& n, const QPointF& w) const;
    bool          hitIn(const PipelineNode& n,  const QPointF& w) const;
    QString       edgeAt(const QPointF& screen) const;   // returns "from→to" or ""

    // drawing
    void drawGrid(QPainter& p);
    void drawEdge(QPainter& p, const PipelineEdge& e, bool selected);
    void drawNode(QPainter& p, const PipelineNode& n);
    void drawPending(QPainter& p);

    // edge bezier path in screen coords
    QPainterPath edgePath(const QPointF& p1, const QPointF& p2) const;
};

// ── PipelineDesignerPage ──────────────────────────────────────────────────────

class PipelineDesignerPage : public QWidget
{
    Q_OBJECT
public:
    explicit PipelineDesignerPage(QWidget* parent = nullptr);

    /** Populate the canvas from a programs list (called by initialize()). */
    void setPrograms(const QList<ProgramInformation>& programs);

    /** Read current graph back as an ordered ProgramInformation list.
     *  Emits nothing — purely a data accessor. */
    QList<ProgramInformation> getPrograms() const;

    bool isModified() const { return m_modified; }

signals:
    /** Emitted when "Apply Pipeline" is clicked.
     *  ParameterView connects this to setProgramList(). */
    void applyRequested(const QList<ProgramInformation>& programs);

    void graphModified();

private slots:
    void onNodeSelected(const QString& id);
    void onSelectionCleared();
    void onGraphModified();
    void onParamEdited();
    void onEnabledToggled(bool);
    void onApply();
    void onClear();
    void onPresetChanged(int index);
    void onPaletteDoubleClicked(QListWidgetItem* item);

private:
    void buildPalette();
    void buildInspector();
    void populateInspector(const QString& id);
    void clearInspector();
    void refreshApplyState();

    // data
    QList<PipelineNode> m_nodes;
    QList<PipelineEdge> m_edges;
    bool                m_modified = false;

    // canvas
    PipelineCanvas* m_canvas;

    // palette
    QListWidget* m_palette;

    // inspector widgets
    QScrollArea* m_inspScroll;
    QWidget*     m_inspWidget;
    QLabel*      m_inspTitle;
    QCheckBox*   m_inspEnabled;
    QWidget*     m_inspParamArea;
    QFormLayout* m_inspForm;
    /** Parallel to the selected node's params: one QLineEdit per row. */
    QVector<QLineEdit*> m_paramEdits;

    // toolbar
    QComboBox*   m_presetCombo;
    QPushButton* m_applyBtn;

    QString m_inspectedId;
};
