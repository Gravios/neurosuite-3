// curationlogger_test.cpp -- the curation log's checkpoint and its JSON writer.
//
// Checked:
//   * nothing is on disk after beginAction/commitAction (deferred flush);
//   * checkpoint() writes every buffered entry with "final":false and keeps
//     the ring (a later undo still flips the status), then close() writes the
//     same action again with "final":true and the status it ended with -- so
//     a reader keeping the last line per (action_idx, phase, cluster) sees the
//     decision, and a crash after a Save keeps everything saved before it;
//   * every line is valid JSON (QJsonDocument parses it), NaN becomes null,
//     and a QVariantList detail is written as a JSON array (it was "");
//   * SESSION_OPEN carries the context map (method, stage, versions);
//   * gestures: a GESTURE entry per hierarchy command, nested (Break Up runs
//     promoteChildren), every primitive begun inside carries gesture_idx, the
//     open gesture is not flushed by a trim, and an undo after the gesture flips
//     the primitive (undo-paired), never the gesture; the layer field.
// QtCore only, own main, run via ctest.

#include "curationlogger.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <cstdio>
#include <string>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) std::printf("FAIL: %s\n", what.c_str()); else std::printf("  ok: %s\n", what.c_str());
    g_fail += !ok;
}

static QList<QJsonObject> readLines(const QString& path)
{
    QList<QJsonObject> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError err;
        const QJsonDocument d = QJsonDocument::fromJson(line.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !d.isObject()) {
            std::printf("  bad JSON: %s\n  %s\n", qPrintable(err.errorString()), qPrintable(line.left(120)));
            out.append(QJsonObject{{"__bad__", true}});
            continue;
        }
        out.append(d.object());
    }
    return out;
}

