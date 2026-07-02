/***************************************************************************
                          configuration.cpp  -  description
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

//include files for the application
#include "configuration.h"
#include <QSettings>

/**
  *@author Lynn Hazan
*/


const bool Configuration::crashRecoveryDefault = true;
const bool Configuration::autoSelectFeaturesDefault = false;
const int  Configuration::autoSelectNFeaturesDefault = 7;
// patch76 — opt-in: off by default to preserve existing recluster behaviour
const bool Configuration::reclusterMeanSubtractedSubdimDefault = false;
const bool Configuration::reclusterChannelVarianceDefault = false;
const bool Configuration::reclusterMedianWaveformResidualDefault = false;
// patch79 — opt-in: off by default
const bool Configuration::autoShowMatricesOnOpenDefault = false;
const double Configuration::autoscaleMarginPercentDefault = 5.0;
const int  Configuration::crashRecoveryIndexDefault = 0;
const int  Configuration::gainDefault = 200;
const int  Configuration::timeIntervalDefault = 60;
const int  Configuration::nbUndoDefault = 2;
const QColor Configuration::backgroundColorDefault = QColor(Qt::black);
const QString Configuration::reclusteringExecutableDefault = QLatin1String("KlustaKwik");
const QString Configuration::reclusteringArgsDefault =
        "%fileBaseName %electrodeGroupID -MinClusters 2 -MaxClusters 12 -UseFeatures %features";
const QString Configuration::realignExecutableDefault = QLatin1String("");
const QString Configuration::realignArgsDefault = QLatin1String("--threshold 0.70 --iterations 2");
const int  Configuration::markerSizeDefault = 2;
const int  Configuration::selectionLineWidthDefault = 1;

Configuration::Configuration():nbChannels(0) {
    read(); // read the settings or set them to the default values
}

void Configuration::read() {
    QSettings settings;

    settings.beginGroup("General");
    crashRecovery = settings.value("crashRecovery",crashRecoveryDefault).toBool();
    crashRecoveryIndex = settings.value("crashRecoveryIndex",crashRecoveryIndexDefault).toInt();
    nbUndo = settings.value("nbUndo",nbUndoDefault).toInt();
    backgroundColor = settings.value("backgroundColor", backgroundColorDefault).value<QColor>();
    reclusteringExecutable = settings.value("reclusteringExecutable",reclusteringExecutableDefault).toString();
    reclusteringArgs = settings.value("reclusteringArgs",reclusteringArgsDefault).toString();
    realignExecutable = settings.value("realignExecutable",realignExecutableDefault).toString();
    realignArgs = settings.value("realignArgs",realignArgsDefault).toString();
    realignThreshold  = settings.value("realignThreshold",  0.70).toDouble();
    realignIterations = settings.value("realignIterations",  2).toInt();
    realignMaxShift   = settings.value("realignMaxShift",    0).toInt();
    realignMode       = settings.value("realignMode",        0).toInt();
    curationLogging   = settings.value("curationLogging",    true).toBool();
    realignVerbose    = settings.value("realignVerbose",     false).toBool();
    autoRealignAfterMerge = settings.value("autoRealignAfterMerge", true).toBool();
    autoRenumberAfterMerge = settings.value("autoRenumberAfterMerge", true).toBool();
    autoUpdateMatricesAfterMerge = settings.value("autoUpdateMatricesAfterMerge", true).toBool();
    errorMatrixIncremental  = settings.value("errorMatrixIncremental",  false).toBool();
    errorMatrixLowPrecision = settings.value("errorMatrixLowPrecision", false).toBool();
    dipSplitMinSize      = settings.value("dipSplitMinSize",      50).toInt();
    dipSplitBloatFactor  = settings.value("dipSplitBloatFactor",  0.0).toDouble();
    dipSplitValleyThresh = settings.value("dipSplitValleyThresh", 0.20).toDouble();
    knnK         = settings.value("knnK",         10).toInt();
    knnThreshold = settings.value("knnThreshold", 0.50).toDouble();
    knnMinNew    = settings.value("knnMinNew",    5).toInt();
    knnMinRef    = settings.value("knnMinRef",    100).toInt();

    // Auto-Merge (patch 0068) — defaults match KKE flag defaults.
    autoMergeAlgorithm           = settings.value("autoMergeAlgorithm",           1).toInt();
    autoMergeMedianK             = settings.value("autoMergeMedianK",             50).toInt();
    autoMergeScoreThreshold      = settings.value("autoMergeScoreThreshold",      0.98).toDouble();
    autoMergeMaxShift            = settings.value("autoMergeMaxShift",            0).toInt();
    autoMergeTaperSamples        = settings.value("autoMergeTaperSamples",        0).toInt();
    autoMergeMinClusterSize      = settings.value("autoMergeMinClusterSize",      25).toInt();
    autoMergeScope               = settings.value("autoMergeScope",               0).toInt();
    autoMergePreviewBeforeApply  = settings.value("autoMergePreviewBeforeApply",  true).toBool();
    markerSize = settings.value("markerSize", markerSizeDefault).toInt();
    selectionLineWidth = settings.value("selectionLineWidth", selectionLineWidthDefault).toInt();
    templateThresholdMin = settings.value("templateThresholdMin", 0.5).toDouble();
    templateThresholdMax = settings.value("templateThresholdMax", 1.0).toDouble();
    if (settings.contains("templateXcorrMetric"))
        templateXcorrMetric = settings.value("templateXcorrMetric", 0).toInt();
    else  // migrate the pre-3.x boolean key (Pearson on/off)
        templateXcorrMetric = settings.value("templateXcorrPearson", false).toBool() ? 1 : 0;
    templateXcorrMetric = qBound(0, templateXcorrMetric, 4);
    useWhiteColorDuringPrinting = settings.value("useWhiteColorDuringPrinting",true).toBool();
    autoSelectFeatures = settings.value("autoSelectFeatures", autoSelectFeaturesDefault).toBool();
    autoSelectNFeatures = settings.value("autoSelectNFeatures", autoSelectNFeaturesDefault).toInt();
    // patch76 — single-cluster mean-subtracted sub-dimensional recluster
    reclusterMeanSubtractedSubdim = settings.value(
        "reclusterMeanSubtractedSubdim",
        reclusterMeanSubtractedSubdimDefault).toBool();
    reclusterChannelVariance = settings.value(
        "reclusterChannelVariance",
        reclusterChannelVarianceDefault).toBool();
    reclusterMedianWaveformResidual = settings.value(
        "reclusterMedianWaveformResidual",
        reclusterMedianWaveformResidualDefault).toBool();
    // patch79 — auto-show error & template matrices on document open
    autoShowMatricesOnOpen = settings.value(
        "autoShowMatricesOnOpen",
        autoShowMatricesOnOpenDefault).toBool();
    autoscaleMarginPercent = settings.value("autoscaleMarginPercent", autoscaleMarginPercentDefault).toDouble();
    settings.endGroup();

    //read cluster view options
    settings.beginGroup("clusterView");
    timeInterval = settings.value("timeInterval",timeIntervalDefault).toInt();
    settings.endGroup();

    //read waveform view options
    settings.beginGroup("waveformView");
    gain = settings.value("gain",gainDefault).toInt();
    settings.endGroup();
}

