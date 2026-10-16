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
#include <QFileInfo>
#include <QThread>

// application specific includes
#include "processwidget.h"
#include "klusters.h"
#include "klustersdoc.h"
#include "klustersjobpool.h"
#include "minmaxthread.h"   // complete type for the finished-signal connect
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
#include "hierarchyfiles.h"

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
    originMethod.clear();
    activeMethod.clear();
    activeFetPath.clear();
    activeSpkPath.clear();
    discardSyncedFeatureSpaces();
    releaseFeatureSpaces();
    dropMovedSpikes();
    parameterFile.clear();
    qDeleteAll(clusterColorListUndoList);
    clusterColorListUndoList.clear();
    qDeleteAll(clusterColorListRedoList);
    clusterColorListRedoList.clear();
    //One clear per side covers everything the descriptor carries — including
    //the deleted-cluster stacks, by-deletion flags and renumbering redo
    //indices the historical list-by-list cleanup missed (the deleted-cluster
    //heap lists leaked across every close/reopen).
    editUndoList.clear();
    editRedoList.clear();


    // Pool jobs hold references to the Data objects: let every queued and
    // running job retire before Data is destroyed.  canCloseDocument()
    // already refused the close while any view had active jobs, so this
    // normally returns at once; it is the backstop for jobs orphaned by a
    // view that died just before the close.
    KlustersJobPool::drain();

    if(clusterColorList != nullptr){
        delete clusteringData;
        clusteringData = nullptr;
        delete clusterColorList;
        clusterColorList = nullptr;
        // hierarchical (.clc) child clustering, if it was loaded
        delete childData;       childData = nullptr;
        delete childColorList;  childColorList = nullptr;
        activeData = nullptr;   activeColorList = nullptr;
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
    // STUB.  This does nothing and reports success.
    //
    // It used to carry a three-step plan naming loadConfigFromNewFormat() and
    // loadDataFromNewFormat(), neither of which exists -- so the comment described
    // an implementation that was never written while the body returned true.  A
    // reader had no way to tell the difference without checking both names.
    //
    // Importing goes through openDocument(); if this is ever needed, it should
    // start from there rather than from the plan that was here.
    Q_UNUSED(url); Q_UNUSED(format);
    return true;
}

