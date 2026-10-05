#include <algorithm>
#include <QApplication>
/***************************************************************************
                          waveformview.cpp  -  description
                             -------------------
    begin                : Fri Sep 26 2003
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
#include "klustersview.h"
#include "klustersdoc.h"
#include "data.h"
#include "itemcolors.h"
#include "waveformview.h"
#include <QThread>   // msleep for the synchronous job quiesce
#include "waveformthread.h"
#include "types.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// include files for Qt
#include <QPaintDevice>
#include <QPolygon>
#include <QCursor>


#include <QList>

#include <QResizeEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <cmath>


const int WaveformView::XMARGIN = 0;
const int WaveformView::YMARGIN = 0;

WaveformView::WaveformView(KlustersDoc& doc,KlustersView& view,const QColor& backgroundColor,int acquisitionGain,const QList<int>& positions,QStatusBar * statusBar,QWidget* parent,
                           bool isTimeFrameMode,long start,long timeFrameWidth,long nbSpkToDisplay,
                           bool overLay,bool mean, const char* name,int minSize, int maxSize, int windowTopLeft ,int windowBottomRight,
                           int border) :
    ViewWidget(doc,view,backgroundColor,statusBar,parent,name,minSize,maxSize,windowTopLeft,windowBottomRight,border,XMARGIN,YMARGIN)
  ,meanPresentation(mean),overLayPresentation(overLay),acquisitionGain(acquisitionGain),
    jobToken(std::make_shared<KlustersJobToken>()),dataReady(true),
    nbSpkToDisplay(nbSpkToDisplay),isZoomed(false),goingToDie(false){

    //Set the default modes
    mode = ZOOM;
    if(isTimeFrameMode)
        presentationMode = TIME_FRAME;
    else
        presentationMode = SAMPLE;

    //Set the drawing variables
    Data& clusteringData = doc.data();
    nbSamplesInWaveform = clusteringData.nbOfSampleInWaveform();
    peakPositionInWaveform = clusteringData.positionOfPeakInWaveform();
    nbchannels = clusteringData.nbOfchannels();
    widthBorder = 0;
    heightBorder = 20;
    Xstep = 10;
    Xspace = 30;
    shift = (nbSamplesInWaveform - 1) * Xstep + Xspace;
    Yspace = 40;
    YsizeForMaxAmp = 100;
    // Ctrl release commits the channel selection; see eventFilter().
    qApp->installEventFilter(this);
    Yfactor = static_cast<float>(YsizeForMaxAmp)/static_cast<float>(acquisitionGain);
    gain = 0;

    channelPositions.resize(nbchannels);
    if(positions.isEmpty()) {
        for(int i = 0; i < nbchannels; ++i)
            channelPositions[i] = i;
    } else {
        for(int i = 0; i < nbchannels; ++i)
            channelPositions[i] = positions.at(i);
    }

    ordinateMin = -(2 * heightBorder + nbchannels * YsizeForMaxAmp + (nbchannels - 1) * Yspace);
    ordinateMax = 2 * Yspace;
    abscissaMin = 0;

    X0 = widthBorder;
    Y0 = nbchannels * YsizeForMaxAmp + (nbchannels - 1) * Yspace + heightBorder - (YsizeForMaxAmp/2);

    isTwoBytesRecording = clusteringData.isRecordingTwoBytes();
    startTime = start;
    endTime = start + timeFrameWidth;

    maximumTime = clusteringData.maxTime();

    updateWindow();

    //Set the cursor shap to a magnifier as the only action allowed on the widget is to zoom.
    setCursor(zoomCursor);
}

WaveformView::~WaveformView(){
    //Supersede the in-flight jobs (and set goingToDie so nothing new launches).
    //Qualified (non-virtual) call: in its own destructor the object is already
    //this dynamic type, so this is the intended teardown; the explicit scope
    //documents that and silences the virtual-call-in-destructor warning.
    WaveformView::willBeKilled();

    //Fence the completion posts: once viewDead is set under the lock, no job
    //will post to this view again (jobs check it under the same lock before
    //posting).  The jobs themselves are not waited for: they were superseded
    //above (so they retire within one poll interval), the pool owns and
    //deletes them, and the only state they share with us beyond the token is
    //Data, which the document keeps alive until it drains the pool.  This
    //removes the old per-thread wait() that stalled tab close for a second
    //per sleeping loader -- and, on heavily over-clustered sessions, for
    //minutes.
    {
        QMutexLocker lock(&jobToken->postMutex);
        jobToken->viewDead = true;
    }

    // Remove any events that jobs posted to us before the fence.
    // Without this, Qt may dispatch those events after our destruction → crash.
    QApplication::removePostedEvents(this);
}

WaveformThread* WaveformView::getWaveforms(){
    return new WaveformThread(*this,doc.data(),jobToken);
}

bool WaveformView::isThreadsRunning() const{
    return jobToken->active.load(std::memory_order_acquire) > 0;
}

void WaveformView::singleColorUpdate(int clusterId,bool active){
    if(active){
        //Add the the cluster id to the clusterUpdateList,
        // so it will be updated during the next update
        if(drawContentsMode == REFRESH){
            clusterUpdateList.append(clusterId);
            drawContentsMode = UPDATE;
        }
        else if(drawContentsMode == UPDATE)
            clusterUpdateList.append(clusterId);
    }
    else{
        //Update drawContentsMode if need it.
        if(drawContentsMode == REFRESH || drawContentsMode == UPDATE)
            drawContentsMode = REDRAW;
    }
}

void WaveformView::askForWaveformInformation(int clusterId){
    //If the widget is not about to be deleted, request the data.
    if(!goingToDie){
        dataReady = false;
        //Enqueue a job to get the waveform data for that cluster.  Requests
        //beyond the pool's worker count wait in the pool as inert job objects
        //(no thread, no descriptors), which is what the view-local cap-queue
        //used to approximate; a request superseded while waiting early-outs
        //when its turn comes.
        WaveformThread* waveformThread = getWaveforms();
        waveformThread->getWaveformInformation(clusterId,presentationMode);
    }
}

void WaveformView::askForWaveformInformation(const QList<int> &clusterIds){
    //If the widget is not about to be deleted, request the data.
    if(!goingToDie){
        // Supersede any in-flight jobs before launching a new full-redraw
        // request.  Every existing call site (removeClusterFromView,
        // spikesAddedToCluster, navigation, etc.) that reaches this
        // overload is replacing the entire waveform display, so a stale
        // completion arriving afterwards would draw over it out of order —
        // the generation bump fences those (queued ones are removed, late
        // ones die on the customEvent guard).  No wait is owed: a doomed
        // job reads only its captured snapshot, and if it is inside a store
        // load it completes that load epoch-valid — parked waiters are
        // served, no in-process marker can be stranded (epoch-snapshot
        // step 8 follow-up; the blocking twin remains stopAndClearThreads).
        // The single-cluster overload (overlay mode) is intentionally left
        // alone.
        supersedeRunningThreads();
        dataReady = false;
        //Enqueue a job to get the waveform data for those clusters.
        WaveformThread* waveformThread = getWaveforms();
        waveformThread->getWaveformInformation(clusterIds,presentationMode);
    }
}


void WaveformView::addClusterToView(int clusterId,bool active){
    isZoomed = false;//Hack because all the tabs share the same data.

    // Supersede any in-flight jobs before launching new ones.
    // Mirrors the invariant in setSampleMode, setTimeFrameMode, etc.
    // (The historical accumulate-sleeping-threads concern is gone: doomed
    // jobs hold no thread while queued and early-out at their next check.)
    if (active) supersedeRunningThreads();

    if(active && overLayPresentation){
        if(drawContentsMode == REFRESH){
            clusterUpdateList.append(clusterId);
            drawContentsMode = UPDATE;
        }
        else if(drawContentsMode == UPDATE)clusterUpdateList.append(clusterId);

        setCursor(Qt::WaitCursor);
        //Create a thread to get the waveform data for that cluster specifying the
        //triggering action.
        askForWaveformInformation(clusterId);
    }
    else if(active){
        //Update drawContentsMode if need it.
        if(drawContentsMode == REFRESH || drawContentsMode == UPDATE)drawContentsMode = REDRAW;

        //The data have to be collected, if need it, for all the clusters.
        if(active && !view.clusters().isEmpty()){
            setCursor(Qt::WaitCursor);
            askForWaveformInformation(view.clusters());
        }
    }
    else{
        //Update drawContentsMode if need it.
        if(drawContentsMode == REFRESH || drawContentsMode == UPDATE)drawContentsMode = REDRAW;
    }
}

void WaveformView::removeClusterFromView(int clusterId,bool active){ 
    isZoomed = false;//Hack because all the tabs share the same data.

    //Update drawContentsMode if need it.
    if(drawContentsMode == REFRESH || drawContentsMode == UPDATE)drawContentsMode = REDRAW;

    //The data have to be collected, if need it, for all the clusters.
    if(active && !view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::addNewClusterToView(QList<int>& fromClusters,int clusterId,bool active){

    isZoomed = false;//Hack because all the tabs share the same data.

    //Update drawContentsMode if need it.
    if(drawContentsMode == REFRESH || drawContentsMode == UPDATE)drawContentsMode = REDRAW;

    //The data have to be collected, if need it, for all the clusters.
    if(active && !view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}


void WaveformView::spikesRemovedFromClusters(QList<int>& fromClusters,bool active){
    isZoomed = false;//Hack because all the tabs share the same data.

    //Update drawContentsMode if need it.
    if(drawContentsMode == REFRESH || drawContentsMode == UPDATE)drawContentsMode = REDRAW;

    //The data have to be collected, if need it, for all the clusters.
    if(active && !view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::spikesAddedToCluster(int clusterId,bool active){  
    isZoomed = false;//Hack because all the tabs share the same data.

    // Supersede any in-flight jobs before launching new ones.
    // Every other launch path (setSampleMode, setMeanPresentation, etc.)
    // does the same; this path was historically the only exception, back
    // when jobs slept in IN_PROCESS retry loops and raced on a live shared
    // cache (the old segfault).  Both are structurally gone: jobs subscribe
    // instead of sleeping and read per-epoch stores.
    supersedeRunningThreads();

    //Update drawContentsMode if need it.
    if(drawContentsMode == REFRESH || drawContentsMode == UPDATE)drawContentsMode = REDRAW;

    //The data have to be collected, if need it, for all the clusters.
    if(active && !view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::customEvent(QEvent *event){
    //Event sent by a waveform job to inform that the data are available.
    if(event->type() == QEvent::User + 200){
        WaveformThread::GetWaveformsEvent* waveformsEvent = static_cast<WaveformThread::GetWaveformsEvent*>(event);

        // Guard: results of a superseded request generation are stale — a
        // stopAndClearThreads() (or willBeKilled()) ran after the job was
        // enqueued, and whoever bumped the generation launched requests for
        // the current view state.  The job retired itself, so there is
        // nothing to clean up.  This replaces the old threadsToBeKill
        // membership check.
        if(waveformsEvent->generation() != jobToken->generation.load(std::memory_order_acquire))
            return;

        if(goingToDie)
            return;

        const bool meanRequested = waveformsEvent->isMeanRequested();
        // Use the value snapshotted when the job was enqueued, not the current
        // live view field (which may have changed while the job was running).
        const bool launchedWithMean = waveformsEvent->wasLaunchedWithMeanPresentation();

        //The data have been retrieved but the means and standard deviations
        //have not been calculated (a plain fetch while the view showed the
        //waveforms themselves): enqueue a follow-up job to calculate them now,
        //which will speed up the next call to a mean presentation.
        if(!launchedWithMean && !meanRequested){
            WaveformThread* meanJob = getWaveforms();
            if(waveformsEvent->isSingleTriggeringCluster())
                meanJob->getMean(waveformsEvent->triggeringCluster(),waveformsEvent->snapshotMode());
            else
                meanJob->getMean(waveformsEvent->triggeringClusters(),waveformsEvent->snapshotMode());
        }

        //Redraw, except when this completion is the silent mean precomputation
        //launched above: its waveforms were already drawn when the fetch that
        //spawned it completed, and its means are only cached for later.
        if(launchedWithMean || !meanRequested){
            //Each time a cluster is added to the view or modified, the size of the window is recalculated.
            if(!isZoomed) updateWindow();
            else drawContentsMode = REDRAW;

            dataReady = true;
            //setCursor(zoomCursor);
            //Update the widget
            update();
        }
    }
    //Event sent by a waveform job to inform that the data are not available
    //for the cluster requested (e.g. it was suppressed after the job was
    //enqueued).  The job retired itself; nothing to clean up.
    if(event->type() == QEvent::User + 250){
        WaveformThread::NoWaveformDataEvent* noDataEvent = static_cast<WaveformThread::NoWaveformDataEvent*>(event);
        // Stale or not makes no difference today, but keep the guard so any
        // future handling inherits it.
        if(noDataEvent->generation() != jobToken->generation.load(std::memory_order_acquire))
            return;
    }
}

void WaveformView::paintEvent ( QPaintEvent *){
    QPainter p(this);
    if((drawContentsMode == UPDATE || drawContentsMode == REDRAW) && dataReady){
        QRect contentsRec = contentsRect();
        viewport = QRect(contentsRec.left(),contentsRec.top(),contentsRec.width(),contentsRec.height() /*- 10*/);

        //Resize the double buffer with the width and the height of the widget(QFrame)
        if (viewport.size() != doublebuffer.size()) {
            if(!doublebuffer.isNull()) {
                QPixmap tmp = QPixmap( viewport.width() +10 ,viewport.height() +10 );
                tmp.fill( Qt::white );
                QPainter painter2( &tmp );
                painter2.drawPixmap( 0,0, doublebuffer );
                painter2.end();
                doublebuffer = tmp;
            } else {
                doublebuffer = QPixmap(viewport.width() + 10 ,viewport.height() +10);
            }
        }


        //Create a painter to paint on the double buffer
        QPainter painter;
        painter.begin(&doublebuffer);

        //set the window (part of the world I want to show)
        QRect r((QRect)window);
        painter.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function

        //Set the viewport (part of the device I want to write on).
        //By default, the viewport is the same as the device's rectangle (contentsRec), taking a smaller
        //one will ensure that the legends (cluster ids) will not ovelap to much a waveform.
        painter.setViewport(viewport);

        if(drawContentsMode == REDRAW){
            //Fill the double buffer with the background
            doublebuffer.fill(palette().color(backgroundRole()));

            if(hasResidualPreview_){
                //A lasso is pending confirmation: show its residual preview in
                //place of the cluster waveforms until it is applied or cancelled.
                drawResidualPreview(painter);
            } else {
                //Shade the selected channels.
                drawChannelSelection(painter);

                //Review mode: a faint primary-template underlay BEHIND the waveforms.
                if(hasTemplatePreview_ && !tpEdit_) drawTemplatePreview(painter);

                //Paint all the waveforms in the shownclusters list (in the double buffer).
                drawWaveforms(painter,view.clusters());

                //Edit mode (lineage overlay): the active template's mean±std band is
                //overlaid translucently ON TOP of the spikes, with the shown cluster's
                //own mean±std as a grey reference band — so the curator reads the model
                //against the actual waveforms instead of the spikes being hidden.
                if(hasTemplatePreview_ && tpEdit_){
                    drawTemplateBand(painter);
                    drawTemplatePreview(painter);
                }
            }
        }

        //The update mode applies only when the color of a cluster has changed.
        if(drawContentsMode == UPDATE && !hasResidualPreview_ && !(hasTemplatePreview_ && tpEdit_)){
            //Paint the waveforms for the clusters contained in clusterUpdateList
            drawWaveforms(painter,clusterUpdateList);

            //Reset the clusterUpdateList list for the next call
            clusterUpdateList.clear();
        }

        //reset transformation due to setWindow and setViewport
        painter.resetTransform() ;

        //Draw the cluster Ids below the waveforms if they are not in overlay presentation.
        if(!overLayPresentation)
            drawClusterIds(painter);

        //Closes the painter on the double buffer
        painter.end();

        //Back to the default
        drawContentsMode = REFRESH;
    }
    //if drawContentsMode == REFRESH, we reuse the double buffer (pixmap)

    //Draw the double buffer (pixmap) by copying it into the widget device.
    p.drawPixmap(0, 0, doublebuffer);
    setCursor(zoomCursor);
}


