/***************************************************************************
                          errormatrixview.cpp  -  description
                             -------------------
    begin                : Mon Jan 5 2004
    copyright            : (C) 2004 by Lynn Hazan
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
#include <cmath>
#include <algorithm>
#include <QApplication>
#include <QTimer>
#include "errormatrixview.h"
#include "matrixgrid.h"
#include "matrixbadge.h"
#include "featuremask.h"
#include <QStringList>
#include <vector>
#include "errormatrixthread.h"
#include "groupingassistant.h"
#include "array.h"
#include "klustersview.h"
#include "klustersdoc.h"
#include "data.h"
#include "configuration.h"

#include "timer.h"

// Incremental error-matrix reuse is a user preference (Preferences > Refinement).
// The NS3_ERRORMATRIX_INCREMENTAL environment variable, when set, overrides the
// preference (used by the diagnostic workflow).  Read live so toggling the
// preference takes effect without a restart.
static bool errorMatrixIncrementalEnabled(){
    if(qEnvironmentVariableIsSet("NS3_ERRORMATRIX_INCREMENTAL"))
        return qEnvironmentVariableIntValue("NS3_ERRORMATRIX_INCREMENTAL") != 0;
    return configuration().getErrorMatrixIncremental();
}

// include files for Qt


#include <QList>

#include <QMouseEvent>
#include <QEvent>
#include <QDebug>

ErrorMatrixView::ErrorMatrixView(KlustersDoc& doc,KlustersView& view,const QColor& backgroundColor,QStatusBar* statusBar,QWidget *parent,const char* name,int minSize, int
                                 maxSize, int windowTopLeft ,int windowBottomRight,int border) :
    ViewWidget(doc,view,backgroundColor,statusBar,parent,name,minSize,maxSize,windowTopLeft,windowBottomRight,border),
    dataReady(false),
    nbColors(100),
    cutoffProbability(0.1),
    init(true),
    hasBeenRenumbered(false),
    nbActions(0),
    nbRedo(0),
    isNotUpToDate(false),
    nbPreviousUndo(0),
    nbPreviousRedo(0),
    goingToDie(false),
    generation(0),
    probabilities(nullptr)
{


    //Set the drawing variables
    abscissaMin = 0;
    ordinateMax = 0;
    cellWidth = 50;

    // Make the view a first-class interaction target so its wheelEvent /
    // mousePressEvent overrides receive Ctrl+wheel (zoom) and Ctrl+drag (pan).
    // Without this the view is embedded inside a QDockWidget → inner QMainWindow
    // → QScrollArea (DockArea) and, left at the default NoFocus, the wheel/press
    // never reach the overrides.  Matches TemplateMatrixView, whose identical
    // zoom/pan interaction works because it sets these in its constructor.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    initializeColorMap();

    // Settle timer for the selected-pair overlay: restarted on each zoom/pan event,
    // it fires once the gesture stops and repaints the boxes that were suppressed
    // during interaction.  Single-shot; parented to this so it is destroyed with the
    // view.
    pairBoxSettleTimer = new QTimer(this);
    pairBoxSettleTimer->setSingleShot(true);
    connect(pairBoxSettleTimer, &QTimer::timeout, this, [this]{
        suppressPairBoxes = false;
        drawContentsMode = REDRAW;
        update();
    });

    //Compute the error matrix.
    updateMatrixContents();
}

ErrorMatrixView::~ErrorMatrixView(){
    //Ask the threads to stop as soon as possible.
    //Qualified (non-virtual) call: in its own destructor the object is already
    //this dynamic type, so this is the intended teardown; the explicit scope
    //documents that and silences the virtual-call-in-destructor warning.
    ErrorMatrixView::willBeKilled();

    //Wait until all the threads have finish before quiting otherwise
    // it may endup in a crash of the application.
    for(ErrorMatrixThread* errorMatrixThread : threadsToBeKill) {
        while(!errorMatrixThread->wait() && !dataReady){};
    }
    
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();
    delete probabilities;
    delete rawProbCache;

    // Drain any thread-posted events so they don't fire after our destruction.
    QApplication::removePostedEvents(this);
}

bool ErrorMatrixView::isThreadsRunning() const {
    if(threadsToBeKill.isEmpty())
        return false;
    else
        return true;
}

void ErrorMatrixView::stopRunningThreads(){
    // Synchronous quiesce mirroring CorrelationView::stopRunningThreads and this
    // view's own destructor teardown: signal each ErrorMatrixThread to stop, wait
    // for run() to return, delete it, and drop any completion event it posted so a
    // later customEvent() can't fire with a dangling thread pointer.  Deliberately
    // does NOT call willBeKilled()/set goingToDie, so a fresh matrix can be
    // recomputed afterwards (updateMatrixContents).
    //
    // Required because ErrorMatrixView is a ViewWidget but — unlike CorrelationView —
    // never overrode the empty ViewWidget::stopRunningThreads virtual.  Thus
    // KlustersView::stopAllViewThreads() called the empty base on it, leaving the
    // ErrorMatrixThread reading Data while a group/merge (and likewise undo/realign)
    // mutated it in place — the non-deterministic QThread segfault when grouping
    // from the error matrix.  stopProcessing() sets the thread's atomic
    // haveToStopProcessing flag and stops its GroupingAssistant, so run() returns
    // promptly and wait() cannot hang (same contract the destructor relies on).
    for(ErrorMatrixThread* errorMatrixThread : threadsToBeKill)
        errorMatrixThread->stopProcessing();
    for(ErrorMatrixThread* errorMatrixThread : threadsToBeKill)
        while(!errorMatrixThread->wait()){}
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();
    QApplication::removePostedEvents(this);
}

void ErrorMatrixView::customEvent(QEvent* event){
    //Event sent by a ErrorMatrixThread to inform that the data are available.
    if(event->type() == QEvent::User + 600){

        ErrorMatrixThread::ErrorMatrixEvent* errorMatrixEvent = (ErrorMatrixThread::ErrorMatrixEvent*) event;
        //Get the event information
        ErrorMatrixThread* errorMatrixThread = errorMatrixEvent->parentThread();
        Array<double>* newProb = errorMatrixThread->getProbabilities();

        // Accept the result only if:
        //  (a) the thread computed a non-null result, AND
        //  (b) this thread belongs to the current generation (not superseded by a
        //      later updateMatrixContents() call such as one triggered by renumbering).
        // Without the generation check a slow pre-renumber thread arriving after a
        // fast post-renumber thread would silently overwrite the correct result.
        const bool accepted = (newProb != nullptr
                               && errorMatrixThread->getGeneration() == generation);
    // Only the generation we are waiting on ends the "computing" state: a
    // superseded result must not cancel the badge for the newer compute.
    if(errorMatrixThread->getGeneration() == generation) computing = false;

        // Background cache warmer: it never drives the display.  Install only the raw
        // cache it produced (if still current) so the next edit is a fast incremental
        // update; the normalized matrix it computed as a byproduct is discarded.  No
        // repaint, no matrixUpdated, no cursor change, and no warmer relaunch.
        if(errorMatrixThread->getSeedOnly()){
            if(accepted && errorMatrixThread->getUsedIncremental()
               && errorMatrixThread->getNewRaw() != nullptr){
                delete rawProbCache;
                rawProbCache      = errorMatrixThread->getNewRaw();   // take ownership
                rawProbCacheIds   = errorMatrixThread->getNewRawIds();
                rawProbCacheSizes = errorMatrixThread->getNewRawSizes();
                rawProbCacheDims  = errorMatrixThread->getNewRawDims();
                rawProbCacheValid = true;
                rawProbCacheChildScope = doc.isChildClusteringActive();
            } else {
                delete errorMatrixThread->getNewRaw();                // superseded / unusable
            }
            delete newProb;                                          // byproduct, never shown
            while(!errorMatrixThread->wait()){};
            threadsToBeKill.removeAll(errorMatrixThread);
            delete errorMatrixThread;
            return;
        }

        if(accepted){
            delete probabilities;  // release the previous result before overwriting
            probabilities = newProb;
            clusterList = errorMatrixThread->getClusterList();
            computedClusterList = errorMatrixThread->getComputedClusterList();
            ignoreClusterIndex = errorMatrixThread->getIgnoreClusterIndex();
            displayOrder.clear();   // a fresh compute invalidates any display reorder

            // Refresh the incremental raw cache from this compute.  If the thread
            // used the incremental path it hands us a fresh raw array (ownership
            // transfers here); otherwise (full-path fallback) the cache is now
            // stale for the new cluster state, so drop it and let the next compute
            // cold-seed a fresh one.
            if(errorMatrixThread->getUsedIncremental()
               && errorMatrixThread->getNewRaw() != nullptr){
                delete rawProbCache;
                rawProbCache      = errorMatrixThread->getNewRaw();  // take ownership
                rawProbCacheIds   = errorMatrixThread->getNewRawIds();
                rawProbCacheSizes = errorMatrixThread->getNewRawSizes();
                rawProbCacheDims  = errorMatrixThread->getNewRawDims();
                rawProbCacheValid = true;
                rawProbCacheChildScope = doc.isChildClusteringActive();
            } else {
                invalidateRawProbCache("full recompute / fell-back result (customEvent)");
            }

            // The accepted compute's clusterList / probabilities reflect current
            // membership, so every pending edit is now applied — consume the
            // changedIds tracking HERE (moved off dispatch).  A superseded compute
            // never reaches this branch, so its changedIds accumulate for the
            // compute that does accept.
            modifiedClusterList.clear();
            deletedMap.clear();
        } else {
            // Discard the stale / null result.  newProb ownership stays with us
            // when non-null; delete it to avoid a leak.  Same for any raw array
            // the discarded thread produced.
            delete newProb;
            delete errorMatrixThread->getNewRaw();
        }

        //Wait to be sure the thread has return from his run method. Even if the send of the event is the last
        //action of the run method it seems that the event loop can be pretty fast and the run has not
        //return when the event is received here.
        while(!errorMatrixThread->wait()){};

        //Delete the errorMatrixThread.
        threadsToBeKill.removeAll(errorMatrixThread);
        delete errorMatrixThread;
        errorMatrixThread = nullptr;

        if(!goingToDie){
            if(accepted){
                // Each time the matrix is accepted, the window size is recalculated
                // from the (updated) clusterList before triggering a repaint.
                updateWindow();
                dataReady = true;
                setCursor(Qt::ArrowCursor);
                update();
                // Notify external listeners (e.g. KlustersApp Shift+S reorder
                // waiting for a stale matrix to refresh) that fresh data is in.
                // Emitted last so observers seeing the signal can immediately
                // read matrixData() / matrixComputedClusterList() and trust it.
                emit matrixUpdated();

                // A full/GPU compute (startup, reload, post-renumber or 2+-edit bail)
                // leaves the incremental cache empty — it was just invalidated above.
                // If the incremental path is enabled, seed the cache in the background
                // now, using all cores, so the first edit is a fast incremental update
                // rather than a CPU cold-seed.  An edit arriving before the warmer
                // finishes supersedes it (generation bump + stopProcessing) and
                // cold-seeds itself, so nothing is lost.
                if(!rawProbCacheValid && errorMatrixIncrementalEnabled())
                    launchCacheWarmer();
            } else {
                // Stale result discarded — restore cursor if no other thread is still
                // computing, so the UI is not stuck in wait-cursor state.
                if(threadsToBeKill.isEmpty())
                    setCursor(Qt::ArrowCursor);
            }
        }
    }
}

bool ErrorMatrixView::isComputing() const{
    // A display-driving compute is any non-seedOnly thread still pending in
    // threadsToBeKill.  The background cache warmer is seedOnly and is excluded
    // so it never holds the edit lock.
    for(ErrorMatrixThread* t : threadsToBeKill)
        if(t && !t->getSeedOnly()) return true;
    return false;
}

void ErrorMatrixView::updateMatrixContents(){
    computing = true;      // paint a badge over the old matrix, do not blank
    if(!goingToDie){
        //Bump the generation so that customEvent() can identify — and discard — results
        //from any threads that were launched before this call.  This prevents a slow
        //pre-renumber thread from overwriting the result of a faster post-renumber thread.
        ++generation;

        //Ask any already-running threads to stop early; they will still post their
        //event but customEvent() will discard the stale result via the generation check.
        for(ErrorMatrixThread* t : threadsToBeKill)
            t->stopProcessing();

        setCursor(Qt::WaitCursor);
        ErrorMatrixThread* thread = computeMatrix();
        threadsToBeKill.append(thread);

        //Reset the information used to show that the matrix is not up to date.
        // NB: modifiedClusterList / deletedMap — the changedIds tracking — are
        // deliberately NOT cleared here.  Clearing them at *dispatch* discarded a
        // merge's changedIds the instant its compute launched; when the auto-realign
        // then preempted that compute (generation bump) the changes were lost and the
        // next compute saw pending=0 and could only cold-seed a full recompute.  They
        // are now cleared on *accept* (customEvent), when a compute has actually
        // applied them, so pending survives a superseded compute.
        selectedPairs.clear();
        hasBeenRenumbered = false;
        rawCacheRenumberRemapped = false;
        renumbering.clear();
        nbActions = 0;
        nbRedo = 0;
        isNotUpToDate = false;
        nbPreviousUndo = 0;
        nbPreviousRedo = 0;


        drawContentsMode = REDRAW;
    }
}

ErrorMatrixThread* ErrorMatrixView::computeMatrix(){
    // Opt-in incremental error matrix.  Read the environment once.  Default OFF:
    // when disabled the full path is used unchanged.  VERIFY additionally runs the
    // full path and logs the max cell discrepancy (validation aid, slower).
    const bool incrementalEnabled = errorMatrixIncrementalEnabled();
    static const bool incrementalVerify =
        (qEnvironmentVariableIntValue("NS3_ERRORMATRIX_INCREMENTAL_VERIFY") != 0);

    // A renumber remaps cluster ids.  The forward renumber slot remaps the
    // id-keyed cache in place through the old->new map and sets
    // rawCacheRenumberRemapped, so the cache IS trustworthy across that case —
    // skip the defensive invalidate for it.  Undo/redo renumber paths do not
    // remap, so they still fall through to the conservative invalidate.
    if(hasBeenRenumbered && !rawCacheRenumberRemapped)
        invalidateRawProbCache("renumber seen at compute launch (hasBeenRenumbered)");
    rawCacheRenumberRemapped = false;

    // Conservative gate: only take the incremental path when AT MOST ONE edit
    // operation has accumulated since the last matrix update.  nbActions is the
    // net count of cluster-editing actions since updateMatrixContents last reset
    // it, so:
    //   * nbActions == 0  → no-op update, or the cold first compute — nothing to
    //                       reuse incorrectly; incremental just (re)seeds.
    //   * nbActions == 1  → a single edit, which maps cleanly onto
    //                       changedClusterIdsSinceCache(): that one operation's
    //                       modifiedClusterList + deletedMap entries fully and
    //                       unambiguously describe which clusters changed.
    //   * nbActions >= 2  → two or more edits have batched up; the accumulated
    //                       union bookkeeping is harder to trust cluster-for-
    //                       cluster (interleaved merges/splits/moves, transient
    //                       ids), so we bail to the full recompute.  That path
    //                       invalidates the raw cache (customEvent), and the next
    //                       single-edit update cold-seeds it again.
    // The full/GPU path is unchanged in every bailed case.
    //
    // A renumber is NOT one of those independent edits: it is a pure relabel (the
    // id compaction a merge/delete cleanup performs), and since 0100 the raw cache
    // survives it in place, so it changes nothing changedClusterIdsSinceCache()
    // must recompute.  renumbering holds one entry per renumber counted in
    // nbActions, so nbActions - renumbering.size() is the count of genuine editing
    // actions.  Excluding renumbers lets a merge whose auto-realign cleanup
    // renumbers (nbActions=2: the reprojection plus the renumber) read as the
    // single semantic edit it is, and take the incremental path, instead of
    // tripping the 2+-edit bail on a relabel.
    const int  semanticEdits = qMax(0, nbActions - static_cast<int>(renumbering.size()));
    const bool singleEdit = (semanticEdits <= 1);
    // On a COLD cache with no pending edit — startup, or the first refresh after a
    // renumber / 2+-edit bail invalidated the cache — the incremental path would
    // "cold-seed": recompute every column with ZERO reuse.  And it would do that on
    // the CPU, because the incremental path is CPU-only (the GPU path returns
    // already-normalised probabilities, so it cannot emit the raw columns the cache
    // stores).  That is exactly the case the full path wins, since it DOES use the
    // GPU.  Route it to the full/GPU path instead; the raw cache is then seeded
    // lazily by the first actual edit (nbActions==1, still cold), after which reuse
    // begins from the second edit on.  Net effect: fast GPU startup, one CPU
    // cold-seed on the first edit, incremental thereafter.
    // The cached columns belong to whichever clustering was active when they were
    // computed.  A hierarchy op can switch that under us -- groupChildrenIntoFiber
    // (promoting children to a new fiber) calls setActiveClustering(false), so a
    // cache seeded in child scope would be reused against the parent clustering.
    // Nothing downstream can catch it: the two clusterings share their spikes and
    // their .fet, so every geometry check in cacheUsable passes.
    if(rawProbCacheValid && rawProbCacheChildScope != doc.isChildClusteringActive())
        invalidateRawProbCache("clustering scope changed");

    const bool coldSeedRefresh = !rawProbCacheValid && (nbActions == 0);
    const bool useIncremental = incrementalEnabled && singleEdit && !coldSeedRefresh;
    const QSet<int> changedIds = useIncremental ? changedClusterIdsSinceCache()
                                                : QSet<int>();

    // Launch-time diagnostic (gated by NS3_ERRORMATRIX_DIAG): log the gate INPUTS and
    // the chosen path at the moment the compute is dispatched.  The worker's own line
    // reports the OUTCOME (reused / fell back / ms) but not why the path was taken, so
    // "FULL (incremental disabled)" and the fell-back "0 clusters" line cannot be
    // told apart from it.  Reading the active Data here settles the key question: if
    // clusters/spikes are already 0 at launch, the compute was dispatched against an
    // empty clustering (a trigger-side problem) rather than producing empty from good
    // data; nbActions / cacheValid / coldSeed separate a batched-edit bail from a
    // cold-seed refresh.  Cheap and side-effect-free; only the pending-set recompute
    // on the full branch costs anything, and only when the switch is on.
    if(qEnvironmentVariableIntValue("NS3_ERRORMATRIX_DIAG") != 0){
        const int launchClusters = doc.matrixData().nbOfClusters();
        const long long launchSpikes =
            static_cast<long long>(doc.matrixData().totalNbOfSpikes());
        const int pending = useIncremental
            ? static_cast<int>(changedIds.size())
            : static_cast<int>(changedClusterIdsSinceCache().size());
        fprintf(stderr,
            "[errormatrix] launch: clusters=%d spikes=%lld nbActions=%d semEdits=%d "
            "cacheValid=%d renumbered=%d incrementalEnabled=%d coldSeed=%d pending=%d -> %s\n",
            launchClusters, launchSpikes, nbActions, semanticEdits,
            rawProbCacheValid ? 1 : 0, hasBeenRenumbered ? 1 : 0,
            incrementalEnabled ? 1 : 0, coldSeedRefresh ? 1 : 0, pending,
            useIncremental        ? "INCREMENTAL"
            : !incrementalEnabled ? "FULL(disabled)"
            : coldSeedRefresh     ? "FULL(cold-seed refresh)"
            : !singleEdit         ? "FULL(batched >=2 edits)"
            :                       "FULL(?)");
    }

    if (qEnvironmentVariableIsSet("NS3_VERBOSE")) {
        const QList<dataType> ids = doc.matrixData().clusterIds();
        const QList<int> scope = doc.matrixScopeClusters();
        QStringList head; for (int i = 0; i < ids.size() && i < 6; ++i) head << QString::number(ids[i]);
        QStringList sh;   for (int i = 0; i < scope.size() && i < 6; ++i) sh << QString::number(scope[i]);
        int overlap = 0; for (int s : scope) if (ids.contains(static_cast<dataType>(s))) ++overlap;
        qDebug().noquote()
            << "[matrixscope] ErrorMatrixView"
            << " childScopeActive=" << doc.isChildClusteringActive()
            << " scopeActive="      << doc.matrixScopeActive()
            << " scopeParent="      << doc.curatedParent()
            << " | data() clusters=" << ids.size() << "first=[" << head.join(',') << "]"
            << " | scope n=" << scope.size() << "first=[" << sh.join(',') << "]"
            << " | OVERLAP=" << overlap
            << (scope.isEmpty() ? "" : (overlap == 0 ? "   <-- DISJOINT: wrong id space"
                                                     : (overlap < scope.size() ? "   <-- PARTIAL" : "")));
    }
    //The creation of a thread automatically start it.
    return new ErrorMatrixThread(
        *this, doc.matrixData(), generation,
        useIncremental, incrementalVerify,
        (rawProbCacheValid ? rawProbCache : nullptr),
        rawProbCacheIds, rawProbCacheSizes, rawProbCacheDims,
        changedIds, /*seedOnly*/ false, activeFeatureDims(),
        // Scoped matrices: when the child palette is driving and its parent has
        // enough children to be worth comparing, restrict the model to those
        // children.  Empty otherwise, which is the unrestricted behaviour.
        doc.matrixScopeClusters());
}

