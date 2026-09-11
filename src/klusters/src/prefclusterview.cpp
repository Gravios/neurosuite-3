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
#include <QDoubleValidator>


PrefClusterView::PrefClusterView(QWidget *parent) : PrefClusterViewLayout(parent) {
    // Text box by request; the validator keeps it numeric.  Floor mirrors
    // Configuration::setTsneSpikeCap, which clamps again on apply.
    tsneCapLineEdit->setValidator(
        new QIntValidator(1000, 100000000, tsneCapLineEdit));
    tsneStartPerpLineEdit->setValidator(new QDoubleValidator(2.0, 1000.0, 1, tsneStartPerpLineEdit));
    tsneStartPerpLineEdit->setToolTip(tr(
        "Perplexity a fresh embedding starts at; the up/down arrows move from here.\n"
        "Low values expose local structure, high values preserve the global layout."));
    tsneItersLineEdit->setValidator(new QIntValidator(50, 100000, tsneItersLineEdit));
    tsneItersLineEdit->setToolTip(tr(
        "Gradient iterations: the main quality-versus-time dial.\n"
        "250 for a quick look, 500 by default, 1000 when the result matters."));
    tsneThetaLineEdit->setValidator(new QDoubleValidator(0.0, 0.8, 2, tsneThetaLineEdit));
    tsneThetaLineEdit->setToolTip(tr(
        "Barnes-Hut approximation of the repulsive term.  0 is exact and very slow;\n"
        "0.5 is the usual compromise, higher is faster and coarser."));
    tsneDimsLineEdit->setValidator(new QIntValidator(0, 4096, tsneDimsLineEdit));
    tsneDimsLineEdit->setToolTip(tr(
        "How many feature dimensions to embed, counting from the first.\n"
        "0 uses every dimension except time."));
    tsneEtaLineEdit->setValidator(new QDoubleValidator(1.0, 100000.0, 1, tsneEtaLineEdit));
    tsneExagLineEdit->setValidator(new QDoubleValidator(1.0, 1000.0, 1, tsneExagLineEdit));
    tsneExagItersLineEdit->setValidator(new QIntValidator(0, 100000, tsneExagItersLineEdit));
    tsneAdvancedGroupBox->setToolTip(tr(
        "These change how the optimiser converges.  Wrong values do not fail loudly —\n"
        "they produce a plausible-looking embedding that means nothing."));
    tsneSubsampleCheckBox->setToolTip(tr(
        "With more spikes selected than the cap allows, embed a random subsample of\n"
        "cap size instead of refusing.  The sample is drawn across the whole selection."));
    tsneRandomSeedCheckBox->setToolTip(tr(
        "Off, the same selection always gives the same embedding.  On, each run draws a\n"
        "new seed — run twice and see whether a split survives before trusting it."));
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

void PrefClusterView::setTsneStartPerplexity(double v){
    tsneStartPerpLineEdit->setText(QString::number(v));
}

double PrefClusterView::getTsneStartPerplexity() const{
    bool ok = false;
    const double v = tsneStartPerpLineEdit->text().toDouble(&ok);
    return ok ? v : 30.0;
}

void PrefClusterView::setTsneIterations(int v){
    tsneItersLineEdit->setText(QString::number(v));
}

int PrefClusterView::getTsneIterations() const{
    bool ok = false;
    const int v = tsneItersLineEdit->text().toInt(&ok);
    return ok ? v : 500;
}

void PrefClusterView::setTsneTheta(double v){
    tsneThetaLineEdit->setText(QString::number(v));
}

double PrefClusterView::getTsneTheta() const{
    bool ok = false;
    const double v = tsneThetaLineEdit->text().toDouble(&ok);
    return ok ? v : 0.5;
}

void PrefClusterView::setTsneMaxDimensions(int v){
    tsneDimsLineEdit->setText(QString::number(v));
}

int PrefClusterView::getTsneMaxDimensions() const{
    bool ok = false;
    const int v = tsneDimsLineEdit->text().toInt(&ok);
    return ok ? v : 0;
}

void PrefClusterView::setTsneLearningRate(double v){
    tsneEtaLineEdit->setText(QString::number(v));
}

double PrefClusterView::getTsneLearningRate() const{
    bool ok = false;
    const double v = tsneEtaLineEdit->text().toDouble(&ok);
    return ok ? v : 200.0;
}

void PrefClusterView::setTsneExaggeration(double v){
    tsneExagLineEdit->setText(QString::number(v));
}

double PrefClusterView::getTsneExaggeration() const{
    bool ok = false;
    const double v = tsneExagLineEdit->text().toDouble(&ok);
    return ok ? v : 12.0;
}

void PrefClusterView::setTsneExaggerationIterations(int v){
    tsneExagItersLineEdit->setText(QString::number(v));
}

int PrefClusterView::getTsneExaggerationIterations() const{
    bool ok = false;
    const int v = tsneExagItersLineEdit->text().toInt(&ok);
    return ok ? v : 250;
}

void PrefClusterView::setTsneSubsampleOverCap(bool b){
    tsneSubsampleCheckBox->setChecked(b);
}

bool PrefClusterView::getTsneSubsampleOverCap() const{
    return tsneSubsampleCheckBox->isChecked();
}

void PrefClusterView::setTsneRandomSeed(bool b){
    tsneRandomSeedCheckBox->setChecked(b);
}

bool PrefClusterView::getTsneRandomSeed() const{
    return tsneRandomSeedCheckBox->isChecked();
}

void PrefClusterView::setTsnePerplexityStep(int step){
    tsneStepLineEdit->setText(QString::number(step));
}

int PrefClusterView::getTsnePerplexityStep() const{
    bool ok = false;
    const int v = tsneStepLineEdit->text().toInt(&ok);
    return ok ? v : 5;       // Configuration clamps the floor on apply
}
