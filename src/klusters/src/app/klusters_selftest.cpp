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
 *        {"burstFollowers": [a, b]}      Split > Split Burst Followers on two clusters;
 *                                        with "mixed": c, references a and b sort cluster c;
 *                                        with "children": true, the ids are children
 *        {"coreCut": [ids]}              Split > Split Off Spurious Spikes on each cluster;
 *                                        with "children": true, the ids are children; with
 *                                        "onsetFactor": f, that preference is set first (not
 *                                        written to disk); the result carries "shortcut", the
 *                                        action's key
 *        {"prefPage": "<class>"}         the Preferences dialog is built and filled from the
 *                                        configuration, without being shown: whether it has a
 *                                        page of that class, the titles of the page's group
 *                                        boxes; "saveTo": a path the page's image is written to;
 *                                        "apply": true writes the dialog's values back (a round
 *                                        trip through every page); "dialogSize": [w, h] with
 *                                        "dialogSaveTo": the whole dialog at that size, this page
 *                                        showing ("scrollToEnd": scrolled to its end first), with
 *                                        the height the page is drawn at and where its last
 *                                        group's bottom edge falls
 *        {"hierarchy": {...}}            the child layer (loaded if it is not): one edit, then
 *                                        every child's parent and size.  "mergeParents": [..],
 *                                        "mergeChildren": [..], "promoteChildren": [..],
 *                                        "dropChildren": [..] (to noise), "showChildren": [..]
 *                                        (the child palette selecting them; [] = back to parents),
 *                                        "compact": true (Compact Cluster IDs), "repairNesting":
 *                                        true, "flatten": true, "renumber": true (parents made
 *                                        consecutive), "toEnd": [parents] (the palette's T),
 *                                        "order": [parents] (a sort), "undo": true (the parent
 *                                        layer), "undoChild" / "redoChild": true; none: only
 *                                        the report.  "upgraded": children the load renumbered
 *                                        from the old numbering
 *        {"purge": n}                    Clusters > Purge Small Clusters below n spikes, confirmed
 *        {"childPalette": {...}}         "show": View > Show Child Cluster Palette; "click": a left
 *                                        click on that child in it; reports what it lists and selects
 *        {"matrixSelect": [ids]}         a click on a cell of a curation matrix ("add": true: Ctrl);
 *                                        reports the layer shown and both palettes' selections
 *        {"matrixView": {...}}           one of the four curation matrices ("which"), opened if "open",
 *                                        at "size": [w, h], computed if "update", then driven by "do":
 *                                        {"wheel": [x, y, delta]} (Ctrl+wheel), {"drag": [x0, y0, x1, y1]}
 *                                        (Ctrl+left), {"click": [x, y]}, {"follow": id}, {"state": [zoom,
 *                                        panX, panY]}; after each: the zoom and pan last
 *                                        announced, the clusters shown, a hash of the view's picture
 *        {"commandDump": true}           every menu command with its text, keys, tips and state, the
 *                                        binding registry's commands, and the edit lock's set
 *        {"configWrite": true}           write the configuration as read; {"prefKeys": [keys]} work Preferences by keys
 *        {"keyCommand": "<id>"}          run a key command of the binding registry; reports blocked / idsChanged
 *        {"undo": true} / {"redo": true} Edit > Undo / Redo; report the steps left each way and the status line
 *        {"undoState": true}             the steps Undo and Redo can take now
 *        {"alignAll": true}              Spikes > Align All Clusters, confirmed
 *        {"cancelJob": true}             Edit > Cancel Running Job; reports whether there was one to cancel
 *        {"actionsEnabled": true}        the command ids that can be triggered now and those that
 *                                        cannot, and which jobs run
 *        "whileBusy": true on any step   it runs on the next tick, without waiting for the job the
 *                                        step before it started
 *        {"annotate": {...}}             a cluster's information: "cluster", "quality", "notes"
 *                                        set it (one undo step), neither only reads it
 *        {"group": [ids]}                the palette's Group on those parents (the automatic
 *                                        realign / renumber of the result run as configured)
 *        {"wait": ms}                    let time pass with events running
 *        {"curationLog": {...}}          "consideredDwell": seconds a comparison must be shown to
 *                                        be logged as CONSIDERED; "recommend": [ids] the
 *                                        Recommended-merges panel activating that pair
 *        {"save": true}                  File > Save
 *        {"reopen": true}                close the document and open it again (unsaved changes dropped);
 *                                        {"reopen": {"replace": {file: with}}} overwrites files in between
 *        {"saveas": "<path.clu>"}        File > Save As (a Save As under a token whose
 *                                        space exists adopts it and reopens there)
 *        {"dump": true, "cluster": <id>} the spaces, their files, whether each is held
 *                                        in memory, the loaded times (and phases) of
 *                                        the cluster's spikes
 * A modal dialog that opens during a step is closed (its default button) and
 * recorded in the step's result, so the run never hangs on one.  "answerNo": [texts]
 * in the spec: a message box whose title or text holds one of them gets No instead.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "neurosuite/core/drift_partition.hpp"   // uniformPartition (residualScan)
#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include <qextendtabwidget.h>
#include <QElapsedTimer>
#include "clusterview.h"
#include "waveformview.h"
#include "clusterpalette.h"
#include "processwidget.h"
#include "data.h"
#include "savethread.h"
#include "serialjobqueue.h"
#include "configuration.h"
#include "prefdialog.h"
#include "actionid.h"
#include <pagekeynavigator.h>
#include <QAbstractItemView>
#include <QSettings>
#include "input/bindingregistry.h"
#include "input/inputdispatcher.h"
#include "input/chord.h"

#include <cmath>
#include <vector>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QEventLoop>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include "errormatrixview.h"
#include "templatematrixview.h"
#include "residualmatrixview.h"
#include "driftmatrixview.h"

#include <neurosuite/core/custody.hpp>

extern int nbUndo;      // the undo depth (main.cpp)

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
    if (spec.contains(QStringLiteral("realignUndoMemoryMB")))    // MB kept for the undo of realigns; 0: none
        configuration().setRealignUndoMemoryMB(spec.value(QStringLiteral("realignUndoMemoryMB")).toInt());
    if (spec.contains(QStringLiteral("nbUndo"))) {                // the undo depth (Preferences > General)
        configuration().setNbUndo(spec.value(QStringLiteral("nbUndo")).toInt());
        nbUndo = configuration().getNbUndo();
    }
    // The three automatic steps after a cluster edit (Preferences > Clustering).
    if (spec.contains(QStringLiteral("autoRealign")))
        configuration().setAutoRealignAfterMerge(spec.value(QStringLiteral("autoRealign")).toBool());
    if (spec.contains(QStringLiteral("autoRenumber")))
        configuration().setAutoRenumberAfterMerge(spec.value(QStringLiteral("autoRenumber")).toBool());
    if (spec.contains(QStringLiteral("autoMatrices")))
        configuration().setAutoUpdateMatricesAfterMerge(spec.value(QStringLiteral("autoMatrices")).toBool());
    if (spec.contains(QStringLiteral("realignVerbose")))
        configuration().setRealignVerbose(spec.value(QStringLiteral("realignVerbose")).toBool());
    selfTestAnswerNo_ = spec.value(QStringLiteral("answerNo")).toArray();
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


