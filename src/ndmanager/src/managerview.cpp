/***************************************************************************
 *   Copyright (C) 2004 by Lynn Hazan                                      *
 *   lynn.hazan@myrealbox.com                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "managerview.h"
// include files for Qt
#include <QDir> 

#include <QStringList> 

#include <QVBoxLayout>
#include <QList>
#include <QFrame>
#include <QProcess>
#include <QDebug>
#include <QAction>
#include <QMessageBox>



ManagerView::ManagerView(QWidget *parent)
    : QFrame(parent),isUptoDate(true)
{
    frameLayout = new QVBoxLayout(this);
    frameLayout->setSpacing(0);
    frameLayout->setContentsMargins(0, 0, 0, 0);
}


ManagerView::~ManagerView(){
}

void ManagerView::neuroscopeFileChange(int){

}


void ManagerView::stopScript(){
}


void ManagerView::updateDocUrl(const QString &url){
    parameterUrl = url;
}

void ManagerView::updateDocumentInformation(const QString& url,bool isUptoDate){
    parameterUrl = url;
    this->isUptoDate = isUptoDate;
}

