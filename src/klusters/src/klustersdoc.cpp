#include <algorithm>
#include <functional>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <stdint.h>
/***************************************************************************
                          klustersdoc.cpp  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
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

// include files for Qt
#include <QDir>
#include <QFile>
#include <QWidget>
#include <QStringList>
#include <QString>
#include <QTimer>
#include <QDateTime>
#include <QApplication>

#include <QList>

#include <QEvent>
#include <QMessageBox>
#include <QDebug>
#include <QAction>
#include <QUrl>
#include <QRegularExpression>
#include <QTextStream>

// application specific includes
#include "processwidget.h"
#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "clusterview.h"
#include "klustersdoc.h"
#include "clusterPalette.h"
#include "types.h"
#include "autosavethread.h"
#include "parameterxmlmodifier.h"
#include "parameteryamlmodifier.h"
#include "parameteryamlreader.h"
#include "clusteruserinformation.h"

//C, C++ include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <math.h>
#include <climits>

#include "timer.h"

extern int nbUndo;

KlustersDoc::KlustersDoc(QWidget* parent,ClusterPalette& clusterPalette,bool autoSave,int savingInterval)
    : clusterColorListUndoList(),clusterColorListRedoList(),modified(false),docUrl(),parent(parent),clusterPalette(clusterPalette),
    addedClustersUndoList(),addedClustersRedoList(),modifiedClustersUndoList(),modifiedClustersRedoList()
  ,autoSave(autoSave),savingInterval(savingInterval),tracesProvider(0L),clustersProvider(0L),channelColorList(0L)
{
    viewList = new QList<KlustersView*>();
    clusterColorList = 0L;
    addedClusters = 0L;
    modifiedClusters = 0L;
    deletedClusters = 0L;
    endAutoSaving = false;
    autoSaveThread = 0L;
}

KlustersDoc::~KlustersDoc(){
    qDebug() << "~KlustersDoc()";

    delete viewList;

    if(clusterColorList != 0L){
        delete clusteringData;
        delete clusterColorList;
        delete addedClusters;
        delete modifiedClusters;
        delete deletedClusters;
    }

    //If an autoSaveThread exists and has not finish, wait until it is done
    if(autoSave && autoSaveThread != 0L){
        if(!autoSaveThread->isRunning()){
            autoSaveThread->removeTmpFile();
            delete autoSaveThread;
            autoSaveThread = 0L;
        }
        else{
            endAutoSaving = true;
            while(!autoSaveThread->wait()){};
            //Wait that the customEvent has process the AutoSaveEvent and deleted the autoSaveThread
            while(autoSaveThread != 0L){};
        }
    }
}

void KlustersDoc::addView(KlustersView *view)
{
    viewList->append(view);
}

void KlustersDoc::removeView(KlustersView *view){
    viewList->removeAll(view);
}


bool KlustersDoc::isLastView() {
    return ((int) viewList->count() == 1);
}


void KlustersDoc::updateAllViews(KlustersView *sender){
    for(int i =0; i<viewList->count();++i)
    {
        KlustersView *view = viewList->at(i);
        view->update(sender);
    }

}

void KlustersDoc::refreshAllViews()
{
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* view = viewList->at(i);
        view->update(nullptr);         // repaint scatter + waveform
        view->updateViewContents();    // fires updateDrawing() → askForCorrelograms()
    }
}

void KlustersDoc::forceClusterRefresh(int clusterId)
{
    for (int i = 0; i < viewList->count(); ++i)
        viewList->at(i)->forceClusterRefresh(clusterId);
}

bool KlustersDoc::canCloseDocument(KlustersApp* mainWindow,const QString& callingMethod){
    //Before closing, make sure that there is no thread running.
    //Loop on all the views, moving to the next one when the current one has no more thread running.
    bool threadRunning = false;

    for(int i =0; i<viewList->count();++i)
    {
        KlustersView *view = viewList->at(i);
        threadRunning = view->isThreadsRunning();
        if(threadRunning)
            break;
    }

    if(threadRunning || !stopAutoSaving(true)){
        //Send an event to the klusters (main window) to let it know that the document can not
        //be close because some thread are still running.
        CloseDocumentEvent* event = getCloseDocumentEvent(callingMethod);
        QApplication::postEvent(mainWindow,event);
        return false;
    }
    else
        return true;
}

void KlustersDoc::closeDocument(){
    //If a document has been open reset the members
    viewList->clear();
    docUrl = QString();
    baseName.clear();
    xmlParameterFile.clear();
    qDeleteAll(clusterColorListUndoList);
    clusterColorListUndoList.clear();
    qDeleteAll(clusterColorListRedoList);
    clusterColorListRedoList.clear();
    qDeleteAll(addedClustersUndoList);
    addedClustersUndoList.clear();
    qDeleteAll(addedClustersRedoList);
    addedClustersRedoList.clear();
    qDeleteAll(modifiedClustersUndoList);
    modifiedClustersUndoList.clear();
    qDeleteAll(modifiedClustersUndoList);
    modifiedClustersRedoList.clear();
    clusterIdsNewOldMap.clear();
    clusterIdsOldNewMap.clear();


    if(clusterColorList != 0L){
        delete clusteringData;
        clusteringData = 0L;
        delete clusterColorList;
        clusterColorList = 0L;
        delete addedClusters;
        addedClusters = 0L;
        delete modifiedClusters;
        modifiedClusters = 0L;
    }
    //Remove the temp files if any
    tmpCluFile.clear();
    tmpSpikeFile.clear();

    //Variables link to TraceView
    if(channelColorList != 0L){
        delete channelColorList;
        channelColorList = 0L;
        delete tracesProvider;
        tracesProvider = 0L;
        delete clustersProvider;
        clustersProvider = 0L;
    }

    displayChannelsGroups.clear();
    channelsSpikeGroups.clear();
    displayGroupsChannels.clear();
    spikeGroupsChannels.clear();
    displayGroupsClusterFile.clear();
    gain = 0;
    acquisitionGain = 0;
}


bool KlustersDoc::importDocument(const QString &url, const char *format ){
    bool returnValue = true;

    //1 - Get the base name of the file
    //2 - load the config information: Parse the XML config file, initialize clusteringData (loadConfigFromNewFormat())
    //3 - load the spikes, clusters, time and PCA information (loadDataFromNewFormat())
    return  returnValue;
}

int KlustersDoc::openDocument(const QString &url,QString& errorInformation, const char *format ){
    //1 - Get the base name of the file
    //2 - load the config information: read the different files, initialize clusteringData (loadConfigFromNewFormat())
    //3 - load the spikes, clusters, time and PCA information (loadDataFromNewFormat())

    //Initialize the members specific to a document
    clusteringData = new Data();
    clusterColorList = new ItemColors();
    addedClusters = new QList<int>();
    modifiedClusters = new QList<int>();
    modified = false;

    //Store the baseName for future use
    QFileInfo urlFileInfo(url);

    QString fileName = urlFileInfo.fileName();
    const QStringList fileParts = fileName.split(".", Qt::SkipEmptyParts);
    if(fileParts.count() < 3)
        return INCORRECT_FILE;
    baseName = fileParts[0];

    for(uint i = 1;i < fileParts.count()-2; ++i)
        baseName += "." + fileParts[i];

    electrodeGroupID = fileParts[fileParts.count()-1];

    //Create the files url to open (baseName.spk.x,baseName.clu.x,baseName.fet.x,baseName.par.x,baseName.par and baseName.xml)
    //and store the url (corresponding to the cluster file). If the xml format parameter file does not exist, the parameter files
    // baseName.par.x,baseName.par will be used otherwise the baseName.xml will be used.

    QString spkFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName +".spk."+ electrodeGroupID;

    QString cluFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName +".clu."+ electrodeGroupID;
    docUrl = cluFileUrl;

    cluFileSaveUrl = urlFileInfo.absolutePath() + QDir::separator() + "." + urlFileInfo.fileName() + ".autosave";


    QString fetFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName +".fet."+ electrodeGroupID;
    //Parameter files
    QString xmlParFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName +".xml";
    xmlParameterFile = xmlParFileUrl;

    // Also check for a YAML parameter file (takes precedence over .xml if both exist)
    const QString yamlParFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName + ".yaml";
    if (QFileInfo(yamlParFileUrl).exists()) {
        xmlParFileUrl  = yamlParFileUrl;
        xmlParameterFile = yamlParFileUrl;
    }



    QString parXFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName +".par."+ electrodeGroupID;


    QString parFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName +".par";


    //Download the spike and fet files in temp files if necessary
    if(!QFile(spkFileUrl).exists())
        return SPK_DOWNLOAD_ERROR;
    QString tmpSpikeFile = spkFileUrl;


    QFile fetFile(fetFileUrl);
    if(!fetFile.exists())
        return FET_DOWNLOAD_ERROR;
    //Open the the spike and fet files. Only the fet file will be loaded the spike file
    // will be used on the fly when waveforms will need to be drawn.
    //The biggest files are open in a C FILE to enable a quick access, the others (parameter files) are open in a QFile
    if(!fetFile.open(QIODevice::ReadOnly)){
        return OPEN_ERROR;
    }

    //The length of the spike file is used to determine the number of spikes.
    QFile spikeFile(tmpSpikeFile);

    if(!spikeFile.open(QIODevice::ReadOnly)){
        fetFile.close();
        return OPEN_ERROR;
    }
    long spkFileLength = spikeFile.size();
    spikeFile.close();

    bool isXmlParExist = false;
    QString tmpXmlParFile;
    QString tmpParXFile;
    QString tmpParFile;
    QFileInfo xmlParFileInfo(xmlParFileUrl);
    QFile xmlParFile;
    QFile parXFile;
    QFile parFile;
    if(xmlParFileInfo.exists()){
        tmpXmlParFile = xmlParFileUrl;
        isXmlParExist = true;
        //Check if the generic parameter file also exist, if so, warn the user that the xml format parameter file will be used.
        QFileInfo parFileInfo(parFileUrl);
        if(parFileInfo.exists()){
            QApplication::restoreOverrideCursor();
            QMessageBox::information(0, tr("Warning!"), tr("Two parameter files were found, %1 and %2. The parameter file %3 will be used.").arg(xmlParFileUrl).arg(parFileUrl).arg(xmlParFileUrl));
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        }
        xmlParFile.setFileName(tmpXmlParFile);
        if(!xmlParFile.open(QIODevice::ReadOnly)){
            fetFile.close();
            return OPEN_ERROR;
        }
    }
    else{
        if(!QFile(parXFileUrl).exists()) {
            fetFile.close();
            return PARX_DOWNLOAD_ERROR;
        }
        tmpParXFile = parXFileUrl;
        parXFile.setFileName(tmpParXFile);
        if(!parXFile.open(QIODevice::ReadOnly)){
            fetFile.close();
            return OPEN_ERROR;
        }
        if(!QFile(parFileUrl).exists()) {
            fetFile.close();
            parXFile.close();
            return PAR_DOWNLOAD_ERROR;
        }
        tmpParFile = parFileUrl;
        parFile.setFileName(tmpParFile);
        if(!parFile.open(QIODevice::ReadOnly)){
            fetFile.close();
            parXFile.close();
            return OPEN_ERROR;
        }
    }

    //If a crashRecoveryFile exits, check if it is newer than the clu file, if so
    //ask the user if he wants to use that one to replace the clu file.
    QFileInfo crashFileInfo(cluFileSaveUrl);
    if(crashFileInfo.exists()){
        QFileInfo cluFileInfo(cluFileUrl);
        if((cluFileInfo.exists() && crashFileInfo.lastModified() > cluFileInfo.lastModified()) ||
                !cluFileInfo.exists()){
            QApplication::restoreOverrideCursor();
            switch(QMessageBox::question(0, tr("More recent cluster file found"), tr("A more recent copy of the cluster file (a rescue file) was found on the disk. This indicates that Klusters crashed while editing these data during a previous session.\n"
                                                   "Do you wish to use the newer copy (The old copy will be saved under another name)?"),QMessageBox::Yes|QMessageBox::No))
            {
            case QMessageBox::Yes: {
                QDir dir(crashFileInfo.dir());
                const QString cluName = cluFileInfo.fileName();
                bool renameStatus;
                if(cluFileInfo.exists()){
                    const QString newName = cluFileInfo.fileName()+ QLatin1String(".") + cluFileInfo.lastModified().toString("MM.dd.yyyy.hh.mm");
                    renameStatus = dir.rename(cluName,newName);
                }
                renameStatus = dir.rename(crashFileInfo.fileName(),cluName);
                if(!renameStatus)
                    QMessageBox::critical(0, tr("I/O Error !"),tr(
                                              "It appears that the rescue file cannot be renamed (possibly because of insufficient file access permissions).\n"
                                              "The rescue file will thus not be used."));
                break;
            }
            case QMessageBox::No:
                break;
            default:
                break;
            }
        }
    }

    //Treat the cluster file separately as it can be empty
    QFile cluFile(cluFileUrl);
    if(cluFile.exists()){
        tmpCluFile = cluFileUrl;
        if(!cluFile.open(QIODevice::ReadOnly)) {
            if(isXmlParExist){
                xmlParFile.close();
            }
            else{
                parXFile.close();
                parFile.close();
            }
            fetFile.close();
            return OPEN_ERROR;
        }

        //Initialize the data
        if(isXmlParExist){
            if(!clusteringData->initialize(fetFile,cluFile,spkFileLength,tmpSpikeFile,xmlParFile,electrodeGroupID.toInt(),errorInformation)){
                //close the files
                xmlParFile.close();
                fetFile.close();
                cluFile.close();
                return INCORRECT_CONTENT;
            }
            xmlParFile.close();
            fetFile.close();
            cluFile.close();
        }
        else{
            if(!clusteringData->initialize(fetFile,cluFile,spkFileLength,tmpSpikeFile,parXFile,parFile,errorInformation)){
                //close the files
                parXFile.close();
                parFile.close();
                fetFile.close();
                cluFile.close();
                return INCORRECT_CONTENT;
            }
            parXFile.close();
            parFile.close();
            fetFile.close();
            cluFile.close();
        }
    }//end //the cluster file exists
    //the cluster file does not exist
    else{
        tmpCluFile =  cluFileUrl;

        //Initialize the data
        if(isXmlParExist){
            if(!clusteringData->initialize(fetFile,spkFileLength,tmpSpikeFile,xmlParFile,electrodeGroupID.toInt(),errorInformation)){
                //close the files
                xmlParFile.close();
                fetFile.close();
                return INCORRECT_CONTENT;
            }
            xmlParFile.close();
            fetFile.close();
        }
        else{
            if(!clusteringData->initialize(fetFile,spkFileLength,tmpSpikeFile,parXFile,parFile,errorInformation)){
                //close the files
                parXFile.close();
                parFile.close();
                fetFile.close();

                return INCORRECT_CONTENT;
            }
            //close the files
            parXFile.close();
            parFile.close();
            fetFile.close();
        }
    }//end the cluster file does not exist

    //Constructs the clusterColorList
    QList<dataType> clusterList = clusteringData->clusterIds();
    QList<dataType>::iterator it;
    for(it = clusterList.begin(); it != clusterList.end(); ++it){
        QColor color;
        if(*it == 1)
            color.setHsv(0,0,220);//Cluster 1 is always gray
        else
            color.setHsv(static_cast<int>(fmod(static_cast<double>(*it)*7,36))*10,255,255);
        clusterColorList->append(static_cast<int>(*it),color);
    }


    //If ask create a thread for the auto saving of the document.
    if(autoSave){
        qDebug()<<"autoSave = true in openDoc";
        endAutoSaving = false;
        autoSaveThread = new AutoSaveThread(*clusteringData,this,cluFileSaveUrl);
        autoSaveThread->start();
    }

    // Establish the four permanent pending files so the originals are never
    // touched during a session.  spkFileName and tmpCluFile are redirected
    // to the pending paths; they stay there for the whole document lifetime.
    m_origSpkPath = spkFileUrl;
    m_origResPath = urlFileInfo.absolutePath() + QDir::separator()
                    + baseName + ".res." + electrodeGroupID;
    m_origFetPath = fetFileUrl;
    // clu original == docUrl (set above); clu pending set in initPendingFiles.
    if (!initPendingFiles()) {
        qWarning() << "[openDocument] could not create pending files";
    }

    return OK;
}

void KlustersDoc::updateAutoSavingInterval(int interval){
    savingInterval = interval;
    endAutoSaving = false;
    if(!autoSave){
        autoSave = true;
        autoSaveThread = new AutoSaveThread(*clusteringData,this,cluFileSaveUrl);
        autoSaveThread->start();
    }
}

bool KlustersDoc::stopAutoSaving(bool currentDocument){
    if(autoSave && autoSaveThread != 0L){
        if(!autoSaveThread->isRunning()){
            autoSaveThread->removeTmpFile();
            delete autoSaveThread;
            autoSaveThread = 0L;
            if(!currentDocument) autoSave = false;
            endAutoSaving = true;
            return true;
        }
        else{
            endAutoSaving = true;
            return false;
        }
    }
    else{
        endAutoSaving = true;
        if(!currentDocument) autoSave = false;
        return true;
    }
}

void KlustersDoc::launchAutoSave(){
    if(!endAutoSaving)autoSaveThread->start();
}

void KlustersDoc::customEvent(QEvent *event){
    //The autoSaveThread has finish, it can be delete.
    if(event->type() == QEvent::User + 500){
        if(endAutoSaving){
            if(autoSaveThread != 0L){
                autoSaveThread->removeTmpFile();
                delete autoSaveThread;
                autoSaveThread = 0L;
            }
        }
        else{
            if(((AutoSaveThread::AutoSaveEvent*)event)->isIOerror())
                QMessageBox::critical(0,tr("I/O Error !"),tr(
                                          "In order to protect your work in case of a crash, Klusters periodically saves a hidden copy of the cluster file"
                                          " in the directory where your files are located (this temporary rescue file is removed when you quit the application).\n"
                                          "However, it now appears that this rescue file cannot be created (possibly because of insufficient file access permissions).\n"
                                          "This feature will thus be disabled for the current session.") );
            else
                //upload the temp file, this can not be done asynchronously.
                //wait savingInterval before starting the autoSaveThread again.
                QTimer::singleShot(savingInterval*60000,this, SLOT(launchAutoSave()));
        }
    }
}

int KlustersDoc::saveDocument(const QString& saveUrl, const char *format /*=0*/){

    // Determine whether this is a Save (same URL) or SaveAs (new URL).
    const bool isSaveAs = (docUrl != saveUrl);

    // For a regular Save:  write clu to the pending clu file (crash-safe).
    // For a SaveAs:        write directly to the new URL (no pending for it).
    const QString cluWritePath = isSaveAs ? saveUrl : m_pendingCluPath;

    //Open the clu file in write mode
    FILE* cluFile = fopen(cluWritePath.toLatin1(),"wb");
    if(cluFile == NULL){
        return OPEN_ERROR;
    }

    if(!clusteringData->saveClusters(cluFile)){
        return SAVE_ERROR;
    }

    //close the file
    fclose(cluFile);

    // For SaveAs: update doc URL and derived paths before committing.
    if(isSaveAs){
        docUrl = saveUrl;
        QFileInfo docUrlFileInfo(docUrl);
        QString fileName = docUrlFileInfo.fileName();
        const QStringList fileParts = fileName.split(".", Qt::SkipEmptyParts);
        baseName = fileParts.first();
        if(fileParts.count() > 2)  {
            for(uint i = 1;i < fileParts.count()-2; ++i){
                baseName += "." + fileParts.at(i);
            }
        }
        if(fileParts.count() < 3)
            electrodeGroupID.clear();
        else
            electrodeGroupID = fileParts.at(fileParts.count()-1);

        QString xmlParFileUrl = docUrlFileInfo.absoluteFilePath() + QDir::separator()+ baseName +".xml";
        xmlParameterFile = xmlParFileUrl;
    }

    //Save the cluster user information if the xmlParameterFile exists
    //NB : for the moment, the specific errors are not return to the user, only a generic message (document could not be saved).
    if(clusteringData->isTraceViewVariablesAvailable()){
        //Save the document information
        qDebug()<<" xmlParameterFile"<<xmlParameterFile;
        QFileInfo parFileInfo = QFileInfo(xmlParameterFile);

        //Check that the file is writable
        if(!parFileInfo.isWritable()) {
            return NOT_WRITABLE;
        }

        QMap<int,ClusterUserInformation> clusterUserInformationMap = QMap<int,ClusterUserInformation>();
        clusteringData->getClusterUserInformation(electrodeGroupID.toInt(),clusterUserInformationMap);

        const bool saveAsYaml = xmlParameterFile.endsWith(QLatin1String(".yaml"), Qt::CaseInsensitive)
                             || xmlParameterFile.endsWith(QLatin1String(".yml"),  Qt::CaseInsensitive);

        if (saveAsYaml) {
            // Build the merged units map: read existing units, overwrite the
            // units for the current electrode group, then write all back.
            ParameterYamlModifier yamlMod;
            if (!yamlMod.parseFile(xmlParameterFile))
                return PARSE_ERROR;

            // Read existing units (all groups)
            QMap<int,QStringList> allUnits;
            {
                ParameterYamlReader reader;
                if (reader.parseFile(xmlParameterFile))
                    reader.getUnits(allUnits);
            }

            // Remove existing entries for this electrode group
            const int pGroup = electrodeGroupID.toInt();
            QMap<int,QStringList> filtered;
            for (auto it = allUnits.cbegin(); it != allUnits.cend(); ++it) {
                if (it.value().size() >= 1 && it.value()[0].toInt() != pGroup)
                    filtered.insert(it.key(), it.value());
            }

            // Add updated entries for this electrode group
            for (auto it = clusterUserInformationMap.cbegin();
                 it != clusterUserInformationMap.cend(); ++it) {
                const ClusterUserInformation& cui = it.value();
                QStringList row;
                row << QString::number(cui.getGroup())
                    << QString::number(cui.getCluster())
                    << cui.getStructure()
                    << cui.getType()
                    << cui.getId()
                    << cui.getQuality()
                    << cui.getNotes();
                filtered.insert(it.key(), row);
            }

            if (!yamlMod.setUnitsInformation(filtered))
                return CREATION_ERROR;
            if (!yamlMod.writeToFile(xmlParameterFile))
                return CREATION_ERROR;
        } else {
            ParameterXmlModifier parameterModifier = ParameterXmlModifier();
            bool status = parameterModifier.parseFile(xmlParameterFile);
            if(!status)
                return PARSE_ERROR;

            status = parameterModifier.setClusterUserInformation(electrodeGroupID.toInt(),clusterUserInformationMap);
            if(!status)
                return CREATION_ERROR;

            status = parameterModifier.writeTofile(xmlParameterFile);
            if(!status)
                return CREATION_ERROR;
        }
    }

    // Commit pending files → originals, then re-seed pending from fresh originals.
    // For SaveAs, update the original paths first so commitAndRenewPending()
    // copies to the correct new location.
    if (isSaveAs) {
        QFileInfo newInfo(docUrl);
        m_origSpkPath = newInfo.absolutePath() + QDir::separator()
                        + baseName + ".spk." + electrodeGroupID;
        m_origResPath = newInfo.absolutePath() + QDir::separator()
                        + baseName + ".res." + electrodeGroupID;
        m_origFetPath = newInfo.absolutePath() + QDir::separator()
                        + baseName + ".fet." + electrodeGroupID;
    }
    commitAndRenewPending();

    modified=false;
    return OK;
}


