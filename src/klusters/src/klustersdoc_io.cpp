// klustersdoc_io.cpp — KlustersDoc document lifecycle and file I/O.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// document open/save/import/close path of KlustersDoc — canCloseDocument,
// closeDocument, importDocument, openDocument, saveDocument, canCloseView, the
// crash-recovery hooks (updateAutoSavingInterval/stopAutoSaving/autoSaveTick),
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
#include <algorithm>
#include <fstream>
#include <iterator>

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
#include "parameteryamlmodifier.h"
#include "dipsplit.h"
#include "parameteryamlreader.h"
#include "clusteruserinformation.h"
#include "hierarchyfiles.h"
#include "filecommit.h"
#include "unitsidecar.h"

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

    // A save that died while installing its files left a commit journal: every
    // file it names was completely written before the journal was, so finish
    // the commit (roll it forward) before anything is read.
    {
        const QString journal = urlFileInfo.absolutePath() + QDir::separator()
                                + "." + cluFileName + ".commit";
        if (QFileInfo::exists(journal)) {
            std::string report;
            const bool done = filecommit::recoverCommit(QFile::encodeName(journal).toStdString(), &report);
            QApplication::restoreOverrideCursor();
            if (done)
                QMessageBox::information(nullptr, tr("Interrupted save completed"),
                    tr("The last save of %1 was interrupted while its files were being "
                       "replaced.  It has now been completed, so the files on disk are the ones "
                       "that save wrote:\n\n%2").arg(cluFileName, QString::fromStdString(report)));
            else
                QMessageBox::warning(nullptr, tr("Interrupted save"),
                    tr("The last save of %1 was interrupted while its files were being "
                       "replaced, and it could not be completed:\n\n%2\nThe journal %3 was left "
                       "in place; check the files before saving again.")
                        .arg(cluFileName, QString::fromStdString(report), journal));
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        }
    }


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

    // A rescue pair whose install was interrupted is completed first.
    filecommit::recoverCommit(QFile::encodeName(cluFileSaveUrl + QStringLiteral(".commit")).toStdString(),
                              nullptr);

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
                // A hierarchical session's rescue also holds the atom ids, taken
                // with the unit ids from one snapshot.  Restore them together --
                // but only if they belong together (same spike count, every atom
                // under one unit); otherwise keep the .clc and let the next save
                // re-derive the map.
                else if (cluName.contains(QStringLiteral(".clu."))) {
                    QString clcName = cluName;
                    clcName.replace(clcName.lastIndexOf(QStringLiteral(".clu.")), 5, QStringLiteral(".clc."));
                    const QString clcRescue = dir.filePath(QStringLiteral(".") + clcName + QStringLiteral(".autosave"));
                    if (QFileInfo::exists(clcRescue)) {
                        const hierarchyfiles::IdFile u = hierarchyfiles::readIdFile(QFile::encodeName(dir.filePath(cluName)).toStdString());
                        const hierarchyfiles::IdFile a = hierarchyfiles::readIdFile(QFile::encodeName(clcRescue).toStdString());
                        const bool fits = u.ok && a.ok && u.ids.size() == a.ids.size()
                            && hierarchyfiles::scanNesting(std::vector<int>(u.ids.begin(), u.ids.end()),
                                                           std::vector<int>(a.ids.begin(), a.ids.end())).straddlers.empty();
                        if (fits) {
                            const QFileInfo clcInfo(dir.filePath(clcName));
                            if (clcInfo.exists())
                                dir.rename(clcName, clcName + QLatin1String(".")
                                           + clcInfo.lastModified().toString("MM.dd.yyyy.hh.mm"));
                            if (!dir.rename(QFileInfo(clcRescue).fileName(), clcName))
                                QMessageBox::critical(nullptr, tr("I/O Error !"),
                                    tr("The rescue copy of the child layer could not be renamed to %1; "
                                       "the existing child layer is used.").arg(clcName));
                        } else {
                            QMessageBox::warning(nullptr, tr("Rescue file"),
                                tr("The rescue copy of the child layer (%1) does not fit the "
                                   "recovered clustering, so the existing child layer is kept.  "
                                   "Repair Nesting re-cuts any atom that now spans two units.")
                                    .arg(QFileInfo(clcRescue).fileName()));
                        }
                    }
                }
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


    loadUnitsFile();

    // Crash recovery: a timer on this thread writes the rescue copies.
    autoSaveFailed = false;
    unitsInfoModified = false;
    syncStamps.clear();
    if(autoSave)
        startAutoSaveTimer();

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

