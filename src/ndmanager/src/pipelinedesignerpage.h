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
 *   ├─ notice bar: load failures / warnings, refused edges
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
#include <QPainterPath>

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
 *    World:  nodes[i].pos is in world coords.
 *    Screen: screen = world + pan.
 *
 *  Interactions
 *  ────────────
 *    Left-drag node body     → move node
 *    Left-drag output port   → draw pending edge; release on input port = connect
 *                              (refused, with edgeRejected, into the root or
 *                              when it would close a cycle)
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

    void deleteSelected();

    /** Drop every selection / drag / connection / pan-in-progress state.
     *  Called whenever the bound node list is replaced wholesale (load,
     *  setPrograms, clearGraphToRoot), so no id from the previous graph
     *  survives to be deleted, dragged or connected from. */
    void resetInteraction();

    /** Advance the id counter past every "n<digits>" id in the bound node
     *  list, so ids kept from a loaded file never collide with freshly
     *  allocated ones. */
    void reserveIds();

    QString selectedNodeId()  const { return selNodeId; }
    QString selectedEdgeKey() const { return selEdgeKey; }

signals:
    void nodeSelected(const QString& id);
    void selectionCleared();
    void graphModified();
    /** An edge drag was refused; @p reason is user-facing. */
    void edgeRejected(const QString& reason);

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

    /** Allocate the next unique node id (e.g. "n42").  Never returns an
     *  id already present in the bound node list. */
    QString allocateId();

    /** Programmatically select a node by id and update the display. */
    void selectNode(const QString& id) {
        selNodeId  = id;
        selEdgeKey.clear();
        update();
        emit nodeSelected(id);
    }

private:

    QList<PipelineNode>* nodes = nullptr;
    QList<PipelineEdge>* edges = nullptr;

    QString selNodeId;
    QString selEdgeKey;   // "fromId→toId"

    // drag state
    QString  dragId;
    QPointF  dragOffset;
    QPointF  dragStartPos;
    bool     panActive = false;
    QPointF  panStart;
    QPointF  pan = {40, 40};

    // connection state
    bool    connecting = false;
    QString connectFrom;
    QPointF connectCursor;

    int idSeq = 0;
    QString newId() { return allocateId(); }
    bool hasNodeId(const QString& id) const;

    /** True when adding from→to would close a cycle: @p to already
     *  reaches @p from (or they are the same node). */
    bool wouldCreateCycle(const QString& from, const QString& to) const;

    // coord helpers
    QPointF w2s(const QPointF& w) const { return w + pan; }
    QPointF s2w(const QPointF& s) const { return s - pan; }

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

    /** Empty-out the graph and synthesise a single ndm_start root node.
     *  Used when no pipeline file exists for the current session — the
     *  user starts from a clean slate with just the orchestrator entry,
     *  not a graph pre-populated from the YAML's parameter-only
     *  programs: block. */
    void clearGraphToRoot();

    /** Serialise the current graph to a YAML pipeline file at @p path.
     *  Schema: { nodes: [{id, type, pos, enabled, params{}}],
     *            edges: [{from, to}] }.
     *  Returns true on success.  On failure, @p error (if non-null) is
     *  populated with a human-readable description and false is returned. */
    bool savePipelineFile(const QString& path, QString* error = nullptr);

    /** Replace the current graph with the contents of a pipeline file.
     *  Enforces the sticky-root rules: exactly one ndm_start (synthesised
     *  if absent, extra ones dropped), no edge into it.  Also drops
     *  unknown node types and cycle-closing edges and renames duplicate
     *  ids.  Every repair is described in @p error (a warning: the call
     *  still returns true).  Node order is kept as in the file — it is
     *  the order ndm_start's topological sort breaks ties in.
     *  On failure the graph is untouched, @p error says why (python3 not
     *  starting, a timeout and a parse error are told apart) and the
     *  path is remembered as failedLoadPath(). */
    bool loadPipelineFile(const QString& path, QString* error = nullptr);

    /** Absolute path of the last pipeline file that failed to load, or
     *  empty.  Cleared by a successful load, and by a successful save to
     *  that same path.  ParameterView::savePipelineDefault asks before
     *  overwriting it: the graph on screen is not what that file holds. */
    QString failedLoadPath() const { return lastFailedLoad; }

    /** Read current graph back as an ordered ProgramInformation list.
     *  Emits nothing — purely a data accessor. */
    QList<ProgramInformation> getPrograms() const;

    /** True when the graph has edits not yet written to a pipeline file:
     *  set by any edit, cleared by a successful savePipelineFile and by
     *  replacing the graph (load, setPrograms, clearGraphToRoot).  Apply
     *  does not clear it — Apply does not write the file ndm_start reads. */
    bool isModified() const { return unsaved; }

    /** Show @p text in the notice bar under the toolbar (red when
     *  @p isError).  Empty text hides the bar.  With @p timeoutMs > 0 the
     *  notice hides itself after that long unless replaced meanwhile. */
    void showNotice(const QString& text, bool isError = false, int timeoutMs = 0);

signals:
    /** Emitted when "Apply Pipeline" is clicked.
     *  ParameterView connects this to setProgramList(). */
    void applyRequested(const QList<ProgramInformation>& programs);

    void graphModified();

    /** Emitted when the user clicks the toolbar Save button (or hits the
     *  Ctrl+Alt+P shortcut routed through ndManager).  ParameterView
     *  resolves the session base path and writes
     *  <session>.default.pipeline. */
    void savePipelineRequested();

    /** Emitted when the user clicks Save As… (Ctrl+Alt+Shift+P).
     *  ParameterView prompts for a name and writes
     *  <session>.<n>.pipeline. */
    void saveAsPipelineRequested();

private slots:
    void onNodeSelected(const QString& id);
    void onSelectionCleared();
    void onGraphModified();
    void onParamEdited();
    void onEnabledToggled(bool);
    void onApply();
    void onClear();
    void onEdgeRejected(const QString& reason);
    void onPresetChanged(int index);
    void onPaletteDoubleClicked(QListWidgetItem* item);

private:
    void buildPalette();
    void buildInspector();
    bool readPipelineFile(const QString& path, QString* error);
    void populateInspector(const QString& id);
    void clearInspector();
    void refreshApplyState();

    // data
    QList<PipelineNode> nodes;
    QList<PipelineEdge> edges;
    /** Graph edited since the last Apply (drives the Apply button). */
    bool                applyPending = false;
    /** Graph edited since the last pipeline-file load / save. */
    bool                unsaved = false;
    QString             lastFailedLoad;

    // canvas
    PipelineCanvas* canvas;

    // palette
    QListWidget* palette;

    // inspector widgets
    QScrollArea* inspScroll;
    QWidget*     inspWidget;
    QLabel*      inspTitle;
    QCheckBox*   inspEnabled;
    QWidget*     inspParamArea;
    QFormLayout* inspForm;
    /** Parallel to the selected node's params: one QLineEdit per row. */
    QVector<QLineEdit*> paramEdits;

    // toolbar
    QComboBox*   presetCombo;
    QPushButton* applyBtn;
    QLabel*      noticeLabel = nullptr;

    QString inspectedId;
};
