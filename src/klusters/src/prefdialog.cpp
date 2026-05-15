/***************************************************************************
                          prefdialog.cpp  -  description
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
// include files for QT
#include <QCheckBox>
#include <QLayout>        // for QVBoxLayout
#include <QLabel>         // for QLabel

#include <QMessageBox>

//include files for the application
#include "prefdialog.h"     // class PrefDialog

#include "configuration.h"          // class Configuration and Config()
#include "prefgeneral.h"            // class PrefGeneral
#include "prefwaveformview.h" // class PrefWaveformView
#include "prefclusterview.h"        // class PrefClusterView
#include "channellist.h"
#include "config-klusters.h"
#include <qhelpviewer.h>

/**
  *@author Lynn Hazan
*/

PrefDialog::PrefDialog(QWidget *parent,int nbChannels)
 : QPageDialog(parent)
{

    setButtons(Help | Default | Ok | Apply | Cancel);
    setDefaultButton(Ok);
    setFaceType(List);
    setWindowTitle(tr("Preferences"));

    setHelp("settings","klusters");
    
    QWidget * w = new QWidget(this);
    prefGeneral = new PrefGeneral(w);
    QPageWidgetItem *item = new QPageWidgetItem(prefGeneral,tr("General"));
    item->setHeader(tr("Klusters General Configuration"));
    item->setIcon(QIcon(":/shared-icons/folder-open"));


    addPage(item);



    //adding page "Cluster view configuration"
    w = new QWidget(this);
    prefclusterView = new PrefClusterView(w);

    item = new QPageWidgetItem(prefclusterView,tr("Cluster view"));
    item->setHeader(tr("Cluster View configuration"));
    item->setIcon(QIcon(":/icons/clusterview"));
    addPage(item);


    //adding page "Waveform view configuration"
    w = new QWidget(this);
    prefWaveformView = new PrefWaveformView(w,nbChannels);

    item = new QPageWidgetItem(prefWaveformView,tr("Waveform view"));
    item->setHeader(tr("Waveform View configuration"));
    item->setIcon(QIcon(":/icons/waveformview"));
    addPage(item);



    // connect interactive widgets and selfmade signals to the enableApply slotDefault
    connect(prefGeneral->crashRecoveryCheckBox, &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefGeneral->crashRecoveryComboBox,&QComboBox::activated,this,&PrefDialog::enableApply);
    connect(prefGeneral->undoSpinBox,&QSpinBox::valueChanged,this,&PrefDialog::enableApply);
    connect(prefGeneral->backgroundColorButton,&QColorButton::colorChanged,this,&PrefDialog::enableApply);
    connect(prefGeneral->reclusteringExecutableLineEdit,&QLineEdit::textChanged,this,&PrefDialog::enableApply);
    //connect(prefGeneral,SIGNAL(reclusteringArgsUpdate()),this,SLOT(enableApply()));
    connect(prefGeneral->reclusteringArgsLineEdit,&QLineEdit::textChanged,this,&PrefDialog::enableApply);
    connect(prefGeneral->markerSizeSpinBox,&QSpinBox::valueChanged,this,&PrefDialog::enableApply);
    connect(prefGeneral->selectionLineWidthSpinBox,&QSpinBox::valueChanged,this,&PrefDialog::enableApply);
    connect(prefGeneral->autoscaleMarginSpinBox,&QDoubleSpinBox::valueChanged,this,&PrefDialog::enableApply);
    connect(prefGeneral->useWhiteColorPrinting, &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefGeneral->autoSelectFeaturesCheckBox, &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefGeneral->autoSelectNFeaturesSpinBox,&QSpinBox::valueChanged,this,&PrefDialog::enableApply);
    // patch76 — mean-subtracted sub-dimensional recluster checkbox
    connect(prefGeneral->reclusterMeanSubtractedSubdimCheckBox,
            &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    // patch79 — auto-show matrices on open checkbox
    connect(prefGeneral->autoShowMatricesOnOpenCheckBox,
            &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefGeneral->realignThresholdSpinBox, &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefGeneral->realignIterationsSpinBox, &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefGeneral->realignMaxShiftSpinBox,   &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefGeneral->dipSplitMinSizeSpinBox,      &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefGeneral->dipSplitBloatFactorSpinBox,  &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefGeneral->dipSplitValleyThreshSpinBox, &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefGeneral->templateThresholdMinSpinBox,&QDoubleSpinBox::valueChanged,this,&PrefDialog::enableApply);
    connect(prefGeneral->templateThresholdMaxSpinBox,&QDoubleSpinBox::valueChanged,this,&PrefDialog::enableApply);
    
    connect(prefclusterView->intervalSpinBox,&QSpinBox::valueChanged,this,&PrefDialog::enableApply);
    connect(prefWaveformView->gainSpinBox,&QSpinBox::valueChanged,this,&PrefDialog::enableApply);
    connect(prefWaveformView,&PrefWaveformView::positionsChanged,this,&PrefDialog::enableApply);


    connect(this, &QExtendDialog::applyClicked, this, &PrefDialog::slotApply);
    connect(this, &QExtendDialog::defaultClicked, this, &PrefDialog::slotDefault);
    connect(this, &QExtendDialog::helpClicked, this, &PrefDialog::slotHelp);

    applyEnable = false;
}

void PrefDialog::slotHelp()
{
    QHelpViewer *helpDialog = new QHelpViewer(this);
    helpDialog->setHtml(KLUSTER_DOC_PATH + QLatin1String("index.html"));
    helpDialog->setAttribute( Qt::WA_DeleteOnClose );
    helpDialog->show();
}

void PrefDialog::updateDialog() {  
  prefGeneral->setCrashRecovery(configuration().isCrashRecovery());
  prefGeneral->setCrashRecoveryIndex(configuration().crashRecoveryIntervalIndex());
  prefGeneral->setNbUndo(configuration().getNbUndo());
  prefGeneral->setBackgroundColor(configuration().getBackgroundColor());
  prefGeneral->setReclusteringExecutable(configuration().getReclusteringExecutable());
  prefGeneral->setReclusteringArguments(configuration().getReclusteringArguments());
  prefGeneral->setRealignThreshold(configuration().getRealignThreshold());
  prefGeneral->setRealignIterations(configuration().getRealignIterations());
  prefGeneral->setRealignMaxShift(configuration().getRealignMaxShift());
  prefGeneral->setDipSplitMinSize(configuration().getDipSplitMinSize());
  prefGeneral->setDipSplitBloatFactor(configuration().getDipSplitBloatFactor());
  prefGeneral->setDipSplitValleyThresh(configuration().getDipSplitValleyThresh());
  prefGeneral->setMarkerSize(configuration().getMarkerSize());
  prefGeneral->setSelectionLineWidth(configuration().getSelectionLineWidth());
  prefGeneral->setAutoscaleMarginPercent(configuration().getAutoscaleMarginPercent());
  prefclusterView->setTimeInterval(configuration().getTimeInterval());
  prefWaveformView->setGain(configuration().getGain());
  prefGeneral->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());
  prefGeneral->setAutoSelectFeatures(configuration().getAutoSelectFeatures());
  prefGeneral->setAutoSelectNFeatures(configuration().getAutoSelectNFeatures());
  prefGeneral->setReclusterMeanSubtractedSubdim(configuration().getReclusterMeanSubtractedSubdim());  // patch76
  prefGeneral->setAutoShowMatricesOnOpen(configuration().getAutoShowMatricesOnOpen());  // patch79
  prefGeneral->setTemplateThresholdMin(configuration().getTemplateThresholdMin());
  prefGeneral->setTemplateThresholdMax(configuration().getTemplateThresholdMax());
  enableButtonApply(false);   // disable apply button
  applyEnable = false;
}
 

