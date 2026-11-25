/***************************************************************************
 *          processlinemaker.cpp  -  description
 *            -------------------
 *
 *   Copyright (C) 2002 John Firebaugh <jfirebaugh@kde.org                 *
 *                                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

//include files for the application
#include "processlinemaker.h"
#include <QProcess>

ProcessLineMaker::ProcessLineMaker()
    : mProc(nullptr),
      counterOut(0),
      counterErr(0),
      isProcessKilled(false),
      isWidgetHidden(false),
      processExited(false)
{
}

ProcessLineMaker::ProcessLineMaker( QProcess* proc )
    : mProc(proc),
      counterOut(0),
      counterErr(0),
      isProcessKilled(false),
      isWidgetHidden(false),
      processExited(false)
{
    connect(mProc, &QProcess::readyReadStandardOutput,
            this, &ProcessLineMaker::slotReceivedStdout );
    
    connect(mProc, &QProcess::readyReadStandardError,
            this, &ProcessLineMaker::slotReceivedStderr );
}

void ProcessLineMaker::slotReceivedStdout()
{
    QString s = QString::fromLocal8Bit(mProc->readAllStandardOutput());

    counterOut++;
    
    // The other stream's unfinished line stays in its buffer until its newline
    // arrives: flushing it here cut a stderr line in two whenever stdout spoke
    // between two reads of it.
    stdoutbuf += s;
    int pos;
    while ( (pos = stdoutbuf.indexOf('\n')) != -1) {
        lineOut = stdoutbuf.left(pos);
        stdoutbuf.remove(0, pos+1);
        emit receivedStdoutLine(lineOut);
    }

    // Do not remove this line! It makes the method thread safe.
    //  Because we can be interrupted whenever more data is available,
    //  this prevents printing the same data twice.
    lineOut.clear();

    counterOut--;

    //The process has benn killed and all the process'outputs sent to be print,
    //warn the processWidget.
    if(counterOut == 0 && (isProcessKilled || isWidgetHidden || processExited)){
        flushUnfinishedLines();
        emit outputTreatmentOver();
    }
}


void ProcessLineMaker::slotReceivedStderr()
{
    const QString s = QString::fromLocal8Bit(mProc->readAllStandardError());
    counterErr++;

    // (stdout's unfinished line stays in its buffer, as above)
    stderrbuf += s;
    int pos;
    while ( (pos = stderrbuf.indexOf('\n')) != -1) {
        lineErr = stderrbuf.left(pos);
        stderrbuf.remove(0, pos+1);
        emit receivedStderrLine(lineErr);
    }
    
    // Do not remove this line! It makes the method thread safe.
    //  Because we can be interrupted whenever more data is available,
    //  this prevents printing the same data twice.
    lineErr.clear();

    counterErr--;
    
    //The process has been killed/exited and all the process outputs sent to be printed;
    //warn the processWidget.  processExited must be checked here to mirror slotReceivedStdout —
    //KlustaKwik writes exclusively to stderr, so omitting this flag caused the hang: the
    //outputTreatmentOver signal was never emitted after process exit.
    if(counterErr == 0 && (isProcessKilled || isWidgetHidden || processExited)){
        flushUnfinishedLines();
        emit outputTreatmentOver();
    }
}

void ProcessLineMaker::flushUnfinishedLines()
{
    // The process is over: a last line it did not end with a newline is shown.
    // (Not for a hidden pane alone: its process still runs and the line is unfinished.)
    if (!processExited && !isProcessKilled) return;
    if (!stdoutbuf.isEmpty()) {
        const QString rest = stdoutbuf;
        stdoutbuf.clear();
        emit receivedStdoutLine(rest);
    }
    if (!stderrbuf.isEmpty()) {
        const QString rest = stderrbuf;
        stderrbuf.clear();
        emit receivedStderrLine(rest);
    }
}