std::vector<int> ErrorMatrixView::activeFeatureDims()
{
    const QList<int> selection = doc.selectedChannels();
    if (selection.isEmpty()) return {};                 // no restriction

    Data& d = doc.matrixData();
    const int nFeatureDims = d.nbOfDimensionsTotal() - 1;   // timestamp excluded
    const FeatureLayout layout =
        fmResolveLayout(nFeatureDims, d.nbOfchannels(),
                        d.nbOfFeaturesByChannel(), doc.isStderivSession());

    std::vector<int> sel;
    sel.reserve(static_cast<size_t>(selection.size()));
    for (int c : selection) sel.push_back(c);

    if (!layout.valid) {
        // The .fet does not match any layout process_pca can emit, so which
        // column belongs to which channel is unknown.  Masking on a guessed
        // stride would silently corrupt the model: use every dimension instead.
        if (statusBar)
            statusBar->showMessage(
                tr("Error matrix: unrecognised feature layout (%1 dims, %2 channels, "
                   "%3 per channel) — channel selection ignored.")
                    .arg(nFeatureDims).arg(d.nbOfchannels())
                    .arg(d.nbOfFeaturesByChannel()), 6000);
        return {};
    }

    // Channels with no feature columns at all: on a stderiv session
    // process_pca_stderiv drops the last (linearly dependent) channel before the
    // PCA, so selecting it cannot influence anything feature-based.  Say so and
    // carry on with the channels that do have columns.
    const std::vector<int> dead = fmChannelsWithoutFeatures(sel, layout);
    if (!dead.empty() && statusBar) {
        QStringList names;
        for (int c : dead) names << QString::number(c);
        statusBar->showMessage(
            tr("Error matrix: channel%1 %2 carr%3 no feature columns "
               "(dropped before the PCA on this session) — ignored in the "
               "channel selection.")
                .arg(dead.size() > 1 ? QStringLiteral("s") : QString())
                .arg(names.join(QStringLiteral(", ")))
                .arg(dead.size() > 1 ? QStringLiteral("y") : QStringLiteral("ies")),
            8000);
    }

    return fmSelectedDims(sel, layout, nFeatureDims);
}

