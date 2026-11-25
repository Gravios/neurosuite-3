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
#include "configuration.h"

class QCheckBox;
class QDoubleSpinBox;


/**
  * Class representing the Cluster View configuration page of the Klusters preferences dialog.
  *@author Lynn Hazan
  */

class PrefClusterView : public PrefClusterViewLayout  {
    Q_OBJECT
public: 
    /** The spike under the cursor: the group built in the constructor. */
    void setHoverSpike(const Configuration::HoverSpike& h);
    Configuration::HoverSpike getHoverSpike() const;
    explicit PrefClusterView(QWidget *parent=nullptr);
    ~PrefClusterView();

    /**Sets the time interval between 2 lines drawn in the cluster views
  * when the time dimension in selected. The time @p time is in second.*/

    /**Returns the time interval between 2 lines drawn in the cluster views
  * when the time dimension in selected. The time is in second.*/

    /**Sets the maximum number of spikes the feature view will embed with
  * t-SNE (the F toggle).  A latency budget, not an OOM guard.*/

    /**Returns the t-SNE spike cap as typed.  A non-numeric entry falls back
  * to the shipped default; Configuration clamps the floor on apply.*/

    /**Sets the perplexity increment the up/down arrows apply in the t-SNE
  * view.  Each press recomputes the embedding at the new value.*/

    /**Returns the perplexity step as typed; a non-numeric entry falls back to
  * the shipped default and Configuration clamps the floor on apply.*/

    // The rest of the engine's parameters.  Each reads back what was typed; a
    // non-numeric field falls back to the shipped default and Configuration
    // clamps the range on apply, so the panel cannot put the engine into a
    // state it would refuse.









    // EAP template-class projection scope (claude/eap-template-class-design §7).

    // Density heatmap (Shift+H): Gaussian smoothing kernel sigma, screen pixels.

Q_SIGNALS:
    /** A spike-under-the-cursor knob was edited. */
    void hoverSpikeChanged();

private:
    QCheckBox*      hoverEnabledCheck = nullptr;
    QSpinBox*       hoverRingMarginSpin = nullptr;
    QDoubleSpinBox* hoverRingWidthSpin = nullptr;
    QDoubleSpinBox* hoverTraceWidthSpin = nullptr;
};

#endif
