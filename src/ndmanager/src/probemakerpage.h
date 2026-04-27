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

#include <QWidget>

#include "klustersshared/parameteryamlreader_probes.h"

class QGraphicsScene;
class QGraphicsView;
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
 *  surfaces flow back to `m_data`, which is then re-rendered by
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

    /** Inspector commit handlers — bound to editingFinished on each
     *  inspector field.  Each one reads the selected item, writes the
     *  field value into the model, then refreshes the affected
     *  graphics item. */
    void onConnectorFieldEdited();
    void onShankFieldEdited();
    void onChannelFieldEdited();

private:
    void buildToolbar(QSplitter* outer);
    void buildLogicalPane(QSplitter* inner);
    void buildPhysicalPane(QSplitter* inner);
    void buildInspector(QSplitter* inner);

    /** Repopulate the logical-view scene from m_data.  Called after
     *  any structural edit (add/remove shank or channel).  Geometry-
     *  only edits are handled by item refreshFromModel(). */
    void rebuildLogicalScene();

    /** Repopulate the physical-view scene from m_data.  Same trigger
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
    ProbeConnector m_data;
    bool           m_modified = false;

    // ── Views ────────────────────────────────────────────────────────────
    probemaker::ProbeLogicalView*  m_logicalView  = nullptr;
    probemaker::ProbePhysicalView* m_physicalView = nullptr;

    // ── Inspector ────────────────────────────────────────────────────────
    QScrollArea* m_inspScroll = nullptr;
    QWidget*     m_inspWidget = nullptr;

    // Connector field group
    QWidget*    m_connGroup        = nullptr;
    QLineEdit*  m_connVendor       = nullptr;
    QLineEdit*  m_connModel        = nullptr;
    QSpinBox*   m_connTotalChan    = nullptr;
    QLineEdit*  m_connNotes        = nullptr;

    // Shank field group
    QWidget*       m_shankGroup    = nullptr;
    QLineEdit*     m_shankLabel    = nullptr;
    QDoubleSpinBox*m_shankLength   = nullptr;
    QDoubleSpinBox*m_shankWidth    = nullptr;
    QDoubleSpinBox*m_shankTipAngle = nullptr;
    QDoubleSpinBox*m_shankOriginX  = nullptr;
    QDoubleSpinBox*m_shankOriginY  = nullptr;
    QLineEdit*     m_shankLayout   = nullptr;

    // Channel field group
    QWidget*       m_chanGroup     = nullptr;
    QSpinBox*      m_chanHwId      = nullptr;
    QDoubleSpinBox*m_chanX         = nullptr;
    QDoubleSpinBox*m_chanY         = nullptr;
    QSpinBox*      m_chanSiteIdx   = nullptr;
    QDoubleSpinBox*m_chanArea      = nullptr;

    // Toolbar buttons
    QPushButton* m_saveBtn       = nullptr;
    QPushButton* m_saveAsBtn     = nullptr;
    QPushButton* m_loadBtn       = nullptr;
    QPushButton* m_addShankBtn   = nullptr;
    QPushButton* m_addChannelBtn = nullptr;
    QPushButton* m_addArrayBtn   = nullptr;
    QPushButton* m_deleteBtn     = nullptr;

    /** Selection state.  Each field is non-null only when an item of
     *  that kind is currently selected (the three are mutually
     *  exclusive — multi-selection across kinds isn't supported in v1
     *  because the inspector wouldn't know what to show). */
    ProbeShank*    m_selectedShank   = nullptr;
    ProbeChannel*  m_selectedChannel = nullptr;
    bool           m_connectorSelected = false;

    /** Set true the first time the user zooms or pans the physical
     *  view; suppresses fitAll() on subsequent scene rebuilds so a
     *  drag/edit doesn't snap their view back to the default.  Cleared
     *  by loadFromFile (fresh content gets a fresh fit) and by the
     *  Fit toolbar button (explicit re-fit). */
    bool           m_userZoomedOrPanned = false;
};