void KlustersDoc::updateAutoSavingInterval(int interval){
    savingInterval = interval;
    autoSave = true;
    if (clusteringData) startAutoSaveTimer();
}

void KlustersDoc::startAutoSaveTimer(){
    if (!autoSaveTimer) return;
    autoSaveTimer->start(qMax(1, savingInterval) * 60000);
}

bool KlustersDoc::stopAutoSaving(bool currentDocument){
    if (autoSaveTimer) autoSaveTimer->stop();
    removeRescueFiles();
    if(!currentDocument) autoSave = false;
    return true;
}

QStringList KlustersDoc::rescueFiles() const {
    QStringList out;
    auto rescueOf = [](const QString& path){
        const QFileInfo fi(path);
        return fi.absolutePath() + QDir::separator() + QStringLiteral(".") + fi.fileName()
               + QStringLiteral(".autosave");
    };
    if (!docUrl.isEmpty()) out << rescueOf(docUrl);
    if (isHierarchicalSession()) out << rescueOf(clcSiblingPath);
    return out;
}

void KlustersDoc::removeRescueFiles(){
    for (const QString& f : rescueFiles()) QFile::remove(f);
}

void KlustersDoc::autoSaveTick(){
    // Nothing to protect, or a save is writing the real files right now.
    if (!clusteringData || autoSaveFailed || isSaving() || !isModified()) return;

    // One snapshot of both layers (as a save takes), written as a pair: both
    // staged, then both renamed, so the rescue copies always match each other.
    const QStringList files = rescueFiles();
    std::vector<std::string> targets;
    std::string e;
    const std::vector<int32_t> units = labelsInSpikeOrder(*clusteringData);
    bool ok = hierarchyfiles::stageIdFile(localPath(files.at(0)),
                  hierarchyfiles::distinctCount(units), units, &e);
    if (ok) targets.push_back(localPath(files.at(0)));
    if (ok && files.size() > 1 && childData) {
        const std::vector<int32_t> atoms = labelsInSpikeOrder(*childData);
        ok = hierarchyfiles::stageIdFile(localPath(files.at(1)),
                                         hierarchyfiles::distinctCount(atoms), atoms, &e);
        if (ok) targets.push_back(localPath(files.at(1)));
    }
    // Installed as one commit: a crash between the two renames is completed on
    // the next open (the journal sits beside the rescue copies), so a unit
    // rescue never meets the atom rescue of an earlier tick.
    if (ok) ok = filecommit::commitStaged(targets, localPath(files.at(0) + QStringLiteral(".commit")), &e);
    if (ok) return;

    filecommit::discardStaged(targets);
    autoSaveFailed = true;
    if (autoSaveTimer) autoSaveTimer->stop();
    QMessageBox::critical(nullptr,tr("I/O Error !"),tr(
        "In order to protect your work in case of a crash, Klusters periodically saves a hidden copy of the cluster file"
        " in the directory where your files are located (this temporary rescue file is removed when you quit the application).\n"
        "However, it now appears that this rescue file cannot be created:\n%1\n"
        "This feature will thus be disabled for the current session.").arg(QString::fromStdString(e)));
}


