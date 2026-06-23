/***************************************************************************
                          prefdialog.cpp  -  description
                             -------------------
    begin                : Fri Feb 27 2004
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
#include <QComboBox>
#include <QLayout>        // for QVBoxLayout
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>         // for QLabel
#include <QTabWidget>

#include <QMessageBox>

#include <klustersshared/theme.h>


//include files for the application
#include "prefdialog.h"     // class PrefDialog

#include "configuration.h"          // class Configuration and Config()
#include "prefgeneral.h"            // class PrefGeneral
#include "prefdefaults.h" // class prefDefaults
#include "positionproperties.h"
#include "clusterproperties.h"
#include "qhelpviewer.h"
#include "config-neuroscope.h"

/**
  *@author Lynn Hazan
*/

PrefDialog::PrefDialog(QWidget *parent)
    : QPageDialog(parent)
{

    setButtons(Help | Default | Ok | Apply | Cancel);
    setDefaultButton(Ok);
    setFaceType(List);
    setWindowTitle(tr("Preferences"));


    setHelp("settings","neuroscope");
    
    //adding page "General options"
    QWidget * w = new QWidget(this);
    prefGeneral = new PrefGeneral(w);
    QPageWidgetItem *item = new QPageWidgetItem(prefGeneral,tr("General"));
    item->setHeader(tr("NeuroScope Configuration"));
    item->setIcon(QIcon(":/shared-icons/folder-open"));


    addPage(item);


    w = new QWidget(this);
    QTabWidget* tabWidget = new QTabWidget(w);
    //adding "Channels" tab
    prefDefaults = new PrefDefaults();
    tabWidget->addTab(prefDefaults,tr("Channels"));
    //connect(prefDefaults,SIGNAL(changed(bool)),this, SIGNAL(changed(bool)) );
    //adding "Units" tab
    clusterProperties = new ClusterProperties();
    tabWidget->addTab(clusterProperties,tr("Units"));
    //adding "Positions" tab
    positionProperties = new PositionProperties();
    tabWidget->addTab(positionProperties,tr("Positions"));


    item = new QPageWidgetItem(tabWidget,tr("Defaults"));
    item->setHeader(tr("NeuroScope Defaults"));
    item->setIcon(QIcon(":/icons/defaults"));


    addPage(item);

    //adding page "Appearance" (suite-wide light/dark/system theme)
    QWidget* appearance = new QWidget(this);
    QVBoxLayout* appearanceLayout = new QVBoxLayout(appearance);
    QHBoxLayout* themeRow = new QHBoxLayout;
    QLabel* themeLabel = new QLabel(tr("Theme:"), appearance);
    themeCombo = new QComboBox(appearance);
    themeCombo->addItem(neurosuite::themeDisplayName(neurosuite::Theme::System),
                        static_cast<int>(neurosuite::Theme::System));
    themeCombo->addItem(neurosuite::themeDisplayName(neurosuite::Theme::Light),
                        static_cast<int>(neurosuite::Theme::Light));
    themeCombo->addItem(neurosuite::themeDisplayName(neurosuite::Theme::Dark),
                        static_cast<int>(neurosuite::Theme::Dark));
    themeRow->addWidget(themeLabel);
    themeRow->addWidget(themeCombo);
    themeRow->addStretch(1);
    QLabel* themeHint = new QLabel(
        tr("Applies to all NeuroSuite programs. Takes effect when you click Apply or OK."),
        appearance);
    themeHint->setWordWrap(true);
    appearanceLayout->addLayout(themeRow);
    appearanceLayout->addWidget(themeHint);
    appearanceLayout->addStretch(1);

    QPageWidgetItem* appearanceItem = new QPageWidgetItem(appearance, tr("Appearance"));
    appearanceItem->setHeader(tr("Appearance"));
    appearanceItem->setIcon(QIcon(":/shared-icons/folder-open"));
    addPage(appearanceItem);

    // activated fires only on user choice (not programmatic setCurrentIndex),
    // so populating the combo in updateDialog() will not light up Apply.
    connect(themeCombo, &QComboBox::activated, this, &PrefDialog::enableApply);

    // connect interactive widgets and selfmade signals to the enableApply slotDefault
    connect(prefGeneral->headerCheckBox, &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefGeneral->backgroundColorButton,SIGNAL(colorChanged(QColor)),this,SLOT(enableApply()));
    connect(prefGeneral->eventPositionSpinBox, &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefGeneral->clusterPositionSpinBox, &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefGeneral->useWhiteColorPrinting, &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefDefaults->screenGainLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefDefaults->voltageRangeLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefDefaults->amplificationLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefDefaults->nbChannelsLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefDefaults->datSamplingRateLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefDefaults->eegSamplingRateLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefDefaults->offsetLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefDefaults->resolutionComboBox, &QComboBox::activated, this, &PrefDialog::enableApply);
    connect(prefDefaults->traceBackgroundLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(clusterProperties->nbSamplesLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(clusterProperties->peakIndexLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(positionProperties->samplingRateLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(positionProperties->widthLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(positionProperties->heightLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(positionProperties->backgroundLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(positionProperties->rotateComboBox, &QComboBox::activated, this, &PrefDialog::enableApply);
    connect(positionProperties->filpComboBox, &QComboBox::activated, this, &PrefDialog::enableApply);
    connect(positionProperties->checkBoxBackground, &QAbstractButton::clicked, this, &PrefDialog::enableApply);


    connect(this, &QExtendDialog::applyClicked, this, &PrefDialog::slotApply);
    connect(this, &QExtendDialog::defaultClicked, this, &PrefDialog::slotDefault);
    connect(this, &QExtendDialog::helpClicked, this, &PrefDialog::slotHelp);
    applyEnable = false;
}

void PrefDialog::slotHelp()
{
    QHelpViewer *helpDialog = new QHelpViewer(this);
    helpDialog->setHtml(NEUROSCOPE_DOC_PATH + QLatin1String("index.html"));
    helpDialog->setAttribute( Qt::WA_DeleteOnClose );
    helpDialog->show();
}

void PrefDialog::updateDialog() {
    prefGeneral->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());
    prefGeneral->setBackgroundColor(configuration().getBackgroundColor());
    prefGeneral->setPaletteHeaders(configuration().isPaletteHeadersDisplayed());
    prefGeneral->setEventPosition(configuration().getEventPosition());
    prefGeneral->setClusterPosition(configuration().getClusterPosition());
    prefDefaults->setScreenGain(configuration().getScreenGain());
    prefDefaults->setVoltageRange(configuration().getVoltageRange());
    prefDefaults->setAmplification(configuration().getAmplification());
    prefDefaults->setNbChannels(configuration().getNbChannels());
    prefDefaults->setDatSamplingRate(configuration().getDatSamplingRate());
    prefDefaults->setEegSamplingRate(configuration().getEegSamplingRate());
    prefDefaults->setOffset(configuration().getOffset());
    prefDefaults->setResolutionIndex(configuration().getResolutionIndex());
    prefDefaults->setTraceBackgroundImage(configuration().getTraceBackgroundImage());
    clusterProperties->setNbSamples(configuration().getNbSamples());
    clusterProperties->setPeakIndex(configuration().getPeakIndex());
    positionProperties->setSamplingRate(configuration().getVideoSamplingRate());
    positionProperties->setWidth(configuration().getWidth());
    positionProperties->setHeight(configuration().getHeight());
    //Rotation and flip values have to set before calling setBackgroundImage
    positionProperties->setRotation(configuration().getRotation());
    positionProperties->setFlip(configuration().getFlip());
    positionProperties->setBackgroundImage(configuration().getBackgroundImage());
    positionProperties->setPositionsBackground(configuration().getPositionsBackground());

    // Appearance: reflect the current suite-wide theme preference.
    {
        const int idx = themeCombo->findData(
            static_cast<int>(neurosuite::loadThemePreference()));
        themeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    enableButtonApply(false);   // disable apply button
    applyEnable = false;
}


void PrefDialog::updateConfiguration(){
    configuration().setUseWhiteColorDuringPrinting(prefGeneral->useWhiteColorDuringPrinting());
    configuration().setBackgroundColor(prefGeneral->getBackgroundColor());
    configuration().setPaletteHeaders(prefGeneral->isPaletteHeadersDisplayed());
    configuration().setEventPosition(prefGeneral->getEventPosition());
    configuration().setClusterPosition(prefGeneral->getClusterPosition());
    configuration().setScreenGain(prefDefaults->getScreenGain());
    configuration().setVoltageRange(prefDefaults->getVoltageRange());
    configuration().setAmplification(prefDefaults->getAmplification());
    configuration().setNbChannels(prefDefaults->getNbChannels());
    configuration().setDatSamplingRate(prefDefaults->getDatSamplingRate());
    configuration().setEegSamplingRate(prefDefaults->getEegSamplingRate());
    configuration().setOffset(prefDefaults->getOffset());
    configuration().setResolutionIndex(prefDefaults->getResolutionIndex());
    configuration().setTraceBackgroundImage(prefDefaults->getTraceBackgroundImage());
    configuration().setNbSamples(clusterProperties->getNbSamples());
    configuration().setPeakIndex(clusterProperties->getPeakIndex());
    configuration().setSamplingRate(positionProperties->getSamplingRate());
    configuration().setWidth(positionProperties->getWidth());
    configuration().setHeight(positionProperties->getHeight());
    configuration().setBackgroundImage(positionProperties->getBackgroundImage());
    configuration().setRotation(positionProperties->getRotation());
    configuration().setFlip(positionProperties->getFlip());
    configuration().setPositionsBackground(positionProperties->getPositionsBackground());

    // Appearance: persist the suite-wide theme and apply it immediately.
    {
        const neurosuite::Theme t =
            static_cast<neurosuite::Theme>(themeCombo->currentData().toInt());
        neurosuite::saveThemePreference(t);
        neurosuite::applyTheme(t);
    }

    enableButtonApply(false);   // disable apply button
    applyEnable = false;
}


void PrefDialog::slotDefault() {
    if(QMessageBox::question(this, tr("Set default options?"), tr("This will set the default options "
                                                   "in ALL pages of the preferences dialog! Do you wish to continue?"),QMessageBox::RestoreDefaults|QMessageBox::Cancel)==QMessageBox::RestoreDefaults){
        
        prefGeneral->setBackgroundColor(configuration().getBackgroundColorDefault());
        prefGeneral->setPaletteHeaders(configuration().isPaletteHeadersDisplayedDefault());
        prefGeneral->setEventPosition(configuration().getEventPositionDefault());
        prefGeneral->setClusterPosition(configuration().getClusterPositionDefault());
        prefGeneral->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());

        prefDefaults->setScreenGain(configuration().getScreenGainDefault());
        prefDefaults->setVoltageRange(configuration().getVoltageRangeDefault());
        prefDefaults->setAmplification(configuration().getAmplificationDefault());
        prefDefaults->setNbChannels(configuration().getNbChannelsDefault());
        prefDefaults->setDatSamplingRate(configuration().getDatSamplingRateDefault());
        prefDefaults->setEegSamplingRate(configuration().getEegSamplingRateDefault());
        prefDefaults->setOffset(configuration().getOffsetDefault());
        prefDefaults->setTraceBackgroundImage(configuration().getTraceBackgroundImage());
        clusterProperties->setNbSamples(configuration().getNbSamplesDefault());
        clusterProperties->setPeakIndex(configuration().getPeakIndexDefault());
        positionProperties->setSamplingRate(configuration().getVideoSamplingRateDefault());
        positionProperties->setWidth(configuration().getWidthDefault());
        positionProperties->setHeight(configuration().getHeightDefault());
        positionProperties->setBackgroundImage(configuration().getBackgroundImageDefault());
        positionProperties->setRotation(configuration().getRotationDefault());
        positionProperties->setFlip(configuration().getFlipDefault());
        positionProperties->setPositionsBackground(configuration().getPositionsBackgroundDefault());

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