void WaveformView::drawWaveforms(QPainter& painter,const QList<int>& clusterList){
    //the clusters will be presented by increasing ids, to do so the lists have to be sorted.
    QList<int> shownClusters;
    QList<int>::const_iterator iterator;
    QList<int> const clusters = view.clusters();
    for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator)
        shownClusters.append(*iterator);

    std::sort(shownClusters.begin(), shownClusters.end());

    QList<int> clusterListSorted;
    for(iterator = clusterList.begin(); iterator != clusterList.end(); ++iterator)
        clusterListSorted.append(*iterator);

    std::sort(clusterListSorted.begin(), clusterListSorted.end());

    //Loop on the clusters to be drawn
    QList<int>::const_iterator clusterIterator;

    ItemColors& clusterColors = doc.clusterColors();
    Data& clusteringData = doc.data();

    //The abscissa of the system coordinate center for the current cluster
    int X = X0;
    //If it is an update that means that there some clusters to redraw.
    //If the presentation is side by side and the position of those clusters in the drawing
    //has to been find in order to redraw them at the correct position.
    bool specificPosition = false;
    if(drawContentsMode == UPDATE && !overLayPresentation)specificPosition = true;

    //The ordinate of the system coordinate center for the current channel of the current cluster
    int Y;

    int clusterShift;
    if(overLayPresentation) clusterShift = 0;
    else clusterShift = shift;

    for(clusterIterator = clusterListSorted.begin(); clusterIterator != clusterListSorted.end(); ++clusterIterator){
        if(specificPosition){
            int index = shownClusters.indexOf(*clusterIterator);
            X = X0 + index * shift;
        }
        //Get the color associated with the cluster and set the color to use to this color
        QPen pen(clusterColors.color(*clusterIterator));
        painter.setPen(pen);
        //Get the iterator on the spikes of the current cluster
        Data::WaveformIterator* waveformIterator;

        if(presentationMode == SAMPLE) waveformIterator = clusteringData.sampleWaveformIterator(static_cast<dataType>(*clusterIterator),nbSpkToDisplay);
        else waveformIterator = clusteringData.timeFrameWaveformIterator(static_cast<dataType>(*clusterIterator),startTime,endTime);

        //Iterate over the waveforms of the cluster and draw them
        if(meanPresentation){
            if(!waveformIterator->isMeanAvailable()) continue;
            int x = 0;
            QPolygon mean;
            QPolygon max;
            QPolygon min;
            for(int i = 0; i < nbSamplesInWaveform; ++i){
                for(int j = 0; j < nbchannels; ++j){
                    Y = Y0 - channelPositions[j] * (YsizeForMaxAmp + Yspace);
                    long meanValue = waveformIterator->nextMeanValue();
                    //The point is drawn in the QT coordinate system where the Y axis in oriented downwards
                    //The value receive from the iterator is already inverted.
                    mean.putPoints((j*nbSamplesInWaveform) + i, 1, X + x,-Y + static_cast<long>(meanValue * Yfactor));
                    long stDeviation = waveformIterator->nextStDeviationValue();
                    min.putPoints((j*nbSamplesInWaveform) + i, 1, X + x,-Y + static_cast<long>((meanValue - stDeviation) * Yfactor));
                    max.putPoints((j*nbSamplesInWaveform) + i, 1, X + x,-Y + static_cast<long>((meanValue + stDeviation) * Yfactor));
                }
                x += Xstep;
            }
            for(int k = 0;k < nbchannels;++k){
                //Draw the mean with a solid line
                pen.setStyle(Qt::SolidLine);
                painter.setPen(pen);

                int pointCount = (nbSamplesInWaveform == -1) ?  mean.size() - k * nbSamplesInWaveform : nbSamplesInWaveform;
                painter.drawPolyline(mean.constData() + k * nbSamplesInWaveform, pointCount);

                //Draw the 2 standard deviations with a dash line
                pen.setStyle(Qt::DotLine);
                painter.setPen(pen);
                int pointCountMin = (nbSamplesInWaveform == -1) ?  min.size() - k * nbSamplesInWaveform : nbSamplesInWaveform;
                painter.drawPolyline(min.constData() + k * nbSamplesInWaveform, pointCountMin);

                int pointCountMax = (nbSamplesInWaveform == -1) ?  max.size() - k * nbSamplesInWaveform : nbSamplesInWaveform;
                painter.drawPolyline(max.constData() + k * nbSamplesInWaveform, pointCountMax);
            }
        }
        //Draw all the selected waveforms
        //The data are store as follow:
        //spike after spike and for each spike sample after sample and for each sample
        //channel after channel.
        else{
            if(!waveformIterator->areSpikesAvailable())continue;
            long nbOfSpikes = waveformIterator->nbOfSpikes();
            for(long i = 0; i < nbOfSpikes; ++i){
                int x = 0;
                QPolygon spike(nbchannels * nbSamplesInWaveform);
                for(int i = 0; i < nbSamplesInWaveform; ++i){
                    for(int j = 0; j < nbchannels; ++j){
                        Y = Y0 - channelPositions[j] * (YsizeForMaxAmp + Yspace);
                        //The point is drawn in the QT coordinate system where the Y axis in oriented downwards
                        //The value receive from the iterator is already inverted.
                        spike.setPoint((j*nbSamplesInWaveform) + i, X + x,-Y + static_cast<long>(waveformIterator->nextSpike() * Yfactor));
                    }
                    x += Xstep;
                }
                for(int k = 0;k < nbchannels;++k){

                    int pointCount = (nbSamplesInWaveform == -1) ?  spike.size() - k * nbSamplesInWaveform : nbSamplesInWaveform;
                    painter.drawPolyline(spike.constData() + k * nbSamplesInWaveform, pointCount);
                }
            }
        }
        //Delete the waveform iterator received for the current cluster
        delete waveformIterator;
        //Reinitialize the Y,the starting ordinate
        Y = Y0 - channelPositions[0] * (YsizeForMaxAmp + Yspace);
        //Shift X to the starting abscissa of the next cluster if there is not
        //to find the specific position of the next cluster (case treated at the beging of the loop)
        if(!specificPosition)X += clusterShift;
    }
}

