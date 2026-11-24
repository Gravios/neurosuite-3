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
        t << pref("General", "crashRecovery", &Configuration::crashRecovery, crashRecoveryDefault);
        t << pref("General", "crashRecoveryIndex", &Configuration::crashRecoveryIndex, crashRecoveryIndexDefault);
        t << pref("General", "nbUndo", &Configuration::nbUndo, nbUndoDefault);
        t << pref("General", "backgroundColor", &Configuration::backgroundColor, backgroundColorDefault);
        t << pref("General", "reclusteringExecutable", &Configuration::reclusteringExecutable, reclusteringExecutableDefault);
        t << pref("General", "reclusteringArgs", &Configuration::reclusteringArgs, reclusteringArgsDefault);
        t << pref("General", "fiberKitReclusterCommand", &Configuration::fiberKitReclusterCommand, fiberKitReclusterCommandDefault);
        t << pref("General", "reclusterEngine", &Configuration::reclusterEngine, reclusterEngineDefault);
        t << pref("General", "realignExecutable", &Configuration::realignExecutable, realignExecutableDefault);
        t << pref("General", "realignArgs", &Configuration::realignArgs, realignArgsDefault);
        t << pref("General", "realignThreshold", &Configuration::realignThreshold, 0.10);
        t << pref("General", "realignIterations", &Configuration::realignIterations, 2);
        t << pref("General", "realignMaxShift", &Configuration::realignMaxShift, 0);
        t << pref("General", "realignKernel", &Configuration::realignKernel, 0);
        t << pref("General", "realignKernelWidth", &Configuration::realignKernelWidth, 0);
        t << pref("General", "realignMode", &Configuration::realignMode, 0);
        t << pref("General", "reorderMethod", &Configuration::reorderMethod, 0);
        t << pref("General", "waveformSortMean", &Configuration::waveformSortMean, false);
        t << prefVia("General", "mergeRecommendMax", &Configuration::mergeRecommendMax, 20, &Configuration::setMergeRecommendMax);
        t << prefVia("General", "showMergeRecommendPanel", &Configuration::showMergeRecommendPanel, true, &Configuration::setShowMergeRecommendPanel);
        t << prefVia("General", "driftSliderMaxClusters", &Configuration::driftSliderMaxClusters, 1000, &Configuration::setDriftSliderMaxClusters);
        t << prefVia("General", "mergeRecommendMaxShift", &Configuration::mergeRecommendMaxShift, 2, &Configuration::setMergeRecommendMaxShift);
        t << prefVia("General", "mergeRecommendErrorFloor", &Configuration::mergeRecommendErrorFloor, 0.05, &Configuration::setMergeRecommendErrorFloor);
        t << prefVia("General", "mergeRecommendQualityFloor", &Configuration::mergeRecommendQualityFloor, 0.90, &Configuration::setMergeRecommendQualityFloor);
        t << pref("General", "curationLogging", &Configuration::curationLogging, true);
        t << pref("General", "reorderDisplayOnly", &Configuration::reorderDisplayOnly, false);
        t << pref("General", "realignVerbose", &Configuration::realignVerbose, false);
        t << pref("General", "reextractSpikesOnSave", &Configuration::reextractSpikesOnSave, false);
        t << pref("General", "syncFeatureSpaceOnSwitch", &Configuration::syncFeatureSpaceOnSwitch, false);
        t << pref("General", "realignFromAnySpace", &Configuration::realignFromAnySpace, true);
        t << pref("General", "supersampleMode", &Configuration::supersampleMode, 0,
                  [](int m) { return (m == 2 || m == 4) ? m : 0; });
        t << pref("General", "supersampleAdopt", &Configuration::supersampleAdopt, false);
        t << pref("General", "supersampleInterp", &Configuration::supersampleInterp, 2,
                  [](int i) { return std::clamp(i, 0, 2); });
        t << pref("General", "matrixFollowSelection", &Configuration::matrixFollowSelection, true);
        t << pref("General", "autoRealignAfterMerge", &Configuration::autoRealignAfterMerge, true);
        t << pref("General", "autoRenumberAfterMerge", &Configuration::autoRenumberAfterMerge, true);
        t << pref("General", "tsneSpikeCap", &Configuration::tsneSpikeCap, 32000,
                  [](int n) { return n < 1000 ? 1000 : n; });
        t << pref("General", "tsnePerplexityStep", &Configuration::tsnePerplexityStep, 5,
                  [](int n) { return n < 1 ? 1 : n; });
        t << prefVia("General", "tsneStartPerplexity", &Configuration::tsneStartPerplexity, 30.0, &Configuration::setTsneStartPerplexity);
        t << prefVia("General", "tsneIterations", &Configuration::tsneIterations, 500, &Configuration::setTsneIterations);
        t << prefVia("General", "tsneTheta", &Configuration::tsneTheta, 0.5, &Configuration::setTsneTheta);
        t << prefVia("General", "tsneMaxDimensions", &Configuration::tsneMaxDimensions, 0, &Configuration::setTsneMaxDimensions);
        t << prefVia("General", "tsneSubsampleOverCap", &Configuration::tsneSubsampleOverCap, false, &Configuration::setTsneSubsampleOverCap);
        t << prefVia("General", "tsneRandomSeed", &Configuration::tsneRandomSeed, false, &Configuration::setTsneRandomSeed);
        t << prefVia("General", "tsneLearningRate", &Configuration::tsneLearningRate, 200.0, &Configuration::setTsneLearningRate);
        t << prefVia("General", "tsneExaggeration", &Configuration::tsneExaggeration, 12.0, &Configuration::setTsneExaggeration);
        t << prefVia("General", "tsneExaggerationIterations", &Configuration::tsneExaggerationIterations, 250, &Configuration::setTsneExaggerationIterations);
        t << pref("General", "autoUpdateMatricesAfterMerge", &Configuration::autoUpdateMatricesAfterMerge, true);
        t << pref("General", "errorMatrixIncremental", &Configuration::errorMatrixIncremental, false);
        t << pref("General", "errorMatrixLowPrecision", &Configuration::errorMatrixLowPrecision, true);
        t << pref("General", "dipSplitMinSize", &Configuration::dipSplitMinSize, 50);
        t << pref("General", "dipSplitBloatFactor", &Configuration::dipSplitBloatFactor, 0.0);
        t << pref("General", "dipSplitValleyThresh", &Configuration::dipSplitValleyThresh, 0.20);
        t << pref("General", "projectionScopeMode", &Configuration::projectionScopeMode, 0);
        t << pref("General", "projectionScopeMinutes", &Configuration::projectionScopeMinutes, 12.0);
        t << pref("General", "projectionOutOfScopeHidden", &Configuration::projectionOutOfScopeHidden, false);
        t << pref("General", "correctCollisionRows", &Configuration::correctCollisionRows, true);
        t << pref("General", "showEapCollisions", &Configuration::showEapCollisions, false);
        t << pref("General", "showEapClassMembers", &Configuration::showEapClassMembers, false);
        t << prefVia("General", "densitySigmaPx", &Configuration::densitySigmaPx, 3.0, &Configuration::setDensitySigmaPx);
        t << pref("General", "knnK", &Configuration::knnK, 10);
        t << pref("General", "knnThreshold", &Configuration::knnThreshold, 0.50);
        t << pref("General", "knnMinNew", &Configuration::knnMinNew, 5);
        t << pref("General", "knnMinRef", &Configuration::knnMinRef, 100);
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
        t << pref("General", "autoMergeAlgorithm", &Configuration::autoMergeAlgorithm, 1);
        t << pref("General", "autoMergeMedianK", &Configuration::autoMergeMedianK, 50);
        t << pref("General", "autoMergeScoreThreshold", &Configuration::autoMergeScoreThreshold, 0.98);
        t << pref("General", "autoMergeUseErrorMatrix", &Configuration::autoMergeUseErrorMatrix, false);
        t << pref("General", "autoMergeErrorProbThreshold", &Configuration::autoMergeErrorProbThreshold, 0.15);
        t << pref("General", "autoMergeMaxShift", &Configuration::autoMergeMaxShift, 0);
        t << pref("General", "autoMergeTaperSamples", &Configuration::autoMergeTaperSamples, 0);
        t << pref("General", "autoMergeMinClusterSize", &Configuration::autoMergeMinClusterSize, 25);
        t << pref("General", "autoMergeScope", &Configuration::autoMergeScope, 0);
        t << pref("General", "autoMergePreviewBeforeApply", &Configuration::autoMergePreviewBeforeApply, true);
        t << pref("General", "markerSize", &Configuration::markerSize, markerSizeDefault);
        t << pref("General", "selectionLineWidth", &Configuration::selectionLineWidth, selectionLineWidthDefault);
        t << pref("General", "templateThresholdMin", &Configuration::templateThresholdMin, templateThresholdMinDefault);
        t << pref("General", "templateThresholdMax", &Configuration::templateThresholdMax, templateThresholdMaxDefault);
        t << pref("General", "templateXcorrMetric", &Configuration::templateXcorrMetric, templateXcorrMetricDefault,
                  [](int m) { return qBound(0, m, templateXcorrMetricMax); });
        t << pref("General", "useWhiteColorDuringPrinting", &Configuration::useWhiteColorDuringPrinting, useWhiteColorDuringPrintingDefault);
        t << pref("General", "autoSelectFeatures", &Configuration::autoSelectFeatures, autoSelectFeaturesDefault);
        t << pref("General", "includeTimeInAutoSelect", &Configuration::includeTimeInAutoSelect, includeTimeInAutoSelectDefault);
        t << pref("General", "autoSelectNFeatures", &Configuration::autoSelectNFeatures, autoSelectNFeaturesDefault);
        t << pref("General", "reclusterMeanSubtractedSubdim", &Configuration::reclusterMeanSubtractedSubdim, reclusterMeanSubtractedSubdimDefault);
        t << pref("General", "reclusterChannelVariance", &Configuration::reclusterChannelVariance, reclusterChannelVarianceDefault);
        t << pref("General", "reclusterMedianWaveformResidual", &Configuration::reclusterMedianWaveformResidual, reclusterMedianWaveformResidualDefault);
        t << pref("General", "suppressReclusterOutputTab", &Configuration::suppressReclusterOutputTab, suppressReclusterOutputTabDefault);
        t << pref("General", "reclusterChunking", &Configuration::reclusterChunking, reclusterChunkingDefault,
                  [](int m) { return qBound(0, m, 3); });
        t << pref("General", "reclusterChunkMinutes", &Configuration::reclusterChunkMinutes, reclusterChunkMinutesDefault,
                  [](double m) { return m > 0.0 ? m : reclusterChunkMinutesDefault; });
        t << pref("General", "autoShowMatricesOnOpen", &Configuration::autoShowMatricesOnOpen, autoShowMatricesOnOpenDefault);
        t << pref("General", "autoShowTemplateLibraryOnOpen", &Configuration::autoShowTemplateLibraryOnOpen, autoShowTemplateLibraryOnOpenDefault);
        t << pref("General", "templatesMode", &Configuration::templatesMode, templatesModeDefault);
        t << pref("General", "autoscaleMarginPercent", &Configuration::autoscaleMarginPercent, autoscaleMarginPercentDefault);

        // ── [clusterView] ──
        t << pref("clusterView", "timeInterval", &Configuration::timeInterval, timeIntervalDefault);

        // ── [waveformView] ──
        t << pref("waveformView", "gain", &Configuration::gain, gainDefault);
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
