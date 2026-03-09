/***************************************************************************
 * probepage.h
 *
 * ndmanager Probe tab: view/edit the list of probes implanted in this
 * session and their association with anatomical groups.
 *
 * Each row of the table represents one probe entry:
 *   ID | ProbeFile | Label | ChannelOffset | AnatomicalGroups
 *
 * "AnatomicalGroups" is a comma-separated list of 1-based anatomical group
 * IDs (matching anatomicalDescription.channelGroups entries) that belong to
 * this probe.  The hierarchy becomes:
 *
 *   probe  →  anatomical group (= shank)  →  channels
 *
 * For single-probe sessions the column can simply contain all group IDs,
 * preserving backward compatibility.
 *
 * Copyright (C) 2025 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef PROBEPAGE_H
#define PROBEPAGE_H

#include "probelayout.h"    // generated from probelayout.ui

// ProbeEntry is defined in libklustersshared — use that authoritative definition
// so ProbePage, NdManagerYamlReader, and ParameterYamlWriter all share one type.
#include <klustersshared/parameteryamlreader_probes.h>

#include <QWidget>
#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * @brief The Probe tab page in ndmanager.
 *
 * Inherits ProbeLayout (Qt Designer), mirrors the pattern of AnatomyPage /
 * SpikePage.  The page is inserted into ParameterView::mStackWidget just
 * after the Anatomical Groups entry when expert mode is active.
 */
class ProbePage : public ProbeLayout
{
    Q_OBJECT

public:
    explicit ProbePage(QWidget* parent = nullptr);
    ~ProbePage() override = default;

    // ---- Data setters / getters (called by ParameterView) ----------------

    /**
     * @brief Populate the table from a list of ProbeEntry structs.
     * Called by ParameterView::initialize() when a file is loaded.
     */
    void setProbes(const QList<ProbeEntry>& probes);

    /**
     * @brief Retrieve the current probe list from the table.
     * Called by ParameterView::getInformation() before saving.
     */
    void getProbes(QList<ProbeEntry>& probes) const;

    /** Set the probe library search path shown in the text box. */
    void setLibraryPath(const QString& path);

    /** Return the current library path (empty = use default). */
    QString getLibraryPath() const;

    /** True if any row has been modified since the last resetModificationStatus(). */
    bool isModified() const { return m_modified; }
    void setModified(bool b) { m_modified = b; }

signals:
    void probesModified();

public slots:
    void addProbe();
    void removeProbe();
    void moveProbeUp();
    void moveProbeDown();
    void browseProbeFile();    ///< open a file dialog in the library path
    void browseLibraryPath();  ///< change the probe library root folder
    void cellEdited(int row, int column);
    void rowSelected();        ///< update diagram when selection changes
    void resetModificationStatus() { m_modified = false; }

private:
    // Table column indices
    enum Col {
        ColId          = 0,
        ColFile        = 1,
        ColLabel       = 2,
        ColOffset      = 3,
        ColGroups      = 4,   ///< anatomical groups
        ColSpikeGroups = 5    ///< spike-sorting groups (defaults to ColGroups if empty)
    };

    void populateRow(int row, const ProbeEntry& entry);
    ProbeEntry rowToEntry(int row) const;
    void renumberIds();

    bool    m_modified  = false;
    QString m_libraryPath;
};

#endif // PROBEPAGE_H
