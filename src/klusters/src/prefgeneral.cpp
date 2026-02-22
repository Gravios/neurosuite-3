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

//QT includes
#include <QIcon>
#include <QFileDialog>



PrefGeneral::PrefGeneral(QWidget *parent )
    : PrefGeneralLayout(parent)
{
    connect(crashRecoveryCheckBox,SIGNAL(stateChanged(int)),this,SLOT(updateCrashRecoveryTimeInterval(int)));
    connect(reclusteringExecutableButton,SIGNAL(clicked()),this,SLOT(updateReclusteringExecutable()));
    connect(realignExecutableButton,SIGNAL(clicked()),this,SLOT(updateRealignExecutable()));

    reclusteringExecutableButton->setIcon(QIcon(":/shared-icons/folder-open"));
    realignExecutableButton->setIcon(QIcon(":/shared-icons/folder-open"));
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

void PrefGeneral::setRealignExecutable(const QString& executable) {realignExecutableLineEdit->setText(executable);}

void PrefGeneral::setRealignArguments(const QString& arguments) {realignArgsLineEdit->setText(arguments);}

bool PrefGeneral::isCrashRecovery() const{return crashRecoveryCheckBox->isChecked();}

int PrefGeneral::crashRecoveryIntervalIndex() const{return crashRecoveryComboBox->currentIndex();}

int PrefGeneral::getNbUndo() const{return undoSpinBox->value();}

QColor PrefGeneral::getBackgroundColor() const
{
    return backgroundColorButton->color();
}

QString PrefGeneral::getReclusteringExecutable() const{return reclusteringExecutableLineEdit->text();}

QString PrefGeneral::getReclusteringArguments() const{return reclusteringArgsLineEdit->text();}

QString PrefGeneral::getRealignExecutable() const{return realignExecutableLineEdit->text();}

QString PrefGeneral::getRealignArguments() const{return realignArgsLineEdit->text();}

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

void PrefGeneral::updateRealignExecutable(){
    const QString executable = QFileDialog::getOpenFileName(this, tr("Select the Realignment executable..."));
    if( !executable.isEmpty() )
      setRealignExecutable(executable);
}

bool PrefGeneral::useWhiteColorDuringPrinting() const
{
    return useWhiteColorPrinting->isChecked();
}

void PrefGeneral::setUseWhiteColorDuringPrinting(bool b)
{
    useWhiteColorPrinting->setChecked(b);
}