void ErrorMatrixView::selectedChannelsChanged(const QList<int>&)
{
    if (goingToDie) return;
    // The model's dimensions changed, so every cached column is meaningless.
    // Note the incremental cache only compares prevNbDimensions (a COUNT), so it
    // cannot notice a swap between two different selections of the same size --
    // dropping it here is what keeps that correct.
    invalidateRawProbCache("channel selection changed");
    updateMatrixContents();
}

void ErrorMatrixView::launchCacheWarmer(){
    // Display-less cold-seed: force the incremental path (prevRaw == nullptr, so it
    // reuses nothing and recomputes every raw column — now parallel across cores) and
    // mark it seedOnly so customEvent() installs ONLY the raw cache.  Carries the
    // current generation; an edit that supersedes it (generation bump + stopProcessing
    // in updateMatrixContents) makes customEvent discard its result, and that edit
    // cold-seeds itself.  Deliberately leaves the cursor alone — this is a background
    // task that must neither block nor signal the UI.  Added to threadsToBeKill so it
    // is quiesced with the others before any Data mutation (the stopRunningThreads
    // contract).
    if (qEnvironmentVariableIsSet("NS3_VERBOSE")) {
        const QList<dataType> ids = doc.matrixData().clusterIds();
        const QList<int> scope = doc.matrixScopeClusters();
        QStringList head; for (int i = 0; i < ids.size() && i < 6; ++i) head << QString::number(ids[i]);
        QStringList sh;   for (int i = 0; i < scope.size() && i < 6; ++i) sh << QString::number(scope[i]);
        int overlap = 0; for (int s : scope) if (ids.contains(static_cast<dataType>(s))) ++overlap;
        qDebug().noquote()
            << "[matrixscope] ErrorMatrixView"
            << " childScopeActive=" << doc.isChildClusteringActive()
            << " scopeActive="      << doc.matrixScopeActive()
            << " scopeParent="      << doc.curatedParent()
            << " | data() clusters=" << ids.size() << "first=[" << head.join(',') << "]"
            << " | scope n=" << scope.size() << "first=[" << sh.join(',') << "]"
            << " | OVERLAP=" << overlap
            << (scope.isEmpty() ? "" : (overlap == 0 ? "   <-- DISJOINT: wrong id space"
                                                     : (overlap < scope.size() ? "   <-- PARTIAL" : "")));
    }
    ErrorMatrixThread* warmer = new ErrorMatrixThread(
        *this, doc.matrixData(), generation,
        /*incremental*/ true, /*verify*/ false,
        /*prevRaw*/ nullptr, QList<int>(), QList<int>(), -1,
        /*changedIds*/ QSet<int>(),
        /*seedOnly*/ true, activeFeatureDims(), doc.matrixScopeClusters());
    threadsToBeKill.append(warmer);
}