void PrefDialog::updateConfiguration(){
  configuration().setCrashRecovery(prefGeneral->isCrashRecovery());
  configuration().setCrashRecoveryIndex(prefGeneral->crashRecoveryIntervalIndex());
  configuration().setNbUndo(prefGeneral->getNbUndo());
  configuration().setBackgroundColor(prefGeneral->getBackgroundColor()); 
  configuration().setReclusteringExecutable(prefGeneral->getReclusteringExecutable());
  configuration().setReclusteringArguments(prefGeneral->getReclusteringArguments());
  configuration().setRealignThreshold(prefGeneral->getRealignThreshold());
  configuration().setRealignIterations(prefGeneral->getRealignIterations());
  configuration().setRealignMaxShift(prefGeneral->getRealignMaxShift());
  configuration().setDipSplitMinSize(prefGeneral->getDipSplitMinSize());
  configuration().setDipSplitBloatFactor(prefGeneral->getDipSplitBloatFactor());
  configuration().setDipSplitValleyThresh(prefGeneral->getDipSplitValleyThresh());
  configuration().setMarkerSize(prefGeneral->getMarkerSize());
  configuration().setSelectionLineWidth(prefGeneral->getSelectionLineWidth());
  configuration().setAutoscaleMarginPercent(prefGeneral->getAutoscaleMarginPercent());
  configuration().setTimeInterval(prefclusterView->getTimeInterval());
  configuration().setGain(prefWaveformView->getGain());
  configuration().setNbChannels(prefWaveformView->getNbChannels());
  configuration().setChannelPositions(prefWaveformView->getChannelPositions()); 
  configuration().setUseWhiteColorDuringPrinting(prefGeneral->useWhiteColorDuringPrinting());
  configuration().setAutoSelectFeatures(prefGeneral->getAutoSelectFeatures());
  configuration().setAutoSelectNFeatures(prefGeneral->getAutoSelectNFeatures());
  configuration().setReclusterMeanSubtractedSubdim(prefGeneral->getReclusterMeanSubtractedSubdim());  // patch76
  configuration().setAutoShowMatricesOnOpen(prefGeneral->getAutoShowMatricesOnOpen());  // patch79
  configuration().setTemplateThresholdMin(prefGeneral->getTemplateThresholdMin());
  configuration().setTemplateThresholdMax(prefGeneral->getTemplateThresholdMax());
  enableButtonApply(false);   // disable apply button
  applyEnable = false;
}