namespace {
// {"matrixView": {...}} on one of the four curation matrices: drive it with the events a
// curator sends and report what it shows.  The same for every kind of matrix: they have
// these names in common.
template <class View>
void selfTestDriveMatrix(View* view, const QJsonObject& o, QJsonObject& result, KlustersView* display)
{
    const auto settle = []() { for (int i = 0; i < 4; ++i) QCoreApplication::processEvents(); };
    double zoom = 0.0, panX = 0.0, panY = 0.0;
    int changes = 0;
    const QMetaObject::Connection c = QObject::connect(view, &View::viewChanged, view,
        [&](double z, double x, double y) { zoom = z; panX = x; panY = y; ++changes; });

    if (o.contains(QStringLiteral("size"))) {               // a known size: the dock floating
        const QJsonArray sz = o.value(QStringLiteral("size")).toArray();
        if (auto* dock = qobject_cast<QDockWidget*>(view->parentWidget())) {
            dock->setFloating(true);
            dock->show();
        }
        view->setFixedSize(sz.at(0).toInt(), sz.at(1).toInt());
        settle();
    }
    if (o.value(QStringLiteral("update")).toBool()) {
        view->updateMatrixContents();
        QElapsedTimer t; t.start();
        while ((view->isThreadsRunning() || !view->hasComputedData()) && t.elapsed() < 60000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        settle();
    }
    const QPoint origin = view->mapToGlobal(QPoint(0, 0));
    for (const QJsonValue& v : o.value(QStringLiteral("do")).toArray()) {
        const QJsonObject a = v.toObject();
        if (a.contains(QStringLiteral("wheel"))) {            // [x, y, delta]: Ctrl + wheel
            const QJsonArray w = a.value(QStringLiteral("wheel")).toArray();
            const QPointF pos(w.at(0).toDouble(), w.at(1).toDouble());
            QWheelEvent ev(pos, pos + origin, QPoint(), QPoint(0, w.at(2).toInt()), Qt::NoButton,
                           a.value(QStringLiteral("plain")).toBool() ? Qt::NoModifier : Qt::ControlModifier,
                           Qt::NoScrollPhase, false);
            QApplication::sendEvent(view, &ev);
        } else if (a.contains(QStringLiteral("drag"))) {      // [x0, y0, x1, y1]: Ctrl + left drag
            const QJsonArray d = a.value(QStringLiteral("drag")).toArray();
            const QPointF p0(d.at(0).toDouble(), d.at(1).toDouble()), p1(d.at(2).toDouble(), d.at(3).toDouble());
            QMouseEvent press(QEvent::MouseButtonPress, p0, p0 + origin, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
            QApplication::sendEvent(view, &press);
            const QPointF mid = (p0 + p1) / 2.0;
            for (const QPointF& p : { mid, p1 }) {
                QMouseEvent move(QEvent::MouseMove, p, p + origin, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
                QApplication::sendEvent(view, &move);
            }
            QMouseEvent release(QEvent::MouseButtonRelease, p1, p1 + origin, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(view, &release);
        } else if (a.contains(QStringLiteral("click"))) {     // [x, y]: a left click ("shift": true)
            const QJsonArray d = a.value(QStringLiteral("click")).toArray();
            const QPointF p(d.at(0).toDouble(), d.at(1).toDouble());
            const Qt::KeyboardModifiers mods = a.value(QStringLiteral("shift")).toBool() ? Qt::ShiftModifier : Qt::NoModifier;
            QMouseEvent press(QEvent::MouseButtonPress, p, p + origin, Qt::LeftButton, Qt::LeftButton, mods);
            QApplication::sendEvent(view, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, p, p + origin, Qt::LeftButton, Qt::NoButton, mods);
            QApplication::sendEvent(view, &release);
        } else if (a.contains(QStringLiteral("follow"))) {    // centre on that cluster
            view->followCluster(a.value(QStringLiteral("follow")).toInt());
        } else if (a.contains(QStringLiteral("state"))) {     // [zoom, panX, panY]
            const QJsonArray d = a.value(QStringLiteral("state")).toArray();
            view->setViewState(d.at(0).toDouble(), d.at(1).toDouble(), d.at(2).toDouble());
        }
        settle();
        // What each action left: the view state last announced, what the display shows, the picture.
        QJsonObject after;
        after.insert(QStringLiteral("zoom"), zoom);
        after.insert(QStringLiteral("panX"), panX);
        after.insert(QStringLiteral("panY"), panY);
        after.insert(QStringLiteral("changes"), changes);
        QJsonArray shown;
        if (display) for (int id : display->clusters()) shown.append(id);
        after.insert(QStringLiteral("shown"), shown);
        const QImage img = view->grab().toImage().convertToFormat(QImage::Format_RGB32);
        after.insert(QStringLiteral("image"), QString::fromLatin1(
            QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(img.constBits()),   // the pixels, as bytes
                                                    img.sizeInBytes()), QCryptographicHash::Sha1).toHex().left(16)));
        QJsonArray steps = result.value(QStringLiteral("after")).toArray();
        steps.append(after);
        result.insert(QStringLiteral("after"), steps);
    }
    QObject::disconnect(c);
    result.insert(QStringLiteral("computed"), view->hasComputedData());
    QJsonArray ids;
    for (int id : view->matrixClusterList()) ids.append(id);
    result.insert(QStringLiteral("clusters"), ids);
    result.insert(QStringLiteral("width"), view->width());
    result.insert(QStringLiteral("height"), view->height());
    if (o.contains(QStringLiteral("saveTo")))
        result.insert(QStringLiteral("saved"), view->grab().save(o.value(QStringLiteral("saveTo")).toString()));
}
} // namespace

void KlustersApp::selfTestTick()
{
    // A modal that a step opened would wait for a person: take its default.
    if (QWidget* w = QApplication::activeModalWidget()) {
        if (auto* box = qobject_cast<QMessageBox*>(w)) {
            QJsonObject note;
            note.insert(QStringLiteral("dialog"), box->windowTitle() + QStringLiteral(": ") + box->text());
            selfTestResults_.append(note);
            // "answerNo": [texts] in the spec: a box whose title or text holds one of them
            // gets No instead of its default.
            bool no = false;
            for (const QJsonValue& v : selfTestAnswerNo_)
                no = no || note.value(QStringLiteral("dialog")).toString().contains(v.toString());
            if (QAbstractButton* b = no ? box->button(QMessageBox::No) : nullptr) b->click();
            else if (QPushButton* b = box->defaultButton()) b->click(); else box->reject();
        } else {
            w->close();
        }
        return;
    }
    const bool busy = fileJobRunning() || isInit || !(processFinished && processOutputsFinished)
                   || (nudgeFollowTimer && nudgeFollowTimer->isActive());
    if (busy) { selfTestIdleTicks_ = 0; }
    else      { ++selfTestIdleTicks_; }
    // {"whileBusy": true} on a step: it runs on the next tick, without waiting for the
    // job the step before it started (what the curator can do while a job runs).
    if (selfTestIndex_ < selfTestSteps_.size()
        && selfTestSteps_.at(selfTestIndex_).toObject().value(QStringLiteral("whileBusy")).toBool())
        selfTestIdleTicks_ = kIdleTicks;
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
        if (step.value(QStringLiteral("children")).toBool()) {
            // The child view showing that child, as a click in the child palette leaves it.
            QString err;
            if (!doc->loadChildClustering(err)) {
                result.insert(QStringLiteral("error"), QStringLiteral("no child layer: ") + err);
                return false;
            }
            doc->setActiveClustering(true);
            if (activeView()) doc->shownClustersUpdate(QList<int>{ id }, *activeView());
        }
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
    if (step.contains(QStringLiteral("reopen"))) {
        // {"reopen": true | {"replace": {file: with, ...}}}: File > Close, then open the same file
        // again, in this process.  Unsaved changes are dropped without the question File > Close
        // would ask.  "replace": while it is closed, each file is overwritten with a copy of
        // another (what a tool run between the two does).
        const QString url = doc->url();
        doc->setModified(false);
        slotFileClose();
        if (mainDock) return false;                 // the close did not complete
        const QJsonObject swaps = step.value(QStringLiteral("reopen")).toObject()
                                      .value(QStringLiteral("replace")).toObject();
        for (auto it = swaps.begin(); it != swaps.end(); ++it) {
            QFile::remove(it.key());
            if (!QFile::copy(it.value().toString(), it.key())) return false;
        }
        openDocumentFile(url);
        return doc && !doc->url().isEmpty();
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
    if (step.contains(QStringLiteral("group"))) {
        // {"group": [ids]}: the palette's Group on those parents, as the curator's merge runs --
        // with the automatic realign and renumber of the result when those preferences are on.
        QList<int> ids;
        for (const QJsonValue& v : step.value(QStringLiteral("group")).toArray()) ids.append(v.toInt());
        slotGroupClusters(ids);
        return true;                                  // the next step waits for what it started
    }
    if (step.contains(QStringLiteral("wait"))) {
        // {"wait": ms}: let that much time pass, events running (a selection being looked at).
        QEventLoop loop;
        QTimer::singleShot(step.value(QStringLiteral("wait")).toInt(), &loop, &QEventLoop::quit);
        loop.exec();
        return true;
    }
    if (step.contains(QStringLiteral("curationLog"))) {
        // {"curationLog": {"consideredDwell": s}}: the seconds a comparison must stay on screen
        // to be logged as CONSIDERED; {"recommend": [ids]}: the Recommended-merges panel
        // activating that pair.
        const QJsonObject o = step.value(QStringLiteral("curationLog")).toObject();
        if (o.contains(QStringLiteral("consideredDwell")))
            doc->setConsideredMinDwell(o.value(QStringLiteral("consideredDwell")).toDouble());
        if (o.contains(QStringLiteral("recommend"))) {
            QList<int> ids;
            for (const QJsonValue& v : o.value(QStringLiteral("recommend")).toArray()) ids.append(v.toInt());
            slotRecommendationActivated(ids);
        }
        result.insert(QStringLiteral("consideredDwell"), doc->consideredMinDwell());
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
    if (step.contains(QStringLiteral("recluster"))) {
        // {"recluster": "" | "median" | "channel"}: Recluster on the current selection, with the
        // active engine and the preferences as they are ("median" / "channel": the one-shot modes).
        // {"recluster": ..., "exe": "<path>", "args": "<template>", "autoSelect": bool, "nFeatures": n}
        // override the executable, its argument template and the toolbar's feature controls;
        // "chunking": 0-3 (Configuration::ReclusterChunking) and "chunkMinutes" set the time chunking.
        const QJsonObject o = step.value(QStringLiteral("opts")).toObject();
        if (o.contains(QStringLiteral("exe")))  reclusteringExecutable = o.value(QStringLiteral("exe")).toString();
        if (o.contains(QStringLiteral("args"))) reclusteringArgs = o.value(QStringLiteral("args")).toString();
        if (o.contains(QStringLiteral("autoSelect"))) autoSelectFeatures = o.value(QStringLiteral("autoSelect")).toBool();
        if (o.contains(QStringLiteral("nFeatures")))  autoSelectNFeatures = o.value(QStringLiteral("nFeatures")).toInt();
        if (o.contains(QStringLiteral("chunking")))
            configuration().setReclusterChunking(o.value(QStringLiteral("chunking")).toInt());
        if (o.contains(QStringLiteral("chunkMinutes")))
            configuration().setReclusterChunkMinutes(o.value(QStringLiteral("chunkMinutes")).toDouble());
        result.insert(QStringLiteral("engine"), activeReclusterEngine());
        result.insert(QStringLiteral("exe"), reclusteringExecutable);
        result.insert(QStringLiteral("argsTemplate"), reclusteringArgs);
        result.insert(QStringLiteral("autoSelect"), autoSelectFeatures);
        result.insert(QStringLiteral("nFeatures"), autoSelectNFeatures);
        const QString m = step.value(QStringLiteral("recluster")).toString();
        if (m == QLatin1String("median")) slotReclusterMedianResidual();
        else if (m == QLatin1String("channel")) slotReclusterChannelVariance();
        else slotRecluster();
        return true;
    }
    if (step.contains(QStringLiteral("reclusterAgainst"))) {
        // {"reclusterAgainst": <mixed id>, "opts": {"exe", "args", ...}}: Recluster >
        // Recluster Against References on the current selection, with that cluster as the mixed
        // one (the dialog's answer).  {"reclusterAgainstReport": true} reports what the last one found.
        const QJsonObject o = step.value(QStringLiteral("opts")).toObject();
        if (o.contains(QStringLiteral("exe")))  reclusteringExecutable = o.value(QStringLiteral("exe")).toString();
        if (o.contains(QStringLiteral("args"))) reclusteringArgs = o.value(QStringLiteral("args")).toString();
        if (o.contains(QStringLiteral("autoSelect"))) autoSelectFeatures = o.value(QStringLiteral("autoSelect")).toBool();
        // "referenceCap": n -- most spikes of one reference pooled, for this run (0 = the rule).
        doc->setReclusterReferenceCapOverride(o.value(QStringLiteral("referenceCap")).toInt(0));
        result.insert(QStringLiteral("started"),
                      startReclusterAgainstReferences(step.value(QStringLiteral("reclusterAgainst")).toInt(-1)));
        return true;
    }
    if (step.contains(QStringLiteral("reclusterAgainstReport"))) {
        const Data::ReclusterAgainstReport& r = doc->reclusterAgainstReport();
        QJsonObject pieces;
        for (auto it = r.pieces.constBegin(); it != r.pieces.constEnd(); ++it)       // by the ids they have now
            pieces.insert(QString::number(doc->reclusterAgainstCurrentId(it.key())), static_cast<double>(it.value()));
        QJsonArray refs;
        for (const Data::ReclusterAgainstReport::Reference& ref : r.references) {
            QJsonObject by;
            for (auto it = ref.byPiece.constBegin(); it != ref.byPiece.constEnd(); ++it)
                by.insert(QString::number(doc->reclusterAgainstCurrentId(it.key())), static_cast<double>(it.value()));
            refs.append(QJsonObject{{QStringLiteral("id"), ref.id}, {QStringLiteral("spikes"), static_cast<double>(ref.spikes)},
                                    {QStringLiteral("pooled"), static_cast<double>(ref.pooled)},
                                    {QStringLiteral("largest"), static_cast<double>(ref.largest)},
                                    {QStringLiteral("elsewhere"), static_cast<double>(ref.elsewhere)},
                                    {QStringLiteral("byPiece"), by}});
        }
        result.insert(QStringLiteral("mixed"), r.mixed);
        result.insert(QStringLiteral("mixedSpikes"), static_cast<double>(r.mixedSpikes));
        result.insert(QStringLiteral("pieces"), pieces);
        result.insert(QStringLiteral("references"), refs);
        result.insert(QStringLiteral("ignored"), r.ignored);
        result.insert(QStringLiteral("summary"), QJsonArray::fromStringList(reclusterAgainstSummary()));
        return true;
    }
    if (step.contains(QStringLiteral("reclusterChunkArgs"))) {
        // {"reclusterChunkArgs": {"exe": ..., "args": [...], "chunking": 0-3, "minutes": n}}:
        // the chunking flag a recluster would add to that command line (nothing is run).
        const QJsonObject o = step.value(QStringLiteral("reclusterChunkArgs")).toObject();
        QStringList args;
        for (const QJsonValue& v : o.value(QStringLiteral("args")).toArray()) args.append(v.toString());
        QJsonArray out;
        for (const QString& a : reclusterChunkArguments(o.value(QStringLiteral("exe")).toString(), args,
                                                        o.value(QStringLiteral("chunking")).toInt(),
                                                        o.value(QStringLiteral("minutes")).toDouble(7.0)))
            out.append(a);
        result.insert(QStringLiteral("chunkArgs"), out);
        QJsonArray model;                                   // and the model-file flag for the same command line
        for (const QString& a : reclusterModelArguments(o.value(QStringLiteral("exe")).toString(), args)) model.append(a);
        result.insert(QStringLiteral("modelArgs"), model);
        return true;
    }
    if (step.contains(QStringLiteral("reclusterLog"))) {
        // {"reclusterLog": true}: the recluster output pane's lines (the command first).
        QJsonArray lines;
        if (processWidget)
            for (int i = 0; i < processWidget->count(); ++i) lines.append(processWidget->item(i)->text());
        result.insert(QStringLiteral("lines"), lines);
        return true;
    }
    if (step.contains(QStringLiteral("hover"))) {
        // {"hover": {"spike": i}}   move the cursor onto the spike with 0-based .spk index i in the
        //                           active cluster view (where it is drawn), as a mouse move does;
        // {"hover": {"at": [x, y]}} move it to that viewport point;
        // {"hover": {"leave": true}} the cursor leaves the view;   {"hover": {"toggle": true}} Shift+V;
        // {"hover": {"prefs": {"enabled", "ringMargin", "ringWidth", "traceWidth"}}} sets the
        // Preferences > Cluster view knobs named and applies them as the dialog's Apply does.
        // Reports what the cluster view tracks (and where that spike is drawn) and what every waveform
        // view overlays: the spike, its
        // cluster, the samples drawn, the overlay / cluster / background colours, and how many pixels
        // of the grabbed waveform view carry the overlay colour and the columns they span ("saveTo": a
        // path the grabbed image is written to).
        ClusterView* cv = activeClusterView();
        if (!cv || !activeView()) { result.insert(QStringLiteral("error"), QStringLiteral("no cluster view")); return false; }
        const QJsonObject o = step.value(QStringLiteral("hover")).toObject();
        if (o.contains(QStringLiteral("prefs"))) {
            const QJsonObject pr = o.value(QStringLiteral("prefs")).toObject();
            Configuration::HoverSpike h = configuration().getHoverSpike();
            if (pr.contains(QStringLiteral("enabled")))    h.enabled      = pr.value(QStringLiteral("enabled")).toBool();
            if (pr.contains(QStringLiteral("ringMargin"))) h.ringMarginPx = pr.value(QStringLiteral("ringMargin")).toInt();
            if (pr.contains(QStringLiteral("ringWidth")))  h.ringWidthPx  = pr.value(QStringLiteral("ringWidth")).toDouble();
            if (pr.contains(QStringLiteral("traceWidth"))) h.traceWidthPx = pr.value(QStringLiteral("traceWidth")).toDouble();
            configuration().setHoverSpike(h);
            applyHoverSpikePreference();
        }
        if (o.value(QStringLiteral("toggle")).toBool()) cv->toggleHoverSpike();
        if (o.contains(QStringLiteral("spike"))) {
            QPoint px;
            const bool found = cv->spikeViewportPos(o.value(QStringLiteral("spike")).toInt(), px);
            result.insert(QStringLiteral("drawn"), found);
            if (found) { cv->hoverAt(px); result.insert(QStringLiteral("x"), px.x()); result.insert(QStringLiteral("y"), px.y()); }
        }
        if (o.contains(QStringLiteral("at"))) {
            const QJsonArray a = o.value(QStringLiteral("at")).toArray();
            cv->hoverAt(QPoint(a.at(0).toInt(), a.at(1).toInt()));
        }
        if (o.value(QStringLiteral("leave")).toBool()) cv->clearHoverSpike();
        result.insert(QStringLiteral("active"), cv->hoverSpikeActive());
        {   // the ring: pixels of the overlay colour on the grabbed cluster view
            const Configuration::HoverSpike& look = configuration().getHoverSpike();
            result.insert(QStringLiteral("prefEnabled"), look.enabled);
            result.insert(QStringLiteral("ringMargin"), look.ringMarginPx);
            result.insert(QStringLiteral("ringWidth"), look.ringWidthPx);
            result.insert(QStringLiteral("traceWidth"), look.traceWidthPx);
            if (cv->hoveredSpike() >= 0) {
                const QColor rc = WaveformView::contrastingColor(doc->clusterColors().color(cv->hoveredCluster()),
                                                                 cv->palette().color(cv->backgroundRole()));
                cv->repaint();
                const QImage img = cv->grab().toImage();
                int n = 0, xMin = img.width(), xMax = -1;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        if (QColor(img.pixel(x, y)).rgb() == rc.rgb()) { ++n; xMin = qMin(xMin, x); xMax = qMax(xMax, x); }
                result.insert(QStringLiteral("ringPixels"), n);
                if (n) result.insert(QStringLiteral("ringSpan"), xMax - xMin + 1);
            }
        }
        result.insert(QStringLiteral("spike"), static_cast<int>(cv->hoveredSpike()));
        result.insert(QStringLiteral("cluster"), cv->hoveredCluster());
        if (cv->hoveredSpike() >= 0) {                // where the tracked spike itself is drawn
            QPoint tp;
            if (cv->spikeViewportPos(cv->hoveredSpike(), tp)) {
                result.insert(QStringLiteral("trackedX"), tp.x()); result.insert(QStringLiteral("trackedY"), tp.y());
            }
        }
        QJsonArray views;
        for (ViewWidget* w : activeView()->getViewList())
            if (WaveformView* wv = qobject_cast<WaveformView*>(w)) {
                QJsonObject v;
                v.insert(QStringLiteral("spike"), static_cast<int>(wv->hoverSpikeRow()));
                v.insert(QStringLiteral("cluster"), wv->hoverSpikeCluster());
                QJsonArray wave;
                for (float f : wv->hoverSpikeWaveform()) wave.append(static_cast<double>(f));
                v.insert(QStringLiteral("wave"), wave);
                const QColor bg = wv->palette().color(wv->backgroundRole());
                const int cid = wv->hoverSpikeCluster() >= 0 ? wv->hoverSpikeCluster() : o.value(QStringLiteral("colourOf")).toInt(-1);
                if (cid >= 0) {
                    const QColor cc = doc->clusterColors().color(cid), oc = WaveformView::contrastingColor(cc, bg);
                    v.insert(QStringLiteral("clusterColour"), cc.name());
                    v.insert(QStringLiteral("overlayColour"), oc.name());
                    v.insert(QStringLiteral("background"), bg.name());
                    wv->repaint();
                    const QImage img = wv->grab().toImage();
                    int n = 0, xMin = img.width(), xMax = -1;
                    for (int y = 0; y < img.height(); ++y)
                        for (int x = 0; x < img.width(); ++x)
                            if (QColor(img.pixel(x, y)).rgb() == oc.rgb()) { ++n; xMin = qMin(xMin, x); xMax = qMax(xMax, x); }
                    v.insert(QStringLiteral("overlayPixels"), n);
                    if (n) { v.insert(QStringLiteral("overlayXMin"), xMin); v.insert(QStringLiteral("overlayXMax"), xMax); }
                    v.insert(QStringLiteral("width"), img.width());
                    if (o.contains(QStringLiteral("saveTo"))) img.save(o.value(QStringLiteral("saveTo")).toString());
                }
                views.append(v);
            }
        result.insert(QStringLiteral("waveformViews"), views);
        return true;
    }
    if (step.contains(QStringLiteral("timeSort"))) {
        // {"timeSort": {}}: Spikes > Sort Spikes by Time without its dialog -- plan, then close,
        // rewrite and reopen.  Reports the plan: "outOfOrder", the "files" rewritten, those
        // "skipped" (another spike count) and "notReordered", and "sorted" (1 / 0 / -1).
        KlustersDoc::TimeSortPlan plan;
        QString err;
        if (!doc->planTimeSort(plan, err)) {
            result.insert(QStringLiteral("error"), err);
            return false;
        }
        QJsonArray files;
        for (const KlustersDoc::RowFile& f : plan.files) files.append(QFileInfo(f.path).fileName());
        result.insert(QStringLiteral("outOfOrder"), static_cast<double>(plan.nOutOfOrder));
        result.insert(QStringLiteral("files"), files);
        result.insert(QStringLiteral("skipped"), QJsonArray::fromStringList(plan.skipped));
        result.insert(QStringLiteral("notReordered"), QJsonArray::fromStringList(plan.notReordered));
        if (plan.nOutOfOrder == 0) return true;
        QString log;
        const int sorted = sortSpikesByTime(plan, log);
        result.insert(QStringLiteral("sorted"), sorted);
        result.insert(QStringLiteral("log"), log);
        return sorted == 1;
    }
    if (step.contains(QStringLiteral("collisionRows"))) {
        // {"collisionRows": {...}}: the collision rows (.eap rows naming a partner unit) as corrected.
        //   "prefs": {"enabled": bool}   Preferences > Cluster view > Subtract the partner from collision rows;
        //   "refresh": true              rebuild (or drop) them now, as Apply does (without it the
        //                                step only reports: "builds" counts the rebuilds so far);
        //   "rows": [r, ...]             0-based rows whose record is reported: "served" (what every
        //                                reader gets) and "raw" (what the .spk holds);
        //   "writeSpkc": path            the corrected rows shown now, as a sparse .spkc.
        // Reports what the last rebuild found: the .eap read, the rows with two or more classes, the
        // rows corrected, the partners fitted and subtracted, and how many corrected rows are served.
        const QJsonObject o = step.value(QStringLiteral("collisionRows")).toObject();
        QCoreApplication::processEvents();          // a save or a space switch just done queues its own rebuild
        if (o.contains(QStringLiteral("prefs"))) {
            const QJsonObject pr = o.value(QStringLiteral("prefs")).toObject();
            if (pr.contains(QStringLiteral("enabled")))
                configuration().setCorrectCollisionRows(pr.value(QStringLiteral("enabled")).toBool());
        }
        if (o.value(QStringLiteral("refresh")).toBool()) doc->refreshCollisionRows();
        // The report is of the rebuild the application made ITSELF (on open, after a realign, a
        // nudge, a save, an edit): the step does not rebuild, so a hook that is missing shows.
        const KlustersDoc::CollisionRowsReport rep = doc->collisionRowsLastReport();
        result.insert(QStringLiteral("builds"), doc->collisionRowsBuildCount());
        result.insert(QStringLiteral("outdated"), doc->collisionRowsOutdated());
        result.insert(QStringLiteral("enabled"), configuration().getCorrectCollisionRows());
        result.insert(QStringLiteral("eap"), QFileInfo(rep.eapPath).fileName());
        result.insert(QStringLiteral("eapFound"), rep.eapFound);
        result.insert(QStringLiteral("skipped"), rep.skipped);
        result.insert(QStringLiteral("collisionRows"), static_cast<double>(rep.collisionRows));
        result.insert(QStringLiteral("corrected"), static_cast<double>(rep.corrected));
        result.insert(QStringLiteral("partnersFitted"), rep.partnersFitted);
        result.insert(QStringLiteral("partnersSubtracted"), rep.partnersSubtracted);
        result.insert(QStringLiteral("partnersWithoutTemplate"), rep.partnersWithoutTemplate);
        result.insert(QStringLiteral("rowsWithoutOwnTemplate"), static_cast<double>(rep.rowsWithoutOwnTemplate));
        result.insert(QStringLiteral("offsetsDiffer"), rep.offsetsDiffer);
        QJsonArray deltas;                                      // accepted fits by (fitted - stored) offset, -4 .. +4
        for (const int v : rep.offsetDeltas) deltas.append(v);
        result.insert(QStringLiteral("offsetDeltas"), deltas);
        result.insert(QStringLiteral("featuresProjected"), static_cast<double>(rep.featuresProjected));
        result.insert(QStringLiteral("featuresRestored"), static_cast<double>(rep.featuresRestored));
        result.insert(QStringLiteral("featuresSkipped"), rep.featuresSkipped);
        result.insert(QStringLiteral("rowsMoved"), static_cast<double>(rep.rowsMoved));
        result.insert(QStringLiteral("classesMoved"), rep.classesMoved);
        result.insert(QStringLiteral("active"), static_cast<double>(doc->collisionRowsActive()));
        result.insert(QStringLiteral("stale"), doc->collisionRowsStale());
        QJsonArray records;
        for (const QJsonValue& v : o.value(QStringLiteral("rows")).toArray()) {
            std::vector<int16_t> served, raw;
            QJsonObject r;
            r.insert(QStringLiteral("row"), v.toInt());
            if (doc->spikeRecord(v.toInt(), served, raw)) {
                QJsonArray s, w;
                for (const int16_t x : served) s.append(x);
                for (const int16_t x : raw) w.append(x);
                r.insert(QStringLiteral("served"), s);
                r.insert(QStringLiteral("raw"), w);
                QJsonArray f;                               // the row's feature values in memory (without the time)
                for (const double x : doc->spikeFeatures(v.toInt())) f.append(x);
                r.insert(QStringLiteral("features"), f);
            }
            records.append(r);
        }
        result.insert(QStringLiteral("records"), records);
        if (o.contains(QStringLiteral("writeSpkc"))) {
            QString why;
            const bool ok = doc->writeSpkc(o.value(QStringLiteral("writeSpkc")).toString(), &why);
            result.insert(QStringLiteral("spkcWritten"), ok);
            if (!ok) result.insert(QStringLiteral("spkcError"), why);
        }
        return true;
    }
    if (step.contains(QStringLiteral("residuals"))) {
        // {"residuals": {"show": bool, "rows": [..], "table": cluster, "tableSpikes": n}}:
        // Waveforms > Residual of Collision Rows.  "show" switches the display (omitted: only
        // rebuild); "rows" returns those rows' residual records with their offset, class, kind
        // and twin; "table" returns the waveform TABLE the view draws cluster `table` from, as
        // the first tableSpikes spikes in sample mode (all of them when there are fewer).
        const QJsonObject o = step.value(QStringLiteral("residuals")).toObject();
        const KlustersDoc::ResidualReport R = o.contains(QStringLiteral("show"))
            ? doc->showResiduals(o.value(QStringLiteral("show")).toBool()) : doc->rebuildResiduals();
        result.insert(QStringLiteral("ok"), R.ok);
        result.insert(QStringLiteral("reason"), R.reason);
        result.insert(QStringLiteral("wanted"), static_cast<double>(R.wanted));
        result.insert(QStringLiteral("computed"), static_cast<double>(R.computed));
        result.insert(QStringLiteral("kept"), static_cast<double>(R.kept));
        result.insert(QStringLiteral("dropped"), static_cast<double>(R.dropped));
        result.insert(QStringLiteral("withoutNeighbours"), static_cast<double>(R.withoutNeighbours));
        result.insert(QStringLiteral("outOfRecording"), static_cast<double>(R.outOfRecording));
        result.insert(QStringLiteral("recovered"), static_cast<double>(R.recovered));
        result.insert(QStringLiteral("detected"), static_cast<double>(R.detected));
        result.insert(QStringLiteral("repeated"), static_cast<double>(R.repeated));
        result.insert(QStringLiteral("fromRecording"), R.fromRecording);
        result.insert(QStringLiteral("source"), R.source);
        result.insert(QStringLiteral("edgeOfResiduals"), R.edgeOfResiduals);
        result.insert(QStringLiteral("edgeOfRecords"), R.edgeOfRecords);
        result.insert(QStringLiteral("count"), static_cast<double>(doc->residualCount()));
        result.insert(QStringLiteral("shown"), doc->residualsShown());
        QJsonArray records;
        for (const QJsonValue& v : o.value(QStringLiteral("rows")).toArray()) {
            std::vector<int16_t> rec;
            int offset = 0, cls = -1, kind = 0; qint64 twin = -1;
            QJsonObject j;
            j.insert(QStringLiteral("row"), v.toInt());
            const bool has = doc->residualRecord(v.toInt(), rec, &offset, &cls, &kind, &twin);
            j.insert(QStringLiteral("has"), has);
            if (has) {
                QJsonArray values;
                for (const int16_t x : rec) values.append(static_cast<int>(x));
                j.insert(QStringLiteral("record"), values);
                j.insert(QStringLiteral("offset"), offset);
                j.insert(QStringLiteral("class"), cls);
                j.insert(QStringLiteral("kind"), kind);
                j.insert(QStringLiteral("twin"), static_cast<double>(twin));
            }
            records.append(j);
        }
        result.insert(QStringLiteral("records"), records);
        if (o.contains(QStringLiteral("table"))) {
            Data& data = doc->data();
            const int cluster = o.value(QStringLiteral("table")).toInt();
            const dataType n = static_cast<dataType>(o.value(QStringLiteral("tableSpikes")).toInt(100000));
            data.loadSampleWaveformTable(cluster, n);
            std::unique_ptr<Data::WaveformIterator> it(data.sampleWaveformIterator(static_cast<dataType>(cluster), n));
            QJsonArray table;
            if (it && it->areSpikesAvailable()) {
                const qint64 values = static_cast<qint64>(it->nbOfSpikes()) * data.nbSamplesPerWaveform() * data.nbOfChannels();
                for (qint64 i = 0; i < values; ++i) table.append(static_cast<int>(it->nextSpike()));
                result.insert(QStringLiteral("tableSpikes"), static_cast<double>(it->nbOfSpikes()));
            }
            result.insert(QStringLiteral("tableValues"), table);
        }
        return R.ok;
    }
    if (step.contains(QStringLiteral("collisionChild")) || step.contains(QStringLiteral("unmarkCollisions"))) {
        // {"collisionChild": {"clusters": [..], "children": bool}}: Clusters > Collision Rows to a Child.
        // {"unmarkCollisions": {"clusters": [..], "children": bool}}: Clusters > Remove Collision Marks,
        // without its dialog.  Both report "rows", the rows of the selection naming a partner before.
        const bool toChild = step.contains(QStringLiteral("collisionChild"));
        const QJsonObject o = step.value(toChild ? QStringLiteral("collisionChild") : QStringLiteral("unmarkCollisions")).toObject();
        QList<int> ids;
        for (const QJsonValue& v : o.value(QStringLiteral("clusters")).toArray()) ids.append(v.toInt());
        const bool children = o.value(QStringLiteral("children")).toBool(false);
        if (children || toChild) {
            QString err;
            if (!doc->loadChildClustering(err) && children) {
                result.insert(QStringLiteral("error"), QStringLiteral("no child layer: ") + err);
                return false;
            }
        }
        if (children && o.contains(QStringLiteral("childOfRow"))) {       // a child named by a row it holds
            const int row = o.value(QStringLiteral("childOfRow")).toInt();
            for (const dataType id : doc->childClusterData().clusterIds())
                if (doc->childClusterData().clusterSpkIndices(static_cast<int>(id)).contains(row)) ids.append(static_cast<int>(id));
        }
        const std::vector<qint64> rows = doc->collisionRowsOf(ids, children);
        QJsonArray named;
        for (const qint64 r : rows) named.append(static_cast<double>(r));
        result.insert(QStringLiteral("rows"), named);
        if (toChild) {
            QString summary;
            QJsonArray made;
            QJsonObject parents, sizes;
            for (const int id : collisionRowsToChildren(ids, children, summary)) {
                made.append(id);
                parents.insert(QString::number(id), doc->parentOfChild(id));
                sizes.insert(QString::number(id), static_cast<double>(doc->childClusterData().nbOfSpikes(static_cast<dataType>(id))));
            }
            result.insert(QStringLiteral("created"), made);
            result.insert(QStringLiteral("createdParent"), parents);
            result.insert(QStringLiteral("createdSize"), sizes);
            result.insert(QStringLiteral("summary"), summary);
        } else {
            result.insert(QStringLiteral("removed"), doc->removeCollisionMarks(rows));
        }
        result.insert(QStringLiteral("modified"), doc->isModified());
        return true;
    }
    if (step.contains(QStringLiteral("markCollisions"))) {
        // {"markCollisions": {"clusters": [..], "children": bool, "falseRate": 0.01, "apply": bool}}:
        // Clusters > Mark Collisions without its dialog -- the scan, and with "apply" the marks.
        // "childOfRow": r (with "children") adds the child that holds row r: a child's id is
        // given on load, so a test names it by a row it knows.
        const QJsonObject o = step.value(QStringLiteral("markCollisions")).toObject();
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
        if (children && o.contains(QStringLiteral("childOfRow"))) {
            const int row = o.value(QStringLiteral("childOfRow")).toInt();
            for (const dataType id : doc->childClusterData().clusterIds())
                if (doc->childClusterData().clusterSpkIndices(static_cast<int>(id)).contains(row)) ids.append(static_cast<int>(id));
        }
        QJsonArray searched;
        for (const int id : ids) searched.append(id);
        result.insert(QStringLiteral("searched"), searched);
        // "presenceSeconds" / "stretchSeconds": KlustersDoc::CollisionScanOptions.
        KlustersDoc::CollisionScanOptions how;
        how.falseRate = o.value(QStringLiteral("falseRate")).toDouble(how.falseRate);
        how.presenceSeconds = o.value(QStringLiteral("presenceSeconds")).toDouble(how.presenceSeconds);
        how.stretchSeconds = o.value(QStringLiteral("stretchSeconds")).toDouble(how.stretchSeconds);
        const KlustersDoc::CollisionScan scan = doc->scanCollisions(ids, children, how);
        result.insert(QStringLiteral("notPresent"), static_cast<double>(scan.notPresent));
        QJsonObject stretches;
        for (auto it = scan.stretchesOf.constBegin(); it != scan.stretchesOf.constEnd(); ++it)
            stretches.insert(QString::number(it.key()), it.value());
        result.insert(QStringLiteral("stretches"), stretches);
        QJsonObject chance;
        for (auto it = scan.expectedOf.constBegin(); it != scan.expectedOf.constEnd(); ++it)
            chance.insert(QString::number(it.key()), it.value());
        result.insert(QStringLiteral("expected"), chance);
        result.insert(QStringLiteral("ok"), scan.ok);
        result.insert(QStringLiteral("reason"), scan.reason);
        result.insert(QStringLiteral("scanned"), static_cast<double>(scan.scanned));
        result.insert(QStringLiteral("alreadyNamed"), static_cast<double>(scan.alreadyNamed));
        result.insert(QStringLiteral("candidates"), static_cast<double>(scan.candidates));
        result.insert(QStringLiteral("controlOver"), static_cast<double>(scan.controlOver));
        result.insert(QStringLiteral("threshold"), scan.threshold);
        result.insert(QStringLiteral("partnerUnits"), scan.partnerUnits);
        result.insert(QStringLiteral("milliseconds"), static_cast<double>(scan.milliseconds));
        QJsonArray hits;
        for (const KlustersDoc::CollisionHit& h : scan.hits) {
            QJsonObject j;
            j.insert(QStringLiteral("row"), static_cast<double>(h.row));
            j.insert(QStringLiteral("cluster"), h.cluster);
            j.insert(QStringLiteral("partner"), h.partner);
            j.insert(QStringLiteral("tau"), h.tau);
            j.insert(QStringLiteral("amp"), h.amp);
            hits.append(j);
        }
        result.insert(QStringLiteral("hits"), hits);
        QJsonObject per;
        for (auto it = scan.rowsOf.constBegin(); it != scan.rowsOf.constEnd(); ++it) {
            QJsonObject j;
            j.insert(QStringLiteral("rows"), static_cast<double>(it.value()));
            j.insert(QStringLiteral("hits"), static_cast<double>(scan.hitsOf.value(it.key())));
            j.insert(QStringLiteral("control"), static_cast<double>(scan.controlOf.value(it.key())));
            per.insert(QString::number(it.key()), j);
        }
        result.insert(QStringLiteral("perCluster"), per);
        QJsonObject alike;
        for (auto it = scan.lookAlikes.constBegin(); it != scan.lookAlikes.constEnd(); ++it) {
            QJsonArray a;
            for (const int c : it.value()) a.append(c);
            alike.insert(QString::number(it.key()), a);
        }
        result.insert(QStringLiteral("lookAlikes"), alike);
        if (scan.ok && o.value(QStringLiteral("apply")).toBool(false)) {
            const KlustersDoc::CollisionMarks m = doc->markCollisions(scan.hits);
            result.insert(QStringLiteral("markOk"), m.ok);
            result.insert(QStringLiteral("markError"), m.error);
            result.insert(QStringLiteral("marked"), m.marked);
            result.insert(QStringLiteral("classesCreated"), m.classesCreated);
            result.insert(QStringLiteral("strayRows"), m.strayRows);
            result.insert(QStringLiteral("withoutClass"), m.withoutClass);
        }
        result.insert(QStringLiteral("pending"), static_cast<double>(doc->pendingCollisionMarkCount()));
        result.insert(QStringLiteral("modified"), doc->isModified());
        return scan.ok;
    }
    if (step.contains(QStringLiteral("waveformQuality"))) {
        // {"waveformQuality": {...}}: the quality dots of every waveform view of the active display.
        //   "show": [ids]      shows those clusters first ("children": true reads them as children);
        //   "nbSpikes": n      the number of spikes the views hold per cluster (the toolbar's spin box);
        //   "overlay": bool    overlaid / side by side;
        //   "prefs": {"enabled", "yellow", "orange", "red", "diameter"}  Preferences > Waveform view;
        //   "saveTo": path     the grabbed view;   "windowSize": [w, h]  the main window is resized first.
        // Per view: its size and, per dot, the cluster, the value, the spikes it was computed on,
        // the rectangle, the colour the value maps to and the colour of the pixel at the dot's centre.
        if (!activeView()) { result.insert(QStringLiteral("error"), QStringLiteral("no display")); return false; }
        const QJsonObject o = step.value(QStringLiteral("waveformQuality")).toObject();
        if (o.contains(QStringLiteral("windowSize"))) {          // the main window, for a grab at a size a curator works at
            const QJsonArray sz = o.value(QStringLiteral("windowSize")).toArray();
            resize(sz.at(0).toInt(), sz.at(1).toInt());
            QCoreApplication::processEvents();
        }
        if (o.contains(QStringLiteral("prefs"))) {
            const QJsonObject pr = o.value(QStringLiteral("prefs")).toObject();
            Configuration::WaveformQuality q = configuration().getWaveformQuality();
            if (pr.contains(QStringLiteral("enabled")))  q.enabled    = pr.value(QStringLiteral("enabled")).toBool();
            if (pr.contains(QStringLiteral("yellow")))   q.yellow     = pr.value(QStringLiteral("yellow")).toDouble();
            if (pr.contains(QStringLiteral("orange")))   q.orange     = pr.value(QStringLiteral("orange")).toDouble();
            if (pr.contains(QStringLiteral("red")))      q.red        = pr.value(QStringLiteral("red")).toDouble();
            if (pr.contains(QStringLiteral("diameter"))) q.diameterPx = pr.value(QStringLiteral("diameter")).toInt();
            configuration().setWaveformQuality(q);
            applyHoverSpikePreference();          // repaints every waveform view, as Apply does
        }
        QList<int> ids;
        if (o.contains(QStringLiteral("show"))) {
            for (const QJsonValue& v : o.value(QStringLiteral("show")).toArray()) ids.append(v.toInt());
            const bool children = o.value(QStringLiteral("children")).toBool();
            if (children) {
                QString err;
                if (!doc->loadChildClustering(err)) { result.insert(QStringLiteral("error"), QStringLiteral("no child layer: ") + err); return false; }
            }
            doc->setActiveClustering(children);
            doc->shownClustersUpdate(ids, *activeView());
        } else {
            ids = activeView()->clusters();
        }
        QList<WaveformView*> wvs;
        for (ViewWidget* w : activeView()->getViewList())
            if (WaveformView* wv = qobject_cast<WaveformView*>(w)) wvs.append(wv);
        for (WaveformView* wv : std::as_const(wvs)) {
            if (o.contains(QStringLiteral("nbSpikes"))) wv->setDisplayNbSpikes(o.value(QStringLiteral("nbSpikes")).toInt());
            if (o.contains(QStringLiteral("overlay"))) {
                if (o.value(QStringLiteral("overlay")).toBool()) wv->setOverLayPresentation(); else wv->setSideBySidePresentation();
            }
        }
        // The spikes and then their statistics arrive from the waveform jobs.
        const bool expectDots = configuration().getWaveformQuality().enabled;
        QElapsedTimer waited; waited.start();
        for (;;) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            bool ready = true;
            for (WaveformView* wv : std::as_const(wvs)) {
                wv->repaint();
                if (wv->isThreadsRunning() || (expectDots && wv->qualityDots().size() != ids.size())) ready = false;
            }
            if (ready || waited.elapsed() > 15000) break;
        }
        QJsonArray views;
        for (WaveformView* wv : std::as_const(wvs)) {
            wv->repaint();
            const QImage img = wv->grab().toImage();
            QJsonObject v;
            v.insert(QStringLiteral("width"), img.width());
            v.insert(QStringLiteral("height"), img.height());
            v.insert(QStringLiteral("background"), wv->palette().color(wv->backgroundRole()).name());
            QJsonArray dots;
            for (const WaveformView::QualityDot& d : wv->qualityDots()) {
                QJsonObject j;
                j.insert(QStringLiteral("cluster"), d.cluster);
                if (std::isfinite(d.value)) j.insert(QStringLiteral("value"), d.value);
                j.insert(QStringLiteral("nSpikes"), static_cast<double>(d.nSpikes));
                j.insert(QStringLiteral("rect"), QJsonArray{ d.rect.x(), d.rect.y(), d.rect.width(), d.rect.height() });
                const QColor c = WaveformView::qualityColor(d.value);
                if (c.isValid()) j.insert(QStringLiteral("colour"), c.name());
                j.insert(QStringLiteral("centrePixel"), QColor(img.pixel(d.rect.center())).name());
                // How much of the dot's bounding square is in the cluster's own colour: a perimeter
                // drawn in it would be most of a circle's worth of pixels.
                const QRgb own = doc->clusterColors().color(d.cluster).rgb();
                int ownPixels = 0;
                for (int y = d.rect.top(); y <= d.rect.bottom(); ++y)
                    for (int x = d.rect.left(); x <= d.rect.right(); ++x)
                        if (img.valid(x, y) && (img.pixel(x, y) & 0x00ffffff) == (own & 0x00ffffff)) ++ownPixels;
                j.insert(QStringLiteral("clusterColour"), QColor(own).name());
                j.insert(QStringLiteral("clusterColourPixels"), ownPixels);
                // ... and how much of it is plain white: the empty circle of a cluster without a value.
                int whitePixels = 0;
                for (int y = d.rect.top(); y <= d.rect.bottom(); ++y)
                    for (int x = d.rect.left(); x <= d.rect.right(); ++x)
                        if (img.valid(x, y) && (img.pixel(x, y) & 0x00ffffff) == 0x00ffffff) ++whitePixels;
                j.insert(QStringLiteral("whitePixels"), whitePixels);
                dots.append(j);
            }
            v.insert(QStringLiteral("dots"), dots);
            if (o.contains(QStringLiteral("saveTo"))) img.save(o.value(QStringLiteral("saveTo")).toString());
            views.append(v);
        }
        result.insert(QStringLiteral("waveformViews"), views);
        return true;
    }
    if (step.contains(QStringLiteral("moveToEnd"))) {
        // {"moveToEnd": true}: the T key on the current parent-palette selection.
        slotMoveSelectedClustersToEnd();
        return true;
    }
    if (step.contains(QStringLiteral("undo"))) {
        // {"undo": true}: Edit > Undo.
        slotUndo();
        result.insert(QStringLiteral("undoSteps"), doc->undoStepCount());
        result.insert(QStringLiteral("redoSteps"), doc->redoStepCount());
        result.insert(QStringLiteral("status"), statusBar()->currentMessage());
        return true;
    }
    if (step.contains(QStringLiteral("redo"))) {
        // {"redo": true}: Edit > Redo.
        slotRedo();
        result.insert(QStringLiteral("undoSteps"), doc->undoStepCount());
        result.insert(QStringLiteral("redoSteps"), doc->redoStepCount());
        result.insert(QStringLiteral("status"), statusBar()->currentMessage());
        return true;
    }
    if (step.contains(QStringLiteral("undoState"))) {
        // {"undoState": true}: how many steps Undo and Redo can take now, and the status line.
        result.insert(QStringLiteral("undoSteps"), doc->undoStepCount());
        result.insert(QStringLiteral("redoSteps"), doc->redoStepCount());
        result.insert(QStringLiteral("status"), statusBar()->currentMessage());
        return true;
    }
    if (step.contains(QStringLiteral("alignAll"))) {
        // {"alignAll": true}: Spikes > Align All Clusters, its question answered yes.
        QTimer::singleShot(0, this, []() {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if (QAbstractButton* yes = box->button(QMessageBox::Yes)) yes->click();
        });
        slotPcaAlignAllClusters();
        result.insert(QStringLiteral("started"), realignRunning || realignBatchActive);
        return true;
    }
    if (step.contains(QStringLiteral("purge"))) {
        // {"purge": n}: Clusters > Purge Small Clusters with n spikes, both its boxes answered yes.
        // Reports the ids it purged and the layer shown before and after.
        result.insert(QStringLiteral("childScopeBefore"), doc->isChildClusteringActive());
        QJsonArray a;
        for (int x : purgeSmallClusters(step.value(QStringLiteral("purge")).toInt(), false)) a.append(x);
        result.insert(QStringLiteral("purged"), a);
        result.insert(QStringLiteral("childScope"), doc->isChildClusteringActive());
        result.insert(QStringLiteral("undoSteps"), doc->undoStepCount());
        return true;
    }
    if (step.contains(QStringLiteral("childPalette"))) {
        // {"childPalette": {"show": bool, "click": id}}: View > Show Child Cluster Palette, and a
        // left click on a child in it -- the curator's way into the child view.
        const QJsonObject o = step.value(QStringLiteral("childPalette")).toObject();
        if (o.contains(QStringLiteral("show")) && mShowChildPalette)
            mShowChildPalette->setChecked(o.value(QStringLiteral("show")).toBool());
        QCoreApplication::processEvents();
        if (o.contains(QStringLiteral("click")))
            result.insert(QStringLiteral("clicked"), childPalette && childPalette->clickItem(o.value(QStringLiteral("click")).toInt()));
        QCoreApplication::processEvents();
        QJsonArray listed, sel;
        if (childPalette) {
            for (int id : childPalette->listedClusters()) listed.append(id);
            for (int id : childPalette->selectedClusters()) sel.append(id);
        }
        result.insert(QStringLiteral("visible"), childPanel && childPanel->isVisible());
        result.insert(QStringLiteral("listed"), listed);
        result.insert(QStringLiteral("childSelection"), sel);
        result.insert(QStringLiteral("childScope"), doc->isChildClusteringActive());
        return true;
    }
    if (step.contains(QStringLiteral("matrixView"))) {
        // {"matrixView": {"which": "error" | "template" | "residual" | "drift", "open": true,
        //   "size": [w, h], "update": true, "do": [actions], "saveTo": path}}: see selfTestDriveMatrix.
        const QJsonObject o = step.value(QStringLiteral("matrixView")).toObject();
        const QString which = o.value(QStringLiteral("which")).toString();
        if (o.value(QStringLiteral("open")).toBool()) {
            if (which == QLatin1String("residual"))   slotNewResidualMatrix();
            else if (which == QLatin1String("drift")) slotNewDriftMatrix();
            else if (!activeView() || !activeView()->findChild<ErrorMatrixView*>()) slotWindowNewGroupingAssistantDisplay();
            QCoreApplication::processEvents();
        }
        KlustersView* display = activeView();
        bool found = false;
        if (display) {
            if (which == QLatin1String("error")) {
                if (auto* v = display->findChild<ErrorMatrixView*>()) { found = true; selfTestDriveMatrix(v, o, result, display); }
            } else if (which == QLatin1String("template")) {
                if (auto* v = display->findChild<TemplateMatrixView*>()) { found = true; selfTestDriveMatrix(v, o, result, display); }
            } else if (which == QLatin1String("residual")) {
                if (auto* v = display->findChild<ResidualMatrixView*>()) { found = true; selfTestDriveMatrix(v, o, result, display); }
            } else if (which == QLatin1String("drift")) {
                if (auto* v = display->findChild<DriftMatrixView*>()) { found = true; selfTestDriveMatrix(v, o, result, display); }
            }
        }
        result.insert(QStringLiteral("found"), found);
        return found;
    }
    if (step.contains(QStringLiteral("matrixSelect"))) {
        // {"matrixSelect": [ids], "add": bool}: what a click (Ctrl-click) on a cell of a curation
        // matrix does -- the document call every matrix view makes.  Reports the layer shown, what
        // the view shows and both palettes' selections afterwards.
        QList<int> ids;
        for (const QJsonValue& v : step.value(QStringLiteral("matrixSelect")).toArray()) ids.append(v.toInt());
        auto arr = [](const QList<int>& l) { QJsonArray a; for (int x : l) a.append(x); return a; };
        result.insert(QStringLiteral("childScopeBefore"), doc->isChildClusteringActive());
        result.insert(QStringLiteral("parentSelectionBefore"), arr(clusterPalette ? clusterPalette->selectedClusters() : QList<int>()));
        result.insert(QStringLiteral("childSelectionBefore"), arr(childPalette ? childPalette->selectedClusters() : QList<int>()));
        if (step.value(QStringLiteral("add")).toBool()) doc->addFromMatrix(ids);
        else                                            doc->selectFromMatrix(ids);
        QCoreApplication::processEvents();
        result.insert(QStringLiteral("childScope"), doc->isChildClusteringActive());
        result.insert(QStringLiteral("matrixScope"), doc->matrixScopeActive());
        result.insert(QStringLiteral("shown"), arr(activeView() ? activeView()->clusters() : QList<int>()));
        result.insert(QStringLiteral("parentSelection"), arr(clusterPalette ? clusterPalette->selectedClusters() : QList<int>()));
        result.insert(QStringLiteral("childSelection"), arr(childPalette ? childPalette->selectedClusters() : QList<int>()));
        return true;
    }
    if (step.contains(QStringLiteral("keyCommand"))) {
        // {"keyCommand": "<id>", "paletteSelect": [ids]}: run a command of the binding registry as its key would (with
        // "whileBusy": true, during a job).  Reports whether a job blocked editing, whether the
        // command was enabled, and whether the parent cluster ids changed.
        const QString id = step.value(QStringLiteral("keyCommand")).toString();
        const input::Command* c = input::registry().command(id);
        auto ids = [this] {
            QJsonArray a;
            if (doc) for (dataType i : doc->parentData().clusterIds()) a.append(static_cast<int>(i));
            return a;
        };
        if (step.contains(QStringLiteral("paletteSelect")) && clusterPalette) {   // what the key acts on
            QList<int> sel;
            for (const QJsonValue& v : step.value(QStringLiteral("paletteSelect")).toArray()) sel.append(v.toInt());
            clusterPalette->selectItems(sel);
        }
        const QJsonArray before = ids();
        const bool blocked = editingBlocked();
        input::Ctx ctx;
        const bool on = c && (!c->enabled || c->enabled(ctx));
        if (on && c->invoke) c->invoke(ctx);
        QApplication::processEvents();
        result.insert(QStringLiteral("keyCommand"), id);
        result.insert(QStringLiteral("known"), c != nullptr);
        result.insert(QStringLiteral("blocked"), blocked);
        result.insert(QStringLiteral("commandEnabled"), on);
        result.insert(QStringLiteral("idsChanged"), before != ids());
        return true;
    }
    if (step.contains(QStringLiteral("cancelJob"))) {
        // {"cancelJob": true}: Edit > Cancel Running Job (with "whileBusy": true to reach a job).
        // {"cancelJob": {"when": "realign"}}: first let events run until a job of that kind
        // ("process", "plugin", "realign") is the cancellable one, for 60 s at most.
        QString what;
        const QString when = step.value(QStringLiteral("cancelJob")).toObject().value(QStringLiteral("when")).toString();
        if (!when.isEmpty()) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < 60000 && !(cancellableJobRuns(&what) && what == when))
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            what.clear();
        }
        result.insert(QStringLiteral("cancellable"), cancellableJobRuns(&what));
        result.insert(QStringLiteral("job"), what);
        result.insert(QStringLiteral("actionEnabled"), mCancelJob && mCancelJob->isEnabled());
        cancelRunningJob();
        return true;
    }
    if (step.contains(QStringLiteral("commandDump"))) {
        // {"commandDump": true}: every command of the menus, in menu order, with what a
        // curator sees of it (menu, text, keys, tips, checkable, whether it is on), and the
        // binding registry's application commands in their order.  test_command_table.py
        // compares it with a stored snapshot: a change of the menus is a change of that file.
        collectEditLockActions();                       // so that the lock's set is known when idle
        QJsonArray cmds;
        for (const ActionCatalogEntry& e : actionCatalog()) {
            const QAction* a = e.action;
            QJsonObject o;
            QStringList keys;
            for (const QKeySequence& k : a->shortcuts()) keys << k.toString(QKeySequence::PortableText);
            o.insert(QStringLiteral("id"), e.id);
            o.insert(QStringLiteral("menu"), e.path.join(QLatin1Char('/')));
            o.insert(QStringLiteral("text"), a->text());
            o.insert(QStringLiteral("keys"), keys.join(QStringLiteral(" | ")));
            o.insert(QStringLiteral("statusTip"), a->statusTip());
            // A tool tip nobody set is the text without its mnemonic; which trailing ellipsis Qt
            // drops from it depends on the Qt version ("..." always, "…" from some release after
            // 6.4 on), so both sides are compared without one.
            const auto noEllipsis = [](QString s) {
                s = s.trimmed();
                if (s.endsWith(QChar(0x2026))) s.chop(1);
                else if (s.endsWith(QStringLiteral("..."))) s.chop(3);
                return s.trimmed();
            };
            const bool tipIsTheText = noEllipsis(a->toolTip()) == noEllipsis(actionid::plainText(a->text()));
            o.insert(QStringLiteral("toolTip"), tipIsTheText ? QString() : a->toolTip());
            o.insert(QStringLiteral("checkable"), a->isCheckable());
            o.insert(QStringLiteral("checked"), a->isChecked());
            o.insert(QStringLiteral("enabled"), a->isEnabled());
            o.insert(QStringLiteral("visible"), a->isVisible());
            o.insert(QStringLiteral("group"), a->actionGroup() != nullptr);
            o.insert(QStringLiteral("editLocked"), editLockDesired.contains(const_cast<QAction*>(a)));
            cmds.append(o);
        }
        QJsonArray reg;
        for (const input::Command& c : input::registry().commands()) {
            if (!c.external) continue;                  // the mirrors of the menu actions
            QJsonObject o;
            o.insert(QStringLiteral("id"), c.id);
            o.insert(QStringLiteral("category"), c.category);
            o.insert(QStringLiteral("label"), c.label);
            o.insert(QStringLiteral("default"), c.defaultChord.toStored());
            reg.append(o);
        }
        QJsonArray locked;
        for (const ActionCatalogEntry& e : actionCatalog())
            if (editLockDesired.contains(e.action)) locked.append(e.id);
        result.insert(QStringLiteral("commands"), cmds);
        result.insert(QStringLiteral("registry"), reg);
        result.insert(QStringLiteral("replacedIds"), QJsonArray::fromStringList(input::registry().replacedIds()));
        result.insert(QStringLiteral("editLocked"), locked);
        return true;
    }
    if (step.contains(QStringLiteral("actionsEnabled"))) {
        // {"actionsEnabled": true}: which commands can be triggered now, and which jobs run.
        QJsonArray on, off;
        const QHash<QString, QAction*> acts = actionsById();
        QStringList ids = acts.keys();
        ids.sort();
        for (const QString& id : ids) (acts.value(id)->isEnabled() ? on : off).append(id);
        result.insert(QStringLiteral("enabled"), on);
        result.insert(QStringLiteral("disabled"), off);
        result.insert(QStringLiteral("realignRunning"), realignRunning || realignBatchActive || (realignQueue && realignQueue->isBusy()));
        result.insert(QStringLiteral("processRunning"), !(processFinished && processOutputsFinished));
        result.insert(QStringLiteral("saveRunning"), saveThread && saveThread->isRunning());
        return true;
    }
    if (step.contains(QStringLiteral("annotate"))) {
        // {"annotate": {"cluster": id, "quality": "..", "notes": "..", "child": false}}: what the
        // palette's information dialog does on OK ("quality" / "notes" absent: only read).
        // Reports the cluster's quality and notes afterwards, and the undo steps.
        const QJsonObject o = step.value(QStringLiteral("annotate")).toObject();
        const bool childLayer = o.value(QStringLiteral("child")).toBool();
        const int id = o.value(QStringLiteral("cluster")).toInt();
        Data* layer = childLayer ? (doc->hasChildClustering() ? &doc->childClusterData() : nullptr) : &doc->parentData();
        QList<QString> fields;
        if (layer) layer->getUserClusterInformation(id, fields);
        UnitAnnotation a = UnitAnnotation::fromFields(fields);
        if (o.contains(QStringLiteral("quality")) || o.contains(QStringLiteral("notes"))) {
            if (o.contains(QStringLiteral("quality"))) a.quality = o.value(QStringLiteral("quality")).toString();
            if (o.contains(QStringLiteral("notes")))   a.notes   = o.value(QStringLiteral("notes")).toString();
            result.insert(QStringLiteral("changed"), doc->setClusterAnnotation(childLayer, id, a));
            fields.clear();
            if (layer) layer->getUserClusterInformation(id, fields);
            a = UnitAnnotation::fromFields(fields);
        }
        result.insert(QStringLiteral("quality"), a.quality);
        result.insert(QStringLiteral("notes"), a.notes);
        result.insert(QStringLiteral("undoSteps"), doc->undoStepCount());
        result.insert(QStringLiteral("redoSteps"), doc->redoStepCount());
        return true;
    }
    if (step.contains(QStringLiteral("labels"))) {
        // {"labels": true}: the parent cluster of every spike row, in row order.
        const QVector<dataType> labels = doc->parentData().labelByFeatureRow();
        QJsonArray out;
        for (int r = 1; r < labels.size(); ++r) out.append(static_cast<int>(labels[r]));
        result.insert(QStringLiteral("labels"), out);
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
    if (step.contains(QStringLiteral("burstFollowers"))) {
        // {"burstFollowers": [a, b]}: Split > Split Burst Followers on the two clusters.
        const QJsonArray ids = step.value(QStringLiteral("burstFollowers")).toArray();
        // "children": true reads the ids as children (the .clc layer), as the action
        // does in child scope.
        const bool children = step.value(QStringLiteral("children")).toBool(false);
        if (children) {
            QString err;
            if (!doc->loadChildClustering(err)) {
                result.insert(QStringLiteral("error"), QStringLiteral("no child layer: ") + err);
                return false;
            }
        }
        doc->setActiveClustering(children);
        // With "mixed": c, the three-cluster form: a and b sort the spikes of c.
        QString summary;
        const QList<int> created = step.contains(QStringLiteral("mixed"))
            ? sortMixedByBursts(ids.at(0).toInt(), ids.at(1).toInt(), step.value(QStringLiteral("mixed")).toInt(), summary, children)
            : splitBurstFollowers(ids.at(0).toInt(), ids.at(1).toInt(), summary, children);
        QJsonArray made;
        QJsonObject parents;
        for (const int id : created) {
            made.append(id);
            if (children) parents.insert(QString::number(id), doc->parentOfChild(id));
        }
        if (children) result.insert(QStringLiteral("createdParent"), parents);
        result.insert(QStringLiteral("created"), made);
        result.insert(QStringLiteral("summary"), summary);
        return true;
    }
    if (step.contains(QStringLiteral("configWrite"))) {
        // {"configWrite": true}: store the configuration as read at start-up (Configuration::write),
        // for test_settings_file.py: what a settings file becomes by being read and written.
        // With "defaults": true, first Preferences > Defaults and OK: what that button stores.
        // With "dialog": true, Preferences opened and OK pressed with nothing changed.
        if (step.value(QStringLiteral("defaults")).toBool() || step.value(QStringLiteral("dialog")).toBool()) {
            if (mPluginRegistry.plugins().isEmpty()) mPluginRegistry.reload();
            PrefDialog dialog(this, (mainDock && doc) ? doc->nbOfchannels() : 0, &mPluginRegistry);
            dialog.setRadialCatalog(actionCatalog());
            dialog.updateDialog();
            if (step.value(QStringLiteral("defaults")).toBool()) dialog.restoreDefaults();
            dialog.updateConfiguration();
            result.insert(QStringLiteral("unboundPreferenceRows"), QJsonArray::fromStringList(dialog.unboundPreferenceRows()));
        }
        configuration().write();
        QSettings().sync();
        return true;
    }
    if (step.contains(QStringLiteral("prefKeys"))) {
        // {"prefKeys": ["Down", "Return", "Tab", "Space", "Escape", ...]}: Preferences opened and
        // worked from the keyboard.  After each key: the page shown, where the focus is ("list",
        // "page", "buttons"), the setting focused and, for a check box, whether it is checked.
        if (mPluginRegistry.plugins().isEmpty()) mPluginRegistry.reload();
        PrefDialog dialog(this, (mainDock && doc) ? doc->nbOfchannels() : 0, &mPluginRegistry);
        dialog.setRadialCatalog(actionCatalog());
        dialog.updateDialog();
        dialog.show();
        QApplication::processEvents();
        PageKeyNavigator* nav = dialog.findChild<PageKeyNavigator*>();
        QJsonArray trace;
        auto state = [&](const QString& key) {
            QJsonObject o;
            QWidget* f = dialog.focusWidget();
            o.insert(QStringLiteral("key"), key);
            o.insert(QStringLiteral("page"), dialog.currentPage() ? dialog.currentPage()->name() : QString());
            o.insert(QStringLiteral("level"), !f ? QStringLiteral("none")
                     : (nav && nav->inPage(f)) ? QStringLiteral("page")
                     : (nav && nav->pageList() && (f == nav->pageList() || nav->pageList()->isAncestorOf(f))) ? QStringLiteral("list")
                     : QStringLiteral("buttons"));
            o.insert(QStringLiteral("focus"), f ? QString::fromLatin1(f->metaObject()->className()) + QLatin1Char(':') + f->objectName() : QString());
            if (const QAbstractButton* b = qobject_cast<const QAbstractButton*>(f)) {
                o.insert(QStringLiteral("text"), b->text());
                if (b->isCheckable()) o.insert(QStringLiteral("checked"), b->isChecked());
            }
            o.insert(QStringLiteral("stops"), nav ? static_cast<int>(nav->pageStops().size()) : -1);
            o.insert(QStringLiteral("open"), dialog.isVisible());
            trace.append(o);
        };
        state(QStringLiteral("(opened)"));
        for (const QJsonValue& v : step.value(QStringLiteral("prefKeys")).toArray()) {
            const QString name = v.toString();
            Qt::KeyboardModifiers mods = Qt::NoModifier;
            int key = 0;
            if (name == QLatin1String("Backtab")) { key = Qt::Key_Backtab; mods = Qt::ShiftModifier; }
            else key = QKeySequence(name)[0].key();
            QWidget* f = dialog.focusWidget();
            if (f && key) {
                const QString text = name == QLatin1String("Space") ? QStringLiteral(" ") : QString();
                QKeyEvent press(QEvent::KeyPress, key, mods, text);
                QApplication::sendEvent(f, &press);
                if (QWidget* f2 = dialog.focusWidget()) {
                    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
                    QApplication::sendEvent(f2, &release);
                }
            }
            {   // a dialog button is pressed by an animated click: let it land
                QEventLoop loop;
                QTimer::singleShot(160, &loop, &QEventLoop::quit);
                loop.exec();
            }
            state(name);
        }
        result.insert(QStringLiteral("navigator"), nav != nullptr);
        result.insert(QStringLiteral("trace"), trace);
        return true;
    }
    if (step.contains(QStringLiteral("prefPage"))) {
        const QString wanted = step.value(QStringLiteral("prefPage")).toString();
        if (mPluginRegistry.plugins().isEmpty()) mPluginRegistry.reload();
        PrefDialog dialog(this, (mainDock && doc) ? doc->nbOfchannels() : 0, &mPluginRegistry);
        dialog.setRadialCatalog(actionCatalog());
        dialog.updateDialog();
        QWidget* page = nullptr;
        const QList<QWidget*> all = dialog.findChildren<QWidget*>();
        for (QWidget* w : all)
            if (QString::fromLatin1(w->metaObject()->className()) == wanted) { page = w; break; }
        result.insert(QStringLiteral("found"), page != nullptr);
        if (!page) return true;
        QJsonArray groups;
        const QList<QGroupBox*> boxes = page->findChildren<QGroupBox*>();
        for (const QGroupBox* b : boxes) groups.append(b->title());
        result.insert(QStringLiteral("groups"), groups);
        const QString saveTo = step.value(QStringLiteral("saveTo")).toString();
        if (!saveTo.isEmpty()) {
            page->resize(page->sizeHint().expandedTo(QSize(640, 480)));
            result.insert(QStringLiteral("saved"), page->grab().save(saveTo));
        }
        // How much room the dialog needs, and whether the page can scroll when it gets less.
        bool scrolls = false;
        for (QWidget* w = page->parentWidget(); w && !scrolls; w = w->parentWidget())
            scrolls = qobject_cast<QScrollArea*>(w) != nullptr;
        result.insert(QStringLiteral("scrolls"), scrolls);
        result.insert(QStringLiteral("pageHeight"), page->sizeHint().height());
        result.insert(QStringLiteral("dialogMinHeight"), dialog.minimumSizeHint().height());
        const QJsonArray size = step.value(QStringLiteral("dialogSize")).toArray();
        const QString dialogTo = step.value(QStringLiteral("dialogSaveTo")).toString();
        if (size.size() == 2 && !dialogTo.isEmpty()) {
            // The dialog as it is on a screen of that size, this page showing.
            result.insert(QStringLiteral("pageShown"), dialog.showPage(page));
            dialog.setAttribute(Qt::WA_DontShowOnScreen);
            dialog.show();
            dialog.resize(size.at(0).toInt(), size.at(1).toInt());
            QCoreApplication::processEvents();
            result.insert(QStringLiteral("dialogHeight"), dialog.height());
            // The page's last group box, before and after scrolling to the end: where its
            // bottom edge is in the dialog, and the height the page is drawn at.
            QScrollArea* area = nullptr;
            for (QWidget* w = page->parentWidget(); w && !area; w = w->parentWidget()) area = qobject_cast<QScrollArea*>(w);
            const QGroupBox* last = boxes.isEmpty() ? nullptr : boxes.last();
            if (last) {
                result.insert(QStringLiteral("lastGroup"), last->title());
                result.insert(QStringLiteral("lastGroupBottom"), last->mapTo(&dialog, QPoint(0, last->height())).y());
            }
            result.insert(QStringLiteral("pageDrawnHeight"), page->height());
            if (area && step.value(QStringLiteral("scrollToEnd")).toBool()) {
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
                QCoreApplication::processEvents();
                result.insert(QStringLiteral("scrollRange"), area->verticalScrollBar()->maximum());
                if (last) result.insert(QStringLiteral("lastGroupBottomScrolled"), last->mapTo(&dialog, QPoint(0, last->height())).y());
            }
            result.insert(QStringLiteral("dialogSaved"), dialog.grab().save(dialogTo));
            dialog.hide();
        }
        if (step.value(QStringLiteral("apply")).toBool()) dialog.updateConfiguration();
        return true;
    }
    if (step.contains(QStringLiteral("hierarchy"))) {
        const QJsonObject o = step.value(QStringLiteral("hierarchy")).toObject();
        QString err;
        if (!doc->loadChildClustering(err)) {
            result.insert(QStringLiteral("error"), QStringLiteral("no child layer: ") + err);
            return false;
        }
        const auto ids = [&o](const char* key) {
            QList<int> out;
            const QJsonArray a = o.value(QLatin1String(key)).toArray();
            for (const QJsonValue& v : a) out << v.toInt();
            return out;
        };
        KlustersView* view = activeView();
        if (!view) { result.insert(QStringLiteral("error"), QStringLiteral("no active view")); return false; }
        if (o.contains(QStringLiteral("mergeParents")))    result.insert(QStringLiteral("returned"), doc->mergeParents(ids("mergeParents"), *view));
        if (o.contains(QStringLiteral("mergeChildren")))   result.insert(QStringLiteral("returned"), doc->mergeChildren(ids("mergeChildren"), *view));
        if (o.contains(QStringLiteral("promoteChildren"))) result.insert(QStringLiteral("returned"), doc->promoteChildren(ids("promoteChildren"), *view));
        if (o.contains(QStringLiteral("compact")))         result.insert(QStringLiteral("returned"), doc->compactAllClusterIds());
        if (o.contains(QStringLiteral("repairNesting")))   result.insert(QStringLiteral("returned"), doc->repairNestingAsStep());
        if (o.contains(QStringLiteral("dropChildren")))    result.insert(QStringLiteral("returned"), doc->dropChildrenToBin(ids("dropChildren"), 1, *view));
        if (o.contains(QStringLiteral("showChildren"))) {
            // What the child palette's selection does (slotChildSelectionChanged): the views
            // show those children, on the child layer; an empty list returns to the parents.
            const QList<int> kids = ids("showChildren");
            doc->setActiveClustering(!kids.isEmpty());
            doc->shownClustersUpdate(kids.isEmpty() ? clusterPalette->selectedClusters() : kids, *view);
        }
        if (o.value(QStringLiteral("flatten")).toBool())   result.insert(QStringLiteral("returned"), doc->flattenHierarchyToClu(*view));
        if (o.value(QStringLiteral("renumber")).toBool())  doc->renumberClusters();
        if (o.contains(QStringLiteral("toEnd")))           doc->renumberClustersToEnd(ids("toEnd"));
        if (o.contains(QStringLiteral("order")))           result.insert(QStringLiteral("returned"), doc->reorderClustersByPermutation(ids("order"), QStringLiteral("self-test")));
        if (o.value(QStringLiteral("undo")).toBool())      doc->undoDispatch();
        if (o.value(QStringLiteral("redo")).toBool())      doc->redoDispatch();
        result.insert(QStringLiteral("undoSteps"), doc->undoStepCount());
        result.insert(QStringLiteral("redoSteps"), doc->redoStepCount());
        if (o.value(QStringLiteral("undoChild")).toBool()) result.insert(QStringLiteral("returned"), doc->undoChildEditDispatch());
        if (o.value(QStringLiteral("redoChild")).toBool()) result.insert(QStringLiteral("returned"), doc->redoChildEditDispatch());
        QJsonObject parents, sizes, parentSizes;
        for (dataType id : doc->childClusterData().clusterIds()) {
            parents.insert(QString::number(id), doc->parentOfChild(static_cast<int>(id)));
            sizes.insert(QString::number(id), static_cast<qint64>(doc->childClusterData().nbOfSpikes(id)));
        }
        for (dataType id : doc->parentData().clusterIds())
            parentSizes.insert(QString::number(id), static_cast<qint64>(doc->parentData().nbOfSpikes(id)));
        result.insert(QStringLiteral("childParents"), parents);
        result.insert(QStringLiteral("childSizes"), sizes);
        result.insert(QStringLiteral("parentSizes"), parentSizes);
        result.insert(QStringLiteral("upgraded"), doc->childIdsUpgradedOnLoad());
        result.insert(QStringLiteral("childScope"), doc->isChildClusteringActive());
        result.insert(QStringLiteral("layerSwitches"), doc->layerSwitchCount());
        QJsonArray shown;
        if (activeView()) for (const int c : activeView()->clusters()) shown.append(c);
        result.insert(QStringLiteral("shown"), shown);
        return true;
    }
    if (step.contains(QStringLiteral("clickParent"))) {
        // A left click on that parent's item in the parent palette.
        if (!clusterPalette || !clusterPalette->clickItem(step.value(QStringLiteral("clickParent")).toInt())) {
            result.insert(QStringLiteral("error"), QStringLiteral("not in the parent palette"));
            return false;
        }
        return true;
    }
    if (step.contains(QStringLiteral("tabRefresh"))) {
        // The refresh every realign, recluster and tab switch ends with.
        if (!tabsParent) {
            result.insert(QStringLiteral("error"), QStringLiteral("no display"));
            return false;
        }
        slotTabChange(tabsParent->currentIndex());
        return true;
    }
    if (step.contains(QStringLiteral("coreCut"))) {
        // {"coreCut": [ids]}: Split > Split Off Spurious Spikes on each of the clusters.
        const QJsonArray ids = step.value(QStringLiteral("coreCut")).toArray();
        const bool children = step.value(QStringLiteral("children")).toBool(false);
        if (children) {
            QString err;
            if (!doc->loadChildClustering(err)) {
                result.insert(QStringLiteral("error"), QStringLiteral("no child layer: ") + err);
                return false;
            }
        }
        doc->setActiveClustering(children);
        const Configuration::CoreCut knobsBefore = configuration().getCoreCut();
        if (step.contains(QStringLiteral("onsetFactor"))) {
            Configuration::CoreCut knobs = configuration().getCoreCut();
            knobs.onsetFactor = step.value(QStringLiteral("onsetFactor")).toDouble();
            configuration().setCoreCut(knobs);
        }
        result.insert(QStringLiteral("onsetFactor"), configuration().getCoreCut().onsetFactor);
        result.insert(QStringLiteral("shortcut"), mSplitOffSpurious ? mSplitOffSpurious->shortcut().toString(QKeySequence::PortableText) : QString());
        QJsonArray made, summaries;
        QJsonObject parents;
        for (const QJsonValue& v : ids) {
            QString summary;
            const int id = splitOffSpurious(v.toInt(), summary, children);
            made.append(id);                                  // 0: nothing split off from this one
            summaries.append(summary);
            if (children && id > 0) parents.insert(QString::number(id), doc->parentOfChild(id));
        }
        configuration().setCoreCut(knobsBefore);              // the step's knob does not outlive it
        if (children) result.insert(QStringLiteral("createdParent"), parents);
        result.insert(QStringLiteral("created"), made);
        result.insert(QStringLiteral("summaries"), summaries);
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
        o.insert(QStringLiteral("held"), doc->featureSpaceHeld(s));
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
    r.insert(QStringLiteral("measuredCorrected"), doc ? doc->realignMeasuredCorrectedRows() : 0);
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
        rep.insert(QStringLiteral("nbUndo"), nbUndo);                 // the undo depth the run ended with
        rep.insert(QStringLiteral("realignThreshold"), configuration().getRealignThreshold());
        {   // the stored key bindings as read (after the migrations of Configuration::read)
            QJsonObject ov;
            const QMap<QString,QString> m = configuration().getInputBindingOverrides();
            for (auto it = m.constBegin(); it != m.constEnd(); ++it) ov.insert(it.key(), it.value());
            rep.insert(QStringLiteral("inputBindingOverrides"), ov);
        }
        QFile out(selfTestReport_);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(QJsonDocument(rep).toJson());
    }
    // Leave without the "save changes?" question: the files are what the steps wrote.
    if (doc) doc->setModified(false);
    QTimer::singleShot(0, qApp, [code]() { qApp->exit(code); });
}