void ErrorMatrixView::invalidateRawProbCache(const char* reason){
    // Log the invalidations that actually drop a live cache (skip the redundant
    // ones where it was already cold) so a "FULL (cold-seed refresh)" launch can be
    // traced to the exact edit that wiped the cache — the renumber that a merge's
    // cleanup performs is the prime suspect, and it dictates whether the fix is to
    // REMAP the cached ids through the renumber map rather than invalidate.
    if(rawProbCacheValid && qEnvironmentVariableIntValue("NS3_ERRORMATRIX_DIAG") != 0)
        fprintf(stderr, "[errormatrix] raw cache invalidated: %s\n",
                (reason && *reason) ? reason : "unspecified");
    delete rawProbCache;
    rawProbCache = nullptr;
    rawProbCacheIds.clear();
    rawProbCacheSizes.clear();
    rawProbCacheDims = -1;
    rawProbCacheValid = false;
}

QSet<int> ErrorMatrixView::changedClusterIdsSinceCache() const {
    // A cluster's raw column is reusable only if its membership (hence Gaussian
    // model) is unchanged.  modifiedClusterList holds every source cluster
    // touched by an edit; deletedMap keys hold the merge/delete TARGETS whose
    // membership grew.  Their union is the set whose raw columns must be
    // recomputed; every other cluster keeps its cached column.
    QSet<int> changed;
    for(int id : modifiedClusterList) changed.insert(id);
    for(auto it = deletedMap.constBegin(); it != deletedMap.constEnd(); ++it){
        changed.insert(it.key());
        for(int id : it.value()) changed.insert(id);
    }
    return changed;
}

// ---------------------------------------------------------------------------
// recomputeCellWidth — derive cellWidth from the current widget size and the
// number of clusters so the matrix always fills the available space.
// Must be called before updateWindow() uses cellWidth.
// ---------------------------------------------------------------------------
// recomputeCellWidth — pixel-model cell size, capped at CELL_WIDTH and clamped to
// >=4px (copied from TemplateMatrixView::updateCellWidth).  Called when the data
// or the layout inputs change; the pan/zoom transform is applied on top via
// effCellSize()/effMatrixTopLeft().
void ErrorMatrixView::recomputeCellWidth()
{
    const int n = clusterList.size();
    if (n <= 0) { cellWidth = CELL_WIDTH; widthBorder = 5; heightBorder = 14; return; }
    const int matH   = std::max(height() - CONTROLS_H, 1);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int fitW   = (availW > 0) ? availW / n : CELL_WIDTH;
    const int fitH   = (availH > 0) ? availH / n : CELL_WIDTH;
    cellWidth        = std::max(4, std::min({fitW, fitH, CELL_WIDTH}));
    widthBorder      = cellWidth / 3 + 5;
    heightBorder     = cellWidth / 3 + 14;
}

// updateWindow — retained entry point for the data-arrival / resize callers.
// In the pixel model it simply recomputes the base cell size; the pan/zoom
// transform is layered on top by effCellSize()/effMatrixTopLeft().
void ErrorMatrixView::updateWindow(){
    recomputeCellWidth();
    if(drawContentsMode == REFRESH) drawContentsMode = REDRAW;
}

QPoint ErrorMatrixView::matrixTopLeft() const
{
    return QPoint(LABEL_MARGIN + static_cast<int>(widthBorder),
                  static_cast<int>(heightBorder));
}

