/***************************************************************************
                          prefclusterview.h  -  description
                             -------------------
    begin                : Thu Dec 11 2003
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

#ifndef PREFCLUSTERVIEW_H
#define PREFCLUSTERVIEW_H

// include files for QT
#include <QWidget>
#include <QSpinBox>

//include files for the application
#include <prefclusterviewlayout.h>


/**
  * Class representing the Cluster View configuration page of the Klusters preferences dialog.
  *@author Lynn Hazan
  */

class PrefClusterView : public PrefClusterViewLayout  {
    Q_OBJECT
public: 
    explicit PrefClusterView(QWidget *parent=nullptr);
    ~PrefClusterView();

    /**Sets the time interval between 2 lines drawn in the cluster views
  * when the time dimension in selected. The time @p time is in second.*/
    void setTimeInterval(int time);

    /**Returns the time interval between 2 lines drawn in the cluster views
  * when the time dimension in selected. The time is in second.*/
    int getTimeInterval() const;

    /**Sets the maximum number of spikes the feature view will embed with
  * t-SNE (the F toggle).  A latency budget, not an OOM guard.*/
    void setTsneSpikeCap(int cap);

    /**Returns the t-SNE spike cap as typed.  A non-numeric entry falls back
  * to the shipped default; Configuration clamps the floor on apply.*/
    int getTsneSpikeCap() const;

    /**Sets the perplexity increment the up/down arrows apply in the t-SNE
  * view.  Each press recomputes the embedding at the new value.*/
    void setTsnePerplexityStep(int step);

    /**Returns the perplexity step as typed; a non-numeric entry falls back to
  * the shipped default and Configuration clamps the floor on apply.*/
    int getTsnePerplexityStep() const;

    // The rest of the engine's parameters.  Each reads back what was typed; a
    // non-numeric field falls back to the shipped default and Configuration
    // clamps the range on apply, so the panel cannot put the engine into a
    // state it would refuse.
    void setTsneStartPerplexity(double v);
    double getTsneStartPerplexity() const;

    void setTsneIterations(int v);
    int getTsneIterations() const;

    void setTsneTheta(double v);
    double getTsneTheta() const;

    void setTsneMaxDimensions(int v);
    int getTsneMaxDimensions() const;

    void setTsneLearningRate(double v);
    double getTsneLearningRate() const;

    void setTsneExaggeration(double v);
    double getTsneExaggeration() const;

    void setTsneExaggerationIterations(int v);
    int getTsneExaggerationIterations() const;

    void setTsneSubsampleOverCap(bool b);
    bool getTsneSubsampleOverCap() const;

    void setTsneRandomSeed(bool b);
    bool getTsneRandomSeed() const;

    // EAP template-class projection scope (claude/eap-template-class-design §7).
    void setProjectionScopeMode(int m);         ///< 0 session-spanning, 1 temporally-restricted
    int  getProjectionScopeMode() const;
    void setProjectionScopeMinutes(double v);   ///< scope-chunk granularity (minutes)
    double getProjectionScopeMinutes() const;
    void setProjectionOutOfScopeHidden(bool b); ///< true = hide, false = grey/non-selectable
    bool getProjectionOutOfScopeHidden() const;
    void setShowEapCollisions(bool b);          ///< ring spikes with >=2 .eap classes
    bool getShowEapCollisions() const;
    void setShowEapClassMembers(bool b);        ///< ring the selected class's .eap members
    bool getShowEapClassMembers() const;

};

#endif
