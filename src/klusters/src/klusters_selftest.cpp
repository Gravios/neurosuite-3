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
#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "data.h"
#include "savethread.h"
#include "serialjobqueue.h"
#include "configuration.h"

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
