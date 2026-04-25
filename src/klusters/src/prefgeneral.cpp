/***************************************************************************
                          prefgeneral.cpp  -  description
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
//Application specific includes.
#include "prefgeneral.h"
#include <QPushButton>

//QT includes
#include <QIcon>
#include <QFileDialog>



PrefGeneral::PrefGeneral(QWidget *parent )
    : PrefGeneralLayout(parent)
{
    connect(crashRecoveryCheckBox,&QCheckBox::stateChanged,this,&PrefGeneral::updateCrashRecoveryTimeInterval);
    connect(reclusteringExecutableButton, &QAbstractButton::clicked, this, &PrefGeneral::updateReclusteringExecutable);

    reclusteringExecutableButton->setIcon(QIcon(":/shared-icons/folder-open"));
}
PrefGeneral::~PrefGeneral(){
}

void PrefGeneral::setCrashRecovery(bool use){
    crashRecoveryCheckBox->setChecked(use);
    if(use)
        updateCrashRecoveryTimeInterval(Qt::Checked);
    else
        updateCrashRecoveryTimeInterval(Qt::Unchecked);
}

void PrefGeneral::setCrashRecoveryIndex(int index){crashRecoveryComboBox->setCurrentIndex(index);}

void PrefGeneral::setNbUndo(int nb){undoSpinBox->setValue(nb);}

void PrefGeneral::setBackgroundColor(const QColor& color) {
    backgroundColorButton->setColor(color);
}

void PrefGeneral::setReclusteringExecutable(const QString& executable) {reclusteringExecutableLineEdit->setText(executable);}

void PrefGeneral::setReclusteringArguments(const QString& arguments) {reclusteringArgsLineEdit->setText(arguments);}

void PrefGeneral::setRealignThreshold(double v)  { realignThresholdSpinBox->setValue(v); }
void PrefGeneral::setRealignIterations(int n)    { realignIterationsSpinBox->setValue(n); }
void PrefGeneral::setRealignMaxShift(int n)      { realignMaxShiftSpinBox->setValue(n); }

void PrefGeneral::setDipSplitMinSize(int n)         { dipSplitMinSizeSpinBox->setValue(n); }
void PrefGeneral::setDipSplitBloatFactor(double v)  { dipSplitBloatFactorSpinBox->setValue(v); }
void PrefGeneral::setDipSplitValleyThresh(double v) { dipSplitValleyThreshSpinBox->setValue(v); }

bool PrefGeneral::isCrashRecovery() const{return crashRecoveryCheckBox->isChecked();}

int PrefGeneral::crashRecoveryIntervalIndex() const{return crashRecoveryComboBox->currentIndex();}

int PrefGeneral::getNbUndo() const{return undoSpinBox->value();}

QColor PrefGeneral::getBackgroundColor() const
{
    return backgroundColorButton->color();
}

QString PrefGeneral::getReclusteringExecutable() const{return reclusteringExecutableLineEdit->text();}

QString PrefGeneral::getReclusteringArguments() const{return reclusteringArgsLineEdit->text();}

double PrefGeneral::getRealignThreshold()  const { return realignThresholdSpinBox->value(); }
int    PrefGeneral::getRealignIterations() const { return realignIterationsSpinBox->value(); }
int    PrefGeneral::getRealignMaxShift()   const { return realignMaxShiftSpinBox->value(); }

int    PrefGeneral::getDipSplitMinSize()      const { return dipSplitMinSizeSpinBox->value(); }
double PrefGeneral::getDipSplitBloatFactor()  const { return dipSplitBloatFactorSpinBox->value(); }
double PrefGeneral::getDipSplitValleyThresh()const { return dipSplitValleyThreshSpinBox->value(); }

void PrefGeneral::updateCrashRecoveryTimeInterval(int state){
    if(state == Qt::Checked)
        crashRecoveryComboBox->setEnabled(true);
    else if(state == Qt::Unchecked)
        crashRecoveryComboBox->setEnabled(false);
}

void PrefGeneral::updateReclusteringExecutable(){
    const QString executable = QFileDialog::getOpenFileName(this, tr("Select the Reclustering executable..."));
    if( !executable.isEmpty() )
      setReclusteringExecutable(executable);
}


bool PrefGeneral::getAutoSelectFeatures() const {
    return autoSelectFeaturesCheckBox->isChecked();
}

void PrefGeneral::setAutoSelectFeatures(bool checked) {
    autoSelectFeaturesCheckBox->setChecked(checked);
}

int  PrefGeneral::getAutoSelectNFeatures() const  { return autoSelectNFeaturesSpinBox->value(); }
void PrefGeneral::setAutoSelectNFeatures(int n)   { autoSelectNFeaturesSpinBox->setValue(n); }

bool PrefGeneral::useWhiteColorDuringPrinting() const
{
    return useWhiteColorPrinting->isChecked();
}

void PrefGeneral::setUseWhiteColorDuringPrinting(bool b)
{
    useWhiteColorPrinting->setChecked(b);
}

void PrefGeneral::setMarkerSize(int size) { markerSizeSpinBox->setValue(size); }
int  PrefGeneral::getMarkerSize()   const { return markerSizeSpinBox->value(); }

void PrefGeneral::setSelectionLineWidth(int w) { selectionLineWidthSpinBox->setValue(w); }
int  PrefGeneral::getSelectionLineWidth() const { return selectionLineWidthSpinBox->value(); }

void   PrefGeneral::setAutoscaleMarginPercent(double p) { autoscaleMarginSpinBox->setValue(p); }
double PrefGeneral::getAutoscaleMarginPercent() const   { return autoscaleMarginSpinBox->value(); }


double PrefGeneral::getTemplateThresholdMin() const {
    return templateThresholdMinSpinBox->value();
}
void PrefGeneral::setTemplateThresholdMin(double v) {
    templateThresholdMinSpinBox->setValue(v);
}
double PrefGeneral::getTemplateThresholdMax() const {
    return templateThresholdMaxSpinBox->value();
}
void PrefGeneral::setTemplateThresholdMax(double v) {
    templateThresholdMaxSpinBox->setValue(v);
}