bool KlustersDoc::canCloseView(){
    bool returnValue = false;
    if(isModified()){
        QString saveURL;
        switch(QMessageBox::question(0, url(),tr("The current file has been modified.\n"
                                                 "Do you want to save it?"),QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel))
        {
        case QMessageBox::Yes:
            saveURL=url();
            int saveStatus;
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            saveStatus = saveDocument(saveURL);
            QApplication::restoreOverrideCursor();
            if(saveStatus != OK){
                switch(QMessageBox::question(0, tr("I/O Error !"),tr("Could not save the current document !\n"
                                                                     "Close anyway ?"),QMessageBox::No|QMessageBox::Yes))
                {
                case QMessageBox::Yes:
                    returnValue = true;
                    modified = false;
                    break;
                case QMessageBox::No:
                    returnValue = false;
                    break;
                default:
                    break;
                }
            }
            else{
                returnValue = true;
                modified = false;
            }
            break;
        case QMessageBox::No:
            returnValue = true;
            modified = false;
            break;
        case QMessageBox::Cancel:
            returnValue = false;
            break;
        default:
            returnValue = false;
            break;
        }
    }
    else
        returnValue = true;

    return returnValue;
}

QString KlustersDoc::documentName() const{
    QFileInfo docUrlFileInfo(docUrl);
    return docUrlFileInfo.absolutePath() + QDir::separator() + baseName + "-" + electrodeGroupID;
}

QString KlustersDoc::documentBaseName() const{
    return baseName;
}

QString KlustersDoc::documentDirectory() const {
    QFileInfo docUrlFileInfo(docUrl);
    return docUrlFileInfo.absolutePath();
}

