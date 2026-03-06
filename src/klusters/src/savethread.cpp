/***************************************************************************
                          savethread.cpp  -  description
                             -------------------
    begin                : Fri Oct 17 2003
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
#include "savethread.h"
#include "timer.h"

//QT include files
#include <QApplication>



void SaveThread::save(const QString& url,KlustersDoc* doc,bool isSaveAs){
    // Guard against calling save() while a previous save is still in flight.
    // Writing url/doc/isSaveAs while run() is reading them would be a data race.
    if(isRunning()){
        qWarning() << "[SaveThread::save] called while thread still running — ignored";
        return;
    }
    this->url = url;
    this->doc = doc;
    this->isSaveAs = isSaveAs;
    start();
}

void SaveThread::run(){
    bool status = true;
    int saveStatus = doc->saveDocument(url);
    if(saveStatus != KlustersDoc::OK){
        status = false;
    }
    //Send an event to the application to upload, if need it the file where the cluster information
    // have been store, and update the menu and the status bar.
    SaveDoneEvent* event = saveDoneEvent(status);
    event->setTemporaryFile(doc->temporaryFile());
    QApplication::postEvent(parent,event);
}
