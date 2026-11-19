/* klusters_selftest.cpp -- a headless driver for the document machinery.
 *
 * NS3_KLUSTERS_SELFTEST=<spec.json> (main.cpp) opens the spec's .clu and runs its
 * steps one at a time, each once the app is idle (no realign, nudge, save, plugin
 * or pending return to a space), writes a JSON report and quits: 0 when every
 * step succeeded, 1 otherwise, 2 when the spec or the open failed.  With
 * QT_QPA_PLATFORM=offscreen this needs no display, so test/selftest/*.py can
 * exercise realign, nudge, the follow of other spaces, super-sampling and Save on
 * a synthetic session and check the files afterwards -- the one way the
 * space-aware realign is tested without a person at the GUI.
 *
 * Spec:  { "clu": "<path>", "report": "<path>", "steps": [ ... ] }
 * Steps: {"show": "<method>"}            showFeatureSpace
 *        {"realign": <cluster>}          startRealignForCluster (the saved settings)
 *        {"nudge": [<cluster>, <delta>]} nudgeClusterTimestamps + the follow
 *        {"supersample": <factor>}       buildSupersampledSpace (no Save As offer)
 *        {"follow": true}                followMovedSpikes now
 *        {"save": true}                  File > Save
 *        {"saveas": "<path.clu>"}        File > Save As (a Save As under a token whose
 *                                        space exists adopts it and reopens there)
 *        {"dump": true, "cluster": <id>} the spaces, their files, the loaded
 *                                        times (and phases) of the cluster's spikes
 * A modal dialog that opens during a step is closed (its default button) and
 * recorded in the step's result, so the run never hangs on one.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "neurosuite/core/drift_partition.hpp"   // uniformPartition (residualScan)
#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "clusterview.h"
#include "clusterPalette.h"
#include "data.h"
#include "savethread.h"
#include "serialjobqueue.h"
#include "configuration.h"

#include <cmath>
#include <vector>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <neurosuite/core/custody.hpp>

namespace {
constexpr int kTickMs       = 50;
constexpr int kIdleTicks    = 4;        // idle this long before a step is taken as done
constexpr int kStepTimeout  = 600 * 1000 / kTickMs;   // 10 minutes per step
}

void KlustersApp::startSelfTest(const QString& specPath)
{
    QFile f(specPath);
    QJsonObject spec;
    if (f.open(QIODevice::ReadOnly)) spec = QJsonDocument::fromJson(f.readAll()).object();
    selfTestReport_ = spec.value(QStringLiteral("report")).toString();
    selfTestSteps_  = spec.value(QStringLiteral("steps")).toArray();
    const QString clu = spec.value(QStringLiteral("clu")).toString();
    if (clu.isEmpty() || selfTestReport_.isEmpty()) {
        qWarning().noquote() << "[selftest] bad spec" << specPath;
        selfTestFinish(2);
        return;
    }
    // The preference the steps assume unless the spec says otherwise.
    if (spec.contains(QStringLiteral("realignFromAnySpace")))
        configuration().setRealignFromAnySpace(spec.value(QStringLiteral("realignFromAnySpace")).toBool());
    // The realign preference's structured fields: the args string a realign runs with is
    // buildRealignArgs() of these (Preferences > Realignment), rebuilt here once they are set.
    if (spec.contains(QStringLiteral("realignKernel")))           // 0 none, 1 gauss, 2 hann, 3 template energy
        configuration().setRealignKernel(spec.value(QStringLiteral("realignKernel")).toInt());
    if (spec.contains(QStringLiteral("realignKernelWidth")))      // samples each side of the peak, 0 = auto
        configuration().setRealignKernelWidth(spec.value(QStringLiteral("realignKernelWidth")).toInt());
    if (spec.contains(QStringLiteral("realignMode")))             // 0 xcorr only, 1 PCA refine, 2 RMS recenter, 3 centroid
        configuration().setRealignMode(spec.value(QStringLiteral("realignMode")).toInt());
    if (spec.contains(QStringLiteral("realignThreshold")))
        configuration().setRealignThreshold(spec.value(QStringLiteral("realignThreshold")).toDouble());
    realignArgs = buildRealignArgs();
    // The three automatic steps after a cluster edit (Preferences > Clustering).
    if (spec.contains(QStringLiteral("autoRealign")))
        configuration().setAutoRealignAfterMerge(spec.value(QStringLiteral("autoRealign")).toBool());
    if (spec.contains(QStringLiteral("autoRenumber")))
        configuration().setAutoRenumberAfterMerge(spec.value(QStringLiteral("autoRenumber")).toBool());
    if (spec.contains(QStringLiteral("autoMatrices")))
        configuration().setAutoUpdateMatricesAfterMerge(spec.value(QStringLiteral("autoMatrices")).toBool());
    if (spec.contains(QStringLiteral("realignVerbose")))
        configuration().setRealignVerbose(spec.value(QStringLiteral("realignVerbose")).toBool());
    if (spec.contains(QStringLiteral("supersampleMode")))
        configuration().setSupersampleMode(spec.value(QStringLiteral("supersampleMode")).toInt());
    if (spec.contains(QStringLiteral("supersampleInterp"))) {      // 0 cubic spline, 1 Lanczos-3, 2 smoothing spline
        configuration().setSupersampleInterp(spec.value(QStringLiteral("supersampleInterp")).toInt());
        if (doc) doc->setSupersampleInterp(interpFromPref(configuration().getSupersampleInterp()));
    }
    openDocumentFile(QFileInfo(clu).absoluteFilePath());
    if (!doc || doc->url().isEmpty()) {
        qWarning().noquote() << "[selftest] could not open" << clu;
        selfTestFinish(2);
        return;
    }
    selfTestIndex_ = 0;
    selfTestTimer_ = new QTimer(this);
    selfTestTimer_->setInterval(kTickMs);
    connect(selfTestTimer_, &QTimer::timeout, this, &KlustersApp::selfTestTick);
    selfTestTimer_->start();
}

void KlustersApp::selfTestTick()
{
    // A modal that a step opened would wait for a person: take its default.
    if (QWidget* w = QApplication::activeModalWidget()) {
        if (auto* box = qobject_cast<QMessageBox*>(w)) {
            QJsonObject note;
            note.insert(QStringLiteral("dialog"), box->windowTitle() + QStringLiteral(": ") + box->text());
            selfTestResults_.append(note);
            if (QPushButton* b = box->defaultButton()) b->click(); else box->reject();
        } else {
            w->close();
        }
        return;
    }
    const bool busy = fileJobRunning() || isInit
                   || (nudgeFollowTimer && nudgeFollowTimer->isActive());
    if (busy) { selfTestIdleTicks_ = 0; }
    else      { ++selfTestIdleTicks_; }
    if (++selfTestWaitTicks_ > kStepTimeout) {
        QJsonObject r;
        r.insert(QStringLiteral("error"), QStringLiteral("timeout waiting for the app to be idle"));
        selfTestResults_.append(r);
        selfTestFinish(1);
        return;
    }
    if (selfTestIdleTicks_ < kIdleTicks) return;

    if (selfTestIndex_ >= selfTestSteps_.size()) { selfTestFinish(0); return; }
    const QJsonObject step = selfTestSteps_.at(selfTestIndex_).toObject();
    QJsonObject result;
    result.insert(QStringLiteral("step"), step);
    const bool ok = selfTestRunStep(step, result);
    result.insert(QStringLiteral("ok"), ok);
    selfTestResults_.append(result);
    ++selfTestIndex_;
    selfTestIdleTicks_ = 0;
    selfTestWaitTicks_ = 0;
    if (!ok) selfTestFinish(1);
}

bool KlustersApp::selfTestRunStep(const QJsonObject& step, QJsonObject& result)
{
    if (!doc) { result.insert(QStringLiteral("error"), QStringLiteral("no document")); return false; }
    if (step.contains(QStringLiteral("show"))) {
        const QString m = step.value(QStringLiteral("show")).toString();
        const bool ok = showFeatureSpace(m, /*syncFirst*/ false);
        result.insert(QStringLiteral("active"), doc->activeFeatureMethod());
        return ok && doc->activeFeatureMethod() == m;
    }
    if (step.contains(QStringLiteral("realign"))) {
        const int id = step.value(QStringLiteral("realign")).toInt();
        if (realignBlockedHere(tr("Realign"), /*silent*/ true)) {
            result.insert(QStringLiteral("error"), QStringLiteral("realign refused in the shown space"));
            return false;
        }
        startRealignForCluster(id);
        result.insert(QStringLiteral("started"), realignRunning || (realignQueue && realignQueue->isBusy()));
        return true;                                  // the result lands in the files; the next step waits
    }
    if (step.contains(QStringLiteral("nudge"))) {
        const QJsonArray a = step.value(QStringLiteral("nudge")).toArray();
        const int id = a.at(0).toInt(), delta = a.at(1).toInt();
        if (!enterClusterSpaceForRealign(tr("Nudge"), /*silent*/ true)) {
            result.insert(QStringLiteral("error"), QStringLiteral("nudge refused in the shown space"));
            return false;
        }
        nudgeInProgress = true;
        const bool ok = doc->nudgeClusterTimestamps(id, delta);
        nudgeInProgress = false;
        returnFromRealignSpace();                     // the follow, and back to the space shown
        return ok;
    }
    if (step.contains(QStringLiteral("supersample"))) {
        const int factor = step.value(QStringLiteral("supersample")).toInt();
        QString token, log, err;
        if (doc->isBorrowedSpace() && !showFeatureSpace(doc->cluMethod(), false)) return false;
        const bool ok = doc->buildSupersampledSpace(factor, &token, log, &err);
        result.insert(QStringLiteral("token"), token);
        result.insert(QStringLiteral("log"), log);
        if (!ok) result.insert(QStringLiteral("error"), err);
        if (ok) { populateFeatureSpaceCombo(); doc->preloadFeatureSpaces(); }
        return ok;
    }
    if (step.contains(QStringLiteral("follow"))) {
        QString log;
        const bool ok = doc->followMovedSpikes(log);
        result.insert(QStringLiteral("log"), log);
        return ok;
    }
    if (step.contains(QStringLiteral("save"))) {
        if (doc->isBorrowedSpace()) {
            // Save works from any space (the .clu keeps its method); the GUI path
            // is the same slot.
        }
        slotFileSave();
        return true;                                  // the next step waits for the save thread
    }
    if (step.contains(QStringLiteral("saveas"))) {
        // File > Save As to the given .clu path (a token naming an existing space
        // adopts it and the document reopens there -- the next step waits).
        startSave(step.value(QStringLiteral("saveas")).toString(), /*isSaveAs*/ true);
        return true;
    }
    if (step.contains(QStringLiteral("select"))) {
        // {"select": [ids]}: the parent palette's selection, as a click would set it.
        QList<int> ids;
        for (const QJsonValue& v : step.value(QStringLiteral("select")).toArray()) ids.append(v.toInt());
        if (!clusterPalette || !activeView()) { result.insert(QStringLiteral("error"), QStringLiteral("no palette")); return false; }
        doc->setActiveClustering(false);
        clusterPalette->selectItems(ids);
        doc->shownClustersUpdate(ids, *activeView());
        return true;
    }
    if (step.contains(QStringLiteral("multiLasso"))) {
        // {"multiLasso": [[rows], [rows], ...]}: keeps one lasso per list (0-based .spk
        // indices) in the active cluster view and splits them, as Enter does.
        ClusterView* cv = activeClusterView();
        if (!cv) { result.insert(QStringLiteral("error"), QStringLiteral("no cluster view")); return false; }
        for (const QJsonValue& lv : step.value(QStringLiteral("multiLasso")).toArray()) {
            QSet<dataType> rows;
            for (const QJsonValue& v : lv.toArray()) rows.insert(static_cast<dataType>(v.toInt() + 1));
            cv->keepMultiLassoRows(rows);
        }
        cv->applyMultiLasso();
        return true;
    }
    if (step.contains(QStringLiteral("moveToEnd"))) {
        // {"moveToEnd": true}: the T key on the current parent-palette selection.
        slotMoveSelectedClustersToEnd();
        return true;
    }
    if (step.contains(QStringLiteral("palette"))) {
        // {"palette": true}: the parent palette as the curator sees it -- the ids in
        // list order, which of them are selected, which are scrolled into view, and
        // each cluster's spike count.
        QJsonArray order, selected;
        QJsonObject counts;
        if (clusterPalette) {
            for (int id : clusterPalette->selectedClusters()) selected.append(id);
            for (int id : clusterPalette->listedClusters()) order.append(id);
            QJsonArray visible;                       // the ids the list's viewport shows
            for (int id : clusterPalette->visibleClusters()) visible.append(id);
            result.insert(QStringLiteral("visible"), visible);
        }
        const QVector<dataType> labels = doc->parentData().labelByFeatureRow();
        QHash<int, int> n;
        for (int r = 1; r < labels.size(); ++r) ++n[static_cast<int>(labels[r])];
        for (auto it = n.cbegin(); it != n.cend(); ++it) counts.insert(QString::number(it.key()), it.value());
        result.insert(QStringLiteral("order"), order);
        result.insert(QStringLiteral("selected"), selected);
        result.insert(QStringLiteral("counts"), counts);
        return true;
    }
    if (step.contains(QStringLiteral("residualScan"))) {
        // The Strip Residual Outliers scan, without the dialog and without applying it:
        // {"residualScan": {"clusters": [..], "kOn", "kOff", "minGain", "window",
        //  "minSpikes", "seed", "seedThreshold", "chunkSeconds"}} -> the flagged 0-based
        // .spk indices per cluster.  chunkSeconds > 0 tiles the session as the dialog's
        // "Uniform" choice does (ceil(T / chunk) equal chunks); 0 = the whole session.
        // "children": true reads the ids as children (the .clc layer), scored whole, as
        // the action does with children selected.  "apply": true then splits the flagged
        // spikes off as the action would ("separate": one cluster per term), in that
        // scope, and reports the layer afterwards.
        const QJsonObject o = step.value(QStringLiteral("residualScan")).toObject();
        QList<int> ids;
        for (const QJsonValue& v : o.value(QStringLiteral("clusters")).toArray()) ids.append(v.toInt());
        const bool children = o.value(QStringLiteral("children")).toBool(false);
        if (children) {
            QString err;
            if (!doc->loadChildClustering(err)) {
                result.insert(QStringLiteral("error"), QStringLiteral("no child layer: ") + err);
                return false;
            }
        }
        const Data& layer = doc->parentData();
        const double sessionSec = static_cast<double>(layer.maxTimeInRecordingUnits())
                                * layer.intervalOfSampling() / 1.0e6;
        const double chunkSec = o.value(QStringLiteral("chunkSeconds")).toDouble(0.0);
        std::vector<double> bounds;
        if (!children && chunkSec > 0.0 && sessionSec > chunkSec)
            bounds = neurosuite::drift::uniformPartition(
                sessionSec, static_cast<int>(std::ceil(sessionSec / chunkSec))).bounds;
        const KlustersDoc::ResidualOutlierResult R = doc->detectResidualOutliers(
            ids,
            o.value(QStringLiteral("kOn")).toDouble(3.0),
            o.value(QStringLiteral("kOff")).toDouble(3.0),
            o.value(QStringLiteral("minGain")).toDouble(0.25),
            o.value(QStringLiteral("window")).toInt(layer.nbOfSampleInWaveform() / 2),
            static_cast<long>(o.value(QStringLiteral("minSpikes")).toInt(40)),
            o.value(QStringLiteral("seed")).toInt(0),
            o.value(QStringLiteral("seedThreshold")).toBool(true),
            bounds, children);
        QJsonObject flagged;
        for (auto it = R.byCluster.constBegin(); it != R.byCluster.constEnd(); ++it) {
            QJsonArray idx;
            for (const KlustersDoc::ResidualFlag& f : it.value()) idx.append(f.index);
            flagged.insert(QString::number(it.key()), idx);
        }
        QJsonArray lines;
        for (const QString& l : R.perCluster) lines.append(l);
        result.insert(QStringLiteral("ok"), R.ok);
        result.insert(QStringLiteral("scored"), R.scored);
        result.insert(QStringLiteral("total"), static_cast<qint64>(R.total));
        result.insert(QStringLiteral("chunks"), R.chunks);
        result.insert(QStringLiteral("sessionSec"), sessionSec);
        result.insert(QStringLiteral("flagged"), flagged);
        result.insert(QStringLiteral("lines"), lines);
        result.insert(QStringLiteral("reason"), R.reason);
        if (R.ok && o.value(QStringLiteral("apply")).toBool(false)) {
            // As the action applies it: on the active layer, which for children is the
            // child clustering.
            if (children) doc->setActiveClustering(true);
            long long placed = 0;
            QList<int> created;
            const QStringList made = applyResidualStrip(
                R, o.value(QStringLiteral("separate")).toBool(false), children, &placed, &created);
            QJsonArray madeLines, createdIds;
            for (const QString& l : made) madeLines.append(l);
            QJsonObject parents;
            for (int id : created) {
                createdIds.append(id);
                if (children) parents.insert(QString::number(id), doc->parentOfChild(id));
            }
            auto sizes = [](Data& d) {
                QJsonObject s;
                for (dataType id : d.clusterIds()) s.insert(QString::number(id), static_cast<qint64>(d.nbOfSpikes(id)));
                return s;
            };
            result.insert(QStringLiteral("placed"), static_cast<qint64>(placed));
            result.insert(QStringLiteral("made"), madeLines);
            result.insert(QStringLiteral("created"), createdIds);
            result.insert(QStringLiteral("createdParent"), parents);
            result.insert(QStringLiteral("parentSizes"), sizes(doc->parentData()));
            if (children) {
                result.insert(QStringLiteral("childSizes"), sizes(doc->childClusterData()));
                QJsonObject all;                     // every child's parent, as the document has it now
                for (dataType id : doc->childClusterData().clusterIds())
                    all.insert(QString::number(id), doc->parentOfChild(static_cast<int>(id)));
                result.insert(QStringLiteral("childParents"), all);
            }
            if (children) doc->setActiveClustering(false);
        }
        return R.ok;
    }
    if (step.contains(QStringLiteral("dump"))) {
        result.insert(QStringLiteral("dump"), selfTestDump(step.value(QStringLiteral("cluster")).toInt(-1)));
        return true;
    }
    result.insert(QStringLiteral("error"), QStringLiteral("unknown step"));
    return false;
}