int ErrorMatrixView::cellAtX(int viewX) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double mx = effMatrixTopLeft().x();
    const int ci = static_cast<int>(std::floor((viewX - mx) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

int ErrorMatrixView::cellAtY(int viewY) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double my = effMatrixTopLeft().y();
    const int ci = static_cast<int>(std::floor((viewY - my) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

// Adaptive minimum zoom (maximum zoom-out): fit the whole grid on screen when it
// overflows even at the zoomMin floor (large n, cellWidth pinned at 4px).
double ErrorMatrixView::effZoomMin() const
{
    const int n = clusterList.size();
    if (n <= 0 || cellWidth <= 0) return zoomMin;
    const int matH   = std::max(height() - CONTROLS_H, 1);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int avail  = std::min(availW, availH);
    if (avail <= 0) return zoomMin;
    const double fitAll = static_cast<double>(avail)
                        / (static_cast<double>(n) * cellWidth);
    return std::min(zoomMin, fitAll);
}

void ErrorMatrixView::zoomAroundPoint(double newZoom, const QPointF& pivot)
{
    newZoom = std::clamp(newZoom, effZoomMin(), zoomMax);
    if (zoom <= 0.0) return;
    const double ratio = newZoom / zoom;
    const QPoint  base = matrixTopLeft();
    panX += (pivot.x() - base.x() - panX) * (1.0 - ratio);
    panY += (pivot.y() - base.y() - panY) * (1.0 - ratio);
    zoom = newZoom;
    // Hold the selected-pair overlay off during the gesture; the settle timer
    // repaints it once the wheel goes quiet.
    suppressPairBoxes = true;
    pairBoxSettleTimer->start(pairBoxSettleMs);
    drawContentsMode = REDRAW;
    update();
    emit viewChanged(zoom, panX, panY);
}

void ErrorMatrixView::resetPanZoom()
{
    panX = panY = 0.0;
    zoom = 1.0;
    drawContentsMode = REDRAW;
    update();
    emit viewChanged(zoom, panX, panY);
}

void ErrorMatrixView::paintEvent ( QPaintEvent*){
    QPainter p(this);
    if(drawContentsMode == REDRAW){
        if(doublebuffer.size() != size())
            doublebuffer = QPixmap(size());
        doublebuffer.fill(palette().color(backgroundRole()));
        QPainter painter(&doublebuffer);
        if(dataReady){
            drawMatrix(painter);
            drawClusterIds(painter);
            if(init){ setMouseTracking(true); init = false; }
        }
        // A recompute no longer wipes the frame: whatever was there stays up and
        // a small badge says fresh numbers are coming.  With nothing to show yet
        // (the first compute) the badge centres itself instead.
        if(computing)
            mbDrawComputingBadge(painter, rect(),
                                 tr("Computing error matrix\u2026"), dataReady);
        painter.end();
        drawContentsMode = REFRESH;
    }
    p.drawPixmap(0, 0, doublebuffer);
}

void ErrorMatrixView::drawClusterIds(QPainter& painter){
    const int n = clusterList.size();
    const QPoint  base = matrixTopLeft();
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();
    const int     w    = std::max(1, static_cast<int>(std::round(eff)));
    const int fontSize = std::max(5, std::min(14,
                            static_cast<int>(std::round(eff / 5.0))));
    QFont f("Helvetica", fontSize);
    painter.setFont(f);
    painter.setPen(colorLegend);

    // Column labels: top strip [0, base.y()], x tracks the cell columns.
    for(int col = 0; col < n; ++col){
        const int px = static_cast<int>(std::round(oriF.x() + col * eff));
        painter.drawText(QRect(px, 0, w, base.y()),
                         Qt::AlignHCenter | Qt::AlignBottom,
                         QString::number(clusterList[displayToMatrix(col)]));
    }
    // Row labels: left strip [0, LABEL_MARGIN-2], y tracks the cell rows.
    for(int row = 0; row < n; ++row){
        const int py = static_cast<int>(std::round(oriF.y() + row * eff));
        painter.drawText(QRect(0, py, LABEL_MARGIN - 2, w),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(clusterList[displayToMatrix(row)]));
    }
}

void ErrorMatrixView::setDisplayOrder(const QList<int>& order)
{
    // Accept only a full permutation of [0, nbClusters); otherwise ignore so a
    // stale/short order can never index out of range at render time.
    const int n = clusterList.size();
    if(order.size() != n){ displayOrder.clear(); update(); return; }
    std::vector<char> seen(static_cast<size_t>(n), 0);
    for(int v : order){
        if(v < 0 || v >= n || seen[static_cast<size_t>(v)]){ displayOrder.clear(); update(); return; }
        seen[static_cast<size_t>(v)] = 1;
    }
    displayOrder = order;
    update();
}

void ErrorMatrixView::resetDisplayOrder()
{
    if(displayOrder.isEmpty()) return;
    displayOrder.clear();
    update();
}

void ErrorMatrixView::drawMatrix(QPainter& painter){
    const int nbClusters = clusterList.size();
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();
    if(!modifiedClusterList.isEmpty() || hasBeenRenumbered || isNotUpToDate){
        //Draw a red rectangle around the matrix to warn the user that
        //the matrix is not up to date anymore.
        QPen pen(Qt::red);
        pen.setWidth(2);
        pen.setStyle(Qt::SolidLine);
        painter.setPen(pen);
        painter.setRenderHints(QPainter::Antialiasing);
        painter.drawRect(QRectF(oriF.x() - 1, oriF.y() - 1,
                                nbClusters * eff + 2, nbClusters * eff + 2));
        painter.setPen(Qt::black);
        painter.setRenderHint(QPainter::Antialiasing, false);
    }

    // O(1) ignore lookup instead of QList::contains() (linear) twice per cell
    // inside the nbClusters^2 loop below — at thousands of clusters the loop is
    // millions of cells, so the linear scans dominated.
    std::vector<char> ignored(static_cast<size_t>(nbClusters) + 2, 0);
    for(int idx : ignoreClusterIndex)
        if(idx >= 0 && idx <= nbClusters + 1)
            ignored[static_cast<size_t>(idx)] = 1;

    // Render the matrix as a single indexed image — one pixel per cell — then
    // blit it scaled to the matrix's world rect.  The previous per-cell
    // painter.drawRect() issued nbClusters^2 calls (≈9M at 3000 clusters) on
    // every REDRAW, i.e. on every pan move and zoom notch; filling an 8-bit
    // image by scanline and drawing it once replaces millions of QPainter calls
    // with plain buffer writes.  Colour computation is unchanged (same colorMap
    // index, same diagonal/ignored -> black).
    if(nbClusters > 0){
        QImage matrixImg(nbClusters, nbClusters, QImage::Format_Indexed8);
        QList<QRgb> table;
        table.reserve(nbColors + 1);
        for(int i = 0; i < nbColors; ++i) table.append(colorMap[i].rgb());
        const int blackIdx = nbColors;                 // diagonal / ignored
        table.append(qRgb(0, 0, 0));
        matrixImg.setColorTable(table);

        for(int r = 0; r < nbClusters; ++r){            // row = display position
            const int ci = displayToMatrix(r) + 1;      // -> 1-based matrix row
            const bool ciIgnored = ignored[static_cast<size_t>(ci)];
            uchar* line = matrixImg.scanLine(r);
            for(int c = 0; c < nbClusters; ++c){        // col = display position
                const int ci2 = displayToMatrix(c) + 1; // -> 1-based matrix col
                if(ci == ci2 || ciIgnored || ignored[static_cast<size_t>(ci2)]){
                    line[c] = static_cast<uchar>(blackIdx);
                } else {
                    int probColorIndex = static_cast<int>(
                        (*probabilities)(ci, ci2) * nbColors / cutoffProbability);
                    if(probColorIndex >= nbColors) probColorIndex = nbColors - 1;
                    line[c] = static_cast<uchar>(probColorIndex);
                }
            }
        }

        const bool prevSmooth =
            painter.testRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false); // crisp cells
        painter.drawImage(QRectF(oriF.x(), oriF.y(),
                                 nbClusters * eff, nbClusters * eff),
                          matrixImg);
        // One-pixel dashed grid so adjacent cells read as separate elements.
        // Full-span lines only; boxing each cell would draw every interior edge
        // twice for the same picture.
        drawMatrixGrid(painter, oriF, eff, nbClusters);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, prevSmooth);
    }

    // Edge-highlight every selected pair (not just the most recent one).  The
    // cell layout mirrors the loop above: column = pair.first cluster (advances
    // x), row = pair.second cluster (advances y), both 0-based offsets from the
    // matrix origin.  A cosmetic pen keeps the outline a constant 2 px at any
    // zoom level.  Pairs whose cluster no longer exists (id -1, or removed) are
    // skipped.
    if(!suppressPairBoxes && !selectedPairs.isEmpty()){
        QPen selPen(Qt::yellow);
        selPen.setWidth(2);
        selPen.setCosmetic(true);
        painter.setPen(selPen);
        painter.setBrush(Qt::NoBrush);
        for(const Pair& selectedPair : selectedPairs){
            const int col = matrixToDisplay(clusterList.indexOf(selectedPair.first));
            const int row = matrixToDisplay(clusterList.indexOf(selectedPair.second));
            if(col < 0 || row < 0)
                continue;
            painter.drawRect(QRectF(oriF.x() + col * eff, oriF.y() + row * eff,
                                    eff, eff));
        }
        painter.setPen(Qt::black);
    }
}


void ErrorMatrixView::initializeColorMap(){
    for(int i = 0;i<nbColors;i++){
        QColor color;
        int x = static_cast<int>(359 * 0.7 * (1 - static_cast<float>(i) / nbColors));
        color.setHsv(x,255,255);
        colorMap.insert(i,color);
    }
}

void ErrorMatrixView::mousePressEvent(QMouseEvent* e){
    // Ctrl + Left arms a pan.  We don't engage until the cursor moves past the
    // drag threshold, so a quick Ctrl-click still reaches the Ctrl-add
    // selection path in mouseReleaseEvent.  A plain press does nothing here;
    // selection happens on release (as it did with the previous empty press).
    if((e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        panArmed    = true;
        panning     = false;
        panAnchorPx = e->position().toPoint();
        panAnchorX  = panX;
        panAnchorY  = panY;
        setCursor(Qt::ClosedHandCursor);
        e->accept();
    }
}

void ErrorMatrixView::mouseMoveEvent(QMouseEvent* e){
    // Pan path (pixel model): Ctrl + Left-drag adds the pixel delta to panX/panY,
    // exactly like TemplateMatrixView.  effMatrixTopLeft() applies it.
    if(panArmed && (e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        const QPoint d = e->position().toPoint() - panAnchorPx;
        if(!panning && (qAbs(d.x()) + qAbs(d.y()) >= panDragThreshold))
            panning = true;
        if(panning){
            panX = panAnchorX + d.x();
            panY = panAnchorY + d.y();
            // Suppress the overlay while the drag is live; settle timer restores it.
            suppressPairBoxes = true;
            pairBoxSettleTimer->start(pairBoxSettleMs);
            drawContentsMode = REDRAW;
            update();
            emit viewChanged(zoom, panX, panY);
        }
        e->accept();
        return;
    }

    //Write the current probability in the statusbar.
    const int col = cellAtX(e->position().toPoint().x());
    const int row = cellAtY(e->position().toPoint().y());
    if(col >= 0 && row >= 0){
        statusBar->showMessage("Clusters (" + QString::number(clusterList[displayToMatrix(row)]) + "," +
                               QString::number(clusterList[displayToMatrix(col)]) + "): p = " +
                               QString::fromLatin1("%1").arg((*probabilities)(displayToMatrix(row) + 1, displayToMatrix(col) + 1)));
    }
}

void ErrorMatrixView::mouseReleaseEvent(QMouseEvent* e){
    // If the Ctrl+Left gesture moved at all it is navigation (zoom/pan), not a
    // selection: swallow the release so it doesn't toggle a cell.  Only a
    // stationary Ctrl-click (no movement) falls through to the Ctrl-add multi-
    // select path below.  (Wheel-zoom never reaches this handler, so it never
    // changes the selection either.)
    if(panArmed){
        const int moved = (e->position().toPoint() - panAnchorPx).manhattanLength();
        panArmed = false;
        panning  = false;
        unsetCursor();
        if(moved >= selectionSuppressMove){
            e->accept();
            return;
        }
    }

    // Notify KlustersApp's last-interacted tracker before any early return:
    // even an empty-matrix click should still mark THIS view as the user's
    // current focus for the Shift+S reorder selection.
    emit viewInteracted();
    if(clusterList.isEmpty())
        return;
    //Select the clusters corresponding to the current cell of the matrix (if they still exist)
    const double eff = effCellSize();
    const QPointF oriF = effMatrixTopLeft();
    int cluster1Index = 0, cluster2Index = 0;
    if(eff > 0.0){
        cluster1Index = qBound(0, static_cast<int>(std::floor((e->position().toPoint().x() - oriF.x()) / eff)), clusterList.count()-1);
        cluster2Index = qBound(0, static_cast<int>(std::floor((e->position().toPoint().y() - oriF.y()) / eff)), clusterList.count()-1);
    }

    int cluster1 = clusterList[displayToMatrix(cluster1Index)];
    int cluster2 = clusterList[displayToMatrix(cluster2Index)];
    Pair pair(cluster1,cluster2);
    QList<int> clustersToShow;
    QList<int> previousSelectedClusters;
    // matrixData(), not data(): when the matrix is scoped these are ATOM ids and
    // data() is the fiber layer, so every cluster would read as "no longer exists"
    // -- or worse, coincide with an unrelated fiber.
    QList<dataType> existingClusters = doc.matrixData().clusterIds();

    //Check if the clusters still exist.
    if(existingClusters.contains(static_cast<dataType>(cluster1))){
        clustersToShow.append(cluster1);
        previousSelectedClusters.append(cluster1);
    }
    else
        pair.first  = -1;

    if(existingClusters.contains(static_cast<dataType>(cluster2))){
        clustersToShow.append(cluster2);
        previousSelectedClusters.append(cluster2);
    }
    else
        pair.second = -1;

    //If the user control click a second time on a cell of the matrix this will deselect the corresponding pair.
    if((e->modifiers() & Qt::ControlModifier) && selectedPairs.contains(pair)){
        selectedPairs.removeAll(pair);
        clustersToShow.clear();
        QList<Pair>::iterator iterator;
        for(iterator = selectedPairs.begin(); iterator != selectedPairs.end(); ++iterator){
            int firstCluster = (*iterator).first;
            int secondCluster = (*iterator).second;

            if(firstCluster != -1 && existingClusters.contains(static_cast<dataType>(firstCluster))){
                clustersToShow.append(firstCluster);
                previousSelectedClusters.append(firstCluster);
            }
            if(secondCluster != -1 && existingClusters.contains(static_cast<dataType>(secondCluster))){
                clustersToShow.append(secondCluster);
                previousSelectedClusters.append(secondCluster);
            }
        }
        doc.selectFromMatrix(clustersToShow, previousSelectedClusters);
    }
    else{
        if(e->modifiers() & Qt::ControlModifier){
            //Store the selected pair
            selectedPairs.append(pair);
            if (doc.matrixScopeActive()) doc.selectFromMatrix(clustersToShow);
            else                        doc.addClustersToActiveView(clustersToShow);
        }
        else{
            selectedPairs.clear();
            //Store the selected pair
            selectedPairs.append(pair);
            doc.selectFromMatrix(clustersToShow);
        }
    }
}

void ErrorMatrixView::wheelEvent(QWheelEvent* e){
    // Ctrl + wheel zooms around the cursor (pixel model, copied from the template
    // matrix view).  Without Ctrl, defer to the base.
    if(!(e->modifiers() & Qt::ControlModifier)){
        ViewWidget::wheelEvent(e);
        return;
    }
    const int delta = e->angleDelta().y();
    if(delta == 0){ e->accept(); return; }
    const double factor = (delta > 0) ? zoomStep : 1.0 / zoomStep;
    zoomAroundPoint(zoom * factor, e->position());
    e->accept();
}

void ErrorMatrixView::mouseDoubleClickEvent(QMouseEvent* e){
    // Double-click resets pan & zoom.
    resetPanZoom();
    e->accept();
}

void ErrorMatrixView::setViewState(double newZoom, double px, double py){
    // Full (zoom + pan) state pushed from the cross-connected template view.  No
    // signal is emitted so the two views do not echo the change back and forth.
    zoom = std::clamp(newZoom, effZoomMin(), zoomMax);
    panX = px;
    panY = py;
    suppressPairBoxes = true;
    pairBoxSettleTimer->start(pairBoxSettleMs);
    drawContentsMode = REDRAW;
    update();
}


void ErrorMatrixView::clustersGrouped(QList<int>& groupedClusters, int newClusterId){
    QList<int> deletedList;

    //Store all the grouped clusters, used in the error matrix,
    //in the modifiedClusterList and ask to redraw the error matrix
    //in order to signal to the user that the error matrix in no more up to date.
    QList<int>::iterator iterator;
    for(iterator = groupedClusters.begin(); iterator != groupedClusters.end(); ++iterator) {
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
            deletedList.append(*iterator);
        }
    }

    deletedMap.insert(newClusterId,deletedList);

    nbActions++;

    drawContentsMode = REDRAW;
}

void ErrorMatrixView::clusterFeaturesReprojected(int clusterId){
    // A nudge or realign reprojected this cluster's spikes onto the PCA basis, so
    // its in-memory .fet features — and therefore its per-spike error-matrix
    // probabilities — changed, even though its membership and id did not.  Mark
    // it modified exactly like a cluster-editing slot would: it then shows as
    // out-of-date AND, crucially, enters changedIds at the next update.  Being in
    // changedIds makes the incremental path recompute this cluster's own column
    // and, via changedSpans, its spikes' rows inside every reused column — so the
    // stale pre-reprojection values are not reused for it.  Membership is
    // unchanged, so no group/split/renumber signal fires for it otherwise.
    if(clusterList.contains(clusterId) && !modifiedClusterList.contains(clusterId))
        modifiedClusterList.append(clusterId);

    nbActions++;

    drawContentsMode = REDRAW;
}

void ErrorMatrixView::clustersDeleted(QList<int>& deletedClusters,int destinationCluster){
    QList<int> deletedList;

    //Store all the deletedClusters clusters, used in the error matrix,
    //in the modifiedClusterList and ask to redraw the error matrix
    //in order to signal to the user that the error matrix in no more up to date.
    QList<int>::iterator iterator;
    for(iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator) {
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
            deletedList.append(*iterator);
        }
    }

    deletedMap.insert(destinationCluster,deletedList);

    nbActions++;

    drawContentsMode = REDRAW;
}

void ErrorMatrixView::removeSpikesFromClusters(QList<int>& fromClusters, int destinationClusterId,QList<int>& emptiedClusters){
    QList<int>::iterator iterator;
    for(iterator = fromClusters.begin(); iterator != fromClusters.end(); ++iterator) {
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
        }
    }

    QList<int> deletedList;
    for(iterator = emptiedClusters.begin(); iterator != emptiedClusters.end(); ++iterator) {
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
            deletedList.append(*iterator);
        }
    }

    deletedMap.insert(destinationClusterId,deletedList);

    nbActions++;
    drawContentsMode = REDRAW;
}

