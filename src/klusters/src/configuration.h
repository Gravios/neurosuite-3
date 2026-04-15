/***************************************************************************
                          configuration.h  -  description
                             -------------------
    begin                : Thu Dec 12 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                :
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

// include files for QT
#include <QString>
#include <QFont>
#include <QColor>

#include <QList>


/**
  * This is the one and only configuration object.
  * The member functions read() and write() can be used to load and save
  * the properties to the application configuration file.
  *@author Lynn Hazan
*/

class Configuration {
public:
    /** Reads the configuration data from the application config file.
    * If a property does not already exist in the config file it will be
    * set to a default value.*/
    void read();
    /** Writes the configuration data to the application config file.*/
    void write() const;

    /**Sets the use of a crash and recovery autosave.*/
    void setCrashRecovery(bool use){crashRecovery = use;}

    /**Sets the time interval between 2 crash and recovery autosave.*/
    void setCrashRecoveryIndex(int index){crashRecoveryIndex = index;}

    /**Sets the gain used to display the waveforms.*/
    void setGain(int gain){this->gain = gain;}

    /**Sets the time interval between 2 lines drawn in the cluster views
    * when the time dimension in selected. The time @p time is in second.*/
    void setTimeInterval(int time){timeInterval = time;}

    /**Sets the number of step in the undo/redo mechanism.*/
    void setNbUndo(int nb){nbUndo = nb;}

    /**Sets the positions of the channels.*/
    void setChannelPositions(const QList<int>& positions){
        channelPositions.clear();
        QList<int>::const_iterator iterator;
        QList<int>::const_iterator end(positions.constEnd());
        for(iterator = positions.constBegin(); iterator != end; ++iterator)
            channelPositions.append(*iterator);
    }

    /**Sets the number of channels.*/
    void setNbChannels(int nb){nbChannels = nb;}

    /**Sets the background color.*/
    void setBackgroundColor(const QColor& color) {backgroundColor = color;}

    /**Sets the reclustering executable.*/
    void setReclusteringExecutable(const QString& executable) {reclusteringExecutable = executable;}

    /**Sets the arguments for the reclustering.*/
    void setReclusteringArguments(const QString& arguments) {reclusteringArgs = arguments;}

    /**Sets the realignment executable.*/
    void setRealignExecutable(const QString& executable) {realignExecutable = executable;}

    /**Sets the arguments for the realignment.*/
    void setRealignArguments(const QString& arguments) {realignArgs = arguments;}

    void setRealignThreshold(double v)  {realignThreshold = qBound(0.0, v, 1.0);}
    void setRealignIterations(int n)     {realignIterations = qMax(1, n);}
    void setRealignMaxShift(int n)        {realignMaxShift = qMax(0, n);}

    /**Sets the scatter plot marker size.*/
    void setMarkerSize(int size) {markerSize = qBound(1, size, 10);}

    /**Sets the selection polygon line width.*/
    void setSelectionLineWidth(int w) {selectionLineWidth = qBound(1, w, 10);}

    void setTemplateThresholdMin(double v) {templateThresholdMin = qBound(0.0, v, 1.0);}
    void setTemplateThresholdMax(double v) {templateThresholdMax = qBound(0.0, v, 1.0);}
    
    /**Returns true if a crash and recovery autosave is performed, false othewise.*/
    bool isCrashRecovery() const{return crashRecovery;}

    /**Returns the time interval between 2 crash and recovery autosave in minutes.*/
    int crashRecoveryInterval() const{
        switch(crashRecoveryIndex){
        case 0:
            return 1;
        case 1:
            return 3;
        case 2:
            return 5;
        case 3:
            return 15;
        case 4:
            return 30;
        default:
            return 1;
        }
    }

    /**Returns the index corresponding to the time interval between
    * 2 crash and recovery autosave in minutes.*/
    int crashRecoveryIntervalIndex() const{return crashRecoveryIndex;}

    /**Returns the gain used to display the waveforms.*/
    int getGain() const{return gain;}

    /**Returns the time interval between 2 lines drawn in the cluster views
    * when the time dimension in selected. The time is in second.*/
    int getTimeInterval() const{return timeInterval;}

    /**Returns the number of step in the undo/redo mechanism.*/
    int getNbUndo() const{return nbUndo;}

    /**Returns the positions of the channels.*/
    QList<int>* getChannelPositions() {return &channelPositions;}

    /**Returns the number of channels.*/
    int getNbChannels() const{return nbChannels;}

    /**Returns the background color.*/
    QColor getBackgroundColor() const{return backgroundColor;}
    
    /**Returns the reclustering executable.*/
    QString getReclusteringExecutable() const{return reclusteringExecutable;}

    /**Returns the arguments for the reclustering.*/
    QString getReclusteringArguments() const{return reclusteringArgs;}

    /**Returns the realignment executable.*/
    QString getRealignExecutable() const{return realignExecutable;}

    /**Returns the arguments for the realignment.*/
    QString getRealignArguments() const{return realignArgs;}

    double getRealignThreshold()  const {return realignThreshold;}
    int    getRealignIterations() const {return realignIterations;}
    int    getRealignMaxShift()   const {return realignMaxShift;}

    /**Returns the scatter plot marker size.*/
    int getMarkerSize() const{return markerSize;}

