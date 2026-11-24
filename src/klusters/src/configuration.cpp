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

#include <algorithm>
#include <cmath>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

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
// Suppressed by default: a recluster opens no output tab unless it fails.
const bool Configuration::suppressReclusterOutputTabDefault = true;
// Unchanged behaviour by default: no chunking flag, so KiloKlustaKwik uses its own 7 minutes.
const int Configuration::reclusterChunkingDefault = Configuration::ReclusterChunkSorterDefault;
const double Configuration::reclusterChunkMinutesDefault = 7.0;
// patch79 — opt-in: off by default
const bool Configuration::autoShowMatricesOnOpenDefault = true;
// Template Library display: open by default (on)
const bool Configuration::autoShowTemplateLibraryOnOpenDefault = true;
const bool Configuration::templatesModeDefault = true;
const double Configuration::autoscaleMarginPercentDefault = 5.0;
const int  Configuration::crashRecoveryIndexDefault = 0;
const int  Configuration::gainDefault = 200;
const int  Configuration::timeIntervalDefault = 60;
const int  Configuration::nbUndoDefault = 20;
const int  Configuration::nbUndoOldDefault = 2;   // until 2026-11-24
const QColor Configuration::backgroundColorDefault = QColor(Qt::black);
// KiloKlustaKwik's CPU build, which every neurosuite-3 build installs (the GPU
// builds add KiloKlustaKwik); it reads the temporary <base>.fet.standard.N that
// slotRecluster writes for it.
const QString Configuration::reclusteringExecutableDefault = QLatin1String("KiloKlustaKwik_cpu");
const QString Configuration::reclusteringArgsDefault =
        "%fileBaseName %electrodeGroupID -MinClusters 2 -MaxClusters 12 -UseFeatures %features";
// Recluster with fiber-kit: the command run on the recluster SUB-SESSION (the selected clusters'
// spikes as a session of their own, see KlustersDoc::createReclusterSubsession).  %fileBaseName is the
// sub-session's base name, %electrodeGroupID the group; `fk run` runs the plan of the session folder's
// fiber-kit.yaml and --final-clu names the .clu Klusters reads back (the clu of the .fet it wrote).
const QString Configuration::fiberKitReclusterCommandDefault =
        "fk run %electrodeGroupID --session %fileBaseName --final-clu %fileBaseName.clu.klusters.%electrodeGroupID";
// The active recluster ENGINE -- what Shift+R (Recluster) runs: "klustakwik" (the built-in
// KlustaKwik path), "fiber-kit" (the fiber-kit plan on the selection's sub-session), or
// "plugin:<name>" (another installed `recluster` descriptor).  Chosen in Recluster >
// Engine, or on the radial menu's Recluster > Plugin ring.
const QString Configuration::reclusterEngineDefault = QLatin1String("klustakwik");
const QString Configuration::realignExecutableDefault = QLatin1String("");
const QString Configuration::realignArgsDefault = QLatin1String("--threshold 0.10 --iterations 2");
const int  Configuration::markerSizeDefault = 2;
const int  Configuration::selectionLineWidthDefault = 1;
const bool Configuration::useWhiteColorDuringPrintingDefault = true;
const double Configuration::templateThresholdMinDefault = 0.5;
const double Configuration::templateThresholdMaxDefault = 1.0;
const int  Configuration::templateXcorrMetricDefault = 0;   // cosine

Configuration::Configuration():nbChannels(0) {
    read(); // read the settings or set them to the default values
}

// The one-time changes of an old default.  Every write() stores every value, so a file
// written before a default changed holds the OLD default as if it had been chosen.  Each
// change below is applied once and recorded by a flag that write() stores, so the same
// value set by hand afterwards is kept.  @p settings is in the General group.
void Configuration::applyOldDefaults(QSettings& settings) {
    // The undo depth was 2 until 2026-11-24.
    if (!settings.value("nbUndoRaisedFromOldDefault", false).toBool() && settings.contains("nbUndo")
        && nbUndo == nbUndoOldDefault)
        nbUndo = nbUndoDefault;
    // Earlier versions stored "KlustaKwik" as the recluster program.  neurosuite-3 never
    // installs a program of that name, so where none is on PATH it becomes today's
    // default; a KlustaKwik that is installed, or any other program, is kept.
    if (reclusteringExecutable == QLatin1String("KlustaKwik")
        && QStandardPaths::findExecutable(reclusteringExecutable).isEmpty())
        reclusteringExecutable = reclusteringExecutableDefault;
    // The realign threshold was 0.70 until 2026-11-24.  At 0.70 the spikes that score
    // lower stay where they are while the rest of their cluster moves.
    if (!settings.value("realignThresholdLoweredFromOldDefault", false).toBool()
        && settings.contains("realignThreshold") && qAbs(realignThreshold - 0.70) < 1e-6)
        realignThreshold = getRealignThresholdDefault();
    // Before 3.x the template metric was a flag, Pearson on / off.
    if (!settings.contains("templateXcorrMetric"))
        templateXcorrMetric = settings.value("templateXcorrPearson", false).toBool() ? 1 : 0;
    // Settings revision 1 (2026-10-29): the matrices are shown on open by default (was
    // off).  A file below revision 1 takes the new default once; write() records the
    // revision, so a later choice of "off" is kept.
    if (settings.value("settingsRevision", 0).toInt() < 1)
        autoShowMatricesOnOpen = autoShowMatricesOnOpenDefault;
}

