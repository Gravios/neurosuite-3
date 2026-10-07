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
// Off by default: including the timestamp as a clustering feature over-fits drift.
const bool Configuration::includeTimeInAutoSelectDefault = false;
const int  Configuration::autoSelectNFeaturesDefault = 7;
// patch76 — opt-in: off by default to preserve existing recluster behaviour
const bool Configuration::reclusterMeanSubtractedSubdimDefault = false;
const bool Configuration::reclusterChannelVarianceDefault = false;
const bool Configuration::reclusterMedianWaveformResidualDefault = false;
// patch79 — opt-in: off by default
const bool Configuration::autoShowMatricesOnOpenDefault = false;
// Template Library display: open by default (on)
const bool Configuration::autoShowTemplateLibraryOnOpenDefault = true;
const bool Configuration::templatesModeDefault = true;
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
    reorderMethod     = settings.value("reorderMethod",      0).toInt();
    // Through the setters, not straight to the members like the knobs above: a
    // hand-edited settings file could otherwise put a negative max here, and the
    // panel casts it to size_t -- which underflows to "no cap" and lists every
    // pair in the session.  The setters clamp.
    setMergeRecommendMax(settings.value("mergeRecommendMax",                 20).toInt());
    setShowMergeRecommendPanel(settings.value("showMergeRecommendPanel",     true).toBool());
    setDriftSliderMaxClusters(settings.value("driftSliderMaxClusters",      1000).toInt());
    setMergeRecommendMaxShift(settings.value("mergeRecommendMaxShift",        2).toInt());
    setMergeRecommendErrorFloor(settings.value("mergeRecommendErrorFloor",   0.05).toDouble());
    setMergeRecommendQualityFloor(settings.value("mergeRecommendQualityFloor", 0.90).toDouble());
    curationLogging   = settings.value("curationLogging",    true).toBool();
    reorderDisplayOnly= settings.value("reorderDisplayOnly", false).toBool();
    realignVerbose    = settings.value("realignVerbose",     false).toBool();
    reextractSpikesOnSave = settings.value("reextractSpikesOnSave", false).toBool();
    autoRealignAfterMerge = settings.value("autoRealignAfterMerge", true).toBool();
    autoRenumberAfterMerge = settings.value("autoRenumberAfterMerge", true).toBool();
    tsneSpikeCap = settings.value("tsneSpikeCap", 32000).toInt();
    if (tsneSpikeCap < 1000) tsneSpikeCap = 1000;
    tsnePerplexityStep = settings.value("tsnePerplexityStep", 5).toInt();
    if (tsnePerplexityStep < 1) tsnePerplexityStep = 1;
    setTsneStartPerplexity(settings.value("tsneStartPerplexity", 30.0).toDouble());
    setTsneIterations(settings.value("tsneIterations", 500).toInt());
    setTsneTheta(settings.value("tsneTheta", 0.5).toDouble());
    setTsneMaxDimensions(settings.value("tsneMaxDimensions", 0).toInt());
    setTsneSubsampleOverCap(settings.value("tsneSubsampleOverCap", false).toBool());
    setTsneRandomSeed(settings.value("tsneRandomSeed", false).toBool());
    setTsneLearningRate(settings.value("tsneLearningRate", 200.0).toDouble());
    setTsneExaggeration(settings.value("tsneExaggeration", 12.0).toDouble());
    setTsneExaggerationIterations(settings.value("tsneExaggerationIterations", 250).toInt());
    autoUpdateMatricesAfterMerge = settings.value("autoUpdateMatricesAfterMerge", true).toBool();
    errorMatrixIncremental  = settings.value("errorMatrixIncremental",  false).toBool();
    errorMatrixLowPrecision = settings.value("errorMatrixLowPrecision", true).toBool();
    dipSplitMinSize      = settings.value("dipSplitMinSize",      50).toInt();
    dipSplitBloatFactor  = settings.value("dipSplitBloatFactor",  0.0).toDouble();
    dipSplitValleyThresh = settings.value("dipSplitValleyThresh", 0.20).toDouble();
    projectionScopeMode        = settings.value("projectionScopeMode",        0).toInt();
    projectionScopeMinutes     = settings.value("projectionScopeMinutes",     12.0).toDouble();
    projectionOutOfScopeHidden = settings.value("projectionOutOfScopeHidden", false).toBool();
    showEapCollisions          = settings.value("showEapCollisions",          false).toBool();
    showEapClassMembers        = settings.value("showEapClassMembers",        false).toBool();
    knnK         = settings.value("knnK",         10).toInt();
    knnThreshold = settings.value("knnThreshold", 0.50).toDouble();
    knnMinNew    = settings.value("knnMinNew",    5).toInt();
    knnMinRef    = settings.value("knnMinRef",    100).toInt();

    // Auto-Merge (patch 0068) — defaults match KKE flag defaults.
    autoMergeAlgorithm           = settings.value("autoMergeAlgorithm",           1).toInt();
    autoMergeMedianK             = settings.value("autoMergeMedianK",             50).toInt();
    autoMergeScoreThreshold      = settings.value("autoMergeScoreThreshold",      0.98).toDouble();
    autoMergeUseErrorMatrix      = settings.value("autoMergeUseErrorMatrix",      false).toBool();
    autoMergeErrorProbThreshold  = settings.value("autoMergeErrorProbThreshold",  0.15).toDouble();
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
    includeTimeInAutoSelect = settings.value("includeTimeInAutoSelect", includeTimeInAutoSelectDefault).toBool();
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
    autoShowTemplateLibraryOnOpen = settings.value(
        "autoShowTemplateLibraryOnOpen",
        autoShowTemplateLibraryOnOpenDefault).toBool();
    templatesMode = settings.value(
        "templatesMode",
        templatesModeDefault).toBool();
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

    //read the input-binding overrides (input-remapping plan §5): a thin diff from
    //the shipped defaults, keyed by command id -> Chord::toString().  The app
    //applies these to input::registry() at startup; stored as a QSettings array so
    //no reliance on command-id characters as keys.
    inputBindingOverrides.clear();
    settings.beginGroup("inputBindings");
    const int nOverrides = settings.beginReadArray("overrides");
    for(int i = 0; i < nOverrides; ++i){
        settings.setArrayIndex(i);
        const QString id    = settings.value("id").toString();
        const QString chord = settings.value("chord").toString();
        if(!id.isEmpty() && !chord.isEmpty())
            inputBindingOverrides.insert(id, chord);
    }
    settings.endArray();
    settings.endGroup();

    //read the saved keymap layouts (input-remapping plan "Keymap profiles"): a named set
    //of binding overrides each, stored as the serialized keymap text (name -> text).  The
    //Preferences ▸ Input layout picker parses them; kept as strings so Configuration stays
    //decoupled from the input module.
    inputProfiles.clear();
    settings.beginGroup("inputProfiles");
    const int nProfiles = settings.beginReadArray("profiles");
    for(int i = 0; i < nProfiles; ++i){
        settings.setArrayIndex(i);
        const QString name = settings.value("name").toString();
        const QString text = settings.value("text").toString();
        if(!name.isEmpty() && !text.isEmpty())
            inputProfiles.insert(name, text);
    }
    settings.endArray();
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
    settings.setValue("reorderMethod",     reorderMethod);
    settings.setValue("mergeRecommendMax",          mergeRecommendMax);
    settings.setValue("showMergeRecommendPanel",    showMergeRecommendPanel);
    settings.setValue("driftSliderMaxClusters",     driftSliderMaxClusters);
    settings.setValue("mergeRecommendMaxShift",     mergeRecommendMaxShift);
    settings.setValue("mergeRecommendErrorFloor",   mergeRecommendErrorFloor);
    settings.setValue("mergeRecommendQualityFloor", mergeRecommendQualityFloor);
    settings.setValue("curationLogging",   curationLogging);
    settings.setValue("reorderDisplayOnly",reorderDisplayOnly);
    settings.setValue("realignVerbose",    realignVerbose);
    settings.setValue("reextractSpikesOnSave", reextractSpikesOnSave);
    settings.setValue("autoRealignAfterMerge", autoRealignAfterMerge);
    settings.setValue("tsneSpikeCap", tsneSpikeCap);
    settings.setValue("tsnePerplexityStep", tsnePerplexityStep);
    settings.setValue("tsneStartPerplexity", tsneStartPerplexity);
    settings.setValue("tsneIterations", tsneIterations);
    settings.setValue("tsneTheta", tsneTheta);
    settings.setValue("tsneMaxDimensions", tsneMaxDimensions);
    settings.setValue("tsneSubsampleOverCap", tsneSubsampleOverCap);
    settings.setValue("tsneRandomSeed", tsneRandomSeed);
    settings.setValue("tsneLearningRate", tsneLearningRate);
    settings.setValue("tsneExaggeration", tsneExaggeration);
    settings.setValue("tsneExaggerationIterations", tsneExaggerationIterations);
    settings.setValue("autoRenumberAfterMerge", autoRenumberAfterMerge);
    settings.setValue("autoUpdateMatricesAfterMerge", autoUpdateMatricesAfterMerge);
    settings.setValue("errorMatrixIncremental",  errorMatrixIncremental);
    settings.setValue("errorMatrixLowPrecision", errorMatrixLowPrecision);
    settings.setValue("dipSplitMinSize",      dipSplitMinSize);
    settings.setValue("dipSplitBloatFactor",  dipSplitBloatFactor);
    settings.setValue("dipSplitValleyThresh", dipSplitValleyThresh);
    settings.setValue("projectionScopeMode",        projectionScopeMode);
    settings.setValue("projectionScopeMinutes",     projectionScopeMinutes);
    settings.setValue("projectionOutOfScopeHidden", projectionOutOfScopeHidden);
    settings.setValue("showEapCollisions",          showEapCollisions);
    settings.setValue("showEapClassMembers",        showEapClassMembers);
    settings.setValue("knnK",         knnK);
    settings.setValue("knnThreshold", knnThreshold);
    settings.setValue("knnMinNew",    knnMinNew);
    settings.setValue("knnMinRef",    knnMinRef);

    // Auto-Merge (patch 0068)
    settings.setValue("autoMergeAlgorithm",           autoMergeAlgorithm);
    settings.setValue("autoMergeMedianK",             autoMergeMedianK);
    settings.setValue("autoMergeScoreThreshold",      autoMergeScoreThreshold);
    settings.setValue("autoMergeUseErrorMatrix",      autoMergeUseErrorMatrix);
    settings.setValue("autoMergeErrorProbThreshold",  autoMergeErrorProbThreshold);
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
    settings.setValue("includeTimeInAutoSelect", includeTimeInAutoSelect);
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
    settings.setValue("autoShowTemplateLibraryOnOpen",
                      autoShowTemplateLibraryOnOpen);
    settings.setValue("templatesMode", templatesMode);
    settings.setValue("autoscaleMarginPercent", autoscaleMarginPercent);
    settings.endGroup();
    
    //write cluster view options
    settings.beginGroup("clusterView");
    settings.setValue("timeInterval",timeInterval);
    settings.endGroup();

    //write waveform view options
    settings.beginGroup("waveformView");
    settings.setValue("gain",gain);
    settings.endGroup();

    //write the input-binding overrides (input-remapping plan §5).  remove() drops
    //any previous array first, so a binding reset to default (removed from the map)
    //does not linger in the file.
    settings.beginGroup("inputBindings");
    settings.remove(QString());
    settings.beginWriteArray("overrides");
    int ib = 0;
    for(QMap<QString,QString>::const_iterator it = inputBindingOverrides.constBegin();
        it != inputBindingOverrides.constEnd(); ++it, ++ib){
        settings.setArrayIndex(ib);
        settings.setValue("id",    it.key());
        settings.setValue("chord", it.value());
    }
    settings.endArray();
    settings.endGroup();

    //write the saved keymap layouts (input-remapping plan "Keymap profiles").  remove()
    //drops any previous array first, so a deleted layout does not linger in the file.
    settings.beginGroup("inputProfiles");
    settings.remove(QString());
    settings.beginWriteArray("profiles");
    int ip = 0;
    for(QMap<QString,QString>::const_iterator it = inputProfiles.constBegin();
        it != inputProfiles.constEnd(); ++it, ++ip){
        settings.setArrayIndex(ip);
        settings.setValue("name", it.key());
        settings.setValue("text", it.value());
    }
    settings.endArray();
    settings.endGroup();
}

Configuration& configuration() {
    //The C++ standard requires that static variables in functions
    //have to be created upon first call of the function.
    static Configuration conf;
    return conf;
}