    /**Returns the selection polygon line width.*/
    int getSelectionLineWidth() const{return selectionLineWidth;}

    double getTemplateThresholdMin() const {return templateThresholdMin;}
    double getTemplateThresholdMax() const {return templateThresholdMax;}

    /**Returns the default value for the crash and recovery mechanism.
    * True if a crash and recovery autosave is performed, false othewise.*/
    bool isCrashRecoveryDefault() const{return crashRecoveryDefault;}

    /**Returns the index corresponding to the default time interval between
    * 2 crash and recovery autosave in minutes.*/
    int crashRecoveryIntervalIndexDefault() const{return crashRecoveryIndexDefault;}

    /**Returns the default gain used to display the waveforms.*/
    int getGainDefault() const{return gainDefault;}
    /**Returns the default time interval between 2 lines drawn in the cluster views
    * when the time dimension in selected. The time is in second.*/
    int getTimeIntervalDefault() const{return timeIntervalDefault;}

    /**Returns the default number of step in the undo/redo mechanism.*/
    int getNbUndoDefault() const{return nbUndoDefault;}

    /**Returns the the default background color.*/
    QColor getBackgroundColorDefault() const{return backgroundColorDefault;}

    /**Returns the default reclustering executable.*/
    QString getReclusteringExecutableDefault() const{return reclusteringExecutableDefault;}

    /**Returns the default arguments for the reclustering.*/
    QString getReclusteringArgumentsDefault() const{return reclusteringArgsDefault;}

    /**Returns the default realignment executable.*/
    QString getRealignExecutableDefault() const{return realignExecutableDefault;}

    /**Returns the default arguments for the realignment.*/
    QString getRealignArgumentsDefault() const{return realignArgsDefault;}

    double getRealignThresholdDefault()  const {return 0.70;}
    int    getRealignIterationsDefault() const {return 2;}
    int    getRealignMaxShiftDefault()   const {return 0;}  // 0 = use peakSamp/2

    /**Returns the default scatter plot marker size.*/
    int getMarkerSizeDefault() const{return markerSizeDefault;}

    /**Returns the default selection polygon line width.*/
    int getSelectionLineWidthDefault() const{return selectionLineWidthDefault;}

    bool getUseWhiteColorDuringPrinting() const { return useWhiteColorDuringPrinting; }

    void setUseWhiteColorDuringPrinting(bool b) { useWhiteColorDuringPrinting = b; }

    bool getAutoSelectFeatures() const { return autoSelectFeatures; }
    bool getAutoSelectFeaturesDefault() const { return autoSelectFeaturesDefault; }
    void setAutoSelectFeatures(bool b) { autoSelectFeatures = b; }

    /**Returns number of top-variance features to pass to KlustaKwik.*/
    int  getAutoSelectNFeatures()        const { return autoSelectNFeatures; }
    /**Returns the default for autoSelectNFeatures.*/
    int  getAutoSelectNFeaturesDefault() const { return autoSelectNFeaturesDefault; }
    /**Sets number of top-variance features to pass to KlustaKwik (clamped 1-25).*/
    void setAutoSelectNFeatures(int n)         { autoSelectNFeatures = qBound(1, n, 25); }

private:
    /**Boolean indicating if a crash and recovery is ask.*/
    bool crashRecovery;
    /**Index of a dropdown list giving the time-interval between 2 autosave for a crashRecovery.*/
    int  crashRecoveryIndex;
    /**Initial gain used to display the waveforms.*/
    int  gain;
    /**Time interval between 2 lines drawn in the cluster views when the time dimension in selected.*/
    int  timeInterval;
    /**Number of step in the undo/redo mechanism.*/
    int  nbUndo;
    /**Positions of the channels in the waveform view.*/
    QList<int> channelPositions;
    /**Number of channels.*/
    int nbChannels;
    /**Background color of the views.*/
    QColor backgroundColor;
    /**Path to the reclustering executable.*/
    QString reclusteringExecutable;
    /**Arguments for the reclustering executable.*/
    QString reclusteringArgs;
    /**Path to the realignment executable.*/
    QString realignExecutable;
    /**Arguments for the realignment executable.*/
    QString realignArgs;
    double  realignThreshold;
    int     realignIterations;
    int     realignMaxShift;
    /**Scatter plot marker size in pixels.*/
    int markerSize;
    /**Selection polygon line width in pixels.*/
    int selectionLineWidth;
    double templateThresholdMin;
    double templateThresholdMax;

    bool useWhiteColorDuringPrinting;
    bool autoSelectFeatures;
    static const bool autoSelectFeaturesDefault;
    int  autoSelectNFeatures;
    static const int  autoSelectNFeaturesDefault;
    static const bool crashRecoveryDefault;
    static const int  crashRecoveryIndexDefault;
    static const int  gainDefault;
    static const int  timeIntervalDefault;
    static const int  nbUndoDefault;
    static const QColor backgroundColorDefault;
    static const QString reclusteringExecutableDefault;
    static const QString reclusteringArgsDefault;
    static const QString realignExecutableDefault;
    static const QString realignArgsDefault;
    static const int  markerSizeDefault;
    static const int  selectionLineWidthDefault;

    Configuration();
    Configuration(const Configuration&);

    friend Configuration& configuration();
};

/// Returns a reference to the application configuration object.
Configuration& configuration();

#endif  // CONFIGURATION_H