void WaveformView::setResidualPreview(int nChan, int nSamp,
                                      const std::vector<float>& meanWave,
                                      const std::vector<float>& fit,
                                      const std::vector<float>& resid,
                                      const QString& verdict){
    // Stop any in-flight waveform job so it cannot redraw normal traces over the
    // preview, then show the preview self-contained (no thread/cache needed).
    supersedeRunningThreads();
    rpChan_ = nChan; rpSamp_ = nSamp;
    rpMean_ = meanWave; rpFit_ = fit; rpResid_ = resid; rpVerdict_ = verdict;
    hasResidualPreview_ = true;
    dataReady = true;
    updateWindow();                       // world box for the currently-shown clusters
    drawContentsMode = REDRAW;
    update();
}

void WaveformView::clearResidualPreview(){
    if(!hasResidualPreview_) return;
    hasResidualPreview_ = false;
    rpMean_.clear(); rpFit_.clear(); rpResid_.clear(); rpVerdict_.clear();
    rpChan_ = rpSamp_ = 0;
    // The cluster waveform cache was never touched, so a plain redraw restores
    // the normal display.
    drawContentsMode = REDRAW;
    update();
}

void WaveformView::drawResidualPreview(QPainter& painter){
    if(!hasResidualPreview_ || rpChan_ <= 0 || rpSamp_ <= 0) return;
    const int need    = rpChan_ * rpSamp_;
    const int step    = YsizeForMaxAmp + Yspace;
    const int nChShow = std::min(rpChan_, nbchannels);

    // One column at X0, channel ch's baseline at world-y -(Y0 - pos*step), exactly
    // as drawWaveforms / drawChannelSelection.  drawWaveforms draws
    // -Y + alreadyInverted*Yfactor with the iterator pre-negating the raw sample
    // for Qt's downward Y; our traces are raw, so we negate here on the line
    // marked (*).  If the preview renders upside-down, flip that sign.
    auto drawTrace = [&](const std::vector<float>& tr, const QColor& color){
        if(static_cast<int>(tr.size()) < need) return;
        painter.setPen(QPen(color));
        for(int ch = 0; ch < nChShow; ++ch){
            const int cpos = (ch < static_cast<int>(channelPositions.size()))
                                 ? channelPositions[ch] : ch;
            const long Y = Y0 - static_cast<long>(cpos) * step;
            QPolygon poly(rpSamp_);
            long x = 0;
            for(int i = 0; i < rpSamp_; ++i){
                const float v = tr[static_cast<size_t>(ch) * rpSamp_ + i];
                poly.setPoint(i, static_cast<int>(X0 + x),
                              static_cast<int>(-Y - static_cast<long>(v * Yfactor)));  // (*) sign
                x += Xstep;
            }
            painter.drawPolyline(poly);
        }
    };
    drawTrace(rpMean_, QColor(140,140,140));          // mean lassoed waveform (grey)
    if(!rpFit_.empty()) drawTrace(rpFit_, QColor(60,120,220));  // basis fit B·ā (blue)
    drawTrace(rpResid_, QColor(214,40,40));           // residual x̄−B·ā (red, headline)
}

