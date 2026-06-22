/***************************************************************************
 * probemakerpage.h
 *
 * The Probe Maker page — interactive editor for probe geometry,
 * shank layout, and channel mapping.  Lives as a peer to the existing
 * Probes tab in ndmanager's parameter view.
 *
 * Reads and writes the canonical `.probe` YAML schema (the same format
 * used by the library files in src/nphys-data/src/probes/).  Session-
 * local probes are saved as `<session>.ndm.<n>.probe` next to the
 * session YAML.
 *
 * See doc/design/probe-maker.md for the design rationale.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <QHash>
#include <QPointF>
#include <QWidget>

#include "klustersshared/parameteryamlreader_probes.h"

class QGraphicsScene;
class QGraphicsView;
class QGraphicsItem;
class QEvent;
class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QSplitter;
class QFormLayout;
class QScrollArea;

namespace probemaker {
class ProbeLogicalView;
class ProbePhysicalView;
class ConnectorItem;
class ShankItem;
class ChannelItem;
}  // namespace probemaker

/**
 * @brief The Probe Maker page.
 *
 *  Two-pane layout (logical graph on the left, physical layout on the
 *  right) plus a shared selection-driven inspector.  Both panes show
 *  the same in-memory `ProbeConnector`; selection is mirrored across
 *  both panes via the page's `itemSelected` signal handlers.
 *
 *  The page is the single source of truth for the probe data; both
 *  views and the inspector observe it.  Edits in any of the three
 *  surfaces flow back to `data`, which is then re-rendered by
 *  refreshing whichever items changed.
 */
class ProbeMakerPage : public QWidget
{
    Q_OBJECT

public:
    explicit ProbeMakerPage(QWidget* parent = nullptr);
    ~ProbeMakerPage() override;

    // ── Data access ──────────────────────────────────────────────────────

    /** Replace the page's data with @p connector and rebuild both
     *  views.  Used by loadFromFile() and by ParameterView when
     *  initialising the page from session state. */
    void setConnector(const ProbeConnector& connector);

    /** Returns a copy of the current data.  Used by saveToFile() and
     *  by the page's "apply to ProbePage table" workflow (TODO — not
     *  implemented in v1).  */
    ProbeConnector connector() const;

    /** True if the data has been edited since last load/save. */
    bool isModified() const { return m_modified; }

    // ── File I/O ─────────────────────────────────────────────────────────

    /** Read a `.probe` YAML file (canonical schema with `probeFile:`
     *  top-level key) and replace the page's data.  Returns true on
     *  success; on failure populates @p error and returns false.
     *
     *  Accepts both library files (src/nphys-data/...) and session-
     *  local `.ndm.<n>.probe` files — schema is identical. */
    bool loadFromFile(const QString& path, QString* error = nullptr);

    /** Serialise the current connector to @p path in the canonical
     *  schema. */
    bool saveToFile(const QString& path, QString* error = nullptr) const;

    /** Empty the data and rebuild both views with just the connector
     *  root and no shanks.  Equivalent to clearGraphToRoot() in the
     *  Pipeline Designer. */
    void clearToConnector();

signals:
    /** Emitted whenever the data model changes, whether by user
     *  action or by a load/clear.  Connected by the parameter view to
     *  drive the document's modified flag. */
    void modified();

    /** Emitted when the toolbar Save button is pressed.  ParameterView
     *  resolves the session base path and writes
     *  <session>.ndm.default.probe. */
    void savePipelineRequested();   // intentionally same idiom as PipelineDesignerPage

    /** Emitted when Save As… is pressed.  ParameterView prompts for a
     *  name and writes <session>.ndm.<n>.probe. */
    void saveAsPipelineRequested();

private slots:
    void onLogicalSelectionChanged();
    void onPhysicalSelectionChanged();
    void onAddShankClicked();
    void onAddChannelClicked();
    void onAddArrayClicked();    ///< add a linear array of N sites
    void onDeleteSelectedClicked();
    void onUndoClicked();
    void onRedoClicked();

    /** Inspector commit handlers — bound to editingFinished on each
     *  inspector field.  Each one reads the selected item, writes the
     *  field value into the model, then refreshes the affected
     *  graphics item. */
    void onConnectorFieldEdited();
    void onShankFieldEdited();
    void onChannelFieldEdited();

private:
    void buildLogicalPane(QSplitter* inner);
    void buildPhysicalPane(QSplitter* inner);
    void buildInspector(QSplitter* inner);

    /** Repopulate the logical-view scene from data.  Called after
     *  any structural edit (add/remove shank or channel).  Geometry-
     *  only edits are handled by item refreshFromModel(). */
    void rebuildLogicalScene();

