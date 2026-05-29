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

// Forward decls — General split into 5 grouped tabs (patch 0067).
class PrefDisplay;
class PrefSession;
class PrefReclustering;
class PrefRefinement;
class PrefAutoMerge;
class PrefWaveformView;
class PrefClusterView;

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

  public Q_SLOTS:
    void slotDefault();
    void slotApply();
    void enableApply();
    void slotHelp();

  Q_SIGNALS:
    void settingsChanged();

  private:
    // Five tabs that replace the old single PrefGeneral.
    PrefDisplay*      prefDisplay;
    PrefSession*      prefSession;
    PrefReclustering* prefReclustering;
    PrefRefinement*   prefRefinement;
    PrefAutoMerge*    prefAutoMerge;

    PrefWaveformView* prefWaveformView;
    PrefClusterView*  prefclusterView;
    bool applyEnable;
};

#endif  // PREFDIALOG_H