// ── template preview (plan §11.4) ──────────────────────────────────────────

void WaveformView::setTemplatePreview(bool editMode, int nChan, int nSamp,
                                      const std::vector<std::vector<float>>& templates,
                                      const std::vector<QColor>& colors,
                                      int bandCluster, bool scaleAbsolute,
                                      const std::vector<std::vector<float>>& stds){
    tpEdit_          = editMode;
    tpChan_          = nChan;
    tpSamp_          = nSamp;
    tpTemplates_     = templates;
    tpStds_          = stds;
    tpColors_        = colors;
    tpBandCluster_   = bandCluster;
    tpScaleAbsolute_ = scaleAbsolute;
    hasTemplatePreview_ = true;
    dataReady = true;
    // Edit mode underlays cluster `bandCluster`'s mean±std as a grey band; make
    // sure that mean is computed.  Best-effort: if the job has not finished the
    // band simply does not draw yet, and its completion posts a redraw (which
    // still routes through this preview because hasTemplatePreview_ stays set).
    if(editMode && bandCluster > 1 && !view.clusters().isEmpty()){
        WaveformThread* meanJob = getWaveforms();
        meanJob->getMean(bandCluster, presentationMode);
    }
    updateWindow();                       // size the world box for the shown clusters
    drawContentsMode = REDRAW;
    update();
}

void WaveformView::clearTemplatePreview(){
    if(!hasTemplatePreview_) return;
    hasTemplatePreview_ = false;
    tpEdit_ = false;
    tpTemplates_.clear();
    tpStds_.clear();
    tpColors_.clear();
    tpChan_ = tpSamp_ = 0;
    tpBandCluster_ = -1;
    // The cluster waveform cache was never touched, so a plain redraw restores
    // the normal display.
    drawContentsMode = REDRAW;
    update();
}

