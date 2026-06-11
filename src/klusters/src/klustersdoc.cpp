#include <algorithm>
#include <functional>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cerrno>          // patch63 — saveDocument errno diagnostics
#include <cstring>         // patch63 — strerror
#include <vector>
#include <stdint.h>
#ifdef _OPENMP
#include <omp.h>            // CPU-fallback realign parallelisation
#endif
#include <QElapsedTimer>    // opt-in per-phase realign timing
#include <chrono>           // inter-cluster gap timestamp (steady_clock)
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
#include "configuration.h"
#include "klustersview.h"
#include "watershed2d.h"
#include "clusterview.h"
#include "klustersdoc.h"
#include "clusterPalette.h"
#include "types.h"
#include "autosavethread.h"
#include "parameteryamlmodifier.h"
#include "dipsplit.h"
#include "parameteryamlreader.h"
#include "clusteruserinformation.h"

//C, C++ include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <math.h>
#include <climits>

#include "timer.h"

#include <neurosuite/core/neurofileio.h>  // variant-aware input resolution

extern int nbUndo;

namespace {
// ── chain-of-custody method helpers ─────────────────────────────────────────
//
// Every per-group artifact is <base>.<type>.<method>.<group> for method in
// {standard,sdiff,stderiv,...}.  The method is read off the .clu anchor at open
// time and pins resolution of all sibling files; there is no preference-guessing
// and no fetIsStderiv/spkIsTransformed inference — method == "stderiv" is the
// single signal for transformed waveforms / stderiv-space features.

// Extract the method token from a per-group path <base>.<type>.<method>.<group>;
// returns "standard" for an untagged legacy name (or anything unparseable).
QString featureMethod(const QString& path) {
    const std::string m = neurofileio::methodFromPath(path.toStdString());
    return m.empty() ? QStringLiteral("standard") : QString::fromStdString(m);
}

// Resolve the method-pinned path <fullBase>.<type>.<method>.<group>.
// Resolve the path for a per-group artifact, preferring the method-tagged form
// but falling back to the shared copy.  Several artifacts have one physical copy
// across methods — .res (spike times are method-independent) and, in the common
// layout, the raw .spk (the stderiv transform is applied downstream at PCA time
// rather than stored as a separate .spk).  So a stderiv .clu must still resolve
// the existing .sp/.res: try <type>.<method>.<grp>, then .<type>.standard.<grp>,
// then untagged .<type>.<grp>, returning the first that exists (or the
// method-tagged path if none, for error reporting).
QString resolveFeature(const QString& fullBase, const QString& type,
                       const QString& group, const QString& method) {
    std::vector<std::string> prefer;
    prefer.push_back(method.toStdString());
    if (method != QLatin1String("standard"))
        prefer.emplace_back("standard");
    prefer.emplace_back("");                       // untagged shared copy
    const neurofileio::ResolvedInput r =
        neurofileio::resolveInput(fullBase.toStdString(), type.toStdString(),
                                  group.toInt(), prefer);
    return QString::fromStdString(r.path);
}

// <base>.<type>.<method>.<group>  ->  <base>  (also handles legacy untagged).
QString stripFeatureSuffix(const QString& path, const QString& type) {
    QString b = path.left(path.lastIndexOf(QLatin1Char('.')));   // strip .<group>
    const int lastDot = b.lastIndexOf(QLatin1Char('.'));
    if (lastDot >= 0) {
        const QString seg = b.mid(lastDot + 1);
        if (seg != type && seg != (type + "D"))                  // a method token
            b = b.left(lastDot);
    }
    return b.left(b.lastIndexOf(QLatin1Char('.')));              // strip .<type>
}
}  // namespace

KlustersDoc::KlustersDoc(QWidget* parent,ClusterPalette& clusterPalette,bool autoSave,int savingInterval)
    : clusterColorListUndoList(),clusterColorListRedoList(),modified(false),docUrl(),parent(parent),clusterPalette(clusterPalette),
    addedClustersUndoList(),addedClustersRedoList(),modifiedClustersUndoList(),modifiedClustersRedoList()
  ,autoSave(autoSave),savingInterval(savingInterval),tracesProvider(nullptr),clustersProvider(nullptr),channelColorList(nullptr)
{
    viewList = new QList<KlustersView*>();
    clusterColorList = nullptr;
    addedClusters = nullptr;
    modifiedClusters = nullptr;
    deletedClusters = nullptr;
    endAutoSaving = false;
    autoSaveThread = nullptr;
}