static ClusterSnapshot snap(int id, double refrac = 0.0)
{
    ClusterSnapshot s;
    s.clusterId = id; s.nSpikes = 100 + id; s.firingRateHz = 2.0;
    s.refracRatio = refrac; s.refracExpected = 5.0; s.burstIndex = 3.5; s.occupiedRateHz = 2.5;
    s.waveformAvailable = true; s.waveformDomChan = 3; s.waveformWidthMs = 0.6;
    s.waveformChanAmp = {1.0, 2.0, 3.0, 8.0, 3.0, 2.0, 1.0, 0.5};
    return s;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath("rat.cur.stderiv.6.final");

    {
        CurationLogger log;
        log.open(path, "rat.clu.stderiv.6.final", "6", 32552.0, 8, 32,
                 {{"method", "stderiv"}, {"stage", "final"}, {"klusters_version", "3.2"}, {"hierarchical", true}});
        log.setMaxBufferEntries(50);

        // action 0: a GROUP of 2 -> 1, with a pair detail holding a NaN and a list
        log.beginAction(CurationLogger::ActionType::GROUP, QList<ClusterSnapshot>{snap(2), snap(3)});
        log.recordActionDetails({{"detail", "pair"}, {"cross_refrac_ratio", std::nan("")},
                                 {"members", QVariantList{2, 3}}});
        log.commitAction(snap(7, 0.4));
        // action 1: a SPLIT that will be undone after the checkpoint
        log.beginAction(CurationLogger::ActionType::SPLIT, snap(7));
        log.commitAction(QList<ClusterSnapshot>{snap(7), snap(8)});

        QList<QJsonObject> L = readLines(path);
        check(L.size() == 1 && L[0]["event"] == "SESSION_OPEN", "before a save only SESSION_OPEN is on disk");
        check(L[0]["method"] == "stderiv" && L[0]["stage"] == "final" && L[0]["hierarchical"] == true
              && L[0]["klusters_version"] == "3.2", "SESSION_OPEN carries the context map");

        log.checkpoint();                                   // == Save
        L = readLines(path);
        int snapLines = 0, detailLines = 0, finals = 0;
        for (const QJsonObject& o : L) {
            if (o.contains("phase")) { ++snapLines; finals += o["final"].toBool(); }
            if (o["event"] == "ACTION_DETAIL") ++detailLines;
        }
        check(snapLines == 3 + 3, "checkpoint wrote both actions' snapshot lines (2+1, 1+2)");
        check(finals == 0, "checkpoint lines are final:false");
        check(detailLines == 1, "the pair detail was written once");
        check(L.last()["event"] == "CHECKPOINT" && L.last()["n_pending"] == 2, "CHECKPOINT marker with the ring size");
        for (const QJsonObject& o : L) check(!o.contains("__bad__"), "every line parses as JSON");
        bool sawNull = false, sawList = false, sawRefrac = false, sawAmp = false;
        for (const QJsonObject& o : L) {
            if (o["event"] == "ACTION_DETAIL") {
                sawNull = o["cross_refrac_ratio"].isNull();
                sawList = o["members"].isArray() && o["members"].toArray().size() == 2;
            }
            if (o["phase"] == "after" && o["cluster"] == 7 && o["action"] == "GROUP") {
                sawRefrac = std::abs(o["refrac_ratio"].toDouble() - 0.4) < 1e-12 && o["refrac_expected"] == 5.0
                            && o["burst_index"] == 3.5 && o["occupied_hz"] == 2.5;
                sawAmp = o["waveform_chan_amp"].toArray().size() == 8 && o["waveform_dom_chan"] == 3
                         && o["waveform_width_ms"] == 0.6;
            }
        }
        check(sawNull, "NaN in a detail is written as null");
        check(sawList, "a QVariantList detail is written as a JSON array");
        check(sawRefrac, "snapshot lines carry refrac_ratio / refrac_expected / burst_index / occupied_hz");
        check(sawAmp, "snapshot lines carry waveform_dom_chan / width_ms / chan_amp[8]");

        log.notifyUndo();                                   // the SPLIT is undone after the save
        check(log.pendingEntryCount() == 2, "checkpoint kept the ring (undo still has its entry)");
        log.close();
    }

    const QList<QJsonObject> L = readLines(path);
    // last line per (action_idx, phase, cluster) wins
    QMap<QString, QJsonObject> last;
    for (const QJsonObject& o : L)
        if (o.contains("phase"))
            last[QString("%1/%2/%3").arg(o["action_idx"].toInt()).arg(o["phase"].toString()).arg(o["cluster"].toInt())] = o;
    check(last["0/after/7"]["final"] == true && last["0/after/7"]["status"] == "good",
          "close rewrote the GROUP as final:true, good");
    check(last["1/after/8"]["final"] == true && last["1/after/8"]["status"] == "bad",
          "the SPLIT undone after the save is final:true, bad -- the checkpoint copy did not lock it");
    int finalDetails = 0;
    for (const QJsonObject& o : L) if (o["event"] == "ACTION_DETAIL" && o["final"] == true) ++finalDetails;
    check(finalDetails == 1, "the detail is rewritten once as final");
    check(L.last()["event"] == "SESSION_CLOSE" && L.last()["n_actions"] == 2, "SESSION_CLOSE");

    // ── gestures and the layer field ──
    const QString path2 = dir.filePath("rat.cur.stderiv.6.g");
    {
        CurationLogger log;
        log.open(path2, "rat.clu.stderiv.6.g", "6", 32552.0, 8, 32);
        log.setMaxBufferEntries(2);                 // tiny ring: the trim must spare the open gesture
        const int gOuter = log.beginGesture("breakUpParents", {{"parents", QVariantList{5}}});
        log.beginAction(CurationLogger::ActionType::MOVE_SPIKES, QList<ClusterSnapshot>{snap(5), snap(9)});
        log.commitAction(QList<ClusterSnapshot>{snap(5), snap(9)});
        log.beginGesture("promoteChildren", {{"children", QVariantList{41}}});
        log.beginAction(CurationLogger::ActionType::MOVE_SPIKES, QList<ClusterSnapshot>{snap(5), snap(12)});
        log.commitAction(QList<ClusterSnapshot>{snap(5), snap(12)});
        log.endGesture({{"produced", 12}});
        log.beginAction(CurationLogger::ActionType::MOVE_SPIKES, QList<ClusterSnapshot>{snap(5), snap(13)});   // overflows the ring of 2
        log.commitAction(QList<ClusterSnapshot>{snap(5), snap(13)});
        check(log.currentGestureIdx() == gOuter, "after the inner endGesture the outer is current again");
        log.endGesture({{"landed", QVariantList{9, 12, 13}}});
        // a child-layer entry, then an undo: the undo must flip the last undo-paired primitive
        ClusterSnapshot c = snap(7); c.layer = "child";
        log.beginAction(CurationLogger::ActionType::HIERARCHY_EDIT, c);
        log.commitAction(c);
        log.markCurrentActionNotUndoable();
        log.notifyUndo();
        log.close();
    }
    const QList<QJsonObject> G = readLines(path2);
    QMap<int, QJsonObject> gest; QList<QJsonObject> prims; QJsonObject childLine;
    for (const QJsonObject& o : G) {
        if (o["action"] == "GESTURE" && o["event"] == "ACTION_DETAIL" && o.contains("gesture")) gest[o["action_idx"].toInt()] = o;
        if (o["action"] == "MOVE_SPIKES" && o["phase"] == "after") prims.append(o);
        if (o["layer"] == "child") childLine = o;
    }
    check(gest.size() == 2, "two GESTURE entries (outer breakUpParents, inner promoteChildren)");
    int outerIdx = -1, innerIdx = -1;
    for (auto it = gest.constBegin(); it != gest.constEnd(); ++it)
        (it.value()["gesture"] == "breakUpParents" ? outerIdx : innerIdx) = it.key();
    check(gest[innerIdx]["gesture_idx"].toInt() == outerIdx, "the inner gesture's own lines carry the outer's index");
    check(gest[outerIdx]["gesture_idx"].toInt() == -1, "the outer gesture is top level (gesture_idx -1)");
    int inOuter = 0, inInner = 0;
    for (const QJsonObject& o : prims) {
        if (o["gesture_idx"].toInt() == outerIdx) ++inOuter;
        if (o["gesture_idx"].toInt() == innerIdx) ++inInner;
    }
    check(inOuter == 4 && inInner == 2, "primitives carry the gesture they ran in (2 outer moves x2 clusters, 1 inner)");
    bool endSeen = false;
    for (const QJsonObject& o : G) if (o["action"] == "GESTURE" && o["gesture_end"] == true && o.contains("landed")) endSeen = true;
    check(endSeen, "endGesture's outcome is recorded on the gesture entry (the trim did not flush it while open)");
    check(childLine["layer"] == "child" && childLine["action"] == "HIERARCHY_EDIT" && childLine["status"] == "good",
          "a child-layer line carries layer=child and, not undo-paired, stays good");
    bool lastMoveBad = false;
    for (const QJsonObject& o : prims) if (o["cluster"] == 13 && o["status"] == "bad") lastMoveBad = true;
    check(lastMoveBad, "the undo flipped the last undo-paired primitive, not the child entry or a gesture");

    std::printf("%s (%d checks, %d failed)\n", g_fail ? "FAILED" : "OK", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