void PrefDialog::slotDefault() {
  if (QMessageBox::question(this, tr("Set default options?"), tr("This will set the default options "
      "in ALL pages of the preferences dialog! Do you wish to continue?"), QMessageBox::RestoreDefaults|QMessageBox::Cancel
      )==QMessageBox::RestoreDefaults){
        
   prefGeneral->setCrashRecovery(configuration().isCrashRecoveryDefault());
   prefGeneral->setCrashRecoveryIndex(configuration().crashRecoveryIntervalIndexDefault());
   prefGeneral->setNbUndo(configuration().getNbUndoDefault());
   prefGeneral->setBackgroundColor(configuration().getBackgroundColorDefault());
   prefGeneral->setReclusteringExecutable(configuration().getReclusteringExecutableDefault());
   prefGeneral->setReclusteringArguments(configuration().getReclusteringArgumentsDefault());
   prefGeneral->setRealignThreshold(configuration().getRealignThresholdDefault());
   prefGeneral->setRealignIterations(configuration().getRealignIterationsDefault());
   prefGeneral->setRealignMaxShift(configuration().getRealignMaxShiftDefault());
   prefGeneral->setDipSplitMinSize(configuration().getDipSplitMinSizeDefault());
   prefGeneral->setDipSplitBloatFactor(configuration().getDipSplitBloatFactorDefault());
   prefGeneral->setDipSplitValleyThresh(configuration().getDipSplitValleyThreshDefault());
   prefGeneral->setMarkerSize(configuration().getMarkerSizeDefault());
   prefGeneral->setSelectionLineWidth(configuration().getSelectionLineWidthDefault());
   prefGeneral->setAutoscaleMarginPercent(configuration().getAutoscaleMarginPercentDefault());
   prefGeneral->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());
   prefGeneral->setAutoSelectFeatures(configuration().getAutoSelectFeaturesDefault());
   prefGeneral->setAutoSelectNFeatures(configuration().getAutoSelectNFeaturesDefault());
   prefGeneral->setReclusterMeanSubtractedSubdim(configuration().getReclusterMeanSubtractedSubdimDefault());  // patch76
   prefGeneral->setAutoShowMatricesOnOpen(configuration().getAutoShowMatricesOnOpenDefault());  // patch79

   prefclusterView->setTimeInterval(configuration().getTimeIntervalDefault());
   prefWaveformView->setGain(configuration().getGainDefault());
   prefWaveformView->resetChannelList(configuration().getNbChannels());
   
   enableApply();   // enable apply button
  }
}


void PrefDialog::slotApply() {
  updateConfiguration();      // transfer settings to configuration object
  emit settingsChanged();     // apply the preferences    
  enableButtonApply(false);   // disable apply button again
}


void PrefDialog::enableApply() {
    enableButtonApply(true);   // enable apply button
    applyEnable = true;
}

void PrefDialog::syncAutoNFeatures(int n){
    prefGeneral->setAutoSelectNFeatures(n);
}

void PrefDialog::resetChannelList(int nbChannels){
  prefWaveformView->resetChannelList(nbChannels);
}

void PrefDialog::enableChannelSettings(bool state){
  prefWaveformView->enableChannelSettings(state);
}
    