bool KlustersDoc::prepareSave(const QString& saveUrl){
    // A finished Save As whose done-event has not been handled yet (a close
    // right after it) still has a sibling move to apply.
    if (savePlan.siblingsMoved) {
        clcSiblingPath = savePlan.clcPath;
        clpSiblingPath = savePlan.clpPath;
    }
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

    // The cluster information goes to the clustering's .cli file, never to the
    // session YAML: the units table there is the record of FINISHED curation,
    // filled on request by ndManager (Actions > Record Curated Units).  Taken
    // here with the ids, so the file always matches the .clu it ships with.
    {
        const neurosuite::custody::Anchor a =
            neurosuite::custody::parseAnchor(QFileInfo(saveUrl).fileName().toStdString());
        const int group = a.ok ? a.group : electrodeGroupID.toInt();
        QMap<int,ClusterUserInformation> info;
        clusteringData->getClusterUserInformation(group, info);
        std::vector<unitsidecar::Row> rows;
        for (auto it = info.cbegin(); it != info.cend(); ++it) {
            const ClusterUserInformation& c = it.value();
            rows.push_back({c.getGroup(), c.getCluster(), c.getStructure().toStdString(),
                            c.getType().toStdString(), c.getId().toStdString(),
                            c.getQuality().toStdString(), c.getNotes().toStdString()});
        }
        plan.unitsPath = fromLocal(unitsidecar::pathFor(localPath(saveUrl)));
        plan.unitsText = unitsidecar::format(rows);
        const bool anything = std::any_of(rows.begin(), rows.end(),
                                          [](const unitsidecar::Row& r){ return !r.describesNothing(); });
        // Write when there is something to record, or to clear a file that has some.
        plan.writeUnits = !plan.unitsPath.isEmpty()
            && (anything || QFileInfo::exists(plan.unitsPath)
                || (!isSaveAs && QFileInfo::exists(fromLocal(unitsidecar::pathFor(localPath(docUrl))))));
    }

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
    // saveDocument discards what it staged on every failure; this catches a save
    // that stopped in between.
    filecommit::discardStaged(savePlan.staged);
    // A Save As that moved the session moves its siblings with it -- here, on the
    // GUI thread that reads them, rather than on the SaveThread.
    if (savePlan.siblingsMoved) {
        clcSiblingPath = savePlan.clcPath;
        clpSiblingPath = savePlan.clpPath;
    }
    savePlan = SavePlan();
    if (ok) {
        unitsInfoModified = false;
        removeRescueFiles();               // the real files now hold what they protected
    } else {
        modified = true;
    }
}