void ErrorMatrixView::newClusterAdded(QList<int>& fromClusters,int clusterId,QList<int>& emptiedClusters){
    QList<int>::iterator iterator;
    for(iterator = fromClusters.begin(); iterator != fromClusters.end(); ++iterator)
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
        }

    QList<int> deletedList;
    for(iterator = emptiedClusters.begin(); iterator != emptiedClusters.end(); ++iterator) {
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
            deletedList.append(*iterator);
        }
    }

    deletedMap.insert(clusterId,deletedList);

    nbActions++;
    drawContentsMode = REDRAW;
}

void ErrorMatrixView::newClustersAdded(QMap<int,int>& fromToNewClusterIds,QList<int>& emptiedClusters){
    QList<int> fromClusters = fromToNewClusterIds.keys();

    //Store all the clusters from where spikes have been taken, used in the error matrix,
    //in the modifiedClusterList and ask to redraw the error matrix
    //in order to signal to the user that the error matrix in no more up to date.
    QList<int>::iterator iterator;
    for(iterator = fromClusters.begin(); iterator != fromClusters.end(); ++iterator) {
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
        }
    }

    nbActions++;
    drawContentsMode = REDRAW;
}


void ErrorMatrixView::newClustersAdded(QList<int>& clustersToRecluster){
    //Store all the automatically reclustered clusters, used in the error matrix,
    //in the modifiedClusterList and ask to redraw the error matrix
    //in order to signal to the user that the error matrix in no more up to date.
    QList<int>::iterator iterator;
    for(iterator = clustersToRecluster.begin(); iterator != clustersToRecluster.end(); ++iterator)
        if(clusterList.contains(*iterator)){
            modifiedClusterList.append(*iterator);
        }

    // A recluster-shape action (watershed / dipSplit / KNN-split) always
    // produces brand-new clusters at the tail and dissolves-or-mutates its
    // source(s).  The matrix can never stay up to date across it, so flag
    // stale unconditionally — mirroring TemplateMatrixView::newClustersAdded.
    //
    // This matters for KNN-split specifically: when the source cluster is
    // only PARTIALLY consumed (some spikes fall below the residual size
    // floor and stay in the source), the doc emits this signal with an
    // EMPTY clustersToRecluster (== emptiedClusters) list — so the loop
    // above appends nothing and, without this line, isNotUpToDate would
    // remain false and the red "out of date" border would never appear
    // even though the source lost spikes and new clusters now exist.
    // nbActions is still incremented so the matching undoAddition() cleanly
    // decrements it and clears the flag on Ctrl+Z.
    isNotUpToDate = true;

    nbActions++;
    drawContentsMode = REDRAW;
}