void Configuration::write() const {  
    QSettings settings;

    settings.beginGroup("General");
    settings.setValue("crashRecovery",crashRecovery);
    settings.setValue("crashRecoveryIndex",crashRecoveryIndex);
    settings.setValue("nbUndo",nbUndo);
    settings.setValue("backgroundColor",backgroundColor);
    settings.setValue("reclusteringExecutable",reclusteringExecutable);
    settings.setValue("reclusteringArgs",reclusteringArgs);
    settings.setValue("realignExecutable",realignExecutable);
    settings.setValue("realignArgs",realignArgs);
    settings.setValue("realignThreshold",  realignThreshold);
    settings.setValue("realignIterations", realignIterations);
    settings.setValue("realignMaxShift",   realignMaxShift);
    settings.setValue("realignMode",       realignMode);
    settings.setValue("curationLogging",   curationLogging);
    settings.setValue("realignVerbose",    realignVerbose);
    settings.setValue("autoRealignAfterMerge", autoRealignAfterMerge);
    settings.setValue("autoRenumberAfterMerge", autoRenumberAfterMerge);
    settings.setValue("autoUpdateMatricesAfterMerge", autoUpdateMatricesAfterMerge);
    settings.setValue("errorMatrixIncremental",  errorMatrixIncremental);
    settings.setValue("errorMatrixLowPrecision", errorMatrixLowPrecision);
    settings.setValue("dipSplitMinSize",      dipSplitMinSize);
    settings.setValue("dipSplitBloatFactor",  dipSplitBloatFactor);
    settings.setValue("dipSplitValleyThresh", dipSplitValleyThresh);
    settings.setValue("knnK",         knnK);
    settings.setValue("knnThreshold", knnThreshold);
    settings.setValue("knnMinNew",    knnMinNew);
    settings.setValue("knnMinRef",    knnMinRef);

    // Auto-Merge (patch 0068)
    settings.setValue("autoMergeAlgorithm",           autoMergeAlgorithm);
    settings.setValue("autoMergeMedianK",             autoMergeMedianK);
    settings.setValue("autoMergeScoreThreshold",      autoMergeScoreThreshold);
    settings.setValue("autoMergeMaxShift",            autoMergeMaxShift);
    settings.setValue("autoMergeTaperSamples",        autoMergeTaperSamples);
    settings.setValue("autoMergeMinClusterSize",      autoMergeMinClusterSize);
    settings.setValue("autoMergeScope",               autoMergeScope);
    settings.setValue("autoMergePreviewBeforeApply",  autoMergePreviewBeforeApply);
    settings.setValue("markerSize", markerSize);
    settings.setValue("selectionLineWidth", selectionLineWidth);
    settings.setValue("templateThresholdMin", templateThresholdMin);
    settings.setValue("templateThresholdMax", templateThresholdMax);
    settings.setValue("templateXcorrMetric", templateXcorrMetric);
    settings.setValue("useWhiteColorDuringPrinting",useWhiteColorDuringPrinting);
    settings.setValue("autoSelectFeatures", autoSelectFeatures);
    settings.setValue("autoSelectNFeatures", autoSelectNFeatures);
    // patch76
    settings.setValue("reclusterMeanSubtractedSubdim",
                      reclusterMeanSubtractedSubdim);
    settings.setValue("reclusterChannelVariance",
                      reclusterChannelVariance);
    settings.setValue("reclusterMedianWaveformResidual",
                      reclusterMedianWaveformResidual);
    // patch79
    settings.setValue("autoShowMatricesOnOpen",
                      autoShowMatricesOnOpen);
    settings.setValue("autoscaleMarginPercent", autoscaleMarginPercent);
    settings.endGroup();
    
    //write cluster view options
    settings.beginGroup("clusterView");
    settings.setValue("timeInterval",timeInterval);
    settings.endGroup();

    //write waveform view options
    settings.beginGroup("waveformView");
    settings.setValue("gain",gain);
    settings.beginGroup("General");
}

Configuration& configuration() {
    //The C++ standard requires that static variables in functions
    //have to be created upon first call of the function.
    static Configuration conf;
    return conf;
}