void KlustersDoc::setGain(int acquisitionGain){
    //Notify all the views of the modification
    for(int i =0; i<viewList->count();++i) {
        KlustersView *view = viewList->at(i);
        view->setGain(acquisitionGain);
    }

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::setBackgroundColor(const QColor &backgroundColor){
    //Notify all the views of the modification
    for(int i =0; i<viewList->count();++i) {
        KlustersView *view = viewList->at(i);
        view->updateBackgroundColor(backgroundColor);
    }

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::setMarkerSize(int size){
    for(int i = 0; i < viewList->count(); ++i){
        const QList<ViewWidget*>& views = viewList->at(i)->getViewList();
        for(ViewWidget* w : views){
            ClusterView* cv = qobject_cast<ClusterView*>(w);
            if(cv) cv->setPointSize(size);
        }
    }
}

void KlustersDoc::setSelectionLineWidth(int w){
    for(int i = 0; i < viewList->count(); ++i){
        const QList<ViewWidget*>& views = viewList->at(i)->getViewList();
        for(ViewWidget* vw : views){
            ClusterView* cv = qobject_cast<ClusterView*>(vw);
            if(cv) cv->setSelectionLineWidth(w);
        }
    }
}

void KlustersDoc::setTimeStepInSecond(int step){
    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //Notify all the views of the modification
    for(int i =0; i<viewList->count();++i){
        KlustersView *view = viewList->at(i);
        if(view != activeView) view->setTimeStepInSecond(step,false);
        else view->setTimeStepInSecond(step,true);
    }

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::setChannelPositions(QList<int>& positions){
    //Notify all the views of the modification

    for(int i =0; i<viewList->count();++i) {
        KlustersView *view = viewList->at(i);
        view->setChannelPositions(positions);
    }

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::singleColorUpdate(int clusterId,KlustersView& activeView){
    //Notify all the views of the modification

    for(int i =0; i<viewList->count();++i)
    {
        KlustersView *view = viewList->at(i);
        if(view != &activeView) view->singleColorUpdate(clusterId,false);
        else view->singleColorUpdate(clusterId,true);
    }

    //Ask the active view to take the modification into account immediately
    activeView.showAllWidgets();
}


void KlustersDoc::shownClustersUpdate(QList<int> clustersToShow,KlustersView& activeView){
    if(clusterColorList->isColorChanged()){
        //Notify all the views of the modification

        for(int i =0; i<viewList->count();++i)
        {
            KlustersView *view = viewList->at(i);
            if(view != &activeView) view->updateColors(false);
            else view->updateColors(true);
        }

        //Reset the color status in clusterColors
        clusterColorList->resetAllColorStatus();

        //Update the palette of clusters
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);
    }

    //The new selection of clusters only means for the active view
    activeView.shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView.updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::shownClustersUpdate(QList<int> clustersToShow){
    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::shownClustersUpdate(QList<int> clustersToShow,QList<int> previousSelectedClusterPairs){
    //Get the clusters currently selected
    QList<int> currentShownClusters = clusterPalette.selectedClusters();

    //Add the clusters which were shown and not part of the previous selected cluster pairs
    QList<int>::iterator clustersToAdd;
    for(clustersToAdd = currentShownClusters.begin(); clustersToAdd != currentShownClusters.end(); ++clustersToAdd )
        if(!previousSelectedClusterPairs.contains(*clustersToAdd)) clustersToShow.append(*clustersToAdd);

    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::showAllClustersExcept(QList<int> clustersToHide){

    QList<dataType> clusterList = clusteringData->clusterIds();
    QList<int> clustersToShow;

    QList<dataType>::iterator clustersToAdd;
    for(clustersToAdd = clusterList.begin(); clustersToAdd != clusterList.end(); ++clustersToAdd ){
        if(!clustersToHide.contains(static_cast<int>(*clustersToAdd))) clustersToShow.append(static_cast<int>(*clustersToAdd));
    }

    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::addClustersToActiveView(QList<int> clustersToShow){
    //Get the clusters currently selected
    QList<int> currentShownClusters = clusterPalette.selectedClusters();

    QList<int>::iterator clustersToAdd;
    for(clustersToAdd = currentShownClusters.begin(); clustersToAdd != currentShownClusters.end(); ++clustersToAdd )
        clustersToShow.append(*clustersToAdd);

    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::groupClusters(QList<int> clustersToGroup,KlustersView& activeView){
    //Call data to group the clusters
    float newClusterId = clusteringData->groupClusters(clustersToGroup);
    int newClusterIdint = static_cast<int>(newClusterId);

    //Prepare the undo
    prepareUndo(newClusterIdint,clustersToGroup);

    //Add the cluster in clusterColors.
    QColor color;
    color.setHsv(static_cast<int>(fmod(newClusterId*7,36))*10,255,255);
    clusterColorList->append(newClusterIdint,color);

    //Remove the clusters which were grouped
    QList<int>::iterator clustersToRemove;
    QList<int>::iterator clustersToRemoveEnd(clustersToGroup.end());
    for (clustersToRemove = clustersToGroup.begin(); clustersToRemove != clustersToRemoveEnd; ++clustersToRemove ){
        clusterColorList->remove(*clustersToRemove);
    }

    //Notify all the views of the modification

    for(int i =0; i<viewList->count();++i){
        KlustersView *view = viewList->at(i);
        if(view != &activeView){
            view->groupedClustersUpdate(clustersToGroup,newClusterIdint,false);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,false);
        }
        else{
            view->groupedClustersUpdate(clustersToGroup,newClusterIdint,true);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,true);
        }
    }

    //Notify the errorMatrixView of the modification
    emit clustersGrouped(clustersToGroup,newClusterIdint);

    //Reset the color status in clusterColors if need it
    if(clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    //Ask the active view to take the modification into account immediately
    activeView.showAllWidgets();

    //Update the palette of cluster
    clusterPalette.updateClusterList();
    QList<int> clustersToShow;
    clustersToShow.append(newClusterIdint);
    clusterPalette.selectItems(clustersToShow);
}


void KlustersDoc::deleteClusters(QList<int> clustersToDelete,KlustersView& activeView,int clusterId){
    QList<int> modifiedcluster;
    modifiedcluster.append(clusterId);

    //If only one cluster has been deleted, the following cluster on the list, if any, will be selected.
    //Find that following cluster.
    int clusterToSelect;
    bool existNextCluster = false;
    if(clustersToDelete.size() == 1){
        int clusterToDelete =  clustersToDelete[0];
        bool previous = false;
        QList<dataType> clusters = clusteringData->clusterIds();
        QList<dataType>::iterator clustersIterator;
        for(clustersIterator = clusters.begin(); clustersIterator != clusters.end(); ++clustersIterator){
            if(previous){
                clusterToSelect = static_cast<int>(*clustersIterator);
                existNextCluster = true;
                break;
            }
            if(*clustersIterator == clusterToDelete) previous = true;
        }
    }

    //case where the clusters are moved to the cluster 0 (artefact)
    if(clusterId == 0){
        //Call data to move the clusters
        clusteringData->moveClustersToArtefact(clustersToDelete);
        //Update clusterColors, add cluster 0 if need it
        if(!clusterColorList->contains(0)){
            //Prepare the undo
            prepareUndo(0,modifiedcluster,clustersToDelete);
            QColor color(Qt::red); //Cluster 01 is always red
            clusterColorList->insert(static_cast<int>(0),color,0);
        }
        else
            //Prepare the undo
            prepareUndo(modifiedcluster,clustersToDelete);
    }
    //case where the clusters are moved to the cluster 1 (noise)
    if(clusterId == 1){
        //Call data to move the clusters
        clusteringData->moveClustersToNoise(clustersToDelete);
        //Update clusterColors, add cluster 1 if need it
        if(!clusterColorList->contains(1)){
            //Prepare the undo
            prepareUndo(1,modifiedcluster,clustersToDelete);
            QColor color;
            color.setHsv(0,0,220);//Cluster 1 is always gray
            if(clusterColorList->contains(0)) clusterColorList->insert(static_cast<int>(1),color,1);
            else clusterColorList->insert(static_cast<int>(1),color,0);
        }
        else
            //Prepare the undo
            prepareUndo(modifiedcluster,clustersToDelete);
    }

    //Update clusterColors,remove the clusters which were deleted
    QList<int>::iterator clustersToRemove;
    for (clustersToRemove = clustersToDelete.begin(); clustersToRemove != clustersToDelete.end(); ++clustersToRemove ){
        if(*clustersToRemove == clusterId) continue;
        clusterColorList->remove(*clustersToRemove);
    }

    //Notify all the views of the modification

    for(int i =0; i<viewList->count();++i){
        KlustersView *view = viewList->at(i);
        if(view != &activeView){
            view->clustersDeletionUpdate(clustersToDelete,clusterId,false);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,false);
        }
        else{
            view->clustersDeletionUpdate(clustersToDelete,clusterId,true);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,true);
        }
    }

    //Notify the errorMatrixView of the modification
    emit clustersDeleted(clustersToDelete,clusterId);

    //Reset the color status in clusterColors if need it
    if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

    //Ask the active view to take the modification into account immediately
    activeView.showAllWidgets();

    //Update the palette of cluster
    clusterPalette.updateClusterList();

    //If only one cluster has been deleted, select the following cluster on the list if any.
    if(existNextCluster){
        QList<int> clusters;
        clusters.append(clusterToSelect);

        //Update the cluster palette
        clusterPalette.selectItems(clusters);
        activeView.shownClustersUpdate(clusters);

        //update the TraceView if any
        activeView.updateTraceView(electrodeGroupID,clusterColorList,true);
    }
}

void KlustersDoc::deleteArtifact(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    deleteSpikesFromClusters(0,region,clustersOfOrigin,dimensionX,dimensionY);
}


void KlustersDoc::deleteNoise(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    deleteSpikesFromClusters(1,region,clustersOfOrigin,dimensionX,dimensionY);
}

void KlustersDoc::deleteSpikesFromClusters(int destination, QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    QList<int> clustersToShow(clustersOfOrigin);

    clusteringData->deleteSpikesFromClusters(region,clustersOfOrigin,destination,dimensionX,dimensionY,fromClusters,emptyClusters);

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //check if any spikes have been selected
    if(fromClusters.isEmpty()){
        activeView->selectionIsEmpty();
        activeView->showAllWidgets();
    }
    else{
        QList<int> updatedClusters = QList<int>(fromClusters);
        updatedClusters.append(destination);

        //Update clusterColors, add cluster 1 if need it
        if(destination == 1 && !clusterColorList->contains(1)){
            //Prepare the undo
            prepareUndo(1,updatedClusters,emptyClusters,true);
            QColor color;
            color.setHsv(0,0,220);//Cluster 1 is always gray
            if(clusterColorList->contains(0)) clusterColorList->insert(static_cast<int>(1),color,1);
            else clusterColorList->insert(static_cast<int>(1),color,0);
        }
        //Update clusterColors, add cluster 0 if need it
        else if(destination == 0 && !clusterColorList->contains(0)){
            //Prepare the undo
            prepareUndo(0,updatedClusters,emptyClusters,true);
            QColor color(Qt::red); //Cluster 01 is always red
            clusterColorList->insert(static_cast<int>(0),color,0);
        }
        else
            //Prepare the undo
            prepareUndo(updatedClusters,emptyClusters,true);

        //Remove all the empty clusters from clusterColors and clustersToShow
        if(!emptyClusters.isEmpty()){
            QList<int>::iterator clustersToRemove;
            for (clustersToRemove = emptyClusters.begin(); clustersToRemove != emptyClusters.end(); ++clustersToRemove ){
                clusterColorList->remove(*clustersToRemove);
                clustersToShow.removeAll(*clustersToRemove);
            }
        }

        //Notify all the views of the modification

        for(int i =0; i<viewList->count();++i){
            KlustersView *view = viewList->at(i);
            if(view != activeView){
                view->removeSpikesFromClustersInView(fromClusters,destination,emptyClusters,false);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,false);
            }
            else{
                view->removeSpikesFromClustersInView(fromClusters,destination,emptyClusters,true);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,true);
            }
        }

        //Notify the errorMatrixView of the modification
        emit removeSpikesFromClusters(fromClusters,destination,emptyClusters);

        //Reset the color status in clusterColors if need it
        if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

        activeView->showAllWidgets();

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);

        //Notify the application that spikes have been deleted.
        emit spikesDeleted();
    }
}


void KlustersDoc::createNewCluster(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    QList<int> clustersToShow(clustersOfOrigin);
    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    float newClusterId = clusteringData->createNewCluster(region,clustersOfOrigin,dimensionX,dimensionY,fromClusters,emptyClusters);

    //Check if a new cluster has been created
    if(newClusterId == 0){
        activeView->selectionIsEmpty();
        activeView->showAllWidgets();
    }
    else{
        int newClusterIdint = static_cast<int>(newClusterId);

        //Prepare the undo
        prepareUndo(newClusterIdint,fromClusters,emptyClusters);

        //Add the cluster in clusterColors and clustersToShow.
        QColor color;
        color.setHsv(static_cast<int>(fmod(newClusterId*7,36))*10,255,255);
        clusterColorList->append(newClusterIdint,color);
        clustersToShow.append(newClusterIdint);
        //Remove all the empty clusters from clusterColors and clustersToShow
        if(!emptyClusters.isEmpty()){
            QList<int>::iterator clustersToRemove;
            for (clustersToRemove = emptyClusters.begin(); clustersToRemove != emptyClusters.end(); ++clustersToRemove ){
                clusterColorList->remove(*clustersToRemove);
                clustersToShow.removeAll(*clustersToRemove);
            }
        }

        //Notify all the views of the modification

        for(int i =0; i<viewList->count();++i){
            KlustersView *view = viewList->at(i);
            if(view != activeView){
                view->addNewClusterToView(fromClusters,newClusterIdint,emptyClusters,false);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,false);
            }
            else{
                view->addNewClusterToView(fromClusters,newClusterIdint,emptyClusters,true);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,true);
            }
        }

        //Notify the errorMatrixView of the modification
        emit newClusterAdded(fromClusters,newClusterIdint,emptyClusters);

        //Reset the color status in clusterColors if need it
        if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

        activeView->showAllWidgets();

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);
    }
}

void KlustersDoc::createNewClusters(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    QList<int> clustersToShow(clustersOfOrigin);
    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    QList <int> newClusters;
    QMap<int,int> fromToNewClusterIds = clusteringData->createNewClusters(region,clustersOfOrigin,dimensionX,dimensionY,emptyClusters);
    newClusters = fromToNewClusterIds.values();
    fromClusters = fromToNewClusterIds.keys();

    //Check if at least one new cluster has been created
    if(newClusters.size() == 0){
        activeView->selectionIsEmpty();
        activeView->showAllWidgets();
    }
    else{
        //Prepare the undo
        prepareUndo(newClusters,fromClusters,emptyClusters);

        //Add the clusters in clusterColors and clustersToShow.
        QColor color;
        QList<int>::iterator clustersToCreate;
        std::sort(newClusters.begin(), newClusters.end());
        for (clustersToCreate = newClusters.begin(); clustersToCreate != newClusters.end(); ++clustersToCreate ){
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,255,255);
            clusterColorList->append(*clustersToCreate,color);
            clustersToShow.append(*clustersToCreate);
        }
        //Remove all the empty clusters
        if(!emptyClusters.isEmpty()){
            QList<int>::iterator clustersToRemove;
            for (clustersToRemove = emptyClusters.begin(); clustersToRemove != emptyClusters.end(); ++clustersToRemove ){
                clusterColorList->remove(*clustersToRemove);
                clustersToShow.removeAll(*clustersToRemove);
            }
        }

        //Notify all the views of the modification

        for(int i =0; i<viewList->count();++i){
            KlustersView *view = viewList->at(i);
            if(view != activeView){
                view->addNewClustersToView(fromToNewClusterIds,emptyClusters,false);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,false);
            }
            else{
                view->addNewClustersToView(fromToNewClusterIds,emptyClusters,true);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,true);
            }
        }

        //Notify the errorMatrixView of the modification
        emit newClustersAdded(fromToNewClusterIds,emptyClusters);


        //Reset the color status in clusterColors if need it
        if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

        activeView->showAllWidgets();

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);
    }
}

void KlustersDoc::prepareClusterColorUndo(){
    //Update the boolean modified here as every action implies a call to the function
    modified = true;

    //Create a new clusterColors which will hold the new configuration
    ItemColors* clusterColorListTemp = new ItemColors(*clusterColorList);

    //Store the current clusterColors in the undo list and make the temporary become the current one.
    clusterColorListUndoList.prepend(clusterColorList);
    clusterColorList = clusterColorListTemp;

    //if the number of undo has been reach remove the last element in the undo list (first inserted)
    int currentClusterColorsNbUndo = clusterColorListUndoList.count();
    if(currentClusterColorsNbUndo > nbUndo)
        delete clusterColorListUndoList.takeAt(currentClusterColorsNbUndo - 1);

    //Clear the redoList
    qDeleteAll(clusterColorListRedoList);
    clusterColorListRedoList.clear();

    //Signal to klusters the new number of undo and redo
    emit updateUndoNb(clusterColorListUndoList.count());
    emit updateRedoNb(0);
}

