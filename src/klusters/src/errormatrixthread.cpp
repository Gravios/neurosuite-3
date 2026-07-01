/***************************************************************************
                          errormatrixthread.cpp  -  description
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
//include files for the application
#include "errormatrixthread.h"

//QT include files
#include <QApplication>
#include <cmath>
#include <algorithm>
#include <cstdio>

void ErrorMatrixThread::run(){
    if(!haveToStopProcessing){
        Array<double>* result = nullptr;

        if(incremental){
            result = assistant.computeMeanProbabilitiesIncremental(
                data, clusterList, computedClusterList, ignoreClusterIndex,
                prevRaw, prevRawIds, prevRawSizes, prevNbDimensions, changedIds,
                &newRaw, &newRawIds, &newRawSizes, &nbReused, verify);

            if(result != nullptr){
                usedIncremental = true;
                newRawDims = data.nbOfDimensionsTotal() - 1;

                // Optional self-verification: recompute the FULL matrix with a
                // fresh assistant and log the largest absolute cell discrepancy.
                // A logic error surfaces as an O(0.1-1) delta; a tiny residual
                // (~1e-9) is expected GPU/CPU summation drift when the full path
                // runs on the GPU.  Verification never changes the returned
                // result — it only reports.
                if(verify && !haveToStopProcessing){
                    GroupingAssistant fullAssistant;
                    QList<int> cl, ccl, ici;
                    Array<double>* full =
                        fullAssistant.computeMeanProbabilities(data, cl, ccl, ici);
                    if(full != nullptr){
                        double maxAbs = 0.0;
                        const long rr = std::min(result->nbOfRows(),    full->nbOfRows());
                        const long cc = std::min(result->nbOfColumns(), full->nbOfColumns());
                        const bool sameShape = (result->nbOfRows()==full->nbOfRows()
                                                && result->nbOfColumns()==full->nbOfColumns());
                        for(long i=1;i<=rr;++i)
                            for(long j=1;j<=cc;++j){
                                double d = std::fabs((*result)(i,j) - (*full)(i,j));
                                if(d>maxAbs) maxAbs=d;
                            }
                        fprintf(stderr,
                            "[errormatrix-incremental] reused %d/%d columns; verify "
                            "max|delta|=%.3e%s\n",
                            nbReused, static_cast<int>(clusterList.size()), maxAbs,
                            sameShape ? "" : " (SHAPE MISMATCH vs full!)");
                        delete full;
                    }
                }
            } else {
                // Incremental hit a hard precondition miss: discard any partial
                // outputs and fall through to the full path below.
                clusterList.clear(); computedClusterList.clear(); ignoreClusterIndex.clear();
                delete newRaw; newRaw = nullptr; newRawIds.clear(); newRawSizes.clear();
                usedIncremental = false;
            }
        }

        if(result == nullptr && !haveToStopProcessing){
            // Full recompute — the default path and the incremental fallback.
            result = assistant.computeMeanProbabilities(
                data, clusterList, computedClusterList, ignoreClusterIndex);
        }

        probabilities = result;
    }

    //Send an event to the ErrorMatrixView to let it know that the computation is finish.
    ErrorMatrixEvent* event = getErrorMatrixEvent();
    QApplication::postEvent(&errorMatrixView,event);
}

