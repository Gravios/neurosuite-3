/***************************************************************************
                          errormatrixthread.h  -  description
                             -------------------
    begin                : Mon Jan 12 2004
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

#ifndef ERRORMATRIXTHREAD_H
#define ERRORMATRIXTHREAD_H

#include <atomic>

//include files for the application
#include "errormatrixview.h"
#include "data.h"
#include "array.h"
#include "groupingassistant.h"

//include files for QT
#include <QThread>


#include <QEvent>
#include <QList>

/**Thread used to compute the Error Matrix. Each element in the matrix
  * indicates how likely it is that the two clusters corresponding to the row and column
  * of the element contain spikes from the same neuron.
  *@author Lynn Hazan
  * @since klusters 1.1
  */

class ErrorMatrixThread : public QThread  {
public:

    //Only the method computeMatrix of ErrorMatrixView has access to the private part of ErrorMatrixThread,
    //the constructor of ErrorMatrixThread being private, only this method con create a new ErrorMatrixThread
    friend ErrorMatrixThread* ErrorMatrixView::computeMatrix();

    ~ErrorMatrixThread(){}
    Array<double>* getProbabilities() const {return probabilities;}
    QList<int> getClusterList() const {return clusterList;}
    QList<int> getComputedClusterList() const {return computedClusterList;}
    QList<int> getIgnoreClusterIndex() const {return ignoreClusterIndex;}

    /**Returns the generation counter at the time this thread was created.
     * Used by ErrorMatrixView::customEvent() to discard results from threads
     * that were superseded by a later updateMatrixContents() call.*/
    int getGeneration() const {return generation;}

    /**Asks the thread to stop his work as soon as possible.*/
    void stopProcessing(){
        haveToStopProcessing.store(true, std::memory_order_release);
        assistant.stopComputing();
    }

    class ErrorMatrixEvent;
    friend class ErrorMatrixEvent;

    ErrorMatrixEvent* getErrorMatrixEvent(){
        return new ErrorMatrixEvent(*this);
    }

    /**
  * Internal class use to send information to the ErrorMatrixView to inform it that
  * the matrix has been computed.
  * @since klusters 1.1
  */
    class ErrorMatrixEvent : public QEvent{
        //Only the method getErrorMatrixEvent of ErrorMatrixThread has access to the private part of ErrorMatrixEvent,
        //the constructor of ErrorMatrixEvent being private, only this method con create a new ErrorMatrixEvent
        friend ErrorMatrixEvent* ErrorMatrixThread::getErrorMatrixEvent();

    public:
        ErrorMatrixThread* parentThread(){return &errorMatrixThread;}
        ~ErrorMatrixEvent(){}

    private:
        explicit ErrorMatrixEvent(ErrorMatrixThread& thread):QEvent(QEvent::Type(QEvent::User + 600)),errorMatrixThread(thread){}

        ErrorMatrixThread& errorMatrixThread;
    };

protected:
    void run();

private:

    ErrorMatrixThread(ErrorMatrixView& view,Data& d, int generation):errorMatrixView(view),data(d),generation(generation),haveToStopProcessing(false),probabilities(nullptr){
        start();
    }

    ErrorMatrixView& errorMatrixView;
    Data& data;
    int generation;
    Array<double>* probabilities;
    QList<int> clusterList;
    QList<int> computedClusterList;
    QList<int> ignoreClusterIndex;
    /**True if the thread has to stop processing, false otherwise.*/
    std::atomic_bool haveToStopProcessing;
    GroupingAssistant assistant;

};

#endif