void KlustersDoc::prepareUndo(QList<int>* addedClustersTemp,QList<int>* modifiedClustersTemp,QList<int>* deletedClustersTemp,bool isModifiedByDeletion){
    //Prepare the undo for the cluster palette
    prepareClusterColorUndo();

    //Store the current addedClusters in the undo list and make the temporary become the current one.
    addedClustersUndoList.prepend(addedClusters);
    addedClusters = addedClustersTemp;

    //Store the current modifiedClusters in the undo list and make the temporary become the current one.
    modifiedClustersUndoList.prepend(modifiedClusters);
    modifiedClusters = modifiedClustersTemp;

    //Store the current deletedClusters in the undo list and make the temporary become the current one.
    deletedClustersUndoList.prepend(deletedClusters);
    deletedClusters = deletedClustersTemp;

    //The renumbering actions which were redo are now lost
    QList<int>::iterator iterator;
    for(iterator = renumberingRedoList.begin(); iterator != renumberingRedoList.end(); ++iterator){
        clusterIdsOldNewMap.remove(*iterator);
        clusterIdsNewOldMap.remove(*iterator);
    }
    renumberingRedoList.clear();

    //if the number of undo has been reach remove the last element in the undo lists (first inserted)
    int currentNbUndo = addedClustersUndoList.count();
    if(currentNbUndo > nbUndo){
        delete addedClustersUndoList.takeAt(currentNbUndo - 1);
        delete modifiedClustersUndoList.takeAt(currentNbUndo - 1);
        delete deletedClustersUndoList.takeAt(currentNbUndo - 1);
        // removeAll(value) removes list entries whose VALUE equals currentNbUndo.
        // (removeAt(index) would be an out-of-bounds crash when the list is short.)
        modifiedClustersByDeleteUndo.removeAll(currentNbUndo);
        if(isModifiedByDeletion) modifiedClustersByDeleteUndo.append(currentNbUndo - 1);

        //The clusterIdsOldNew and clusterIdsNewOld maps are associated with
        //undo numbers. As the meaning of the numbers change (first undo will not be accessible anymore,
        //and the following ones are shift by one down (2->1, 3->2 etc..)), the maps have to be updated accordingly.
        if(clusterIdsOldNewMap.count() == 1 && clusterIdsOldNewMap.contains(1)){
            clusterIdsOldNewMap.remove(1);
            clusterIdsNewOldMap.remove(1);
        }
        else{
            for(int i = 2; i <= nbUndo; ++i){
                if(!clusterIdsOldNewMap.contains(i)) continue;
                QMap<int,int> clusterIdsOldNew = clusterIdsOldNewMap[i];
                clusterIdsOldNewMap.insert(i-1,clusterIdsOldNew);
                QMap<int,int> clusterIdsNewOld = clusterIdsNewOldMap[i];
                clusterIdsNewOldMap.insert(i-1,clusterIdsNewOld);
            }
            //remove the map entries with the bigger key (has not be taken into account by the previous loop)
            if(!clusterIdsOldNewMap.isEmpty()) {
                QList<int> undoNbs = clusterIdsOldNewMap.keys();
                std::sort(undoNbs.begin(), undoNbs.end());
                int biggerUndo = undoNbs.last();
                clusterIdsOldNewMap.remove(biggerUndo);
                clusterIdsNewOldMap.remove(biggerUndo);
            }
        }
    }
    else if(isModifiedByDeletion) modifiedClustersByDeleteUndo.append(currentNbUndo);

    //Clear the redoLists
    qDeleteAll(addedClustersRedoList);
    addedClustersRedoList.clear();
    qDeleteAll(modifiedClustersRedoList);
    modifiedClustersRedoList.clear();
    qDeleteAll(deletedClustersRedoList);
    deletedClustersRedoList.clear();
}



void KlustersDoc::nbUndoChangedCleaning(int newNbUndo){
    //if the new number of possible undo is smaller than the current one,
    // clean the undo/redo related variables.
    if(newNbUndo < nbUndo){
        //Make data clean its internal variables
        clusteringData->nbUndoChangedCleaning(newNbUndo);

        //Process the renumbering variables. All the undo indices in renumberingRedoList which
        //are bigger than newNbUndo will not be accesible any more, delete them.
        QList<int>::iterator iterator;
        QList<int> suppressIndices;
        for(iterator = renumberingRedoList.begin(); iterator != renumberingRedoList.end(); ++iterator){
            if(*iterator > newNbUndo){
                clusterIdsOldNewMap.remove(*iterator);
                clusterIdsNewOldMap.remove(*iterator);
                suppressIndices.append(*iterator);
            }
        }
        for(iterator = suppressIndices.begin(); iterator != suppressIndices.end(); ++iterator)
            renumberingRedoList.removeAll(*iterator);

        int currentNbUndo = clusterColorListUndoList.count();

        //if the current number of undo is bigger than the new number of undo,
        // remove the last elements in the undo lists (first ones inserted).
        if(currentNbUndo > newNbUndo){
            while(currentNbUndo > newNbUndo){
                delete addedClustersUndoList.takeAt(currentNbUndo - 1);
                delete modifiedClustersUndoList.takeAt(currentNbUndo - 1);
                delete deletedClustersUndoList.takeAt(currentNbUndo - 1);
                delete clusterColorListUndoList.takeAt(currentNbUndo - 1);
                modifiedClustersByDeleteUndo.removeAll(currentNbUndo);

                //The clusterIdsOldNew and clusterIdsNewOld maps are associated with
                //undo numbers. As the meaning of the numbers change (first undo will not be accessible anymore,
                //and the following ones are shift by one down (2->1, 3->2 etc..)), the maps have to be updated accordingly.
                if(clusterIdsOldNewMap.count() == 1 && clusterIdsOldNewMap.contains(1)){
                    clusterIdsOldNewMap.remove(1);
                    clusterIdsNewOldMap.remove(1);
                }
                else{
                    for(int i = 2; i <= currentNbUndo; ++i){
                        if(!clusterIdsOldNewMap.contains(i)) continue;
                        QMap<int,int> clusterIdsOldNew = clusterIdsOldNewMap[i];
                        clusterIdsOldNewMap.insert(i-1,clusterIdsOldNew);
                        QMap<int,int> clusterIdsNewOld = clusterIdsNewOldMap[i];
                        clusterIdsNewOldMap.insert(i-1,clusterIdsNewOld);
                    }
                    //remove the map entries with the bigger key (has not be taken into account by the previous loop)
                    QList<int> undoNbs = clusterIdsOldNewMap.keys();
                    std::sort(undoNbs.begin(), undoNbs.end());
                    int biggerUndo = undoNbs.last();
                    clusterIdsOldNewMap.remove(biggerUndo);
                    clusterIdsNewOldMap.remove(biggerUndo);
                }

                currentNbUndo = clusterColorListUndoList.count();
            }
            //clear the redo lists
            qDeleteAll(addedClustersRedoList);
            addedClustersRedoList.clear();
            qDeleteAll(modifiedClustersRedoList);
            modifiedClustersRedoList.clear();
            qDeleteAll(deletedClustersRedoList);
            deletedClustersRedoList.clear();
            qDeleteAll(clusterColorListRedoList);
            clusterColorListRedoList.clear();
        }
        //currentNbUndo < newNbUndo, check the redo list.
        else{
            //number of undo and redo must be <= new number of undo. Remove redo elements if need it.
            int currentNbRedo = clusterColorListRedoList.count();
            if((currentNbRedo + currentNbUndo) > newNbUndo){
                while((currentNbRedo + currentNbUndo) > newNbUndo){
                    delete addedClustersRedoList.takeAt(currentNbRedo - 1);
                    delete modifiedClustersRedoList.takeAt(currentNbRedo - 1);
                    delete deletedClustersRedoList.takeAt(currentNbRedo - 1);
                    delete clusterColorListRedoList.takeAt(currentNbRedo - 1);
                    modifiedClustersByDeleteRedo.removeAll(currentNbRedo);

                    currentNbRedo = clusterColorListRedoList.count();
                }
            }
        }

        //Make the views clean its internal variables

        for(int i =0; i<viewList->count();++i) {
            KlustersView *view = viewList->at(i);
            view->nbUndoChangedCleaning(newNbUndo);
        }

        //Signal to klusters the new number of undo and redo
        emit updateUndoNb(clusterColorListUndoList.count());
        emit updateRedoNb(clusterColorListRedoList.count());
    }
}


void KlustersDoc::prepareUndo(){
    //Create a new empty list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();

    //Create a new empty list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();

    //Create a new empty list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}

void KlustersDoc::prepareUndo(int newCluster,QList<int>& deletedClusters){
    //Create a new list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    addedClustersTemp->append(newCluster);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}

void KlustersDoc::prepareUndo(QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion){
    //Create a new empty list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = modifiedClusters.begin(); iterator != modifiedClusters.end(); ++iterator)
        modifiedClustersTemp->append(*iterator);

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp,isModifiedByDeletion);
}

void KlustersDoc::prepareUndo(int newCluster, QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion){
    //Create a new empty list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    addedClustersTemp->append(newCluster);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = modifiedClusters.begin(); iterator != modifiedClusters.end(); ++iterator)
        modifiedClustersTemp->append(*iterator);

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp,isModifiedByDeletion);
}

void KlustersDoc::prepareUndo(QList<int>& newClusters, QList<int>& modifiedClusters,QList<int>& deletedClusters){
    //Create a new list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = newClusters.begin(); iterator != newClusters.end(); ++iterator)
        addedClustersTemp->append(*iterator);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();
    for (iterator = modifiedClusters.begin(); iterator != modifiedClusters.end(); ++iterator)
        modifiedClustersTemp->append(*iterator);

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}


void KlustersDoc::prepareUndo(QMap<int,int> clusterIdsOldNew,QMap<int,int> clusterIdsNewOld){
    prepareUndo();

    //Update the renumbering lists
    int currentNbUndo = clusterColorListUndoList.count();
    qDebug()<<"currentNbUndo in KlustersDoc::prepareUndo: "<<currentNbUndo;
    clusterIdsOldNewMap.insert(currentNbUndo,clusterIdsOldNew);
    clusterIdsNewOldMap.insert(currentNbUndo,clusterIdsNewOld);
}


void KlustersDoc::prepareReclusteringUndo(QList<int>& newClusters,QList<int>& deletedClusters){
    //Create a new list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for(iterator = newClusters.begin(); iterator != newClusters.end(); ++iterator)
        addedClustersTemp->append(*iterator);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for(iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}

void KlustersDoc::undo(){

    qDebug()<<"in KlustersDoc::undo 1";

    //Update the boolean modified here as every undo action implies a call to the function.
    //The user can save and make an undo just behind, in that case the document is modified.
    modified = true;

    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    if(!activeView)
        return;
    clusteringData->undo(*addedClusters,*modifiedClusters);

    //If clusterColorListUndoList is not empty, make the current clusterColorList become the first element
    //of the clusterColorListRedoList and the first element of the clusterColorListUndoList become the current clusterColorList
    //do the same for the addedClusters and modifiedClusters Lists.
    if(clusterColorListUndoList.count()>0){
        clusterColorListRedoList.prepend(clusterColorList);
        ItemColors* clusterColorListTemp = clusterColorListUndoList.takeAt(0);
        clusterColorList =  clusterColorListTemp;

        int nbUndo = clusterColorListUndoList.count();

        qDebug() << "nbUndo in KlustersDoc::undo: "<<nbUndo;

        //If this undo does concern renumbering
        if(clusterIdsNewOldMap.contains(nbUndo + 1)){
            qDebug() << "renumber in KlustersDoc::undo, nbUndo + 1 : "<<nbUndo + 1;
            //Add the current undo indice to the renumberingRedoList
            renumberingRedoList.append(nbUndo + 1);

            //Notify all the views of the undo

            for(int i =0; i<viewList->count();++i) {
                KlustersView *view = viewList->at(i);
                if(view != activeView){
                    view->undoRenumbering(clusterIdsNewOldMap[nbUndo + 1],false);
                    //update the TraceView if any
                    view->updateTraceView(electrodeGroupID,clusterColorList,false);
                }
                else{
                    view->undoRenumbering(clusterIdsNewOldMap[nbUndo + 1],true);
                    //update the TraceView if any
                    view->updateTraceView(electrodeGroupID,clusterColorList,true);
                }
            }

            //Notify the errorMatrixView of the modification
            emit undoRenumbering(clusterIdsNewOldMap[nbUndo + 1]);
        }
        else{
            if(modifiedClustersByDeleteUndo.contains(nbUndo + 1) != 0){
                modifiedClustersByDeleteUndo.removeAll(nbUndo + 1);
                int nbRedo = clusterColorListRedoList.count();
                modifiedClustersByDeleteRedo.append(nbRedo);
            }

            //Notify all the views of the undo
            if(addedClusters->size() > 0 && modifiedClusters->size() > 0){
                qDebug() << "addedClusters->size() > 0 && modifiedClusters->size() > 0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->undo(*addedClusters,*modifiedClusters,false);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->undo(*addedClusters,*modifiedClusters,true);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }

                //Notify the errorMatrixView of the modification
                emit undoAdditionModification(*addedClusters,*modifiedClusters);
            }
            else if(!addedClusters->isEmpty() && modifiedClusters->isEmpty()){
                qDebug() << "addedClusters->size() > 0 && modifiedClusters->size() == 0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->undoAddedClusters(*addedClusters,false);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->undoAddedClusters(*addedClusters,true);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }

                //Notify the errorMatrixView of the modification
                emit undoAddition(*addedClusters);
            }
            else if(addedClusters->isEmpty() && !modifiedClusters->isEmpty()){
                qDebug() << "addedClusters->size() == 0 && modifiedClusters->size() > 0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->undoModifiedClusters(*modifiedClusters,false);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->undoModifiedClusters(*modifiedClusters,true);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }

                //Notify the errorMatrixView of the modification
                emit undoModification(*modifiedClusters);
            }
            //////!!!!This last condition should not be reach anymore, to test and remove.!!!!!////
            else if(addedClusters->size() == 0 && modifiedClusters->size() == 0){
                qDebug() << "addedClusters->size() == 0 && modifiedClusters->size() == 0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->undo(false);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->undo(true);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }
            }
        }
        addedClustersRedoList.prepend(addedClusters);
        QList<int>* addedClustersTemp = addedClustersUndoList.takeAt(0);
        addedClusters =  addedClustersTemp;

        modifiedClustersRedoList.prepend(modifiedClusters);
        QList<int>* modifiedClustersTemp = modifiedClustersUndoList.takeAt(0);
        modifiedClusters =  modifiedClustersTemp;

        deletedClustersRedoList.prepend(deletedClusters);
        QList<int>* deletedClustersTemp = deletedClustersUndoList.takeAt(0);
        deletedClusters =  deletedClustersTemp;

        QList<int> clustersToShow = activeView->clusters();

        //Call redraw on the active view
        activeView->showAllWidgets();

        //Update the clusterPalette
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);

        //Signal to klusters the new number of undo and redo
        emit updateUndoNb(clusterColorListUndoList.count());
        emit updateRedoNb(clusterColorListRedoList.count());
    }

    qDebug()<<"in KlustersDoc::undo 2";
}


