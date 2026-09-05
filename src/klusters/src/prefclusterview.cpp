/***************************************************************************
                          prefclusterview.cpp  -  description
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

#include "prefclusterview.h"

#include <QIntValidator>


PrefClusterView::PrefClusterView(QWidget *parent) : PrefClusterViewLayout(parent) {
    // Text box by request; the validator keeps it numeric.  Floor mirrors
    // Configuration::setTsneSpikeCap, which clamps again on apply.
    tsneCapLineEdit->setValidator(
        new QIntValidator(1000, 100000000, tsneCapLineEdit));
    tsneStepLineEdit->setValidator(
        new QIntValidator(1, 1000, tsneStepLineEdit));
    tsneStepLineEdit->setToolTip(
        tr("How much one up/down arrow press changes the perplexity while the\n"
           "t-SNE view is showing.  Each press recomputes the embedding."));
    tsneCapLineEdit->setToolTip(
        tr("Selections with more spikes than this refuse the t-SNE toggle.\n"
           "Latency budget: ~30k spikes take on the order of a minute."));
}
PrefClusterView::~PrefClusterView(){
}

void PrefClusterView::setTimeInterval(int time){
    intervalSpinBox->setValue(time);
}

int PrefClusterView::getTimeInterval() const{
    return intervalSpinBox->value();
}

void PrefClusterView::setTsneSpikeCap(int cap){
    tsneCapLineEdit->setText(QString::number(cap));
}

int PrefClusterView::getTsneSpikeCap() const{
    bool ok = false;
    const int v = tsneCapLineEdit->text().toInt(&ok);
    return ok ? v : 32000;   // Configuration clamps the floor on apply
}

void PrefClusterView::setTsnePerplexityStep(int step){
    tsneStepLineEdit->setText(QString::number(step));
}

int PrefClusterView::getTsnePerplexityStep() const{
    bool ok = false;
    const int v = tsneStepLineEdit->text().toInt(&ok);
    return ok ? v : 5;       // Configuration clamps the floor on apply
}