QJsonObject KlustersApp::selfTestDump(int clusterId) const
{
    QJsonObject d;
    d.insert(QStringLiteral("clu"), doc->url());
    d.insert(QStringLiteral("cluMethod"), doc->cluMethod());
    d.insert(QStringLiteral("active"), doc->activeFeatureMethod());
    d.insert(QStringLiteral("modified"), doc->isModified());
    d.insert(QStringLiteral("supersampleFactor"), doc->supersampleFactor());
    d.insert(QStringLiteral("phaseColumn"), doc->supersamplePhaseColumn());
    d.insert(QStringLiteral("lastSaveError"), doc->lastSaveError());
    QJsonArray spaces;
    for (const KlustersDoc::FeatureSpace& s : doc->availableFeatureSpaces()) {
        QJsonObject o;
        o.insert(QStringLiteral("method"), s.method);
        o.insert(QStringLiteral("fet"), s.fetPath);
        o.insert(QStringLiteral("spk"), s.spkPath);
        o.insert(QStringLiteral("usable"), s.usable);
        o.insert(QStringLiteral("synced"), s.synced);
        o.insert(QStringLiteral("syncStale"), s.syncStale);
        o.insert(QStringLiteral("reason"), s.reason);
        spaces.append(o);
    }
    d.insert(QStringLiteral("spaces"), spaces);
    if (clusterId >= 0) {
        // The loaded table's times (and phases) of the cluster's spikes, in row order.
        const Data& data = doc->data();
        const int timeDim = data.timeDimension();
        const int phaseCol = doc->supersamplePhaseColumn();
        QJsonArray rows, times, phases;
        const QVector<dataType> labels = data.labelByFeatureRow();
        for (int r = 1; r < labels.size(); ++r) {
            if (labels[r] != clusterId) continue;
            rows.append(r - 1);
            times.append(static_cast<double>(data.featureValue(static_cast<dataType>(r), timeDim)));
            if (phaseCol >= 0) phases.append(static_cast<double>(data.featureValue(static_cast<dataType>(r), phaseCol + 1)));
        }
        QJsonObject c;
        c.insert(QStringLiteral("id"), clusterId);
        c.insert(QStringLiteral("rows"), rows);
        c.insert(QStringLiteral("times"), times);
        c.insert(QStringLiteral("phases"), phases);
        d.insert(QStringLiteral("cluster"), c);
    }
    return d;
}

void KlustersApp::selfTestNoteRealign(bool ok, int nShifted)
{
    if (!selfTestTimer_) return;
    QJsonObject r;
    r.insert(QStringLiteral("realignDone"), ok);
    r.insert(QStringLiteral("nShifted"), nShifted);
    r.insert(QStringLiteral("log"), selfTestRealignLog_.right(20000));
    selfTestRealignLog_.clear();
    selfTestResults_.append(r);
}

void KlustersApp::selfTestFinish(int code)
{
    if (selfTestTimer_) selfTestTimer_->stop();
    if (!selfTestReport_.isEmpty()) {
        QJsonObject rep;
        rep.insert(QStringLiteral("code"), code);
        rep.insert(QStringLiteral("results"), selfTestResults_);
        QFile out(selfTestReport_);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(QJsonDocument(rep).toJson());
    }
    // Leave without the "save changes?" question: the files are what the steps wrote.
    if (doc) doc->setModified(false);
    QTimer::singleShot(0, qApp, [code]() { qApp->exit(code); });
}