void KlustersDoc::redo(){
    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    //Update the boolean modified here as every redo action implies a call to the function.
    //The user can save and make an redo just behind, in that case the document is modified.
    modified = true;

    //If clusterColorListRedoList is not empty, make the current clusterColorList become the first element
    //of the clusterColorListUndoList and the first element of the clusterColorListRedoList become the current clusterColorList
    //do the same for the addedClusters and modifiedClusters Lists.
    if(clusterColorListRedoList.count()>0){
        clusterColorListUndoList.prepend(clusterColorList);
        ItemColors* clusterColorListTemp = clusterColorListRedoList.takeAt(0);
        clusterColorList =  clusterColorListTemp;

        addedClustersUndoList.prepend(addedClusters);
        QList<int>* addedClustersTemp = addedClustersRedoList.takeAt(0);
        addedClusters =  addedClustersTemp;

        modifiedClustersUndoList.prepend(modifiedClusters);
        QList<int>* modifiedClustersTemp = modifiedClustersRedoList.takeAt(0);
        modifiedClusters =  modifiedClustersTemp;

        deletedClustersUndoList.prepend(deletedClusters);
        QList<int>* deletedClustersTemp = deletedClustersRedoList.takeAt(0);
        deletedClusters =  deletedClustersTemp;

        clusteringData->redo(*addedClusters,*modifiedClusters,*deletedClusters);

        //If this redo does concern renumbering
        int nbUndo = clusterColorListUndoList.count();

        qDebug() << "in KlustersDoc::redo, nbUndo  : "<<nbUndo;

        if(clusterIdsOldNewMap.contains(nbUndo)){
            qDebug() << "renumber in KlustersDoc::redo, nbUndo  : "<<nbUndo;
            //remove the current undo indice from the renumberingRedoList
            renumberingRedoList.removeAll(nbUndo);

            //Notify all the views of the undo
            for(int i =0; i<viewList->count();++i) {
                KlustersView *view = viewList->at(i);
                if(view != activeView){
                    view->redoRenumbering(clusterIdsOldNewMap[nbUndo],false);
                    //update the TraceView if any
                    view->updateTraceView(electrodeGroupID,clusterColorList,false);
                }
                else{
                    view->redoRenumbering(clusterIdsOldNewMap[nbUndo],true);
                    //update the TraceView if any
                    view->updateTraceView(electrodeGroupID,clusterColorList,true);
                }
            }

            //Notify the errorMatrixView of the modification
            emit redoRenumbering(clusterIdsOldNewMap[nbUndo]);
        }
        else{
            int nbRedo = clusterColorListRedoList.count();
            bool isModifiedByDeletion = false;
            if(modifiedClustersByDeleteRedo.contains(nbRedo + 1) != 0){
                isModifiedByDeletion = true;
                modifiedClustersByDeleteRedo.removeAll(nbRedo + 1);
                int nbUndo = clusterColorListUndoList.count();
                modifiedClustersByDeleteUndo.append(nbUndo);
            }

            //Notify all the views of the undo
            if(addedClusters->size() > 0 && modifiedClusters->size() > 0){
                qDebug() << "in KlustersDoc::redo, nbUndo  addedClusters->size() > 0 && modifiedClusters->size()>0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->redo(*addedClusters,*modifiedClusters,isModifiedByDeletion,false,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->redo(*addedClusters,*modifiedClusters,isModifiedByDeletion,true,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }

                //Notify the errorMatrixView of the modification
                emit redoAdditionModification(*addedClusters,*modifiedClusters,isModifiedByDeletion,*deletedClusters);
            }
            else if(addedClusters->size() > 0 && modifiedClusters->size() == 0){
                qDebug() << "in KlustersDoc::redo, nbUndo  addedClusters->size() > 0 && modifiedClusters->size()==0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->redoAddedClusters(*addedClusters,false,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->redoAddedClusters(*addedClusters,true,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }

                //Notify the errorMatrixView of the modification
                emit redoAddition(*addedClusters,*deletedClusters);
            }
            else if(addedClusters->size() == 0 && modifiedClusters->size() > 0){
                qDebug() << "in KlustersDoc::redo, nbUndo  addedClusters->size() == 0 && modifiedClusters->size()>0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->redoModifiedClusters(*modifiedClusters,isModifiedByDeletion,false,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->redoModifiedClusters(*modifiedClusters,isModifiedByDeletion,true,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }

                //Notify the errorMatrixView of the modification
                emit redoModification(*modifiedClusters,isModifiedByDeletion,*deletedClusters);
            }
            else if(addedClusters->size() == 0 && modifiedClusters->size() == 0){
                qDebug() << "in KlustersDoc::redo, nbUndo  addedClusters->size() == 0 && modifiedClusters->size() ==0";
                for(int i =0; i<viewList->count();++i) {
                    KlustersView *view = viewList->at(i);
                    if(view != activeView){
                        view->redo(false,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,false);
                    }
                    else{
                        view->redo(true,*deletedClusters);
                        //update the TraceView if any
                        view->updateTraceView(electrodeGroupID,clusterColorList,true);
                    }
                }

                //Notify the errorMatrixView of the modification
                emit redoDeletion(*deletedClusters);
            }
        }

        qDebug() << "in KlustersDoc::redo, 2  : ";

        QList<int> clustersToShow = activeView->clusters();

        //Call redraw on the active view
        activeView->showAllWidgets();
        //Update the clusterPalette
        clusterPalette.updateClusterList();

        qDebug() << "in KlustersDoc::redo, 3 b : ";

        clusterPalette.selectItems(clustersToShow);

        qDebug() << "in KlustersDoc::redo, 4  : ";

        //Signal to klusters the new number of undo and redo
        emit updateUndoNb(clusterColorListUndoList.count());
        emit updateRedoNb(clusterColorListRedoList.count());

        qDebug() << "in KlustersDoc::redo, end  : ";
    }
}

void KlustersDoc::renumberClusters(){
    //Get the active view.
    KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

    QMap<int,int> clusterIdsOldNew;
    QMap<int,int> clusterIdsNewOld;

    clusteringData->renumber(clusterIdsOldNew,clusterIdsNewOld);

    prepareUndo(clusterIdsOldNew,clusterIdsNewOld);

    //Update the clusterColorList, keep the same colors, only update the clusterIds
    QList<dataType> clusterList = clusteringData->clusterIds();
    int nbClusters = clusterList.size();

    for (int i = 0; i < nbClusters; ++i){
        int clusterId = static_cast<int>(clusterList[i]);
        clusterColorList->changeItemId(i,clusterId);
    }

    //Notify all the views of the modification
    const int numberOfView(viewList->count());
    for(int i =0; i<numberOfView;++i)
    {
        KlustersView* view = viewList->at(i);
        if (view != activeView){
            view->renumberClusters(clusterIdsOldNew,false);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,false);
        } else {
            view->renumberClusters(clusterIdsOldNew,true);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,true);
        }
    }

    //Notify the errorMatrixView of the modification
    emit renumber(clusterIdsOldNew);

    //Reset the color status in clusterColors if need it
    if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

    activeView->showAllWidgets();

    //Update the palette of cluster
    QList<int> activeClusters = activeView->clusters();
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(activeClusters);
    shownClustersUpdate(activeClusters,*activeView);
}

int KlustersDoc::createFeatureFile(QList<int>& clustersToRecluster,const QString& reclusteringFetFileName){
    QFile fetFile(reclusteringFetFileName);
    if(!fetFile.open(QIODevice::WriteOnly))
        return OPEN_ERROR;

    //Create the file
    clusteringData->createFeatureFile(clustersToRecluster,fetFile);
    fetFile.close();
    if(fetFile.error() == QFile::NoError)
        return OK;
    else
        return CREATION_ERROR;
}

int KlustersDoc::integrateReclusteredClusters(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList,QString reclusteringFetFileName){

    QString cluFileName(reclusteringFetFileName);
    qDebug()<<"reclusteringFetFileName "<<reclusteringFetFileName;
    cluFileName.replace(".fet.",".clu.");

    QString cluFileUrl(cluFileName);
    QString tmpCluFile = cluFileUrl;
    if(!QFile::exists(cluFileUrl)) {
        QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        return DOWNLOAD_ERROR;
    }

    qDebug()<<" tmpCluFile"<<tmpCluFile;
    QFile cluFile(tmpCluFile);

    if(!cluFile.open(QIODevice::ReadOnly)){
        if(!QFile::remove(reclusteringFetFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
        if(!QFile::remove(cluFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        return OPEN_ERROR;
    }

    //Actually integrate the new clusters.
    if(!clusteringData->integrateReclusteredClusters(clustersToRecluster,reclusteredClusterList,cluFile)){
        cluFile.close();
        if(!QFile::remove(reclusteringFetFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
        if(!QFile::remove(cluFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        return INCORRECT_CONTENT;
    }
    cluFile.close();

    //Suppress the fet and clu files.
    if(!QFile::remove(reclusteringFetFileName))
        QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
    if(!QFile::remove(cluFileName))
        QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );

    return OK;
}

void KlustersDoc::reclusteringUpdate(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList){
    //Prepare the undo
    prepareReclusteringUndo(reclusteredClusterList,clustersToRecluster);

    //Check if the active view is a ProcessWidget
    bool isProcessWidget = dynamic_cast<KlustersApp*>(parent)->doesActiveDisplayContainProcessWidget();

    if(!isProcessWidget){
        //Get the active view.
        KlustersView* activeView = static_cast<KlustersApp*>(parent)->activeView();

        QList<int> clustersToShow;
        QList<int>::const_iterator iterator;
        QList<int> const clusters = activeView->clusters();
        for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator)
            clustersToShow.append(*iterator);

        //Add the new clusters in clusterColors and clustersToShow.
        QColor color;
        QList<int>::iterator clustersToCreate;
        for(clustersToCreate = reclusteredClusterList.begin(); clustersToCreate != reclusteredClusterList.end(); ++clustersToCreate ){
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,255,255);
            clusterColorList->append(*clustersToCreate,color);
            clustersToShow.append(*clustersToCreate);
        }

        //Remove all the reclustered clusters from clusterColors and clustersToShow.
        QList<int>::iterator clustersToRemove;
        for (clustersToRemove = clustersToRecluster.begin(); clustersToRemove != clustersToRecluster.end(); ++clustersToRemove ){
            clusterColorList->remove(*clustersToRemove);
            clustersToShow.removeAll(*clustersToRemove);
        }

        //Notify all the views of the modification
        for(int i =0; i<viewList->count();++i){
            KlustersView* view = viewList->at(i);
            if(view != activeView){
                view->addNewClustersToView(clustersToRecluster,reclusteredClusterList,false);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,false);
            }
            else{
                view->addNewClustersToView(clustersToRecluster,reclusteredClusterList,true);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,true);
            }
        }

        //Notify the errorMatrixView of the modification
        emit newClustersAdded(clustersToRecluster);

        activeView->showAllWidgets();

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);
    }
    else{//processWidget
        //Add the new clusters in clusterColors.
        QColor color;
        QList<int>::iterator clustersToCreate;
        for(clustersToCreate = reclusteredClusterList.begin(); clustersToCreate != reclusteredClusterList.end(); ++clustersToCreate ){
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,255,255);
            clusterColorList->append(*clustersToCreate,color);
        }

        //Remove all the reclustered clusters from clusterColors and clustersToShow.
        QList<int>::iterator clustersToRemove;
        for (clustersToRemove = clustersToRecluster.begin(); clustersToRemove != clustersToRecluster.end(); ++clustersToRemove ){
            clusterColorList->remove(*clustersToRemove);
        }

        //Notify all the views of the modification
        for(int i =0; i<viewList->count();++i) {
	    KlustersView* view = viewList->at(i);
            if(!qobject_cast<ProcessWidget*>(view)){
                view->addNewClustersToView(clustersToRecluster,reclusteredClusterList,false);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,false);
            }
	}

        //Notify the errorMatrixView of the modification
        emit newClustersAdded(clustersToRecluster);

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        QList<int> emptyList;
        clusterPalette.selectItems(emptyList);
    }
}

