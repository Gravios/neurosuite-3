/***************************************************************************
                          clusterPalette.h  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef CLUSTERPALETTE_H
#define CLUSTERPALETTE_H

//QT include files
#include <QVariant>
#include <QWidget>
#include <QListWidget>
#include <QToolTip>

#include <QList>
#include <QMap>      // renumberPinnedIds takes a QMap<int,int>
#include <QSet>      // sPinnedIds is QSet<int>

class QStatusBar;


// forward declaration of the KlustersDoc class
class KlustersDoc;

/**
  * This class represents the Cluster Panel of the application.
  * It receives the user selections and triggers the actions which have to be done.
  *@author Lynn Hazan
  */


class ClusterPaletteWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit ClusterPaletteWidget(QWidget *parent);

protected:
    void focusInEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

Q_SIGNALS:
    void changeColor(QListWidgetItem* item);
    void onItem(QListWidgetItem* item);
    /** Emitted whenever the palette widget receives keyboard focus.
     *  klusters.cpp connects this to a slot that ensures the Overview
     *  Display tab is visible, then returns focus to the palette. */
    void paletteGainedFocus();
    /** Emitted when S toggles a cluster's selection state; connected to
     *  ClusterPalette::slotClickRedraw to trigger a display update. */
    void selectionToggled();

private:
    /** Tracks which item S was last pressed on, for the "S twice = isolate"
     *  behaviour: pressing S on an already-selected item that was the last
     *  S-target deselects all others and keeps only that item selected. */
    QListWidgetItem *lastSPressItem{nullptr};

    /** Cluster IDs explicitly toggled by the S key.  Arrow navigation
     *  restores selection on items whose cluster id is in this set, so
     *  S-pinning survives palette refreshes (renumbers, full recluster,
     *  etc.) — the icon for a renumbered cluster keeps its pinned state
     *  even though its iconView row may have moved.  Cleared when S
     *  isolates back to one cluster.
     *
     *  Stored as cluster ids rather than iconView rows so the set is
     *  stable across operations that re-build the iconView (T-key
     *  partial renumber, R-key full renumber, recluster, dipsplit,
     *  watershed) — those rebuild the icon list, leaving any
     *  row-indexed set pointing at unrelated icons. */
    QSet<int> sPinnedIds;

    /** True while arrow-key navigation is in progress. */
    bool navigating{false};

public:
    bool isNavigating() const { return navigating; }
    const QSet<int>& getSPinnedIds() const { return sPinnedIds; }

    friend class ClusterPalette;
};

/** Left-side dock widget showing the palette of clusters.
 *
 *  Renders one icon per cluster ID with the cluster's display colour and
 *  an "active/visible" checkbox.  Click-and-drag selects multiple
 *  clusters; cluster selection drives which clusters are shown in the
 *  active KlustersView and which are eligible for keyboard-shortcut
 *  curation actions (G = group, R = renumber, Shift+R = recluster, etc.).
 *
 *  The palette also intercepts a small set of keys when it has keyboard
 *  focus: S toggles the current cluster's visibility, T moves the
 *  selected cluster(s) to the tail of the palette, PageUp/PageDown
 *  nudge timestamps in the parent KlustersDoc.  These intercepts run
 *  through KlustersApp::eventFilter rather than as QAction shortcuts so
 *  they only fire when the palette is the focus widget.
 */
class ClusterPalette : public QWidget
{
    Q_OBJECT
    
public:
    /**
    * @param backgroundColor backgroundColor of the cluster palette.
    * @param parent the parent QWidget.
    * @param statusBar a reference to the application status bar.
    * @param name name of the widget (can be used for introspection).
    */
    explicit ClusterPalette(const QColor &backgroundColor, QWidget* parent = nullptr, QStatusBar * statusBar = nullptr, const char* name = nullptr );
    /*
   *  Destroys the object and frees any allocated resources.
   */
    ~ClusterPalette();