    /** Repopulate the physical-view scene from data.  Same trigger
     *  as rebuildLogicalScene; the two are always called together. */
    void rebuildPhysicalScene();

    /** Mirror the selected item from one view into the other.  We
     *  identify items by their (kind, id) pair stored in QGraphicsItem
     *  data roles. */
    void mirrorSelection(const QGraphicsScene* sourceScene,
                         QGraphicsScene*       targetScene);

    /** Show or hide inspector field groups based on the current
     *  selection's kind.  Connector / shank / channel each get their
     *  own FormLayout. */
    void refreshInspector();

    void setModified(bool b);

    // ── Data ─────────────────────────────────────────────────────────────
    ProbeConnector data;
    bool           m_modified = false;

    // ── Views ────────────────────────────────────────────────────────────
    probemaker::ProbeLogicalView*  logicalView  = nullptr;
    probemaker::ProbePhysicalView* physicalView = nullptr;

    // ── Inspector ────────────────────────────────────────────────────────
    QScrollArea* inspScroll = nullptr;
    QWidget*     inspWidget = nullptr;

    // Connector field group
    QWidget*    connGroup        = nullptr;
    QLineEdit*  connVendor       = nullptr;
    QLineEdit*  connModel        = nullptr;
    QSpinBox*   connTotalChan    = nullptr;
    QLineEdit*  connNotes        = nullptr;

    // Shank field group
    QWidget*       shankGroup    = nullptr;
    QLineEdit*     shankLabel    = nullptr;
    QDoubleSpinBox*shankLength   = nullptr;
    QDoubleSpinBox*shankWidth    = nullptr;
    QDoubleSpinBox*shankTipAngle = nullptr;
    QDoubleSpinBox*shankOriginX  = nullptr;
    QDoubleSpinBox*shankOriginY  = nullptr;
    QLineEdit*     shankLayout   = nullptr;

    // Channel field group
    QWidget*       chanGroup     = nullptr;
    QSpinBox*      chanHwId      = nullptr;
    QDoubleSpinBox*chanX         = nullptr;
    QDoubleSpinBox*chanY         = nullptr;
    QSpinBox*      chanSiteIdx   = nullptr;
    QDoubleSpinBox*chanArea      = nullptr;

    // Toolbar buttons
    QPushButton* saveBtn       = nullptr;
    QPushButton* saveAsBtn     = nullptr;
    QPushButton* addShankBtn   = nullptr;
    QPushButton* addChannelBtn = nullptr;
    QPushButton* addArrayBtn   = nullptr;
    QPushButton* deleteBtn     = nullptr;
    QPushButton* undoBtn       = nullptr;
    QPushButton* redoBtn       = nullptr;

    /** Selection state.  Each field is non-null only when an item of
     *  that kind is currently selected (the three are mutually
     *  exclusive — multi-selection across kinds isn't supported in v1
     *  because the inspector wouldn't know what to show). */
    ProbeShank*    selectedShank   = nullptr;
    ProbeChannel*  selectedChannel = nullptr;
    bool           connectorSelected = false;

    /** Set true the first time the user zooms or pans the physical
     *  view; suppresses fitAll() on subsequent scene rebuilds so a
     *  drag/edit doesn't snap their view back to the default.  Cleared
     *  by loadFromFile (fresh content gets a fresh fit) and by the
     *  Fit toolbar button (explicit re-fit). */
    bool           userZoomedOrPanned = false;

    /** Per-item logical-view positions, used by the logical-graph
     *  editor to remember where the user dragged each node across
     *  scene rebuilds.  Without this, every model edit (which
     *  triggers rebuildLogicalScene) would snap the user's hand-
     *  arranged graph back to the auto-layout.
     *
     *  Keys are stable composite strings, not raw model pointers,
     *  because `QList<ProbeShank>` reallocates on append (and
     *  `QList<ProbeChannel>` likewise inside each shank) — so model
     *  addresses are not stable across edits, and a stale address
     *  could collide with a freshly-appended item placed at the same
     *  recycled location.  See logicalKeyForXxx() helpers in
     *  probemakerpage.cpp for the key format.
     *
     *  Cleared wholesale on clearToConnector / setConnector; pruned
     *  of dead keys at the top of every rebuildLogicalScene. */
    QHash<QString, QPointF> logicalState;