void WaveformView::setTemplatePreviewScaleAbsolute(bool absolute){
    if(tpScaleAbsolute_ == absolute) return;
    tpScaleAbsolute_ = absolute;
    if(hasTemplatePreview_){
        drawContentsMode = REDRAW;
        update();
    }
}

void WaveformView::drawTemplateBand(QPainter& painter){
    // Edit mode only: cluster tpBandCluster_'s mean±std as a grey dotted band
    // underlay, consuming the iterator sample-major exactly as the mean
    // presentation in drawWaveforms (so the mean/stdev pairing lines up).
    if(tpBandCluster_ <= 1) return;
    Data& clusteringData = doc.data();
    Data::WaveformIterator* it;
    if(presentationMode == SAMPLE)
        it = clusteringData.sampleWaveformIterator(static_cast<dataType>(tpBandCluster_), nbSpkToDisplay);
    else
        it = clusteringData.timeFrameWaveformIterator(static_cast<dataType>(tpBandCluster_), startTime, endTime);
    if(!it->isMeanAvailable()){ delete it; return; }

    const int X = X0;                     // the first (left-most) column
    QPolygon mn, mx;
    int x = 0;
    for(int i = 0; i < nbSamplesInWaveform; ++i){
        for(int j = 0; j < nbchannels; ++j){
            const long Y = Y0 - channelPositions[j] * (YsizeForMaxAmp + Yspace);
            const long meanValue = it->nextMeanValue();           // already inverted
            const long stDev     = it->nextStDeviationValue();
            mn.putPoints((j*nbSamplesInWaveform) + i, 1, X + x, -Y + static_cast<long>((meanValue - stDev) * Yfactor));
            mx.putPoints((j*nbSamplesInWaveform) + i, 1, X + x, -Y + static_cast<long>((meanValue + stDev) * Yfactor));
        }
        x += Xstep;
    }
    delete it;

    QPen band(QColor(150,150,150));
    band.setStyle(Qt::DotLine);
    painter.setPen(band);
    for(int k = 0; k < nbchannels; ++k){
        const int cntMin = (nbSamplesInWaveform == -1) ? mn.size() - k*nbSamplesInWaveform : nbSamplesInWaveform;
        painter.drawPolyline(mn.constData() + k*nbSamplesInWaveform, cntMin);
        const int cntMax = (nbSamplesInWaveform == -1) ? mx.size() - k*nbSamplesInWaveform : nbSamplesInWaveform;
        painter.drawPolyline(mx.constData() + k*nbSamplesInWaveform, cntMax);
    }
}

void WaveformView::drawTemplatePreview(QPainter& painter){
    // Templates are raw, CHANNEL-MAJOR (ch*nSamp + i) — the same layout + sign
    // convention as drawResidualPreview (raw negated on the point; if it renders
    // upside-down, flip that sign on the marked line).
    if(tpTemplates_.empty() || tpChan_ <= 0 || tpSamp_ <= 0) return;
    const int step    = YsizeForMaxAmp + Yspace;
    const int nChShow = std::min(tpChan_, nbchannels);
    const int alpha   = tpEdit_ ? 255 : 90;              // front vs faint underlay
    const int need    = tpChan_ * tpSamp_;

    auto peakOf = [&](const std::vector<float>& tr)->float{
        float pk = 0.f;
        for(float v : tr){ const float a = (v < 0.f) ? -v : v; if(a > pk) pk = a; }
        return pk;
    };
    auto factorFor = [&](const std::vector<float>& tr)->double{
        if(tpScaleAbsolute_) return Yfactor;             // absolute: data gain
        const float pk = peakOf(tr);
        if(pk <= 0.f) return Yfactor;
        return (0.75 * YsizeForMaxAmp) / pk;             // best-fit: tallest spans ~75%
    };
    auto drawOne = [&](const std::vector<float>& tr, const QColor& col, int X, double factor){
        if(static_cast<int>(tr.size()) < need) return;
        QColor c = col; c.setAlpha(alpha);
        QPen pen(c); pen.setWidth(tpEdit_ ? 2 : 1);
        painter.setPen(pen);
        for(int ch = 0; ch < nChShow; ++ch){
            const int cpos = (ch < static_cast<int>(channelPositions.size())) ? channelPositions[ch] : ch;
            const long Y = Y0 - static_cast<long>(cpos) * step;
            QPolygon poly(tpSamp_);
            long x = 0;
            for(int i = 0; i < tpSamp_; ++i){
                const float v = tr[static_cast<size_t>(ch) * tpSamp_ + i];     // channel-major
                poly.setPoint(i, static_cast<int>(X + x),
                              static_cast<int>(-Y - static_cast<long>(v * factor)));  // (*) sign
                x += Xstep;
            }
            painter.drawPolyline(poly);
        }
    };

    // A mean±std BAND: a translucent filled ribbon between (mean-std) and (mean+std)
    // per channel, so the model reads against the actual spikes without hiding them.
    // Drawn only when a matching std curve was supplied (tpStds_); otherwise the
    // caller gets the bare mean polyline from drawOne.
    auto drawBand = [&](const std::vector<float>& tr, const std::vector<float>& sd,
                        const QColor& col, int X, double factor){
        if(static_cast<int>(tr.size()) < need || static_cast<int>(sd.size()) < need) return;
        QColor fill = col; fill.setAlpha(tpEdit_ ? 70 : 45);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        for(int ch = 0; ch < nChShow; ++ch){
            const int cpos = (ch < static_cast<int>(channelPositions.size())) ? channelPositions[ch] : ch;
            const long Y = Y0 - static_cast<long>(cpos) * step;
            QPolygon band(2 * tpSamp_);
            long x = 0;
            for(int i = 0; i < tpSamp_; ++i){
                const float m = tr[static_cast<size_t>(ch) * tpSamp_ + i];
                const float s = sd[static_cast<size_t>(ch) * tpSamp_ + i];
                band.setPoint(i,                   static_cast<int>(X + x),
                              static_cast<int>(-Y - static_cast<long>((m + s) * factor)));   // (*) sign
                band.setPoint(2 * tpSamp_ - 1 - i, static_cast<int>(X + x),
                              static_cast<int>(-Y - static_cast<long>((m - s) * factor)));   // lower, reversed
                x += Xstep;
            }
            painter.drawPolygon(band);
        }
        painter.setBrush(Qt::NoBrush);
    };
    auto hasStd = [&](size_t t){ return t < tpStds_.size() && static_cast<int>(tpStds_[t].size()) >= need; };

    if(tpEdit_){
        // Templates overlaid at the first column, on top of the spikes.  A supplied
        // std draws the ±band first, then a thin mean centreline over it.
        for(size_t t = 0; t < tpTemplates_.size(); ++t){
            const QColor col = (t < tpColors_.size()) ? tpColors_[t] : QColor(60,120,220);
            const double f   = factorFor(tpTemplates_[t]);
            if(hasStd(t)) drawBand(tpTemplates_[t], tpStds_[t], col, X0, f);
            drawOne(tpTemplates_[t], col, X0, f);
        }
    } else {
        // Review: the primary template as a faint underlay behind each shown
        // cluster's waveform column (one column at X0 in overlay presentation).
        const std::vector<float>& prim = tpTemplates_.front();
        const QColor col = tpColors_.empty() ? QColor(60,120,220) : tpColors_.front();
        const double factor = factorFor(prim);
        const bool   primStd = hasStd(0);
        QList<int> cl = view.clusters();
        std::sort(cl.begin(), cl.end());
        const int colShift = overLayPresentation ? 0 : shift;
        if(cl.isEmpty() || colShift == 0){
            if(primStd) drawBand(prim, tpStds_[0], col, X0, factor);
            drawOne(prim, col, X0, factor);
        } else {
            int X = X0;
            for(int n = 0; n < cl.size(); ++n){
                if(primStd) drawBand(prim, tpStds_[0], col, X, factor);
                drawOne(prim, col, X, factor);
                X += colShift;
            }
        }
    }
}