void KlustersDoc::createProviders(){
    QFileInfo docInfo(docUrl);
    const QString datUrl = docInfo.absolutePath() + "/" + docInfo.baseName() +".dat";

    int resolution = clusteringData->getResolution();
    int voltageRange = clusteringData->getVoltageRange();
    double samplingRate = clusteringData->getSamplingRate();
    int channelNb = clusteringData->getTotalNbChannels();

    //Create the tracesProviders
    tracesProvider = new TracesProvider(datUrl,channelNb,
                                        resolution,samplingRate,clusteringData->getOffset());


    acquisitionGain = static_cast<int>(0.5 +
                                       static_cast<float>(pow(static_cast<double>(2),static_cast<double>(resolution))
                                                          / static_cast<float>(voltageRange * 1000))
                                       * clusteringData->getAmplification());

    //the screen grain is fixed to 0.2
    float screenGain = 0.2;
    gain = static_cast<int>(0.5 + screenGain * static_cast<float>(acquisitionGain));

    //Create the colorlist
    //Constructs the channelColorList, assign to all the channels the same blue color.
    //Put all the channels of the spike group corresponding to the open file in the same group(the electrodeGroupID)
    channelColorList = new ChannelColors();
    QColor color;
    QList<int> group;
    color.setHsv(210,255,255);

    QList<int>& currentChannels =  clusteringData->getCurrentChannels();
    QList<int>::const_iterator iterator;
    for(iterator = currentChannels.begin(); iterator != currentChannels.end(); ++iterator){
        channelColorList->append(*iterator,color);
        displayChannelsGroups.insert(*iterator,electrodeGroupID.toInt());
        channelsSpikeGroups.insert(*iterator,electrodeGroupID.toInt());
        group.append(*iterator);
    }

    displayGroupsChannels.insert(electrodeGroupID.toInt(),group);
    spikeGroupsChannels.insert(electrodeGroupID.toInt(),group);

    ////Put all the other channels in the trash group (group 0).
    QList<int> trashGroup;
    for(int i = 0; i < channelNb; ++i){
        if(!currentChannels.contains(i)){
            channelColorList->append(i,color);
            displayChannelsGroups.insert(i,0);
            channelsSpikeGroups.insert(i,0);
            trashGroup.append(i);
        }
    }

    displayGroupsChannels.insert(0,trashGroup);
    spikeGroupsChannels.insert(0,trashGroup);

    clustersProvider = new ClustersProvider(docUrl,samplingRate,samplingRate,*clusteringData,tracesProvider->getTotalNbSamples());

    //The current cluster file contains the data for the unique display group.
    QList<int> list;
    list.append(electrodeGroupID.toInt());
    displayGroupsClusterFile.insert(electrodeGroupID.toInt(),list);
}


void KlustersDoc::showUserClusterInformation(){
    clusterPalette.showUserClusterInformation(electrodeGroupID.toInt());
}



// ===========================================================================
// KlustersDoc::realignSpikes  (normalised cross-correlation template method)
// ===========================================================================
//
// Algorithm overview
// ------------------
// 1. Load all waveforms for the cluster into a contiguous int16 buffer.
// 2. Compute the cluster template: int16 mean waveform across all spikes.
// 3. Dispatch XcorrDispatch::compute() — runs on CUDA / HIP / SYCL / OMP,
//    returns per-spike optimal lag and normalised xcorr score.
// 4. For each spike with |lag| > 0 and score ≥ minScore:
//      a. Re-extract the waveform at the shifted position (from .dat if
//         available, otherwise by rolling the existing buffer with zero-pad).
//      b. Write the new waveform to .spk.N.
//      c. Update the timestamp in .res.N and the feature row in .fet.N
//         (calling process_refeaturize for re-projection onto PCA basis).
//      d. If the new timestamp violates sort order, perform a sorted
//         insertion into .res / .spk / .clu / .fet and in memory.
//
// The xcorr approach uses the full multi-channel spatiotemporal waveform
// shape rather than the peak of a single channel, so it correctly handles
// spikes detected on different channels within the same cluster.

#include "realign_xcorr.h"   // XcorrDispatch lives here via the dispatch TU

// Forward declaration — XcorrDispatch is defined in realign_xcorr_dispatch.cpp
namespace XcorrDispatch {
int compute(const int16_t*, const int16_t*,
            int, int, int, int, float, int*, float*);
const char* backendName();
}

// ---------------------------------------------------------------------------
// File I/O helpers  (unchanged from previous implementation)
// ---------------------------------------------------------------------------

static bool readSpkWaveform(const QString& spkPath, long spikeIdx0,
                             int nChannels, int nSamples,
                             std::vector<int16_t>& waveform)
{
    // spikeIdx0: 0-based
    QFile f(spkPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    qint64 bytesPerSpike = static_cast<qint64>(nChannels) * nSamples * 2;
    if (!f.seek(spikeIdx0 * bytesPerSpike)) { f.close(); return false; }
    waveform.resize(static_cast<size_t>(nChannels * nSamples));
    qint64 nRead = f.read(reinterpret_cast<char*>(waveform.data()),
                          static_cast<qint64>(waveform.size() * 2));
    f.close();
    return nRead == static_cast<qint64>(waveform.size() * 2);
}

static bool writeSpkWaveform(const QString& spkPath, long spikeIdx0,
                              int nChannels, int nSamples,
                              const std::vector<int16_t>& waveform)
{
    QFile f(spkPath);
    if (!f.open(QIODevice::ReadWrite)) return false;
    qint64 bytesPerSpike = static_cast<qint64>(nChannels) * nSamples * 2;
    if (!f.seek(spikeIdx0 * bytesPerSpike)) { f.close(); return false; }
    qint64 nWritten = f.write(reinterpret_cast<const char*>(waveform.data()),
                              static_cast<qint64>(waveform.size() * 2));
    f.close();
    return nWritten == static_cast<qint64>(waveform.size() * 2);
}

// Read all lines of a text file into a vector<QString>.
static bool readTextFile(const QString& path, std::vector<QString>& lines)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    while (!ts.atEnd())
        lines.push_back(ts.readLine());
    f.close();
    return true;
}

// Write vector<QString> back to a text file (overwrite).
static bool writeTextFile(const QString& path, const std::vector<QString>& lines)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream ts(&f);
    for (const auto& l : lines) ts << l << "\n";
    f.close();
    return true;
}

// Swap the waveform of two spikes in .spk.N.
static bool swapSpkEntries(const QString& spkPath, long idxA0, long idxB0,
                            int nChannels, int nSamples)
{
    std::vector<int16_t> a, b;
    if (!readSpkWaveform(spkPath, idxA0, nChannels, nSamples, a)) return false;
    if (!readSpkWaveform(spkPath, idxB0, nChannels, nSamples, b)) return false;
    if (!writeSpkWaveform(spkPath, idxA0, nChannels, nSamples, b)) return false;
    if (!writeSpkWaveform(spkPath, idxB0, nChannels, nSamples, a)) return false;
    return true;
}