    // ── Undo / redo ────────────────────────────────────────────────────
    //
    // Snapshot-based undo: every discrete operation (toolbar action,
    // inspector field commit, drag-to-reposition) calls
    // pushUndoSnapshot() *before* mutating data or logicalState.
    // Undo pops the most recent snapshot and replaces data plus the
    // logical-view position cache with the snapshot's; redo replays
    // the inverse.  The model is small enough (a ProbeConnector with
    // a QList<ProbeShank>) to copy in full on every op without
    // measurable cost.
    //
    // Drag tracking: an event filter on both viewports' viewport
    // widgets snapshots on MouseButtonPress over a movable item and
    // pops the just-pushed snapshot on MouseButtonRelease if the
    // tracked item didn't actually move (no-op click-selects).  See
    // eventFilter() and dragSnapshotPushed below.
    //
    // Selection isn't saved in the snapshot.  After undo or redo, no
    // item is selected — the model has new shank/channel addresses
    // post-assignment, so the old selectedShank / selectedChannel
    // pointers would be dangling.  The slots null them out and the
    // user re-selects to pick up where they were.
    struct UndoSnapshot {
        ProbeConnector          data;          ///< model snapshot
        QHash<QString, QPointF> logicalState;  ///< logical-view positions
        QString                 description;   ///< human-readable, for tooltips
    };
    QList<UndoSnapshot> undoStack;
    QList<UndoSnapshot> redoStack;
    static constexpr int kMaxUndoDepth = 200;

    /** Set true while onUndoClicked / onRedoClicked are restoring a
     *  prior snapshot.  pushUndoSnapshot returns early in that mode so
     *  the restore itself doesn't land on the stack — without this,
     *  the rebuilds called during restore could re-enter mutating
     *  code paths that push, blowing up history. */
    bool undoInProgress = false;

    /** Set true while a higher-level mutation (onAddArrayClicked,
     *  onAddChannelClicked when there's no shank yet) is recursively
     *  invoking another mutator (onAddShankClicked) for a sub-step.
     *  pushUndoSnapshot returns early in that mode so the user's
     *  single conceptual action becomes one undo entry rather than
     *  one entry per nested call.  Distinct from undoInProgress
     *  because the semantics differ — restore is "model is being
     *  reverted, don't touch history"; nested-mutation is "model is
     *  being mutated normally, but already snapshotted." */
    bool inNestedMutation = false;

    /** Edit identity for coalescing consecutive same-field edits into
     *  a single undo step.  When the user drags a spinbox arrow from
     *  1500 → 1501 → 1502, valueChanged fires once per tick; without
     *  coalescing each tick would land on the undo stack as a separate
     *  step.  We instead push only the FIRST tick (capturing the
     *  pre-edit state) and let subsequent same-field ticks update
     *  data without pushing.
     *
     *  identityKind names the conceptual field (e.g. "shank.length",
     *  "channel.posX"); identityModelPtr disambiguates which item.
     *  Any non-edit op (add / delete / undo / redo / load) calls
     *  resetEditIdentity() so the next edit, even on the same field,
     *  is a fresh undo step. */
    QString lastEditKind;
    void*   lastEditModelPtr = nullptr;
    void resetEditIdentity()
    {
        lastEditKind.clear();
        lastEditModelPtr = nullptr;
    }

    /** Push the current data state on the undo stack with the given
     *  description, and clear the redo stack (a new edit invalidates
     *  any prior redo history).  Caps undo depth at kMaxUndoDepth by
     *  dropping the oldest entry. */
    void pushUndoSnapshot(const QString& description);

    /** Coalescing variant: if the most recent push had the same
     *  (editKind, modelPtr), don't push again — the state already on
     *  the undo stack is already the pre-edit state.  Otherwise push
     *  normally. */
    void pushUndoSnapshotCoalesced(const QString& description,
                                    const QString& editKind,
                                    void*          modelPtr);

    /** Update enabled state and tooltips on the undo/redo buttons
     *  based on current stack contents.  Called after every push,
     *  undo, and redo. */
    void updateUndoRedoButtons();

    // ── Drag tracking via event filter ─────────────────────────────────
    //
    // The page installs itself as event filter on both viewports'
    // viewport widgets (logicalView->viewport(), physicalView->
    // viewport()).  On MouseButtonPress over a movable item, it
    // pushes a snapshot and remembers the item's pre-drag pos.  On
    // MouseButtonRelease, it compares the current pos to the
    // remembered one — if they're equal, the user just clicked
    // (no drag actually happened) and we pop the just-pushed
    // snapshot to keep the undo history clean.  If they differ,
    // the snapshot stays.

    /** True iff a drag-press handler pushed an undo snapshot that's
     *  still pending a corresponding release.  Used by the release
     *  handler to decide whether to keep or pop the snapshot. */
    bool             dragSnapshotPushed = false;

    /** The item whose press triggered the snapshot, and the position
     *  it had at that moment.  Compared at release to determine if
     *  the drag was a no-op. */
    QGraphicsItem*   dragItem = nullptr;
    QPointF          dragStartPos;

protected:
    /** Implements drag-press / drag-release detection on both
     *  viewports — see "Drag tracking" doc above. */
    bool eventFilter(QObject* obj, QEvent* ev) override;
};