void WaveformView::updateWindow(){
    int nbOfClusters = view.clusters().size();

    //Update the window if the clusters are side by side or any case if there is only one cluster.
    if(!overLayPresentation){
        abscissaMax = 2 * widthBorder + nbOfClusters * (nbSamplesInWaveform -1) * Xstep + (nbOfClusters - 1) * Xspace;

        //Set the window
        window = ZoomWindow(QRect(QPoint(abscissaMin,ordinateMin),QPoint(abscissaMax,ordinateMax)));

        //Everything has to be redraw
        drawContentsMode = REDRAW ;
    }
    else{
        abscissaMax = 2 * widthBorder + (nbSamplesInWaveform -1) * Xstep;

        //Set the window
        window = ZoomWindow(QRect(QPoint(abscissaMin,ordinateMin),QPoint(abscissaMax,ordinateMax)));

        //update the drawing mode if needed (if UPDATE, no change is need it).
        if(drawContentsMode == REFRESH)drawContentsMode = REDRAW ;
    }
}

void WaveformView::setMeanPresentation(){
    supersedeRunningThreads();
    meanPresentation = true;
    isZoomed = false;//Hack because all the tabs share the same data.
    drawContentsMode = REDRAW;

    dataReady = false;
    if(!view.clusters().isEmpty()){
        //Enqueue a job to get the waveform data for the clusters.
        WaveformThread* waveformThread = getWaveforms();
        setCursor(Qt::WaitCursor);
        waveformThread->getMean(view.clusters(),presentationMode);
    }
}

void WaveformView::setAllWaveformsPresentation(){
    supersedeRunningThreads();
    meanPresentation = false;
    isZoomed = false;//Hack because all the tabs share the same data.
    drawContentsMode = REDRAW;

    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}