bool KlustersDoc::realignSpikes(int clusterId, QString& logOut, int& nShifted, int& nSwapped,
                                std::function<void(const QString&,bool)> liveLog,
                                const QString& args,
                                QVector<float>* meanBefore,
                                QVector<float>* meanAfter,
                                QString* backupBase)
{
    nShifted = 0;
    nSwapped = 0;
    logOut.clear();

    // Helper: emit live if callback provided, otherwise buffer in logOut for later.
    auto emitLine = [&](const QString& line, bool isError = false) {
        if (liveLog) {
            // Split on newlines so each physical line is a separate widget row.
            const QStringList parts = line.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& p : parts)
                liveLog(p, isError);
        } else {
            logOut += line;
        }
    };

    // Convenience: write a formatted line (same interface as QTextStream).
    // We build a local QString and flush via emitLine.
    // Use a lambda that returns a helper object supporting operator<<.
    // Simpler: just write directly with emitLine.
    // We keep a local QTextStream on a buffer and flush after each statement.
    QString _lineBuf;
    QTextStream log(&_lineBuf);
    // After each log << ... << "\n"; call emitFlush() to push it live.
    auto emitFlush = [&]() {
        if (!_lineBuf.isEmpty()) {
            emitLine(_lineBuf);
            _lineBuf.clear();
        }
    };

    const Data& d         = data();
    const int   nChan     = d.nbOfChannels();
    const int   nSamp     = d.nbSamplesPerWaveform();
    const int   peakSamp  = d.peakSampleIndex(); // 1-based (XML value)
    const int   peakSamp0 = peakSamp - 1;        // 0-based waveform index
    const int   timeDim   = d.timeDimension();  // = nDimensions from .fet header
    const int   nFeatCols = timeDim - 1;        // feature columns, last col is ts

    // -----------------------------------------------------------------------
    // Parse configurable parameters from args string.
    // Supported: --threshold F  --iterations N  --maxshift N
    // Defaults:  threshold=0.70  iterations=2  maxshift=peakSamp/2
    // -----------------------------------------------------------------------
    int   maxShift  = std::max(1, peakSamp0 / 2);
    float minScore  = 0.70f;
    int   nIter     = 2;
    {
        const QStringList tokens = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int ti = 0; ti < tokens.size(); ++ti) {
            const QString& tok = tokens[ti];
            if ((tok == QStringLiteral("--threshold") || tok == QStringLiteral("-t"))
                    && ti + 1 < tokens.size()) {
                bool ok2; float v = tokens[++ti].toFloat(&ok2);
                if (ok2 && v > 0.0f && v <= 1.0f) minScore = v;
            } else if ((tok == QStringLiteral("--iterations") || tok == QStringLiteral("-i"))
                    && ti + 1 < tokens.size()) {
                bool ok2; int v = tokens[++ti].toInt(&ok2);
                if (ok2 && v >= 1 && v <= 20) nIter = v;
            } else if ((tok == QStringLiteral("--maxshift") || tok == QStringLiteral("-m"))
                    && ti + 1 < tokens.size()) {
                bool ok2; int v = tokens[++ti].toInt(&ok2);
                if (ok2 && v >= 1) maxShift = v;
            }
        }
    }

    const QString dir   = documentDirectory();
    const QString base  = documentBaseName();
    const QString grpId = currentElectrodeGroupID();

    const QString spkPath = dir + "/" + base + ".spk." + grpId;
    const QString resPath = dir + "/" + base + ".res." + grpId;
    const QString fetPath = dir + "/" + base + ".fet." + grpId;
    const QString cluPath = dir + "/" + base + ".clu." + grpId;
    const QString pcaPath = dir + "/" + base + ".pca." + grpId;

    for (const QString& p : {spkPath, resPath, fetPath}) {
        if (!QFileInfo::exists(p)) {
            log << "ERROR: missing file: " << p << "\n";
            return false;
        }
    }

    log << "Cluster " << clusterId
        << "  nChan=" << nChan << "  nSamp=" << nSamp
        << "  nFeatCols=" << nFeatCols
        << "  timeDim=" << timeDim
        << "  maxShift=+-" << maxShift
        << "  backend=" << XcorrDispatch::backendName() << "\n";
    emitFlush();

    // -----------------------------------------------------------------------
    // Cluster spike indices
    // -----------------------------------------------------------------------
    SortableTable spkTable;
    if (!clusteringData->spikePositions(clusterId, spkTable)) {
        log << "ERROR: cluster " << clusterId << " not found.\n";
        return false;
    }
    const int64_t N = static_cast<int64_t>(spkTable.nbOfColumns());
    if (N == 0) { log << "Cluster is empty.\n"; emitFlush(); return true; }
    log << N << " spikes in cluster.\n";
    emitFlush();

    // gidx[i] = 0-based global file index of cluster spike i
    std::vector<int64_t> gidx(static_cast<size_t>(N));
    for (int64_t i = 0; i < N; ++i)
        gidx[static_cast<size_t>(i)] =
            static_cast<int64_t>(spkTable(1, static_cast<dataType>(i + 1))) - 1;

    // -----------------------------------------------------------------------
    // No backup needed: writes are deferred until saveDocument() is called.
    // -----------------------------------------------------------------------
    if (backupBase) *backupBase = QString();  // kept for API compatibility

    // -----------------------------------------------------------------------
    // Load PCA eigenvectors (per-channel basis)
    // -----------------------------------------------------------------------
    struct PcaBasis {
        int  nCh=0, data2use=0, nComp=0, recShift=0;
        bool centered = false;
        std::vector<std::vector<double>> means;  // [ch][data2use]
        std::vector<std::vector<double>> evec;   // [ch][data2use * nComp] col-major
        bool valid() const { return nCh>0 && data2use>0 && nComp>0; }
    } pca;

    if (QFileInfo::exists(pcaPath)) {
        FILE* fp = fopen(pcaPath.toLocal8Bit().constData(), "rb");
        if (fp) {
            int32_t hdr[5] = {};
            bool ok = (fread(hdr, sizeof(int32_t), 5, fp) == 5);
            if (ok) {
                pca.nCh      = (int)hdr[0];
                pca.data2use = (int)hdr[1];
                pca.nComp    = (int)hdr[2];
                pca.centered = (hdr[3] != 0);
                pca.recShift = (int)hdr[4];
                if (pca.nCh<=0    || pca.nCh>64       ||
                    pca.data2use<=0 || pca.data2use>4096 ||
                    pca.nComp<=0   || pca.nComp>64       ||
                    pca.recShift<0 || pca.recShift+pca.data2use>nSamp) {
                    log << "WARNING: .pca header out of range (nCh="
                        << pca.nCh << " data2use=" << pca.data2use
                        << " nComp=" << pca.nComp << " recShift="
                        << pca.recShift << ") — ignoring .pca file\n";
                    pca = PcaBasis{}; ok = false;
                }
            }
            if (ok) {
                pca.means.resize(static_cast<size_t>(pca.nCh));
                for (int ch = 0; ch < pca.nCh && ok; ++ch) {
                    pca.means[static_cast<size_t>(ch)].resize(
                        static_cast<size_t>(pca.data2use));
                    ok = (fread(pca.means[static_cast<size_t>(ch)].data(),
                                sizeof(double),
                                static_cast<size_t>(pca.data2use), fp)
                          == static_cast<size_t>(pca.data2use));
                }
            }
            if (ok) {
                const size_t evSz = static_cast<size_t>(pca.data2use)
                                  * static_cast<size_t>(pca.nComp);
                try {
                    pca.evec.resize(static_cast<size_t>(pca.nCh));
                    for (int ch = 0; ch < pca.nCh && ok; ++ch) {
                        pca.evec[static_cast<size_t>(ch)].resize(evSz);
                        ok = (fread(pca.evec[static_cast<size_t>(ch)].data(),
                                    sizeof(double), evSz, fp) == evSz);
                    }
                } catch (const std::bad_alloc&) {
                    log << "WARNING: .pca evec allocation failed (data2use="
                        << pca.data2use << " nComp=" << pca.nComp
                        << ") — .pca file is corrupt or wrong format, ignoring\n";
                    pca = PcaBasis{}; ok = false;
                }
            }
            if (!ok) pca = PcaBasis{};
            fclose(fp);
        }
    }

    const int nPcaFeats   = pca.valid() ? (pca.nCh * pca.nComp) : 0;
    const int nExtraFeats = (pca.valid() && nPcaFeats < nFeatCols)
                            ? (nFeatCols - nPcaFeats) : 0;
    if (!pca.valid())
        log << "WARNING: .pca unavailable — features will not be recomputed.\n";
    else
        log << "PCA: " << pca.nCh << "ch x " << pca.nComp
            << "comp  recShift=" << pca.recShift
            << (pca.centered ? " centered" : "")
            << "  extraFeats=" << nExtraFeats << "\n";
    emitFlush();

    // -----------------------------------------------------------------------
    // Load cluster waveforms from binary .spk (int16, no header)
    // Layout: spike 0 samples, spike 1 samples, ...
    // Each spike: nChan * nSamp int16 values
    // -----------------------------------------------------------------------
    const size_t  spkElems      = static_cast<size_t>(nChan)
                                * static_cast<size_t>(nSamp);
    const int64_t bytesPerSpike = static_cast<int64_t>(spkElems) * 2;

    log << "Loading " << N << " waveforms ("
        << (N * bytesPerSpike / (1024*1024)) << " MB)...\n";
    emitFlush();

    std::vector<int16_t> wavBuf(static_cast<size_t>(N) * spkElems);
    {
        FILE* sf = fopen(spkPath.toLocal8Bit().constData(), "rb");
        if (!sf) {
            log << "ERROR: cannot open " << spkPath << "\n";
            return false;
        }
        for (int64_t i = 0; i < N; ++i) {
            const off_t off = (off_t)(gidx[static_cast<size_t>(i)] * bytesPerSpike);
            if (fseeko(sf, off, SEEK_SET) != 0) {
                fclose(sf);
                log << "ERROR: .spk seek failed at spike "
                    << gidx[static_cast<size_t>(i)] << "\n";
                return false;
            }
            int16_t* dst = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            if (fread(dst, 2, spkElems, sf) != spkElems) {
                fclose(sf);
                log << "ERROR: .spk short read at spike "
                    << gidx[static_cast<size_t>(i)] << "\n";
                return false;
            }
        }
        fclose(sf);
    }
    log << "Waveforms loaded.\n";
    emitFlush();

    // -----------------------------------------------------------------------
    // Transpose wavBuf from sample-major (.spk on-disk: [s * nChan + ch])
    // to channel-major (algorithm + XcorrDispatch: [ch * nSamp + s]).
    // -----------------------------------------------------------------------
    {
        std::vector<int16_t> cm(static_cast<size_t>(N) * spkElems);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* src = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            int16_t* dst = cm.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (int s = 0; s < nSamp; ++s)
                for (int ch = 0; ch < nChan; ++ch)
                    dst[ch * nSamp + s] = src[s * nChan + ch];
        }
        wavBuf = std::move(cm);
    }

    // Capture before-mean in channel-major order for the review dialog.
    if (meanBefore) {
        meanBefore->resize(static_cast<int>(spkElems));
        for (size_t e = 0; e < spkElems; ++e) {
            double acc = 0.0;
            for (int64_t i = 0; i < N; ++i)
                acc += wavBuf[static_cast<size_t>(i) * spkElems + e];
            (*meanBefore)[static_cast<int>(e)] = static_cast<float>(acc / N);
        }
    }

    // -----------------------------------------------------------------------
    // Read cluster timestamps and extra feature columns from binary files.
    //
    // Binary .res: N_total * int64_t, no header.
    //   spike p (0-based) → byte offset p * 8
    //
    // Binary .fet: int32_t nDimensions; then N_total * nDimensions * int64_t
    //   spike p (0-based), column c (0-based) →
    //     byte offset sizeof(int32_t) + (p * nDimensions + c) * 8
    //   columns 0..nFeatCols-1 = features; column nFeatCols = timestamp
    // -----------------------------------------------------------------------
    std::vector<int64_t> clusterTs(static_cast<size_t>(N));
    // Extra non-PCA feature columns per cluster spike [spike][col]
    std::vector<std::vector<int64_t>> extraFeats;
    if (nExtraFeats > 0)
        extraFeats.assign(static_cast<size_t>(N),
                          std::vector<int64_t>(static_cast<size_t>(nExtraFeats), 0));

    {
        FILE* rf = fopen(resPath.toLocal8Bit().constData(), "rb");
        if (!rf) {
            log << "ERROR: cannot open " << resPath << "\n";
            return false;
        }
        FILE* ff = fopen(fetPath.toLocal8Bit().constData(), "rb");
        if (!ff) {
            fclose(rf);
            log << "ERROR: cannot open " << fetPath << "\n";
            return false;
        }

        // Validate .fet nDimensions header
        int32_t fetNDim = 0;
        if (fread(&fetNDim, sizeof(int32_t), 1, ff) != 1) {
            fclose(rf); fclose(ff);
            log << "ERROR: cannot read .fet header\n";
            return false;
        }
        if (fetNDim != (int32_t)timeDim) {
            log << "WARNING: .fet nDimensions=" << fetNDim
                << " but timeDim=" << timeDim
                << " — proceeding with file value\n";
        }
        const int32_t fileDim = (fetNDim > 0) ? fetNDim : (int32_t)timeDim;

        for (int64_t i = 0; i < N; ++i) {
            const int64_t p = gidx[static_cast<size_t>(i)];

            // Read timestamp from .res at byte offset p*8
            if (fseeko(rf, (off_t)(p * (int64_t)sizeof(int64_t)), SEEK_SET) != 0 ||
                fread(&clusterTs[static_cast<size_t>(i)],
                      sizeof(int64_t), 1, rf) != 1) {
                fclose(rf); fclose(ff);
                log << "ERROR: cannot read .res at spike " << p << "\n";
                return false;
            }

            // Read extra feature columns from .fet row p
            for (int k = 0; k < nExtraFeats; ++k) {
                const int col = nPcaFeats + k;
                const off_t off = (off_t)sizeof(int32_t)
                                + (off_t)(p * (int64_t)fileDim + col)
                                * (off_t)sizeof(int64_t);
                if (fseeko(ff, off, SEEK_SET) != 0 ||
                    fread(&extraFeats[static_cast<size_t>(i)][static_cast<size_t>(k)],
                          sizeof(int64_t), 1, ff) != 1) {
                    log << "WARNING: cannot read extra feat col=" << col
                        << " spike=" << p << "\n";
                }
            }
        }
        fclose(rf);
        fclose(ff);
    }

    // -----------------------------------------------------------------------
    // Iterative xcorr alignment on waveform buffer
    // -----------------------------------------------------------------------
    std::vector<int>   cumShift(static_cast<size_t>(N), 0);
    std::vector<float> bestScore(static_cast<size_t>(N), 0.0f);

    for (int iter = 0; iter < nIter; ++iter) {
        // Build mean template from current wavBuf
        std::vector<int64_t> acc(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e)
                acc[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> tmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e)
            tmpl[e] = static_cast<int16_t>(acc[e] / N);

        // Pre-align template: shift so its peak lands at peakSamp0.
        // Without this the xcorr moves every spike away from the true peak.
        {
            int    tmplPeak = 0;
            double bestAmp  = -1.0;
            for (int t = 0; t < nSamp; ++t) {
                double amp = 0.0;
                for (int ch = 0; ch < nChan; ++ch)
                    amp += std::abs(static_cast<double>(
                               tmpl[static_cast<size_t>(ch * nSamp + t)]));
                if (amp > bestAmp) { bestAmp = amp; tmplPeak = t; }
            }
            const int tShift = peakSamp0 - tmplPeak;
            if (tShift != 0) {
                // To move the peak from tmplPeak to peakSamp0: src = (t - tShift + N) % N
                std::vector<int16_t> shifted(spkElems);
                for (int ch = 0; ch < nChan; ++ch)
                    for (int t = 0; t < nSamp; ++t) {
                        const int src = (t - tShift + nSamp) % nSamp;
                        shifted[static_cast<size_t>(ch * nSamp + t)] =
                            tmpl[static_cast<size_t>(ch * nSamp + src)];
                    }
                tmpl = std::move(shifted);
            }
        }

        std::vector<int>   sh(static_cast<size_t>(N), 0);
        std::vector<float> sc(static_cast<size_t>(N), 0.0f);
        int rc = XcorrDispatch::compute(
            wavBuf.data(), tmpl.data(),
            static_cast<int>(N), nChan, nSamp,
            maxShift, minScore, sh.data(), sc.data());
        if (rc != 0) {
            log << "ERROR: XcorrDispatch rc=" << rc << "\n";
            return false;
        }

        int changed = 0;
        for (int64_t i = 0; i < N; ++i) {
            const int s = sh[static_cast<size_t>(i)];
            if (s == 0) continue;
            int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            // Circular shift: newSpike[t] = oldSpike[(t + shift) % N].
            // Derivation: score(lag) = Σ tmpl[s]·spike[(s+lag)%N] peaks at
            // lag = bestLag when spike[(s+bestLag)%N] ≈ tmpl[s], so
            // newSpike[t] = spike[(t+bestLag)%N] aligns the waveform.
            std::vector<int16_t> tmp(spkElems);
            for (int t = 0; t < nSamp; ++t) {
                const int src = (t + s + nSamp) % nSamp;
                for (int ch = 0; ch < nChan; ++ch)
                    tmp[static_cast<size_t>(ch * nSamp + t)] =
                        w[static_cast<size_t>(ch * nSamp + src)];
            }
            std::copy(tmp.begin(), tmp.end(), w);
            cumShift[static_cast<size_t>(i)] += s;
            bestScore[static_cast<size_t>(i)] = sc[static_cast<size_t>(i)];
            ++changed;
        }
        log << "  iter " << (iter+1) << ": " << changed << " shifted\n";
        if (changed == 0) break;
    }

    for (int64_t i = 0; i < N; ++i)
        if (cumShift[static_cast<size_t>(i)] != 0) ++nShifted;
    log << nShifted << " spike(s) shifted.\n";
    emitFlush();

    // -----------------------------------------------------------------------
    // Score / shift statistics
    // -----------------------------------------------------------------------
    {
        // Scores: bestScore[i] > 0 only for shifted spikes.
        // Collect all per-spike final scores (run one more xcorr pass
        // on the final template to get scores for unshifted spikes too).
        std::vector<int64_t> acc2(spkElems, 0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (size_t e = 0; e < spkElems; ++e)
                acc2[e] += static_cast<int64_t>(w[e]);
        }
        std::vector<int16_t> finalTmpl(spkElems);
        for (size_t e = 0; e < spkElems; ++e)
            finalTmpl[e] = static_cast<int16_t>(acc2[e] / N);

        std::vector<int>   dummySh(static_cast<size_t>(N), 0);
        std::vector<float> allScores(static_cast<size_t>(N), 0.0f);
        // Use maxShift=0 so no shifting occurs — we just want the scores.
        XcorrDispatch::compute(
            wavBuf.data(), finalTmpl.data(),
            static_cast<int>(N), nChan, nSamp,
            0, 0.0f, dummySh.data(), allScores.data());

        float scoreMin =  2.0f, scoreMax = -2.0f, scoreSum = 0.0f;
        int   nBelow   = 0;
        int   shiftMin = INT_MAX, shiftMax = INT_MIN;
        float shiftAbsSum = 0.0f;

        for (int64_t i = 0; i < N; ++i) {
            const float s  = allScores[static_cast<size_t>(i)];
            const int   sh = cumShift[static_cast<size_t>(i)];
            if (s < scoreMin) scoreMin = s;
            if (s > scoreMax) scoreMax = s;
            scoreSum += s;
            if (s < minScore) ++nBelow;
            if (sh < shiftMin) shiftMin = sh;
            if (sh > shiftMax) shiftMax = sh;
            shiftAbsSum += static_cast<float>(sh < 0 ? -sh : sh);
        }
        const float scoreMean = scoreSum / static_cast<float>(N);
        const float shiftMean = shiftAbsSum / static_cast<float>(N);

        log << "\n--- Alignment statistics (" << N << " spikes) ---\n";
        log << "  xcorr score  min=" << QString::number(static_cast<double>(scoreMin), 'f', 3)
            << "  max="              << QString::number(static_cast<double>(scoreMax), 'f', 3)
            << "  mean="             << QString::number(static_cast<double>(scoreMean), 'f', 3)
            << "  (threshold="       << QString::number(static_cast<double>(minScore),  'f', 2) << ")\n";
        log << "  below threshold: " << nBelow << " / " << N
            << " (" << QString::number(100.0 * nBelow / N, 'f', 1) << "%) — left unshifted\n";
        if (nShifted > 0) {
            log << "  shift (samples) min=" << shiftMin
                << "  max=" << shiftMax
                << "  mean-abs=" << QString::number(static_cast<double>(shiftMean), 'f', 2) << "\n";
        }
        log << "\n";
        emitFlush();
    }

    // -----------------------------------------------------------------------
    // Compute new timestamps and sort cluster spikes by new timestamp
    // -----------------------------------------------------------------------
    std::vector<int64_t> newTs(static_cast<size_t>(N));
    for (int64_t i = 0; i < N; ++i)
        newTs[static_cast<size_t>(i)] =
            clusterTs[static_cast<size_t>(i)]
            + static_cast<int64_t>(cumShift[static_cast<size_t>(i)]);

    // sortedOrder[j] = which cluster spike goes to sorted position j
    std::vector<int64_t> sortedOrder(static_cast<size_t>(N));
    std::iota(sortedOrder.begin(), sortedOrder.end(), int64_t{0});
    std::stable_sort(sortedOrder.begin(), sortedOrder.end(),
        [&](int64_t a, int64_t b) {
            return newTs[static_cast<size_t>(a)]
                 < newTs[static_cast<size_t>(b)];
        });

    // The cluster's file positions, sorted ascending, are the destination slots
    std::vector<int64_t> targetPos(gidx.begin(), gidx.end());
    std::sort(targetPos.begin(), targetPos.end());

    for (int64_t j = 0; j < N; ++j) {
        if (gidx[static_cast<size_t>(sortedOrder[static_cast<size_t>(j)])]
                != targetPos[static_cast<size_t>(j)])
            ++nSwapped;
    }
    if (nSwapped > 0)
        log << nSwapped << " spike(s) reordered.\n";

    // -----------------------------------------------------------------------
    // Project waveform onto per-channel PCA basis → full .fet row (timeDim values)
    // Row layout: [pca_ch0_pc0, pca_ch0_pc1, ..., pca_chN_pcK, extras..., timestamp]
    // -----------------------------------------------------------------------
    auto makeFetRow = [&](int64_t csIdx, const int16_t* wav,
                           int64_t ts) -> std::vector<int64_t>
    {
        std::vector<int64_t> row(static_cast<size_t>(timeDim), int64_t{0});

        if (pca.valid() && pca.nCh == nChan) {
            // Per-channel PCA: each channel projected independently
            int outCol = 0;
            for (int ch = 0; ch < pca.nCh; ++ch) {
                const double* E    = pca.evec[static_cast<size_t>(ch)].data();
                const double* mean = pca.means[static_cast<size_t>(ch)].data();
                for (int c = 0; c < pca.nComp; ++c) {
                    double dot = 0.0;
                    for (int j2 = 0; j2 < pca.data2use; ++j2) {
                        double raw = static_cast<double>(
                            wav[static_cast<size_t>(
                                ch * nSamp + pca.recShift + j2)]);
                        const double x = pca.centered
                                         ? (raw - mean[j2]) : raw;
                        dot += E[j2 + c * pca.data2use] * x;
                    }
                    row[static_cast<size_t>(outCol++)] =
                        static_cast<int64_t>(std::llround(dot));
                }
            }
            // Extra (non-PCA) feature columns copied verbatim
            for (int k = 0; k < nExtraFeats; ++k)
                row[static_cast<size_t>(nPcaFeats + k)] =
                    extraFeats[static_cast<size_t>(csIdx)]
                               [static_cast<size_t>(k)];
        } else {
            // No PCA available: copy existing feature values from in-memory Data
            const dataType spikeRow =
                static_cast<dataType>(
                    gidx[static_cast<size_t>(csIdx)] + 1);  // 1-based
            for (int col = 0; col < nFeatCols; ++col)
                row[static_cast<size_t>(col)] =
                    static_cast<int64_t>(
                        clusteringData->featureValue(spikeRow, col + 1));
        }

        // Last column is always the timestamp
        row[static_cast<size_t>(nFeatCols)] = ts;
        return row;
    };

    // -----------------------------------------------------------------------
    // Open raw signal file for re-extraction from .fil/.dat
    // (still needed to fill wavBuf with real signal at corrected timestamps)
    // -----------------------------------------------------------------------
    const int totalNbChan = clusteringData->getTotalNbChannels();
    const QList<int>& groupChannels = clusteringData->getCurrentChannels();

    // Resolve .fil / .dat path
    QString filPath;
    {
        const QString base = spkPath.left(spkPath.lastIndexOf(QLatin1Char('.')));
        const QString noExt = base.left(base.lastIndexOf(QLatin1Char('.')));
        if (QFileInfo::exists(noExt + QStringLiteral(".fil")))
            filPath = noExt + QStringLiteral(".fil");
        else
            filPath = noExt + QStringLiteral(".dat");
    }
    FILE* filF = fopen(filPath.toLocal8Bit().constData(), "rb");
    if (!filF)
        log << "WARNING: cannot open raw file " << filPath
            << " — waveforms will not be re-extracted\n";

    const int64_t totalSamples = filF
        ? (static_cast<int64_t>(QFileInfo(filPath).size())
           / (static_cast<int64_t>(sizeof(short)) * totalNbChan))
        : 0LL;

    // -----------------------------------------------------------------------
    // Write realignment results directly into the persistent .pending files.
    // These were seeded from the originals on document open (and re-seeded
    // after every save/reject), so they are always complete files ready for
    // random-access writes.  The originals remain untouched until save.
    // -----------------------------------------------------------------------
    FILE* spkW = fopen(m_pendingSpkPath.toLocal8Bit().constData(), "r+b");
    FILE* resW = fopen(m_pendingResPath.toLocal8Bit().constData(), "r+b");
    FILE* fetW = fopen(m_pendingFetPath.toLocal8Bit().constData(), "r+b");
    if (!spkW || !resW || !fetW) {
        if (spkW) fclose(spkW);
        if (resW) fclose(resW);
        if (fetW) fclose(fetW);
        if (filF) fclose(filF);
        log << "ERROR: cannot open pending files for writing\n";
        return false;
    }

    PendingRealign pending;
    pending.bytesPerSpike  = bytesPerSpike;
    pending.spkElems       = spkElems;
    pending.timeDim        = timeDim;
    pending.nFeatCols      = nFeatCols;
    pending.records.reserve(static_cast<size_t>(N));

    for (int64_t j = 0; j < N; ++j) {
        const int64_t csIdx = sortedOrder[static_cast<size_t>(j)];
        const int64_t dest  = targetPos[static_cast<size_t>(j)];
        const int64_t ts    = newTs[static_cast<size_t>(csIdx)];

        int16_t* w = wavBuf.data()
            + static_cast<ptrdiff_t>(csIdx) * static_cast<ptrdiff_t>(spkElems);

        // Capture original in-memory values before any update (needed for reject)
        PendingSpkRecord rec;
        rec.destPos = dest;
        rec.ts      = ts;
        rec.origTs  = static_cast<int64_t>(
            clusteringData->featureValue(
                static_cast<dataType>(dest + 1),
                static_cast<int>(timeDim)));  // timestamp is last dim
        rec.origFet.reserve(nFeatCols);
        for (int col = 0; col < nFeatCols; ++col)
            rec.origFet.append(
                clusteringData->featureValue(
                    static_cast<dataType>(dest + 1), col + 1));

        // Re-extract waveform from .fil/.dat at the new timestamp.
        std::vector<int16_t> spkRow(static_cast<size_t>(spkElems));
        if (filF && cumShift[static_cast<size_t>(csIdx)] != 0) {
            const int64_t startSample = ts - static_cast<int64_t>(peakSamp0);
            if (startSample >= 0 && startSample + nSamp <= totalSamples) {
                const off_t rawOff = static_cast<off_t>(startSample)
                                   * static_cast<off_t>(totalNbChan)
                                   * static_cast<off_t>(sizeof(short));
                if (fseeko(filF, rawOff, SEEK_SET) == 0) {
                    std::vector<int16_t> rawFrame(static_cast<size_t>(nSamp * totalNbChan));
                    if (fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filF) == rawFrame.size()) {
                        for (int s = 0; s < nSamp; ++s) {
                            for (int ci = 0; ci < nChan; ++ci) {
                                const int16_t v =
                                    rawFrame[static_cast<size_t>(
                                        s * totalNbChan + groupChannels[ci])];
                                spkRow[static_cast<size_t>(s * nChan + ci)] = v;
                                w[static_cast<size_t>(ci * nSamp + s)] = v;
                            }
                        }
                    } else {
                        for (int s = 0; s < nSamp; ++s)
                            for (int ch = 0; ch < nChan; ++ch)
                                spkRow[static_cast<size_t>(s * nChan + ch)] =
                                    w[static_cast<size_t>(ch * nSamp + s)];
                    }
                } else {
                    for (int s = 0; s < nSamp; ++s)
                        for (int ch = 0; ch < nChan; ++ch)
                            spkRow[static_cast<size_t>(s * nChan + ch)] =
                                w[static_cast<size_t>(ch * nSamp + s)];
                }
            } else {
                for (int s = 0; s < nSamp; ++s)
                    for (int ch = 0; ch < nChan; ++ch)
                        spkRow[static_cast<size_t>(s * nChan + ch)] =
                            w[static_cast<size_t>(ch * nSamp + s)];
            }
        } else {
            for (int s = 0; s < nSamp; ++s)
                for (int ch = 0; ch < nChan; ++ch)
                    spkRow[static_cast<size_t>(s * nChan + ch)] =
                        w[static_cast<size_t>(ch * nSamp + s)];
        }

        rec.fetRow = makeFetRow(csIdx, w, ts);

        // Write into the pending files (random-access, same layout as originals)
        fseeko(spkW, static_cast<off_t>(dest) * static_cast<off_t>(bytesPerSpike), SEEK_SET);
        fwrite(spkRow.data(), sizeof(int16_t), static_cast<size_t>(spkElems), spkW);

        fseeko(resW, static_cast<off_t>(dest) * static_cast<off_t>(sizeof(int64_t)), SEEK_SET);
        fwrite(&ts, sizeof(int64_t), 1, resW);

        const off_t fetOff = static_cast<off_t>(sizeof(int32_t))
            + static_cast<off_t>(dest) * static_cast<off_t>(timeDim)
              * static_cast<off_t>(sizeof(int64_t));
        fseeko(fetW, fetOff, SEEK_SET);
        fwrite(rec.fetRow.data(), sizeof(int64_t), static_cast<size_t>(timeDim), fetW);

        // Update in-memory feature table and timestamp so scatter/feature
        // views reflect the new alignment without requiring a save first.
        {
            QList<dataType> vals;
            vals.reserve(nFeatCols);
            for (int col = 0; col < nFeatCols; ++col)
                vals.append(static_cast<dataType>(rec.fetRow[static_cast<size_t>(col)]));
            clusteringData->updateFeatureRow(
                static_cast<dataType>(dest + 1), vals);
            clusteringData->updateTimestamp(
                static_cast<dataType>(dest + 1),
                static_cast<dataType>(ts));
        }

        rec.spkRow = std::move(spkRow);  // keep copy for flush-to-original
        pending.records.push_back(std::move(rec));
    }

    fclose(spkW);
    fclose(resW);
    fclose(fetW);
    if (filF) fclose(filF);

    // spkFileName already points to m_pendingSpkPath (set on open and kept
    // permanently) — no redirect needed here.
    m_pendingRealign.push_back(std::move(pending));

    log << "Done. " << nShifted << " shifted, " << nSwapped
        << " reordered. (pending save)\n";

    // Capture after-mean for the review dialog.
    if (meanAfter) {
        meanAfter->resize(static_cast<int>(spkElems));
        for (size_t e = 0; e < spkElems; ++e) {
            double acc = 0.0;
            for (int64_t i = 0; i < N; ++i)
                acc += wavBuf[static_cast<size_t>(i) * spkElems + e];
            (*meanAfter)[static_cast<int>(e)] = static_cast<float>(acc / N);
        }
    }

    return true;
}