void ErrorMatrixView::renumber(QMap<int,int>& clusterIdsOldNew){
    hasBeenRenumbered = true;
    // A renumber is a pure relabel: cluster models and spikes are unchanged, so
    // each cached raw column raw_p(s,id) is byte-identical under the new id — only
    // its column label moves.  Remap the id-keyed column labels in place through
    // the old->new map (ids absent from the map keep their id) instead of
    // discarding a still-valid cache, and flag that the renumber was absorbed so
    // the launch gate does not re-invalidate defensively.  This id compaction is
    // what a merge's cleanup performs; discarding the cache here is precisely what
    // forced every post-merge matrix to a full GPU recompute.
    if(rawProbCacheValid){
        for(int& id : rawProbCacheIds)
            id = clusterIdsOldNew.value(id, id);
        rawCacheRenumberRemapped = true;
    }
    // The changedIds tracking is now retained across a preempted compute, so at a
    // renumber it can still hold pre-renumber ids — remap modifiedClusterList and
    // the deletedMap keys/values through the same old->new map so
    // changedClusterIdsSinceCache() resolves against the post-renumber clusters.
    for(int& id : modifiedClusterList)
        id = clusterIdsOldNew.value(id, id);
    if(!deletedMap.isEmpty()){
        QMap<int,QList<int> > remappedDeleted;
        for(auto it = deletedMap.constBegin(); it != deletedMap.constEnd(); ++it){
            QList<int> vals;
            for(int v : it.value())
                vals.append(clusterIdsOldNew.value(v, v));
            remappedDeleted.insert(clusterIdsOldNew.value(it.key(), it.key()), vals);
        }
        deletedMap.swap(remappedDeleted);
    }
    nbActions++;
    renumbering.insert(nbActions,true);
    drawContentsMode = REDRAW;
}

void ErrorMatrixView::undoRenumbering(QMap<int,int>& clusterIdsNewOld){
    if(nbActions > 0){
        renumbering.remove(nbActions);
        if(renumbering.isEmpty())
            hasBeenRenumbered = false;

        nbActions--;
        nbRedo++;
        if(nbActions == 0)
            isNotUpToDate = false;
        else
            isNotUpToDate = true;
        drawContentsMode = REDRAW;
    }
    else{
        if(nbPreviousUndo == 0) {
            nbPreviousRedo++;
            isNotUpToDate = true;
        } else if(nbPreviousUndo == 1) {//There was a redo before
            nbPreviousUndo--;
            isNotUpToDate = false;
        } else {//nbPreviousUndo >1
            nbPreviousUndo--;
            nbPreviousRedo++;
            isNotUpToDate = true;
        }
        drawContentsMode = REDRAW;
    }
}

void ErrorMatrixView::undoAdditionModification(QList<int>& addedClusters,QList<int>& updatedClusters){
    if(nbActions > 0){
        nbActions--;
        nbRedo++;
        isNotUpToDate = false;

        QList<int>::iterator iterator;
        for(iterator = updatedClusters.begin(); iterator != updatedClusters.end(); ++iterator){

            if(modifiedClusterList.contains(*iterator)){
                modifiedClusterList.removeAll(*iterator);
            }

            if(deletedMap.contains(*iterator)){
                QList<int> deletedList = deletedMap[*iterator];
                QList<int>::iterator iterator2;
                for(iterator2 = deletedList.begin(); iterator2 != deletedList.end(); ++iterator2)
                    modifiedClusterList.removeAll(*iterator2);
            }
        }

        drawContentsMode = REDRAW;
    }
    else{
        if(nbPreviousUndo == 0){
            nbPreviousRedo++;
            isNotUpToDate = true;
        }
        else if(nbPreviousUndo == 1){//There was a redo before
            nbPreviousUndo--;
            isNotUpToDate = false;
        }
        else{//nbPreviousUndo >1
            nbPreviousUndo--;
            nbPreviousRedo++;
            isNotUpToDate = true;
        }
        drawContentsMode = REDRAW;
    }
}


void ErrorMatrixView::undoAddition(QList<int>& addedClusters){
    if(nbActions > 0){
        nbActions--;
        nbRedo++;
        isNotUpToDate = false;

        QList<int>::iterator iterator;
        for(iterator = addedClusters.begin(); iterator != addedClusters.end(); ++iterator){
            if(deletedMap.contains(*iterator)){
                QList<int> deletedList = deletedMap[*iterator];
                QList<int>::iterator iterator2;
                for(iterator2 = deletedList.begin(); iterator2 != deletedList.end(); ++iterator2)
                    modifiedClusterList.removeAll(*iterator2);

                drawContentsMode = REDRAW;
            }
        }
    }
    else{
        if(nbPreviousUndo == 0){
            nbPreviousRedo++;
            isNotUpToDate = true;
        }
        else if(nbPreviousUndo == 1){//There was a redo before
            nbPreviousUndo--;
            isNotUpToDate = false;
        }
        else{//nbPreviousUndo >1
            nbPreviousUndo--;
            nbPreviousRedo++;
            isNotUpToDate = true;
        }
        drawContentsMode = REDRAW;
    }
}


void ErrorMatrixView::undoModification(QList<int>& updatedClusters){
    if(nbActions > 0){
        nbActions--;
        nbRedo++;
        isNotUpToDate = false;

        QList<int>::iterator iterator;
        for(iterator = updatedClusters.begin(); iterator != updatedClusters.end(); ++iterator){
            if(modifiedClusterList.contains(*iterator)){
                modifiedClusterList.removeAll(*iterator);
            }
            if(deletedMap.contains(*iterator)){
                QList<int> deletedList = deletedMap[*iterator];
                QList<int>::iterator iterator2;
                for(iterator2 = deletedList.begin(); iterator2 != deletedList.end(); ++iterator2){
                    modifiedClusterList.removeAll(*iterator2);
                }
            }
        }
        drawContentsMode = REDRAW;
    }
    else{
        if(nbPreviousUndo == 0){
            nbPreviousRedo++;
            isNotUpToDate = true;
        }
        else if(nbPreviousUndo == 1){//There was a redo before
            nbPreviousUndo--;
            isNotUpToDate = false;
        }
        else{//nbPreviousUndo >1
            nbPreviousUndo--;
            nbPreviousRedo++;
            isNotUpToDate = true;
        }
        drawContentsMode = REDRAW;
    }
}