void WaveformView::setSampleMode(){
    supersedeRunningThreads();
    presentationMode = SAMPLE;
    isZoomed = false;//Hack because all the tabs share the same data.
    drawContentsMode = REDRAW;

    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::setTimeFrameMode(){
    supersedeRunningThreads();
    presentationMode = TIME_FRAME;
    isZoomed = false;//Hack because all the tabs share the same data.
    drawContentsMode = REDRAW;

    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::setTimeFrame(long start, long width){
    supersedeRunningThreads();
    startTime = start;
    endTime = start + width;
    if(endTime > maximumTime) endTime = maximumTime;

    isZoomed = false;//Hack because all the tabs share the same data.
    drawContentsMode = REDRAW;

    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::setDisplayNbSpikes(long nbSpikes){
    supersedeRunningThreads();
    nbSpkToDisplay =  nbSpikes;
    isZoomed = false;//Hack because all the tabs share the same data.
    drawContentsMode = REDRAW;

    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}


void WaveformView::drawClusterIds(QPainter& painter){
    // overLayPresentation: caller already guards — this function is only called
    // when !overLayPresentation, so spike counts are always appropriate here.

    QList<int> shownClusters;
    QList<int>::const_iterator iterator;
    QList<int> const clusters = view.clusters();
    for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator)
        shownClusters.append(*iterator);
    std::sort(shownClusters.begin(), shownClusters.end());

    QFont f("Helvetica",8);
    painter.setFont(f);

    ItemColors& clusterColors = doc.clusterColors();
    Data& clusteringData = doc.data();

    //The abscissa of the legend for the current waveform.
    uint X = widthBorder;
    //The ordinate of the legend for the current waveform.
    uint Y = 0;

    for(iterator = shownClusters.begin(); iterator != shownClusters.end(); ++iterator){
        const int cid = *iterator;
        // Draw in the cluster's own colour so each label is visually linked
        // to the waveform traces of that cluster.
        const QColor clusterColor = clusterColors.color(cid);
        painter.setPen(clusterColor);

        const long nSpk = static_cast<long>(clusteringData.nbOfSpikes(static_cast<dataType>(cid)));
        const QString label = QString("%1 (%2)").arg(cid).arg(nSpk);

        painter.drawText(worldToViewport(X,-Y).x() + 8,
                         worldToViewport(X,-Y).y(),
                         label);
        X += shift;
    }
}

void WaveformView::updateDrawing(){
    //The data have to be collected if need it and everything has to be redrawn
    if(!view.clusters().isEmpty() && drawContentsMode == REDRAW){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::clustersRenumbered(bool active){
    //The data have to be collected and everything has to be redrawn.
    //If the widget is in the active view, it is done immediately otherswise it will be done
    //when the view willbecome active (updateDrawing will be called).
    drawContentsMode = REDRAW;

    if(!view.clusters().isEmpty() && active){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::mouseDoubleClickEvent (QMouseEvent *e){
    //Trigger parent event
    ViewWidget::mouseDoubleClickEvent(e);
    if((!view.clusters().isEmpty())){
        Data& clusteringData = doc.data();
        bool waveformsNotAvailable = false;
        QList<int>::const_iterator clusterIterator;
        QList<int> const clusters = view.clusters();
        for(clusterIterator = clusters.begin(); clusterIterator != clusters.end(); ++clusterIterator){
            Data::WaveformIterator* waveformIterator;
            if(presentationMode == SAMPLE) waveformIterator = clusteringData.sampleWaveformIterator(static_cast<dataType>(*clusterIterator),nbSpkToDisplay);
            else waveformIterator = clusteringData.timeFrameWaveformIterator(static_cast<dataType>(*clusterIterator),startTime,endTime);
            if(meanPresentation && (!waveformIterator->isMeanAvailable())) waveformsNotAvailable = true;
            else if(!waveformIterator->areSpikesAvailable()) waveformsNotAvailable = true;
            delete waveformIterator;
        }
        if(waveformsNotAvailable){
            setCursor(Qt::WaitCursor);
            askForWaveformInformation(clusters);
        }
    }
    isZoomed = true;
}


void WaveformView::mousePressEvent(QMouseEvent* e){
    // Ctrl+Left picks channels.  Swallow it here: BaseFrame::mousePressEvent
    // would otherwise arm a zoom rubber band on the same press.
    if((e->button() == Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        e->accept();
        return;
    }
    ViewWidget::mousePressEvent(e);
}

void WaveformView::mouseReleaseEvent(QMouseEvent* e){
    // Ctrl+Left toggles the clicked channel.  Swallow it before the parent:
    // BaseFrame::mouseReleaseEvent treats a plain left click in ZOOM mode as
    // "zoom in by 2", which would fire on every pick.
    if((e->button() == Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        const QPoint world = viewportToWorld(e->position().toPoint().x(),
                                             e->position().toPoint().y());
        const int channel = channelAtWorldY(world.y());
        if(channel >= 0){
            if(pendingChannelSelection.contains(channel))
                pendingChannelSelection.removeAll(channel);
            else
                pendingChannelSelection.append(channel);
            channelSelectionDirty = true;
            // Repaint the shading immediately; the document (and so the
            // matrices) is only told once Ctrl is released.
            drawContentsMode = REDRAW;
            update();
        }
        e->accept();
        return;
    }

    //Trigger parent event
    ViewWidget::mouseReleaseEvent(e);

    if((e->button() & Qt::LeftButton) && (!view.clusters().isEmpty())){
        Data& clusteringData = doc.data();
        bool waveformsNotAvailable = false;
        QList<int>::const_iterator clusterIterator;
        QList<int> const clusters = view.clusters();
        for(clusterIterator = clusters.begin(); clusterIterator != clusters.end(); ++clusterIterator){
            Data::WaveformIterator* waveformIterator;
            if(presentationMode == SAMPLE)
                waveformIterator = clusteringData.sampleWaveformIterator(static_cast<dataType>(*clusterIterator),nbSpkToDisplay);
            else
                waveformIterator = clusteringData.timeFrameWaveformIterator(static_cast<dataType>(*clusterIterator),startTime,endTime);
            if(meanPresentation && (!waveformIterator->isMeanAvailable()))
                waveformsNotAvailable = true;
            else if(!waveformIterator->areSpikesAvailable())
                waveformsNotAvailable = true;
            delete waveformIterator;
        }
        if(waveformsNotAvailable){
            setCursor(Qt::WaitCursor);
            askForWaveformInformation(clusters);
        }
    }

    isZoomed = true;
}


int WaveformView::channelAtWorldY(long worldY) const{
    // drawWaveforms puts channel j's baseline at world ordinate
    //   -(Y0 - channelPositions[j] * step),  step = YsizeForMaxAmp + Yspace
    // so worldY + Y0 == channelPositions[j] * step at the baseline.  Round to
    // the nearest band and reject anything past its half-width (the gap
    // between two channels belongs to neither).
    const int step = YsizeForMaxAmp + Yspace;
    if(step <= 0 || nbchannels <= 0) return -1;
    const long rel = worldY + Y0;
    // Half-open bands: position p owns rel in [p*step - step/2, p*step + step/2),
    // which is exactly the rectangle drawChannelSelection shades.  Floor rather
    // than round: lround() rounds halves AWAY from zero, so the top edge of the
    // topmost band would land on position -1 and be rejected, leaving a dead
    // line where the shading says the channel is clickable.
    const int position = static_cast<int>(
        std::floor((static_cast<double>(rel) + step / 2.0) / step));
    if(position < 0 || position >= nbchannels) return -1;
    for(int j = 0; j < nbchannels; ++j)
        if(channelPositions[j] == position) return j;
    return -1;
}

void WaveformView::drawChannelSelection(QPainter& painter){
    if(pendingChannelSelection.isEmpty()) return;
    // Shade the full width of each selected channel's band.  Drawn before the
    // waveforms so the traces stay on top.
    const QRect r((QRect)window);
    const int step = YsizeForMaxAmp + Yspace;
    QColor shade = palette().color(backgroundRole()).lightness() > 127
                       ? QColor(0, 0, 0, 30)      // light background -> darken
                       : QColor(255, 255, 255, 40);   // dark background -> lighten
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(shade);
    for(int channel : pendingChannelSelection){
        if(channel < 0 || channel >= nbchannels) continue;
        const long baseline = -(Y0 - static_cast<long>(channelPositions[channel]) * step);
        painter.drawRect(QRect(r.left(), static_cast<int>(baseline - step / 2),
                               r.width(), step));
    }
    painter.restore();
}

bool WaveformView::eventFilter(QObject* watched, QEvent* event){
    // Commit the pending selection when Ctrl is released, so picking several
    // channels costs one matrix recompute rather than one per click.  Filtering
    // the application (rather than handling keyReleaseEvent) is deliberate: no
    // view in this hierarchy sets a focus policy, so key events never reach it.
    if(event->type() == QEvent::KeyRelease && channelSelectionDirty){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_Control){
            channelSelectionDirty = false;
            doc.setSelectedChannels(pendingChannelSelection);
        }
    }
    return ViewWidget::eventFilter(watched, event);
}

void WaveformView::resizeEvent(QResizeEvent* e){
    drawContentsMode = REDRAW;

    if(!view.clusters().isEmpty()){
        Data& clusteringData = doc.data();
        bool waveformsNotAvailable = false;
        QList<int>::const_iterator clusterIterator;
        QList<int> const clusters = view.clusters();
        for(clusterIterator = clusters.begin(); clusterIterator != clusters.end(); ++clusterIterator){
            Data::WaveformIterator* waveformIterator;
            if(presentationMode == SAMPLE) waveformIterator = clusteringData.sampleWaveformIterator(static_cast<dataType>(*clusterIterator),nbSpkToDisplay);
            else waveformIterator = clusteringData.timeFrameWaveformIterator(static_cast<dataType>(*clusterIterator),startTime,endTime);
            if(meanPresentation && (!waveformIterator->isMeanAvailable())) waveformsNotAvailable = true;
            else if(!waveformIterator->areSpikesAvailable()) waveformsNotAvailable = true;
            delete waveformIterator;
        }
        if(waveformsNotAvailable){
            setCursor(Qt::WaitCursor);
            askForWaveformInformation(clusters);
        }
    }
}

void WaveformView::willBeKilled(){
    if(!goingToDie){
        goingToDie = true;
        //Supersede the in-flight jobs: each stops at its next cancellation
        //check (where it used to read its per-thread stop flag), and its
        //completion event fails the generation guard in customEvent().
        jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    }
}

void WaveformView::stopAndClearThreads(){
    // Supersede every in-flight job of this view by bumping the request
    // generation.  Does NOT set goingToDie, so new requests can still be
    // enqueued afterwards.
    jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    // Synchronous quiesce: no waveform job is inside a read once this
    // returns.  ZERO live callers since the view's own relaunch resets
    // (mode switches, cluster navigation, edit repaints) moved to the
    // non-blocking supersedeRunningThreads() (epoch-snapshot step 8
    // follow-up): this is the view's member of the documented blocking
    // family, reachable only through the stopRunningThreads() override
    // that KlustersView::stopAllViewThreads() (itself zero-caller) would
    // invoke.  Superseded jobs notice the bump within one poll interval
    // and queued-not-yet-started ones early-out as workers free up, so a
    // caller would block for at most ~1 s, bounded by the pool's worker
    // count.
    while(jobToken->active.load(std::memory_order_acquire) > 0)
        QThread::msleep(1);
    // Drop completion events the superseded jobs posted before retiring.
    // They carry values, not thread pointers, and would fail the generation
    // guard anyway; removing them just saves the no-op dispatches.  Only our
    // two event types are removed — unlike the old blanket removal, queued
    // signal deliveries to this widget survive.
    QApplication::removePostedEvents(this, QEvent::User + 200);
    QApplication::removePostedEvents(this, QEvent::User + 250);
}

void WaveformView::supersedeRunningThreads(){
    //Non-blocking twin of stopAndClearThreads() (epoch-snapshot step 6b):
    //waveform jobs read their captured snapshot — tables, pinned reader,
    //per-epoch store — so a membership-only edit needs no wait; the bump
    //stops doomed work early and fences stale events.  The blocking
    //quiesce remains for the .spk byte writers (the realign paths).
    jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    QApplication::removePostedEvents(this, QEvent::User + 200);
    QApplication::removePostedEvents(this, QEvent::User + 250);
}

void WaveformView::print(QPainter& printPainter,int width,int height, bool whiteBackground){
    //Draw the double buffer (pixmap) by copying it into the printer device throught the painter.
    QRect viewportOld = QRect(viewport.left(),viewport.top(),viewport.width(),viewport.height());

    viewport = QRect(printPainter.viewport().left(),printPainter.viewport().top(),printPainter.viewport().width(),printPainter.viewport().height()-10);

    //Set the window (part of the world I want to show)
    QRect r((QRect)window);
    printPainter.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function

    //Set the viewport (part of the device I want to write on).
    //By default, the viewport is the same as the device's rectangle (contentsRec), taking a smaller
    //one will ensure that the legends (cluster ids) will not ovelap to much a waveform.
    printPainter.setViewport(viewport);

    //Fill the background with the background color and ensure we draw the same portion of the world than on the screen
    QRect back = QRect(r.left(),r.top(),r.width(),r.height()+10);

    QColor colorLegendTmp = colorLegend;
    QColor background= palette().color(backgroundRole());
    if(whiteBackground){
        colorLegend = Qt::black;
        QPalette palette;
        palette.setColor(backgroundRole(), Qt::white);
        setPalette(palette);
    }

    printPainter.fillRect(back,palette().color(backgroundRole()));
    printPainter.setClipRect(back);

    //Paint all the waveforms in the shownclusters list (in the double buffer)
    drawWaveforms(printPainter,view.clusters());

    //reset transformation due to setWindow and setViewport
    printPainter.resetTransform();

    //Draw the cluster Ids below the waveforms if they are not in overlay presentation.
    if(!overLayPresentation) drawClusterIds(printPainter);

    printPainter.setClipping(false);

    //Restore the colors.
    if(whiteBackground){
        colorLegend = colorLegendTmp;
        QPalette palette;
        palette.setColor(backgroundRole(), background);
        setPalette(palette);

    }

    //Restore the previous state
    viewport = QRect(viewportOld.left(),viewportOld.top(),viewportOld.width(),viewportOld.height());
}

void WaveformView::autoFitAmplitude()
{
    // Scan all loaded waveform samples for the peak absolute value,
    // then set gain so that peak maps to 75 % of YsizeForMaxAmp.
    // Formula: Yfactor * peak = 0.75 * YsizeForMaxAmp
    //          0.75^gain * acquisitionGain = YsizeForMaxAmp / Yfactor (definition)
    //  => 0.75^gain = peak / acquisitionGain * (100/75)
    //  => gain = log(peak / (acquisitionGain * 0.75)) / log(0.75)
    if (view.clusters().isEmpty()) return;

    Data& clusteringData = doc.data();
    long peakAbs = 0;
    const QList<int>& clusters = view.clusters();
    for (int cid : clusters) {
        Data::WaveformIterator* it =
            clusteringData.sampleWaveformIterator(
                static_cast<dataType>(cid), nbSpkToDisplay);
        if (!it->areSpikesAvailable()) { delete it; continue; }
        const long nSpk = it->nbOfSpikes();
        const int  nPts = clusteringData.nbSamplesPerWaveform()
                        * clusteringData.nbOfChannels();
        for (long s = 0; s < nSpk; ++s)
            for (int p = 0; p < nPts; ++p) {
                const long v = std::abs(static_cast<long>(it->nextSpike()));
                if (v > peakAbs) peakAbs = v;
            }
        delete it;
    }
    if (peakAbs <= 0) return;

    // Compute gain: 0.75^gain * acquisitionGain = acquisitionGain * peakAbs / (acquisitionGain * 0.75)
    // Simplifies to: 0.75^gain = peakAbs / (acquisitionGain * 0.75)
    const double target = static_cast<double>(peakAbs)
                        / (static_cast<double>(acquisitionGain) * 0.75);
    if (target <= 0.0) return;
    const int newGain = static_cast<int>(std::round(
        std::log(target) / std::log(0.75)));
    // Clamp to a sane range to avoid invisible or exploding waveforms.
    gain = std::max(-20, std::min(40, newGain));
    Yfactor = static_cast<float>(YsizeForMaxAmp)
            / static_cast<float>(std::pow(0.75, gain) * acquisitionGain);
    drawContentsMode = REDRAW;
    update();
}

void WaveformView::increaseAmplitude(){
    //Decreases the ordinate scale resulting in
    //an enlargement of the waveforms in the ordinate direction.
    //factor = traceVspace / ((4/3)^gain * acquisitionGain)
    gain++;
    Yfactor = static_cast<float>(YsizeForMaxAmp)/static_cast<float>(pow(0.75,gain) * acquisitionGain);
    
    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::decreaseAmplitude(){
    //Increases the ordinate scale resulting in
    //an reduction of the waveforms in the ordinate direction.
    //factor = traceVspace / ((4/3)^gain * acquisitionGain)
    gain--;
    Yfactor = static_cast<float>(YsizeForMaxAmp)/static_cast<float>(pow(0.75,gain) * acquisitionGain);
    
    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::setOverLayPresentation(){
    overLayPresentation = true;
    isZoomed = false;//Hack because all the tabs share the same data.

    //Everything has to be redraw
    //     updateWindow();
    //     update();

    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }
}

void WaveformView::setSideBySidePresentation(){
    overLayPresentation = false;
    isZoomed = false;//Hack because all the tabs share the same data.
    
    //Everything has to be redraw
    //     updateWindow();
    //     update();

    //The data have to be collected if need it and everything has to be redraw
    if(!view.clusters().isEmpty()){
        setCursor(Qt::WaitCursor);
        askForWaveformInformation(view.clusters());
    }

}