int KlustersDoc::openDocument(const QString &url,QString& errorInformation, const char *format ){
    // Three steps, all inlined below rather than in the separate
    // loadConfigFromNewFormat()/loadDataFromNewFormat() the old comment named --
    // neither has existed for some time:
    //   1 - derive the base name from the file
    //   2 - read the session parameters and initialise clusteringData
    //   3 - load the spikes, clusters, times and PCA

    //Initialize the members specific to a document
    clusteringData = new Data();
    // The dimension-extrema recompute (0-crossing membership edits, undo/redo)
    // runs on Data's worker thread and used to finish silently -- the views'
    // world bounds derive from the extrema, so admitted spikes drew clipped
    // at the stale edge until the next dimension pick.  QThread::finished is
    // emitted in the worker context; the receiver context makes the lambda
    // run queued on the GUI thread.  The connection dies with the thread
    // object when this Data is deleted at document close.
    connect(clusteringData->dimensionExtremaThread(), &QThread::finished,
            this, [this]{ emit dimensionExtremaChanged(); });
    clusterColorList = new ItemColors();
    activeData = clusteringData;          // views render the parent until a child is shown
    activeColorList = clusterColorList;
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
    // (.clu parents / .clc microfiber children) or a sibling the user opened to
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

    // cluFileUrl: if a cluster-id file itself was opened (.clu parents or .clc
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
    hierarchyScanFoundViolation = false;   // no scan has run for the next document yet
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
    // Feature space: the .clu's own method is loaded (see switchFeatureSpace),
    // read from the pending copies when they exist (initPendingFiles pointed the
    // reader there), so a rollback to it restores exactly what is loaded now.
    originMethod  = sessionMethod;
    activeMethod  = sessionMethod;
    activeFetPath = QFileInfo::exists(pendingFetPath) ? pendingFetPath : origFetPath;
    activeSpkPath = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;

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
            if(static_cast<AutoSaveThread::AutoSaveEvent*>(event)->isIOerror())
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

namespace {
// Local 8-bit file names for the std:: file helpers, and back.
std::string localPath(const QString& p) { return QFile::encodeName(p).toStdString(); }
QString fromLocal(const std::string& p) { return QFile::decodeName(QByteArray::fromStdString(p)); }

// The cluster id of every spike in .res order (Data's feature rows are 1-based).
std::vector<int32_t> labelsInSpikeOrder(const Data& d)
{
    const QVector<dataType> byRow = d.labelByFeatureRow();
    std::vector<int32_t> out;
    out.reserve(byRow.size() > 0 ? static_cast<std::size_t>(byRow.size() - 1) : 0);
    for (qsizetype r = 1; r < byRow.size(); ++r) out.push_back(static_cast<int32_t>(byRow[r]));
    return out;
}
} // namespace

bool KlustersDoc::prepareSave(const QString& saveUrl){
    savePlan = SavePlan();
    lastSaveErrorMessage.clear();
    lastSaveWarningMessage.clear();
    if (!clusteringData) {
        lastSaveErrorMessage = tr("No clustering is open.");
        return false;
    }
    const bool isSaveAs = (docUrl != saveUrl);

    SavePlan plan;
    plan.url         = saveUrl;
    plan.dirtySerial = modified.serial();
    plan.fiber       = labelsInSpikeOrder(*clusteringData);
    plan.fiberCount  = hierarchyfiles::distinctCount(plan.fiber);

    // A flat session (no .clc, or a .clc without its .clp) writes the .clu only;
    // an orphan .clc is ignored, as it is on open.
    if (isHierarchicalSession()) {
        plan.clcPath = clcSiblingPath;
        plan.clpPath = clpSiblingPath;
        if (isSaveAs) {
            // The triple shares one anchor, so the new .clc/.clp are named after
            // the new .clu.  Save As used to write the .clu alone and keep the old
            // sibling paths: the new name opened flat, and the next plain Save
            // wrote the new clustering's map into the OLD .clp.
            plan.clcPath = fromLocal(hierarchyfiles::siblingPath(localPath(saveUrl), "clc"));
            plan.clpPath = fromLocal(hierarchyfiles::siblingPath(localPath(saveUrl), "clp"));
            if (plan.clcPath.isEmpty() || plan.clpPath.isEmpty()) {
                lastSaveErrorMessage = tr(
                    "This is a hierarchical session, so Save As also writes its .clc and .clp, "
                    "named after the new cluster file.\n\n\"%1\" has no \".clu.\" in it to name "
                    "them from; choose a name of the same form as \"%2\".")
                    .arg(QFileInfo(saveUrl).fileName(), QFileInfo(docUrl).fileName());
                return false;
            }
            plan.movesSiblings = true;
        }

        if (childData) {
            // The maps drive the child palette; keep them honest while on the GUI
            // thread (the save itself no longer reads them -- see below).
            if (!validateHierarchyMaps("prepareSave")) {
                rebuildHierarchyFromData();
                if (!validateHierarchyMaps("prepareSave/after-rebuild"))
                    qWarning() << "[hierarchy] the rebuild did not settle the maps";
            }
            plan.atom      = labelsInSpikeOrder(*childData);
            plan.atomCount = hierarchyfiles::distinctCount(plan.atom);
            plan.writeClc  = true;
        } else {
            // A hierarchical session whose atom layer did not load: the atoms on
            // disk are the session's atoms (nothing edited them), so a plain Save
            // leaves the .clc alone and Save As copies it.
            const neurofileio::CluFile clc = neurofileio::readCluBinary(
                localPath(clcSiblingPath), static_cast<int64_t>(plan.fiber.size()));
            if (clc.ok) {
                plan.atom.assign(clc.ids.begin(), clc.ids.end());
                plan.atomCount = clc.nClusters;
                plan.writeClc  = isSaveAs;
            }
        }

        if (plan.atom.size() != plan.fiber.size() || plan.fiber.empty()) {
            lastSaveWarningMessage = tr(
                "The child layer %1 could not be read for this clustering, so its parent map "
                "(.clp) was not rewritten%2.")
                .arg(QFileInfo(clcSiblingPath).fileName(),
                     isSaveAs ? tr(" and the new name has no .clc or .clp: it opens as a "
                                   "flat clustering") : QString());
            plan.writeClc = plan.writeClp = false;
            plan.movesSiblings = false;
        } else {
            // The .clp is the map the per-spike layers imply, taken from this one
            // snapshot -- not childToParent, which the GUI keeps editing while the
            // SaveThread writes.
            const hierarchyfiles::NestingScan scan = hierarchyfiles::scanNesting(plan.fiber, plan.atom);
            if (!scan.straddlers.empty() && !childData) {
                // Nothing has re-cut the straddling atoms (the re-cut needs the atom
                // layer loaded), so a .clp written now would record an arbitrary
                // owner for each.  Keep the previous map, and carry it on Save As.
                lastSaveWarningMessage = tr(
                    "%n atom(s) of %1 now span more than one unit, and the atom layer is not "
                    "loaded to re-cut them, so the parent map (.clp) was left as it was.  Load "
                    "the hierarchical view, use Hierarchy > Repair Nesting, and save again.",
                    "", static_cast<int>(scan.straddlers.size()))
                    .arg(QFileInfo(clcSiblingPath).fileName());
                if (isSaveAs) {
                    const hierarchyfiles::IdFile old = hierarchyfiles::readIdFile(localPath(clpSiblingPath));
                    if (old.ok) { plan.clp = old.ids; plan.writeClp = true; }
                }
            } else {
                if (!scan.straddlers.empty())
                    lastSaveWarningMessage = tr(
                        "%n atom(s) span more than one unit; the parent map (.clp) records "
                        "each under the first unit it was seen in.  Hierarchy > Repair Nesting "
                        "re-cuts them.", "", static_cast<int>(scan.straddlers.size()));
                plan.clp      = hierarchyfiles::clpFromMap(scan.childToParent);
                plan.writeClp = true;
            }
        }
    }

    savePlan = std::move(plan);
    savePlan.valid = true;
    return true;
}

void KlustersDoc::finishSave(bool ok){
    // saveDocument installs or discards what it staged; this only catches a save
    // that stopped in between.
    if (savePlan.writeClc && !savePlan.clcPath.isEmpty())
        hierarchyfiles::discardStaged(localPath(savePlan.clcPath));
    if (savePlan.writeClp && !savePlan.clpPath.isEmpty())
        hierarchyfiles::discardStaged(localPath(savePlan.clpPath));
    savePlan = SavePlan();
    if (!ok) modified = true;
}

bool KlustersDoc::stageHierarchySiblings(QString* error){
    std::string e;
    if (savePlan.writeClc
        && !hierarchyfiles::stageIdFile(localPath(savePlan.clcPath), savePlan.atomCount,
                                        savePlan.atom, &e)) {
        *error = tr("Could not write the child layer (.clc); nothing was saved:\n%1")
                     .arg(QString::fromStdString(e));
        return false;
    }
    if (savePlan.writeClp
        && !hierarchyfiles::stageIdFile(localPath(savePlan.clpPath),
                                        static_cast<int32_t>(savePlan.clp.size()), savePlan.clp, &e)) {
        if (savePlan.writeClc) hierarchyfiles::discardStaged(localPath(savePlan.clcPath));
        *error = tr("Could not write the parent map (.clp); nothing was saved:\n%1")
                     .arg(QString::fromStdString(e));
        return false;
    }
    return true;
}

bool KlustersDoc::installHierarchySiblings(QString* error){
    std::string e;
    if (savePlan.writeClc && !hierarchyfiles::installStaged(localPath(savePlan.clcPath), true, &e)) {
        if (savePlan.writeClp) hierarchyfiles::discardStaged(localPath(savePlan.clpPath));
        *error = tr("The cluster file was saved, but its child layer could not be replaced, so "
                    "the .clu and .clc on disk disagree until the next successful save:\n%1")
                     .arg(QString::fromStdString(e));
        return false;
    }
    if (savePlan.writeClp && !hierarchyfiles::installStaged(localPath(savePlan.clpPath), true, &e)) {
        *error = tr("The cluster file and child layer were saved, but the parent map (.clp) "
                    "could not be replaced; the next successful save regenerates it:\n%1")
                     .arg(QString::fromStdString(e));
        return false;
    }
    return true;
}

int KlustersDoc::saveDocument(const QString& saveUrl, const char *format /*=0*/){
    Q_UNUSED(format);

    // patch63 — clear any stale error from a previous save attempt.  (The
    // warnings belong to the snapshot and were reset by prepareSave.)
    lastSaveErrorMessage.clear();

    // Determine whether this is a Save (same URL) or SaveAs (new URL).
    const bool isSaveAs = (docUrl != saveUrl);

    // Save As re-resolves and moves the .clu space's .spk/.res/.fet; with another
    // method's space loaded it would re-seed the readers under the wrong
    // features.  Plain Save is fine: it writes only the .clu space's files.
    if (isSaveAs && isBorrowedSpace()) {
        lastSaveErrorMessage = tr("Switch back to the %1 feature space before Save As.")
                               .arg(originMethod);
        return SAVE_ERROR;
    }

    // What is written was captured on the GUI thread by prepareSave().  A caller
    // on the GUI thread that skipped it gets one here; the SaveThread cannot take
    // one (the snapshot reads state only the GUI thread may touch).
    if (!savePlan.valid || savePlan.url != saveUrl) {
        if (QThread::currentThread() != thread() || !prepareSave(saveUrl)) {
            if (lastSaveErrorMessage.isEmpty())
                lastSaveErrorMessage = tr("Internal error: the save was started without its "
                                          "snapshot (KlustersDoc::prepareSave).");
            return SAVE_ERROR;
        }
    }

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
        // Always the .clu space's own .spk (its pending copy when one is live),
        // never a borrowed space's file the reader may be pointing at.
        const QString reTarget = isBorrowedSpace()
            ? (QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath)
            : clusteringData->getSpkFileName();
        const bool reOk = reextractAllSpikesFromFil(reTarget, reLog);
        // The re-extract installs the rebuilt .spk with rename(): a NEW inode.
        // Rebind each layer to it by installing a FRESH reader and
        // republishing (epoch-snapshot step 2) — invalidating the shared
        // reader would also redirect any older epoch a job still holds,
        // which must keep reading its own pinned file version instead.
        // Rebind whenever the reader reads the file just rebuilt — the .clu
        // space, or a borrowed space sharing its waveforms (a _D lag space).
        if (clusteringData->getSpkFileName() == reTarget) {
            clusteringData->reopenSpkReader();
            if (childData) childData->reopenSpkReader();
        }
        qWarning().noquote()
            << "[saveDocument]" << (reOk ? "re-extract:" : "re-extract FAILED:")
            << reLog;
    }

    // ── the .clu ────────────────────────────────────────────────────────────
    // The clustering is written to the session's clu write target, tmpCluFile: the
    // pending copy when a pending set is live (initPendingFiles redirected it there
    // after a SUCCESSFUL seed), else the real .clu.  commitAndRenewPending below then
    // copies the pending copy over docUrl -- which a Save As has already pointed at
    // the new name.
    //
    // Save As used to write straight to the new URL and leave the pending copy as it
    // was at the last save; the commit then copied that stale pending copy over the
    // file just written, so the new name received the clustering as last SAVED and
    // every edit since was missing from it (they stayed in memory only).  Without a
    // pending set there is nothing to commit, and Save As writes the new URL itself.
    //
    // Written through a staged file and a rename, so the real .clu (the target when
    // no pending set is live) is never left truncated by a crash or a full disk.
    const QString cluWritePath =
        (isSaveAs && pendingCluPath.isEmpty()) ? saveUrl : tmpCluFile;
    {
        std::string e;
        if (!hierarchyfiles::writeIdFile(localPath(cluWritePath), savePlan.fiberCount,
                                         savePlan.fiber, false, &e)) {
            // patch63 — say exactly what failed instead of a bare error code.
            lastSaveErrorMessage = QString(
                "Cannot write the cluster file:\n%1\n\n%2\n\n"
                "Likely causes:\n"
                "  • File owned by a different user (common after running\n"
                "    process_drifttracker / KiloKlustaKwik as another uid,\n"
                "    e.g. via sudo or a build account)\n"
                "  • Parent directory not writable by the user running Klusters\n"
                "  • File or its directory marked immutable (chattr +i)\n"
                "  • Filesystem mounted read-only or out of space")
                .arg(cluWritePath, QString::fromStdString(e));
            qWarning().noquote() << "[saveDocument] clu write failed:" << QString::fromStdString(e);
            return OPEN_ERROR;
        }
    }

    // ── the .clc / .clp, staged next to their targets ──────────────────────
    // Installed only after the .clu is committed below.  They used to be written in
    // place first, before the parameter file and the .clu commit -- both of which can
    // fail -- so a failed save left new .clc/.clp beside the old .clu.
    QString hierError;
    if (!stageHierarchySiblings(&hierError)) {
        lastSaveErrorMessage = hierError;
        return SAVE_ERROR;
    }
    auto discardSiblings = [this]{
        if (savePlan.writeClc) hierarchyfiles::discardStaged(localPath(savePlan.clcPath));
        if (savePlan.writeClp) hierarchyfiles::discardStaged(localPath(savePlan.clpPath));
    };

    // ── Save As: where the session moves (worked out before anything moves) ──
    QString newBaseName = baseName, newGroup = electrodeGroupID, newParameterFile = parameterFile;
    QString saMethod = QStringLiteral("standard");   // method parsed from a SaveAs target
    if(isSaveAs){
        QFileInfo docUrlFileInfo(saveUrl);
        QString fileName = docUrlFileInfo.fileName();
        const QStringList fileParts = fileName.split(".", Qt::SkipEmptyParts);

        // Parse the SaveAs target through the shared custody policy -- the same
        // <base>.<type>[.<method>].<grp>[.<suffix>] parser as openDocument, so a
        // tagged path (foo.clu.stderiv.8, foo.clu.standard.8.stack, or a .clc
        // child) updates the base name, group and method.
        const neurosuite::custody::Anchor anchor =
            neurosuite::custody::parseAnchor(fileName.toStdString());
        if (anchor.ok) {
            newBaseName = QString::fromStdString(anchor.base);
            if (!anchor.method.empty())
                saMethod = QString::fromStdString(anchor.method);
            newGroup = QString::number(anchor.group);
        } else {
            // Legacy fallback (unrecognised filename) — preserves old behaviour
            // so callers that don't pass a tagged form still work.
            newBaseName = fileParts.first();
            if(fileParts.count() > 2)  {
                for(qsizetype i = 1;i < fileParts.count()-2; ++i){
                    newBaseName += "." + fileParts.at(i);
                }
            }
            if(fileParts.count() < 3)
                newGroup.clear();
            else
                newGroup = fileParts.at(fileParts.count()-1);
        }

        // The directory of the new .clu, not the file itself: absoluteFilePath()
        // gave "<dir>/<new.clu>/<base>.yaml", which never exists, so a Save As of
        // any session with cluster-information support failed as "not writable"
        // after the .clu had been written and docUrl moved.
        newParameterFile = docUrlFileInfo.absolutePath() + QDir::separator() + newBaseName + ".yaml";
    }

    // ── the parameter file must be writable before anything is committed ────
    //NB : for the moment, the specific errors are not return to the user, only a generic message (document could not be saved).
    const bool writeUnits = clusteringData->isTraceViewVariablesAvailable();
    if (writeUnits && !QFileInfo(newParameterFile).isWritable()) {
        lastSaveErrorMessage = tr("The parameter file is missing or not writable, so the "
                                  "cluster information (structure, type, quality, notes) "
                                  "could not be saved; nothing was saved:\n%1").arg(newParameterFile);
        discardSiblings();
        return NOT_WRITABLE;
    }

    // ── commit ─────────────────────────────────────────────────────────────
    // Commit pending files → originals, then re-seed pending from fresh originals.
    // For SaveAs, update the original paths first so commitAndRenewPending()
    // copies to the correct new location.
    if (isSaveAs) {
        docUrl           = saveUrl;
        baseName         = newBaseName;
        electrodeGroupID = newGroup;
        parameterFile    = newParameterFile;
        // Synced working copies belong to the old location's spaces; Save As
        // does not carry them (and is only reachable from the .clu's own space).
        // slotFileSaveAs already dropped them on the GUI thread; this covers any
        // other caller and is a no-op otherwise.
        if (!syncedPending.isEmpty()) discardSyncedFeatureSpaces();
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
        discardSiblings();
        return SAVE_ERROR;
    }

    // The .clu is on disk: put its .clc/.clp beside it.  A Save As points the
    // session at the new names even if this fails, so the next Save writes the
    // triple there instead of into the old location's files.
    const bool siblingsOk = installHierarchySiblings(&hierError);
    if (savePlan.movesSiblings) {
        clcSiblingPath = savePlan.clcPath;
        clpSiblingPath = savePlan.clpPath;
    }

    // Save As may name another method (saMethod): the .clu now belongs to it,
    // and its pending copies were re-seeded by commitAndRenewPending.  Only
    // reachable from the .clu's own space (Save As is refused while borrowed).
    if (isSaveAs) {
        originMethod  = saMethod;
        activeMethod  = saMethod;
        activeFetPath = QFileInfo::exists(pendingFetPath) ? pendingFetPath : origFetPath;
        activeSpkPath = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;
    }

    // ── cluster information (units) in the parameter file ─────────────────
    if (writeUnits) {
        NS3_DIAG()<<" parameterFile"<<parameterFile;
        QMap<int,ClusterUserInformation> clusterUserInformationMap = QMap<int,ClusterUserInformation>();
        clusteringData->getClusterUserInformation(electrodeGroupID.toInt(),clusterUserInformationMap);

        // Build the merged units map: read existing units, overwrite the
        // units for the current electrode group, then write all back.
        ParameterYamlModifier yamlMod;
        if (!yamlMod.parseFile(parameterFile)) {
            lastSaveErrorMessage = tr("The clustering was saved, but the parameter file could "
                                      "not be read to store the cluster information:\n%1")
                                   .arg(parameterFile);
            return PARSE_ERROR;
        }

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

        if (!yamlMod.setUnitsInformation(filtered) || !yamlMod.writeToFile(parameterFile)) {
            lastSaveErrorMessage = tr("The clustering was saved, but the cluster information "
                                      "could not be written to the parameter file:\n%1")
                                   .arg(parameterFile);
            return CREATION_ERROR;
        }
    }

    if (!siblingsOk) {
        lastSaveErrorMessage = hierError;
        return SAVE_ERROR;
    }

    // Clean only if nothing was edited after the snapshot: the curator keeps
    // working while the SaveThread runs, and `modified = false` here used to mark
    // those later edits as saved, so a close afterwards asked nothing.
    modified.clearIfUnchanged(savePlan.dirtySerial);
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
            saveStatus = prepareSave(saveURL) ? saveDocument(saveURL) : SAVE_ERROR;
            finishSave(saveStatus == OK);
            QApplication::restoreOverrideCursor();
            if(saveStatus != OK){
                switch(QMessageBox::question(nullptr, tr("I/O Error !"),
                                             tr("Could not save the current document:\n%1\n\n"
                                                "Close anyway ?").arg(lastSaveError()),
                                             QMessageBox::No|QMessageBox::Yes))
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
