/***************************************************************************
                          neuroscopeview.cpp  -  description
                             -------------------
    begin                : Wed Feb 25 19:05:25 EST 2004
    copyright            : (C) 2004 by Lynn Hazan
    email                : lynn.hazan.myrealbox.com
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
#include <QPainter>


#include <QList>
#include <QPixmap>
#include <QStatusBar>

// application specific includes
#include "neuroscopeview.h"
#include "neuroscopedoc.h"
#include "neuroscope.h"
#include "tracewidget.h"
// Qt6 PMF connect requires complete types for all signal/slot parameter types
#include "clustersprovider.h"
#include "eventsprovider.h"
#include "itemcolors.h"


class EventData;

NeuroscopeView::NeuroscopeView(NeuroscopeApp& mainWindow, const QString &label, long startTime, long duration, const QColor &backgroundColor, int wflags, QStatusBar* statusBar, QList<int>* channelsToDisplay,
                               bool greyScale, TracesProvider& tracesProvider, bool multiColumns, bool verticalLines,
                               bool raster, bool waveforms, bool labelsDisplay, int unitGain, int acquisitionGain, ChannelColors* channelColors,
                               QMap<int,QList<int> >* groupsChannels, QMap<int,int>* channelsGroups, bool autocenterChannels,
                               QList<int> offsets, QList<int> channelGains, QList<int> selected, QMap<int,bool> skipStatus, int rasterHeight, const QString &backgroundImagePath, QWidget* parent, const char* name):
    DockArea(parent)
  ,shownChannels(channelsToDisplay),mainWindow(mainWindow),greyScaleMode(greyScale),
    multiColumns(multiColumns),verticalLines(verticalLines),raster(raster),waveforms(waveforms),selectMode(false),autocenterChannels(autocenterChannels),
    channelOffsets(),gains(),selectedChannels(),tabLabel(label),startTime(startTime),timeWindow(duration),
    labelsDisplay(labelsDisplay),isPositionFileShown(false),positionView(0L),eventsInPositionView(false), positionsDockWidget(0){

    //Duplicate the offset,gain and channelSelected lists
    QList<int>::iterator offsetIterator;
    for(offsetIterator = offsets.begin(); offsetIterator != offsets.end(); ++offsetIterator)
        channelOffsets.append(*offsetIterator);

    QList<int>::iterator gainIterator;
    for(gainIterator = channelGains.begin(); gainIterator != channelGains.end(); ++gainIterator)
        gains.append(*gainIterator);

    QList<int>::iterator selectedIterator;
    for(selectedIterator = selected.begin(); selectedIterator != selected.end(); ++selectedIterator)
        selectedChannels.append(*selectedIterator);


    QList<int> skippedChannels;
    QMap<int,bool>::Iterator iterator;
    for(iterator = skipStatus.begin(); iterator != skipStatus.end(); ++iterator) if(iterator.value()) skippedChannels.append(iterator.key());

    //Create the mainDock
    mainDock = new QDockWidget(tr("field potentials"));
    mainDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::RightDockWidgetArea,mainDock);

    traceWidget = new TraceWidget(startTime,duration,greyScale,tracesProvider,multiColumns,verticalLines,raster,
                                  waveforms,labelsDisplay,*shownChannels,unitGain,acquisitionGain,channelColors,groupsChannels,channelsGroups,autocenterChannels,
                                  channelOffsets,gains,skippedChannels,rasterHeight,QImage(backgroundImagePath),mainDock,"traces",backgroundColor,statusBar,5);
	 /// Added by M.Zugaro to enable automatic forward paging
    connect(traceWidget,&TraceWidget::stopped,this,&NeuroscopeView::traceWidgetStopped);

    mainDock->setWidget(traceWidget);
    mainDock->setFocusPolicy(Qt::NoFocus);

    //Set Connection(s) common to all widgets.
    connect(this,&NeuroscopeView::updateContents,traceWidget,&TraceWidget::updateContents);
    connect(this,&NeuroscopeView::changeBackgroundColor,traceWidget, &TraceWidget::changeBackgroundColor);
    connect(this,&NeuroscopeView::greyScale,traceWidget, &TraceWidget::setGreyScale);
    connect(traceWidget,&TraceWidget::channelsSelected,this, &NeuroscopeView::slotChannelsSelected);
    connect(this,&NeuroscopeView::modeToSet,traceWidget,&TraceWidget::setMode);
    connect(this,&NeuroscopeView::multiColumnsDisplay,traceWidget,&TraceWidget::setMultiColumns);
    connect(this,&NeuroscopeView::clusterVerticalLinesDisplay,traceWidget,&TraceWidget::setClusterVerticalLines);
    connect(this,&NeuroscopeView::clusterRasterDisplay,traceWidget,&TraceWidget::setClusterRaster);
    connect(this,&NeuroscopeView::clusterWaveformsDisplay,traceWidget,&TraceWidget::setClusterWaveforms);
    connect(this,&NeuroscopeView::showChannels,traceWidget,&TraceWidget::showChannels);
    connect(this,&NeuroscopeView::channelColorUpdate,traceWidget,&TraceWidget::channelColorUpdate);
    connect(this,&NeuroscopeView::groupColorUpdate,traceWidget,&TraceWidget::groupColorUpdate);
    connect(this,&NeuroscopeView::increaseAllAmplitude,traceWidget,&TraceWidget::increaseAllChannelsAmplitude);
    connect(this,&NeuroscopeView::decreaseAllAmplitude,traceWidget,&TraceWidget::decreaseAllChannelsAmplitude);
    connect(this,&NeuroscopeView::increaseAmplitude,traceWidget,&TraceWidget::increaseSelectedChannelsAmplitude);
    connect(this,&NeuroscopeView::decreaseAmplitude,traceWidget,&TraceWidget::decreaseSelectedChannelsAmplitude);
    connect(this,&NeuroscopeView::updateGains,traceWidget,&TraceWidget::setGains);
    connect(this,&NeuroscopeView::updateDrawing,traceWidget, &TraceWidget::updateDrawing);
    connect(this,&NeuroscopeView::groupsHaveBeenModified,traceWidget, &TraceWidget::groupsModified);
    connect(this,&NeuroscopeView::channelsToBeSelected,traceWidget,&TraceWidget::selectChannels);
    connect(this,&NeuroscopeView::resetChannelOffsets,traceWidget,&TraceWidget::resetOffsets);
    connect(this,&NeuroscopeView::resetChannelGains,traceWidget,&TraceWidget::resetGains);
    connect(this,&NeuroscopeView::drawTraces,traceWidget,&TraceWidget::drawTraces);
    connect(this,&NeuroscopeView::reset,traceWidget,&TraceWidget::reset);
    connect(traceWidget,&TraceWidget::updateStartAndDuration,this, &NeuroscopeView::setStartAndDuration);
    connect(this,&NeuroscopeView::autocenterChannelsChanged,traceWidget, &TraceWidget::setAutocenterChannels);
    connect(this,&NeuroscopeView::showLabels,traceWidget, &TraceWidget::showLabels);
    connect(this,&NeuroscopeView::displayCalibration,traceWidget, &TraceWidget::showCalibration);
    connect(this,&NeuroscopeView::newSamplingRate,traceWidget,&TraceWidget::samplingRateModified);
    connect(this,&NeuroscopeView::newClusterProvider,traceWidget,
            &TraceWidget::addClusterProvider);
    connect(this,&NeuroscopeView::clusterProviderRemoved,traceWidget,&TraceWidget::removeClusterProvider);
    connect(this,&NeuroscopeView::showClusters,traceWidget,&TraceWidget::showClusters);
    connect(this,&NeuroscopeView::clusterColorUpdated,traceWidget,&TraceWidget::clusterColorUpdate);
    connect(this,
        static_cast<void(NeuroscopeView::*)(QPainter&,int,int,const QString&,bool)>(&NeuroscopeView::print),
        traceWidget, &TraceWidget::print);
    connect(this,&NeuroscopeView::newEventProvider,traceWidget,
            &TraceWidget::addEventProvider);
    connect(this,&NeuroscopeView::eventProviderRemoved,traceWidget,&TraceWidget::removeEventProvider);
    connect(this,&NeuroscopeView::showEvents,traceWidget,&TraceWidget::showEvents);
    connect(this,&NeuroscopeView::eventColorUpdated,traceWidget,&TraceWidget::eventColorUpdate);
    connect(this,&NeuroscopeView::nextEvent,traceWidget,&TraceWidget::showNextEvent);
    connect(this,&NeuroscopeView::previousEvent,traceWidget,&TraceWidget::showPreviousEvent);
    connect(traceWidget,&TraceWidget::eventModified,this, &NeuroscopeView::slotEventModified);
    connect(this, static_cast<void(NeuroscopeView::*)(bool,const QString&,double,double)>(&NeuroscopeView::updateEvents),
        traceWidget, static_cast<void(TraceWidget::*)(bool,const QString&,double,double)>(&TraceWidget::updateEvents));
    connect(this,&NeuroscopeView::eventToRemove,traceWidget,&TraceWidget::removeEvent);
    connect(traceWidget,&TraceWidget::eventRemoved,this, &NeuroscopeView::slotEventRemoved);
    connect(this, static_cast<void(NeuroscopeView::*)(bool,const QString&,double)>(&NeuroscopeView::updateEvents),
        traceWidget, static_cast<void(TraceWidget::*)(bool,const QString&,double)>(&TraceWidget::updateEvents));
    connect(this,&NeuroscopeView::newEventProperties,traceWidget,&TraceWidget::eventToAddProperties);
    connect(traceWidget,&TraceWidget::eventAdded,this, &NeuroscopeView::slotEventAdded);
    connect(this, static_cast<void(NeuroscopeView::*)(const QString&,QList<int>&,bool)>(&NeuroscopeView::updateEvents),
        traceWidget, static_cast<void(TraceWidget::*)(const QString&,QList<int>&,bool)>(&TraceWidget::updateEvents));
    connect(this,&NeuroscopeView::nextCluster,traceWidget,&TraceWidget::showNextCluster);
    connect(this,&NeuroscopeView::previousCluster,traceWidget,&TraceWidget::showPreviousCluster);
    connect(this,&NeuroscopeView::waveformInformationUpdated,traceWidget,&TraceWidget::updateWaveformInformation);
    connect(this,&NeuroscopeView::clusterProviderUpdated,traceWidget,&TraceWidget::updateClusterData);
    connect(this,&NeuroscopeView::noneBrowsingClusterListUpdated,traceWidget,&TraceWidget::updateNoneBrowsingClusterList);
    connect(this,&NeuroscopeView::noneBrowsingEventListUpdated,traceWidget,&TraceWidget::updateNoneBrowsingEventList);
    connect(this,&NeuroscopeView::skipStatusChanged,traceWidget,&TraceWidget::updateSkipStatus);
    connect(this,&NeuroscopeView::decreaseTheRasterHeight,traceWidget,&TraceWidget::decreaseRasterHeight);
    connect(this,&NeuroscopeView::increaseTheRasterHeight,traceWidget,&TraceWidget::increaseRasterHeight);
    connect(this,&NeuroscopeView::traceBackgroundImageUpdate,traceWidget,&TraceWidget::traceBackgroundImageUpdate);
    
    connect(&globalEventProvider,&GlobalEventsProvider::getCurrentEventInformation,traceWidget,&TraceWidget::getCurrentEventInformation);
}

NeuroscopeView::~NeuroscopeView()
{
    delete shownChannels;
}


void NeuroscopeView::print(QPrinter* printer,const QString& filePath,bool whiteBackground)
{
    QPainter printPainter;
    const int width = printer->width();
    const int height = printer->height();
    printPainter.begin(printer);

    //For the moment there is no list of contained views, therefore the signal is the only way to trigger the print of
    //the traceWidget
    //Print the TraceView
    emit print(printPainter,width,height,filePath,whiteBackground);

    //Print the positionView.
    if(isPositionFileShown){
        printer->newPage();
        NeuroscopeDoc* doc = mainWindow.getDocument();
        positionView->print(printPainter,width,height,whiteBackground,doc->getWhiteTrajectoryBackground());
    }

    printPainter.end();
} 

void NeuroscopeView::setChannelNb(int nb){
    shownChannels->clear();
    selectedChannels.clear();

    emit reset();
}

void NeuroscopeView::shownChannelsUpdate(const QList<int>& channelsToShow){
    shownChannels->clear();
    selectedChannels.clear();

    //update the list of shown channels and the list of selected channels
    QList<int>::const_iterator shownChannelsIterator;
    for(shownChannelsIterator = channelsToShow.begin(); shownChannelsIterator != channelsToShow.end(); ++shownChannelsIterator){
        shownChannels->append(*shownChannelsIterator);
        selectedChannels.append(*shownChannelsIterator);
    }

    emit showChannels(*shownChannels);

    //Show all the enclosed widgets of the dockWindows.
    showAllWidgets();
}


void NeuroscopeView::setMultiColumns(bool multiple){
    multiColumns = multiple;
    emit multiColumnsDisplay(multiple);
}

void NeuroscopeView::setClusterVerticalLines(bool lines){
    verticalLines = lines;
    emit clusterVerticalLinesDisplay(lines);
}

void NeuroscopeView::setClusterRaster(bool raster){
    this->raster = raster;
    emit clusterRasterDisplay(raster);
}
void NeuroscopeView::setClusterWaveforms(bool waveforms){
    this->waveforms = waveforms;
    emit clusterWaveformsDisplay(waveforms);
}

void NeuroscopeView::setClusterProvider(ClustersProvider* clustersProvider,QString name,ItemColors* clusterColors,bool active,
                                        QList<int>& clustersToShow,QMap<int,QList<int> >* displayGroupsClusterFile,
                                        QMap<int,int>* channelsSpikeGroups,int nbSamplesBefore,int nbSamplesAfter,const QList<int>& clustersToSkip){
    QList<int>* currentSelectedClusters = new QList<int>();
    QList<int>::iterator shownClustersIterator;
    QList<int>::iterator shownClustersIteratorEnd(clustersToShow.end());
    for(shownClustersIterator = clustersToShow.begin(); shownClustersIterator != shownClustersIteratorEnd; ++shownClustersIterator)
        currentSelectedClusters->append(*shownClustersIterator);

    selectedClusters.insert(name,currentSelectedClusters);


    QList<int>* currentSkippedClusters = new QList<int>();
    QList<int>::const_iterator skippedClustersIterator;
    for(skippedClustersIterator = clustersToSkip.begin(); skippedClustersIterator != clustersToSkip.end(); ++skippedClustersIterator)
        currentSkippedClusters->append(*skippedClustersIterator);

    clustersNotUsedForBrowsing.insert(name,currentSkippedClusters);

    emit newClusterProvider(clustersProvider,name,clusterColors,active,clustersToShow,displayGroupsClusterFile,
                            channelsSpikeGroups,nbSamplesBefore,nbSamplesAfter,clustersToSkip);
}

void NeuroscopeView::removeClusterProvider(const QString &name, bool active){
    selectedClusters.remove(name);
    clustersNotUsedForBrowsing.remove(name);
    emit clusterProviderRemoved(name,active);
}

void NeuroscopeView::shownClustersUpdate(const QString &name, const QList<int>& clustersToShow){
    QList<int>* currentSelectedClusters = selectedClusters[name];
    currentSelectedClusters->clear();

    //update the list of shown clusters
    QList<int>::ConstIterator iterator;
    QList<int>::ConstIterator iteratorEnd(clustersToShow.constEnd());
    for(iterator = clustersToShow.constBegin(); iterator != iteratorEnd; ++iterator){
        currentSelectedClusters->append(*iterator);
    }
    emit showClusters(name,*currentSelectedClusters);

    //Show all the enclosed widgets of the dockWindows.
    showAllWidgets();
}

void NeuroscopeView::updateNoneBrowsingClusterList(const QString &providerName, const QList<int>& clustersToNotBrowse){
    QList<int>* currentSkippedClusters = clustersNotUsedForBrowsing[providerName];
    currentSkippedClusters->clear();

    //update the list of skipped events
    QList<int>::const_iterator skippedClustersIterator;
    for(skippedClustersIterator = clustersToNotBrowse.begin(); skippedClustersIterator != clustersToNotBrowse.end(); ++skippedClustersIterator){
        currentSkippedClusters->append(*skippedClustersIterator);
    }

    emit noneBrowsingClusterListUpdated(providerName,clustersToNotBrowse);
}


void NeuroscopeView::setEventProvider(EventsProvider* eventsProvider,const QString &name,ItemColors* eventColors,bool active,
                                      QList<int>& eventsToShow,const QList<int>& eventsToSkip){
    QList<int>* currentSelectedEvents = new QList<int>();
    QList<int>::iterator shownEventsIterator;
    for(shownEventsIterator = eventsToShow.begin(); shownEventsIterator != eventsToShow.end(); ++shownEventsIterator)
        currentSelectedEvents->append(*shownEventsIterator);

    selectedEvents.insert(name,currentSelectedEvents);


    QList<int>* currentSkippedEvents = new QList<int>();
    QList<int>::const_iterator skippedEventsIterator;
    QList<int>::const_iterator skippedEventsIteratorEnd(eventsToSkip.constEnd());
    for(skippedEventsIterator = eventsToSkip.constBegin(); skippedEventsIterator != skippedEventsIteratorEnd; ++skippedEventsIterator)
        currentSkippedEvents->append(*skippedEventsIterator);

    eventsNotUsedForBrowsing.insert(name,currentSkippedEvents);

    //Warn the TraceWidget(s)
    emit newEventProvider(eventsProvider,name,eventColors,active,eventsToShow,eventsToSkip);
}

void NeuroscopeView::removeEventProvider(const QString &name, bool active, bool lastFile){
    selectedEvents.remove(name);
    eventsNotUsedForBrowsing.remove(name);
    if(lastFile) eventsInPositionView = false;

    //Warn the TraceWidget(s) and positionView
    emit eventProviderRemoved(name,active,lastFile);
}

void NeuroscopeView::shownEventsUpdate(const QString& name,const QList<int>& eventsToShow){
    QList<int>* currentSelectedEvents = selectedEvents[name];
    currentSelectedEvents->clear();

    //update the list of shown clusters
    QList<int>::ConstIterator iterator;
    for(iterator = eventsToShow.constBegin(); iterator != eventsToShow.constEnd(); ++iterator){
        currentSelectedEvents->append(*iterator);
    }

    emit showEvents(name,*currentSelectedEvents);
    emit updateEventDisplay();

    //Show all the enclosed widgets of the dockWindows.
    showAllWidgets();
}


void NeuroscopeView::updateNoneBrowsingEventList(const QString& providerName,const QList<int>& eventsToNotBrowse){
    QList<int>* currentSkippedEvents = eventsNotUsedForBrowsing[providerName];
    currentSkippedEvents->clear();

    //update the list of skipped events
    QList<int>::const_iterator skippedEventsIterator;
    for(skippedEventsIterator = eventsToNotBrowse.begin(); skippedEventsIterator != eventsToNotBrowse.end(); ++skippedEventsIterator)
        currentSkippedEvents->append(*skippedEventsIterator);

    emit noneBrowsingEventListUpdated(providerName,eventsToNotBrowse);
}

void NeuroscopeView::updateEvents(const QString& providerName,int selectedEventId,float time,float newTime,bool active){
    emit updateEvents(active,providerName,time,newTime);
    emit updateEventDisplay();
}

void NeuroscopeView::updateEventsAfterRemoval(const QString& providerName,int eventId,float time,bool active){
    emit updateEvents(active,providerName,time);
    emit updateEventDisplay();
}

void NeuroscopeView::updateEventsAfterAddition(const QString& providerName,int eventId,float time,bool active){
    QList<int>* currentSelectedEvents = selectedEvents[providerName];

    if(active && !currentSelectedEvents->contains(eventId)){
        currentSelectedEvents->append(eventId);
        emit updateEvents(providerName,*currentSelectedEvents,active);
    } else {
        emit updateEvents(active,providerName,time);
    }

    emit updateEventDisplay();
}

void NeuroscopeView::updateSelectedEventsIds(const QString& providerName,QMap<int,int>& oldNewEventIds,int modifiedEventId,bool active,bool added){

    if(eventsNotUsedForBrowsing.contains(providerName)){
        QList<int>* currentSkippedEvents = eventsNotUsedForBrowsing.take(providerName);
        QList<int>* newSkippedEventsIds = new QList<int>();
        QList<int>::iterator iterator;

        //An event description has been added
        if(added){
            for(iterator = currentSkippedEvents->begin(); iterator != currentSkippedEvents->end(); ++iterator)
                newSkippedEventsIds->append(oldNewEventIds[*iterator]);

            //The events are skipped by default
            newSkippedEventsIds->append(modifiedEventId);
        }
        //an event description has been removed
        else{
            for(iterator = currentSkippedEvents->begin(); iterator != currentSkippedEvents->end(); ++iterator)
                if(oldNewEventIds.contains(*iterator)) newSkippedEventsIds->append(oldNewEventIds[*iterator]);
        }

        eventsNotUsedForBrowsing.insert(providerName,newSkippedEventsIds);
        delete currentSkippedEvents;

        emit noneBrowsingEventListUpdated(providerName,*newSkippedEventsIds);
    }

    if(selectedEvents.contains(providerName)){
        QList<int>* currentSelectedEvents = selectedEvents.take(providerName);
        QList<int>* newSelectedEventsIds = new QList<int>();
        QList<int>::iterator iterator;

        //An event description has been added
        if(added){
            for(iterator = currentSelectedEvents->begin(); iterator != currentSelectedEvents->end(); ++iterator) {
                newSelectedEventsIds->append(oldNewEventIds[*iterator]);
                qDebug()<<" oldNewEventIds"<<oldNewEventIds[*iterator];
            }

            //Add the new type of event to the active view in order to display the added event right away.
            if(active)newSelectedEventsIds->append(modifiedEventId);
        }
        //an event description has been removed
        else{
            for(iterator = currentSelectedEvents->begin(); iterator != currentSelectedEvents->end(); ++iterator){
                if(oldNewEventIds.contains(*iterator)){
                    newSelectedEventsIds->append(oldNewEventIds[*iterator]);
                }
            }
        }

        selectedEvents.insert(providerName,newSelectedEventsIds);
        delete currentSelectedEvents;

        //If at least one of the selected events has had his id modified, warn the traceView.
        emit updateEvents(providerName,*newSelectedEventsIds,active);
        emit updateEventDisplay();
    }
}

void NeuroscopeView::removePositionProvider(const QString& name,bool active){
    if(positionView != 0L)
        removePositionView();

    //Show all the enclosed widgets of the dockWindows.
    if(active)
        showAllWidgets();
}

void NeuroscopeView::addPositionView(PositionsProvider* positionsProvider,const QImage& backgroundImage,const QColor& backgroundColor,long startTime,long duration,int width,int height,bool showEvents){
    isPositionFileShown = true;
    eventsInPositionView = showEvents;

    //Create and add the position view
    positionsDockWidget = new QDockWidget();
    positionsDockWidget->setFeatures(QDockWidget::NoDockWidgetFeatures);
    //createDockWidget( "Positions", QPixmap());
    positionView = new PositionView(*positionsProvider,globalEventProvider,backgroundImage,startTime,duration,showEvents,height,width,positionsDockWidget,"PositionView",backgroundColor);
    positionsDockWidget->setWidget(positionView);//assign the widget

    addDockWidget(Qt::TopDockWidgetArea,positionsDockWidget);
    //Enable the View to be inform that the positions dockWidget is being close.
    //To do so, connect the positions dockwidget close button to the dockBeingClosed slot of is contained widget
    //and connect this widget parentDockBeingClosed signal to the view positionDockClosed slot.
    //connect(positions, SIGNAL(headerCloseButtonClicked()),positionView, SLOT(dockBeingClosed()));
    connect(positionView, &BaseFrame::parentDockBeingClosed, this, &NeuroscopeView::positionDockClosed);

    //Set the different connections with the view
    connect(this,&NeuroscopeView::positionInformationUpdated,positionView,&PositionView::updatePositionInformation);
    connect(this,&NeuroscopeView::timeChanged,positionView,&PositionView::displayTimeFrame);
    connect(this,&NeuroscopeView::changeBackgroundColor,positionView, &PositionView::changeBackgroundColor);
    connect(traceWidget, &TraceWidget::eventsAvailable, positionView,
            static_cast<void(PositionView::*)(QHash<QString,EventData*>&,QMap<QString,QList<int>>&,QHash<QString,ItemColors*>&,QObject*,double)>(&PositionView::dataAvailable));
    connect(this,&NeuroscopeView::updateEventDisplay,positionView,&PositionView::updateEventDisplay);
    connect(this,&NeuroscopeView::eventColorUpdated,positionView,&PositionView::eventColorUpdate);
    connect(this,&NeuroscopeView::updateDrawing,positionView, &PositionView::updateDrawing);
    connect(this,&NeuroscopeView::newEventProvider,positionView,&PositionView::addEventProvider);
    connect(this,&NeuroscopeView::eventProviderRemoved,positionView,&PositionView::removeEventProvider);
    connect(this,&NeuroscopeView::eventsShownInPositionView,positionView,&PositionView::setEventsInPositionView);

    //Request the data for all the events (can be done only after the connection has be set)
    if(eventsInPositionView) globalEventProvider.requestData(startTime,startTime + duration,positionView);

    //Show all the enclosed widgets of the dockWindows.
    showAllWidgets();
}


void  NeuroscopeView::positionDockClosed(QWidget* view){
    removePositionView();
    emit positionViewClosed();
}

void NeuroscopeView::removePositionView(){
    removeDockWidget(positionsDockWidget);

    delete positionView;
    positionView = 0L;
    isPositionFileShown = false;
}

void NeuroscopeView::resetOffsets(const QList<int>& selectedIds){
    NeuroscopeDoc* doc = mainWindow.getDocument();

    const QMap<int,int>& channelDefaultOffsets = doc->getChannelDefaultOffsets();
    QMap<int,int> selectedChannelDefaultOffsets;

    //update the list of selected channels
    selectedChannels.clear();
    QList<int>::const_iterator selectedIterator;
    QList<int>::const_iterator selectedIteratorEnd(selectedIds.end());
    for(selectedIterator = selectedIds.begin(); selectedIterator != selectedIteratorEnd; ++selectedIterator){
        selectedChannels.append(*selectedIterator);
        selectedChannelDefaultOffsets.insert(*selectedIterator,channelDefaultOffsets[*selectedIterator]);
    }

    emit resetChannelOffsets(selectedChannelDefaultOffsets);
}

int NeuroscopeView::getRasterHeight(){
    return traceWidget->getRasterHeight();
}

void NeuroscopeView::setEventsInPositionView(bool shown){
    eventsInPositionView = shown;
    emit eventsShownInPositionView(shown);
}

void NeuroscopeView::slotChannelsSelected(const QList<int>& selectedIds){
    selectedChannels.clear();
    selectedChannels = selectedIds;
    emit channelsSelected(selectedIds);
}

void  NeuroscopeView::eventColorUpdate(const QColor &color, const QString &name,int eventId,bool active)
{
    emit eventColorUpdated(color, name,eventId,active);
}

void NeuroscopeView::setSelectedChannels(const QList<int>& selectedIds)
{
    //update the list of selected channels
    selectedChannels.clear();
    selectedChannels = selectedIds;
    emit channelsSelected(selectedIds);
}

void NeuroscopeView::selectChannels(const QList<int>& selectedIds)
{
    selectedChannels.clear();
    selectedChannels=selectedIds;
    emit channelsToBeSelected(selectedIds);
}

void NeuroscopeView::setMode(BaseFrame::Mode selectedMode,bool active)
{
    if(selectedMode == 2)
        selectMode = true;
    else
        selectMode = false;
    emit modeToSet(selectedMode,active);
}

void NeuroscopeView::setAutocenterChannels(bool status)
{
    autocenterChannels = status;
    emit autocenterChannelsChanged(status);
}

void NeuroscopeView::showLabelsUpdate(bool status)
{
    labelsDisplay = status;
    emit showLabels(status);
}

const QList<int>* NeuroscopeView::getSelectedClusters(const QString& name) const
{
    return selectedClusters[name];
}

const QList<int>* NeuroscopeView::getSelectedEvents(const QString& name) const
{
    return selectedEvents[name];
}

void NeuroscopeView::removeEvent(){
    emit eventToRemove();
    emit updateEventDisplay();
}

void NeuroscopeView::slotEventAdded(const QString &providerName, const QString &addedEventDescription,double time){
    emit eventAdded(providerName,addedEventDescription,time);
}
