/***************************************************************************
                          propertiesdialog.cpp  -  description
                             -------------------
    begin                : Sun Feb 29 2004
    copyright            : (C) 2004 by Lynn Hazan
    email                : lynn.hazan.myrealbox.com
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
#include <QLayout>        // for QVBoxLayout
#include <QLabel>         // for QLabel
#include <QMessageBox>
#include <QTabWidget>
#include <QDialog>
#include <QDialogButtonBox>
//include files for the application
#include "propertiesdialog.h"

#include "qhelpviewer.h"
#include "config-neuroscope.h"


PropertiesDialog::PropertiesDialog(QWidget *parent)
    : QDialog(parent)
    ,modified(false)
    ,nbChannelsModified(false)
    ,oops(false)
    ,atStartUp(false)
{
    QVBoxLayout *lay = new QVBoxLayout;
    setLayout(lay);
    mTabWidget = new QTabWidget;
    lay->addWidget(mTabWidget);

    setWindowTitle(tr("File Properties"));
    properties = new Properties;
    mTabWidget->addTab(properties, tr("Channels"));
    clusterProperties = new ClusterProperties;
    mTabWidget->addTab(clusterProperties,tr("Units"));
    positionProperties = new PositionProperties;
    mTabWidget->addTab(positionProperties,tr("Positions"));
    // connect interactive widgets and selfmade signals to the enableApply slotDefault
    connect(properties->nbChannelsLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::channelNbModified);
    connect(properties->screenGainLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(properties->voltageRangeLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(properties->amplificationLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(properties->samplingRateLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(properties->asSamplingRateLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(properties->offsetLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(properties->resolutionComboBox, &QComboBox::activated, this, &PropertiesDialog::propertyModified);
    connect(properties->traceBackgroundLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);


    connect(clusterProperties->nbSamplesLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(clusterProperties->peakIndexLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(positionProperties->samplingRateLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(positionProperties->widthLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(positionProperties->heightLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(positionProperties->backgroundLineEdit, &QLineEdit::textChanged, this, &PropertiesDialog::propertyModified);
    connect(positionProperties->rotateComboBox, &QComboBox::activated, this, &PropertiesDialog::propertyModified);
    connect(positionProperties->filpComboBox, &QComboBox::activated, this, &PropertiesDialog::propertyModified);
    connect(positionProperties->checkBoxBackground, &QAbstractButton::clicked, this, &PropertiesDialog::propertyModified);

    QDialogButtonBox *dialogButton = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::Help);
    lay->addWidget(dialogButton);
    connect(dialogButton, &QDialogButtonBox::accepted, this, &PropertiesDialog::slotVerify);
    connect(dialogButton, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(dialogButton, &QDialogButtonBox::helpRequested, this, &PropertiesDialog::slotHelp);
}
PropertiesDialog::~PropertiesDialog(){
}

void PropertiesDialog::slotHelp()
{
    QHelpViewer *helpDialog = new QHelpViewer(this);
    helpDialog->setHtml(NEUROSCOPE_DOC_PATH + QLatin1String("index.html"));
    helpDialog->setAttribute( Qt::WA_DeleteOnClose );
    helpDialog->show();

}

void PropertiesDialog::updateDialog(int channelNb,double SR, int resolution,int offset,float screenGain,int voltageRange,
                                    int amplification,int nbSamples,int peakIndex,double videoSamplingRate, int width,
                                    int height, const QString& backgroundImage,int rotation,int flip,
                                    double acquisitionSystemSamplingRate,bool positionsBackground,const QString& traceBackgroundImage){
    properties->setScreenGain(screenGain);
    properties->setAcquisitionSystemSamplingRate(acquisitionSystemSamplingRate);
    properties->setVoltageRange(voltageRange);
    properties->setAmplification(amplification);
    properties->setNbChannels(channelNb);
    properties->setSamplingRate(SR);
    properties->setOffset(offset);
    properties->setResolution(resolution);
    properties->setTraceBackgroundImage(traceBackgroundImage);
    clusterProperties->setNbSamples(nbSamples);
    clusterProperties->setPeakIndex(peakIndex);
    positionProperties->setSamplingRate(videoSamplingRate);
    positionProperties->setWidth(width);
    positionProperties->setHeight(height);
    //Rotation and flip values have to be set before calling setBackgroundImage
    positionProperties->setRotation(rotation);
    positionProperties->setFlip(flip);
    positionProperties->setBackgroundImage(backgroundImage);
    positionProperties->setPositionsBackground(positionsBackground);

    nbChannels = channelNb;
}


void PropertiesDialog::slotVerify(){  
    if(nbChannels != properties->getNbChannels() && !atStartUp){
        if(QMessageBox::warning(this, tr("Change the number of channels?"),
                                tr("Changing the number of channels will reset all the groups. Do you wish to continue?"),
                                QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Cancel){
            properties->setNbChannels(nbChannels);
            nbChannelsModified = false;
            oops = true;
        } else {
            modified = true;
        }
    }
    else{
        if(nbChannelsModified)
            modified = true;
    }
    accept();
}

void PropertiesDialog::showPositionPage(){
    mTabWidget->setCurrentWidget(positionProperties);
}