KlustersDoc::~KlustersDoc(){
    NS3_DIAG() << "~KlustersDoc()";

    // Disconnect all signals between KlustersDoc and KlustersViews before
    // any object starts being destroyed.  Views are parented to Qt widgets
    // and deleted later by the parent-child hierarchy; if doc signals are
    // still connected when those deletions run, Qt dispatches into a dead
    // object and asserts "class destructor may have already run".
    for (KlustersView* v : qAsConst(*viewList)) {
        if (v) {
            QObject::disconnect(this, nullptr, v, nullptr);
            QObject::disconnect(v,    nullptr, this, nullptr);
        }
    }
    delete viewList;

    if(clusterColorList != nullptr){
        delete clusteringData;
        delete clusterColorList;
        delete addedClusters;
        delete modifiedClusters;
        delete deletedClusters;
    }

    //If an autoSaveThread exists and has not finish, wait until it is done
    if(autoSave && autoSaveThread != nullptr){
        if(!autoSaveThread->isRunning()){
            autoSaveThread->removeTmpFile();
            delete autoSaveThread;
            autoSaveThread = nullptr;
        }
        else{
            endAutoSaving = true;
            while(!autoSaveThread->wait()){};
            //Wait that the customEvent has process the AutoSaveEvent and deleted the autoSaveThread
            while(autoSaveThread != nullptr){};
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
    return (static_cast<int>(viewList->count()) == 1);
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
    parameterFile.clear();
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
    qDeleteAll(modifiedClustersRedoList);
    modifiedClustersRedoList.clear();
    clusterIdsNewOldMap.clear();
    clusterIdsOldNewMap.clear();


    if(clusterColorList != nullptr){
        delete clusteringData;
        clusteringData = nullptr;
        delete clusterColorList;
        clusterColorList = nullptr;
        delete addedClusters;
        addedClusters = nullptr;
        delete modifiedClusters;
        modifiedClusters = nullptr;
    }
    //Remove the temp files if any
    tmpCluFile.clear();
    tmpSpikeFile.clear();

    //Variables link to TraceView
    if(channelColorList != nullptr){
        delete channelColorList;
        channelColorList = nullptr;
        delete tracesProvider;
        tracesProvider = nullptr;
        delete clustersProvider;
        clustersProvider = nullptr;
    }

    displayChannelsGroups.clear();
    channelsSpikeGroups.clear();
    displayGroupsChannels.clear();
    spikeGroupsChannels.clear();
    displayGroupsClusterFile.clear();
    gain = 0;
    acquisitionGain = 0;

    // Flush and close the curation log for this session
    if (curationLogger)
        curationLogger->close();
}


bool KlustersDoc::importDocument(const QString &url, const char *format ){
    bool returnValue = true;

    //1 - Get the base name of the file
    //2 - load the config information: Parse the YAML config file, initialize clusteringData (loadConfigFromNewFormat())
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
    deletedClusters = new QList<int>();
    modified = false;

    //Store the baseName for future use
    QFileInfo urlFileInfo(url);

    QString fileName = urlFileInfo.fileName();
    const QStringList fileParts = fileName.split(".", Qt::SkipEmptyParts);
    if(fileParts.count() < 3)
        return INCORRECT_FILE;

    // Parse the opened .clu anchor.  Chain-of-custody form is
    // <base>.clu.<method>.<grp>; an optional non-integer suffix after the group
    // (e.g. .clu.<method>.<grp>.drift / .bak / .merged) shifts the group index.
    // Legacy untagged <base>.clu.<grp> is read as method=standard.  The method
    // pins resolution of every sibling (.spk/.fet/.pca/.res).
    QString sessionMethod = QStringLiteral("standard");
    qsizetype groupIdx = fileParts.count() - 1;
    {
        bool lastIsInt = false;
        (void)fileParts.last().toInt(&lastIsInt);
        if (!lastIsInt && fileParts.count() >= 4) {
            bool prevIsInt = false;
            (void)fileParts[fileParts.count() - 2].toInt(&prevIsInt);
            if (prevIsInt)
                groupIdx = fileParts.count() - 2;   // last token is a post-group suffix
        }
        // Method token sits immediately before the group, unless that slot is
        // the type token "clu" itself (legacy untagged name).
        if (groupIdx >= 2 && fileParts[groupIdx - 1] != QLatin1String("clu"))
            sessionMethod = fileParts[groupIdx - 1];
    }

    // baseName = everything up to the "clu" type token.  With a method tag the
    // type token is at groupIdx-2; without it (legacy) at groupIdx-1.
    {
        const bool tagged = (sessionMethod != QLatin1String("standard"))
                         || (groupIdx >= 2 && fileParts[groupIdx - 2] == QLatin1String("clu"));
        const qsizetype typeIdx = tagged ? (groupIdx - 2) : (groupIdx - 1);
        baseName = fileParts[0];
        for (qsizetype i = 1; i < typeIdx; ++i)
            baseName += "." + fileParts[i];
    }

    electrodeGroupID = fileParts[groupIdx];

    //Create the files url to open (baseName.spk.x,baseName.clu.x,baseName.fet.x,baseName.par.x,baseName.par and baseName.yaml)

    // All siblings are resolved at the anchor's method (chain-of-custody).
    QString spkFileUrl = resolveFeature(
        urlFileInfo.absolutePath() + QDir::separator() + baseName,
        "spk", electrodeGroupID, sessionMethod);

    // docUrl is the .clu file actually opened.
    QString cluFileUrl = urlFileInfo.absoluteFilePath();
    docUrl = cluFileUrl;

    cluFileSaveUrl = urlFileInfo.absolutePath() + QDir::separator() + "." + urlFileInfo.fileName() + ".autosave";


    QString fetFileUrl = resolveFeature(
        urlFileInfo.absolutePath() + QDir::separator() + baseName,
        "fet", electrodeGroupID, sessionMethod);
    //Parameter files
    // Parameter file: YAML only
    const QString yamlParFileUrl = urlFileInfo.absolutePath() + QDir::separator() + baseName + ".yaml";
    parameterFile = yamlParFileUrl;



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

    bool isYamlParExist = false;
    QString tmpYamlParFile;
    QString tmpParXFile;
    QString tmpParFile;
    QFileInfo yamlParFileInfo(yamlParFileUrl);
    QFile yamlParFile;
    QFile parXFile;
    QFile parFile;
    if(yamlParFileInfo.exists()){
        tmpYamlParFile = yamlParFileUrl;
        isYamlParExist = true;
        //Check if the generic parameter file also exist, if so, warn the user that the YAML parameter file will be used.
        QFileInfo parFileInfo(parFileUrl);
        if(parFileInfo.exists()){
            QApplication::restoreOverrideCursor();
            QMessageBox::information(0, tr("Warning!"), tr("Two parameter files were found, %1 and %2. The parameter file %3 will be used.").arg(yamlParFileUrl).arg(parFileUrl).arg(yamlParFileUrl));
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        }
        yamlParFile.setFileName(tmpYamlParFile);
        if(!yamlParFile.open(QIODevice::ReadOnly)){
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
            if(isYamlParExist){
                yamlParFile.close();
            }
            else{
                parXFile.close();
                parFile.close();
            }
            fetFile.close();
            return OPEN_ERROR;
        }

        //Initialize the data
        if(isYamlParExist){
            if(!clusteringData->initialize(fetFile,cluFile,spkFileLength,tmpSpikeFile,yamlParFile,electrodeGroupID.toInt(),errorInformation)){
                //close the files
                yamlParFile.close();
                fetFile.close();
                cluFile.close();
                return INCORRECT_CONTENT;
            }
            yamlParFile.close();
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
        if(isYamlParExist){
            if(!clusteringData->initialize(fetFile,spkFileLength,tmpSpikeFile,yamlParFile,electrodeGroupID.toInt(),errorInformation)){
                //close the files
                yamlParFile.close();
                fetFile.close();
                return INCORRECT_CONTENT;
            }
            yamlParFile.close();
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
            color.setHsv(static_cast<int>(fmod(static_cast<double>(*it)*7,36))*10,200,255);
        clusterColorList->append(static_cast<int>(*it),color);
    }


    //If ask create a thread for the auto saving of the document.
    if(autoSave){
        NS3_DIAG()<<"autoSave = true in openDoc";
        endAutoSaving = false;
        autoSaveThread = new AutoSaveThread(*clusteringData,this,cluFileSaveUrl);
        autoSaveThread->start();
    }

    // Establish the four permanent pending files so the originals are never
    // touched during a session.  spkFileName and tmpCluFile are redirected
    // to the pending paths; they stay there for the whole document lifetime.
    origSpkPath = spkFileUrl;
    origResPath = resolveFeature(
        urlFileInfo.absolutePath() + QDir::separator() + baseName,
        "res", electrodeGroupID, sessionMethod);
    origFetPath = fetFileUrl;
    // clu original == docUrl (set above); clu pending set in initPendingFiles.
    if (!initPendingFiles()) {
        qWarning() << "[openDocument] could not create pending files";
    }

    // ── Curation logger ─────────────────────────────────────────────────
    {
        clusterActionCount.clear();
        lastLoggedActionIdx = -1;
        // Opening the logger is gated by the "Enable curation logging"
        // preference.  When off we leave curationLogger null: every call site
        // is guarded by `if (curationLogger && curationLogger->isOpen())` and
        // logBefore/logAfter early-return, so no per-action audit snapshot
        // (snapshotClusters -> computeAllCentroids) is taken.  This is the
        // low-overhead path for performance testing; undo/redo are unaffected
        // (rollback is driven by clusteringData->undo, not the logger).
        if (configuration().getCurationLogging()) {
            const QString logPath = urlFileInfo.absolutePath() + QDir::separator()
                                    + baseName + ".curation_log." + electrodeGroupID + ".jl";
            curationLogger = std::make_unique<CurationLogger>();
            curationLogger->open(
                logPath,
                baseName + ".clu." + sessionMethod + "." + electrodeGroupID,
                electrodeGroupID,
                clusteringData->getSamplingRate(),
                clusteringData->nbOfchannels(),
                clusteringData->totalNbOfPCAs()
            );
            // Pair the in-memory ring buffer with the user's max-undo
            // preference so every still-undoable action retains a tentative
            // log entry whose status flips on undo/redo.
            curationLogger->setMaxBufferEntries(nbUndo);
        } else {
            curationLogger.reset();
        }
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
    if(autoSave && autoSaveThread != nullptr){
        if(!autoSaveThread->isRunning()){
            autoSaveThread->removeTmpFile();
            delete autoSaveThread;
            autoSaveThread = nullptr;
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
            if(autoSaveThread != nullptr){
                autoSaveThread->removeTmpFile();
                delete autoSaveThread;
                autoSaveThread = nullptr;
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
                QTimer::singleShot(savingInterval*60000, this, &KlustersDoc::launchAutoSave);
        }
    }
}

int KlustersDoc::saveDocument(const QString& saveUrl, const char *format /*=0*/){

    // patch63 — clear any stale error from a previous save attempt.
    lastSaveErrorMessage.clear();

    // Determine whether this is a Save (same URL) or SaveAs (new URL).
    const bool isSaveAs = (docUrl != saveUrl);

    // For a regular Save:  write clu to the pending clu file (crash-safe).
    // For a SaveAs:        write directly to the new URL (no pending for it).
    const QString cluWritePath = isSaveAs ? saveUrl : pendingCluPath;

    //Open the clu file in write mode
    FILE* cluFile = fopen(qPrintable(cluWritePath),"wb");
    if(cluFile == nullptr){
        // patch63 — surface exactly why fopen failed instead of returning
        // a bare error code that becomes a generic "I/O Error" dialog.
        const int e = errno;
        lastSaveErrorMessage = QString(
            "Cannot open file for writing:\n%1\n\n"
            "errno %2 (%3)\n\n"
            "Likely causes:\n"
            "  • File owned by a different user (common after running\n"
            "    process_drifttracker / KiloKlustaKwik as another uid,\n"
            "    e.g. via sudo or a build account)\n"
            "  • Parent directory not writable by the user running Klusters\n"
            "  • File or its directory marked immutable (chattr +i)\n"
            "  • File locked by another process (lsof to check)\n"
            "  • Filesystem mounted read-only or out of space")
            .arg(cluWritePath)
            .arg(e)
            .arg(QString::fromLocal8Bit(std::strerror(e)));
        qWarning().noquote() << "[saveDocument] fopen failed:"
                             << cluWritePath << "errno" << e
                             << QString::fromLocal8Bit(std::strerror(e));
        return OPEN_ERROR;
    }

    if(!clusteringData->saveClusters(cluFile)){
        // patch63 — write failed mid-save (disk full, I/O error).
        fclose(cluFile);
        lastSaveErrorMessage = QString(
            "saveClusters() failed while writing to:\n%1\n\n"
            "errno %2 (%3)\n\n"
            "Likely cause: disk full or I/O error.")
            .arg(cluWritePath)
            .arg(errno)
            .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return SAVE_ERROR;
    }

    //close the file
    fclose(cluFile);

    QString cluFileSuffix_;
    QString saMethod = QStringLiteral("standard");   // method parsed from a SaveAs target
    // For SaveAs: update doc URL and derived paths before committing.
    if(isSaveAs){
        docUrl = saveUrl;
        QFileInfo docUrlFileInfo(docUrl);
        QString fileName = docUrlFileInfo.fileName();
        const QStringList fileParts = fileName.split(".", Qt::SkipEmptyParts);

        // Same scan-from-end parser as openDocument.  Chain-of-custody form is
        // <base>.<type>.<method>.<grp>[.suffix]; the method token sits between
        // the type token and the integer group.  A SaveAs to a tagged path
        // (e.g. foo.clu.stderiv.8 or foo.clu.standard.8.stack) must update
        // baseName / electrodeGroupID / saMethod / cluFileSuffix_ correctly.
        static const QStringList kTypeTokens = {
            QStringLiteral("clu"), QStringLiteral("fet"),
            QStringLiteral("spk"), QStringLiteral("par"),
        };
        qsizetype typeIdx = -1;
        for (qsizetype probe = 2; probe <= 5 && probe <= fileParts.count(); ++probe) {
            const qsizetype idx = fileParts.count() - probe;
            if (idx < 1) break;
            if (kTypeTokens.contains(fileParts[idx])) { typeIdx = idx; break; }
        }
        if (typeIdx >= 0) {
            baseName = fileParts.first();
            for (qsizetype i = 1; i < typeIdx; ++i)
                baseName += "." + fileParts.at(i);
            // After the type token: [.<method>].<grp>[.<suffix>].
            qsizetype grpIdx = typeIdx + 1;
            bool nextIsInt = false;
            (void)fileParts.at(typeIdx + 1).toInt(&nextIsInt);
            if (!nextIsInt && typeIdx + 2 < fileParts.count()) {
                saMethod = fileParts.at(typeIdx + 1);     // method token
                grpIdx   = typeIdx + 2;
            }
            electrodeGroupID = fileParts.at(grpIdx);
            cluFileSuffix_.clear();
            for (qsizetype i = grpIdx + 1; i < fileParts.count(); ++i)
                cluFileSuffix_ += QLatin1Char('.') + fileParts.at(i);
        } else {
            // Legacy fallback (unrecognised filename) — preserves old behaviour
            // so callers that don't pass a tagged form still work.
            baseName = fileParts.first();
            if(fileParts.count() > 2)  {
                for(qsizetype i = 1;i < fileParts.count()-2; ++i){
                    baseName += "." + fileParts.at(i);
                }
            }
            if(fileParts.count() < 3)
                electrodeGroupID.clear();
            else
                electrodeGroupID = fileParts.at(fileParts.count()-1);
            cluFileSuffix_.clear();
        }

        parameterFile = docUrlFileInfo.absoluteFilePath() + QDir::separator() + baseName + ".yaml";
    }

    //Save the cluster user information if the parameterFile exists
    //NB : for the moment, the specific errors are not return to the user, only a generic message (document could not be saved).
    if(clusteringData->isTraceViewVariablesAvailable()){
        //Save the document information
        NS3_DIAG()<<" parameterFile"<<parameterFile;
        QFileInfo parFileInfo = QFileInfo(parameterFile);

        //Check that the file is writable
        if(!parFileInfo.isWritable()) {
            return NOT_WRITABLE;
        }

        QMap<int,ClusterUserInformation> clusterUserInformationMap = QMap<int,ClusterUserInformation>();
        clusteringData->getClusterUserInformation(electrodeGroupID.toInt(),clusterUserInformationMap);

        // Always YAML
        {
            // Build the merged units map: read existing units, overwrite the
            // units for the current electrode group, then write all back.
            ParameterYamlModifier yamlMod;
            if (!yamlMod.parseFile(parameterFile))
                return PARSE_ERROR;

            // Read existing units (all groups)
            QMap<int,QStringList> allUnits;
            {
                ParameterYamlReader reader;
                if (reader.parseFile(parameterFile))
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
            if (!yamlMod.writeToFile(parameterFile))
                return CREATION_ERROR;
        }
    }

    // Commit pending files → originals, then re-seed pending from fresh originals.
    // For SaveAs, update the original paths first so commitAndRenewPending()
    // copies to the correct new location.
    if (isSaveAs) {
        QFileInfo newInfo(docUrl);
        // Re-resolve at the new location, pinned to the method parsed from the
        // SaveAs target (chain-of-custody).
        const QString newBase = newInfo.absolutePath() + QDir::separator() + baseName;
        origSpkPath = resolveFeature(newBase, "spk", electrodeGroupID, saMethod);
        origResPath = resolveFeature(newBase, "res", electrodeGroupID, saMethod);
        origFetPath = resolveFeature(newBase, "fet", electrodeGroupID, saMethod);
    }
    // patch63 — commitAndRenewPending now reports failure.  When the
    // pending → original copy fails (typical: NTFS/fuseblk permission
    // issue, restrictive ACL, locked target), the pending .clu has the
    // user's edits but the final .clu file is unchanged.  Without this
    // check Klusters reported "save successful" while the on-disk file
    // was stale — extremely confusing.
    QString commitError;
    if (!commitAndRenewPending(&commitError)) {
        lastSaveErrorMessage = commitError;
        return SAVE_ERROR;
    }

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

QList<int> KlustersDoc::getSiblingElectrodeGroups(int groupId) const
{
    QList<int> result;
    if (parameterFile.isEmpty()) return result;

    ParameterYamlReader reader;
    if (!reader.parseFile(parameterFile)) return result;
    return reader.getSiblingElectrodeGroups(groupId);
}

void KlustersDoc::setGain(int acquisitionGain){
    //Notify all the views of the modification
    for(int i =0; i<viewList->count();++i) {
        KlustersView *view = viewList->at(i);
        view->setGain(acquisitionGain);
    }

    //Get the active view.
    KlustersView* activeView = app()->activeView();

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
    KlustersView* activeView = app()->activeView();

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
    KlustersView* activeView = app()->activeView();

    //Notify all the views of the modification
    for (KlustersView* view : *viewList)
        view->setTimeStepInSecond(step, view == activeView);

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
    KlustersView* activeView = app()->activeView();

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::singleColorUpdate(int clusterId,KlustersView& activeView){
    //Notify all the views of the modification

    for (KlustersView* view : *viewList)
        view->singleColorUpdate(clusterId, view == &activeView);

    //Ask the active view to take the modification into account immediately
    activeView.showAllWidgets();
}


void KlustersDoc::shownClustersUpdate(const QList<int>& clustersToShow,KlustersView& activeView){
    if(clusterColorList->isColorChanged()){
        //Notify all the views of the modification

        for (KlustersView* view : *viewList)
            view->updateColors(view == &activeView);

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

void KlustersDoc::shownClustersUpdate(const QList<int>& clustersToShow){
    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::shownClustersUpdate(const QList<int>& clustersToShow,const QList<int>& previousSelectedClusterPairs){
    //Get the clusters currently selected
    QList<int> currentShownClusters = clusterPalette.selectedClusters();

    //Add the clusters which were shown and not part of the previous selected cluster pairs
    QList<int> mergedClusters = clustersToShow;
    for (int c : currentShownClusters)
        if (!previousSelectedClusterPairs.contains(c)) mergedClusters.append(c);

    //Update the palette of cluster
    clusterPalette.selectItems(mergedClusters);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(mergedClusters);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::showAllClustersExcept(const QList<int>& clustersToHide){

    QList<dataType> clusterList = clusteringData->clusterIds();
    QList<int> clustersToShow;

    QList<dataType>::iterator clustersToAdd;
    for(clustersToAdd = clusterList.begin(); clustersToAdd != clusterList.end(); ++clustersToAdd ){
        if(!clustersToHide.contains(static_cast<int>(*clustersToAdd))) clustersToShow.append(static_cast<int>(*clustersToAdd));
    }

    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::addClustersToActiveView(const QList<int>& clustersToShow){
    //Get the clusters currently selected
    QList<int> currentShownClusters = clusterPalette.selectedClusters();

    QList<int> mergedClusters = clustersToShow;
    for (int v : currentShownClusters)
        mergedClusters.append(v);

    //Update the palette of cluster
    clusterPalette.selectItems(mergedClusters);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(mergedClusters);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::groupClusters(QList<int> clustersToGroup,KlustersView& activeView){
    //Call data to group the clusters
    logBefore(CurationLogger::ActionType::GROUP, clustersToGroup);
    float newClusterId = clusteringData->groupClusters(clustersToGroup);
    int newClusterIdint = static_cast<int>(newClusterId);

    //Prepare the undo
    prepareUndo(newClusterIdint,clustersToGroup);

    //Add the cluster in clusterColors.
    QColor color;
    color.setHsv(static_cast<int>(fmod(newClusterId*7,36))*10,200,255);
    clusterColorList->append(newClusterIdint,color);

    //Remove the clusters which were grouped
    QList<int>::iterator clustersToRemove;
    QList<int>::iterator clustersToRemoveEnd(clustersToGroup.end());
    for (clustersToRemove = clustersToGroup.begin(); clustersToRemove != clustersToRemoveEnd; ++clustersToRemove ){
        clusterColorList->remove(*clustersToRemove);
    }

    //Notify all the views of the modification

    for (KlustersView* view : *viewList) {
        const bool isActive = (view == &activeView);
        view->groupedClustersUpdate(clustersToGroup, newClusterIdint, isActive);
        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
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
    logAfter(clustersToShow);
}


void KlustersDoc::moveSpikeSubsetToCluster(int fromCluster,
                                            const QVector<int>& spkFileIndices,
                                            int toCluster,
                                            KlustersView& activeView)
{
    if (spkFileIndices.isEmpty()) return;

    logBefore(CurationLogger::ActionType::MOVE_SPIKES,
              QList<int>{ fromCluster, toCluster });

    // Convert 0-based .spk indices to 1-based feature-row indices.
    QSet<dataType> featureRowSet;
    featureRowSet.reserve(spkFileIndices.size());
    for (int idx : spkFileIndices)
        featureRowSet.insert(static_cast<dataType>(idx + 1));

    QList<int> fromClusters, emptiedClusters;
    clusteringData->moveSpikeSubset(fromCluster, featureRowSet,
                                     toCluster, fromClusters, emptiedClusters);

    if (fromClusters.isEmpty()) {
        activeView.showAllWidgets();
        return;
    }

    QList<int> updatedClusters = {fromCluster, toCluster};

    // Ensure cluster 1 (noise) has its grey colour when first receiving spikes.
    if (toCluster == 1 && !clusterColorList->contains(1)) {
        QColor grey;
        grey.setHsv(0, 0, 220);
        if (clusterColorList->contains(0)) clusterColorList->insert(1, grey, 1);
        else                               clusterColorList->insert(1, grey, 0);
    }

    prepareUndo(updatedClusters, emptiedClusters, true);

    QList<int> clustersToShow = {fromCluster, toCluster};
    for (int cid : emptiedClusters) {
        clusterColorList->remove(cid);
        clustersToShow.removeAll(cid);
    }

    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        const bool isActive = (v == &activeView);
        v->removeSpikesFromClustersInView(fromClusters, toCluster, emptiedClusters, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    emit removeSpikesFromClusters(fromClusters, toCluster, emptiedClusters);

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    activeView.showAllWidgets();
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);
    logAfter(clustersToShow);
}


void KlustersDoc::deleteClusters(QList<int> clustersToDelete,KlustersView& activeView,int clusterId){
    // Log before the spikes move so we capture the source cluster characteristics
    logBefore(clusterId == 1 ? CurationLogger::ActionType::DELETE_NOISE
                              : CurationLogger::ActionType::DELETE_ARTEFACT,
              clustersToDelete);

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

    for (KlustersView* view : *viewList) {
        const bool isActive = (view == &activeView);
            view->clustersDeletionUpdate(clustersToDelete,clusterId, isActive);
            view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
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
    // Log the resulting state of the destination cluster (noise=1, artefact=0)
    logAfter(QList<int>{ clusterId });
}

void KlustersDoc::deleteArtifact(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    logBefore(CurationLogger::ActionType::DELETE_REGION_ARTEFACT,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));
    deleteSpikesFromClusters(0,region,clustersOfOrigin,dimensionX,dimensionY);
    logAfter(QList<int>{ 0 });
}


void KlustersDoc::deleteNoise(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    logBefore(CurationLogger::ActionType::DELETE_REGION_NOISE,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));
    deleteSpikesFromClusters(1,region,clustersOfOrigin,dimensionX,dimensionY);
    logAfter(QList<int>{ 1 });
}

void KlustersDoc::deleteSpikesFromClusters(int destination, QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    QList<int> clustersToShow(clustersOfOrigin);

    clusteringData->deleteSpikesFromClusters(region,clustersOfOrigin,destination,dimensionX,dimensionY,fromClusters,emptyClusters);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

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

        for (KlustersView* view : *viewList) {
            const bool isActive = (view == activeView);
                view->removeSpikesFromClustersInView(fromClusters,destination,emptyClusters, isActive);
                view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
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


// ---------------------------------------------------------------------------
// KlustersDoc::commitClusterCreation
//
// Shared post-mutation UI plumbing for any operation that produces ONE new
// cluster derived from existing ones.  Used by createNewCluster (polygon
// selection) and dipSplitApply (bimodality split commit).  Keeping this in
// one place ensures these paths can't drift apart and re-introduce bugs
// like the missing palette refresh that hid DipSplit's freshly-created
// cluster.
// ---------------------------------------------------------------------------
void KlustersDoc::commitClusterCreation(int newId,
                                         QList<int>& fromClusters,
                                         QList<int>& emptiedClusters,
                                         KlustersView* activeView)
{
    // Undo bookkeeping FIRST — before mutating clusterColorList.
    //
    // prepareUndo() deep-copies the current clusterColorList onto the undo
    // stack and swaps in a fresh copy as the new "current".  If we append
    // the new cluster's colour BEFORE this snapshot, both the snapshot AND
    // the new current contain the new cluster — so when the user later
    // undoes the action, the colour-list rolls back to a state that still
    // has the new cluster's entry, even though Data::undo has rolled the
    // spike table back to a state where the new cluster has no spikes.
    // The palette then renders an icon for a cluster that no longer
    // exists in Data; clicking it dereferences nothing → segfault.
    //
    // The matching pattern in groupClusters (line 1032) gets this right:
    // prepareUndo is called BEFORE clusterColorList->append.
    prepareUndo(newId, fromClusters, emptiedClusters);

    // Register colour AFTER the snapshot is captured — this puts newId
    // into the post-action clusterColorList, which is what the view
    // notifications below need to look up its colour.
    QColor color;
    color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10, 200, 255);
    clusterColorList->append(newId, color);

    // Build clustersToShow: active view's current set + new cluster −
    // emptied clusters.
    QList<int> clustersToShow;
    if (activeView) {
        const QList<int>& cur = activeView->clusters();
        for (int c : cur) clustersToShow.append(c);
    }
    if (!clustersToShow.contains(newId)) clustersToShow.append(newId);

    // Drop emptied clusters from the colour list and to-show list.
    for (int cid : emptiedClusters) {
        clusterColorList->remove(cid);
        clustersToShow.removeAll(cid);
    }

    // Per-view notification.
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* view = viewList->at(i);
        const bool isActive = (view == activeView);
        view->addNewClusterToView(fromClusters, newId,
                                  emptiedClusters, isActive);
        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    // Notify the error-matrix / template-matrix views.
    emit newClusterAdded(fromClusters, newId, emptiedClusters);

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    // Refresh active view widgets + cluster palette.
    if (activeView) activeView->showAllWidgets();
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);

    // Invalidate waveform + correlogram caches for every source cluster
    // that lost spikes — the cached mean waveform / correlogram histogram
    // no longer matches the on-disk spike list.  Also tell every view to
    // re-draw these clusters so any cached pixmaps are dropped and the
    // worker threads re-launch.  The new cluster itself has no caches yet,
    // so invalidation isn't required for it; its waveform thread is
    // launched by addNewClusterToView above.
    //
    // Data::createNewCluster does equivalent invalidation internally;
    // Data::moveSpikeSubset does not, so DipSplit needs it here.  Running
    // both for createNewCluster's path is idempotent — invalidate on an
    // empty cache is a no-op.
    for (int cid : fromClusters) {
        if (emptiedClusters.contains(cid)) continue;   // already removed
        invalidateWaveformCache(cid);
        invalidateCorrelogramCache(cid);
    }
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        for (int cid : fromClusters) {
            if (emptiedClusters.contains(cid)) continue;
            v->invalidateClusterDisplay(cid);
        }
    }
}

// ---------------------------------------------------------------------------
// KlustersDoc::commitTwoClusterCreation
//
// Sibling of commitClusterCreation for paths that produce TWO new clusters
// from one (or more) source clusters in a single atomic Data mutation.
// Used by dipSplitApply.
//
// Architecturally this is the recluster pattern (as used by watershed):
//   - prepareReclusteringUndo(newClusterList, inputs)  — single doc-side
//     undo entry covering N source clusters dissolving into M new
//     clusters.
//   - addNewClustersToView(inputs, newClusterList, …) — single view-side
//     undo entry; the recluster-variant primitive does all source-removal
//     and new-cluster registration in one bookkeeping pass.
//   - emit newClustersAdded(inputs)                   — recluster-shaped
//     signal for matrix views (single, takes only the dissolved-sources
//     list).
//
// Three-stack symmetry: KlustersDoc undo, Data undo (already pushed by
// splitClusterTwoWays), and KlustersView undo all get exactly ONE entry.
// One Ctrl+Z fully reverts; one Ctrl+Y fully replays.  Matches the
// watershed precedent verbatim.
// ---------------------------------------------------------------------------
void KlustersDoc::commitTwoClusterCreation(int leftId,
                                            int rightId,
                                            QList<int>& fromClusters,
                                            QList<int>& emptiedClusters,
                                            KlustersView* activeView)
{
    QList<int> newClusterList;
    newClusterList.append(leftId);
    newClusterList.append(rightId);

    // ── Doc-level undo: prepareReclusteringUndo treats the operation
    // ── as N→M (sources dissolving, new clusters appearing).  Single
    // ── doc-side entry.
    prepareReclusteringUndo(newClusterList, emptiedClusters);

    // ── Colour palette update: build clustersToShow exactly like
    // ── watershed's main branch: start from "currently shown except
    // ── the dissolved sources", then append the new IDs.
    QList<int> clustersToShow;
    if (activeView) {
        const QList<int> currentlyShown = activeView->clusters();
        for (int c : currentlyShown)
            if (!emptiedClusters.contains(c)) clustersToShow.append(c);
    }
    QColor color;
    for (int newId : newClusterList) {
        color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10,
                     200, 255);
        clusterColorList->append(newId, color);
        clustersToShow.append(newId);
    }
    for (int oldId : emptiedClusters)
        clusterColorList->remove(oldId);

    // ── View notification: one call per view, recluster-variant
    // ── primitive — single view-side undo entry per view.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == activeView);
        v->addNewClustersToView(emptiedClusters, newClusterList, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }
    // Recluster-shaped matrix-view notification: takes only the
    // sources-dissolved list, not per-cluster from-list.
    emit newClustersAdded(emptiedClusters);

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    if (activeView) activeView->showAllWidgets();

    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);

    // Caches: source clusters' caches are stale (they're gone), but the
    // dissolved-cluster entries vanish with the cluster itself.  No-op
    // loop kept for symmetry with commitClusterCreation in case a
    // future caller passes a non-emptied modifier in fromClusters.
    for (int cid : fromClusters) {
        if (emptiedClusters.contains(cid)) continue;
        invalidateWaveformCache(cid);
        invalidateCorrelogramCache(cid);
    }
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        for (int cid : fromClusters) {
            if (emptiedClusters.contains(cid)) continue;
            v->invalidateClusterDisplay(cid);
        }
    }
}


void KlustersDoc::createNewCluster(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    //Get the active view.
    KlustersView* activeView = app()->activeView();

    // Snapshot source clusters before the data mutation
    logBefore(CurationLogger::ActionType::SPLIT,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));

    float newClusterId = clusteringData->createNewCluster(region,clustersOfOrigin,dimensionX,dimensionY,fromClusters,emptyClusters);

    //Check if a new cluster has been created
    if(newClusterId == 0){
        activeView->selectionIsEmpty();
        activeView->showAllWidgets();
    }
    else{
        const int newClusterIdint = static_cast<int>(newClusterId);

        // All UI plumbing — colour registration, undo, view notifications,
        // palette refresh — is in the shared helper.
        commitClusterCreation(newClusterIdint, fromClusters, emptyClusters,
                              activeView);

        // Log after: surviving source clusters + the new cluster
        QList<int> resultIds;
        for (int id : fromClusters)
            if (!emptyClusters.contains(id))
                resultIds.append(id);
        resultIds.append(newClusterIdint);

        // Manual-split detail: label this polygon split like the algorithmic
        // ones (KNN/watershed) and, crucially, preserve the projection
        // (dimensionX, dimensionY) the curator drew it in — the discriminating
        // view that separated the sub-units, otherwise lost.
        if (curationLogger && curationLogger->isOpen()) {
            QStringList srcList;
            for (int id : fromClusters) srcList << QString::number(id);
            QMap<QString, QVariant> details;
            details.insert(QStringLiteral("algorithm"),     QStringLiteral("manual_polygon"));
            details.insert(QStringLiteral("status"),        QStringLiteral("accepted"));
            details.insert(QStringLiteral("source_cluster"),
                           fromClusters.size() == 1 ? fromClusters.first() : -1);
            details.insert(QStringLiteral("source_clusters"), srcList.join(QLatin1Char(',')));
            details.insert(QStringLiteral("n_source_clusters"), static_cast<int>(fromClusters.size()));
            details.insert(QStringLiteral("new_cluster"),    newClusterIdint);
            details.insert(QStringLiteral("n_new_clusters"),  1);
            details.insert(QStringLiteral("dimension_x"),     dimensionX);
            details.insert(QStringLiteral("dimension_y"),     dimensionY);
            details.insert(QStringLiteral("n_emptied_sources"), static_cast<int>(emptyClusters.size()));
            curationLogger->recordActionDetails(details);
        }
        logAfter(resultIds);
    }
}

void KlustersDoc::createNewClusters(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    QList<int> clustersToShow(clustersOfOrigin);
    //Get the active view.
    KlustersView* activeView = app()->activeView();

    logBefore(CurationLogger::ActionType::SPLIT_N,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));

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
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,200,255);
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

        for (KlustersView* view : *viewList) {
            const bool isActive = (view == activeView);
                view->addNewClustersToView(fromToNewClusterIds,emptyClusters, isActive);
                view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
        }

        //Notify the errorMatrixView of the modification
        emit newClustersAdded(fromToNewClusterIds,emptyClusters);


        //Reset the color status in clusterColors if need it
        if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

        activeView->showAllWidgets();

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);

        // Log after: surviving sources + all newly created clusters
        {
            QList<int> resultIds;
            for (int id : fromClusters)
                if (!emptyClusters.contains(id))
                    resultIds.append(id);
            for (int id : newClusters)
                resultIds.append(id);

            // Manual SPLIT_N detail: per-source→new mapping and the projection
            // the curator drew it in (see createNewCluster for rationale).
            if (curationLogger && curationLogger->isOpen()) {
                QStringList srcList, newList, pairList;
                for (int id : fromClusters) srcList << QString::number(id);
                for (int id : newClusters)  newList << QString::number(id);
                for (auto it = fromToNewClusterIds.constBegin();
                          it != fromToNewClusterIds.constEnd(); ++it)
                    pairList << (QString::number(it.key()) + QLatin1Char(':')
                                 + QString::number(it.value()));
                QMap<QString, QVariant> details;
                details.insert(QStringLiteral("algorithm"),       QStringLiteral("manual_polygon_n"));
                details.insert(QStringLiteral("status"),          QStringLiteral("accepted"));
                details.insert(QStringLiteral("source_clusters"), srcList.join(QLatin1Char(',')));
                details.insert(QStringLiteral("n_source_clusters"), static_cast<int>(fromClusters.size()));
                details.insert(QStringLiteral("new_clusters"),    newList.join(QLatin1Char(',')));
                details.insert(QStringLiteral("n_new_clusters"),  static_cast<int>(newClusters.size()));
                details.insert(QStringLiteral("from_to"),         pairList.join(QLatin1Char(',')));
                details.insert(QStringLiteral("dimension_x"),     dimensionX);
                details.insert(QStringLiteral("dimension_y"),     dimensionY);
                details.insert(QStringLiteral("n_emptied_sources"), static_cast<int>(emptyClusters.size()));
                curationLogger->recordActionDetails(details);
            }
            logAfter(resultIds);
        }
    }
}

// ---------------------------------------------------------------------------
// KlustersDoc::watershedSelectedClusters
//
// Run a 2D density watershed on the *selected* clusters in the palette,
// using the active scatter view's X/Y feature dimensions.  Each watershed
// basin becomes a new cluster; the source clusters are dissolved (their
// spikes redistributed across new clusters or, if some spikes fell on
// watershed lines / below density threshold, into a small residual that
// stays in the source).
//
// Returns the number of new clusters created (0 on failure / no basins).
// ---------------------------------------------------------------------------
int KlustersDoc::watershedSelectedClusters(const QList<int>& selectedClusters,
                                           const Watershed2D::Config& cfg)
{
    KlustersView* activeView = app()->activeView();
    if (!activeView) return 0;

    const int dimX = activeView->abscissaDimension();
    const int dimY = activeView->ordinateDimension();
    QList<int>  inputs = selectedClusters;
    if (inputs.isEmpty()) return 0;

    // Drop 0 / 1 from input — we never reassign artefact / noise spikes.
    inputs.removeAll(0);
    inputs.removeAll(1);
    if (inputs.isEmpty()) return 0;

    // ── Collect the (x, y) coordinates and matching feature-row indices
    // ── for every spike in the input clusters.
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<dataType> rowIdxs;
    {
        size_t totalEst = 0;
        for (int cid : inputs) totalEst += clusteringData->nbOfSpikes(cid);
        xs.reserve(totalEst);
        ys.reserve(totalEst);
        rowIdxs.reserve(totalEst);
    }
    for (int cid : inputs) {
        SortableTable subset;
        if (!clusteringData->spikePositions(cid, subset)) continue;
        const dataType n = subset.nbOfColumns();
        for (dataType i = 1; i <= n; ++i) {
            const dataType row = subset(1, i);
            xs.push_back(static_cast<double>(clusteringData->featureValue(row, dimX)));
            ys.push_back(static_cast<double>(clusteringData->featureValue(row, dimY)));
            rowIdxs.push_back(row);
        }
    }
    if (xs.size() < 50) return 0;     // not enough points to cluster

    // ── Run watershed.  Caller-supplied config is taken as-is, but if
    // ── the caller left minPeakHeight / minBasinSize at sentinel 0
    // ── values we auto-tune them from the data size.
    Watershed2D::Config c = cfg;
    if (c.minPeakHeight <= 0)
        c.minPeakHeight = std::max(3, static_cast<int>(xs.size() / 2000));
    if (c.minBasinSize <= 0)
        c.minBasinSize  = std::max(20, static_cast<int>(xs.size() / 500));

    Watershed2D::Result res = Watershed2D::run(xs, ys, c);
    if (!res.ok || res.numBasins == 0) return 0;
    if (res.numBasins == 1) {
        // Watershed returns just one basin: pointless to "split" — bail.
        return 0;
    }

    // ── Build feature-row -> basin map.  Unlabeled spikes (label 0)
    // ── must still get a label so integrateBasinLabeling's "every
    // ── source spike has a basin" contract holds — assign them all
    // ── to basin (numBasins+1), which becomes the residual cluster
    // ── after the renumber.  This way the residual is its own
    // ── tail-positioned cluster rather than being left in the source.
    QHash<dataType, int> rowToBasin;
    rowToBasin.reserve(static_cast<int>(rowIdxs.size()));
    const int residualBasin = res.numBasins + 1;
    bool sawResidual = false;
    int residualCount = 0;
    // Per-basin spike counts (basin label -> count); residualBasin is
    // included if any unlabeled spikes were rerouted.
    QMap<int,int> basinCounts;
    for (size_t i = 0; i < rowIdxs.size(); ++i) {
        int lab = res.pointLabels[i];
        if (lab <= 0) {
            lab = residualBasin;
            sawResidual = true;
            ++residualCount;
        }
        rowToBasin.insert(rowIdxs[i], lab);
        basinCounts[lab]++;
    }

    // Per-source spike counts (cid -> count) for the "before" snapshot.
    QMap<int,int> sourceCounts;
    for (int cid : inputs)
        sourceCounts[cid] = static_cast<int>(clusteringData->nbOfSpikes(cid));

    // ── Curation log (before).
    logBefore(CurationLogger::ActionType::WATERSHED, inputs);

    // ── Hand off to the recluster-style integrate pipeline.  This
    // ── dissolves source clusters and emits new ones at IDs strictly
    // ── greater than the previous max — exactly the renumber-after-
    // ── last-cluster behaviour the user expects.
    QList<int> newClusterList;
    if (!clusteringData->integrateBasinLabeling(inputs, rowToBasin,
                                                 newClusterList)) {
        return 0;
    }
    if (newClusterList.isEmpty()) return 0;

    // ── Doc-level undo: same shape as recluster.
    prepareReclusteringUndo(newClusterList, inputs);

    // ── Update colour palette: add new, remove dissolved.  Mirrors
    // ── reclusteringUpdate's main branch.
    KlustersView* mainActiveView = app()->activeView();
    QList<int> clustersToShow;
    {
        const QList<int> currentlyShown = mainActiveView->clusters();
        for (int c : currentlyShown)
            if (!inputs.contains(c)) clustersToShow.append(c);
    }
    QColor color;
    for (int newId : newClusterList) {
        color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10,
                     200, 255);
        clusterColorList->append(newId, color);
        clustersToShow.append(newId);
    }
    for (int oldId : inputs)
        clusterColorList->remove(oldId);

    // ── View notification.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == mainActiveView);
        v->addNewClustersToView(inputs, newClusterList, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }
    emit newClustersAdded(inputs);

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    mainActiveView->showAllWidgets();

    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);

    // ── Curation-log details ─────────────────────────────────────────────
    // One ACTION_DETAIL record covering: source clusters and their sizes,
    // resolved watershed config (after auto-tune), kernel diagnostics, and
    // per-basin spike counts.  Keys mirror the dipsplit pattern so a
    // downstream reader can treat the WATERSHED records uniformly.
    if (curationLogger && curationLogger->isOpen()) {
        QMap<QString, QVariant> details;
        details.insert(QStringLiteral("algorithm"),
                       QStringLiteral("watershed_2d"));

        // Source side: cluster IDs (in input order) + per-source counts +
        // total spike count fed into the watershed.
        {
            QStringList src;
            for (int cid : inputs) src.append(QString::number(cid));
            details.insert(QStringLiteral("source_clusters"),
                           src.join(QStringLiteral(",")));
            QStringList srcCounts;
            for (int cid : inputs)
                srcCounts.append(QString("%1:%2").arg(cid)
                                 .arg(sourceCounts.value(cid, 0)));
            details.insert(QStringLiteral("source_counts"),
                           srcCounts.join(QStringLiteral(",")));
            details.insert(QStringLiteral("total_input_spikes"),
                           static_cast<qulonglong>(xs.size()));
        }

        // Feature-space the watershed ran in.
        details.insert(QStringLiteral("dim_x"), dimX);
        details.insert(QStringLiteral("dim_y"), dimY);

        // Config: BOTH requested values and resolved (post-auto-tune) values.
        details.insert(QStringLiteral("grid_size"),         c.gridSize);
        details.insert(QStringLiteral("smooth_sigma_req"),  cfg.smoothSigma);
        details.insert(QStringLiteral("smooth_sigma_eff"),  res.effSigma);
        details.insert(QStringLiteral("min_peak_height_req"),
                       cfg.minPeakHeight);
        details.insert(QStringLiteral("min_peak_height_eff"),
                       res.effPeakHeight);
        details.insert(QStringLiteral("min_basin_size_req"),
                       cfg.minBasinSize);
        details.insert(QStringLiteral("min_basin_size_eff"),
                       res.effMinBasinSize);
        details.insert(QStringLiteral("use_local_maxima"),
                       c.useLocalMaxima);

        // Kernel diagnostics.
        details.insert(QStringLiteral("num_peaks"),   res.numPeaks);
        details.insert(QStringLiteral("num_basins"),  res.numBasins);

        // Result side: new cluster IDs and their sizes.  Residual basin
        // (catch-all for spikes that fell outside any peak) is logged
        // separately so downstream tooling can distinguish "watershed
        // basin proper" from "residual leftover".
        {
            QStringList newIds;
            for (int nid : newClusterList) newIds.append(QString::number(nid));
            details.insert(QStringLiteral("new_clusters"),
                           newIds.join(QStringLiteral(",")));

            // Per-basin counts in the order the new IDs were assigned.
            // integrateBasinLabeling sorts new IDs by basin label
            // ascending, so basin 1 → newClusterList[0], basin 2 →
            // newClusterList[1], ..., residualBasin → newClusterList[k].
            QStringList basinPairs;
            const auto basinKeys = basinCounts.keys();   // ascending
            for (int i = 0; i < basinKeys.size() && i < newClusterList.size();
                 ++i) {
                const int basinLabel = basinKeys[i];
                const int newId      = newClusterList[i];
                const int count      = basinCounts.value(basinLabel, 0);
                basinPairs.append(QString("%1:%2").arg(newId).arg(count));
            }
            details.insert(QStringLiteral("new_cluster_counts"),
                           basinPairs.join(QStringLiteral(",")));
        }

        details.insert(QStringLiteral("residual_present"),  sawResidual);
        details.insert(QStringLiteral("residual_count"),    residualCount);

        curationLogger->recordActionDetails(details);
    }

    logAfter(newClusterList);

    return newClusterList.size();
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
    // Keep the curation logger's in-memory ring buffer the same size as
    // the data-level undo capacity so every still-undoable action has a
    // tentative log entry.  Shrinking the buffer flushes the oldest
    // entries to disk with their current status.
    if (curationLogger && curationLogger->isOpen()) {
        curationLogger->setMaxBufferEntries(newNbUndo);
    }

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
    NS3_DIAG()<<"currentNbUndo in KlustersDoc::prepareUndo: "<<currentNbUndo;
    clusterIdsOldNewMap.insert(currentNbUndo,clusterIdsOldNew);
    clusterIdsNewOldMap.insert(currentNbUndo,clusterIdsNewOld);
}


void KlustersDoc::prepareReclusteringUndo(QList<int>& newClusters,QList<int>& deletedClusters){
    //Create a new list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    for (int v : newClusters)
        addedClustersTemp->append(v);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (int v : deletedClusters)
        deletedClustersTemp->append(v);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}

void KlustersDoc::undo(){

    NS3_DIAG()<<"in KlustersDoc::undo 1";

    //Update the boolean modified here as every undo action implies a call to the function.
    //The user can save and make an undo just behind, in that case the document is modified.
    modified = true;

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    if(!activeView)
        return;

    //If clusterColorListUndoList is not empty, make the current clusterColorList become the first element
    //of the clusterColorListRedoList and the first element of the clusterColorListUndoList become the current clusterColorList
    //do the same for the addedClusters and modifiedClusters Lists.
    if(clusterColorListUndoList.count()>0){
        // Quiesce every view's worker threads BEFORE the data layer swaps
        // spikesByCluster/clusterInfoMap.  A WaveformThread (or CorrelationThread)
        // in flight here would otherwise read across the swap, or finish and post
        // a stale per-cluster result that the view applies afterwards — leaving
        // the async waveform/correlation views showing pre-undo data while the
        // synchronous feature scatter and cluster list already show the new
        // state (the reported desync).  Stopping clears each view's
        // threadsToBeKill, so any already-posted stale result is dropped by the
        // event guards; the post-swap view->undo()/refresh below recomputes from
        // the new data, so all views end up consistent.
        for (int i = 0; i < viewList->count(); ++i)
            viewList->at(i)->stopAllViewThreads();

        // Must be called after the guard: if the undo list is empty there is nothing
        // to revert at the data layer either, and calling it unconditionally can leave
        // addedClusters/modifiedClusters in an inconsistent state (null after takeAt on
        // an empty list) which later causes a crash through the waveform cleanup path.
        clusteringData->undo(*addedClusters,*modifiedClusters);

        clusterColorListRedoList.prepend(clusterColorList);
        ItemColors* clusterColorListTemp = clusterColorListUndoList.takeAt(0);
        clusterColorList =  clusterColorListTemp;

        int nbUndo = clusterColorListUndoList.count();

        NS3_DIAG() << "nbUndo in KlustersDoc::undo: "<<nbUndo;

        //If this undo does concern renumbering
        if(clusterIdsNewOldMap.contains(nbUndo + 1)){
            NS3_DIAG() << "renumber in KlustersDoc::undo, nbUndo + 1 : "<<nbUndo + 1;
            //Add the current undo indice to the renumberingRedoList
            renumberingRedoList.append(nbUndo + 1);

            // Reverse any S-pin renumbers made by the original action
            // so a pin the user set on the pre-renumber cluster id is
            // restored when undo brings that id back.
            clusterPalette.renumberPinnedIds(clusterIdsNewOldMap[nbUndo + 1]);

            //Notify all the views of the undo

            for (KlustersView* view : *viewList) {
                const bool isActive = (view == activeView);
                    view->undoRenumbering(clusterIdsNewOldMap[nbUndo + 1], isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
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
                NS3_DIAG() << "addedClusters->size() > 0 && modifiedClusters->size() > 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undo(*addedClusters,*modifiedClusters, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoAdditionModification(*addedClusters,*modifiedClusters);
            }
            else if(!addedClusters->isEmpty() && modifiedClusters->isEmpty()){
                NS3_DIAG() << "addedClusters->size() > 0 && modifiedClusters->size() == 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undoAddedClusters(*addedClusters, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoAddition(*addedClusters);
            }
            else if(addedClusters->isEmpty() && !modifiedClusters->isEmpty()){
                NS3_DIAG() << "addedClusters->size() == 0 && modifiedClusters->size() > 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undoModifiedClusters(*modifiedClusters, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoModification(*modifiedClusters);
            }
            //////!!!!This last condition should not be reach anymore, to test and remove.!!!!!////
            else if(addedClusters->size() == 0 && modifiedClusters->size() == 0){
                NS3_DIAG() << "addedClusters->size() == 0 && modifiedClusters->size() == 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->undo(isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }
            }
        }
        addedClustersRedoList.prepend(addedClusters);
        QList<int>* addedClustersTemp = addedClustersUndoList.isEmpty()
                                        ? new QList<int>()
                                        : addedClustersUndoList.takeAt(0);
        addedClusters =  addedClustersTemp;

        modifiedClustersRedoList.prepend(modifiedClusters);
        QList<int>* modifiedClustersTemp = modifiedClustersUndoList.isEmpty()
                                           ? new QList<int>()
                                           : modifiedClustersUndoList.takeAt(0);
        modifiedClusters =  modifiedClustersTemp;

        deletedClustersRedoList.prepend(deletedClusters);
        QList<int>* deletedClustersTemp = deletedClustersUndoList.isEmpty()
                                          ? new QList<int>()
                                          : deletedClustersUndoList.takeAt(0);
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

    NS3_DIAG()<<"in KlustersDoc::undo 2";

    // Curation log: flip the topmost good entry's status to "bad" so the
    // record reflects that the user reverted this action.  No disk write
    // happens here — the entry stays in the in-memory ring until it
    // either gets pushed out by overflow or is finalised at close().
    if (curationLogger && curationLogger->isOpen()) {
        curationLogger->notifyUndo();
    }
}


void KlustersDoc::redo(){
    //Get the active view.
    KlustersView* activeView = app()->activeView();

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

        // Stop in-flight view worker threads before the data swap (see the
        // matching comment in undo()): prevents a stale waveform/correlation
        // result from landing after the swap and desyncing the async views from
        // the synchronous feature scatter / cluster list.
        for (int i = 0; i < viewList->count(); ++i)
            viewList->at(i)->stopAllViewThreads();

        clusteringData->redo(*addedClusters,*modifiedClusters,*deletedClusters);

        //If this redo does concern renumbering
        int nbUndo = clusterColorListUndoList.count();

        NS3_DIAG() << "in KlustersDoc::redo, nbUndo  : "<<nbUndo;

        if(clusterIdsOldNewMap.contains(nbUndo)){
            NS3_DIAG() << "renumber in KlustersDoc::redo, nbUndo  : "<<nbUndo;
            //remove the current undo indice from the renumberingRedoList
            renumberingRedoList.removeAll(nbUndo);

            // Re-apply the original rename to S-pinned ids so a pin
            // restored by undo gets re-translated when redo replays
            // the renumber.
            clusterPalette.renumberPinnedIds(clusterIdsOldNewMap[nbUndo]);

            //Notify all the views of the undo
            for (KlustersView* view : *viewList) {
                const bool isActive = (view == activeView);
                    view->redoRenumbering(clusterIdsOldNewMap[nbUndo], isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
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
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() > 0 && modifiedClusters->size()>0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redo(*addedClusters, *modifiedClusters, isModifiedByDeletion, isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoAdditionModification(*addedClusters,*modifiedClusters,isModifiedByDeletion,*deletedClusters);
            }
            else if(addedClusters->size() > 0 && modifiedClusters->size() == 0){
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() > 0 && modifiedClusters->size()==0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redoAddedClusters(*addedClusters, isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoAddition(*addedClusters,*deletedClusters);
            }
            else if(addedClusters->size() == 0 && modifiedClusters->size() > 0){
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() == 0 && modifiedClusters->size()>0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redoModifiedClusters(*modifiedClusters, isModifiedByDeletion, isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoModification(*modifiedClusters,isModifiedByDeletion,*deletedClusters);
            }
            else if(addedClusters->size() == 0 && modifiedClusters->size() == 0){
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() == 0 && modifiedClusters->size() ==0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redo(isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoDeletion(*deletedClusters);
            }
        }

        NS3_DIAG() << "in KlustersDoc::redo, 2  : ";

        QList<int> clustersToShow = activeView->clusters();

        //Call redraw on the active view
        activeView->showAllWidgets();
        //Update the clusterPalette
        clusterPalette.updateClusterList();

        NS3_DIAG() << "in KlustersDoc::redo, 3 b : ";

        clusterPalette.selectItems(clustersToShow);

        NS3_DIAG() << "in KlustersDoc::redo, 4  : ";

        //Signal to klusters the new number of undo and redo
        emit updateUndoNb(clusterColorListUndoList.count());
        emit updateRedoNb(clusterColorListRedoList.count());

        NS3_DIAG() << "in KlustersDoc::redo, end  : ";
    }

    // Curation log: flip the topmost bad entry back to "good" — the user
    // restored this action so its on-disk record (when eventually
    // flushed) should not be marked as a reverted decision.
    if (curationLogger && curationLogger->isOpen()) {
        curationLogger->notifyRedo();
    }
}

void KlustersDoc::renumberClusters(){
    //Get the active view.
    KlustersView* activeView = app()->activeView();

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

    // Translate S-pinned cluster ids through the rename so any
    // pinning the user established before R survives the renumber.
    clusterPalette.renumberPinnedIds(clusterIdsOldNew);

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

// ---------------------------------------------------------------------------
// KlustersDoc::applyClusterRename
//
// Single primitive for "rename a set of clusters" actions.  Used by:
//   - renumberClustersToEnd (T key)        — partial rename to tail
//   - renumberClusters (full sequential)   — TODO Phase 2.5 plumbing below
//   - watershed residual cleanup           — folded into integrateBasinLabeling
//
// The mutation runs across THREE state tables and the GUI signal bus,
// in the order required for consistency:
//
//   1. Data layer (renumberPartial) — the spike table + clusterInfoMap
//      get the new IDs; old IDs vanish.  This pushes its own data-side
//      undo entry.
//   2. Colour list — each renamed cluster's ItemColor entry is
//      relabelled in place via changeItemId so the colour persists.
//   3. Each KlustersView — shownClusters list rewrites its old IDs to
//      new IDs via changeClusterIds (called inside renumberClusters).
//   4. Errormatrix / template-matrix — receive the rename via the
//      KlustersDoc::renumber() Qt signal.
//
// Caller is responsible for the doc-level UNDO snapshot (`prepareUndo` /
// `prepareReclusteringUndo` / etc.) and any logBefore/logAfter pairs;
// this helper does only the apply.
//
// `partialOldToNew` lists ONLY the renamed clusters.  `fullOldToNew` is
// a covering map (every existing cluster ID maps to its post-rename ID,
// identity for unchanged) — required because KlustersView::renumberClusters
// calls changeClusterIds, which historically expected a covering map
// (since fixed to be partial-safe, but covering callers are still safer).
// Pass nullptr for `fullOldToNew` to have it built automatically.
// ---------------------------------------------------------------------------
void KlustersDoc::applyClusterRename(const QMap<int,int>& partialOldToNew,
                                      const QMap<int,int>* fullOldToNewOpt)
{
    if (partialOldToNew.isEmpty()) return;

    KlustersView* activeView = app()->activeView();
    if (!activeView) return;

    // Build a covering map if the caller didn't supply one.
    QMap<int,int> fullOwned;
    const QMap<int,int>* full = fullOldToNewOpt;
    if (!full) {
        const QList<dataType> existing = clusteringData->clusterIds();
        for (dataType eid : existing) {
            const int iid = static_cast<int>(eid);
            fullOwned.insert(iid, partialOldToNew.value(iid, iid));
        }
        full = &fullOwned;
    }

    // 1. Data layer — pushes its own data-side undo entry.
    clusteringData->renumberPartial(partialOldToNew);

    // 2. Colour list — rename in place so colours persist.
    for (auto it = partialOldToNew.constBegin();
         it != partialOldToNew.constEnd(); ++it) {
        const int oldId = it.key();
        const int newId = it.value();
        const int idx = clusterColorList->itemIndex(oldId);
        if (idx >= 0) clusterColorList->changeItemId(idx, newId);
    }
    // changeItemId mutates IDs in-place without re-ordering the
    // underlying itemList, so a partial rename like the T-key
    // "renumber to end" leaves the renamed entry sitting at its old
    // storage position.  The palette renders in storage order, so the
    // icon would still appear where the OLD ID lived.  Re-sort by
    // itemId so the palette layout matches the new ID ordering.
    //
    // Safe to do here because (a) the renumber-undo snapshot was
    // taken before this — prepareClusterColorUndo deep-copies via
    // ItemColors's copy ctor, and the snapshot is immutable from this
    // point — and (b) Data::renumberPartial now re-packs the spike
    // table in ascending-id order, so highestClusterId() and friends
    // report correct values for all subsequent renumbers.
    clusterColorList->sortByItemId();

    // 2b. Translate S-pinned cluster ids through the rename so a
    // pinned cluster keeps its pin under its new id.  Without this,
    // pins would be silently dropped on the next palette refresh
    // (updateClusterList prunes pinned ids whose cluster no longer
    // exists, and after a rename the OLD id is gone).
    clusterPalette.renumberPinnedIds(partialOldToNew);

    // 3. Each view — rewrites shownClusters; emits its own clustersRenumbered.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == activeView);
        // Cast the const-ref to a non-const ref for the legacy view API.
        // changeClusterIds inside renumberClusters does not mutate it.
        v->renumberClusters(const_cast<QMap<int,int>&>(*full), isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    // 4. Errormatrix / template-matrix.
    emit renumber(const_cast<QMap<int,int>&>(*full));

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    activeView->showAllWidgets();

    // Refresh palette.
    QList<int> activeClusters = activeView->clusters();
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(activeClusters);
}

// ---------------------------------------------------------------------------
// KlustersDoc::renumberClustersToEnd
//
// Targeted rename: each cluster in `clustersToRenumber` gets a new ID
// greater than the current global maximum, so they end up at the end of
// the (sorted-by-ID) palette.  Triggered by the palette T shortcut.
//
// Selection ordering: clusters are renamed in ascending order so the
// lowest-numbered selected cluster gets the lowest new ID
// (currentMax+1), preserving relative order at the tail.
//
// Edge cases handled by the caller (slotMoveSelectedClustersToEnd):
//   - global-max cluster filtered out (already at end)
//   - 0 / 1 (artefact / noise) filtered out
//
// Single undo entry covers the whole batch — Ctrl+Z reverts all renames
// in one step, matching the user's mental model of T as a single action.
// ---------------------------------------------------------------------------
void KlustersDoc::renumberClustersToEnd(QList<int> clustersToRenumber)
{
    if (clustersToRenumber.isEmpty()) return;
    if (!app()->activeView()) return;

    const QList<dataType> existing = clusteringData->clusterIds();
    if (existing.isEmpty()) return;
    int nextNewId = static_cast<int>(clusteringData->nextFreeClusterId());

    // Build the partial old→new map AND a full coverage map.
    QMap<int,int> partialOldToNew;
    QMap<int,int> fullOldToNew;
    QMap<int,int> fullNewToOld;
    for (dataType eid : existing) {
        fullOldToNew.insert(static_cast<int>(eid), static_cast<int>(eid));
        fullNewToOld.insert(static_cast<int>(eid), static_cast<int>(eid));
    }
    std::sort(clustersToRenumber.begin(), clustersToRenumber.end());
    for (int oldId : clustersToRenumber) {
        if (oldId == 0 || oldId == 1) continue;            // never rename specials
        if (!fullOldToNew.contains(oldId))     continue;   // skip missing
        const int newId = nextNewId++;
        partialOldToNew.insert(oldId, newId);
        fullOldToNew.insert(oldId, newId);
        fullNewToOld.insert(newId, oldId);
        fullNewToOld.remove(oldId);
    }
    if (partialOldToNew.isEmpty()) return;

    // ── Curation log: this is a renumber, not a group ──
    logBefore(CurationLogger::ActionType::RENUMBER_PARTIAL, clustersToRenumber);

    // ── Doc-level undo snapshot (uses the renumber-specific stack so
    //    KlustersDoc::undo can detect this as a renumber action).
    prepareUndo(fullOldToNew, fullNewToOld);

    // ── Apply.
    applyClusterRename(partialOldToNew, &fullOldToNew);

    // Log post-rename.
    QList<int> newIds;
    for (int oldId : clustersToRenumber)
        newIds.append(partialOldToNew.value(oldId, oldId));
    logAfter(newIds);
}

// ---------------------------------------------------------------------------
// KlustersDoc::reorderClustersByPermutation
//
// Drives the rename pipeline (logBefore + prepareUndo + applyClusterRename +
// logAfter) from an externally-supplied cluster order.  Used by the Shift+S
// "Reorder Clusters by Similarity" action — the slot computes the order via
// agglomerative clustering on the active similarity matrix and hands it
// here, so all the undo/log/palette/view bookkeeping stays inside the doc
// layer where it belongs (mirrors the renumberClustersToEnd pattern).
//
// New IDs start at 2; clusters 0 (artefact) and 1 (noise) are untouched and
// stay at the front of the palette.  Any newOrder entry referencing 0, 1,
// or a missing cluster makes the whole call a no-op (returns -1) so we never
// half-apply a rename.
// ---------------------------------------------------------------------------
int KlustersDoc::reorderClustersByPermutation(const QList<int>& newOrder)
{
    if (newOrder.isEmpty()) return 0;
    if (!app()->activeView()) return -1;

    const QList<dataType> existing = clusteringData->clusterIds();
    if (existing.isEmpty()) return -1;

    QSet<int> existingSet;
    for (dataType eid : existing) existingSet.insert(static_cast<int>(eid));

    // Validate: every newOrder entry must be a real, non-special cluster.
    QSet<int> seen;
    for (int cid : newOrder) {
        if (cid <= 1) return -1;
        if (!existingSet.contains(cid)) return -1;
        if (seen.contains(cid)) return -1;       // duplicates not allowed
        seen.insert(cid);
    }

    // Build maps.  We rename the listed clusters to consecutive IDs
    // starting at 2; any existing cluster NOT in newOrder keeps its
    // current ID — that's an artefact of the slot only handing us
    // clusters present in the similarity matrix, and is fine because
    // applyClusterRename uses Data::renumberPartial which preserves
    // unlisted IDs.
    QMap<int,int> partialOldToNew;
    QMap<int,int> fullOldToNew;
    QMap<int,int> fullNewToOld;
    for (dataType eid : existing) {
        const int iid = static_cast<int>(eid);
        fullOldToNew.insert(iid, iid);
        fullNewToOld.insert(iid, iid);
    }
    int nextNewId = 2;
    QList<int> renamedClusters;
    for (int oldId : newOrder) {
        const int newId = nextNewId++;
        if (oldId == newId) continue;          // already in target slot
        partialOldToNew.insert(oldId, newId);
        fullOldToNew.insert(oldId, newId);
        fullNewToOld.remove(oldId);
        fullNewToOld.insert(newId, oldId);
        renamedClusters.append(oldId);
    }

    if (partialOldToNew.isEmpty()) return 0;   // already in order

    // Reject if the rename would map two old IDs to the same new ID.
    // This shouldn't happen given the per-newOrder uniqueness check
    // above PLUS the consecutive-from-2 assignment, but applyClusterRename
    // doesn't validate this, so we defend before mutating state.
    QSet<int> targets;
    for (auto it = fullOldToNew.constBegin();
         it != fullOldToNew.constEnd(); ++it)
    {
        if (targets.contains(it.value())) return -1;
        targets.insert(it.value());
    }

    // Curation log + undo snapshot use the same pattern as
    // renumberClustersToEnd, so undo/redo behaviour and log replay
    // both treat this as a partial-renumber action.
    logBefore(CurationLogger::ActionType::RENUMBER_PARTIAL, renamedClusters);
    prepareUndo(fullOldToNew, fullNewToOld);
    applyClusterRename(partialOldToNew, &fullOldToNew);

    QList<int> newIds;
    newIds.reserve(renamedClusters.size());
    for (int oldId : renamedClusters)
        newIds.append(partialOldToNew.value(oldId, oldId));
    logAfter(newIds);

    return partialOldToNew.size();
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

// patch78 — wrapper for the mean-subtracted sub-dimensional path.
// Returns one of the standard enum values (OK / OPEN_ERROR /
// CREATION_ERROR) to avoid colliding the success path's K+1 dim count
// with the enum codes (CREATION_ERROR=8, SAVE_ERROR=5, UPLOAD_ERROR=6,
// INCORRECT_CONTENT=7, etc.).  The actual number of dimensions written
// is returned via the *nDimWritten out-parameter so the caller can
// build the right %features bit-string.
int KlustersDoc::createMeanSubtractedSubdimFeatureFile(
        int clusterId, int K,
        const QString& reclusteringFetFileName,
        int* nDimWritten,
        QVector<double>* eigvalsOut)
{
    if (nDimWritten) *nDimWritten = 0;
    QFile fetFile(reclusteringFetFileName);
    if (!fetFile.open(QIODevice::WriteOnly)) return OPEN_ERROR;
    const int nDim = clusteringData->createMeanSubtractedSubdimFeatureFile(
        clusterId, K, fetFile, eigvalsOut);
    // createMeanSubtractedSubdimFeatureFile closes fetFile internally
    // (it re-opens via fopen for binary I/O).
    if (nDim <= 0) return CREATION_ERROR;
    if (nDimWritten) *nDimWritten = nDim;
    return OK;
}

// Wrapper for the raw-waveform median-residual path.  Same enum/out-parameter
// contract as createMeanSubtractedSubdimFeatureFile.  Pools the spikes of all
// @p clusterIds before taking the median waveform.
int KlustersDoc::createMedianWaveformResidualFeatureFile(
        const QList<int>& clusterIds, int K,
        const QString& reclusteringFetFileName,
        int* nDimWritten,
        QVector<double>* eigvalsOut)
{
    if (nDimWritten) *nDimWritten = 0;
    QFile fetFile(reclusteringFetFileName);
    if (!fetFile.open(QIODevice::WriteOnly)) return OPEN_ERROR;
    const int nDim = clusteringData->createMedianWaveformResidualFeatureFile(
        clusterIds, K, fetFile, eigvalsOut);
    if (nDim <= 0) return CREATION_ERROR;
    if (nDimWritten) *nDimWritten = nDim;
    return OK;
}

// patch81 — Remove the staged YAML (and .yml fallback) that
// KlustersApp::slotRecluster copied next to the temp .fet so that
// KlustaKwikYaml.cpp's tryPath("<fileBase>", ".yaml") would find it.
//
// The temp YAML lives next to reclusteringFetFileName with the same
// basename but a different extension.  Strip ".fet.<elecID>" off the
// end and try .yaml + .yml.  Failures are silent: the file may not
// exist (orig YAML was missing, or this is a second-attempt cleanup
// where a prior call already removed it), and either case is fine.
static void patch81_cleanupTempYaml(const QString& reclusteringFetFileName)
{
    const int dotFet = reclusteringFetFileName.lastIndexOf(
        QLatin1String(".fet."));
    if (dotFet < 0) return;
    const QString base = reclusteringFetFileName.left(dotFet);
    const QString yamlPath = base + QLatin1String(".yaml");
    const QString ymlPath  = base + QLatin1String(".yml");
    if (QFile::exists(yamlPath)) QFile::remove(yamlPath);
    if (QFile::exists(ymlPath))  QFile::remove(ymlPath);
}

int KlustersDoc::integrateReclusteredClusters(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList,QString reclusteringFetFileName){

    // Capture cluster state before KlustaKwik's output is integrated
    logBefore(CurationLogger::ActionType::RECLUSTER, clustersToRecluster);

    QString cluFileName(reclusteringFetFileName);
    NS3_DIAG()<<"reclusteringFetFileName "<<reclusteringFetFileName;
    cluFileName.replace(".fet.",".clu.");

    QString cluFileUrl(cluFileName);
    QString tmpCluFile = cluFileUrl;
    if(!QFile::exists(cluFileUrl)) {
        QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        return DOWNLOAD_ERROR;
    }

    NS3_DIAG()<<" tmpCluFile"<<tmpCluFile;
    QFile cluFile(tmpCluFile);

    if(!cluFile.open(QIODevice::ReadOnly)){
        if(!QFile::remove(reclusteringFetFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
        if(!QFile::remove(cluFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        patch81_cleanupTempYaml(reclusteringFetFileName);
        return OPEN_ERROR;
    }

    //Actually integrate the new clusters.
    if(!clusteringData->integrateReclusteredClusters(clustersToRecluster,reclusteredClusterList,cluFile)){
        cluFile.close();
        if(!QFile::remove(reclusteringFetFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
        if(!QFile::remove(cluFileName))
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        patch81_cleanupTempYaml(reclusteringFetFileName);
        return INCORRECT_CONTENT;
    }
    cluFile.close();

    //Suppress the fet and clu files.
    if(!QFile::remove(reclusteringFetFileName))
        QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
    if(!QFile::remove(cluFileName))
        QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
    patch81_cleanupTempYaml(reclusteringFetFileName);

    // Log the newly created clusters — reclusteredClusterList is populated by
    // integrateReclusteredClusters() above and contains the KlustaKwik outputs.
    logAfter(reclusteredClusterList);

    return OK;
}

void KlustersDoc::reclusteringUpdate(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList){
    //Prepare the undo
    prepareReclusteringUndo(reclusteredClusterList,clustersToRecluster);

    //Check if the active view is a ProcessWidget
    bool isProcessWidget = dynamic_cast<KlustersApp*>(parent)->doesActiveDisplayContainProcessWidget();

    if(!isProcessWidget){
        //Get the active view.
        KlustersView* activeView = app()->activeView();

        QList<int> clustersToShow;
        QList<int>::const_iterator iterator;
        QList<int> const clusters = activeView->clusters();
        for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator)
            clustersToShow.append(*iterator);

        //Add the new clusters in clusterColors and clustersToShow.
        QColor color;
        QList<int>::iterator clustersToCreate;
        for(clustersToCreate = reclusteredClusterList.begin(); clustersToCreate != reclusteredClusterList.end(); ++clustersToCreate ){
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,200,255);
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
        for (KlustersView* view : *viewList) {
            const bool isActive = (view == activeView);
                view->addNewClustersToView(clustersToRecluster,reclusteredClusterList, isActive);
                view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
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
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,200,255);
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
    color.setHsv(210,200,255);

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
#include "realign_center.h"  // realign_center::circularRecenterShift (shared)
#include "pca_refine_dispatch.h"

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

    // Snapshot the cluster before any waveform data is modified.  During an
    // Align-All batch the centroid pass this triggers is served from a
    // batch-scoped cache (see beginRealignBatchLog), so per-cluster logging
    // stays cheap and the log streams cluster-by-cluster.
    logBefore(CurationLogger::ActionType::REALIGN, QList<int>{ clusterId });

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

    // Per-phase wall-clock timing, opt-in via NS3_REALIGN_TIMING=1.  Emits one
    // line per cluster (setup / compute / writeback / total) so a batch can be
    // checked for which phase grows with cluster index.  No-op (one cheap
    // elapsed() pair) when the env var is unset.
    const bool _timing = qEnvironmentVariableIsSet("NS3_REALIGN_TIMING");
    QElapsedTimer _rtmr; _rtmr.start();
    qint64 _rtSetupMs = 0, _rtComputeMs = 0;
    // Writeback sub-phase markers (elapsed ms): boundary after sort-prep, after
    // file-open+verify, and after the write loop+close.  Pinpoints which part of
    // the (flat ~130ms) writeback is the fixed per-cluster cost.
    qint64 _rtWprepMs = 0, _rtWopenMs = 0, _rtWwriteMs = 0;
    // Gap since the previous cluster's realign finished — captures the
    // inter-cluster overhead (worker teardown + slotRealignFinished +
    // next-worker spin-up) that is invisible to the in-function timers.
    const long long _nowStartMs = (long long)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    const long long _gapMs =
        (m_realignPrevEndMs >= 0) ? (_nowStartMs - m_realignPrevEndMs) : -1;

    const Data& d         = data();
    const int   nChan     = d.nbOfChannels();
    const int   nSamp     = d.nbSamplesPerWaveform();
    const int   peakSamp  = d.peakSampleIndex(); // 1-based index from .par.N / parameter file
    const int   peakSamp0 = peakSamp - 1;        // 0-based waveform index
    const int   timeDim   = d.timeDimension();  // = nDimensions from .fet header
    const int   nFeatCols = timeDim - 1;        // feature columns, last col is ts

    // -----------------------------------------------------------------------
    // Parse configurable parameters from args string.
    // Supported: --threshold F  --iterations N  --maxshift N  --topchannels N
    //            --pca-refine  --recenter-rms  --rmin F
    // Defaults:  threshold=0.70  iterations=2  maxshift=peakSamp/2  rmin=0.40
    // -----------------------------------------------------------------------
    int   maxShift  = std::max(1, peakSamp0 / 2);
    float minScore  = 0.70f;
    int   nIter     = 2;
    int   nTopChan  = 0;   // 0 = use all channels (legacy behaviour)
    bool  pcaRefine = false;   // patch82: second-pass PCA-energy-max alignment
    bool  rmsRecenter = false; // post-alignment RMS circular group-recenter
    float rMin        = 0.4f;  // min mean-resultant-length to trust the centroid
    {
        const QStringList tokens = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (qsizetype ti = 0; ti < tokens.size(); ++ti) {
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
            } else if ((tok == QStringLiteral("--topchannels") || tok == QStringLiteral("-k"))
                    && ti + 1 < tokens.size()) {
                bool ok2; int v = tokens[++ti].toInt(&ok2);
                if (ok2 && v >= 0) nTopChan = v;
            } else if (tok == QStringLiteral("--pca-refine") ||
                       tok == QStringLiteral("-p")) {
                // patch82 — no value, just a boolean flag.
                pcaRefine = true;
            } else if (tok == QStringLiteral("--recenter-rms")) {
                // RMS circular group-recenter — no value, boolean flag.
                rmsRecenter = true;
            } else if (tok == QStringLiteral("--rmin")
                    && ti + 1 < tokens.size()) {
                bool ok2; float v = tokens[++ti].toFloat(&ok2);
                if (ok2 && v >= 0.0f && v <= 1.0f) rMin = v;
            }
        }
    }

    const QString dir   = documentDirectory();
    const QString base  = documentBaseName();
    const QString grpId = currentElectrodeGroupID();

    // Read from the pending file when it exists (i.e. after at least one
    // unsaved realignment pass), otherwise fall back to the original.
    // Writing always targets the pending files (see spkW/resW/fetW below).
    // If we always read from the originals, a second realignment pass would
    // load pre-aligned waveforms, xcorr them against the updated template,
    // find the same non-zero lags all over again, and write them back —
    // pushing late spikes further forward and early spikes further backward.
    auto pendingOrOrig = [](const QString& orig, const QString& pending) {
        return QFileInfo::exists(pending) ? pending : orig;
    };
    const QString spkPath = pendingOrOrig(origSpkPath, pendingSpkPath);
    const QString resPath = pendingOrOrig(origResPath, pendingResPath);
    const QString fetPath = pendingOrOrig(origFetPath, pendingFetPath);
    const QString cluPath = resolveFeature(dir + "/" + base, "clu", grpId,
                                           featureMethod(origFetPath));
    // Pipeline detection — decouple .spk storage format from .fet feature
    // space.  These are orthogonal signals in Pipeline C (raw .spk + stderiv
    // .fetD/.pcaD) and only coincide in Pipeline A (both raw) and Pipeline D
    // (both stderiv).  Conflating them corrupts .fetD under Pipeline C by
    // selecting the raw-space .pca basis and skipping the stderiv transform
    // during feature reprojection.
    //
    //   spkIsTransformed — .spk stores stderiv waveforms (Pipeline D).
    //                      When writing back, apply applyStderivTransform
    //                      to the raw extraction so .spk stays in stderiv
    //                      space.  For raw .spk (Pipeline A, C) write the
    //                      untransformed waveform.
    //   fetIsStderiv    — .fet features are in stderiv space, built on the
    //                      stderiv .pca basis.  Select .pca.stderiv.N and apply
    //                      the stderiv transform to the raw waveform before
    //                      projecting onto eigenvectors.
    // Chain-of-custody, but the two flags are INDEPENDENT (Pipeline-C): they are
    // read off the artifacts actually resolved, not from one shared method.
    //   spkIsTransformed — true only if the resolved .spk is itself a stderiv
    //     file; with a shared raw .spk it is false even for a stderiv sort.
    //   fetIsStderiv     — true if the resolved .fet is stderiv-space (the
    //     feature space the sort was done in), which selects the stderiv .pca
    //     basis and transforms the raw waveform before projecting.
    const QString fetMethod = featureMethod(origFetPath);
    const bool spkIsTransformed = (featureMethod(origSpkPath) == QLatin1String("stderiv"));
    const bool fetIsStderiv     = (fetMethod == QLatin1String("stderiv"));
    // Kept as alias for existing legacy-named uses in this function that
    // really want the feature-space flag, not the .spk storage flag.
    const bool isStderivRealign = fetIsStderiv;
    // Basis follows the feature space (.pca at fetMethod, with shared fallback).
    const QString pcaPath = resolveFeature(
        dir + "/" + base, "pca", grpId, fetMethod);
    const QString pcaDPath_ra = pcaPath;  // retained name for logging below

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
        << "  topChan=" << (nTopChan > 0 ? QString::number(nTopChan)
                                         : QStringLiteral("all"))
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
    PcaBasis pca;   // type defined in klustersdoc.h (member-cached below)

    const QFileInfo _pcaFi(pcaPath);
    const bool      _pcaExists = _pcaFi.exists();
    const qint64    _pcaMtime  = _pcaExists
        ? _pcaFi.lastModified().toMSecsSinceEpoch() : -1;

    if (isStderivRealign && !QFileInfo::exists(pcaDPath_ra))
        log << "WARNING: stderiv PCA basis (.pca.stderiv/.pcaD) for group "
            << grpId << " not found — run ndm_pca_stderiv to generate it.\n";

    if (_pcaExists && m_realignPcaCache.valid()
        && m_realignPcaCachePath == pcaPath
        && m_realignPcaCacheMtime == _pcaMtime) {
        // Cache hit — reuse the basis loaded for an earlier cluster in the batch.
        pca = m_realignPcaCache;
        log << "PCA file: " << pcaPath << " [cached]\n";
        emitFlush();
    } else {
        log << "PCA file: " << pcaPath
            << (_pcaExists ? " [found]" : " [NOT FOUND]") << "\n";
        emitFlush();
        if (_pcaExists) {
        FILE* fp = fopen(pcaPath.toLocal8Bit().constData(), "rb");
        if (fp) {
            int32_t hdr[5] = {};
            bool ok = (fread(hdr, sizeof(int32_t), 5, fp) == 5);
            if (ok) {
                pca.nCh      = static_cast<int>(hdr[0]);
                pca.data2use = static_cast<int>(hdr[1]);
                pca.nComp    = static_cast<int>(hdr[2]);
                pca.centered = (hdr[3] != 0);
                pca.recShift = static_cast<int>(hdr[4]);
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
        if (pca.valid()) {
            // Loaded fresh from disk — cache for the rest of the batch so the
            // next cluster reuses it instead of re-reading the basis file.
            m_realignPcaCache      = pca;
            m_realignPcaCachePath  = pcaPath;
            m_realignPcaCacheMtime = _pcaMtime;
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
            if (fseeko(rf, (off_t)(p * static_cast<int64_t>(sizeof(int64_t))), SEEK_SET) != 0 ||
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
                                + (off_t)(p * static_cast<int64_t>(fileDim) + col)
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
        //
        // Top-K channel selection: when nTopChan > 0 and < nChan, select the
        // nTopChan channels with the largest absolute amplitude in the
        // template.  Energy for peak detection is computed only on those.
        // The same mask is then applied to the template and all waveform
        // buffers (non-top channels zeroed) so the xcorr ignores them too.
        // This excludes collision / residual activity on non-primary
        // channels from influencing alignment.
        std::vector<uint8_t> chanUse(static_cast<size_t>(nChan), 1);
        if (nTopChan > 0 && nTopChan < nChan) {
            std::vector<std::pair<int64_t,int>> chanAmp(static_cast<size_t>(nChan));
            for (int ch = 0; ch < nChan; ++ch) {
                int64_t pk = 0;
                for (int t = 0; t < nSamp; ++t) {
                    const int64_t v = std::abs(static_cast<int64_t>(
                        tmpl[static_cast<size_t>(ch * nSamp + t)]));
                    if (v > pk) pk = v;
                }
                chanAmp[static_cast<size_t>(ch)] = {pk, ch};
            }
            std::partial_sort(chanAmp.begin(),
                              chanAmp.begin() + nTopChan,
                              chanAmp.end(),
                              [](const auto& a, const auto& b){
                                  return a.first > b.first;
                              });
            std::fill(chanUse.begin(), chanUse.end(), 0);
            for (int k = 0; k < nTopChan; ++k)
                chanUse[static_cast<size_t>(chanAmp[static_cast<size_t>(k)].second)] = 1;
        }

        {
            int    tmplPeak = 0;
            double bestAmp  = -1.0;
            for (int t = 0; t < nSamp; ++t) {
                double amp = 0.0;
                for (int ch = 0; ch < nChan; ++ch)
                    if (chanUse[static_cast<size_t>(ch)])
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

        // Apply top-K mask to a SCRATCH copy for the xcorr call only.
        // Mutating wavBuf would destroy data needed for the next iteration's
        // template build, the final xcorr pass, and the post-realign mean.
        // Same reasoning for tmpl — keep the original, mask a copy.
        //
        // Layout is channel-major [ch * nSamp + s]; zeroing ranges of
        // contiguous samples for masked-out channels gives the xcorr
        // dispatcher zeros on both sides of the correlation for those
        // channels, so they contribute nothing at any lag.
        const int16_t* xcorrWav = wavBuf.data();
        const int16_t* xcorrTmpl = tmpl.data();
        std::vector<int16_t> wavMasked;
        std::vector<int16_t> tmplMasked;
        if (nTopChan > 0 && nTopChan < nChan) {
            tmplMasked = tmpl;   // copy
            wavMasked  = wavBuf; // copy (N × spkElems int16 — cheap vs. xcorr)
            for (int ch = 0; ch < nChan; ++ch) {
                if (chanUse[static_cast<size_t>(ch)]) continue;
                std::fill(tmplMasked.begin() + ch * nSamp,
                          tmplMasked.begin() + (ch + 1) * nSamp,
                          int16_t(0));
                for (int64_t i = 0; i < N; ++i) {
                    int16_t* row = wavMasked.data()
                                 + static_cast<ptrdiff_t>(i)
                                 * static_cast<ptrdiff_t>(spkElems);
                    std::fill(row + ch * nSamp,
                              row + (ch + 1) * nSamp,
                              int16_t(0));
                }
            }
            xcorrWav  = wavMasked.data();
            xcorrTmpl = tmplMasked.data();
        }

        std::vector<int>   sh(static_cast<size_t>(N), 0);
        std::vector<float> sc(static_cast<size_t>(N), 0.0f);
        int rc = XcorrDispatch::compute(
            xcorrWav, xcorrTmpl,
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
            // lag = bestLag when spike[(s+bestLag)%N] ≈ tmpl[s], meaning the
            // spike peak is late by bestLag.  Rolling forward by bestLag
            // brings the peak back to the template position.  The timestamp
            // must move in the opposite direction (see newTs below).
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
    // patch82 — PCA-projection-maximizing refine pass (opt-in via --pca-refine)
    //
    // After the mean-template xcorr alignment converges, refine each
    // spike's position by trying candidate shifts s in [-maxShift, +maxShift]
    // and picking the one that MAXIMIZES the spike's PCA-projection energy:
    //
    //     energy(s) = sum_ch sum_k <basis_ch_k, spike(s) - mean_ch>²
    //
    // Intuition: a well-aligned spike concentrates its variance in the
    // directions of greatest cluster variance (the kept PCA components);
    // a misaligned spike spreads variance across all directions,
    // suppressing this sum.  energy(s) peaks where the spike most
    // strongly looks like a member of the cluster as measured by the
    // canonical PCA basis.
    //
    // Reads each candidate window FRESH from .fil (no circular wrap
    // artifacts that would skew the PCA scores near the window edges).
    // Uses a local FILE* so the existing .fil open at line ~4060 stays
    // untouched.
    //
    // Update propagation: when bestS != 0,
    //   - cumShift[i] += bestS (so the existing sort + disk-write code
    //     downstream uses the refined position)
    //   - wavBuf[i] is overwritten with the freshly-extracted window
    //     at the refined position, so the score statistics block below
    //     (computing finalTmpl + scores) sees post-refine waveforms
    //     instead of stale circular-shifted ones.
    //
    // Falls back silently to no-op when:
    //   - .pca eigenvector basis didn't load (pca.valid() == false)
    //   - .fil can't be opened
    //   - the cluster's spikes' windows would run off the end of .fil
    //     at any candidate shift (handled per-spike)
    if (pcaRefine && pca.valid()) {
        log << "PCA-projection refine pass: search ±" << maxShift
            << " samples, "
            << pca.nCh << " channels × " << pca.nComp << " components\n";
        emitFlush();

        const int totalNbChanP82 = clusteringData->getTotalNbChannels();
        const QList<int>& groupChannelsP82 = clusteringData->getCurrentChannels();
        QString filPathP82;
        {
            const QString baseSpk = spkPath.left(spkPath.lastIndexOf(QLatin1Char('.')));
            const QString noExt = baseSpk.left(baseSpk.lastIndexOf(QLatin1Char('.')));
            if (QFileInfo::exists(noExt + QStringLiteral(".fil")))
                filPathP82 = noExt + QStringLiteral(".fil");
            else
                filPathP82 = noExt + QStringLiteral(".dat");
        }
        FILE* filFp82 = fopen(filPathP82.toLocal8Bit().constData(), "rb");
        if (!filFp82) {
            log << "  WARNING: cannot open " << filPathP82
                << " for PCA-refine — skipping.\n";
            emitFlush();
        } else {
            const int64_t totalSamplesP82 =
                static_cast<int64_t>(QFileInfo(filPathP82).size())
              / (static_cast<int64_t>(sizeof(short)) * totalNbChanP82);

            // Scratch buffers reused across spikes.
            std::vector<int16_t> rawFrame(
                static_cast<size_t>(nSamp * totalNbChanP82));
            std::vector<int16_t> candCM(static_cast<size_t>(spkElems));

            // Use the SMALLER of pca.nCh and nChan in case they disagree
            // (stderiv pipelines drop one channel; pca.nCh = nChan - 1).
            const int chForPca = std::min(pca.nCh, nChan);
            const int kComp    = pca.nComp;
            const int d2u      = pca.data2use;
            const int rShift   = pca.recShift;
            const bool centered = pca.centered;
            const bool useStder = isStderivRealign;

            int nRefined = 0;
            int nClamped = 0;   // spikes where the search bumped the .fil edge

            // ── GPU PCA-refine fast path ─────────────────────────────────
            // For large clusters this replaces the per-spike per-candidate
            // CPU loop below with: one wide .fil read per spike (covers all
            // candidates) + one GPU kernel that evaluates every (spike,
            // candidate) PCA energy and returns the argmax shift per spike.
            //
            // Returns true when the GPU path completed; otherwise we fall
            // through to the CPU loop unchanged.
            _rtSetupMs = _rtmr.elapsed();
            bool gpuPathRan = false;
            do {
                if (!PcaRefineGpu::hasGpu()) break;
                if (N < PcaRefineGpu::gpuThreshold()) break;
                if (nChan > 256) break;        // safety — kernel sums across nChan in one block
                const int M       = 2 * maxShift + 1;
                const int wideLen = nSamp + 2 * maxShift;

                // Bound the working buffer at ~512 MB to avoid surprising
                // the GPU on very large clusters.  Above this the CPU loop
                // is still fast enough that splitting the batch isn't
                // worth the code.
                const long long rawBytes =
                    (long long)N * nChan * wideLen * sizeof(int16_t);
                if (rawBytes > (long long)512 * 1024 * 1024) break;

                std::vector<int16_t> rawWindows(
                    (size_t)N * nChan * wideLen, 0);
                std::vector<int> validRow((size_t)N, 1);

                // ── Phase A — read one wide window per spike ────────────
                // Saves ~M× the .fil syscalls vs the CPU per-candidate loop.
                // A spike whose widest candidate runs off the .fil edge
                // gets validRow[i]=0 — those drop into the CPU path so
                // edge-clamp accounting (nClamped) matches the legacy
                // behaviour exactly.
                int prepared = 0;
                for (int64_t i = 0; i < N; ++i) {
                    const int   curr = cumShift[(size_t)i];
                    const int64_t baseTs = clusterTs[(size_t)i] + curr;
                    const int64_t leftStart = baseTs - maxShift
                                            - (int64_t)peakSamp0;
                    if (leftStart < 0 ||
                        leftStart + wideLen > totalSamplesP82) {
                        validRow[(size_t)i] = 0;
                        continue;
                    }
                    const off_t rawOff = (off_t)leftStart
                                       * (off_t)totalNbChanP82
                                       * (off_t)sizeof(short);
                    if (fseeko(filFp82, rawOff, SEEK_SET) != 0) {
                        validRow[(size_t)i] = 0;
                        continue;
                    }
                    // Read sample-major into rawFrame (reused), then
                    // gather to channel-major into rawWindows.
                    std::vector<int16_t> wide(
                        (size_t)wideLen * totalNbChanP82);
                    if (fread(wide.data(), sizeof(short),
                              wide.size(), filFp82) != wide.size()) {
                        validRow[(size_t)i] = 0;
                        continue;
                    }
                    int16_t* spkBase = rawWindows.data()
                                     + (size_t)i * nChan * wideLen;
                    for (int t = 0; t < wideLen; ++t)
                        for (int ci = 0; ci < nChan; ++ci)
                            spkBase[(size_t)ci * wideLen + t] =
                                wide[(size_t)t * totalNbChanP82
                                   + groupChannelsP82[ci]];
                    ++prepared;
                }

                if (prepared < PcaRefineGpu::gpuThreshold()) break;

                // ── Phase B — pack PCA basis into flat float arrays ─────
                std::vector<float> evecFlat(
                    (size_t)chForPca * kComp * d2u);
                std::vector<float> meansFlat(
                    centered ? (size_t)chForPca * d2u : 0);
                for (int ch = 0; ch < chForPca; ++ch) {
                    const auto& ev = pca.evec[(size_t)ch];
                    for (int k = 0; k < kComp; ++k)
                        for (int u = 0; u < d2u; ++u)
                            evecFlat[(size_t)ch * kComp * d2u
                                   + (size_t)k * d2u + u] =
                                (float)ev[(size_t)k * d2u + u];
                    if (centered) {
                        const auto& mu = pca.means[(size_t)ch];
                        for (int u = 0; u < d2u; ++u)
                            meansFlat[(size_t)ch * d2u + u] =
                                (float)mu[(size_t)u];
                    }
                }

                // ── Phase C — kernel launch + best-shift collection ─────
                std::vector<int> bestShiftGpu((size_t)N, 0);
                const int rc = PcaRefineGpu::refine(
                    (int)N, M, wideLen, nSamp, nChan, chForPca,
                    kComp, d2u, rShift, maxShift,
                    centered ? 1 : 0, useStder ? 1 : 0,
                    rawWindows.data(),
                    evecFlat.data(),
                    centered ? meansFlat.data() : nullptr,
                    bestShiftGpu.data());
                if (rc != 0) {
                    log << "  PCA-refine GPU dispatch returned "
                        << rc << " — falling back to CPU.\n";
                    emitFlush();
                    break;
                }

                // ── Phase D — apply shifts + refresh wavBuf per spike ────
                // For spikes where validRow[i] == 0 (edge-clamped), we
                // mark them as clamped and leave cumShift untouched —
                // matches the CPU path's "every candidate ran off the
                // .fil edge" accounting.
                std::vector<int16_t> rawFrame(
                    (size_t)nSamp * totalNbChanP82);
                for (int64_t i = 0; i < N; ++i) {
                    if (!validRow[(size_t)i]) { ++nClamped; continue; }
                    const int bestS = bestShiftGpu[(size_t)i];
                    if (bestS == 0) continue;

                    cumShift[(size_t)i] = cumShift[(size_t)i] + bestS;
                    ++nRefined;

                    // Refresh wavBuf[i] from .fil at the chosen position
                    // (same logic as the CPU branch below).
                    const int64_t bestStart =
                        clusterTs[(size_t)i] + cumShift[(size_t)i]
                      - (int64_t)peakSamp0;
                    const off_t bestOff = (off_t)bestStart
                                        * (off_t)totalNbChanP82
                                        * (off_t)sizeof(short);
                    if (fseeko(filFp82, bestOff, SEEK_SET) != 0) continue;
                    if (fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filFp82)
                            != rawFrame.size()) continue;
                    int16_t* wTgt = wavBuf.data()
                        + (ptrdiff_t)i * (ptrdiff_t)spkElems;
                    for (int t = 0; t < nSamp; ++t)
                        for (int ci = 0; ci < nChan; ++ci)
                            wTgt[(size_t)ci * nSamp + t] =
                                rawFrame[(size_t)t * totalNbChanP82
                                       + groupChannelsP82[ci]];
                    if (useStder) {
                        std::vector<int16_t> sdPrev((size_t)nChan, 0);
                        for (int t = 0; t < nSamp; ++t) {
                            int64_t sum = 0;
                            for (int ci = 0; ci < nChan; ++ci)
                                sum += wTgt[(size_t)ci * nSamp + t];
                            for (int ci = 0; ci < nChan; ++ci) {
                                const int v = wTgt[(size_t)ci * nSamp + t];
                                const int sd = nChan * v - (int)sum;
                                const int16_t sdCl = (int16_t)
                                    std::max(-32768, std::min(32767, sd));
                                const int diff = (int)sdCl
                                    - (int)sdPrev[(size_t)ci];
                                sdPrev[(size_t)ci] = sdCl;
                                wTgt[(size_t)ci * nSamp + t] = (int16_t)
                                    std::max(-32768, std::min(32767, diff));
                            }
                        }
                    }
                }
                gpuPathRan = true;
            } while (false);

            // ── CPU per-spike refine (GPU path not taken) ────────────────────
            // Each spike's best-shift search is independent: it reads from
            // .fil and writes only its own cumShift[i] and wavBuf[i] slot.  The
            // only shared mutable state is the .fil handle (its file position)
            // and the rawFrame/candCM scratch buffers, so we parallelise across
            // spikes — each thread gets its own handle + buffers.  Falls back to
            // a serial loop when OpenMP is unavailable or per-thread handles
            // cannot be opened, giving identical results either way.
            if (!gpuPathRan) {
                enum class RefineResult { None, Refined, Clamped };
                auto refineSpike =
                    [&](int64_t i, FILE* filFp82,
                        std::vector<int16_t>& rawFrame,
                        std::vector<int16_t>& candCM) -> RefineResult {
                const int   curr = cumShift[static_cast<size_t>(i)];
                const int64_t baseTs = clusterTs[static_cast<size_t>(i)] + curr;
                int bestS = 0;
                double bestEnergy = -1.0;
                int validCandidates = 0;
                for (int s = -maxShift; s <= maxShift; ++s) {
                    const int64_t candStart = baseTs + s
                                            - static_cast<int64_t>(peakSamp0);
                    if (candStart < 0 ||
                        candStart + nSamp > totalSamplesP82) continue;

                    const off_t rawOff = static_cast<off_t>(candStart)
                                       * static_cast<off_t>(totalNbChanP82)
                                       * static_cast<off_t>(sizeof(short));
                    if (fseeko(filFp82, rawOff, SEEK_SET) != 0) continue;
                    if (fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filFp82)
                            != rawFrame.size()) continue;

                    // Subset to group channels (sample-major in rawFrame
                    // → channel-major for PCA projection).
                    for (int t = 0; t < nSamp; ++t)
                        for (int ci = 0; ci < nChan; ++ci)
                            candCM[static_cast<size_t>(ci * nSamp + t)] =
                                rawFrame[static_cast<size_t>(
                                    t * totalNbChanP82 + groupChannelsP82[ci])];

                    // Apply stderiv transform in-place if pipeline needs it.
                    // Mirrors the disk-write block's transform (lines 4280+).
                    if (useStder) {
                        std::vector<int16_t> sdPrev(
                            static_cast<size_t>(nChan), 0);
                        for (int t = 0; t < nSamp; ++t) {
                            int64_t sum = 0;
                            for (int ci = 0; ci < nChan; ++ci)
                                sum += candCM[static_cast<size_t>(ci * nSamp + t)];
                            for (int ci = 0; ci < nChan; ++ci) {
                                const int v = candCM[static_cast<size_t>(ci * nSamp + t)];
                                const int sd = nChan * v - static_cast<int>(sum);
                                const int16_t sdCl = static_cast<int16_t>(
                                    std::max(-32768, std::min(32767, sd)));
                                const int diff = static_cast<int>(sdCl)
                                    - static_cast<int>(sdPrev[static_cast<size_t>(ci)]);
                                sdPrev[static_cast<size_t>(ci)] = sdCl;
                                candCM[static_cast<size_t>(ci * nSamp + t)] =
                                    static_cast<int16_t>(
                                        std::max(-32768, std::min(32767, diff)));
                            }
                        }
                    }

                    // PCA projection energy.  candCM is channel-major:
                    //   candCM[ch * nSamp + t]
                    // pca.means[ch] is [data2use], pca.evec[ch] is
                    // [data2use * nComp] in col-major (same layout the
                    // makeFetRow lambda uses below at ~line 3995).
                    double energy = 0.0;
                    for (int ch = 0; ch < chForPca; ++ch) {
                        const auto& mu = pca.means[static_cast<size_t>(ch)];
                        const auto& ev = pca.evec[static_cast<size_t>(ch)];
                        for (int k = 0; k < kComp; ++k) {
                            double score = 0.0;
                            for (int u = 0; u < d2u; ++u) {
                                const int sIdx = rShift + u;
                                double x = static_cast<double>(
                                    candCM[static_cast<size_t>(ch * nSamp + sIdx)]);
                                if (centered)
                                    x -= mu[static_cast<size_t>(u)];
                                score += ev[static_cast<size_t>(k * d2u + u)] * x;
                            }
                            energy += score * score;
                        }
                    }
                    ++validCandidates;

                    if (energy > bestEnergy) {
                        bestEnergy = energy;
                        bestS = s;
                    }
                }

                if (validCandidates == 0) {
                    // Every candidate ran off the .fil edge — nothing we
                    // can do, leave cumShift unchanged.
                    return RefineResult::Clamped;
                }
                if (bestS == 0) return RefineResult::None;   // already optimal

                cumShift[static_cast<size_t>(i)] = curr + bestS;

                // Refresh wavBuf[i] from the .fil at the chosen position
                // so the downstream score-stats block uses post-refine
                // content.  We need to re-extract once more (we don't
                // cache the per-candidate candCM, only the winning bestS).
                {
                    const int64_t bestStart =
                        clusterTs[static_cast<size_t>(i)]
                      + cumShift[static_cast<size_t>(i)]
                      - static_cast<int64_t>(peakSamp0);
                    const off_t bestOff = static_cast<off_t>(bestStart)
                                        * static_cast<off_t>(totalNbChanP82)
                                        * static_cast<off_t>(sizeof(short));
                    if (fseeko(filFp82, bestOff, SEEK_SET) == 0 &&
                        fread(rawFrame.data(), sizeof(short),
                              rawFrame.size(), filFp82) == rawFrame.size()) {
                        int16_t* wTgt = wavBuf.data()
                            + static_cast<ptrdiff_t>(i)
                            * static_cast<ptrdiff_t>(spkElems);
                        for (int t = 0; t < nSamp; ++t)
                            for (int ci = 0; ci < nChan; ++ci)
                                wTgt[static_cast<size_t>(ci * nSamp + t)] =
                                    rawFrame[static_cast<size_t>(
                                        t * totalNbChanP82
                                      + groupChannelsP82[ci])];
                        if (useStder) {
                            std::vector<int16_t> sdPrev(
                                static_cast<size_t>(nChan), 0);
                            for (int t = 0; t < nSamp; ++t) {
                                int64_t sum = 0;
                                for (int ci = 0; ci < nChan; ++ci)
                                    sum += wTgt[static_cast<size_t>(ci * nSamp + t)];
                                for (int ci = 0; ci < nChan; ++ci) {
                                    const int v = wTgt[static_cast<size_t>(ci * nSamp + t)];
                                    const int sd = nChan * v - static_cast<int>(sum);
                                    const int16_t sdCl = static_cast<int16_t>(
                                        std::max(-32768, std::min(32767, sd)));
                                    const int diff = static_cast<int>(sdCl)
                                        - static_cast<int>(sdPrev[static_cast<size_t>(ci)]);
                                    sdPrev[static_cast<size_t>(ci)] = sdCl;
                                    wTgt[static_cast<size_t>(ci * nSamp + t)] =
                                        static_cast<int16_t>(
                                            std::max(-32768, std::min(32767, diff)));
                                }
                            }
                        }
                    }
                }
                return RefineResult::Refined;
                };  // refineSpike

                // Dispatch: parallel across spikes when OpenMP is available and
                // the cluster is large enough to amortise thread setup; each
                // thread opens its own .fil handle and owns its scratch buffers.
                bool ranParallel = false;
#ifdef _OPENMP
                const int nThr = omp_get_max_threads();
                if (nThr > 1 && N >= 64) {
                    std::vector<FILE*> fh(static_cast<size_t>(nThr), nullptr);
                    bool allOpen = true;
                    for (int t = 0; t < nThr; ++t) {
                        fh[static_cast<size_t>(t)] =
                            fopen(filPathP82.toLocal8Bit().constData(), "rb");
                        if (!fh[static_cast<size_t>(t)]) { allOpen = false; break; }
                    }
                    if (allOpen) {
                        #pragma omp parallel reduction(+:nRefined, nClamped)
                        {
                            const int tid = omp_get_thread_num();
                            FILE* filT = fh[static_cast<size_t>(tid)];
                            std::vector<int16_t> rawT(rawFrame.size());
                            std::vector<int16_t> candT(candCM.size());
                            #pragma omp for schedule(dynamic, 16)
                            for (int64_t i = 0; i < N; ++i) {
                                const RefineResult r =
                                    refineSpike(i, filT, rawT, candT);
                                if (r == RefineResult::Refined)      ++nRefined;
                                else if (r == RefineResult::Clamped) ++nClamped;
                            }
                        }
                        ranParallel = true;
                    }
                    for (FILE* h : fh) if (h) fclose(h);
                }
#endif
                if (!ranParallel) {
                    for (int64_t i = 0; i < N; ++i) {
                        const RefineResult r =
                            refineSpike(i, filFp82, rawFrame, candCM);
                        if (r == RefineResult::Refined)      ++nRefined;
                        else if (r == RefineResult::Clamped) ++nClamped;
                    }
                }
            }  // if (!gpuPathRan)
            fclose(filFp82);
            _rtComputeMs = _rtmr.elapsed();

            log << "  PCA-refine: " << nRefined
                << " spike(s) refined";
            if (nClamped > 0)
                log << " (" << nClamped << " skipped — window off edge)";
            log << ".\n";

            // Recompute nShifted from the now-refined cumShift.
            nShifted = 0;
            for (int64_t i = 0; i < N; ++i)
                if (cumShift[static_cast<size_t>(i)] != 0) ++nShifted;
            log << "  total shifted after refine: " << nShifted << "\n";
            emitFlush();
        }
    } else if (pcaRefine) {
        log << "PCA-projection refine requested but PCA basis not loaded "
               "— skipping.\n";
        emitFlush();
    }

    // -----------------------------------------------------------------------
    // RMS circular group-recenter (opt-in via --recenter-rms)
    //
    // After per-spike alignment, shift the WHOLE cluster by a single offset so
    // its energy envelope sits at peakSamp0.  The anchor is the circular
    // weighted mean of the per-sample RMS² profile (energy across the group,
    // summed over all channels).  The sample axis of the .spk window is
    // periodic, so a circular mean is the correct centroid; it is also exactly
    // invariant to the RMS noise floor — Σ exp(i·2πs/N) over a full period is
    // the sum of the Nth roots of unity, i.e. zero — so a uniform pedestal in
    // the weights cancels with no baseline subtraction.  Weighting by energy
    // (RMS², not RMS) concentrates the mass on the peak.
    //
    // The mean resultant length R (∈[0,1]) guards the degenerate case: when the
    // energy splits into two lobes ~N/2 apart the resultant collapses and the
    // centroid is meaningless.  A clean (bi/tri)phasic spike is a single lobe,
    // so R stays high; if R < rMin we skip the recenter and keep the per-spike
    // alignment rather than throw the cluster to an arbitrary offset.
    //
    // The shift uses the same convention as the per-spike roll
    // (newSpike[t] = oldSpike[(t+δ)%N]; cumShift += δ), so the score stats,
    // mean-after and fresh .fil re-extraction below all pick it up.  Note the
    // circular roll of wavBuf only drives the diagnostics — the .spk content is
    // re-extracted linearly from .fil at clusterTs+cumShift, so the recenter is
    // measured circularly but realised as a clean unwrapped window.
    if (rmsRecenter) {
        // Per-sample energy across the group, summed over all channels.
        std::vector<double> energy(static_cast<size_t>(nSamp), 0.0);
        for (int64_t i = 0; i < N; ++i) {
            const int16_t* w = wavBuf.data()
                + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(spkElems);
            for (int ch = 0; ch < nChan; ++ch) {
                const int16_t* wc = w + ch * nSamp;
                for (int t = 0; t < nSamp; ++t) {
                    const double v = static_cast<double>(wc[t]);
                    energy[static_cast<size_t>(t)] += v * v;
                }
            }
        }

        // Circular weighted mean of the energy profile (shared math).
        const realign_center::RecenterResult rc =
            realign_center::circularRecenterShift(energy.data(), nSamp,
                                                  peakSamp0, rMin);

        if (!rc.applied) {
            log << "RMS recenter: R=" << QString::number(rc.R, 'f', 3)
                << " < rmin=" << QString::number(rMin, 'f', 2)
                << " (energy not single-lobed) — recenter skipped.\n";
            emitFlush();
        } else {
            const int dg = rc.shift;
            if (dg != 0) {
                std::vector<int16_t> tmp(spkElems);
                for (int64_t i = 0; i < N; ++i) {
                    int16_t* w = wavBuf.data()
                        + static_cast<ptrdiff_t>(i)
                        * static_cast<ptrdiff_t>(spkElems);
                    for (int t = 0; t < nSamp; ++t) {
                        const int src = (t + dg + nSamp) % nSamp;
                        for (int ch = 0; ch < nChan; ++ch)
                            tmp[static_cast<size_t>(ch * nSamp + t)] =
                                w[static_cast<size_t>(ch * nSamp + src)];
                    }
                    std::copy(tmp.begin(), tmp.end(), w);
                    cumShift[static_cast<size_t>(i)] += dg;
                }
                // The uniform shift can move previously-unshifted spikes.
                nShifted = 0;
                for (int64_t i = 0; i < N; ++i)
                    if (cumShift[static_cast<size_t>(i)] != 0) ++nShifted;
            }

            log << "RMS recenter: centroid="
                << QString::number(rc.centroid, 'f', 2)
                << "  R=" << QString::number(rc.R, 'f', 3)
                << "  group shift=" << dg << " sample(s)";
            if (dg != 0) log << "  (now " << nShifted << " shifted)";
            log << ".\n";
            emitFlush();
        }
    }

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

        // Apply top-K mask to scratch copies so the final score reflects the
        // same channel subset that drove the alignment (consistent with the
        // iteration-loop xcorr).  Non-destructive — wavBuf and finalTmpl are
        // still needed below for the post-realign mean accumulation.
        const int16_t* scoreWav  = wavBuf.data();
        const int16_t* scoreTmpl = finalTmpl.data();
        std::vector<int16_t> finalWavMasked;
        std::vector<int16_t> finalTmplMasked;
        if (nTopChan > 0 && nTopChan < nChan) {
            // Recompute chanUse from the final template (cluster mean may
            // have shifted channel dominance across iterations).
            std::vector<std::pair<int64_t,int>> chanAmp(
                static_cast<size_t>(nChan));
            for (int ch = 0; ch < nChan; ++ch) {
                int64_t pk = 0;
                for (int t = 0; t < nSamp; ++t) {
                    const int64_t v = std::abs(static_cast<int64_t>(
                        finalTmpl[static_cast<size_t>(ch * nSamp + t)]));
                    if (v > pk) pk = v;
                }
                chanAmp[static_cast<size_t>(ch)] = {pk, ch};
            }
            std::partial_sort(chanAmp.begin(),
                              chanAmp.begin() + nTopChan,
                              chanAmp.end(),
                              [](const auto& a, const auto& b){
                                  return a.first > b.first;
                              });
            std::vector<uint8_t> chanUseFinal(
                static_cast<size_t>(nChan), 0);
            for (int k = 0; k < nTopChan; ++k)
                chanUseFinal[static_cast<size_t>(
                    chanAmp[static_cast<size_t>(k)].second)] = 1;

            finalTmplMasked = finalTmpl;
            finalWavMasked  = wavBuf;
            for (int ch = 0; ch < nChan; ++ch) {
                if (chanUseFinal[static_cast<size_t>(ch)]) continue;
                std::fill(finalTmplMasked.begin() + ch * nSamp,
                          finalTmplMasked.begin() + (ch + 1) * nSamp,
                          int16_t(0));
                for (int64_t i = 0; i < N; ++i) {
                    int16_t* row = finalWavMasked.data()
                                 + static_cast<ptrdiff_t>(i)
                                 * static_cast<ptrdiff_t>(spkElems);
                    std::fill(row + ch * nSamp,
                              row + (ch + 1) * nSamp,
                              int16_t(0));
                }
            }
            scoreWav  = finalWavMasked.data();
            scoreTmpl = finalTmplMasked.data();
        }

        // Use maxShift=0 so no shifting occurs — we just want the scores.
        XcorrDispatch::compute(
            scoreWav, scoreTmpl,
            static_cast<int>(N), nChan, nSamp,
            0, 0.005f, dummySh.data(), allScores.data());

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
    // Two timestamp values are computed per spike:
    //
    //   extTs   — the realigned window position: extTs = oldTs + cumShift.
    //             The .spk waveform is re-extracted from .fil/.dat at this
    //             position (window [extTs - peakSamp0 ...]), which captures the
    //             content that lands at peakSamp0 after the shift and avoids the
    //             wrap-around artifact a circular shift produces at the edges.
    //             extTs is ALSO what gets written to .res, to the .fet timestamp
    //             column, and to the in-memory timestamp — so all four stay in
    //             lock-step and the pipeline-wide invariant
    //                 .spk[i] peak  ≡  .fil at .res[i]
    //             holds.  nudge (and any other tool that re-reads .fil at the
    //             .res offset) depends on this.  Writing the pre-shift detection
    //             sample to .res instead — as an earlier revision did — left
    //             .res pointing cumShift samples away from where the .spk window
    //             actually sits, so the next nudge jumped by cumShift rather
    //             than by the requested single sample.
    //
    //   resTs   — the spike's CURRENT .res value (i.e. where its .spk window
    //             sits going into this pass).  Used only to seed extTs and for
    //             the raw-source .spk-match verification below; it is NOT a
    //             separate value persisted to .res.
    //
    // Sorting is done by extTs so spikes stay in temporal order after any shift
    // of their window position.
    std::vector<int64_t> newTs(static_cast<size_t>(N));   // = extTs (for sort + re-extract)
    std::vector<int64_t> resTs(static_cast<size_t>(N));   // current .res value (seeds extTs + verification)
    for (int64_t i = 0; i < N; ++i) {
        resTs[static_cast<size_t>(i)]  = clusterTs[static_cast<size_t>(i)];
        newTs[static_cast<size_t>(i)]  = clusterTs[static_cast<size_t>(i)]
            + static_cast<int64_t>(cumShift[static_cast<size_t>(i)]);
    }

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

        // wavBuf is loaded from .spkD which already stores the stderiv-
        // transformed waveform in channel-major layout [ch*nSamp+s].
        // For stderiv sessions pca.nCh = nChan-1 (last channel is linearly
        // dependent and excluded); for raw sessions pca.nCh = nChan.
        // Either way, project wav[ch * nSamp + recShift + j2] directly —
        // no second stderiv transform needed.
        const bool canProject = pca.valid() &&
            (isStderivRealign ? (pca.nCh == nChan - 1) : (pca.nCh == nChan));

        if (canProject) {
            int outCol = 0;
            for (int ch = 0; ch < pca.nCh; ++ch) {
                const double* E    = pca.evec[static_cast<size_t>(ch)].data();
                const double* mean = pca.means[static_cast<size_t>(ch)].data();
                for (int c = 0; c < pca.nComp; ++c) {
                    double dot = 0.0;
                    for (int j2 = 0; j2 < pca.data2use; ++j2) {
                        double x = static_cast<double>(
                            wav[static_cast<size_t>(
                                ch * nSamp + pca.recShift + j2)]);
                        if (pca.centered) x -= mean[j2];
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
            // PCA unavailable: copy existing feature values from in-memory Data
            const dataType spikeRow =
                static_cast<dataType>(
                    gidx[static_cast<size_t>(csIdx)] + 1);  // 1-based
            Q_ASSERT_X(clusteringData->isValidSpikeIndex(spikeRow),
                       "featureValue", "spikeRow out of range");
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
    _rtWprepMs = _rtmr.elapsed();
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
    FILE* spkW = fopen(pendingSpkPath.toLocal8Bit().constData(), "r+b");
    FILE* resW = fopen(pendingResPath.toLocal8Bit().constData(), "r+b");
    FILE* fetW = fopen(pendingFetPath.toLocal8Bit().constData(), "r+b");
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

    // -----------------------------------------------------------------------
    // patch69 — verify that the chosen raw source (.fil or .dat) actually
    // matches what's stored in .spk.  Without this check, if .fil got
    // deleted but the .spk was originally extracted FROM .fil (filtered),
    // the code at lines 4013-4016 silently falls back to .dat (unfiltered)
    // and re-extracts unfiltered waveforms into .spk slots that were
    // filtered, corrupting the cluster a little more on every nudge.
    //
    // Algorithm: pick the first cluster spike whose extraction window fits
    // entirely within the raw file, read it, apply the same stderiv
    // transform we'd apply during normal extraction, and compare to that
    // spike's existing .spk slot (the channel-major copy currently in
    // wavBuf).  If RMS difference exceeds 50% of the .spk content's RMS,
    // the chosen raw source does not match — abort with a clear error
    // rather than silently corrupting more waveforms.
    //
    // Skipped only when filF is null (no raw source — log already warned).
    // -----------------------------------------------------------------------
    if (filF) {
        bool verifyDone = false;
        int  verifySkipped = 0;
        for (int64_t csV = 0; csV < N && !verifyDone; ++csV) {
            const int64_t tsV          = resTs[static_cast<size_t>(csV)];
            const int64_t startSampleV = tsV - static_cast<int64_t>(peakSamp0);
            if (startSampleV < 0 || startSampleV + nSamp > totalSamples) {
                ++verifySkipped;
                continue;
            }
            const off_t rawOffV = static_cast<off_t>(startSampleV)
                                * static_cast<off_t>(totalNbChan)
                                * static_cast<off_t>(sizeof(short));
            if (fseeko(filF, rawOffV, SEEK_SET) != 0) { ++verifySkipped; continue; }
            std::vector<int16_t> rawFrameV(static_cast<size_t>(nSamp * totalNbChan));
            if (fread(rawFrameV.data(), sizeof(short),
                      rawFrameV.size(), filF) != rawFrameV.size()) {
                ++verifySkipped; continue;
            }
            // Channel-major raw waveform for the group's channels
            std::vector<int16_t> verifyWav(static_cast<size_t>(nChan * nSamp));
            for (int s = 0; s < nSamp; ++s)
                for (int ci = 0; ci < nChan; ++ci)
                    verifyWav[static_cast<size_t>(ci * nSamp + s)] =
                        rawFrameV[static_cast<size_t>(s * totalNbChan + groupChannels[ci])];

            // Mirror the stderiv transform used in the per-spike loop, so
            // verifyWav lives in the same space as wavBuf for raw OR stderiv
            // .spk formats.
            if (spkIsTransformed) {
                std::vector<int16_t> sdPrev(static_cast<size_t>(nChan), 0);
                std::vector<int16_t> sdOut(static_cast<size_t>(nChan * nSamp));
                for (int s = 0; s < nSamp; ++s) {
                    int64_t sum = 0;
                    for (int ci = 0; ci < nChan; ++ci)
                        sum += verifyWav[static_cast<size_t>(ci * nSamp + s)];
                    for (int ci = 0; ci < nChan; ++ci) {
                        const int v   = verifyWav[static_cast<size_t>(ci * nSamp + s)];
                        const int sd  = nChan * v - static_cast<int>(sum);
                        const int16_t sdCl = static_cast<int16_t>(
                            std::max(-32768, std::min(32767, sd)));
                        const int diff = static_cast<int>(sdCl)
                            - static_cast<int>(sdPrev[static_cast<size_t>(ci)]);
                        sdPrev[static_cast<size_t>(ci)] = sdCl;
                        sdOut[static_cast<size_t>(ci * nSamp + s)] =
                            static_cast<int16_t>(
                                std::max(-32768, std::min(32767, diff)));
                    }
                }
                verifyWav = std::move(sdOut);
            }

            // Compare to wavBuf[csV * spkElems ..] which holds the .spk
            // contents in channel-major layout (same as verifyWav above).
            const int16_t* refSpk = wavBuf.data()
                + static_cast<ptrdiff_t>(csV) * static_cast<ptrdiff_t>(spkElems);
            double sumDiff2 = 0.0, sumRef2 = 0.0;
            for (int e = 0; e < spkElems; ++e) {
                const double dv = static_cast<double>(verifyWav[static_cast<size_t>(e)])
                                - static_cast<double>(refSpk[static_cast<size_t>(e)]);
                sumDiff2 += dv * dv;
                sumRef2  += static_cast<double>(refSpk[static_cast<size_t>(e)])
                          * static_cast<double>(refSpk[static_cast<size_t>(e)]);
            }
            const double rmsRatio = (sumRef2 > 0.0)
                ? std::sqrt(sumDiff2 / sumRef2)
                : 0.0;

            if (rmsRatio > 0.5) {
                log << "ERROR: raw source verification FAILED\n"
                    << "  raw file:           " << filPath << "\n"
                    << "  verify spike index: " << csV
                    << " (ts=" << tsV << ")\n"
                    << "  RMS difference:     "
                    << QString::number(rmsRatio * 100.0, 'f', 1)
                    << "% of .spk content (threshold 50%)\n"
                    << "  Likely cause: .spk was extracted from a different raw\n"
                    << "  source than the one currently on disk (e.g. .fil was\n"
                    << "  deleted and the code fell back to .dat, or the raw file\n"
                    << "  was overwritten by a later pipeline stage).\n"
                    << "  Aborting realignment to avoid further .spk corruption.\n"
                    << "  Re-extract waveforms with process_extractspikes before\n"
                    << "  retrying the nudge.\n";
                emitFlush();
                if (spkW) fclose(spkW);
                if (resW) fclose(resW);
                if (fetW) fclose(fetW);
                fclose(filF);
                return false;
            }
            log << "Raw source verified: " << filPath
                << " matches .spk (verify spike " << csV
                << ", RMS diff "
                << QString::number(rmsRatio * 100.0, 'f', 1)
                << "%, skipped " << verifySkipped
                << " spikes near file edges)\n";
            emitFlush();
            verifyDone = true;
        }
        if (!verifyDone) {
            log << "WARNING: could not verify raw source — no spike's extraction\n"
                << "  window fit within the raw file (all near boundaries).\n"
                << "  Proceeding without verification.\n";
            emitFlush();
        }
    }

    _rtWopenMs = _rtmr.elapsed();
    for (int64_t j = 0; j < N; ++j) {
        const int64_t csIdx = sortedOrder[static_cast<size_t>(j)];
        const int64_t dest  = targetPos[static_cast<size_t>(j)];
        const int64_t ts = newTs[static_cast<size_t>(csIdx)]; // extTs: re-extraction window AND .res/.fet/in-memory timestamp (kept consistent)

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
                        // Build channel-major raw waveform for w[]
                        std::vector<int16_t> rawCM(static_cast<size_t>(nChan * nSamp));
                        for (int s = 0; s < nSamp; ++s)
                            for (int ci = 0; ci < nChan; ++ci)
                                rawCM[static_cast<size_t>(ci * nSamp + s)] =
                                    rawFrame[static_cast<size_t>(
                                        s * totalNbChan + groupChannels[ci])];
                        // .spk write path: transform only when the .spk file
                        // actually stores stderiv waveforms (Pipeline D).  The
                        // wavBuf `w` parallel update must match the .spk format,
                        // so xcorr against the on-disk template stays consistent.
                        if (spkIsTransformed) {
                            // Apply stderiv transform: spatial all-pairs derivative
                            // then temporal first-difference.  Output updates both
                            // spkRow (sample-major, for .spkD write) and w (channel-
                            // major, so wavBuf stays in stderiv space for xcorr).
                            std::vector<int16_t> sdWav(static_cast<size_t>(nSamp * nChan));
                            std::vector<int16_t> sdPrev(static_cast<size_t>(nChan), 0);
                            for (int s = 0; s < nSamp; ++s) {
                                int64_t sum = 0;
                                for (int ci = 0; ci < nChan; ++ci)
                                    sum += rawCM[static_cast<size_t>(ci * nSamp + s)];
                                for (int ci = 0; ci < nChan; ++ci) {
                                    const int v = rawCM[static_cast<size_t>(ci * nSamp + s)];
                                    const int sd = nChan * v - static_cast<int>(sum);
                                    const int16_t sdCl = static_cast<int16_t>(
                                        std::max(-32768, std::min(32767, sd)));
                                    const int diff = static_cast<int>(sdCl)
                                        - static_cast<int>(sdPrev[static_cast<size_t>(ci)]);
                                    sdPrev[static_cast<size_t>(ci)] = sdCl;
                                    const int16_t tdv = static_cast<int16_t>(
                                        std::max(-32768, std::min(32767, diff)));
                                    sdWav[static_cast<size_t>(s * nChan + ci)] = tdv;
                                    // Update wavBuf (channel-major) in stderiv space
                                    w[static_cast<size_t>(ci * nSamp + s)] = tdv;
                                }
                            }
                            // spkRow: sample-major stderiv (what .spkD stores)
                            spkRow = std::move(sdWav);
                        } else {
                            // Raw pipeline: sample-major for .spk, channel-major for w
                            for (int s = 0; s < nSamp; ++s)
                                for (int ci = 0; ci < nChan; ++ci) {
                                    spkRow[static_cast<size_t>(s * nChan + ci)] =
                                        rawCM[static_cast<size_t>(ci * nSamp + s)];
                                    w[static_cast<size_t>(ci * nSamp + s)] =
                                        rawCM[static_cast<size_t>(ci * nSamp + s)];
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
        fwrite(&ts, sizeof(int64_t), 1, resW);  // extTs — keep .res in lock-step with the re-extracted .spk window so .spk[i] peak == .fil at .res[i] (the invariant nudge depends on)

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
    _rtWwriteMs = _rtmr.elapsed();

    // spkFileName already points to pendingSpkPath (set on open and kept
    // permanently) — no redirect needed here.
    pendingRealign.push_back(std::move(pending));

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

    // Log the cluster after realignment — features and timestamps have been
    // updated.  Served from the batch centroid cache during Align-All.
    logAfter(QList<int>{ clusterId });

    if (_timing) {
        const qint64 _tot = _rtmr.elapsed();
        log << "[timing] cluster " << clusterId
            << ": nspk=" << N
            << " gap=" << _gapMs << "ms"
            << " setup=" << _rtSetupMs << "ms"
            << " compute=" << (_rtComputeMs - _rtSetupMs) << "ms"
            << " wsort=" << (_rtWprepMs - _rtComputeMs) << "ms"
            << " wopen=" << (_rtWopenMs - _rtWprepMs) << "ms"
            << " wwrite=" << (_rtWwriteMs - _rtWopenMs) << "ms"
            << " wlog=" << (_tot - _rtWwriteMs) << "ms"
            << " total=" << _tot << "ms";
        emitFlush();
        // Mirror to stderr so the line lands in the launching terminal too —
        // far easier to redirect/capture than the in-app realign output panel
        // (which is where the live log above goes).
        fprintf(stderr,
                "[timing] cluster %d: nspk=%lld gap=%lldms setup=%lldms "
                "compute=%lldms wsort=%lldms wopen=%lldms wwrite=%lldms "
                "wlog=%lldms total=%lldms\n",
                clusterId, (long long)N, _gapMs, (long long)_rtSetupMs,
                (long long)(_rtComputeMs - _rtSetupMs),
                (long long)(_rtWprepMs - _rtComputeMs),
                (long long)(_rtWopenMs - _rtWprepMs),
                (long long)(_rtWwriteMs - _rtWopenMs),
                (long long)(_tot - _rtWwriteMs), (long long)_tot);
        fflush(stderr);
    }
    // Record this call's end so the next cluster can report its gap.  Updated
    // unconditionally (cheap) so it is correct regardless of the timing flag.
    m_realignPrevEndMs = (long long)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

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
    pendingSpkPath = origSpkPath + QStringLiteral(".pending");
    pendingResPath = origResPath + QStringLiteral(".pending");
    pendingFetPath = origFetPath + QStringLiteral(".pending");
    pendingCluPath = docUrl       + QStringLiteral(".pending");

    // Helper: overwrite dst with a fresh copy of src.
    auto seedFile = [](const QString& src, const QString& dst) -> bool {
        QFile::remove(dst);
        if (!QFile::copy(src, dst)) {
            qWarning() << "[initPendingFiles] copy failed:" << src << "->" << dst;
            return false;
        }
        return true;
    };

    const bool ok = seedFile(origSpkPath, pendingSpkPath)
                 && seedFile(origResPath, pendingResPath)
                 && seedFile(origFetPath, pendingFetPath)
                 && seedFile(docUrl,        pendingCluPath);

    if (ok) {
        // Redirect the waveform reader and clu writer to the pending files.
        // They will remain here for the entire document session.
        clusteringData->setSpkFileName(pendingSpkPath);
        tmpCluFile = pendingCluPath;
    }
    return ok;
}

bool KlustersDoc::commitAndRenewPending(QString* outError)
{
    // patch63 — Step 1: copy each pending file over the original.
    // QFile::copy refuses to overwrite, so remove the target first.
    // Each step now reports failure via outError + return value instead
    // of silently swallowing the error.  Without this, a failed
    // QFile::copy here meant the user saw "save successful" but the
    // on-disk file was unchanged.
    QString firstError;
    auto copyOver = [&firstError](const QString& src, const QString& dst,
                                  const char* label) -> bool {
        if (!QFile::exists(src)) {
            if (firstError.isEmpty()) {
                firstError = QString(
                    "Cannot commit %1 — pending file missing:\n%2")
                    .arg(QString::fromLatin1(label)).arg(src);
            }
            qWarning().noquote() << "[commitAndRenewPending]" << label
                                 << "pending missing:" << src;
            return false;
        }
        QFile::remove(dst);    // OK if dst doesn't exist
        if (!QFile::copy(src, dst)) {
            // QFile doesn't expose errno reliably — best we can do is
            // report both paths and check permissions/existence.
            QFileInfo dstInfo(dst);
            QFileInfo dirInfo(dstInfo.absolutePath());
            const QString hint =
                !dirInfo.isWritable() ? "parent directory not writable by current user"
              : QFileInfo::exists(dst) ? "destination still present after remove() — likely owned by another user, or immutable (chattr +i)"
              : "QFile::copy refused — check file ownership and parent-directory write permission";
            if (firstError.isEmpty()) {
                firstError = QString(
                    "Cannot commit %1:\n  %2\n→ %3\n\nHint: %4")
                    .arg(QString::fromLatin1(label))
                    .arg(src).arg(dst).arg(hint);
            }
            qWarning().noquote() << "[commitAndRenewPending]" << label
                                 << "copy failed:" << src << "->" << dst
                                 << "hint:" << hint;
            return false;
        }
        return true;
    };
    bool allOk = true;
    allOk &= copyOver(pendingSpkPath, origSpkPath, "spk");
    allOk &= copyOver(pendingResPath, origResPath, "res");
    allOk &= copyOver(pendingFetPath, origFetPath, "fet");
    allOk &= copyOver(pendingCluPath, docUrl,      "clu");

    // Clear the in-memory queue — all realignment batches have been
    // applied to disk (even if a later step failed).  Don't replay them.
    pendingRealign.clear();

    // Step 2 — renew: re-seed the pending files from the fresh originals so
    // the next realignment (or another save cycle) starts from a clean slate.
    initPendingFiles();

    if (!allOk && outError) *outError = firstError;
    return allOk;
}

void KlustersDoc::rejectLastRealign()
{
    if (pendingRealign.empty()) return;

    const PendingRealign& p = pendingRealign.back();

    // Restore in-memory feature/timestamp data.
    for (const PendingSpkRecord& rec : p.records) {
        clusteringData->updateFeatureRow(
            static_cast<dataType>(rec.destPos + 1),
            rec.origFet);
        clusteringData->updateTimestamp(
            static_cast<dataType>(rec.destPos + 1),
            static_cast<dataType>(rec.origTs));
    }

    pendingRealign.pop_back();

    // Re-seed pending files from the untouched originals so the waveform
    // viewer immediately reflects the restored state.
    initPendingFiles();
}

// ---------------------------------------------------------------------------
// KlustersDoc::clusterHasMembers
// ---------------------------------------------------------------------------
// Returns true iff `clusterId` is present in Data::clusterInfoMap with
// nbSpikes() > 0.  This is the same map createFeatureFile() reads when
// building the recluster temp .fet, so a false return here will produce
// an empty .fet → KK aborts with the cryptic "Array::SetSize: n < 1
// (n=0, tag=Data (nPoints*nDims))" once it tries to allocate.
//
// The cluster palette reads its membership counts from spikesByCluster
// (a row → cluster table) which can stay populated even when
// clusterInfoMap loses the corresponding key — typically after a
// curation-log replay, an undone merge/split, or an aborted reorder
// where the row-table commit landed but the clusterInfoMap rebuild
// didn't fire.  (Note: nudge itself does NOT desync the map — it only
// mutates feature rows and timestamps.  If recluster fails right after
// a nudge, the desync was already present BEFORE the nudge.)  Catching
// the desync here turns a downstream KK exception into a useful UI
// message and lets the user save / reopen to resync.
// ---------------------------------------------------------------------------
bool KlustersDoc::clusterHasMembers(int clusterId) const
{
    if (!clusteringData) return false;
    return clusteringData->clusterHasMembers(clusterId);
}

// ---------------------------------------------------------------------------
// KlustersDoc::nudgeClusterTimestamps
// ---------------------------------------------------------------------------
// Shift every spike in @p clusterId by @p deltaSamples raw samples.
// Re-extracts waveforms from .fil at the new timestamp and reprojects onto
// the .pca.N eigenvectors, applying the spatial+temporal derivative if the
// PCA model has nCh == nChan - 1 (stderiv pipeline detection).
// ---------------------------------------------------------------------------
bool KlustersDoc::nudgeClusterTimestamps(int clusterId, int deltaSamples)
{
    if (!clusteringData) return false;
    if (deltaSamples == 0) return true;

    logBefore(CurationLogger::ActionType::NUDGE, QList<int>{ clusterId });

    // ── Stop all in-flight WaveformThreads BEFORE any file writes ─────────
    // A WaveformThread reads from pendingSpkPath (= spkFileName) without
    // holding any lock around the fread call.  If we write to that file
    // while the thread is mid-read we get a torn read → garbage waveforms
    // or, when that data drives an array index, a segfault.  Stop all
    // threads first so the file is idle before we touch it.
    for (int i = 0; i < viewList->count(); ++i)
        viewList->at(i)->stopAllViewThreads();

    if (pendingResPath.isEmpty()) {
        if (!initPendingFiles()) return false;
    }

    Data& d              = *clusteringData;
    const int   nChan    = d.nbOfChannels();
    const int   nSamp    = d.nbSamplesPerWaveform();
    const int   peakSamp0 = d.peakSampleIndex() - 1;  // 0-based
    const int   timeDim  = d.timeDimension();
    const int   nFeatCols = timeDim - 1;
    const int   totalNbChan = d.getTotalNbChannels();
    const QList<int>& groupChannels = d.getCurrentChannels();

    // ── Load .pca.N / .pcaD.N model ──────────────────────────────────────
    // .pcaD.N is written by process_pca when its output is .fetD.N.
    // Its existence definitively identifies a stderiv session — no need
    // for the nCh==nChan-1 heuristic.
    // Header format (5 x int32): nCh, data2use, nComp, isCentered, recShift
    struct PcaBasis {
        int  nCh=0, data2use=0, nComp=0, recShift=0;
        bool centered=false;
        std::vector<std::vector<double>> means, evec;
        bool valid() const { return nCh>0 && data2use>0 && nComp>0; }
    } pca;

    // Derive session base: strip ".<type>[.<variant>].N" (handles .spk.N,
    // .spkD.N and dotted .spk.stderiv.N).
    const QString sessionBase = stripFeatureSuffix(origSpkPath, "spk");

    // Pipeline detection — two independent signals.  Decoupling them fixes
    // Pipeline C (raw .spk + stderiv .fetD/.pcaD) which was previously
    // misclassified as a raw session because only the .spk variant was
    // checked: PCA basis selection would fall back to .pca.N and feature
    // reprojection would skip the stderiv transform, silently corrupting
    // .fetD rows written by nudge.
    //
    //   spkIsTransformed — .spk stores stderiv waveforms (Pipeline D).
    //                      Apply applyStderivTransform when writing the
    //                      re-extracted raw waveform back to .spk; for
    //                      raw .spk (Pipeline A, C) write it unchanged.
    //                      Do NOT infer this from .pcaD existence — if
    //                      the session was opened with raw .spk.N and
    //                      ndm_pca_stderiv ran later producing .pcaD.N,
    //                      applying the transform to already-raw waveforms
    //                      would double-transform them.
    //   fetIsStderiv    — .fet features are stderiv-space, built on the
    //                      stderiv .pca basis.  Select .pca.stderiv.N and apply
    //                      the stderiv transform before projecting the raw
    //                      waveform onto the eigenvectors.
    // Independent flags (Pipeline-C), read off the resolved artifacts:
    const QString nudgeMethod = featureMethod(origFetPath);
    const bool spkIsTransformed = (featureMethod(origSpkPath) == QLatin1String("stderiv"));
    const bool fetIsStderiv     = (nudgeMethod == QLatin1String("stderiv"));
    // Legacy name retained for any downstream use that really means the
    // feature-space flag; nothing in nudge uses this directly after the
    // refactor below, but keep it for grep compatibility during review.
    const bool isStderivSession = fetIsStderiv;
    (void)isStderivSession;

    // PCA basis follows the session method: .pca.stderiv.N stores eigenvectors
    // on the (nChan-1) stderiv-space channels, paired with .fet.stderiv.N.
    const QString chosenPca = resolveFeature(
        sessionBase, "pca", electrodeGroupID, nudgeMethod);

    if (QFileInfo::exists(chosenPca)) {
        FILE* fp = fopen(chosenPca.toLocal8Bit().constData(), "rb");
        if (fp) {
            // 5-word header: nCh, data2use, nComp, isCentered, recShift
            int32_t hdr[5] = {};
            if (fread(hdr, sizeof(int32_t), 5, fp) == 5) {
                pca.nCh=hdr[0]; pca.data2use=hdr[1]; pca.nComp=hdr[2];
                pca.centered=(hdr[3]!=0); pca.recShift=hdr[4];
                if (pca.nCh>0 && pca.nCh<=64 && pca.data2use>0 && pca.nComp>0) {
                    pca.means.resize(static_cast<size_t>(pca.nCh));
                    pca.evec .resize(static_cast<size_t>(pca.nCh));
                    bool ok=true;
                    for (int ch=0; ch<pca.nCh && ok; ++ch) {
                        pca.means[static_cast<size_t>(ch)].resize(static_cast<size_t>(pca.data2use));
                        ok=(fread(pca.means[static_cast<size_t>(ch)].data(),8,
                                  static_cast<size_t>(pca.data2use),fp)
                            == static_cast<size_t>(pca.data2use));
                    }
                    for (int ch=0; ch<pca.nCh && ok; ++ch) {
                        const size_t evSz=static_cast<size_t>(pca.data2use*pca.nComp);
                        pca.evec[static_cast<size_t>(ch)].resize(evSz);
                        ok=(fread(pca.evec[static_cast<size_t>(ch)].data(),8,evSz,fp)==evSz);
                    }
                    if (!ok) pca=PcaBasis{};
                } else { pca=PcaBasis{}; }
            }
            fclose(fp);
        }
    }

    // ── Refuse to nudge when the PCA basis failed to load ────────────────
    // makeFetRow returns a fixed-size zero-filled row when pca.valid() is
    // false (it cannot reproject without eigenvectors).  The disk write at
    // line ~4767 was previously gated on `!fetRowWritten.empty()`, which
    // ALWAYS passes — the row has the right size, just zero contents — so
    // a missing or unreadable .pca[D].N file resulted in every spike's .fet
    // row being silently overwritten with zeros.  The in-memory update is
    // correctly guarded on pca.valid() (line ~4802), so on-disk and
    // in-memory diverge: features go to zero on disk while the cluster
    // looks normal in the GUI until the session is reopened.
    //
    // Loud failure is the right behaviour here.  Nudging without
    // reprojection is not actually well-defined (you'd shift timestamps
    // and waveforms but leave features pointing at the old position),
    // so refuse rather than producing an inconsistent state.
    if (!pca.valid()) {
        QString msg = QString(
            "[nudge] cluster %1: refusing to nudge — PCA basis (%2) "
            "could not be loaded.  Run process_pca / process_pca_stderiv "
            "to regenerate, or check file permissions.")
            .arg(clusterId).arg(chosenPca);
        qWarning().noquote() << msg;
        if (auto* sb = app() ? app()->statusBar() : nullptr)
            sb->showMessage(QString("Nudge refused: PCA basis (%1) "
                                    "not available.").arg(chosenPca), 8000);
        return false;
    }

    // Feature-reprojection path: requires a valid .pcaD basis AND a .fetD
    // feature file.  The PCA basis is only meaningful if we know whether it
    // was computed in stderiv space — fetIsStderiv encodes exactly that.
    const bool isStderivFet = fetIsStderiv && pca.valid();
    // .spk write path: transform only if the file on disk is actually in
    // stderiv space (Pipeline D).  Independent of whether .fet is stderiv.
    const bool isStderivSpk = spkIsTransformed;
    // Alias kept for comments / logs / future code that references
    // "isStderiv".  Do not use it inside the transform branches below —
    // use the specific flag that matches the branch's output target.
    const bool isStderiv = isStderivFet || isStderivSpk;
    (void)isStderiv;

    // ── Open .fil/.dat for waveform re-extraction ─────────────────────────
    // sessionBase is already the bare session path (no .spk/.spkD/.N suffix).
    QString filPath;
    if (QFileInfo::exists(sessionBase + QStringLiteral(".fil")))
        filPath = sessionBase + QStringLiteral(".fil");
    else
        filPath = sessionBase + QStringLiteral(".dat");
    FILE* filF = fopen(filPath.toLocal8Bit().constData(), "rb");
    const int64_t totalSamples = filF
        ? (static_cast<int64_t>(QFileInfo(filPath).size())
           / (static_cast<int64_t>(sizeof(short)) * totalNbChan))
        : 0LL;

    // Upper bound for timestamps: last sample at which a full waveform window
    // fits — i.e. the peak can sit at peakSamp0 and the window doesn't
    // run past the end of the recording.
    const int64_t maxValidTs = totalSamples > 0
        ? (totalSamples - static_cast<int64_t>(nSamp)
           + static_cast<int64_t>(peakSamp0))
        : static_cast<int64_t>(d.maxDimension(d.timeDimension()));

    // ── Open pending files for writing ────────────────────────────────────
    FILE* spkW = fopen(pendingSpkPath.toLocal8Bit().constData(), "r+b");
    FILE* resW = fopen(pendingResPath.toLocal8Bit().constData(), "r+b");
    FILE* fetW = fopen(pendingFetPath.toLocal8Bit().constData(), "r+b");
    if (!resW || !fetW || !spkW) {
        if (spkW) fclose(spkW);
        if (resW) fclose(resW);
        if (fetW) fclose(fetW);
        if (filF) fclose(filF);
        return false;
    }

    // ── Per-spike waveform re-extraction helper ───────────────────────────
    // Reads raw waveform from .fil into channel-major layout [ch*nSamp+s].
    //
    // patch89: also reads ONE EXTRA raw sample at (startSample - 1) into
    // `prevSample` (channel-major, group channels only, nChan int16s).
    // applyStderivTransform uses this to compute the correct prev_sdiff
    // for the sample-0 temporal first-difference, matching the canonical
    // extractor's g_prev_sdiff behaviour across chunk boundaries in
    // fill_sdiff_buffer (process_extractspikes_stderiv.cpp:181-186).
    //
    // Pre-patch89 this lambda always returned `prevSample` empty and
    // applyStderivTransform fell back to sd[-1]=0 per spike — that did
    // NOT match the canonical extractor (which uses the prior sample's
    // spatial derivative as prev_sdiff for sample 0), so every nudged
    // spike differed from a canonically-extracted spike by exactly
    // sdiff[ws-1] at sample 0.  Visible as cluster mean dispersion
    // accumulating across nudges.  Edge case: when startSample==0 (spike
    // at the very start of the recording), no prev sample exists;
    // prevSample stays empty and the transform falls back to prev=0 to
    // match the canonical extractor's first-chunk startup behaviour.
    const int64_t bytesPerSpike = static_cast<int64_t>(nChan) * nSamp * 2;

    auto extractWaveform = [&](int64_t ts,
                                std::vector<int16_t>& wav,
                                std::vector<int16_t>& prevSample) -> bool {
        wav.assign(static_cast<size_t>(nChan * nSamp), 0);
        prevSample.clear();
        if (!filF) return false;
        const int64_t startSample = ts - static_cast<int64_t>(peakSamp0);
        if (startSample < 0 || startSample + nSamp > totalSamples) return false;

        // patch89: read (nSamp + 1) raw samples starting at (startSample - 1)
        // when we have a sample to spare; the leading sample feeds prev_sdiff.
        const bool hasPrev = (startSample >= 1);
        const int64_t readStart = hasPrev ? (startSample - 1) : startSample;
        const int nReadSamples  = nSamp + (hasPrev ? 1 : 0);

        const off_t rawOff = static_cast<off_t>(readStart)
                           * static_cast<off_t>(totalNbChan) * 2;
        if (fseeko(filF, rawOff, SEEK_SET) != 0) return false;
        std::vector<int16_t> rawFrame(
            static_cast<size_t>(nReadSamples) * static_cast<size_t>(totalNbChan));
        if (fread(rawFrame.data(), 2, rawFrame.size(), filF) != rawFrame.size())
            return false;

        // Split rawFrame: when hasPrev, sample 0 of rawFrame is the prev
        // sample; samples [1..nSamp] are the spike window.  When !hasPrev,
        // all nSamp samples are the spike window.
        if (hasPrev) {
            prevSample.resize(static_cast<size_t>(nChan));
            for (int ci = 0; ci < nChan; ++ci)
                prevSample[static_cast<size_t>(ci)] =
                    rawFrame[static_cast<size_t>(groupChannels[ci])];
        }
        const int wavOffset = hasPrev ? 1 : 0;
        for (int s = 0; s < nSamp; ++s)
            for (int ci = 0; ci < nChan; ++ci)
                wav[static_cast<size_t>(ci * nSamp + s)] =
                    rawFrame[static_cast<size_t>((s + wavOffset) * totalNbChan
                             + groupChannels[ci])];
        return true;
    };

    // ── Stderiv transform: matches ndm_extractspikes_stderiv exactly ──────
    // Returns transformed waveform in sample-major layout [s*nChan+ch]
    // (ALL nChan channels, including the linearly-dependent last one).
    //
    // prevRaw: kept for ABI compatibility with existing call sites; ignored.
    // The canonical pipeline applies the temporal first-difference per
    // spike with sdiff[-1] = 0, so the actual preceding raw sample is
    // not used.  See the comment on `prev` inside the body for the full
    // rationale.
    // Saturation counters — populated by applyStderivTransform via lambda
    // capture-by-reference.  Reported once after the main loop.  The int16
    // .spk format inherently clips spatial derivatives that exceed ±32768,
    // and temporal first-differences between two identically-saturated
    // samples are exactly 0 (sd[s] - sd[s-1] = -32768 - -32768 = 0).  This
    // produces visible "zero plateau" segments on the largest-amplitude
    // spikes in a cluster — a property of the canonical pipeline, not a
    // nudge bug, but worth surfacing so it can be distinguished from real
    // corruption.
    int64_t spatialClampCount  = 0;
    int64_t temporalClampCount = 0;
    int64_t temporalZeroCount  = 0;  // # of (sd - prev = 0) events

    // ── Spatial-derivative order: read from YAML ─────────────────────────
    // The canonical pipeline (process_extractspikes_stderiv) supports four
    // spatial-derivative modes selected via -d / sdiffOrder:
    //   0 = SDIFF_NONE       (pass-through, no spatial transform)
    //   1 = SDIFF_FIRST      (val − val[ci+1], or val − val[ci-1] at the edge)
    //   2 = SDIFF_LAPLACIAN  (val − ½(val[ci-1] + val[ci+1]) at interior)
    //   3 = SDIFF_ALLPAIRS   (nChan·val − Σ val   ←  the YAML default)
    //
    // Earlier nudge builds hardcoded SDIFF_ALLPAIRS.  Sessions clustered
    // with any other mode would silently get wrong waveforms after nudge:
    // the transform applied by nudge would not match the one applied by
    // the original .spkD/.fetD/.pcaD generation, so the new waveform sits
    // in a slightly off-axis subspace of the .pcaD eigenvector basis.
    //
    // Now read sdiffOrder from the session YAML.  The value is stored under
    // ndm_extractspikes_stderiv (extraction time).  ndm_pca_stderiv stores
    // its own copy under that program too, but they should agree — if they
    // don't, the session is inconsistent and we should refuse to nudge.
    // Default to ALLPAIRS only when the YAML doesn't carry the field at
    // all (legacy sessions predating the YAML schema).
    int sdiffOrder = 3;  // SDIFF_ALLPAIRS — the canonical default
    if (!parameterFile.isEmpty()) {
        ParameterYamlReader reader;
        if (reader.parseFile(parameterFile)) {
            const QString s = reader.getProgramParameter(
                QStringLiteral("ndm_extractspikes_stderiv"),
                QStringLiteral("sdiffOrder"));
            if (!s.isEmpty()) {
                bool ok = false;
                const int v = s.toInt(&ok);
                if (ok && v >= 0 && v <= 3) sdiffOrder = v;
            }
        }
    }
    // Capture as const inside the lambda to make the branch cheap.
    const int kSdiffOrder = sdiffOrder;
    NS3_DIAG() << "[nudge] sdiffOrder=" << kSdiffOrder
               << " (0=none 1=first 2=laplacian 3=allpairs)"
               << " parameterFile=" << parameterFile;

    auto applyStderivTransform = [&](const std::vector<int16_t>& wavCM,
                                     const std::vector<int16_t>& prevRaw,
                                     std::vector<int16_t>& out) {
        // wavCM: channel-major [ch*nSamp+s]
        // out:   sample-major  [s*nChan+ch]
        out.resize(static_cast<size_t>(nSamp * nChan));

        // Step 1: spatial derivative (matches process_extractspikes_stderiv
        // computeSDiff for each mode).  Result lands in `out` in the
        // sample-major layout that matches the canonical Pass 2 writer.
        for (int s = 0; s < nSamp; ++s) {
            // Cache the channel values at this time-sample for cheap
            // intra-channel access in modes 1 (FIRST) and 2 (LAPLACIAN).
            // We read channel-major wavCM[ci * nSamp + s].
            for (int ci = 0; ci < nChan; ++ci) {
                const int val = wavCM[static_cast<size_t>(ci * nSamp + s)];
                int sd;
                switch (kSdiffOrder) {
                case 0:  // SDIFF_NONE — pass-through
                    sd = val;
                    break;
                case 1: {  // SDIFF_FIRST — val − next-channel (or prev at edge)
                    const int other = (ci < nChan - 1)
                        ? wavCM[static_cast<size_t>((ci+1) * nSamp + s)]
                        : (nChan > 1
                            ? wavCM[static_cast<size_t>((ci-1) * nSamp + s)]
                            : 0);
                    sd = val - other;
                    break; }
                case 2: {  // SDIFF_LAPLACIAN
                    if (nChan == 1) { sd = val; break; }
                    if (ci == 0)
                        sd = val - wavCM[static_cast<size_t>(1 * nSamp + s)];
                    else if (ci == nChan - 1)
                        sd = val - wavCM[static_cast<size_t>((nChan-2) * nSamp + s)];
                    else {
                        // ½(prev+next) implemented in integer arithmetic with
                        // the same rounding behaviour as the canonical:
                        //   sd = round(val − 0.5*(prev + next))
                        //      = (2*val − prev − next + sign·1) / 2
                        // canonical uses double + std::round → tie-to-nearest-even
                        // is unreachable for int16 inputs; the (a+1)/2 trick
                        // matches the floor(.5+) behaviour for all values
                        // representable as int16.
                        const int prev = wavCM[static_cast<size_t>((ci-1) * nSamp + s)];
                        const int next = wavCM[static_cast<size_t>((ci+1) * nSamp + s)];
                        const int twoVal = 2 * val - prev - next;
                        sd = (twoVal >= 0)
                            ? (twoVal + 1) / 2
                            : -((-twoVal + 1) / 2);
                    }
                    break; }
                case 3:  // SDIFF_ALLPAIRS — fall through (default)
                default: {
                    int sum = 0;
                    for (int cj = 0; cj < nChan; ++cj)
                        sum += wavCM[static_cast<size_t>(cj * nSamp + s)];
                    sd = nChan * val - sum;
                    break; }
                }
                if (sd >  32767) { sd =  32767; ++spatialClampCount; }
                else if (sd < -32768) { sd = -32768; ++spatialClampCount; }
                out[static_cast<size_t>(s * nChan + ci)] = static_cast<int16_t>(sd);
            }
        }

        // patch89: initial prev[] for the temporal first-difference.
        //
        // The canonical extractor (fill_sdiff_buffer in
        // process_extractspikes_stderiv.cpp:181-194) computes the temporal
        // first-difference as:
        //
        //     diff[t] = sd[t] - sd[t-1]                      if t > 0
        //     diff[0] = sd[0] - g_prev_sdiff[ch]             if g_prev_sdiff non-empty
        //     diff[0] = sd[0] - 0                            otherwise (first chunk only)
        //
        // where g_prev_sdiff carries the previous chunk's last sample's
        // spatial derivative.  Across all but the very first chunk, sample 0
        // sees a non-zero prev.  For typical mid-recording spikes the prev
        // sample is baseline noise so sd[ws-1] is small but non-zero — and
        // critically, the .spkD content on disk was written using this
        // non-zero prev.  Sample-0 of every spike in the on-disk .spkD
        // therefore reflects (sd[ws] - sd[ws-1]), NOT just sd[ws].
        //
        // Pre-patch89 nudge code used prev=0 unconditionally, producing
        // sample-0 = sd[ws] for nudged spikes.  This differed from the
        // canonical content by exactly sd[ws-1] at sample 0.  The .pcaD
        // basis was trained on canonical content, so the offset projected
        // as a constant feature-space displacement, dispersing nudged
        // cluster spikes.  Visible symptom: cluster mean dispersion that
        // accumulates with every nudge.
        //
        // Fix: when prevRaw is provided (size == nChan), compute its
        // spatial derivative using the same kSdiffOrder logic as above and
        // use it as the initial prev[].  When prevRaw is empty (only at
        // startSample == 0, i.e. spike at the very start of the recording),
        // fall back to prev[]=0 to match the canonical extractor's
        // first-chunk startup behaviour.
        std::vector<int16_t> prev(static_cast<size_t>(nChan), 0);
        if (!prevRaw.empty() && static_cast<int>(prevRaw.size()) == nChan) {
            for (int ci = 0; ci < nChan; ++ci) {
                const int val = prevRaw[static_cast<size_t>(ci)];
                int sd;
                switch (kSdiffOrder) {
                case 0:  // SDIFF_NONE — pass-through
                    sd = val;
                    break;
                case 1: {  // SDIFF_FIRST
                    const int other = (ci < nChan - 1)
                        ? prevRaw[static_cast<size_t>(ci + 1)]
                        : (nChan > 1
                            ? prevRaw[static_cast<size_t>(ci - 1)]
                            : 0);
                    sd = val - other;
                    break; }
                case 2: {  // SDIFF_LAPLACIAN
                    if (nChan == 1) { sd = val; break; }
                    if (ci == 0)
                        sd = val - prevRaw[1];
                    else if (ci == nChan - 1)
                        sd = val - prevRaw[static_cast<size_t>(nChan - 2)];
                    else {
                        const int p = prevRaw[static_cast<size_t>(ci - 1)];
                        const int n = prevRaw[static_cast<size_t>(ci + 1)];
                        const int twoVal = 2 * val - p - n;
                        sd = (twoVal >= 0)
                            ? (twoVal + 1) / 2
                            : -((-twoVal + 1) / 2);
                    }
                    break; }
                case 3:  // SDIFF_ALLPAIRS (default)
                default: {
                    int sum = 0;
                    for (int cj = 0; cj < nChan; ++cj)
                        sum += prevRaw[static_cast<size_t>(cj)];
                    sd = nChan * val - sum;
                    break; }
                }
                if (sd >  32767) sd =  32767;
                else if (sd < -32768) sd = -32768;
                prev[static_cast<size_t>(ci)] = static_cast<int16_t>(sd);
            }
        }

        // Step 2: temporal first-difference in-place, using prev as the
        // baseline for output sample 0.
        for (int s = 0; s < nSamp; ++s) {
            int16_t* row = out.data() + s * nChan;
            for (int ci = 0; ci < nChan; ++ci) {
                const int16_t sd  = row[ci];
                int diff = static_cast<int>(sd)
                         - static_cast<int>(prev[static_cast<size_t>(ci)]);
                if (diff >  32767) { diff =  32767; ++temporalClampCount; }
                else if (diff < -32768) { diff = -32768; ++temporalClampCount; }
                if (diff == 0 && s > 0) ++temporalZeroCount;
                prev[static_cast<size_t>(ci)] = sd;  // save SD, not diff
                row[ci] = static_cast<int16_t>(diff);
            }
        }
    };

    // ── Feature projection helper ─────────────────────────────────────────
    // wav: channel-major [ch*nSamp+s] — raw OR stderiv depending on isStderiv.
    // prevRaw: kept on the signature for ABI compatibility; ignored.  The
    //          stderiv transform now uses sdiff[-1]=0 per spike, matching
    //          the canonical pipeline that built .pcaD.
    // Returns full feature row (nFeatCols int64_t + timestamp placeholder).
    auto makeFetRow = [&](int64_t ts,
                          const std::vector<int16_t>& wavRaw,
                          const std::vector<int16_t>& prevRaw,
                          dataType spikeRow) -> std::vector<int64_t>
    {
        std::vector<int64_t> row(static_cast<size_t>(timeDim), 0LL);
        if (!pca.valid()) return row;

        // For stderiv mode: apply spatial+temporal derivative to raw waveform.
        // The derivative is applied in sample-major [s*nChan+ci] space then
        // projected using channel-major indexing into the .pca.N eigenvectors.
        // We build a compact derivative buffer in sample-major layout.
        std::vector<double> xform;  // [s * pca.nCh + ch] after derivative
        if (isStderivFet) {
            // Apply the exact same transform as ndm_extractspikes_stderiv
            // (integer arithmetic, all nChan channels).
            // Then use first pca.nCh = nChan-1 channels for projection
            // (last channel is linearly dependent and excluded from PCA).
            std::vector<int16_t> sdWav;
            applyStderivTransform(wavRaw, prevRaw, sdWav);  // sample-major [s*nChan+ch]
            const int kCh = pca.nCh;  // = nChan - 1
            xform.resize(static_cast<size_t>(nSamp * kCh));
            for (int s = 0; s < nSamp; ++s)
                for (int ci = 0; ci < kCh; ++ci)
                    xform[static_cast<size_t>(s * kCh + ci)] =
                        static_cast<double>(sdWav[static_cast<size_t>(s*nChan+ci)]);
        }

        int outCol = 0;
        for (int ch = 0; ch < pca.nCh; ++ch) {
            const double* E    = pca.evec[static_cast<size_t>(ch)].data();
            const double* mean = pca.means[static_cast<size_t>(ch)].data();
            for (int c = 0; c < pca.nComp; ++c) {
                double dot = 0.0;
                for (int j2 = 0; j2 < pca.data2use; ++j2) {
                    double x;
                    if (isStderivFet) {
                        x = xform[static_cast<size_t>(
                            (pca.recShift + j2) * pca.nCh + ch)];
                    } else {
                        x = static_cast<double>(
                            wavRaw[static_cast<size_t>(
                                ch * nSamp + pca.recShift + j2)]);
                    }
                    if (pca.centered) x -= mean[j2];
                    dot += E[j2 + c * pca.data2use] * x;
                }
                row[static_cast<size_t>(outCol++)] = std::llround(dot);
            }
        }

        // Extra feature columns (non-PCA, e.g. peak amplitude): preserve
        // the existing in-memory values rather than writing zeros.
        const int nPcaFeats = pca.nCh * pca.nComp;
        for (int col = nPcaFeats; col < nFeatCols; ++col)
            row[static_cast<size_t>(col)] =
                static_cast<int64_t>(clusteringData->featureValue(
                    spikeRow, static_cast<int>(col + 1)));

        row[static_cast<size_t>(nFeatCols)] = ts;
        return row;
    };

    // ── Main loop ─────────────────────────────────────────────────────────
    SortableTable spkTable;
    if (!clusteringData->spikePositions(clusterId, spkTable)) {
        fclose(spkW); fclose(resW); fclose(fetW);
        if (filF) fclose(filF);
        return false;
    }
    const int64_t N = static_cast<int64_t>(spkTable.nbOfColumns());

    // ── Pre-nudge invariant self-check ───────────────────────────────────
    // Klusters' nudge — and any other tool that re-reads .fil at .res
    // offsets — assumes:
    //
    //     .spk[i] peak  ≡  .fil at file-sample .res[i]
    //
    // This holds after canonical extractspikes (writes refined-peak
    // position to .res) and after the patched alignspikes (updates .res
    // to extTs = resTs + shifts).  An unpatched alignspikes leaves .res
    // pointing at the original detection-threshold position while
    // re-extracting .spk centred on the refined peak — every nudge then
    // lands at a window mispositioned by `shifts[i]` samples relative
    // to where the peak actually is in .fil.
    //
    // Detect this by reading the on-disk .spk for a small sample of
    // spikes (first, middle, last in the cluster), finding the
    // sum-|stderiv| peak position within the spike window, and comparing
    // to peakSamp0.  A consistent offset across the sample indicates the
    // invariant is broken; warn loudly so the user can re-run the
    // patched pipeline rather than producing more corrupted data.
    {
        const QString readPath = QFileInfo(pendingSpkPath).exists()
            ? pendingSpkPath : origSpkPath;
        FILE* fr = fopen(readPath.toLocal8Bit().constData(), "rb");
        if (fr && N > 0) {
            const std::vector<int64_t> probeIdx = {
                0,
                N / 2,
                N - 1,
            };
            std::vector<int> argmaxOffsets;
            argmaxOffsets.reserve(probeIdx.size());
            std::vector<int16_t> buf(static_cast<size_t>(nChan * nSamp));
            for (int64_t pIdx : probeIdx) {
                if (pIdx < 0 || pIdx >= N) continue;
                const dataType row = static_cast<dataType>(
                    spkTable(1, static_cast<dataType>(pIdx + 1)));
                const int64_t pos0 = static_cast<int64_t>(row) - 1;
                if (fseeko(fr, pos0 * static_cast<off_t>(bytesPerSpike),
                           SEEK_SET) != 0) continue;
                if (fread(buf.data(), 2, buf.size(), fr) != buf.size())
                    continue;
                // Sum |.spk| across channels per sample; in stderiv mode
                // this peaks at the refined-peak position; in raw mode it
                // peaks at the same place modulo sign (we use abs so sign
                // doesn't matter).  Layout depends on isStderivSpk: stderiv
                // is sample-major [s*nChan+c], raw is channel-major.
                int bestS = 0;
                int64_t bestE = -1;
                for (int s = 0; s < nSamp; ++s) {
                    int64_t e = 0;
                    for (int ci = 0; ci < nChan; ++ci) {
                        const size_t k = isStderivSpk
                            ? static_cast<size_t>(s * nChan + ci)
                            : static_cast<size_t>(ci * nSamp + s);
                        e += std::abs(static_cast<int>(buf[k]));
                    }
                    if (e > bestE) { bestE = e; bestS = s; }
                }
                argmaxOffsets.push_back(bestS - peakSamp0);
            }
            fclose(fr);
            // Median absolute offset across the probes.  If the median is
            // > 1 sample, the .spk peak is consistently NOT at peakSamp0
            // — strongly suggests the .res/.spk invariant is broken.
            if (argmaxOffsets.size() >= 2) {
                std::vector<int> abs_off;
                abs_off.reserve(argmaxOffsets.size());
                for (int o : argmaxOffsets) abs_off.push_back(std::abs(o));
                std::sort(abs_off.begin(), abs_off.end());
                const int median = abs_off[abs_off.size() / 2];
                if (median > 1) {
                    // The cluster's .spk peaks don't sit at peakSamp0.
                    // This is NOT something nudge introduces — it's a
                    // pre-existing condition (most often a stale .par.N
                    // peakSampleIndex, or .spk written by an old
                    // ndm_alignspikes that didn't update .res).
                    //
                    // Nudge is a rigid time translation: it shifts every
                    // .res[i] by delta and re-reads the .fil window at
                    // (newRes - peakSamp0).  Whatever within-window
                    // alignment existed before the nudge is preserved
                    // afterward — the median offset will still be
                    // `median` samples after this operation.
                    //
                    // We used to refuse here, but that conflates two
                    // independent concerns: window-internal peak position
                    // (what `median` measures) and timestamp accuracy
                    // (what nudge corrects).  The user's nudge intent
                    // doesn't depend on the former, so we proceed and
                    // just inform them.
                    QString offTxt;
                    for (int o : argmaxOffsets)
                        offTxt += QString::number(o) + ' ';
                    qInfo().noquote() << QString(
                        "[nudge] cluster %1: existing alignment off by %2 "
                        "samples (peakSamp0=%3, per-probe offsets: %4); "
                        "nudge will preserve the offset.")
                        .arg(clusterId).arg(median).arg(peakSamp0)
                        .arg(offTxt.trimmed());
                    if (auto* sb = app() ? app()->statusBar() : nullptr)
                        sb->showMessage(QString(
                            "Cluster %1: existing alignment off by %2 "
                            "samples; nudge will preserve the offset.")
                            .arg(clusterId).arg(median), 5000);
                    // Fall through — proceed with the nudge.
                }
            }
        } else if (fr) {
            fclose(fr);
        }
    }

    // Defensive accounting — surface I/O failures rather than silently
    // writing at stale positions.  These should normally remain zero.
    int64_t spkSeekFail   = 0;
    int64_t resSeekFail   = 0;
    int64_t fetSeekFail   = 0;
    int64_t shortSpkWrite = 0;
    int64_t shortResWrite = 0;
    int64_t shortFetWrite = 0;
    int64_t boundarySkip  = 0;  // extractWaveform returned false
    int64_t fetReadback   = 0;  // # spikes whose .fet readback differed from intended
    int64_t spkReadback   = 0;  // # spikes whose .spk readback differed from intended

    // Per-spike trace mode: when NS3_VERBOSE is set, dump detailed numbers for
    // the first nudged spike so the user can verify that:
    //   (a) extractWaveform is reading exactly nSamp consecutive samples from
    //       the right offset (no half-window mismatch),
    //   (b) applyStderivTransform produces values that match what
    //       process_extractspikes_stderiv would on the same raw window,
    //   (c) the on-disk .fet bytes after our write match the values we asked
    //       to write (ruling out concurrent thread interference),
    //   (d) the feature delta is in line with what process_refeaturize_stderiv
    //       would compute for a +1-sample shift of the same spike.
    // The reference computation can be reproduced offline by running:
    //   process_refeaturize_stderiv -p session.pcaD.G -w nSamp -n (nChan-1) \
    //                                -i one-spike-index session.spkD.G
    // on the .spkD slice corresponding to the nudged spike.
    const bool traceFirst = qEnvironmentVariableIsSet("NS3_VERBOSE");

    // ── Mean-waveform dump (gated by NUDGE_DUMP_MEAN env var) ─────────────
    // When enabled, walk every spike in the cluster twice — once before the
    // main rewrite loop and once after — accumulating the mean waveform
    // straight from the bytes on disk in .spk.pending.  Both means are
    // written to a single text file in the session directory so the user
    // can upload it and we can plot before/after side by side.  This is
    // the most direct test for "nudge moves the post-peak portion of the
    // waveform by more than the pre-peak portion": if true, the after-mean
    // will show a non-uniform horizontal shift relative to the before-mean.
    // Reads use a separate read-only FILE* so we don't perturb the spkW
    // cursor that the main loop relies on.
    const bool dumpMean = qEnvironmentVariableIsSet("NUDGE_DUMP_MEAN");
    auto computeMean = [&](std::vector<double>& meanCM /* [ch*nSamp+s] */)
                          -> int64_t {
        meanCM.assign(static_cast<size_t>(nChan * nSamp), 0.0);
        FILE* fr = fopen(pendingSpkPath.toLocal8Bit().constData(), "rb");
        if (!fr) return 0;
        std::vector<int16_t> buf(static_cast<size_t>(nChan * nSamp));
        int64_t nRead = 0;
        for (int64_t i = 0; i < N; ++i) {
            const dataType row = static_cast<dataType>(
                spkTable(1, static_cast<dataType>(i + 1)));
            const int64_t pos0 = static_cast<int64_t>(row) - 1;
            if (fseeko(fr, pos0 * static_cast<off_t>(bytesPerSpike),
                       SEEK_SET) != 0) continue;
            if (fread(buf.data(), 2, buf.size(), fr) != buf.size()) continue;
            // .spk layout is sample-major [s*nChan+ci]; accumulate to
            // channel-major mean buffer [ci*nSamp+s] so the file dump
            // reads naturally one channel at a time.
            for (int s = 0; s < nSamp; ++s)
                for (int ci = 0; ci < nChan; ++ci)
                    meanCM[static_cast<size_t>(ci * nSamp + s)] +=
                        buf[static_cast<size_t>(s * nChan + ci)];
            ++nRead;
        }
        fclose(fr);
        if (nRead > 0)
            for (auto& v : meanCM)
                v /= static_cast<double>(nRead);
        return nRead;
    };

    std::vector<double> meanBefore;
    int64_t nReadBefore = 0;
    if (dumpMean) nReadBefore = computeMean(meanBefore);

    for (int64_t i = 0; i < N; ++i) {
        const dataType row  = static_cast<dataType>(
            spkTable(1, static_cast<dataType>(i + 1)));
        const int64_t  pos0 = static_cast<int64_t>(row) - 1;

        // Read the authoritative old timestamp from .res.pending rather than
        // the .fetD copy in the features array.  The .fetD timestamp column may
        // have been zeroed by an earlier buggy build (gotWav=false wrote zeros);
        // .res.pending is always written atomically per spike and is safe to use.
        int64_t oldTs64 = 0;
        if (fseeko(resW, static_cast<off_t>(pos0) * static_cast<off_t>(sizeof(int64_t)),
                   SEEK_SET) != 0) {
            ++resSeekFail;
            oldTs64 = static_cast<int64_t>(clusteringData->featureValue(row, timeDim));
        } else if (fread(&oldTs64, sizeof(int64_t), 1, resW) != 1) {
            oldTs64 = static_cast<int64_t>(clusteringData->featureValue(row, timeDim));
        }
        const dataType oldTs = static_cast<dataType>(oldTs64);
        dataType newTs = oldTs + static_cast<dataType>(deltaSamples);
        if (newTs < 0) newTs = 0;
        if (maxValidTs > 0 && newTs > static_cast<dataType>(maxValidTs))
            newTs = static_cast<dataType>(maxValidTs);

        const int64_t ts64 = static_cast<int64_t>(newTs);

        // Capture the in-memory feature row BEFORE the write, so we can show
        // the user the feature delta and compare against an offline reference.
        std::vector<int64_t> oldFetRow;
        if (traceFirst && i == 0 && pca.valid()) {
            oldFetRow.reserve(static_cast<size_t>(nFeatCols));
            for (int col = 0; col < nFeatCols; ++col)
                oldFetRow.push_back(static_cast<int64_t>(
                    clusteringData->featureValue(row, col + 1)));
        }

        // Re-extract waveform at new timestamp.  patch89: prevSample is
        // populated with the raw sample at (startSample - 1) when one is
        // available, so applyStderivTransform's sample-0 temporal-diff uses
        // the correct prev_sdiff matching the canonical extractor's
        // g_prev_sdiff behaviour.  prevSample is empty only when the spike
        // sits at the very start of the recording (startSample == 0).
        std::vector<int16_t> wav;
        std::vector<int16_t> prevSample;
        const bool gotWav = extractWaveform(ts64, wav, prevSample);
        if (!gotWav) ++boundarySkip;

        // Write .spk at new position.  Use spkIsTransformed (via isStderivSpk),
        // not the feature-space flag: .spk format is independent of .fet format.
        std::vector<int16_t> spkRowWritten;  // kept for trace + readback
        if (gotWav) {
            if (fseeko(spkW, static_cast<off_t>(pos0) * static_cast<off_t>(bytesPerSpike),
                       SEEK_SET) != 0) {
                ++spkSeekFail;
            } else if (isStderivSpk) {
                // stderiv pipeline: .spk stores transformed waveform
                // (all nChan channels, sample-major, matching ndm_extractspikes_stderiv)
                applyStderivTransform(wav, prevSample, spkRowWritten);
                const size_t want = static_cast<size_t>(nChan * nSamp);
                if (fwrite(spkRowWritten.data(), 2, want, spkW) != want) ++shortSpkWrite;
            } else {
                // raw pipeline: convert channel-major → sample-major
                spkRowWritten.assign(static_cast<size_t>(nChan * nSamp), 0);
                for (int s = 0; s < nSamp; ++s)
                    for (int ch = 0; ch < nChan; ++ch)
                        spkRowWritten[static_cast<size_t>(s * nChan + ch)] =
                            wav[static_cast<size_t>(ch * nSamp + s)];
                const size_t want = static_cast<size_t>(nChan * nSamp);
                if (fwrite(spkRowWritten.data(), 2, want, spkW) != want) ++shortSpkWrite;
            }
        }

        // Write .res — but ONLY when the new waveform was successfully
        // extracted from .fil.  When the new window crosses a recording
        // boundary, extractWaveform returns false and .spk / .fet writes
        // are skipped below, leaving stale waveform/feature content on
        // disk.  If we still updated .res in that case, the spike's .res
        // timestamp would no longer match the .spk window position — the
        // .res/.spk invariant the entire pipeline depends on (.spk[i] peak
        // ≡ .fil at .res[i]) breaks for the affected spikes.  Visible
        // symptom: a small fraction of cluster spikes show waveforms
        // shifted by deltaSamples relative to where their .res says.
        // Boundary spikes therefore stay at their original timestamp —
        // counted via boundarySkip and reported in the summary so the
        // user can see how many spikes were left behind.
        if (gotWav) {
            if (fseeko(resW, static_cast<off_t>(pos0) * static_cast<off_t>(sizeof(int64_t)),
                       SEEK_SET) != 0) {
                ++resSeekFail;
            } else if (fwrite(&ts64, sizeof(int64_t), 1, resW) != 1) {
                ++shortResWrite;
            }
        }

        // Reproject and write .fet — only when waveform was successfully read.
        // When gotWav=false (spike at recording boundary or .fil unreadable),
        // preserve the existing feature values rather than zeroing them out.
        std::vector<int64_t> fetRowWritten;
        if (gotWav) {
            fetRowWritten = makeFetRow(ts64, wav, prevSample, row);
            if (!fetRowWritten.empty()) {
                // Update on-disk .fetD
                const off_t fetOff = static_cast<off_t>(sizeof(int32_t))
                    + static_cast<off_t>(pos0) * static_cast<off_t>(timeDim)
                      * static_cast<off_t>(sizeof(int64_t));
                if (fseeko(fetW, fetOff, SEEK_SET) != 0) {
                    ++fetSeekFail;
                } else {
                    const size_t want = static_cast<size_t>(timeDim);
                    if (fwrite(fetRowWritten.data(), sizeof(int64_t), want, fetW) != want)
                        ++shortFetWrite;
                    fflush(fetW);
                }

                // Read-back verification: confirm the bytes on disk now match
                // what we asked to write.  A mismatch would indicate either a
                // concurrent writer (no other code path should be touching
                // .fetD.pending while nudge holds spkW/resW/fetW open) or a
                // filesystem-level reorder; either is a bug we want to catch.
                if (fseeko(fetW, fetOff, SEEK_SET) == 0) {
                    std::vector<int64_t> rb(static_cast<size_t>(timeDim), 0LL);
                    const size_t got = fread(rb.data(), sizeof(int64_t),
                                             static_cast<size_t>(timeDim), fetW);
                    if (got == static_cast<size_t>(timeDim)) {
                        for (int col = 0; col < timeDim; ++col) {
                            if (rb[static_cast<size_t>(col)]
                                != fetRowWritten[static_cast<size_t>(col)]) {
                                ++fetReadback;
                                break;
                            }
                        }
                    }
                }

                // Update in-memory feature table (PCA dims only).
                // updateFeatureRow explicitly excludes the timestamp
                // column (see Data::updateFeatureRow), so we have to
                // call updateTimestamp separately — otherwise the
                // in-memory `.fet` time column stays at oldTs while the
                // on-disk one has been advanced to newTs.  Anything that
                // reads the in-memory feature table (autocorrelogram,
                // refractory-violation panel, error matrix) would
                // continue to show the cluster at its pre-nudge
                // timestamps until the session is closed and reopened.
                if (pca.valid()) {
                    QList<dataType> vals;
                    vals.reserve(nFeatCols);
                    for (int col = 0; col < nFeatCols; ++col)
                        vals.append(static_cast<dataType>(
                            fetRowWritten[static_cast<size_t>(col)]));
                    clusteringData->updateFeatureRow(row, vals);
                    clusteringData->updateTimestamp(row, newTs);
                }
            }
        }

        // .spk read-back verification, mirroring the .fet check above.
        if (gotWav && !spkRowWritten.empty()) {
            if (fseeko(spkW, static_cast<off_t>(pos0)
                       * static_cast<off_t>(bytesPerSpike), SEEK_SET) == 0) {
                fflush(spkW);
                std::vector<int16_t> rbSpk(spkRowWritten.size(), 0);
                const size_t got = fread(rbSpk.data(), 2,
                                         spkRowWritten.size(), spkW);
                if (got == spkRowWritten.size()) {
                    for (size_t k = 0; k < spkRowWritten.size(); ++k) {
                        if (rbSpk[k] != spkRowWritten[k]) {
                            ++spkReadback;
                            break;
                        }
                    }
                }
            }
        }

        // Per-spike trace for the first nudged spike.
        if (traceFirst && i == 0 && gotWav) {
            // Find detection channel (largest peak-to-peak in the raw window).
            int detCh = 0;
            int bestPp = -1;
            for (int ci = 0; ci < nChan; ++ci) {
                int16_t mn = INT16_MAX, mx = INT16_MIN;
                for (int s = 0; s < nSamp; ++s) {
                    const int16_t v = wav[static_cast<size_t>(ci * nSamp + s)];
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
                const int pp = static_cast<int>(mx) - static_cast<int>(mn);
                if (pp > bestPp) { bestPp = pp; detCh = ci; }
            }

            QString rawTxt;
            for (int s = 0; s < std::min(nSamp, 12); ++s)
                rawTxt += QString::number(
                    wav[static_cast<size_t>(detCh * nSamp + s)]) + ' ';
            qDebug().noquote() << QString(
                "[nudge-trace] cluster=%1 spike=%2 (file pos %3) "
                "oldTs=%4 newTs=%5 delta=%6 peakSamp0=%7 startSample=%8 detCh=%9")
                .arg(clusterId).arg(i).arg(pos0)
                .arg(static_cast<long long>(oldTs))
                .arg(static_cast<long long>(newTs))
                .arg(deltaSamples)
                .arg(peakSamp0)
                .arg(static_cast<long long>(ts64 - peakSamp0))
                .arg(detCh);
            qDebug().noquote() << QString(
                "[nudge-trace]  raw[detCh,0..11]: %1").arg(rawTxt);

            if (isStderivSpk && spkRowWritten.size()
                >= static_cast<size_t>(nChan * std::min(nSamp, 12))) {
                QString sdTxt;
                for (int s = 0; s < std::min(nSamp, 12); ++s)
                    sdTxt += QString::number(
                        spkRowWritten[static_cast<size_t>(s * nChan + detCh)]) + ' ';
                qDebug().noquote() << QString(
                    "[nudge-trace]  sdWritten[detCh,0..11]: %1").arg(sdTxt);
            }

            if (!fetRowWritten.empty()) {
                const int nShow = std::min(nFeatCols, 8);
                QString oldF, newF, dF;
                for (int c = 0; c < nShow; ++c) {
                    const int64_t o = (c < static_cast<int>(oldFetRow.size()))
                        ? oldFetRow[static_cast<size_t>(c)] : 0;
                    const int64_t n = fetRowWritten[static_cast<size_t>(c)];
                    oldF += QString::number(static_cast<long long>(o)) + ' ';
                    newF += QString::number(static_cast<long long>(n)) + ' ';
                    dF   += QString::number(static_cast<long long>(n - o)) + ' ';
                }
                qDebug().noquote() << QString(
                    "[nudge-trace]  oldFet[0..%1]: %2").arg(nShow-1).arg(oldF);
                qDebug().noquote() << QString(
                    "[nudge-trace]  newFet[0..%1]: %2").arg(nShow-1).arg(newF);
                qDebug().noquote() << QString(
                    "[nudge-trace]  deltaFet[0..%1]: %2").arg(nShow-1).arg(dF);
            }
        }
    }

    fclose(spkW); fclose(resW); fclose(fetW);
    if (filF) fclose(filF);

    // ── Mean-waveform dump: AFTER pass + write file ────────────────────────
    if (dumpMean) {
        std::vector<double> meanAfter;
        const int64_t nReadAfter = computeMean(meanAfter);

        // Write next to the .spk.pending file with a unique timestamp so
        // repeated nudges produce distinct dump files.
        QFileInfo spkInfo(pendingSpkPath);
        const QString outName = QString("nudge-meanwave-c%1-d%2-%3.txt")
            .arg(clusterId).arg(deltaSamples)
            .arg(QDateTime::currentSecsSinceEpoch());
        const QString outPath = spkInfo.dir().absoluteFilePath(outName);

        FILE* fw = fopen(outPath.toLocal8Bit().constData(), "w");
        if (fw) {
            fprintf(fw, "# nudge mean-waveform dump\n");
            fprintf(fw, "# clusterId=%d delta=%d nChan=%d nSamp=%d "
                        "peakSamp0=%d isStderivSpk=%d isStderivFet=%d "
                        "nSpikes=%lld nReadBefore=%lld nReadAfter=%lld\n",
                    clusterId, deltaSamples, nChan, nSamp,
                    peakSamp0,
                    isStderivSpk ? 1 : 0,
                    isStderivFet ? 1 : 0,
                    static_cast<long long>(N),
                    static_cast<long long>(nReadBefore),
                    static_cast<long long>(nReadAfter));
            // Column layout chosen to be trivial to load with numpy:
            //   data = np.loadtxt(path, skiprows=3)   # 4 cols: ch, s, b, a
            fprintf(fw, "ch s before after\n");
            const size_t nb = meanBefore.size();
            const size_t na = meanAfter.size();
            for (int ci = 0; ci < nChan; ++ci) {
                for (int s = 0; s < nSamp; ++s) {
                    const size_t k = static_cast<size_t>(ci * nSamp + s);
                    const double b = (k < nb) ? meanBefore[k] : 0.0;
                    const double a = (k < na) ? meanAfter[k]  : 0.0;
                    fprintf(fw, "%d %d %.6f %.6f\n", ci, s, b, a);
                }
            }
            fclose(fw);
            qDebug().noquote() << QString("[nudge-meanwave] wrote %1 "
                "(N=%2 nReadBefore=%3 nReadAfter=%4)")
                .arg(outPath)
                .arg(N).arg(nReadBefore).arg(nReadAfter);
        } else {
            qWarning().noquote() << QString(
                "[nudge-meanwave] cannot open %1 for writing").arg(outPath);
        }
    }
    // Diagnostic summary.  All "fail" / "shortWrite" / "Readback" counters
    // should be zero under normal operation; non-zero indicates filesystem
    // trouble or a concurrent writer.  The saturation counters reflect the
    // int16 .spk format limit and are expected to be non-zero on clusters
    // containing very-large-amplitude spikes — this is a property of the
    // canonical pipeline (see the boundary-condition comment above), not
    // nudge corruption.
    NS3_DIAG() << "[nudge] cluster=" << clusterId
               << " delta=" << deltaSamples
               << " N=" << N
               << " boundarySkip=" << boundarySkip
               << " spatialClamp=" << spatialClampCount
               << " temporalClamp=" << temporalClampCount
               << " temporalZero=" << temporalZeroCount
               << " (of " << (N * static_cast<int64_t>(nSamp - 1) * nChan)
               << " interior temporal-diff samples)"
               << " fetReadback=" << fetReadback
               << " spkReadback=" << spkReadback;
    if (spkSeekFail || resSeekFail || fetSeekFail
        || shortSpkWrite || shortResWrite || shortFetWrite
        || fetReadback || spkReadback) {
        qWarning() << "[nudge] I/O anomalies cluster=" << clusterId
                   << " spkSeekFail=" << spkSeekFail
                   << " resSeekFail=" << resSeekFail
                   << " fetSeekFail=" << fetSeekFail
                   << " shortSpkWrite=" << shortSpkWrite
                   << " shortResWrite=" << shortResWrite
                   << " shortFetWrite=" << shortFetWrite
                   << " fetReadback=" << fetReadback
                   << " spkReadback=" << spkReadback;
    }

    setModified(true);

    // Surface boundary-skip count to the user.  After the bug-B fix,
    // spikes whose new window crosses a recording boundary stay at their
    // original timestamp rather than being silently desynchronised.  If
    // any spikes were left behind, tell the user via the status bar so
    // they know the cluster wasn't fully translated.
    if (boundarySkip > 0) {
        QString msg = QString(
            "Nudge: cluster %1 — %2 of %3 spike(s) near recording boundary "
            "kept at original timestamp (new window outside .fil range).")
            .arg(clusterId)
            .arg(static_cast<long long>(boundarySkip))
            .arg(static_cast<long long>(N));
        qInfo().noquote() << "[nudge]" << msg;
        if (auto* sb = app() ? app()->statusBar() : nullptr)
            sb->showMessage(msg, 8000);
    }

    // Recompute per-dimension min/max so the scatter view world-window
    // axes are correct after feature values have been updated.
    clusteringData->minMaxDimensionCalculation(QList<int>{clusterId});

    // Invalidate caches so the next thread launch re-reads from disk/memory.
    invalidateWaveformCache(clusterId);
    invalidateCorrelogramCache(clusterId);

    // Force every ClusterView to recalculate its world window.
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        v->updateDimensions(v->abscissaDimension(), v->ordinateDimension());
    }

    // For each view showing this cluster: set REDRAW mode on the scatter
    // (ClusterView) to erase ghost points, then relaunch the WaveformThread
    // and CorrelogramThread against the updated data.
    for (int i = 0; i < viewList->count(); ++i)
        viewList->at(i)->invalidateClusterDisplay(clusterId);

    // Refresh the trace view (if any) so its cluster timestamp markers
    // shift to the new positions on .fil playback.  Without this, the
    // trace would only repaint when something else triggered a redraw
    // — e.g. an open errormatrix's update — making nudge appear to
    // "only work when errormatrix is available".
    KlustersView* activeView =
        app()->activeView();
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        v->updateTraceView(electrodeGroupID, clusterColorList,
                           v == activeView);
    }

    logAfter(QList<int>{ clusterId });

    return true;
}

// ============================================================================
// Curation logger helpers
// ============================================================================

void KlustersDoc::logBefore(CurationLogger::ActionType action,
                             const QList<int>& clusterIds)
{
    if (!curationLogger || !curationLogger->isOpen() || clusterIds.isEmpty())
        return;

    QList<ClusterSnapshot> snaps = snapshotClusters(clusterIds);

    // Stamp each snapshot with this cluster's prior action count, then increment.
    for (ClusterSnapshot& s : snaps) {
        s.actionHistoryDepth = clusterActionCount.value(s.clusterId, 0);
        clusterActionCount[s.clusterId]++;
    }

    lastLoggedActionIdx = curationLogger->beginAction(action, snaps);
}

void KlustersDoc::logAfter(const QList<int>& clusterIds)
{
    if (!curationLogger || !curationLogger->isOpen() || clusterIds.isEmpty())
        return;

    QList<ClusterSnapshot> snaps = snapshotClusters(clusterIds);
    // Preserve action_history_depth for result clusters (they were just created
    // or modified, so their count is the depth inherited from the action).
    for (ClusterSnapshot& s : snaps)
        s.actionHistoryDepth = clusterActionCount.value(s.clusterId, 0);

    curationLogger->commitAction(snaps);
}

void KlustersDoc::beginRealignBatchLog(const QList<int>& /*clusterIds*/)
{
    // Enable the batch-scoped centroid cache.  Per-cluster realign logging stays
    // ON; each cluster's logBefore/logAfter reuses one computeAllCentroids()
    // pass (populated lazily on the first snapshot, inside the realign worker)
    // instead of recomputing the full-dataset centroids twice per cluster.
    m_centroidCache.clear();
    m_centroidCacheValid   = false;
    m_centroidCacheEnabled = true;
}

void KlustersDoc::endRealignBatchLog()
{
    // Tear down the batch cache so subsequent snapshots are exact again.
    m_centroidCacheEnabled = false;
    m_centroidCacheValid   = false;
    m_centroidCache.clear();
}

// ---------------------------------------------------------------------------
// KlustersDoc::dipSplitDecide
//
// Pure decision function: tests a cluster for hidden bimodality and returns
// a structured decision (accept/reject + metrics + per-spike labels).  No
// side effects on KlustersDoc state — safe to call repeatedly, safe to use
// from a "preview before commit" UI, safe to unit-test in isolation.
//
// Algorithm (ported from KiloKlustaKwik Phase 8):
//   1. Collect cluster members (nD-dim feature vectors, time dim excluded)
//   2. Gate A (bloat):   fit μ + Σ, Cholesky, compute Mahalanobis² per spike,
//                         reject if mahal²₉₀ < bloatFactor · χ²(d, 0.9)
//                         (skipped when bloatFactor == 0)
//   3. Top-3 PCs:        power iteration with deflation on the cluster
//   4. Gate B (valley):  project onto each PC, KDE valley test
//   5. Seed:             k=2 partition at valley location
//   6. Refine:           k-means on full nD features
//   7. BIC gate:         accept only if BIC(k=2) < BIC(k=1)
// ---------------------------------------------------------------------------
KlustersDoc::DipSplitDecision
KlustersDoc::dipSplitDecide(int   clusterId,
                             int   minSize,
                             float bloatFactor,
                             float valleyThresh)
{
    DipSplitDecision D;
    D.clusterId = clusterId;
    D.reason    = QStringLiteral("skip");

    // Validate cluster exists and fetch its spikes.
    SortableTable spkTable;
    if (!clusteringData->spikePositions(clusterId, spkTable)) {
        D.reason = QStringLiteral("cluster_not_found");
        return D;
    }
    const int M = static_cast<int>(spkTable.nbOfColumns());
    if (M < minSize * 2) {
        D.reason = QStringLiteral("too_small");
        return D;
    }

    // Feature dimensions, excluding the timestamp column.
    const Data& d = data();
    const int nDtot = d.nbOfDimensionsTotal();
    const int dPCA  = nDtot - 1;
    if (dPCA < 2) {
        D.reason = QStringLiteral("bad_features");
        return D;
    }

    // Build feature matrix X [M × dPCA], row-major.  featureValue() is
    // 1-based in both spike index and dimension index.  We also remember the
    // 1-based feature row for each member so the caller can map labels back
    // to spike file rows for the data move.
    std::vector<float>      X(static_cast<size_t>(M) * dPCA);
    QList<dataType>         rowsByMember;
    rowsByMember.reserve(M);
    for (int i = 0; i < M; ++i) {
        const dataType row1 = spkTable(1, static_cast<dataType>(i + 1));
        rowsByMember.append(row1);
        for (int j = 0; j < dPCA; ++j)
            X[static_cast<size_t>(i) * dPCA + j] =
                static_cast<float>(d.featureValue(row1, j + 1));
    }

    // -------------------------------------------------------------------
    // Gate A — bloat test
    // Fit single Gaussian (μ, Σ), compute Mahalanobis² for each member,
    // compare 90th percentile to χ²(d, 0.9) · bloatFactor.
    // -------------------------------------------------------------------
    std::vector<double> mu(dPCA, 0.0);
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < dPCA; ++j)
            mu[j] += X[static_cast<size_t>(i) * dPCA + j];
    for (int j = 0; j < dPCA; ++j) mu[j] /= M;

    // Covariance (full, row-major); small-sample ridge on diagonal.
    std::vector<double> cov(static_cast<size_t>(dPCA) * dPCA, 0.0);
    for (int i = 0; i < M; ++i) {
        const float* row = X.data() + static_cast<size_t>(i) * dPCA;
        for (int r = 0; r < dPCA; ++r) {
            const double dr = row[r] - mu[r];
            for (int c = r; c < dPCA; ++c) {
                const double dc = row[c] - mu[c];
                cov[static_cast<size_t>(r * dPCA + c)] += dr * dc;
            }
        }
    }
    const double invN = 1.0 / std::max(1, M - 1);
    double diagMean = 0.0;
    for (int r = 0; r < dPCA; ++r) {
        for (int c = r; c < dPCA; ++c)
            cov[static_cast<size_t>(r * dPCA + c)] *= invN;
        diagMean += cov[static_cast<size_t>(r * dPCA + r)];
    }
    diagMean /= dPCA;
    // Mirror upper→lower and add small ridge for numerical stability
    const double ridge = 1e-6 * diagMean;
    for (int r = 0; r < dPCA; ++r) {
        cov[static_cast<size_t>(r * dPCA + r)] += ridge;
        for (int c = r + 1; c < dPCA; ++c)
            cov[static_cast<size_t>(c * dPCA + r)] =
                cov[static_cast<size_t>(r * dPCA + c)];
    }

    // In-place Cholesky: L such that L·Lᵀ = Σ, stored in lower triangle.
    std::vector<double> L(static_cast<size_t>(dPCA) * dPCA, 0.0);
    bool cholOK = true;
    for (int r = 0; r < dPCA && cholOK; ++r) {
        for (int c = 0; c <= r && cholOK; ++c) {
            double s = cov[static_cast<size_t>(r * dPCA + c)];
            for (int k = 0; k < c; ++k)
                s -= L[static_cast<size_t>(r * dPCA + k)]
                   * L[static_cast<size_t>(c * dPCA + k)];
            if (r == c) {
                if (s <= 0.0) { cholOK = false; break; }
                L[static_cast<size_t>(r * dPCA + r)] = std::sqrt(s);
            } else {
                L[static_cast<size_t>(r * dPCA + c)] =
                    s / L[static_cast<size_t>(c * dPCA + c)];
            }
        }
    }
    if (!cholOK) {
        D.reason = QStringLiteral("bad_features");
        return D;
    }

    // Mahalanobis² per spike via forward substitution L·y = (x-μ), m² = |y|².
    std::vector<double> mahal2(static_cast<size_t>(M));
    std::vector<double> yvec(static_cast<size_t>(dPCA));
    for (int i = 0; i < M; ++i) {
        const float* row = X.data() + static_cast<size_t>(i) * dPCA;
        double m2 = 0.0;
        for (int r = 0; r < dPCA; ++r) {
            double s = row[r] - mu[r];
            for (int k = 0; k < r; ++k)
                s -= L[static_cast<size_t>(r * dPCA + k)] * yvec[k];
            const double diag = L[static_cast<size_t>(r * dPCA + r)];
            const double y = s / diag;
            yvec[r] = y;
            m2 += y * y;
        }
        mahal2[static_cast<size_t>(i)] = m2;
    }

    // 90th-percentile of Mahalanobis² distribution.
    {
        std::vector<double> sorted = mahal2;
        std::sort(sorted.begin(), sorted.end());
        const double idx = 0.90 * (sorted.size() - 1);
        const size_t lo  = static_cast<size_t>(std::floor(idx));
        const size_t hi  = std::min(sorted.size() - 1, lo + 1);
        const double frac = idx - lo;
        D.mahal2P90 = sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }
    // χ²(d, 0.9) via Wilson-Hilferty (z_0.9 = 1.2816).
    {
        const double dD = static_cast<double>(dPCA);
        const double a  = 1.0 - 2.0 / (9.0 * dD);
        const double b  = 1.2816 * std::sqrt(2.0 / (9.0 * dD));
        D.chi2_90 = dD * std::pow(a + b, 3.0);
    }
    // bloatFactor == 0 disables the gate.  This is the recommended setting
    // for interactive Klusters use: the user is already targeting a cluster
    // they suspect is bimodal, and a fresh-fit Gaussian (no EM context) makes
    // the gate unreliable for small or low-dimensional clusters.  Non-zero
    // values enforce the gate as in KiloKlustaKwik Phase 8.
    if (bloatFactor > 0.0f &&
        D.mahal2P90 < bloatFactor * D.chi2_90) {
        D.reason = QStringLiteral("not_bloated");
        return D;
    }

    // -------------------------------------------------------------------
    // Top-3 PCs via power iteration (uses cluster centroid already in mu).
    // -------------------------------------------------------------------
    constexpr int kPCA = 3;
    std::vector<double> pcs(static_cast<size_t>(kPCA) * dPCA, 0.0);
    dipsplit::top_pcs_power_iteration(X.data(), M, dPCA, kPCA, pcs.data());

    // Gate B — valley test on each PC; keep the deepest valley.
    int    bestPC         = -1;
    double bestDepth      = 0.0;
    double bestValleyLoc  = 0.0;
    std::vector<double> bestProj;
    for (int pc = 0; pc < kPCA; ++pc) {
        const double* u = pcs.data() + pc * dPCA;
        std::vector<double> proj(static_cast<size_t>(M));
        for (int i = 0; i < M; ++i) {
            const float* row = X.data() + static_cast<size_t>(i) * dPCA;
            double s = 0.0;
            for (int j = 0; j < dPCA; ++j)
                s += (row[j] - mu[j]) * u[j];
            proj[static_cast<size_t>(i)] = s;
        }
        const dipsplit::ValleyResult vr =
            dipsplit::valley_test(proj.data(), M, valleyThresh);
        if (vr.depth > bestDepth) {
            bestPC        = pc;
            bestDepth     = vr.depth;
            bestValleyLoc = vr.valley_loc;
            bestProj      = std::move(proj);
        }
    }
    D.bestPC    = bestPC;
    D.bestDepth = bestDepth;
    if (bestPC < 0 || bestDepth < valleyThresh) {
        D.reason = QStringLiteral("no_valley");
        return D;
    }

    // -------------------------------------------------------------------
    // Seed k=2 at the valley; compute initial centroids.
    // -------------------------------------------------------------------
    std::vector<int> labels(static_cast<size_t>(M), 0);
    for (int i = 0; i < M; ++i)
        labels[static_cast<size_t>(i)] =
            (bestProj[static_cast<size_t>(i)] >= bestValleyLoc) ? 1 : 0;

    std::vector<double> c0(dPCA, 0.0), c1(dPCA, 0.0);
    int n0 = 0, n1 = 0;
    for (int i = 0; i < M; ++i) {
        const float* row = X.data() + static_cast<size_t>(i) * dPCA;
        if (labels[static_cast<size_t>(i)] == 0) {
            for (int j = 0; j < dPCA; ++j) c0[j] += row[j];
            ++n0;
        } else {
            for (int j = 0; j < dPCA; ++j) c1[j] += row[j];
            ++n1;
        }
    }
    if (n0 < minSize || n1 < minSize) {
        D.n0 = n0; D.n1 = n1;
        D.reason = QStringLiteral("small_child");
        return D;
    }
    for (int j = 0; j < dPCA; ++j) { c0[j] /= n0; c1[j] /= n1; }

    // Refine with k-means.
    dipsplit::kmeans2_refine(X.data(), M, dPCA, c0.data(), c1.data(),
                             labels.data(), /*max_iters=*/20);

    n0 = n1 = 0;
    for (int i = 0; i < M; ++i)
        (labels[static_cast<size_t>(i)] == 0) ? ++n0 : ++n1;
    D.n0 = n0; D.n1 = n1;
    if (n0 < minSize || n1 < minSize) {
        D.reason = QStringLiteral("small_child");
        return D;
    }

    // Gate C — BIC: accept only if two-cluster model beats one-cluster.
    const dipsplit::BicPair bp = dipsplit::bic_two_vs_one(
        X.data(), M, dPCA, labels.data());
    D.deltaBIC = bp.bic_k1 - bp.bic_k2;
    if (!(bp.bic_k2 < bp.bic_k1)) {
        D.reason = QStringLiteral("bic_worse");
        return D;
    }

    // Accepted — return labels and row mapping for the commit.
    D.accepted     = true;
    D.reason       = QStringLiteral("split");
    D.labels       = std::move(labels);
    D.rowsByMember = std::move(rowsByMember);
    return D;
}

// ---------------------------------------------------------------------------
// KlustersDoc::dipSplitApply
//
// Commits a pre-computed DipSplitDecision (model A: two-cluster split).
//
// Architecture:
//   1. Allocate two new IDs at the tail: leftId = nextFreeClusterId(),
//      rightId = leftId + 1.
//   2. Build leftRows (label==0) and rightRows (label==1).
//   3. Call Data::splitClusterTwoWays — partitions the source's spikes
//      into leftId + rightId in a single rebuild.  Source becomes empty
//      and is removed.  ONE Data-side undo entry.
//   4. Call commitTwoClusterCreation — palette colours, view
//      notifications, doc-side undo (one entry).  View-side undo
//      routes through addNewClustersToView (recluster variant) for
//      one view-side entry.
//
// One Ctrl+Z fully reverts.  One Ctrl+Y fully replays.  No rename, no
// asymmetric undo.
//
// Palette result for source=22 with previous max=31:
//   pre:   {1, 2, ..., 21, 22, 23, ..., 31}
//   post:  {1, 2, ..., 21,     23, ..., 31, 32=left, 33=right}
//
// minSize/bloatFactor/valleyThresh are recorded in the curation log
// alongside the decision metrics; they don't influence behaviour here.
// ---------------------------------------------------------------------------
KlustersDoc::DipSplitResult
KlustersDoc::dipSplitApply(const DipSplitDecision& D,
                            int   minSize,
                            float bloatFactor,
                            float valleyThresh)
{
    auto resultFromDecision = [](const DipSplitDecision& D) {
        DipSplitResult R;
        R.accepted   = D.accepted;
        R.sourceId   = D.clusterId;
        R.n0         = D.n0;
        R.n1         = D.n1;
        R.bestPC     = D.bestPC;
        R.bestDepth  = D.bestDepth;
        R.mahal2P90  = D.mahal2P90;
        R.chi2_90    = D.chi2_90;
        R.deltaBIC   = D.deltaBIC;
        R.reason     = D.reason;
        return R;
    };

    auto buildLogDetails = [&](const DipSplitDecision& D, int leftId, int rightId) {
        QMap<QString, QVariant> m;
        m.insert(QStringLiteral("algorithm"),     QStringLiteral("dipsplit"));
        m.insert(QStringLiteral("source_cluster"), D.clusterId);
        m.insert(QStringLiteral("left_cluster"),   leftId);
        m.insert(QStringLiteral("right_cluster"),  rightId);
        m.insert(QStringLiteral("reason"),         D.reason);
        m.insert(QStringLiteral("min_size"),       minSize);
        m.insert(QStringLiteral("bloat_factor"),   static_cast<double>(bloatFactor));
        m.insert(QStringLiteral("valley_thresh"),  static_cast<double>(valleyThresh));
        m.insert(QStringLiteral("n_left"),         D.n0);
        m.insert(QStringLiteral("n_right"),        D.n1);
        m.insert(QStringLiteral("best_pc"),        D.bestPC);
        m.insert(QStringLiteral("best_depth"),     D.bestDepth);
        m.insert(QStringLiteral("mahal2_p90"),     D.mahal2P90);
        m.insert(QStringLiteral("chi2_90"),        D.chi2_90);
        m.insert(QStringLiteral("delta_bic"),      D.deltaBIC);
        return m;
    };

    if (!D.accepted)
        return resultFromDecision(D);

    const int sourceClusterId = D.clusterId;

    // ── Allocate two free cluster IDs at the tail ────────────────────────
    const int leftId  = static_cast<int>(clusteringData->nextFreeClusterId());
    const int rightId = leftId + 1;
    if (leftId == 0) {
        DipSplitResult R = resultFromDecision(D);
        R.accepted = false;
        R.reason   = QStringLiteral("no_free_id");
        return R;
    }

    // ── Curation-log: open the action ─────────────────────────────────────
    logBefore(CurationLogger::ActionType::SPLIT, QList<int>{ sourceClusterId });

    // ── Build the row-sets for label==0 (left) and label==1 (right) ──────
    QSet<dataType> leftRows;
    QSet<dataType> rightRows;
    leftRows.reserve(D.n0);
    rightRows.reserve(D.n1);
    const int M = static_cast<int>(D.labels.size());
    for (int i = 0; i < M; ++i) {
        const dataType row = D.rowsByMember.at(i);
        if (D.labels[static_cast<size_t>(i)] == 0)
            leftRows.insert(row);
        else
            rightRows.insert(row);
    }

    // ── Mutate (single Data-side undo entry) ─────────────────────────────
    KlustersView* activeView = app()->activeView();

    QList<int> fromClusters;
    QList<int> emptiedClusters;
    QList<int> newClusters;
    clusteringData->splitClusterTwoWays(sourceClusterId,
                                         leftRows,  leftId,
                                         rightRows, rightId,
                                         fromClusters, emptiedClusters,
                                         newClusters);
    if (newClusters.size() != 2) {
        // Degenerate split — one side empty after row resolution.
        if (activeView) activeView->showAllWidgets();
        if (curationLogger && curationLogger->isOpen()) {
            curationLogger->recordActionDetails(buildLogDetails(D, 0, 0));
        }
        logAfter(QList<int>{ sourceClusterId });
        DipSplitResult R = resultFromDecision(D);
        R.accepted = false;
        R.reason   = QStringLiteral("small_child");
        return R;
    }

    // ── UI plumbing (single doc-side undo entry, single view-side entry) ─
    commitTwoClusterCreation(leftId, rightId, fromClusters, emptiedClusters,
                             activeView);

    // ── Curation-log: details + after-snapshot ───────────────────────────
    if (curationLogger && curationLogger->isOpen()) {
        QMap<QString, QVariant> details = buildLogDetails(D, leftId, rightId);
        curationLogger->recordActionDetails(details);
    }
    logAfter(QList<int>{ leftId, rightId });

    // ── Build result ─────────────────────────────────────────────────────
    DipSplitResult R = resultFromDecision(D);
    R.leftId  = leftId;
    R.rightId = rightId;
    return R;
}



// ---------------------------------------------------------------------------
// KlustersDoc::splitClusterByKnnVsReferences
//
// Doc-level orchestration for the K-nearest-neighbour N-way split.  The
// shape mirrors dipSplitApply:
//
//   1. Validate the source via clusterHasMembers (patch96 desync guard).
//   2. Open the curation log for this action.
//   3. Call Data::splitClusterByKnnVsReferences — pure algorithm; on
//      success, prepareUndo has been called (single Data-side undo).
//   4. Update the colour palette: append a colour entry per new cluster
//      (HSV-cycled by id, same scheme as commitTwoClusterCreation).
//   5. Record one doc-side undo via prepareReclusteringUndo (treating
//      the operation as N-source → M-new — the source is in
//      emptiedClusters iff fully consumed).
//   6. Update every KlustersView with addNewClustersToView (single
//      view-side entry each — the recluster variant).
//   7. Record curation-log details + after-snapshot.
//
// One Ctrl+Z fully reverts.  No rename, no asymmetric undo.
// ---------------------------------------------------------------------------
KlustersDoc::KnnSplitResult
KlustersDoc::splitClusterByKnnVsReferences(int    sourceCluster,
                                            int    K,
                                            double majorityThreshold,
                                            int    minNewClusterSize,
                                            int    minRefClusterSize)
{
    KnnSplitResult R;
    R.sourceId = sourceCluster;

    // ── Desync guard (same predicate as slotRecluster) ────────────────────
    if (!clusterHasMembers(sourceCluster)) {
        R.reason = tr("Cluster %1 is not registered in clusterInfoMap "
                      "(spikesByCluster ↔ clusterInfoMap desync). "
                      "Save the session and re-open before retrying.")
                      .arg(sourceCluster);
        return R;
    }

    // ── Open curation log ─────────────────────────────────────────────────
    logBefore(CurationLogger::ActionType::SPLIT, QList<int>{ sourceCluster });

    // ── Run the algorithm (single Data-side undo entry on success) ────────
    QList<int> newClusters;
    QList<int> matchedReferences;
    QList<int> emptiedClusters;
    QString    err;
    const bool ok = clusteringData->splitClusterByKnnVsReferences(
        sourceCluster, K, majorityThreshold,
        minNewClusterSize, minRefClusterSize,
        newClusters, matchedReferences, emptiedClusters, err);
    if (!ok) {
        // Nothing was mutated.  Close the log entry as "no-op" and bail.
        if (curationLogger && curationLogger->isOpen()) {
            QMap<QString, QVariant> details;
            details.insert(QStringLiteral("algorithm"),       QStringLiteral("knn_split_vs_references"));
            details.insert(QStringLiteral("source_cluster"),  sourceCluster);
            details.insert(QStringLiteral("k"),               K);
            details.insert(QStringLiteral("majority_thresh"), majorityThreshold);
            details.insert(QStringLiteral("min_new_size"),    minNewClusterSize);
            details.insert(QStringLiteral("min_ref_size"),    minRefClusterSize);
            details.insert(QStringLiteral("status"),          QStringLiteral("rejected"));
            details.insert(QStringLiteral("reason"),          err);
            curationLogger->recordActionDetails(details);
        }
        logAfter(QList<int>{ sourceCluster });
        R.reason = err;
        return R;
    }

    R.accepted          = true;
    R.newClusters       = newClusters;
    R.matchedReferences = matchedReferences;
    R.emptiedClusters   = emptiedClusters;
    R.nResidual         = 0;
    for (int i = 0; i < newClusters.size(); ++i)
        if (matchedReferences.value(i, 0) == -1)
            R.nResidual = clusteringData->nbOfSpikes(
                static_cast<dataType>(newClusters[i]));

    // ── Doc-side undo entry (one for the whole N-way operation) ───────────
    prepareReclusteringUndo(newClusters, emptiedClusters);

    // ── UI plumbing: palette colours + view notifications ─────────────────
    // Mirrors commitTwoClusterCreation exactly, generalised to N new
    // clusters.  Builds clustersToShow as: current view contents minus
    // emptied sources, plus all new clusters.
    KlustersView* activeView = app()->activeView();
    QList<int> clustersToShow;
    if (activeView) {
        const QList<int> currentlyShown = activeView->clusters();
        for (int c : currentlyShown)
            if (!emptiedClusters.contains(c)) clustersToShow.append(c);
    }
    QColor color;
    for (int newId : newClusters) {
        // Same HSV scheme used by commitTwoClusterCreation — keeps the
        // palette stable and visually consistent with other split paths.
        color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10,
                     200, 255);
        clusterColorList->append(newId, color);
        clustersToShow.append(newId);
    }
    for (int oldId : emptiedClusters)
        clusterColorList->remove(oldId);

    // Per-view notification: recluster-variant primitive — one view-side
    // undo entry per view.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == activeView);
        v->addNewClustersToView(emptiedClusters, newClusters, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }
    // Matrix-view / signal-bus notification — recluster shape (dissolved
    // sources only, new clusters appear at the tail).
    emit newClustersAdded(emptiedClusters);

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    if (activeView) activeView->showAllWidgets();

    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);

    // The source cluster (if not fully consumed) has stale waveform /
    // correlogram caches — invalidate them.  emptied clusters' caches
    // vanish with the cluster itself, so skip those.
    QList<int> fromClusters;
    fromClusters.append(sourceCluster);
    for (int cid : fromClusters) {
        if (emptiedClusters.contains(cid)) continue;
        invalidateWaveformCache(cid);
        invalidateCorrelogramCache(cid);
    }
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        for (int cid : fromClusters) {
            if (emptiedClusters.contains(cid)) continue;
            v->invalidateClusterDisplay(cid);
        }
    }

    setModified(true);

    // ── Curation-log details ──────────────────────────────────────────────
    if (curationLogger && curationLogger->isOpen()) {
        QMap<QString, QVariant> details;
        details.insert(QStringLiteral("algorithm"),       QStringLiteral("knn_split_vs_references"));
        details.insert(QStringLiteral("source_cluster"),  sourceCluster);
        details.insert(QStringLiteral("k"),               K);
        details.insert(QStringLiteral("majority_thresh"), majorityThreshold);
        details.insert(QStringLiteral("min_new_size"),    minNewClusterSize);
        details.insert(QStringLiteral("min_ref_size"),    minRefClusterSize);
        details.insert(QStringLiteral("n_new_clusters"),  static_cast<int>(newClusters.size()));
        QList<QVariant> newIdsV;     for (int i : newClusters)       newIdsV.append(i);
        QList<QVariant> refIdsV;     for (int i : matchedReferences) refIdsV.append(i);
        QList<QVariant> emptiedV;    for (int i : emptiedClusters)   emptiedV.append(i);
        details.insert(QStringLiteral("new_clusters"),       newIdsV);
        details.insert(QStringLiteral("matched_references"), refIdsV);
        details.insert(QStringLiteral("emptied"),            emptiedV);
        details.insert(QStringLiteral("status"),             QStringLiteral("accepted"));
        curationLogger->recordActionDetails(details);
    }
    logAfter(newClusters);

    R.reason = tr("Split %1 into %2 new cluster(s)%3.")
                .arg(sourceCluster)
                .arg(newClusters.size())
                .arg(emptiedClusters.contains(sourceCluster)
                     ? tr(" (source consumed)") : tr(""));
    return R;
}