bool KlustersDoc::stageIdTarget(const QString& path, int32_t header, const std::vector<int32_t>& ids,
                                bool force, std::vector<std::string>& targets, QString* error){
    const std::string p = localPath(path);
    // Already on disk: the ids are compared, not the count header (other writers
    // put other things there and every reader ignores it).
    if (!force) {
        const hierarchyfiles::IdFile onDisk = hierarchyfiles::readIdFile(p);
        if (onDisk.ok && onDisk.ids == ids) return true;
    }
    std::string e;
    if (!hierarchyfiles::stageIdFile(p, header, ids, &e)) {
        *error = QString::fromStdString(e);
        return false;
    }
    targets.push_back(p);
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
    // is live, which the commit below then installs as the original.  (Without a
    // pending set it rewrites the real .spk directly, outside the commit.)  Plain
    // Save only and non-fatal: on failure the existing .spk is left in place and
    // the save proceeds.
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

    // ── where the session goes (Save As), worked out before anything moves ──
    QString newBaseName = baseName, newGroup = electrodeGroupID, newParameterFile = parameterFile;
    QString saMethod = QStringLiteral("standard");   // method parsed from a SaveAs target
    QString spkTarget = origSpkPath, resTarget = origResPath, fetTarget = origFetPath;
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
        // gave "<dir>/<new.clu>/<base>.yaml", which never exists.
        newParameterFile = docUrlFileInfo.absolutePath() + QDir::separator() + newBaseName + ".yaml";
        // Re-resolve the waveform triple at the new location, pinned to the
        // method parsed from the SaveAs target (chain-of-custody).
        const QString newBase = docUrlFileInfo.absolutePath() + QDir::separator() + newBaseName;
        spkTarget = resolveFeature(newBase, "spk", newGroup, saMethod);
        resTarget = resolveFeatureAny(newBase, "res", newGroup, saMethod);
        fetTarget = resolveFeature(newBase, "fet", newGroup, saMethod);
    }
    const QString cluTarget = isSaveAs ? saveUrl : docUrl;

    // ── phase 1: stage every file that is not already at parity ─────────────
    // Each new file is written completely next to its target ("<target>.saving")
    // and flushed; no target is touched.  A file whose content is already what
    // the save would write is not staged at all: an unchanged .spk is not copied
    // (filecommit::atParity), an unchanged .clu/.clc/.clp is not rewritten.
    std::vector<std::string>& targets = savePlan.staged;
    targets.clear();
    auto abandon = [&](const QString& why, int code){
        filecommit::discardStaged(targets);
        targets.clear();
        lastSaveErrorMessage = why;
        return code;
    };
    QString err;

    // .clu.  Save As always writes the new name.
    if (!stageIdTarget(cluTarget, savePlan.fiberCount, savePlan.fiber, isSaveAs, targets, &err))
        return abandon(QString(
            "Cannot write the cluster file:\n%1\n\n%2\n\n"
            "Likely causes:\n"
            "  • File owned by a different user (common after running\n"
            "    process_drifttracker / KiloKlustaKwik as another uid,\n"
            "    e.g. via sudo or a build account)\n"
            "  • Parent directory not writable by the user running Klusters\n"
            "  • File or its directory marked immutable (chattr +i)\n"
            "  • Filesystem mounted read-only or out of space").arg(cluTarget, err), OPEN_ERROR);

    // .clc / .clp.
    if (savePlan.writeClc
        && !stageIdTarget(savePlan.clcPath, savePlan.atomCount, savePlan.atom, isSaveAs, targets, &err))
        return abandon(tr("Could not write the child layer (.clc); nothing was saved:\n%1").arg(err),
                       SAVE_ERROR);
    if (savePlan.writeClp
        && !stageIdTarget(savePlan.clpPath, static_cast<int32_t>(savePlan.clp.size()), savePlan.clp,
                          isSaveAs, targets, &err))
        return abandon(tr("Could not write the parent map (.clp); nothing was saved:\n%1").arg(err),
                       SAVE_ERROR);

    // .spk/.res/.fet of the .clu space and the synced feature spaces.
    bool waveformsStaged = false;
    if (!stagePendingFiles(spkTarget, resTarget, fetTarget, targets, waveformsStaged, &err))
        return abandon(err, SAVE_ERROR);

    // Cluster information: the clustering's .cli file (never the session
    // YAML -- see prepareSave), staged like the rest, left alone when unchanged.
    if (savePlan.writeUnits) {
        const std::string target = localPath(savePlan.unitsPath);
        std::string current;
        {
            std::ifstream in(target, std::ios::binary);
            if (in) current.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (isSaveAs || current != savePlan.unitsText) {
            const std::string st = filecommit::staged(target);
            std::string fe;
            bool ok;
            {
                std::ofstream out(st, std::ios::binary | std::ios::trunc);
                out << savePlan.unitsText;
                out.close();
                ok = static_cast<bool>(out);
            }
            if (!ok || !filecommit::flushFile(st, &fe)) {
                QFile::remove(QString::fromStdString(st));
                return abandon(tr("Could not write the cluster information file; nothing was "
                                  "saved:\n%1").arg(savePlan.unitsPath), CREATION_ERROR);
            }
            targets.push_back(target);
        }
    }

    // ── phase 2: install them all, or none ──────────────────────────────────
    // The journal sits next to the .clu it commits; openDocument() finishes a
    // commit a crash interrupted.
    auto journalOf = [](const QString& clu){
        const QFileInfo fi(clu);
        return fi.absolutePath() + QDir::separator() + QStringLiteral(".") + fi.fileName()
               + QStringLiteral(".commit");
    };
    // A Save As writes the journal beside the old .clu as well: its .spk/.res/
    // .fet/.yaml may be the same files, and reopening either name must finish
    // an interrupted commit.
    std::vector<std::string> journals = { localPath(journalOf(cluTarget)) };
    if (isSaveAs) journals.push_back(localPath(journalOf(docUrl)));
    {
        std::string e;
        // The .clc/.clp keep their previous version as .bak, as they always have.
        std::vector<std::string> keepBak;
        if (savePlan.writeClc) keepBak.push_back(localPath(savePlan.clcPath));
        if (savePlan.writeClp) keepBak.push_back(localPath(savePlan.clpPath));
        if (!filecommit::commitStaged(targets, journals, &e, keepBak)) {
            targets.clear();
            lastSaveErrorMessage = tr("The save could not replace its files, so none of them was "
                                      "replaced -- the files on disk are as they were before the "
                                      "save:\n%1").arg(QString::fromStdString(e));
            return SAVE_ERROR;
        }
        targets.clear();
    }

    // ── the session follows the files ───────────────────────────────────────
    if (isSaveAs) {
        docUrl           = saveUrl;
        baseName         = newBaseName;
        electrodeGroupID = newGroup;
        parameterFile    = newParameterFile;
        origSpkPath      = spkTarget;
        origResPath      = resTarget;
        origFetPath      = fetTarget;
        // Synced working copies belong to the old location's spaces; Save As
        // does not carry them.  startSave dropped them on the GUI thread; this
        // covers any other caller and is a no-op otherwise.
        if (!syncedPending.isEmpty()) discardSyncedFeatureSpaces();
        // Applied by finishSave() on the GUI thread.
        savePlan.siblingsMoved = savePlan.movesSiblings;
    }
    // The pending .clu is the session's scratch copy of the ids (read when the
    // atom layer loads); keep it equal to what was just saved.
    if (!pendingCluPath.isEmpty() && !isSaveAs) {
        std::vector<std::string> scratch;
        QString ignored;
        if (stageIdTarget(pendingCluPath, savePlan.fiberCount, savePlan.fiber, false, scratch, &ignored)
            && !scratch.empty()) {
            std::string e;
            hierarchyfiles::installStaged(scratch.front(), false, &e);
        }
    }
    renewPendingAfterCommit(waveformsStaged || isSaveAs);

    // Save As may name another method (saMethod): the .clu now belongs to it,
    // and its pending copies were re-seeded by renewPendingAfterCommit.  Only
    // reachable from the .clu's own space (Save As is refused while borrowed).
    if (isSaveAs) {
        originMethod  = saMethod;
        activeMethod  = saMethod;
        activeFetPath = QFileInfo::exists(pendingFetPath) ? pendingFetPath : origFetPath;
        activeSpkPath = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;
    }

    // Clean only if nothing was edited after the snapshot (the GUI is locked
    // during a save, so this is the belt to that brace).
    modified.clearIfUnchanged(savePlan.dirtySerial);
    return OK;
}