    //Mode of action, in immediate the change of color
    //and the selection of cluster is immediately trigger
    enum Mode {IMMEDIATE = 1, DELAY = 2};
    
    enum DataStored {
        INDEX      = Qt::UserRole + 1,   ///< storage index in ItemColors itemList
        CLUSTER_ID = Qt::UserRole + 2,   ///< the cluster id this item displays —
                                         ///< stable across renumbers, unlike the
                                         ///< iconView row.  Used so sPinnedIds
                                         ///< can pin clusters by id rather than
                                         ///< by palette row.
    };

    void createClusterList(KlustersDoc* doc);
    void updateClusterList();
    void selectItems(const QList<int> &selectedClusters);

    /** Rewrite the S-pinned-ids set through a partial cluster-rename map.
     *  Each entry whose key matches a pinned id gets its id replaced by
     *  the new id; pinned ids not in the map are left alone (because
     *  they refer to clusters that weren't renamed).  Called by
     *  KlustersDoc::applyClusterRename so S-pinning survives both the
     *  T-key partial renumber and the R-key full renumber.
     *  No-op if no pinned ids are set. */
    void renumberPinnedIds(const QMap<int,int>& oldToNew);

    void setImmediateMode(){mode = IMMEDIATE;}
    void setDelayMode(){mode = DELAY;}
    void reset();
    /**Returns the list of selected clusters*/
    QList<int> selectedClusters();

    /** Hides the user cluster information, that is show the normal cluster palette.*/
    void hideUserClusterInformation();

    /** Shows the user cluster information, that is show a modified cluster palette presenting the cluster ids and the user cluster information.
      * @param electrodeGroupId id of the current electrode group.
     */
    void showUserClusterInformation(int electrodeGroupId);

    /**updates the background color of the palette.*/
    void changeBackgroundColor(const QColor& color);

    /** Transfer keyboard focus to the inner list widget so arrow-key
     *  navigation works immediately after the Overview tab is raised. */
    void setFocusToList();

public Q_SLOTS:
    /** Called by KlustersApp when S is pressed while the palette has focus.
     *  Intercept needed because Qt::Key_S is also the Split Clusters shortcut. */
    void toggleCurrentSelection();
    void changeColor(QListWidgetItem *item);
    void moveClustersToNoise();
    void moveClustersToArtefact();
    void groupClusters();
    void updateClusters();

protected Q_SLOTS:
    /** The right click on a cluster icon bring a dialog allowing the user to enter information on the cluster
    * (structure, type, isolation distance, quality and notes).
    */
    void slotClickRedraw();
    void languageChange();
    /**
     * When moving the mouse over an cluster icon, the statusBar is updated with the information the user might could provided
      * (structure, type, isolation distance, quality and notes).
     */
    void slotOnItem(QListWidgetItem *item);
    
    void slotCustomContextMenuRequested(const QPoint&);

Q_SIGNALS:
    void singleChangeColor(int selectedCluster);
    void updateShownClusters(const QList<int>& selectedClusters);
    void groupClusters(const QList<int> &selectedClusters);
    void moveClustersToNoise(const QList<int> &selectedClusters);
    void moveClustersToArtefact(const QList<int> &selectedClusters);
    void clusterInformationModified();
    /** Forwarded from ClusterPaletteWidget: emitted when the palette gains
     *  keyboard focus. klusters.cpp uses this to switch to the Overview tab. */
    void paletteGainedFocus();

private:
    ClusterPaletteWidget* iconView;
    KlustersDoc* doc;

    Mode mode;//default IMMEDIATE

    /**Prevent from emitting signal while globaly selecting items*/
    bool isInSelectItems;

    /**Prevent from emitting a signal of update if the selection had not changed.*/
    bool isUpToDate;
    
    /**Current palette background Color.*/
    QColor backgroundColor;

    /**Pointer to the status bar of the application.*/
    QStatusBar* statusBar;

    /**Allows to update correctly the cluster text.*/
    bool isInUserClusterInfoMode;

};

#endif // CLUSTERPALETTE_H