void Configuration::read() {
    QSettings settings;

    // Every preference of the table (configuration_prefs.cpp): the stored value, or the
    // default where the key is absent, through the preference's limits.
    for (const char* group : { "General", "clusterView", "waveformView" }) {
        settings.beginGroup(QLatin1String(group));
        for (const PrefSpec& spec : prefTable())
            if (spec.group == QLatin1String(group)) spec.set(*this, settings.value(spec.key, spec.def));
        if (QLatin1String(group) == QLatin1String("General")) applyOldDefaults(settings);
        settings.endGroup();
    }
    // The settings structs were filled field by field: their setters bring a struct as a
    // whole within its limits (an edge out of order, a width out of range).
    setCoreCut(CoreCut(coreCut));
    setHoverSpike(HoverSpike(hoverSpike));
    setWaveformQuality(WaveformQuality(waveformQuality));

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
    // Until 2026-11-24 the child-view key command "Dissolve parent into children" had the
    // id of the Hierarchy menu's command, hierarchy.dissolveParent, and replaced it in the
    // registry: a stored binding of that id was made for the key command.  Once, it moves
    // to the key command's own id.
    const bool dissolveKeySplit = settings.value("dissolveParentKeySplit", false).toBool();
    if(!dissolveKeySplit && inputBindingOverrides.contains(QStringLiteral("hierarchy.dissolveParent")))
        inputBindingOverrides.insert(QStringLiteral("hierarchy.dissolveParentKey"),
                                     inputBindingOverrides.take(QStringLiteral("hierarchy.dissolveParent")));
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
            inputProfiles.insert(name, dissolveKeySplit ? text : QString(text).replace(
                QRegularExpression(QStringLiteral("\\bhierarchy\\.dissolveParent\\b")),
                QStringLiteral("hierarchy.dissolveParentKey")));
    }
    settings.endArray();
    settings.endGroup();

    //read the radial context menu layout (radiallayout.h JSON; empty = default).
    settings.beginGroup("radialMenu");
    radialMenuLayout = settings.value("layout", QString()).toString();
    settings.endGroup();
}

void Configuration::write() const {
    QSettings settings;

    // Every preference of the table, and the flags of the one-time changes (applyOldDefaults).
    for (const char* group : { "General", "clusterView", "waveformView" }) {
        settings.beginGroup(QLatin1String(group));
        for (const PrefSpec& spec : prefTable())
            if (spec.group == QLatin1String(group)) settings.setValue(spec.key, spec.get(*this));
        if (QLatin1String(group) == QLatin1String("General")) {
        settings.setValue("nbUndoRaisedFromOldDefault", true);
        settings.setValue("realignThresholdLoweredFromOldDefault", true);
        settings.setValue("settingsRevision", settingsRevision);
        }
        settings.endGroup();
    }

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
    settings.setValue("dissolveParentKeySplit", true);
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

    //write the radial context menu layout; an empty one (the default) removes the key
    //so a later change to the shipped default reaches users who never edited theirs.
    settings.beginGroup("radialMenu");
    if(radialMenuLayout.isEmpty()) settings.remove("layout");
    else settings.setValue("layout", radialMenuLayout);
    settings.endGroup();
}

Configuration& configuration() {
    //The C++ standard requires that static variables in functions
    //have to be created upon first call of the function.
    static Configuration conf;
    return conf;
}

void Configuration::setCoreCut(const CoreCut& c)
{
    const CoreCut d;
    coreCut = c;
    coreCut.onsetFactor = std::isfinite(c.onsetFactor) ? qBound(1.1, c.onsetFactor, 1000.0) : d.onsetFactor;
    coreCut.maxShiftMs  = std::isfinite(c.maxShiftMs) ? qBound(0.0, c.maxShiftMs, 2.0) : d.maxShiftMs;
    coreCut.maxOrdered  = qBound(1000, c.maxOrdered, 200000);
    coreCut.minSpikes   = qBound(50, c.minSpikes, coreCut.maxOrdered);
    // rise < flat start <= flat end < fall, the crossing (0) inside [rise, fall]
    const bool ordered = std::isfinite(c.riseStartMs) && std::isfinite(c.flatStartMs) && std::isfinite(c.flatEndMs) && std::isfinite(c.fallEndMs)
                         && c.riseStartMs < c.flatStartMs && c.flatStartMs <= c.flatEndMs && c.flatEndMs < c.fallEndMs
                         && c.riseStartMs >= -5.0 && c.fallEndMs <= 5.0;
    if (!ordered) {
        coreCut.riseStartMs = d.riseStartMs; coreCut.flatStartMs = d.flatStartMs;
        coreCut.flatEndMs   = d.flatEndMs;   coreCut.fallEndMs   = d.fallEndMs;
    }
}

void Configuration::setWaveformQuality(const WaveformQuality& q)
{
    const WaveformQuality d;
    const auto level = [](double v, double fallback) { return std::isfinite(v) ? qBound(-1.0, v, 10.0) : fallback; };
    waveformQuality.enabled    = q.enabled;
    waveformQuality.yellow     = level(q.yellow, d.yellow);
    waveformQuality.orange     = std::max(waveformQuality.yellow, level(q.orange, d.orange));
    waveformQuality.red        = std::max(waveformQuality.orange, level(q.red, d.red));
    waveformQuality.diameterPx = qBound(4, q.diameterPx, 48);
}

void Configuration::setHoverSpike(const HoverSpike& h)
{
    const HoverSpike d;
    hoverSpike.enabled      = h.enabled;
    hoverSpike.ringMarginPx = qBound(0, h.ringMarginPx, 64);
    hoverSpike.ringWidthPx  = std::isfinite(h.ringWidthPx)  ? qBound(0.5, h.ringWidthPx, 12.0)  : d.ringWidthPx;
    hoverSpike.traceWidthPx = std::isfinite(h.traceWidthPx) ? qBound(0.5, h.traceWidthPx, 12.0) : d.traceWidthPx;
}
