/***************************************************************************
 * configuration_prefs.cpp -- the table of Klusters' preferences.
 *
 * One row per preference of the settings file: group, key, the member that
 * holds it, the default, and its limit where a stored value has to be kept in
 * range.  Configuration::read() and write() are loops over the rows and the
 * default getters look their value up here, so a default exists once.
 *
 * Three kinds of row:
 *   pref       the member takes the stored value, through the limit if one is given
 *   prefVia    the member's setter takes it (the setter holds the limits)
 *   prefField  one field of a settings struct (CoreCut, HoverSpike, WaveformQuality);
 *              its default is the struct's, and read() passes the struct through its
 *              setter afterwards
 *
 * Adding a preference: RECIPES.md 2.8.  A key is permanent -- settings files hold it.
 * What is NOT in the table: the one-time changes of an old default
 * (Configuration::applyOldDefaults), the key bindings, the keymap layouts and the
 * radial menu's layout, which are not single values.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "configuration.h"

#include <algorithm>
#include <type_traits>

namespace {

template <class T>
PrefSpec pref(const char* group, const char* key, T Configuration::* member, std::type_identity_t<T> def,
              std::type_identity_t<std::function<T(T)>> limit = {})
{
    PrefSpec s;
    s.group = QLatin1String(group);
    s.key   = QLatin1String(key);
    s.def   = QVariant::fromValue(def);
    s.get   = [member](const Configuration& c) { return QVariant::fromValue(c.*member); };
    s.set   = [member, limit](Configuration& c, const QVariant& v) {
        const T value = v.value<T>();
        c.*member = limit ? limit(value) : value;
    };
    return s;
}

template <class T, class A>
PrefSpec prefVia(const char* group, const char* key, T Configuration::* member, std::type_identity_t<T> def,
                 void (Configuration::*setter)(A))
{
    PrefSpec s = pref<T>(group, key, member, def);
    s.set = [setter](Configuration& c, const QVariant& v) { (c.*setter)(v.value<T>()); };
    return s;
}

template <class S, class T>
PrefSpec prefField(const char* group, const char* key, S Configuration::* owner, T S::* field)
{
    PrefSpec s;
    s.group = QLatin1String(group);
    s.key   = QLatin1String(key);
    s.def   = QVariant::fromValue(S().*field);
    s.get   = [owner, field](const Configuration& c) { return QVariant::fromValue((c.*owner).*field); };
    s.set   = [owner, field](Configuration& c, const QVariant& v) { (c.*owner).*field = v.value<T>(); };
    return s;
}

} // namespace

const QList<PrefSpec>& Configuration::prefTable()
{
    static const QList<PrefSpec> table = [] {
        QList<PrefSpec> t;

        // ── [General] ──
        t << pref("General", "crashRecovery", &Configuration::crashRecovery, crashRecoveryDefault).on("crashRecoveryCheckBox");
        t << pref("General", "crashRecoveryIndex", &Configuration::crashRecoveryIndex, crashRecoveryIndexDefault,
                  [](int i) { return qBound(0, i, 5); }).on("crashRecoveryComboBox");   // the range its page allows
        t << pref("General", "nbUndo", &Configuration::nbUndo, nbUndoDefault,
                  [](int n) { return qBound(0, n, 30); }).on("undoSpinBox");   // the range its page allows
        t << prefVia("General", "realignUndoMemoryMB", &Configuration::realignUndoMemoryMB, 4096,
                     &Configuration::setRealignUndoMemoryMB).on("realignUndoMemorySpinBox");   // 0 .. 262144, in the setter
        t << pref("General", "backgroundColor", &Configuration::backgroundColor, backgroundColorDefault).on("backgroundColorButton");
        t << pref("General", "reclusteringExecutable", &Configuration::reclusteringExecutable, reclusteringExecutableDefault).on("reclusteringExecutableLineEdit");
        t << pref("General", "reclusteringArgs", &Configuration::reclusteringArgs, reclusteringArgsDefault);
        t << pref("General", "fiberKitReclusterCommand", &Configuration::fiberKitReclusterCommand, fiberKitReclusterCommandDefault);
        t << pref("General", "reclusterEngine", &Configuration::reclusterEngine, reclusterEngineDefault);
        t << pref("General", "realignExecutable", &Configuration::realignExecutable, realignExecutableDefault);
        t << pref("General", "realignArgs", &Configuration::realignArgs, realignArgsDefault);
        t << prefVia("General", "realignThreshold", &Configuration::realignThreshold, 0.10, &Configuration::setRealignThreshold).on("realignThresholdSpinBox");
        t << prefVia("General", "realignIterations", &Configuration::realignIterations, 2, &Configuration::setRealignIterations).on("realignIterationsSpinBox");
        t << prefVia("General", "realignMaxShift", &Configuration::realignMaxShift, 0, &Configuration::setRealignMaxShift).on("realignMaxShiftSpinBox");
        t << prefVia("General", "realignKernel", &Configuration::realignKernel, 0, &Configuration::setRealignKernel).on("realignKernelCombo");
        t << prefVia("General", "realignKernelWidth", &Configuration::realignKernelWidth, 0, &Configuration::setRealignKernelWidth).on("realignKernelWidthSpinBox");
        t << prefVia("General", "realignMode", &Configuration::realignMode, 0, &Configuration::setRealignMode).on("realignModeOffRadio|realignModePcaRadio|realignModeRmsRadio|realignModeCentroidRadio");
        t << prefVia("General", "reorderMethod", &Configuration::reorderMethod, 0, &Configuration::setReorderMethod).on("reorderMethodComboBox");
        t << pref("General", "waveformSortMean", &Configuration::waveformSortMean, false).on("waveformSortStatisticComboBox");
        t << prefVia("General", "mergeRecommendMax", &Configuration::mergeRecommendMax, 20, &Configuration::setMergeRecommendMax).on("mergeRecommendMaxSpinBox");
        t << prefVia("General", "showMergeRecommendPanel", &Configuration::showMergeRecommendPanel, true, &Configuration::setShowMergeRecommendPanel).on("showMergeRecommendPanelCheckBox");
        t << prefVia("General", "driftSliderMaxClusters", &Configuration::driftSliderMaxClusters, 1000, &Configuration::setDriftSliderMaxClusters).on("driftSliderMaxClustersSpinBox");
        t << prefVia("General", "mergeRecommendMaxShift", &Configuration::mergeRecommendMaxShift, 2, &Configuration::setMergeRecommendMaxShift);
        t << prefVia("General", "mergeRecommendErrorFloor", &Configuration::mergeRecommendErrorFloor, 0.05, &Configuration::setMergeRecommendErrorFloor).on("mergeRecommendErrorFloorSpinBox");
        t << prefVia("General", "mergeRecommendQualityFloor", &Configuration::mergeRecommendQualityFloor, 0.90, &Configuration::setMergeRecommendQualityFloor).on("mergeRecommendQualityFloorSpinBox");
        t << pref("General", "curationLogging", &Configuration::curationLogging, true).on("curationLoggingCheckBox");
        t << pref("General", "reorderDisplayOnly", &Configuration::reorderDisplayOnly, false).on("reorderDisplayOnlyCheckBox");
        t << pref("General", "realignVerbose", &Configuration::realignVerbose, false).on("realignVerboseCheckBox");
        t << pref("General", "reextractSpikesOnSave", &Configuration::reextractSpikesOnSave, false).on("reextractSpikesOnSaveCheckBox");
        t << pref("General", "syncFeatureSpaceOnSwitch", &Configuration::syncFeatureSpaceOnSwitch, false).on("syncFeatureSpaceOnSwitchCheckBox");
        t << pref("General", "realignFromAnySpace", &Configuration::realignFromAnySpace, true).on("realignFromAnySpaceCheckBox");
        t << prefVia("General", "supersampleMode", &Configuration::supersampleMode, 0, &Configuration::setSupersampleMode).on("supersampleModeCombo", { 0, 2, 4 });
        t << pref("General", "supersampleAdopt", &Configuration::supersampleAdopt, false).on("supersampleAdoptCheckBox");
        t << prefVia("General", "supersampleInterp", &Configuration::supersampleInterp, 2, &Configuration::setSupersampleInterp).on("supersampleInterpCombo");
        t << pref("General", "matrixFollowSelection", &Configuration::matrixFollowSelection, true);
        t << pref("General", "autoRealignAfterMerge", &Configuration::autoRealignAfterMerge, true).on("autoRealignAfterMergeCheckBox");
        t << pref("General", "autoRenumberAfterMerge", &Configuration::autoRenumberAfterMerge, true).on("autoRenumberAfterMergeCheckBox");
        t << prefVia("General", "tsneSpikeCap", &Configuration::tsneSpikeCap, 32000, &Configuration::setTsneSpikeCap).on("tsneCapLineEdit");
        t << prefVia("General", "tsnePerplexityStep", &Configuration::tsnePerplexityStep, 5, &Configuration::setTsnePerplexityStep).on("tsneStepLineEdit");
        t << prefVia("General", "tsneStartPerplexity", &Configuration::tsneStartPerplexity, 30.0, &Configuration::setTsneStartPerplexity).on("tsneStartPerpLineEdit");
        t << prefVia("General", "tsneIterations", &Configuration::tsneIterations, 500, &Configuration::setTsneIterations).on("tsneItersLineEdit");
        t << prefVia("General", "tsneTheta", &Configuration::tsneTheta, 0.5, &Configuration::setTsneTheta).on("tsneThetaLineEdit");
        t << prefVia("General", "tsneMaxDimensions", &Configuration::tsneMaxDimensions, 0, &Configuration::setTsneMaxDimensions).on("tsneDimsLineEdit");
        t << prefVia("General", "tsneSubsampleOverCap", &Configuration::tsneSubsampleOverCap, false, &Configuration::setTsneSubsampleOverCap).on("tsneSubsampleCheckBox");
        t << prefVia("General", "tsneRandomSeed", &Configuration::tsneRandomSeed, false, &Configuration::setTsneRandomSeed).on("tsneRandomSeedCheckBox");
        t << prefVia("General", "tsneLearningRate", &Configuration::tsneLearningRate, 200.0, &Configuration::setTsneLearningRate).on("tsneEtaLineEdit");
        t << prefVia("General", "tsneExaggeration", &Configuration::tsneExaggeration, 12.0, &Configuration::setTsneExaggeration).on("tsneExagLineEdit");
        t << prefVia("General", "tsneExaggerationIterations", &Configuration::tsneExaggerationIterations, 250, &Configuration::setTsneExaggerationIterations).on("tsneExagItersLineEdit");
        t << pref("General", "autoUpdateMatricesAfterMerge", &Configuration::autoUpdateMatricesAfterMerge, true).on("autoUpdateMatricesAfterMergeCheckBox");
        t << pref("General", "errorMatrixIncremental", &Configuration::errorMatrixIncremental, false).on("errorMatrixIncrementalCheckBox");
        t << pref("General", "errorMatrixLowPrecision", &Configuration::errorMatrixLowPrecision, true).on("errorMatrixLowPrecisionCheckBox");
        t << prefVia("General", "dipSplitMinSize", &Configuration::dipSplitMinSize, 50, &Configuration::setDipSplitMinSize).on("dipSplitMinSizeSpinBox");
        t << prefVia("General", "dipSplitBloatFactor", &Configuration::dipSplitBloatFactor, 0.0, &Configuration::setDipSplitBloatFactor).on("dipSplitBloatFactorSpinBox");
        t << prefVia("General", "dipSplitValleyThresh", &Configuration::dipSplitValleyThresh, 0.20, &Configuration::setDipSplitValleyThresh).on("dipSplitValleyThreshSpinBox");
        t << prefVia("General", "projectionScopeMode", &Configuration::projectionScopeMode, 0, &Configuration::setProjectionScopeMode).on("projModeCombo");
        t << prefVia("General", "projectionScopeMinutes", &Configuration::projectionScopeMinutes, 12.0, &Configuration::setProjectionScopeMinutes).on("projScopeMinutesLineEdit");
        t << pref("General", "projectionOutOfScopeHidden", &Configuration::projectionOutOfScopeHidden, false).on("projHideOutOfScopeCheckBox");
        t << pref("General", "correctCollisionRows", &Configuration::correctCollisionRows, true).on("correctCollisionRowsCheckBox");
        t << pref("General", "showEapCollisions", &Configuration::showEapCollisions, false).on("showCollisionsCheckBox");
        t << pref("General", "showEapClassMembers", &Configuration::showEapClassMembers, false).on("showClassMembersCheckBox");
        t << prefVia("General", "densitySigmaPx", &Configuration::densitySigmaPx, 3.0, &Configuration::setDensitySigmaPx).on("densitySigmaSpinBox");
        t << prefVia("General", "knnK", &Configuration::knnK, 10, &Configuration::setKnnK).on("knnKSpinBox");
        t << prefVia("General", "knnThreshold", &Configuration::knnThreshold, 0.50, &Configuration::setKnnThreshold).on("knnThresholdSpinBox");
        t << prefVia("General", "knnMinNew", &Configuration::knnMinNew, 5, &Configuration::setKnnMinNew).on("knnMinNewSpinBox");
        t << prefVia("General", "knnMinRef", &Configuration::knnMinRef, 100, &Configuration::setKnnMinRef).on("knnMinRefSpinBox");
        t << prefField("General", "coreCutOnsetFactor", &Configuration::coreCut, &CoreCut::onsetFactor);
        t << prefField("General", "coreCutRiseStartMs", &Configuration::coreCut, &CoreCut::riseStartMs);
        t << prefField("General", "coreCutFlatStartMs", &Configuration::coreCut, &CoreCut::flatStartMs);
        t << prefField("General", "coreCutFlatEndMs", &Configuration::coreCut, &CoreCut::flatEndMs);
        t << prefField("General", "coreCutFallEndMs", &Configuration::coreCut, &CoreCut::fallEndMs);
        t << prefField("General", "coreCutMaxShiftMs", &Configuration::coreCut, &CoreCut::maxShiftMs);
        t << prefField("General", "coreCutMaxOrdered", &Configuration::coreCut, &CoreCut::maxOrdered);
        t << prefField("General", "coreCutMinSpikes", &Configuration::coreCut, &CoreCut::minSpikes);
        t << prefField("General", "coreCutUseGpu", &Configuration::coreCut, &CoreCut::useGpu);
        t << prefField("General", "hoverSpikeEnabled", &Configuration::hoverSpike, &HoverSpike::enabled);
        t << prefField("General", "hoverSpikeRingMarginPx", &Configuration::hoverSpike, &HoverSpike::ringMarginPx);
        t << prefField("General", "hoverSpikeRingWidthPx", &Configuration::hoverSpike, &HoverSpike::ringWidthPx);
        t << prefField("General", "hoverSpikeTraceWidthPx", &Configuration::hoverSpike, &HoverSpike::traceWidthPx);
        t << prefField("General", "waveformQualityEnabled", &Configuration::waveformQuality, &WaveformQuality::enabled);
        t << prefField("General", "waveformQualityYellow", &Configuration::waveformQuality, &WaveformQuality::yellow);
        t << prefField("General", "waveformQualityOrange", &Configuration::waveformQuality, &WaveformQuality::orange);
        t << prefField("General", "waveformQualityRed", &Configuration::waveformQuality, &WaveformQuality::red);
        t << prefField("General", "waveformQualityDiameterPx", &Configuration::waveformQuality, &WaveformQuality::diameterPx);
        t << prefVia("General", "autoMergeAlgorithm", &Configuration::autoMergeAlgorithm, 1, &Configuration::setAutoMergeAlgorithm);
        t << prefVia("General", "autoMergeMedianK", &Configuration::autoMergeMedianK, 50, &Configuration::setAutoMergeMedianK);
        t << prefVia("General", "autoMergeScoreThreshold", &Configuration::autoMergeScoreThreshold, 0.98, &Configuration::setAutoMergeScoreThreshold);
        t << pref("General", "autoMergeUseErrorMatrix", &Configuration::autoMergeUseErrorMatrix, false);
        t << prefVia("General", "autoMergeErrorProbThreshold", &Configuration::autoMergeErrorProbThreshold, 0.15, &Configuration::setAutoMergeErrorProbThreshold);
        t << prefVia("General", "autoMergeMaxShift", &Configuration::autoMergeMaxShift, 0, &Configuration::setAutoMergeMaxShift);
        t << prefVia("General", "autoMergeTaperSamples", &Configuration::autoMergeTaperSamples, 0, &Configuration::setAutoMergeTaperSamples);
        t << prefVia("General", "autoMergeMinClusterSize", &Configuration::autoMergeMinClusterSize, 25, &Configuration::setAutoMergeMinClusterSize);
        t << prefVia("General", "autoMergeScope", &Configuration::autoMergeScope, 0, &Configuration::setAutoMergeScope);
        t << pref("General", "autoMergePreviewBeforeApply", &Configuration::autoMergePreviewBeforeApply, true);
        t << prefVia("General", "markerSize", &Configuration::markerSize, markerSizeDefault, &Configuration::setMarkerSize).on("markerSizeSpinBox");
        t << prefVia("General", "selectionLineWidth", &Configuration::selectionLineWidth, selectionLineWidthDefault, &Configuration::setSelectionLineWidth).on("selectionLineWidthSpinBox");
        t << prefVia("General", "templateThresholdMin", &Configuration::templateThresholdMin, templateThresholdMinDefault, &Configuration::setTemplateThresholdMin).on("templateThresholdMinSpinBox");
        t << prefVia("General", "templateThresholdMax", &Configuration::templateThresholdMax, templateThresholdMaxDefault, &Configuration::setTemplateThresholdMax).on("templateThresholdMaxSpinBox");
        t << prefVia("General", "templateXcorrMetric", &Configuration::templateXcorrMetric, templateXcorrMetricDefault, &Configuration::setTemplateXcorrMetric).on("templateXcorrMetricComboBox");
        t << pref("General", "useWhiteColorDuringPrinting", &Configuration::useWhiteColorDuringPrinting, useWhiteColorDuringPrintingDefault).on("useWhiteColorPrinting");
        t << pref("General", "autoSelectFeatures", &Configuration::autoSelectFeatures, autoSelectFeaturesDefault).on("autoSelectFeaturesCheckBox");
        t << pref("General", "includeTimeInAutoSelect", &Configuration::includeTimeInAutoSelect, includeTimeInAutoSelectDefault);
        t << prefVia("General", "autoSelectNFeatures", &Configuration::autoSelectNFeatures, autoSelectNFeaturesDefault, &Configuration::setAutoSelectNFeatures).on("autoSelectNFeaturesSpinBox");
        t << pref("General", "reclusterMeanSubtractedSubdim", &Configuration::reclusterMeanSubtractedSubdim, reclusterMeanSubtractedSubdimDefault).on("reclusterMeanSubtractedSubdimCheckBox");
        t << pref("General", "reclusterChannelVariance", &Configuration::reclusterChannelVariance, reclusterChannelVarianceDefault).on("reclusterChannelVarianceCheckBox");
        t << pref("General", "reclusterMedianWaveformResidual", &Configuration::reclusterMedianWaveformResidual, reclusterMedianWaveformResidualDefault).on("reclusterMedianWaveformResidualCheckBox");
        t << pref("General", "suppressReclusterOutputTab", &Configuration::suppressReclusterOutputTab, suppressReclusterOutputTabDefault).on("suppressReclusterOutputTabCheckBox");
        t << pref("General", "reclusterChunking", &Configuration::reclusterChunking, reclusterChunkingDefault,
                  [](int m) { return qBound(0, m, 3); });
        t << pref("General", "reclusterChunkMinutes", &Configuration::reclusterChunkMinutes, reclusterChunkMinutesDefault,
                  [](double m) { return m > 0.0 ? m : reclusterChunkMinutesDefault; });
        t << pref("General", "autoShowMatricesOnOpen", &Configuration::autoShowMatricesOnOpen, autoShowMatricesOnOpenDefault).on("autoShowMatricesOnOpenCheckBox");
        t << pref("General", "autoShowTemplateLibraryOnOpen", &Configuration::autoShowTemplateLibraryOnOpen, autoShowTemplateLibraryOnOpenDefault).on("autoShowTemplateLibraryOnOpenCheckBox");
        t << pref("General", "templatesMode", &Configuration::templatesMode, templatesModeDefault).on("templatesModeCheckBox");
        t << prefVia("General", "autoscaleMarginPercent", &Configuration::autoscaleMarginPercent, autoscaleMarginPercentDefault, &Configuration::setAutoscaleMarginPercent).on("autoscaleMarginSpinBox");

        // ── [clusterView] ──
        t << pref("clusterView", "timeInterval", &Configuration::timeInterval, timeIntervalDefault,
                  [](int s) { return qBound(60, s, 6000); }).on("intervalSpinBox");   // the range its page allows

        // ── [waveformView] ──
        t << pref("waveformView", "gain", &Configuration::gain, gainDefault,
                  [](int g) { return qBound(200, g, 6000); });   // the range its page allows
        return t;
    }();
    return table;
}

QVariant Configuration::prefDefault(const char* key)
{
    const QLatin1String k(key);
    for (const PrefSpec& s : prefTable())
        if (s.key == k) return s.def;
    Q_ASSERT_X(false, "Configuration::prefDefault", key);
    return {};
}
