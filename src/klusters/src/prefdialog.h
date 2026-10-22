/***************************************************************************
                          prefdialog.h  -  description
                             -------------------
    begin                : Thu Dec 12 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                :
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PREFDIALOG_H
#define PREFDIALOG_H

#include <QWidget>
#include <qpagedialog.h>

class PrefDisplay;
class PrefSession;
class PrefReclustering;
class PrefRealignment;
class PrefSplitting;
class PrefMerging;
class PrefClusterOrder;
class PrefMatrices;
class PrefWaveformView;
class PrefClusterView;
class PrefInput;
class PrefRadialMenu;
struct ActionCatalogEntry;
class QComboBox;

class PrefDialog : public QPageDialog {
    Q_OBJECT
  public:
    explicit PrefDialog(QWidget *parent, int nbChannels = 0);

    void updateDialog();
    void updateConfiguration();
    bool isApplyEnable() const { return applyEnable; }

    void resetChannelList(int nbChannels);
    void enableChannelSettings(bool state);

    /** Syncs the N-features spinbox from the toolbar without triggering applyPreferences. */
    void syncAutoNFeatures(int n);

    /** The actions the Radial menu page can place (KlustersApp::actionCatalog());
     *  set before updateDialog(). */
    void setRadialCatalog(const QList<ActionCatalogEntry>& entries);

  public Q_SLOTS:
    void slotDefault();
    void slotApply();
    void enableApply();
    void slotHelp();

  Q_SIGNALS:
    void settingsChanged();

  private:
    // General
    PrefSession*      prefSession;
    // Views
    PrefDisplay*      prefDisplay;
    PrefMatrices*     prefMatrices;
    // Curation
    PrefRealignment*  prefRealignment;
    PrefSplitting*    prefSplitting;
    PrefMerging*      prefMerging;
    PrefClusterOrder* prefClusterOrder;
    PrefReclustering* prefReclustering;

    PrefWaveformView* prefWaveformView;
    PrefClusterView*  prefclusterView;
    PrefInput*        prefInput;        // generated input-binding page (input-remapping plan §5)
    PrefRadialMenu*   prefRadialMenu;   // the right-click wheel's layout editor
    QComboBox*        themeCombo;
    bool applyEnable;
};

#endif  // PREFDIALOG_H