void KlustersDoc::invalidateWaveformCache(int clusterId)
{
    clusteringData->invalidateWaveformCache(clusterId);
}

void KlustersDoc::invalidateCorrelogramCache(int clusterId)
{
    clusteringData->invalidateCorrelogramCache(clusterId);
}

// ---------------------------------------------------------------------------
// Persistent pending files: init on open, commit+renew on save, reseed on reject
// ---------------------------------------------------------------------------

bool KlustersDoc::initPendingFiles()
{
    // Build the four pending paths from the current originals.
    m_pendingSpkPath = m_origSpkPath + QStringLiteral(".pending");
    m_pendingResPath = m_origResPath + QStringLiteral(".pending");
    m_pendingFetPath = m_origFetPath + QStringLiteral(".pending");
    m_pendingCluPath = docUrl       + QStringLiteral(".pending");

    // Helper: overwrite dst with a fresh copy of src.
    auto seedFile = [](const QString& src, const QString& dst) -> bool {
        QFile::remove(dst);
        if (!QFile::copy(src, dst)) {
            qWarning() << "[initPendingFiles] copy failed:" << src << "->" << dst;
            return false;
        }
        return true;
    };

    const bool ok = seedFile(m_origSpkPath, m_pendingSpkPath)
                 && seedFile(m_origResPath, m_pendingResPath)
                 && seedFile(m_origFetPath, m_pendingFetPath)
                 && seedFile(docUrl,        m_pendingCluPath);

    if (ok) {
        // Redirect the waveform reader and clu writer to the pending files.
        // They will remain here for the entire document session.
        clusteringData->setSpkFileName(m_pendingSpkPath);
        tmpCluFile = m_pendingCluPath;
    }
    return ok;
}

void KlustersDoc::commitAndRenewPending()
{
    // Step 1 — commit: copy each pending file over the original.
    // QFile::copy refuses to overwrite, so remove the target first.
    auto copyOver = [](const QString& src, const QString& dst) {
        QFile::remove(dst);
        if (!QFile::copy(src, dst))
            qWarning() << "[commitAndRenewPending] copy failed:" << src << "->" << dst;
    };
    copyOver(m_pendingSpkPath, m_origSpkPath);
    copyOver(m_pendingResPath, m_origResPath);
    copyOver(m_pendingFetPath, m_origFetPath);
    copyOver(m_pendingCluPath, docUrl);

    // Clear the in-memory queue — all realignment batches are now on disk.
    m_pendingRealign.clear();

    // Step 2 — renew: re-seed the pending files from the fresh originals so
    // the next realignment (or another save cycle) starts from a clean slate.
    initPendingFiles();
}

void KlustersDoc::rejectLastRealign()
{
    if (m_pendingRealign.empty()) return;

    const PendingRealign& p = m_pendingRealign.back();

    // Restore in-memory feature/timestamp data.
    for (const PendingSpkRecord& rec : p.records) {
        clusteringData->updateFeatureRow(
            static_cast<dataType>(rec.destPos + 1),
            rec.origFet);
        clusteringData->updateTimestamp(
            static_cast<dataType>(rec.destPos + 1),
            static_cast<dataType>(rec.origTs));
    }

    m_pendingRealign.pop_back();

    // Re-seed pending files from the untouched originals so the waveform
    // viewer immediately reflects the restored state.
    initPendingFiles();
}