void ErrorMatrixView::redoRenumbering(QMap<int,int>& clusterIdsOldNew){
    if(nbPreviousRedo == 1){
        nbPreviousRedo--;
        isNotUpToDate = false;
        drawContentsMode = REDRAW;
    }
    else if (nbPreviousRedo > 1){
        nbPreviousRedo--;
        nbPreviousUndo++;
        isNotUpToDate = true;
        drawContentsMode = REDRAW;
    }
    else if(nbActions > 0 || (nbActions == 0 && nbRedo > 0)){
        nbActions++;
        nbRedo--;
        isNotUpToDate = false;

        renumbering.insert(nbActions,true);
        hasBeenRenumbered = true;
        drawContentsMode = REDRAW;
    }
    else if(nbPreviousRedo == 0){//there was no previous undo
        isNotUpToDate = true;
        nbPreviousUndo++;
        drawContentsMode = REDRAW;
    }
}


void ErrorMatrixView::redoAdditionModification(QList<int>& addedClusters,QList<int>& modifiedClusters,bool isModifiedByDeletion,QList<int>& deletedClusters){
    if(nbPreviousRedo == 1){
        nbPreviousRedo--;
        isNotUpToDate = false;
        drawContentsMode = REDRAW;
    }
    else if (nbPreviousRedo > 1){
        nbPreviousRedo--;
        nbPreviousUndo++;
        isNotUpToDate = true;
        drawContentsMode = REDRAW;
    }
    else if(nbActions > 0 || (nbActions == 0 && nbRedo > 0)){
        nbActions++;
        nbRedo--;
        isNotUpToDate = false;

        QList<int>::iterator iterator;
        for(iterator = modifiedClusters.begin(); iterator != modifiedClusters.end(); ++iterator)
            if(clusterList.contains(*iterator)){
                modifiedClusterList.append(*iterator);
            }

        for(iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
            if(clusterList.contains(*iterator)){
                modifiedClusterList.append(*iterator);
            }

        drawContentsMode = REDRAW;
    }
    else if(nbPreviousRedo == 0){//there was no previous undo
        isNotUpToDate = true;
        nbPreviousUndo++;
        drawContentsMode = REDRAW;
    }
}


void ErrorMatrixView::redoAddition(QList<int>& addedClusters,QList<int>& deletedClusters){
    if(nbPreviousRedo == 1){
        nbPreviousRedo--;
        isNotUpToDate = false;
        drawContentsMode = REDRAW;
    }
    else if (nbPreviousRedo > 1){
        nbPreviousRedo--;
        nbPreviousUndo++;
        isNotUpToDate = true;
        drawContentsMode = REDRAW;
    }
    else if(nbActions > 0 || (nbActions == 0 && nbRedo > 0)){
        nbActions++;
        nbRedo--;
        // Re-applying an addition (group → new cluster, or any recluster-
        // shape split) always re-creates clusters, so the matrix is stale
        // again afterwards.  This previously set isNotUpToDate = false and
        // relied solely on the deletedClusters loop below to repopulate
        // modifiedClusterList — but that loop appends nothing when
        // deletedClusters is empty, which is exactly the case for a
        // KNN-split whose source survives (partially consumed).  Without
        // this, Ctrl+Y would leave the matrix falsely "up to date".
        // Flagging stale unconditionally is the symmetric counterpart of
        // newClustersAdded() setting the flag on the forward action and is
        // always safe — a recompute is never wrong, only (for the
        // consumed-source case the loop already covered) redundant.
        isNotUpToDate = true;

        QList<int>::iterator iterator;
        for(iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
            if(clusterList.contains(*iterator)){
                modifiedClusterList.append(*iterator);
            }

        drawContentsMode = REDRAW;
    }
    else if(nbPreviousRedo == 0){//there was no previous undo
        isNotUpToDate = true;
        nbPreviousUndo++;
        drawContentsMode = REDRAW;
    }
}


void ErrorMatrixView::redoModification(QList<int>& updatedClusters,bool isModifiedByDeletion,QList<int>& deletedClusters){
    if(nbPreviousRedo == 1){
        nbPreviousRedo--;
        isNotUpToDate = false;
        drawContentsMode = REDRAW;
    }
    else if (nbPreviousRedo > 1){
        nbPreviousRedo--;
        nbPreviousUndo++;
        isNotUpToDate = true;
        drawContentsMode = REDRAW;
    }
    else if(nbActions > 0 || (nbActions == 0 && nbRedo > 0)){
        nbActions++;
        nbRedo--;
        isNotUpToDate = false;

        QList<int>::iterator iterator;
        for(iterator = updatedClusters.begin(); iterator != updatedClusters.end(); ++iterator)
            if(modifiedClusterList.contains(*iterator)) modifiedClusterList.removeAll(*iterator);
            else if(clusterList.contains(*iterator)){
                modifiedClusterList.append(*iterator);
            }

        for(iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
            if(clusterList.contains(*iterator)){
                modifiedClusterList.append(*iterator);
            }

        drawContentsMode = REDRAW;
    }
    else if(nbPreviousRedo == 0){//there was no previous undo
        isNotUpToDate = true;
        nbPreviousUndo++;
        drawContentsMode = REDRAW;
    }
}


void ErrorMatrixView::redoDeletion(QList<int>& deletedClusters){
    if(nbPreviousRedo == 1){
        nbPreviousRedo--;
        isNotUpToDate = false;
        drawContentsMode = REDRAW;
    }
    else if (nbPreviousRedo > 1){
        nbPreviousRedo--;
        nbPreviousUndo++;
        isNotUpToDate = true;
        drawContentsMode = REDRAW;
    }
    else if(nbActions > 0 || (nbActions == 0 && nbRedo > 0)){
        nbActions++;
        nbRedo--;
        isNotUpToDate = false;

        QList<int>::iterator iterator;
        for(iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
            if(clusterList.contains(*iterator)){
                modifiedClusterList.append(*iterator);
            }

        drawContentsMode = REDRAW;
    }
    else if(nbPreviousRedo == 0){//there was no previous undo
        isNotUpToDate = true;
        nbPreviousUndo++;
        drawContentsMode = REDRAW;
    }
}

void ErrorMatrixView::willBeKilled(){
    if(!goingToDie){
        goingToDie = true;
        //inform the running threads to stop processing as soon as possible.
        for(ErrorMatrixThread* errorMatrixThread : threadsToBeKill) {
            errorMatrixThread->stopProcessing();
        }
    }
}

void ErrorMatrixView::print(QPainter& printPainter,int width,int height, bool whiteBackground){
    Q_UNUSED(width); Q_UNUSED(height);
    // Pixel model: the on-screen doublebuffer holds the rendered matrix at the
    // current zoom/pan.  Re-render it (optionally on a white background for
    // print), then scale-blit it into the printer viewport preserving aspect.
    QColor   legendTmp = colorLegend;
    QPalette palTmp    = palette();
    if(whiteBackground){
        colorLegend = Qt::black;
        QPalette pal = palTmp;
        pal.setColor(backgroundRole(), Qt::white);
        setPalette(pal);
    }
    drawContentsMode = REDRAW;
    repaint();                      // synchronous paintEvent -> refresh doublebuffer

    const QRect vp = printPainter.viewport();
    if(!doublebuffer.isNull()){
        QSize sz = doublebuffer.size();
        sz.scale(vp.size(), Qt::KeepAspectRatio);
        const QRect target(vp.left() + (vp.width()  - sz.width())  / 2,
                           vp.top()  + (vp.height() - sz.height()) / 2,
                           sz.width(), sz.height());
        printPainter.drawPixmap(target, doublebuffer, doublebuffer.rect());
    }

    if(whiteBackground){
        colorLegend = legendTmp;
        setPalette(palTmp);
        drawContentsMode = REDRAW;
        repaint();                  // restore on-screen appearance
    }
}