bool KlustersDoc::differsFromDisk(){
    if (!clusteringData || docUrl.isEmpty()) return false;
    if (unitsInfoModified || !pendingRealign.empty()) return true;

    auto idsDiffer = [](const QString& path, const std::vector<int32_t>& ids){
        const std::string p = localPath(path);
        const hierarchyfiles::IdFile f = hierarchyfiles::readIdFile(p);
        return !f.ok || f.ids != ids;
    };
    if (idsDiffer(docUrl, labelsInSpikeOrder(*clusteringData))) return true;
    if (isHierarchicalSession() && childData
        && idsDiffer(clcSiblingPath, labelsInSpikeOrder(*childData)))
        return true;

    // The working copies: only a realign, nudge or sync changes them.
    if (!pendingCluPath.isEmpty()) {
        const QList<QPair<QString, QString>> pairs = {
            {pendingSpkPath, origSpkPath}, {pendingResPath, origResPath}, {pendingFetPath, origFetPath}};
        for (const auto& pr : pairs)
            if (!pr.first.isEmpty()
                && !filecommit::atParity(localPath(pr.first), localPath(pr.second), syncStamps[pr.first]))
                return true;
        for (auto it = syncedPending.constBegin(); it != syncedPending.constEnd(); ++it)
            if (!filecommit::atParity(localPath(it.value()), localPath(it.key()), syncStamps[it.value()]))
                return true;
    }
    return false;
}


bool KlustersDoc::canCloseView(){
    bool returnValue = false;
    // Ask only when there is something to lose: an edit that was undone, or a
    // realign rejected, leaves the document "modified" with nothing different
    // from the files.
    if(isModified() && !differsFromDisk())
        modified = false;
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

void KlustersDoc::loadUnitsFile(){
    if (!clusteringData) return;
    const std::string path = unitsidecar::pathFor(localPath(docUrl));
    if (path.empty()) return;
    std::ifstream in(path, std::ios::binary);
    if (!in) return;                                   // none yet: the YAML's values stand
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<unitsidecar::Row> rows;
    std::string err;
    if (!unitsidecar::parse(text, rows, &err)) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(nullptr, tr("Cluster information"),
            tr("%1 could not be read (%2), so the cluster information shown is the session's.")
                .arg(QFile::decodeName(QByteArray::fromStdString(path)), QString::fromStdString(err)));
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        return;
    }
    // The file is the clustering's own record: every cluster takes what it says,
    // and a cluster it does not list has none.
    QMap<int, unitsidecar::Row> byCluster;
    for (const auto& r : rows) byCluster.insert(r.cluster, r);
    for (dataType id : clusteringData->clusterIds()) {
        const int c = static_cast<int>(id);
        if (c == 0 || c == 1) continue;
        const unitsidecar::Row r = byCluster.value(c);
        clusteringData->setUserClusterInformation(c, QString::fromStdString(r.structure),
            QString::fromStdString(r.type), QString::fromStdString(r.isolationDistance),
            QString::fromStdString(r.quality), QString::fromStdString(r.notes));
    }
}
