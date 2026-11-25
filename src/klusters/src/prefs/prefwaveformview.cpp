/***************************************************************************
                          prefwaveformview.cpp  -  description
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
//include files for the application
#include "prefwaveformview.h"
#include <QPushButton>

// include files for QT
#include <QPainter>
#include <QBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QDir>


PrefWaveformView::PrefWaveformView(QWidget *parent,int channelCount,const char *name):
    PrefWaveformViewLayout(parent),nbChannels(channelCount){
    if(channelCount > 0) {
        initializeChannelList();
    } else {
        saveButton->setEnabled(false);
        loadButton->setEnabled(false);
        channelList->setEnabled(false);
    }

    connect(saveButton, &QAbstractButton::clicked, this, &PrefWaveformView::saveChannelOrder);
    connect(loadButton, &QAbstractButton::clicked, this, &PrefWaveformView::loadChannelOrder);
    connect(channelList,&ChannelList::positionsChanged,this,&PrefWaveformView::updateChannelPositions);

    // The quality dot: built here, not in the .ui, like the other groups added since.
    QGroupBox* qualityBox = new QGroupBox(tr("Cluster quality dot"), this);
    qualityBox->setObjectName(QStringLiteral("waveformQualityGroupBox"));
    qualityBox->setToolTip(tr(
        "A dot in the upper right corner of each cluster's waveforms, parents and children alike.\n"
        "Its colour is the variance of the shown spikes about their median waveform, beyond the\n"
        "background at the edges of the window, as a share of the median waveform's energy.\n"
        "0 = the spikes differ from their median by the background only.  Hover the dot for the value.\n"
        "A hint, not a verdict: a second cell, bursts, drift and misalignment all raise it, and two\n"
        "cells of similar shape and amplitude do not."));
    QFormLayout* qform = new QFormLayout(qualityBox);
    qualityEnabledCheck = new QCheckBox(tr("Show the dot"), qualityBox);
    qform->addRow(qualityEnabledCheck);
    const auto level = [qualityBox]() {
        QDoubleSpinBox* s = new QDoubleSpinBox(qualityBox);
        s->setRange(-1.0, 10.0); s->setDecimals(2); s->setSingleStep(0.01);
        return s;
    };
    qualityYellowSpin = level(); qform->addRow(tr("Yellow from"), qualityYellowSpin);
    qualityOrangeSpin = level(); qform->addRow(tr("Orange from"), qualityOrangeSpin);
    qualityRedSpin    = level(); qform->addRow(tr("Red from"), qualityRedSpin);
    qualityYellowSpin->setToolTip(tr("Green below this.  The defaults (0.12, 0.27, 0.52) were measured in a stderiv_C5 space;\n"
                                     "raw (standard) waveforms read lower: about 0.00, 0.05 and 0.20."));
    qualityDiameterSpin = new QSpinBox(qualityBox);
    qualityDiameterSpin->setRange(4, 48); qualityDiameterSpin->setSuffix(tr(" px"));
    qform->addRow(tr("Diameter"), qualityDiameterSpin);
    if (QGridLayout* grid = qobject_cast<QGridLayout*>(layout()))
        grid->addWidget(qualityBox, grid->rowCount(), 0, 1, -1);
    else if (QBoxLayout* rows = qobject_cast<QBoxLayout*>(layout()))
        rows->addWidget(qualityBox);
    setWaveformQuality(Configuration::WaveformQuality());
    connect(qualityEnabledCheck, &QCheckBox::toggled, this, [this](bool on) {
        qualityYellowSpin->setEnabled(on); qualityOrangeSpin->setEnabled(on); qualityRedSpin->setEnabled(on); qualityDiameterSpin->setEnabled(on);
        Q_EMIT waveformQualityChanged();
    });
    for (QDoubleSpinBox* s : {qualityYellowSpin, qualityOrangeSpin, qualityRedSpin})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) { Q_EMIT waveformQualityChanged(); });
    connect(qualityDiameterSpin, &QSpinBox::valueChanged, this, [this](int) { Q_EMIT waveformQualityChanged(); });
}

void PrefWaveformView::setWaveformQuality(const Configuration::WaveformQuality& q)
{
    const QSignalBlocker b1(qualityEnabledCheck), b2(qualityYellowSpin), b3(qualityOrangeSpin), b4(qualityRedSpin), b5(qualityDiameterSpin);
    qualityEnabledCheck->setChecked(q.enabled);
    qualityYellowSpin->setValue(q.yellow);
    qualityOrangeSpin->setValue(q.orange);
    qualityRedSpin->setValue(q.red);
    qualityDiameterSpin->setValue(q.diameterPx);
    qualityYellowSpin->setEnabled(q.enabled); qualityOrangeSpin->setEnabled(q.enabled); qualityRedSpin->setEnabled(q.enabled); qualityDiameterSpin->setEnabled(q.enabled);
}

Configuration::WaveformQuality PrefWaveformView::getWaveformQuality() const
{
    Configuration::WaveformQuality q;
    q.enabled    = qualityEnabledCheck->isChecked();
    q.yellow     = qualityYellowSpin->value();
    q.orange     = qualityOrangeSpin->value();
    q.red        = qualityRedSpin->value();
    q.diameterPx = qualityDiameterSpin->value();
    return q;
}
PrefWaveformView::~PrefWaveformView(){
}

void PrefWaveformView::initializeChannelList(){
    ///Initialize the position of the channels.
    //The first one in the file will be the first one (at the top), second one will be beneath and so on.
    for(int i = 0; i < nbChannels; ++i){
        channelPositions.append(i);
        channelList->addItem(QString::number(i + 1));
    }
}

void PrefWaveformView::resetChannelList(int nb){
    nbChannels = nb;
    channelPositions.clear();
    channelList->clear();

    ///Initialize the position of the channels.
    //The first one in the file will be the first one (at the top), second one will be beneath and so on.
    for(int i = 0; i < nbChannels; ++i){
        channelPositions.append(i);
        channelList->addItem(QString::number(i + 1));
    }

    saveButton->setEnabled(true);
    loadButton->setEnabled(true);
    channelList->setEnabled(true);
}

void PrefWaveformView::saveChannelOrder(){
    const QString url = QFileDialog::getSaveFileName(this, tr("Save as…"),QDir::currentPath(), tr("All files (*)") );
    if(!url.isEmpty()){
        FILE* channelFile = fopen(qPrintable(url),"w");
        if(channelFile == nullptr){
            QMessageBox::critical (this,tr("Error !"),
                                   tr("The selected file could not be opened, possibly because of access permissions !")
                                   );
            return;
        }

        int writeStatus = 0;
        for(int i = 0; i< nbChannels; ++i)
            writeStatus = fprintf(channelFile, "%i\n",channelPositions[i]);

        fclose(channelFile);

        if(writeStatus < 0){
            QMessageBox::critical (this,tr("IO Error !"),
                                   tr("The data could not have been saved due to an writing error.")
                                   );
            return;
        }
    }
}

void PrefWaveformView::loadChannelOrder(){
    QSettings settings;
    const QString url = QFileDialog::getOpenFileName(this, tr("Load File…"), settings.value("CurrentDirectory").toString(),
                                               tr("All files (*)") );

    if(url.isEmpty())
      return;
    QMap<int,int> positions;

    QDir CurrentDir;
    settings.setValue("CurrentDirectory", CurrentDir.absoluteFilePath(url));

    QString tmpChannelFile = url;
    if(!QFile(tmpChannelFile).exists()){
        QMessageBox::critical (this,tr("Error !"),
                               tr("The selected file could not be downloaded !")
                               );
        return;
    }

    QFile channelFile(tmpChannelFile);
    if(!channelFile.open(QIODevice::ReadOnly)){
        QMessageBox::critical (this,tr("Error !"),
                               tr("The selected file could not be opened !")
                               );
        return;
    }

    QTextStream positionStream(&channelFile);
    QString line;
    int channel = 0;
    for(line = positionStream.readLine(); !line.isNull();line = positionStream.readLine()){
        bool ok;
        int position = line.toInt(&ok,10);
        if(!ok){
            QMessageBox::critical (this,tr("Error !"),
                                   tr("The selected file does not have the correct format (list of channels number),\n"
                                      "it can not be used."));

            channelFile.close();

            return;
        }
        //The channels are counted from 0 to nbChannels - 1.
        positions.insert(channel,position);
        channel++;
    }

    channelFile.close();

    if(nbChannels != static_cast<int>(positions.count())){
        QMessageBox::critical (this,tr("Error !"),
                               tr("The number of channels in the selected file does not correspond to the number of channels of the current file !\n"
                                  "This file can not be used for the current document.")
                               );
        return;
    }

    //Update the list with the positions get from the file.
    channelList->clear();
    for(int i = 0; i< nbChannels; ++i){
        channelList->insertItem(positions[i],QString::number(i + 1));
        channelPositions[i] = positions[i];
    }

    //signal to the dialog that channel positions have changed so it can update the Apply button.
    emit positionsChanged();
}

void PrefWaveformView::updateChannelPositions(){
    for(int i = 0; i< nbChannels; ++i){
        QString currentChannel = (channelList->item(i))->text();
        int currentChannelInt = currentChannel.toInt() - 1;
        channelPositions[currentChannelInt] = i;
    }

    //signal to the dialog that channel positions have changed so it can update the Apply button.
    emit positionsChanged();
}


