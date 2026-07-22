// klustersdoc_io.cpp — KlustersDoc document lifecycle and file I/O.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// document open/save/import/close path of KlustersDoc — canCloseDocument,
// closeDocument, importDocument, openDocument, saveDocument, canCloseView, the
// autosave hooks (updateAutoSavingInterval/stopAutoSaving/launchAutoSave/customEvent),
// and the document-name/path accessors.  Declarations remain in klustersdoc.h;
// mechanical relocation, no logic change.  Carries the same preamble as
// klustersdoc.cpp — including klustersdoc_internal.h and `extern int nbUndo;` — so
// the custody resolveFeature() calls and nbUndo resolve identically.
//
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
#include <neurosuite/core/custody.hpp>     // shared chain-of-custody policy
#include "klustersdoc_internal.h"  // shared custody path helpers (split TUs)

extern int nbUndo;

// Chain-of-custody path helpers now live in klustersdoc_internal.h so the split
// klustersdoc_*.cpp translation units share one definition instead of each
// carrying a private anonymous-namespace copy.  Pull the three names into this TU.
using klustersdoc_internal::featureMethod;
using klustersdoc_internal::resolveFeature;
using klustersdoc_internal::resolveFeatureAny;
using klustersdoc_internal::stripFeatureSuffix;


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
        // hierarchical (.clc) child clustering, if it was loaded
        delete childData;       childData = nullptr;
        delete childColorList;  childColorList = nullptr;
        activeData = nullptr;   activeColorList = nullptr;
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
    activeData = clusteringData;          // views render the parent until a child is shown
    activeColorList = clusterColorList;
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

    // Parse the opened anchor through the shared custody policy -- the single
    // implementation of <base>.<type>[.<method>].<grp>[.<suffix>] (handles a
    // dotted base, an optional method tag, and a post-group suffix such as
    // .drift / .merged / .microfiber).  The anchor type may be a cluster-id file
    // (.clu fibers / .clc microfiber children) or a sibling the user opened to
    // bootstrap clustering (.fet); its method pins resolution of every sibling
    // (.spk/.fet/.pca/.res).  An untagged legacy name yields method="" which is
    // read as "standard".
    const neurosuite::custody::Anchor anchor =
        neurosuite::custody::parseAnchor(fileName.toStdString());
    if (!anchor.ok)
        return INCORRECT_FILE;
    const QString anchorType    = QString::fromStdString(anchor.type);
    const QString sessionMethod = anchor.method.empty()
                                      ? QStringLiteral("standard")
                                      : QString::fromStdString(anchor.method);
    baseName         = QString::fromStdString(anchor.base);
    electrodeGroupID = QString::number(anchor.group);

    //Create the files url to open (baseName.spk.x,baseName.clu.x,baseName.fet.x,baseName.par.x,baseName.par and baseName.yaml)

    // All siblings are resolved at the anchor's method (chain-of-custody).
    QString spkFileUrl = resolveFeature(
        urlFileInfo.absolutePath() + QDir::separator() + baseName,
        "spk", electrodeGroupID, sessionMethod);

    // cluFileUrl: if a cluster-id file itself was opened (.clu fibers or .clc
    // microfiber children -- same writeClu format), use it verbatim (this
    // preserves a post-group suffix such as .merged / .microfiber).  Otherwise
    // the user opened a sibling (e.g. the .fet, to bootstrap clustering before
    // KlustaKwik), so resolve the .clu sibling -- a missing .clu then falls
    // through to the all-cluster-1 default, instead of the opened file being
    // mis-read as a cluster file (which scatters every spike into its own cluster).
    QString cluFileUrl =
        neurosuite::custody::isClusterIdType(anchorType.toStdString())
            ? urlFileInfo.absoluteFilePath()
            : resolveFeature(urlFileInfo.absolutePath() + QDir::separator() + baseName,
                             "clu", electrodeGroupID, sessionMethod);
    docUrl = cluFileUrl;

    const QString cluFileName = QFileInfo(cluFileUrl).fileName();
    cluFileSaveUrl = urlFileInfo.absolutePath() + QDir::separator() + "." + cluFileName + ".autosave";


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
    QString spikeFilePath = spkFileUrl;


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
    QFile spikeFile(spikeFilePath);

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
            QMessageBox::information(nullptr, tr("Warning!"), tr("Two parameter files were found, %1 and %2. The parameter file %3 will be used.").arg(yamlParFileUrl).arg(parFileUrl).arg(yamlParFileUrl));
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
            switch(QMessageBox::question(nullptr, tr("More recent cluster file found"), tr("A more recent copy of the cluster file (a rescue file) was found on the disk. This indicates that Klusters crashed while editing these data during a previous session.\n"
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
                    QMessageBox::critical(nullptr, tr("I/O Error !"),tr(
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
            if(!clusteringData->initialize(fetFile,cluFile,spkFileLength,spikeFilePath,yamlParFile,electrodeGroupID.toInt(),errorInformation)){
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
            if(!clusteringData->initialize(fetFile,cluFile,spkFileLength,spikeFilePath,parXFile,parFile,errorInformation)){
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
            if(!clusteringData->initialize(fetFile,spkFileLength,spikeFilePath,yamlParFile,electrodeGroupID.toInt(),errorInformation)){
                //close the files
                yamlParFile.close();
                fetFile.close();
                return INCORRECT_CONTENT;
            }
            yamlParFile.close();
            fetFile.close();
        }
        else{
            if(!clusteringData->initialize(fetFile,spkFileLength,spikeFilePath,parXFile,parFile,errorInformation)){
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

    // ── hierarchical view: detect an optional .clc child sibling ─────────────
    // The .clc holds the microfiber / pure-shape children of these units (same
    // .res alignment).  Loaded lazily when the user enables hierarchical view;
    // here we only resolve its path (via custody) and capture the sibling paths
    // needed to re-read it into a second Data.  A fresh open also clears any
    // child clustering left over from a previously opened document.
    if (childData){ delete childData; childData = nullptr; }
    if (childColorList){ delete childColorList; childColorList = nullptr; }
    activeData = clusteringData; activeColorList = clusterColorList;
    parentToChildren.clear(); childToParent.clear(); childScopeVisible.clear();
    childScopeActive = false;
    siblingFetPath       = fetFileUrl;
    siblingSpkPath       = spikeFilePath;
    siblingYamlPath      = yamlParFileUrl;
    siblingSpkFileLength = spkFileLength;
    siblingYamlForm      = isYamlParExist;
    clcSiblingPath.clear();
    clpSiblingPath.clear();
    // The .clc / .clp siblings share the opened .clu's full anchor (method,
    // group AND suffix) -- only the type token differs.  resolveFeature would
    // drop the suffix (e.g. ".microfiber"), so derive the sibling names by
    // swapping the type token in the opened cluster filename instead.  Only
    // attempt this when a .clu parent was opened (not a .clc viewed directly).
    if (isYamlParExist && anchor.ok && anchor.type == std::string("clu")
        && cluFileName.contains(QStringLiteral(".clu."))) {
        const QString dir = urlFileInfo.absolutePath() + QDir::separator();
        QString clcName = cluFileName; clcName.replace(QStringLiteral(".clu."), QStringLiteral(".clc."));
        QString clpName = cluFileName; clpName.replace(QStringLiteral(".clu."), QStringLiteral(".clp."));
        if (QFile::exists(dir + clcName)) clcSiblingPath = dir + clcName;
        if (QFile::exists(dir + clpName)) clpSiblingPath = dir + clpName;
        qDebug() << "[hierarchy] opened" << cluFileName
                 << "-> clc:" << (clcSiblingPath.isEmpty() ? QStringLiteral("(none)") : clcName)
                 << "clp:"   << (clpSiblingPath.isEmpty() ? QStringLiteral("(none)") : clpName);
    }

    // Establish the four permanent pending files so the originals are never
    // touched during a session.  spkFileName and tmpCluFile are redirected
    // to the pending paths; they stay there for the whole document lifetime.
    origSpkPath = spkFileUrl;
    // .res is shared across methods -- resolve it across all of them, or opening a
    // session whose detection ran under a different token fails at the pending copy.
    origResPath = resolveFeatureAny(
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
        // The curation log is named for the clustering stage (the .clu variant,
        // e.g. "stderiv"): the stage is the file's trailing suffix
        // (<base>.curation_log.<group>.<method>), so each stage keeps its own
        // log instead of overwriting one file. The untagged default stage
        // ("standard") is treated as "no stage" and gets no log.
        const bool stagedClustering = (sessionMethod != QLatin1String("standard"));
        if (configuration().getCurationLogging() && stagedClustering) {
            const QString logPath = urlFileInfo.absolutePath() + QDir::separator()
                                    + baseName + ".curation_log." + electrodeGroupID
                                    + "." + sessionMethod;
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
                QMessageBox::critical(nullptr,tr("I/O Error !"),tr(
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

    // Optional (Refinement preferences): rebuild the current group's .spk from the
    // raw .fil at every spike's current timestamp before committing, so the .spk
    // stays consistent with the .res/.fet (e.g. after nudging / realigning).  Off
    // by default.  Writes the reader's .spk -- the pending copy when a pending set
    // is live -- which commitAndRenewPending below then promotes to the original,
    // so no extra re-seed is needed.  Plain Save only (SaveAs of the waveform
    // triple is a separate step) and non-fatal: on failure the existing .spk is
    // left in place and the save proceeds.
    if (!isSaveAs && clusteringData
        && configuration().getReextractSpikesOnSave()) {
        QString reLog;
        const bool reOk =
            reextractAllSpikesFromFil(clusteringData->getSpkFileName(), reLog);
        qWarning().noquote()
            << "[saveDocument]" << (reOk ? "re-extract:" : "re-extract FAILED:")
            << reLog;
    }

    // For a regular Save:  write clu to the pending clu file (crash-safe).
    // For a SaveAs:        write directly to the new URL (no pending for it).
    // tmpCluFile is the session's clu write target: the real .clu at open, redirected to
    // pendingCluPath by initPendingFiles only when the seed SUCCEEDED.  Using pendingCluPath directly
    // wrote to a phantom path when the seed had failed.
    const QString cluWritePath = isSaveAs ? saveUrl : tmpCluFile;

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

    // hierarchical view: regenerate the .clc/.clp siblings from the edited
    // child->parent map so the triple stays consistent.  Only on a regular Save
    // (the captured sibling paths are the originals); SaveAs of the triple is a
    // follow-up.
    // Not gated on childData: the .clp must be regenerated whenever the .clu changes, even if the
    // hierarchical view was never opened this session -- otherwise it is left stale on disk.
    if (!isSaveAs)
        saveHierarchySiblings();

    QString cluFileSuffix_;
    QString saMethod = QStringLiteral("standard");   // method parsed from a SaveAs target
    // For SaveAs: update doc URL and derived paths before committing.
    if(isSaveAs){
        docUrl = saveUrl;
        QFileInfo docUrlFileInfo(docUrl);
        QString fileName = docUrlFileInfo.fileName();
        const QStringList fileParts = fileName.split(".", Qt::SkipEmptyParts);

        // Parse the SaveAs target through the shared custody policy -- the same
        // <base>.<type>[.<method>].<grp>[.<suffix>] parser as openDocument, so a
        // tagged path (foo.clu.stderiv.8, foo.clu.standard.8.stack, or a .clc
        // child) updates baseName / electrodeGroupID / saMethod / cluFileSuffix_.
        const neurosuite::custody::Anchor anchor =
            neurosuite::custody::parseAnchor(fileName.toStdString());
        if (anchor.ok) {
            baseName         = QString::fromStdString(anchor.base);
            if (!anchor.method.empty())
                saMethod     = QString::fromStdString(anchor.method);
            electrodeGroupID = QString::number(anchor.group);
            cluFileSuffix_   = anchor.suffix.empty()
                                   ? QString()
                                   : QLatin1Char('.') + QString::fromStdString(anchor.suffix);
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
        origResPath = resolveFeatureAny(newBase, "res", electrodeGroupID, saMethod);
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
        switch(QMessageBox::question(nullptr, url(),tr("The current file has been modified.\n"
                                                 "Do you want to save it?"),QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel))
        {
        case QMessageBox::Yes:
            saveURL=url();
            int saveStatus;
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            saveStatus = saveDocument(saveURL);
            QApplication::restoreOverrideCursor();
            if(saveStatus != OK){
                switch(QMessageBox::question(nullptr, tr("I/O Error !"),tr("Could not save the current document !\n"
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
