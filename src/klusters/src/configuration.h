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
    /**Sets the post-alignment mode (0=off, 1=PCA refine, 2=RMS recenter).*/
    void setRealignMode(int m)            {realignMode = (m < 0 || m > 2) ? 0 : m;}
    void setReorderMethod(int m)          {reorderMethod = (m < 0 || m > 2) ? 0 : m;}
    /**Merge recommendations: cap on the listed pairs.  Clamped to 1..200 -- the
     * panel ranks every pair before capping, so an unbounded value would only
     * lengthen a list nobody reads to the end of.*/
    void setMergeRecommendMax(int n)      {mergeRecommendMax = (n < 1) ? 1 : ((n > 200) ? 200 : n);}
    /**Lags searched when scoring a pair's envelope overlap, in samples.  Clamped to
     * [0,8]: 0 restores the strict sample-for-sample comparison, and the window is
     * trimmed by maxShift at BOTH ends so every lag is scored on the same samples,
     * which puts a hard ceiling well below nSamp/2.*/
    void setMergeRecommendMaxShift(int n) {mergeRecommendMaxShift = (n < 0) ? 0 : ((n > 8) ? 8 : n);}
    /**Absolute floor on the symmetrised error probability, below which a pair is
     * dropped whatever the residual says.  Clamped to [0,1]: it is a
     * probability, and a floor of 1 simply admits nothing.*/
    void setMergeRecommendErrorFloor(double v)
        {mergeRecommendErrorFloor = (v < 0.0) ? 0.0 : ((v > 1.0) ? 1.0 : v);}
    /**Minimum combined percentile rank to list a pair.  Clamped to [0,1]; it is
     * a rank, not a score.*/
    void setMergeRecommendQualityFloor(double v)
        {mergeRecommendQualityFloor = (v < 0.0) ? 0.0 : ((v > 1.0) ? 1.0 : v);}
    void setCurationLogging(bool b)       {curationLogging = b;}
    void setReorderDisplayOnly(bool b)    {reorderDisplayOnly = b;}
    void setRealignVerbose(bool b)        {realignVerbose = b;}
    void setAutoRealignAfterMerge(bool b) {autoRealignAfterMerge = b;}
    void setAutoRenumberAfterMerge(bool b) {autoRenumberAfterMerge = b;}
    void setAutoUpdateMatricesAfterMerge(bool b) {autoUpdateMatricesAfterMerge = b;}
    void setErrorMatrixIncremental(bool b)  {errorMatrixIncremental  = b;}
    void setErrorMatrixLowPrecision(bool b) {errorMatrixLowPrecision = b;}

    void setDipSplitMinSize(int n)       {dipSplitMinSize     = qMax(2, n);}
    void setDipSplitBloatFactor(double v){dipSplitBloatFactor = qBound(0.0, v, 10.0);}
    void setDipSplitValleyThresh(double v){dipSplitValleyThresh = qBound(0.0, v, 1.0);}

    // KNN voting split (Shift+K) defaults.  Mirrors the dipSplit pattern
    // above: persisted in QSettings, prefilled into the modal Shift+K
    // dialog, written back on accept so the dialog remembers the user's
    // last-used values across sessions.  Bounds match the spinbox ranges
    // in slotSplitClusterByKnn.
    void setKnnK(int n)              {knnK         = qBound(2, n, 200);}
    void setKnnThreshold(double v)   {knnThreshold = qBound(0.0, v, 1.0);}
    void setKnnMinNew(int n)         {knnMinNew    = qBound(1, n, 10000);}
    void setKnnMinRef(int n)         {knnMinRef    = qBound(10, n, 100000);}

    // Auto-Merge (patch 0068) — settings for the Auto-Merge action.
    void setAutoMergeAlgorithm(int a)           {autoMergeAlgorithm     = qBound(0, a, 1);}
    void setAutoMergeMedianK(int k)             {autoMergeMedianK       = qBound(1, k, 500);}
    void setAutoMergeScoreThreshold(double v)   {autoMergeScoreThreshold= qBound(0.0, v, 1.0);}
    void setAutoMergeUseErrorMatrix(bool b)      {autoMergeUseErrorMatrix = b;}
    void setAutoMergeErrorProbThreshold(double v){autoMergeErrorProbThreshold = qBound(0.0, v, 1.0);}
    void setAutoMergeMaxShift(int n)            {autoMergeMaxShift      = qBound(0, n, 128);}
    void setAutoMergeTaperSamples(int n)        {autoMergeTaperSamples  = qBound(0, n, 64);}
    void setAutoMergeMinClusterSize(int n)      {autoMergeMinClusterSize= qBound(2, n, 1000000);}
    void setAutoMergeScope(int s)               {autoMergeScope         = qBound(0, s, 1);}
    void setAutoMergePreviewBeforeApply(bool b) {autoMergePreviewBeforeApply = b;}

    /**Sets the scatter plot marker size.*/
    void setMarkerSize(int size) {markerSize = qBound(1, size, 10);}

    /**Sets the selection polygon line width.*/
    void setSelectionLineWidth(int w) {selectionLineWidth = qBound(1, w, 10);}

    void setTemplateThresholdMin(double v) {templateThresholdMin = qBound(0.0, v, 1.0);}
    void setTemplateThresholdMax(double v) {templateThresholdMax = qBound(0.0, v, 1.0);}
    void setTemplateXcorrMetric(int v)     {templateXcorrMetric = qBound(0, v, 4);}
    
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
    /**Returns the post-alignment mode (0=off, 1=PCA refine, 2=RMS recenter).*/
    int    getRealignMode()       const {return realignMode;}
    int    getReorderMethod()     const {return reorderMethod;}
    int    getMergeRecommendMaxShift()     const {return mergeRecommendMaxShift;}
    int    getMergeRecommendMax()          const {return mergeRecommendMax;}
    double getMergeRecommendErrorFloor()   const {return mergeRecommendErrorFloor;}
    double getMergeRecommendQualityFloor() const {return mergeRecommendQualityFloor;}
    bool   getCurationLogging()   const {return curationLogging;}
    bool   getReorderDisplayOnly()const {return reorderDisplayOnly;}
    bool   getRealignVerbose()    const {return realignVerbose;}
    bool   getAutoRealignAfterMerge() const {return autoRealignAfterMerge;}
    bool   getAutoRenumberAfterMerge() const {return autoRenumberAfterMerge;}
    bool   getAutoUpdateMatricesAfterMerge() const {return autoUpdateMatricesAfterMerge;}
    /**Error matrix: reuse cached raw columns on a single edit (incremental) vs. full recompute.*/
    bool   getErrorMatrixIncremental()  const {return errorMatrixIncremental;}
    bool   getErrorMatrixLowPrecision() const {return errorMatrixLowPrecision;}

    int    getDipSplitMinSize()     const {return dipSplitMinSize;}
    double getDipSplitBloatFactor() const {return dipSplitBloatFactor;}
    double getDipSplitValleyThresh()const {return dipSplitValleyThresh;}

    int    getKnnK()         const {return knnK;}
    double getKnnThreshold() const {return knnThreshold;}
    int    getKnnMinNew()    const {return knnMinNew;}
    int    getKnnMinRef()    const {return knnMinRef;}

    // Auto-Merge (patch 0068) — getters.
    int    getAutoMergeAlgorithm()       const {return autoMergeAlgorithm;}
    int    getAutoMergeMedianK()         const {return autoMergeMedianK;}
    double getAutoMergeScoreThreshold()  const {return autoMergeScoreThreshold;}
    bool   getAutoMergeUseErrorMatrix()  const {return autoMergeUseErrorMatrix;}
    double getAutoMergeErrorProbThreshold() const {return autoMergeErrorProbThreshold;}
    int    getAutoMergeMaxShift()        const {return autoMergeMaxShift;}
    int    getAutoMergeTaperSamples()    const {return autoMergeTaperSamples;}
    int    getAutoMergeMinClusterSize()  const {return autoMergeMinClusterSize;}
    int    getAutoMergeScope()           const {return autoMergeScope;}
    bool   getAutoMergePreviewBeforeApply() const {return autoMergePreviewBeforeApply;}

    /**Returns the scatter plot marker size.*/
    int getMarkerSize() const{return markerSize;}

    /**Returns the selection polygon line width.*/
    int getSelectionLineWidth() const{return selectionLineWidth;}

    double getTemplateThresholdMin() const {return templateThresholdMin;}
    double getTemplateThresholdMax() const {return templateThresholdMax;}
    /// Template-matrix cell metric: 0 = cosine, 1 = Pearson, 2 = raw (non-
    /// normalised) cross-correlation, 3 = noise-disattenuated cosine (within-
    /// cluster mean noise removed from the norms), 4 = fast-AP-windowed cosine
    /// (peak +/- 8 samples only).
    int    getTemplateXcorrMetric() const  {return templateXcorrMetric;}
    /// Convenience: true when the metric is Pearson.  Consumers that need a
    /// bounded [0,1] score (per-spike pair xcorr, autoMerge) read this and so
    /// fall back to cosine when the raw metric is selected.
    bool   getTemplateXcorrPearson() const {return templateXcorrMetric == 1;}

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
    int    getRealignModeDefault()       const {return 0;}  // 0 = off (plain xcorr)
    int    getReorderMethodDefault()     const {return 0;}  // 0 = single-linkage (MST), 1 = spectral (Fiedler), 2 = feature-space (fet PC1)
    // Merge-recommendation defaults.  These are starting guesses, not measured
    // values: 0.05 is low enough to keep any pair the error matrix does not
    // actively disbelieve, and 0.90 asks both matrices to rank a pair in their
    // own top decile before it is called a recommendation.
    int    getMergeRecommendMaxDefault()          const {return 20;}
    int    getMergeRecommendMaxShiftDefault()     const {return 2;}
    double getMergeRecommendErrorFloorDefault()   const {return 0.05;}
    double getMergeRecommendQualityFloorDefault() const {return 0.90;}
    bool   getCurationLoggingDefault()   const {return true;}
    bool   getReorderDisplayOnlyDefault()const {return false;}
    bool   getRealignVerboseDefault()    const {return false;}
    bool   getAutoRealignAfterMergeDefault() const {return true;}
    bool   getAutoRenumberAfterMergeDefault() const {return true;}
    bool   getAutoUpdateMatricesAfterMergeDefault() const {return true;}
    bool   getErrorMatrixIncrementalDefault()  const {return false;}
    bool   getErrorMatrixLowPrecisionDefault() const {return true;}

    int    getDipSplitMinSizeDefault()      const {return 50;}
    double getDipSplitBloatFactorDefault()  const {return 0.0;}
    double getDipSplitValleyThreshDefault() const {return 0.20;}

    int    getKnnKDefault()         const {return 10;}
    double getKnnThresholdDefault() const {return 0.50;}
    int    getKnnMinNewDefault()    const {return 5;}
    int    getKnnMinRefDefault()    const {return 100;}

    // Auto-Merge (patch 0068) — defaults match KKE's flag defaults.
    int    getAutoMergeAlgorithmDefault()        const {return 1;}      ///< median (matches KKE MedianKnn preference)
    int    getAutoMergeMedianKDefault()          const {return 50;}     ///< matches KKE MedianKnnTemplateMatchK
    double getAutoMergeScoreThresholdDefault()   const {return 0.98;}   ///< matches KKE TemplateMatchScore
    bool   getAutoMergeUseErrorMatrixDefault()     const {return false;}  ///< default criterion is template xcorr
    double getAutoMergeErrorProbThresholdDefault() const {return 0.15;}   ///< error-matrix mode merge threshold
    int    getAutoMergeMaxShiftDefault()         const {return 0;}      ///< 0 = auto (nSamp/4)
    int    getAutoMergeTaperSamplesDefault()     const {return 0;}      ///< off
    int    getAutoMergeMinClusterSizeDefault()   const {return 25;}     ///< matches KKE min
    int    getAutoMergeScopeDefault()            const {return 0;}      ///< selected only (safer default)
    bool   getAutoMergePreviewBeforeApplyDefault() const {return true;}

    /**Returns the default scatter plot marker size.*/
    int getMarkerSizeDefault() const{return markerSizeDefault;}

    /**Returns the default selection polygon line width.*/
    int getSelectionLineWidthDefault() const{return selectionLineWidthDefault;}

    bool getUseWhiteColorDuringPrinting() const { return useWhiteColorDuringPrinting; }

    void setUseWhiteColorDuringPrinting(bool b) { useWhiteColorDuringPrinting = b; }

    bool getAutoSelectFeatures() const { return autoSelectFeatures; }
    bool getAutoSelectFeaturesDefault() const { return autoSelectFeaturesDefault; }
    void setAutoSelectFeatures(bool b) { autoSelectFeatures = b; }

    /**Whether the spike timestamp is included as a clustering feature when
     * auto-selecting features for reclustering.  Off by default: including the
     * normalised timestamp makes the reclusterer over-fit to within-session drift.*/
    bool getIncludeTimeInAutoSelect() const { return includeTimeInAutoSelect; }
    bool getIncludeTimeInAutoSelectDefault() const { return includeTimeInAutoSelectDefault; }
    void setIncludeTimeInAutoSelect(bool b) { includeTimeInAutoSelect = b; }

    /**Returns number of top-variance features to pass to KlustaKwik.*/
    int  getAutoSelectNFeatures()        const { return autoSelectNFeatures; }
    /**Returns the default for autoSelectNFeatures.*/
    int  getAutoSelectNFeaturesDefault() const { return autoSelectNFeaturesDefault; }
    /**Sets number of top-variance features to pass to KlustaKwik (clamped 1-25).*/
    void setAutoSelectNFeatures(int n)         { autoSelectNFeatures = qBound(1, n, 25); }

    // patch76 — mean-subtracted sub-dimensional reclustering mode for a
    // single cluster.  Independent of autoSelectFeatures; activating it
    // takes precedence when exactly one cluster is selected for recluster.
    // Reuses autoSelectNFeatures as the K (number of residual-PCA
    // components) to avoid introducing a second knob with the same role.
    bool getReclusterMeanSubtractedSubdim() const
        { return reclusterMeanSubtractedSubdim; }
    bool getReclusterMeanSubtractedSubdimDefault() const
        { return reclusterMeanSubtractedSubdimDefault; }
    void setReclusterMeanSubtractedSubdim(bool b)
        { reclusterMeanSubtractedSubdim = b; }

    // Channel-level high-variance feature selection.  When on (with
    // autoSelectFeatures), recluster ranks *channels* by aggregate feature
    // variance and turns on all PCA columns of the top channels, rather than
    // ranking individual feature columns.  Reuses autoSelectNFeatures as the
    // number of channels to keep.
    bool getReclusterChannelVariance() const
        { return reclusterChannelVariance; }
    bool getReclusterChannelVarianceDefault() const
        { return reclusterChannelVarianceDefault; }
    void setReclusterChannelVariance(bool b)
        { reclusterChannelVariance = b; }

    // Raw-waveform median-residual reclustering (single cluster).
    bool getReclusterMedianWaveformResidual() const
        { return reclusterMedianWaveformResidual; }
    bool getReclusterMedianWaveformResidualDefault() const
        { return reclusterMedianWaveformResidualDefault; }
    void setReclusterMedianWaveformResidual(bool b)
        { reclusterMedianWaveformResidual = b; }

    // patch79 — auto-show the error & template matrices when a document
    // is opened.  Saves the user from manually triggering a new Grouping
    // Assistant Display + Error Matrix dock + Template Matrix dock +
    // Update Error Matrix every time they open a file.  Off by default;
    // an opt-in convenience for users who consistently want both
    // matrices visible after a document loads.  Combines with the
    // existing global geometry/windowState save (closeEvent) to give
    // the requested "same layout on restart" behaviour.
    bool getAutoShowMatricesOnOpen() const
        { return autoShowMatricesOnOpen; }
    bool getAutoShowMatricesOnOpenDefault() const
        { return autoShowMatricesOnOpenDefault; }
    void setAutoShowMatricesOnOpen(bool b)
        { autoShowMatricesOnOpen = b; }

    /**Returns the fractional margin applied to the autoscale fit in
     * ClusterView (F key), expressed as a percent of the data extent on
     * each side.  Default 5 (i.e. 5% on each side).  Range 0–50.*/
    double getAutoscaleMarginPercent()        const { return autoscaleMarginPercent; }
    /**Returns the default for the autoscale margin (5%).*/
    double getAutoscaleMarginPercentDefault() const { return autoscaleMarginPercentDefault; }
    /**Sets the autoscale fit margin (clamped 0–50%).*/
    void   setAutoscaleMarginPercent(double p)      {
        if (p < 0.0)  p = 0.0;
        if (p > 50.0) p = 50.0;
        autoscaleMarginPercent = p;
    }

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
    int     realignMode;
    int     reorderMethod;   // reorder-by-similarity: 0 = single-linkage (MST), 1 = spectral (Fiedler), 2 = feature-space (fet PC1)
    int     mergeRecommendMax;                // merge recommendations: max pairs listed
    int     mergeRecommendMaxShift;           // merge recommendations: lags searched, samples
    double  mergeRecommendErrorFloor;     // absolute floor on the error probability
    double  mergeRecommendQualityFloor;   // min combined percentile rank
    bool    curationLogging;   // record per-action curation audit snapshots
    bool    reorderDisplayOnly;// reorder-by-similarity: rearrange matrix display only, no cluster renumber
    bool    realignVerbose;    // stream per-spike realignment detail to stderr
    bool    autoRealignAfterMerge;  // run spike alignment after each interactive merge
    bool    autoRenumberAfterMerge;        // renumber clusters after each merge (interactive + auto-merge)
    bool    autoUpdateMatricesAfterMerge;  // recompute error/template/residual matrices after each merge
    bool    errorMatrixIncremental;   // error matrix: incremental reuse on single edits (else full recompute)
    bool    errorMatrixLowPrecision;  // error matrix: FP32 GPU compute (fast) vs FP64 (exact)

    int     dipSplitMinSize;       ///< minimum cluster size to consider for DipSplit
    double  dipSplitBloatFactor;   ///< Mahalanobis bloat threshold (× χ²(d, 0.9))
    double  dipSplitValleyThresh;  ///< minimum KDE valley depth in [0, 1]

    // KNN voting split (Shift+K) — see slotSplitClusterByKnn.
    int     knnK;                  ///< K neighbours per spike (2-200)
    double  knnThreshold;          ///< majority vote threshold (fraction of K, 0-1)
    int     knnMinNew;             ///< min size for a new cluster to be kept (1-10000)
    int     knnMinRef;             ///< min "good" reference-cluster size (10-100000)

    // Auto-Merge (patch 0068) — settings for the upcoming Auto-Merge action.
    // Mirrors KKE's WithinChunkTemplateMatch / MedianKnn parameters so
    // klusters' interactive merge uses the same mechanism KKE does
    // offline.  Persisted in QSettings, restored on dialog open.
    int     autoMergeAlgorithm;    ///< 0 = mean templates, 1 = median templates
    int     autoMergeMedianK;      ///< neighbour count for median template (median mode only)
    double  autoMergeScoreThreshold; ///< minimum xcorr score to merge a pair (0.0-1.0)
    bool    autoMergeUseErrorMatrix; ///< true = error-matrix criterion, false = template xcorr
    double  autoMergeErrorProbThreshold; ///< error-matrix mode: merge threshold on max directed confusion (0.0-1.0)
    int     autoMergeMaxShift;     ///< max xcorr shift in samples; 0 = auto (nSamp/4)
    int     autoMergeTaperSamples; ///< Hann taper window length on each end; 0 = off
    int     autoMergeMinClusterSize; ///< clusters smaller than this are skipped
    int     autoMergeScope;        ///< 0 = selected only, 1 = all active
    bool    autoMergePreviewBeforeApply;
    /**Scatter plot marker size in pixels.*/
    int markerSize;
    /**Selection polygon line width in pixels.*/
    int selectionLineWidth;
    double templateThresholdMin;
    double templateThresholdMax;
    int templateXcorrMetric = 0;   // 0=cosine 1=pearson 2=raw 3=disatten 4=fastAP

    bool useWhiteColorDuringPrinting;
    bool autoSelectFeatures;
    static const bool autoSelectFeaturesDefault;
    bool includeTimeInAutoSelect;
    static const bool includeTimeInAutoSelectDefault;
    int  autoSelectNFeatures;
    static const int  autoSelectNFeaturesDefault;

    // patch76 — mean-subtracted sub-dimensional reclustering for one cluster
    bool reclusterMeanSubtractedSubdim;
    static const bool reclusterMeanSubtractedSubdimDefault;
    bool reclusterChannelVariance;
    static const bool reclusterChannelVarianceDefault;
    bool reclusterMedianWaveformResidual;
    static const bool reclusterMedianWaveformResidualDefault;

    // patch79 — auto-show error & template matrices on document open
    bool autoShowMatricesOnOpen;
    static const bool autoShowMatricesOnOpenDefault;
    /**Margin (percent of data extent) added on each side of the autoscale fit
     * in ClusterView (F key).  0 = tight fit, 5 = original behaviour.*/
    double autoscaleMarginPercent;
    static const double autoscaleMarginPercentDefault;
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
