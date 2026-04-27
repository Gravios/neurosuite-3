/***************************************************************************
 * probepage.h
 *
 * ndmanager Probe tab — list of probes implanted in this session, with
 * inspector and embedded geometry editor.
 *
 * Layout (single wide page, no sub-tabs):
 *
 *   ┌──────────┬──────────────────────────┬─────────────────────────────┐
 *   │ Probes   │ Inspector                │ Probe Geometry              │
 *   │ ──────   │ ─────────                │ (ProbeMakerPage)            │
 *   │ • 1      │ Label:  [_______]        │   ★ ROOT                    │
 *   │ • 2  ◄   │ File:   [_______]        │     │                       │
 *   │ • 3      │ Channel offset: [16]     │  Shank A   Shank B          │
 *   │   +      │                          │     │        │              │
 *   │   −      │ Anatomical groups: 1, 2  │   ch 0..7  ch 8..15         │
 *   │          │ Spike groups:      1, 2  │                             │
 *   └──────────┴──────────────────────────┴─────────────────────────────┘
 *
 * Selecting a probe in the list loads its .probe file into the right-
 * hand ProbeMakerPage; edits there are persisted to
 *   <session>.probe.<id>.probe
 * automatically.  Adding a probe (+) clones the system-wide
 *   /usr/local/share/neurosuite/probes/empty.probe
 * template so the user has a working starter file to edit immediately.
 *
 * The data model still uses ProbeEntry from libklustersshared.  Anat/
 * spike group lists are computed by importProbeYaml from the probe
 * geometry on import or save; the inspector shows them read-only.
 *
 * Copyright (C) 2025–2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef PROBEPAGE_H
#define PROBEPAGE_H

#include <klustersshared/parameteryamlreader_probes.h>

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QSplitter;

class ProbeMakerPage;

/**
 * @brief The Probe tab in ndmanager's expert-mode parameter view.
 *
 * Replaces the original ProbeLayout (.ui-driven QTableWidget) with a
 * three-pane single-page layout: probe-id list on the left, inspector
 * form in the middle, embedded ProbeMakerPage geometry editor on the
 * right.  No tabs, no sub-pages — everything is on one wide canvas.
 */
class ProbePage : public QWidget
{
    Q_OBJECT

public:
    explicit ProbePage(QWidget* parent = nullptr);
    ~ProbePage() override;

    // ---- Data setters / getters (called by ParameterView) ----------------

    void setProbes(const QList<ProbeEntry>& probes);
    void getProbes(QList<ProbeEntry>& probes) const;

    void setLibraryPath(const QString& path);
    QString getLibraryPath() const;

    bool isModified() const { return m_modified; }
    void setModified(bool b) { m_modified = b; }
    /** Total recording channels — used to compute the leftover group on import. */
    void setNbChannels(int n) { m_nbChannels = n; }

signals:
    /** Emitted whenever any inspector field is edited or a probe is
     *  added/removed/reordered. */
    void probesModified();

    /**
     * Emitted after a probe geometry is imported or saved.
     *
     * @param probes         Updated full probe list.
     * @param anatomyGroups  groupId → channel list (0-based), derived from
     *                       the probe geometry.  Ready to pass directly to
     *                       AnatomyPage::setGroups().
     * @param spikeGroups    Same shape; equals anatomyGroups when the probe
     *                       has no separate spike-group override.
     * @param firstNewGroupId 1-based group ID assigned to the first new
     *                       group on import (= previous max + 1).
     */
    void probeLayoutImported(QList<ProbeEntry>             probes,
                             QMap<int, QList<int>>         anatomyGroups,
                             QMap<int, QList<int>>         spikeGroups,
                             int                           firstNewGroupId);

public slots:
    void resetModificationStatus() { m_modified = false; }

    /**
     * Recompute anatomical/spike group memberships for every probe from
     * scratch by re-parsing each .probe file.  Called after Browse,
     * Add, Remove, or any inspector edit that affects channelOffset.
     * Propagates the result via probeLayoutImported.
     */
    void recalculateAll();

private slots:
    void onListSelectionChanged();
    void onAddClicked();          ///< clone empty.probe into a new entry
    void onRemoveClicked();       ///< delete current entry + its session-local file
    void onBrowseLibraryClicked();///< import a .probe file from the library
    void onLabelEdited();
    void onFileEdited();
    void onOffsetEdited();
    void onMakerModified();       ///< maker reports an edit → save .probe.N

private:
    void buildUi();
    void rebuildList();           ///< redraw the probe list from m_probes
    void loadProbeIntoMaker(int probeIndex);
    void refreshInspector();      ///< populate inspector fields from the current selection
    void updateGroupsLabels();    ///< populate the anat/spike read-only labels
    void saveCurrentProbeFile();  ///< write maker state to <session>.probe.<id>.probe

    /** Returns the local filename a probe with @p probeId should have
     *  in the session directory: <session>.probe.<probeId>.probe. */
    static QString sessionProbeFilename(int probeId);

    /** Resolve a probeFile cell value (absolute, session-local, or
     *  library basename) to an existing absolute path.  Returns empty
     *  string if neither candidate exists. */
    QString resolveProbePath(const QString& cellValue) const;

    /** Returns the path to the system-wide empty.probe template,
     *  honouring an optional NS3_PROBE_LIBRARY_PATH env var override. */
    QString emptyTemplatePath() const;

    /**
     * Copy @p srcPath into the session directory, renamed to
     * <session>.probe.<probeId>.probe.  Returns the bare filename to
     * store on the entry, or empty on failure / cancel.
     */
    QString copyProbeIntoSession(const QString& srcPath, int probeId);

    /**
     * Parse a .probe YAML, fill @p entry's anatomicalGroups/spikeGroups,
     * and populate the channel-list maps.
     */
    bool importProbeYaml(const QString&        path,
                         ProbeEntry&           entry,
                         int                   nextGroupId,
                         QMap<int,QList<int>>& outAnatomy,
                         QMap<int,QList<int>>& outSpike);

    /** totalChannels from a .probe YAML, or 0 on error. */
    static int probeChannelCount(const QString& probePath);

    // ── Data ─────────────────────────────────────────────────────────────
    QList<ProbeEntry> m_probes;
    int               m_currentIndex = -1;     ///< -1 = no selection
    bool              m_modified     = false;
    QString           m_libraryPath;
    int               m_nbChannels   = 0;
    bool              m_loadingMaker = false;  ///< suppress save during programmatic load

    // ── UI ───────────────────────────────────────────────────────────────
    QSplitter*       m_split          = nullptr;
    QListWidget*     m_list           = nullptr;
    QPushButton*     m_addBtn         = nullptr;
    QPushButton*     m_removeBtn      = nullptr;
    QPushButton*     m_browseBtn      = nullptr;

    // Inspector
    QLineEdit*       m_inspLabel      = nullptr;
    QLineEdit*       m_inspFile       = nullptr;
    QSpinBox*        m_inspOffset     = nullptr;
    QLabel*          m_inspAnatomy    = nullptr;   ///< read-only display
    QLabel*          m_inspSpike      = nullptr;   ///< read-only display

    // Geometry editor
    ProbeMakerPage*  m_maker          = nullptr;
};

#endif // PROBEPAGE_H
