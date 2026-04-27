/***************************************************************************
 * probepage.h
 *
 * ndmanager Probe tab: view/edit the list of probes implanted in this
 * session and their association with anatomical groups.
 *
 * Each row represents one probe entry:
 *   ID | ProbeFile | Label | ChannelOffset | AnatomicalGroups | SpikeGroups
 *
 * When a probe file is imported via Browse, the .probe YAML is parsed and
 * the anatomy / spike groups are derived automatically.  A signal is then
 * emitted so ParameterView can push the derived maps into AnatomyPage and
 * SpikePage immediately.
 *
 * Copyright (C) 2025 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef PROBEPAGE_H
#define PROBEPAGE_H

#include "probelayout.h"    // generated from probelayout.ui

#include <klustersshared/parameteryamlreader_probes.h>

#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QStackedWidget;
class ProbeMakerPage;

/**
 * @brief The Probe tab page in ndmanager's expert-mode parameter view.
 *
 * Mirrors the pattern of AnatomyPage / SpikePage.  Inserted into
 * ParameterView::mStackWidget just after the Spike Groups entry when expert
 * mode is active.
 */
class ProbePage : public ProbeLayout
{
    Q_OBJECT

public:
    explicit ProbePage(QWidget* parent = nullptr);
    ~ProbePage() override = default;

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
    /** Emitted whenever any cell is edited or a row is added/removed. */
    void probesModified();

    /**
     * Emitted after a .probe file is successfully imported via Browse.
     *
     * @param probes         Updated full probe list (all rows).
     * @param anatomyGroups  groupId → channel list (0-based), derived from
     *                       the imported probe file.  Ready to pass directly
     *                       to AnatomyPage::setGroups().
     * @param spikeGroups    Same shape; equals anatomyGroups when the probe
     *                       has no separate spike-group override.
     * @param firstNewGroupId The first 1-based group ID assigned to the
     *                       newly imported probe (= previous max + 1).
     */
    void probeLayoutImported(QList<ProbeEntry>             probes,
                             QMap<int, QList<int>>         anatomyGroups,
                             QMap<int, QList<int>>         spikeGroups,
                             int                           firstNewGroupId);

public slots:
    void addProbe();
    void removeProbe();
    void moveProbeUp();
    void moveProbeDown();
    void browseProbeFile();    ///< open file dialog, import on success
    void browseLibraryPath();  ///< change the probe library root folder
    void cellEdited(int row, int column);
    void rowSelected();        ///< update diagram preview
    void resetModificationStatus() { m_modified = false; }

    /**
     * Recompute channel offsets and anatomy/spike groups for every row from
     * scratch.  Called after any structural change (Browse, Add, Remove,
     * Move, or editing the probe file column directly).
     */
    void recalculateAll();

private:
    enum Col {
        ColId          = 0,
        ColFile        = 1,
        ColLabel       = 2,
        ColOffset      = 3,
        ColGroups      = 4,   ///< anatomical groups
        ColSpikeGroups = 5    ///< spike groups (empty = same as anatomical)
    };

    void populateRow(int row, const ProbeEntry& entry);
    ProbeEntry rowToEntry(int row) const;
    void renumberIds();

    /**
     * Copy the probe file at @p srcPath into the session directory.
     * When @p probeId is non-negative the file is renamed to
     * <session>.probe.<probeId>.probe so the file is unambiguously
     * associated with the row whose ID is @p probeId.  When @p probeId
     * is -1 the legacy "keep source basename" behaviour is used (only
     * for callers that pre-date the renumberIds flow).
     *
     * Returns the bare filename to store in the table's File column,
     * or an empty string on failure / user cancel.  If the file
     * already exists at the destination, the user is asked whether
     * to overwrite, reuse, or cancel.
     */
    QString copyProbeIntoSession(const QString& srcPath, int probeId = -1);

    /** Returns the local filename a probe with @p probeId should have
     *  in the session directory: <session>.probe.<probeId>.probe where
     *  <session> is the current working directory's basename. */
    static QString sessionProbeFilename(int probeId);

    /** Load the probe file referenced by row @p row into the embedded
     *  ProbeMakerPage and show the maker frame.  When the file is
     *  missing, hides the maker frame and shows the no-probe placeholder. */
    void loadProbeIntoMaker(int row);

    /** Save the embedded maker's current connector state to
     *  <session>.probe.<probeId>.probe for the currently-tracked row,
     *  and update the table's File column to point at it.  Called on
     *  row-change and on the maker's modified() signal. */
    void saveMakerToCurrentRow();

    /**
     * Parse a .probe YAML file and fill @p entry (label, probeFile, groups)
     * and @p derivedAnatomy / @p derivedSpike.
     *
     * @param path         Absolute path to the .probe file.
     * @param entry        ProbeEntry to fill (probeFile, label, channelOffset,
     *                     anatomicalGroups, spikeGroups already set on entry).
     * @param nextGroupId  1-based ID for the first new group (caller maintains
     *                     the counter across probes).
     * @param outAnatomy   Filled with groupId → 0-based channel list.
     * @param outSpike     Same; equals outAnatomy when probe has no override.
     * @return true on success, false if parse failed.
     */
    bool importProbeYaml(const QString&        path,
                         ProbeEntry&           entry,
                         int                   nextGroupId,
                         QMap<int,QList<int>>& outAnatomy,
                         QMap<int,QList<int>>& outSpike);

    bool    m_modified   = false;
    QString m_libraryPath;
    int     m_nbChannels = 0;

    /** Embedded probe-geometry editor.  Lives inside the existing
     *  diagramGroupBox area on the right side of the page; replaces
     *  the static SVG preview when a row is selected and a probe
     *  file is loaded.  The page owns its lifetime via Qt parent. */
    class ProbeMakerPage* m_probeMaker = nullptr;

    /** Container that wraps m_probeMaker plus the placeholder QLabel
     *  ("Select a row to edit").  Toggled visible/hidden as
     *  selection changes; m_probeMaker is shown when a probe is
     *  loaded, the placeholder otherwise. */
    class QStackedWidget* m_makerStack = nullptr;

    /** Row whose probe is currently loaded into m_probeMaker.  -1
     *  when no probe is loaded.  Used to know which file to save
     *  back to when the maker emits modified(). */
    int m_currentMakerRow = -1;

    /** Suppresses re-entrant load when the page is updating the maker
     *  programmatically (e.g. during setProbes population).  Without
     *  this the maker's modified() signal would bounce back through
     *  saveMakerToCurrentRow() on every load. */
    bool m_loadingMaker = false;

    /**
     * Return totalChannels from a .probe YAML, or 0 on error.
     * Used to auto-compute channel offsets without a full parse.
     */
    static int probeChannelCount(const QString& probePath);
};

#endif // PROBEPAGE_H
