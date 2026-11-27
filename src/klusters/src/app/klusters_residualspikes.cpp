/***************************************************************************
    klusters_residualspikes.cpp
    ---------------------------
    KlustersApp "Add Residual Spikes as Clusters".

    A collision row holds its own unit's spike and a partner's.  The partner's
    is, nearly always, a row of no cluster: the detector does not fire twice
    within its dead time, so the second spike of a collision is missing from its
    unit's train.  KlustersDoc::alignedResidualSpikes makes each such spike
    ready -- cut from the recording around its own peak, the row's own unit
    subtracted -- and this adds them to the session as rows.

    They go to NEW clusters, one per partner named, not into the partners'
    clusters: a residual is named by a fit, and whether it is that unit's spike
    is for the curator to see (in the new cluster's waveforms and features
    beside the partner's) before the two are merged.

    Adding rows changes every row-aligned file, so -- as decollide does -- a new
    stage is written beside the open one and opened:

        <base>.res / .clu[.clc .clp] / .spk.<every method> / .spkx.<every method>
              / .fet.<every space> / .eap
        under <stage>_rs ("rs" on an untagged session)

    The .spkx (formats/spkx.md) holds each new row's record over a wider window:
    the recording does not hold the row, so a realign, nudge or re-extract cuts
    it from there.  The open stage's own .spkx rows are carried, renumbered.

    Existing rows keep their order and everything they hold.  A new row is put
    next to the row it was found in, on the side its time says, so a session in
    time order stays in time order.  Its .eap row names no class.  The .spkc and
    .spkr are not carried: they are rebuilt (the rows they index have moved).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "clusterpalette.h"
#include "hierarchyfiles.h"
#include "reservedclusters.h"

#include <neurosuite/core/custody.hpp>
#include <neurosuite/core/neurofileio.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
constexpr int kNeighbourSteps = 8;      ///< rows a new row may be moved past to sit in time order

/** Reads a .clu-framed int32 file whole: the header, then every value. */
bool readFramed(const QString& path, int& header, std::vector<int>& values)
{
    std::ifstream in(path.toLocal8Bit().constData(), std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff bytes = in.tellg();
    if (bytes < 4 || (bytes - 4) % 4 != 0) return false;
    in.seekg(0);
    int32_t h = 0;
    in.read(reinterpret_cast<char*>(&h), 4);                 // reinterpret_cast: raw int32 of the file
    std::vector<int32_t> v(static_cast<std::size_t>((bytes - 4) / 4));
    if (!v.empty()) in.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(v.size() * 4));
    if (!in) return false;
    header = h;
    values.assign(v.begin(), v.end());
    return true;
}
} // namespace

bool KlustersApp::writeResidualStage(const KlustersDoc::ResidualSpikes& found, QString& outCluPath, QString& summary,
                                     QMap<int, int>* newClusters)
{
    using namespace neurofileio;
    outCluPath.clear();
    if (!doc || doc->url().isEmpty() || !found.ok || found.spikes.empty()) { summary = tr("Add residual spikes: nothing to add."); return false; }
    const int nChan = doc->nbOfchannels();
    const int nSamp = doc->getNbSamplesBeforePeak() + doc->getNbSamplesAfterPeak() + 1;
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;

    const QString cluPath = doc->url();
    const QString spkPath = doc->origSpkFilePath();
    const QString resPath = doc->origResFilePath();
    const QString fetPath = doc->origFetFilePath();
    const auto aClu = neurosuite::custody::parseAnchor(QFileInfo(cluPath).fileName().toStdString());
    const auto aSpk = neurosuite::custody::parseAnchor(QFileInfo(spkPath).fileName().toStdString());
    const auto aRes = neurosuite::custody::parseAnchor(QFileInfo(resPath).fileName().toStdString());
    const auto aFet = neurosuite::custody::parseAnchor(QFileInfo(fetPath).fileName().toStdString());
    if (!aClu.ok || !aSpk.ok || !aRes.ok || !aFet.ok) { summary = tr("Add residual spikes: could not parse the session file names."); return false; }
    const QString dir = QFileInfo(cluPath).absolutePath() + QDir::separator();
    const std::string base = (dir + QString::fromStdString(aClu.base)).toStdString();
    const int grp = aClu.group;
    const QString outTag = aClu.suffix.empty() ? QStringLiteral("rs") : (QString::fromStdString(aClu.suffix) + QStringLiteral("_rs"));
    const std::string outTagS = outTag.toStdString();

    ClusterResData cr = readClusterRes(cluPath.toStdString(), resPath.toStdString());
    const std::size_t n = cr.ids.size();
    if (!cr.ok || cr.times.size() != n || found.spaces.isEmpty() || found.spaces.front().fetCommitted != fetPath) {
        summary = tr("Add residual spikes: could not read consistent res/clu/fet for this group.");
        return false;
    }
    const std::size_t k = found.spikes.size();
    for (const KlustersDoc::ResidualSpike& s : found.spikes)
        if (s.sourceRow < 0 || static_cast<std::size_t>(s.sourceRow) >= n) { summary = tr("Add residual spikes: a spike names a row the files do not hold."); return false; }

    // ── a new cluster per partner, above every id in use and outside the reserved ones ──
    int highest = 1;
    for (const int id : cr.ids) if (!isReservedClusterId(static_cast<dataType>(id))) highest = std::max(highest, id);
    QMap<int, int> clusterOf;                                  // partner -> new cluster, partners ascending
    for (const KlustersDoc::ResidualSpike& s : found.spikes) clusterOf.insert(s.partner, 0);
    for (auto it = clusterOf.begin(); it != clusterOf.end(); ++it) {
        do { ++highest; } while (isReservedClusterId(static_cast<dataType>(highest)));
        it.value() = highest;
    }
    if (newClusters) *newClusters = clusterOf;

    // ── where each new row goes: beside the row it was found in, on the side its time says ──
    // slot[i] = the new rows put right after existing row i (i = -1: before the first), by time.
    std::vector<std::vector<std::size_t>> after(n + 1);         // index i + 1
    for (std::size_t q = 0; q < k; ++q) {
        const KlustersDoc::ResidualSpike& s = found.spikes[q];
        qint64 at = s.sourceRow;                                 // the new row goes right after `at`
        if (s.time >= cr.times[static_cast<std::size_t>(at)]) {
            for (int step = 0; step < kNeighbourSteps && static_cast<std::size_t>(at + 1) < n && cr.times[static_cast<std::size_t>(at + 1)] <= s.time; ++step) ++at;
        } else {
            --at;
            for (int step = 0; step < kNeighbourSteps && at >= 0 && cr.times[static_cast<std::size_t>(at)] > s.time; ++step) --at;
        }
        after[static_cast<std::size_t>(at + 1)].push_back(q);
    }
    for (std::vector<std::size_t>& slot : after)
        std::sort(slot.begin(), slot.end(), [&](std::size_t x, std::size_t y) { return found.spikes[x].time < found.spikes[y].time; });
    // The new numbering: for each new position, the old row (>= 0) or the new spike (-1 - q).
    std::vector<qint64> source;
    source.reserve(n + k);
    for (const std::size_t q : after[0]) source.push_back(-1 - static_cast<qint64>(q));
    for (std::size_t i = 0; i < n; ++i) {
        source.push_back(static_cast<qint64>(i));
        for (const std::size_t q : after[i + 1]) source.push_back(-1 - static_cast<qint64>(q));
    }
    const std::size_t total = source.size();

    // ── res, clu ──
    std::vector<int64_t> times(total);
    std::vector<int> ids(total);
    for (std::size_t r = 0; r < total; ++r) {
        if (source[r] >= 0) { times[r] = cr.times[static_cast<std::size_t>(source[r])]; ids[r] = cr.ids[static_cast<std::size_t>(source[r])]; }
        else { const KlustersDoc::ResidualSpike& s = found.spikes[static_cast<std::size_t>(-1 - source[r])]; times[r] = s.time; ids[r] = clusterOf.value(s.partner); }
    }
    const std::string outClu = stagePath(base, "clu", aClu.method, grp, outTagS);
    if (!writeResBinary(stagePath(base, "res", aRes.method, grp, outTagS), times)
        || !writeCluBinary(outClu, hierarchyfiles::distinctCount(ids), ids)) {
        summary = tr("Add residual spikes: could not write the new stage's .res / .clu.");
        return false;
    }

    // ── the child layer, when there is one: a new cluster is its own child ──
    const QString clcPath = QString(cluPath).replace(QStringLiteral(".clu."), QStringLiteral(".clc."));
    const QString clpPath = QString(cluPath).replace(QStringLiteral(".clu."), QStringLiteral(".clp."));
    bool hierarchical = false;
    if (QFileInfo::exists(clcPath)) {
        int header = 0;
        std::vector<int> child;
        if (readFramed(clcPath, header, child) && child.size() == n) {
            // (a .clc in the numbering used before 2026-11-21 is brought to this one first: a
            // self child beside old ids would make the file read as neither)
            const hierarchyfiles::ChildIdUpgrade upgrade = hierarchyfiles::upgradeChildIds(cr.ids, child);
            if (upgrade.needed) child = upgrade.ids;
            std::vector<int> grown(total);
            for (std::size_t r = 0; r < total; ++r)
                grown[r] = source[r] >= 0 ? child[static_cast<std::size_t>(source[r])]
                                          : static_cast<int>(hierarchyfiles::selfChildOf(ids[r]));
            const hierarchyfiles::NestingScan nesting = hierarchyfiles::scanNesting(ids, grown);
            const std::vector<int32_t> clp = hierarchyfiles::clpFromMap(nesting.childToParent);
            const std::vector<int> parentOf(clp.begin(), clp.end());
            const std::string outClc = QString::fromStdString(outClu).replace(QStringLiteral(".clu."), QStringLiteral(".clc.")).toStdString();
            const std::string outClp = QString::fromStdString(outClu).replace(QStringLiteral(".clu."), QStringLiteral(".clp.")).toStdString();
            hierarchical = nesting.ok
                           && writeCluBinary(outClc, hierarchyfiles::distinctCount(grown), grown)
                           && writeCluBinary(outClp, static_cast<int>(parentOf.size()), parentOf);
            if (!hierarchical) { summary = tr("Add residual spikes: could not write the new stage's .clc / .clp."); return false; }
        }
    }

    // ── every .spk method: the rows as they are, the new rows' records ──
    QStringList spkMethods = availableSpkVariants();
    if (spkMethods.isEmpty()) spkMethods << QString::fromStdString(aSpk.method);
    QStringList written, skipped, carriedSkipped;
    qint64 extendedRows = 0;
    // old row -> its row in the new stage
    std::vector<int64_t> newRowOf(n, -1);
    for (std::size_t r = 0; r < total; ++r) if (source[r] >= 0) newRowOf[static_cast<std::size_t>(source[r])] = static_cast<int64_t>(r);
    for (const QString& m : spkMethods) {
        if (!found.spikes.front().records.contains(m)) { skipped << m; continue; }
        const SpkFile sm = readSpk(stagePath(base, "spk", m.toStdString(), grp, aSpk.suffix), nSamp, nChan);
        if (!sm.ok || sm.nSpikes != static_cast<int64_t>(n)) { skipped << m; continue; }
        std::vector<int16_t> grown(total * rec);
        for (std::size_t r = 0; r < total; ++r) {
            if (source[r] >= 0) { std::memcpy(&grown[r * rec], &sm.samples[static_cast<std::size_t>(source[r]) * rec], rec * sizeof(int16_t)); continue; }
            const auto record = found.spikes[static_cast<std::size_t>(-1 - source[r])].records.constFind(m);
            if (record == found.spikes[static_cast<std::size_t>(-1 - source[r])].records.constEnd() || record->size() != rec) {
                summary = tr("Add residual spikes: a spike has no record for %1.").arg(m);
                return false;
            }
            std::memcpy(&grown[r * rec], record->data(), rec * sizeof(int16_t));
        }
        if (!writeSpk(stagePath(base, "spk", m.toStdString(), grp, outTagS), nSamp, nChan, grown)) {
            summary = tr("Add residual spikes: could not write the new stage's .spk (%1).").arg(m);
            return false;
        }
        // The extended records (formats/spkx.md): the new rows', and those the open stage
        // holds for its own added rows, renumbered.  What realign, nudge and re-extract
        // on save cut these rows from instead of the recording.
        {
            SpkxFile x;
            x.nSpikes = static_cast<int64_t>(total); x.nSamples = nSamp; x.nChannels = nChan;
            x.margin = found.margin; x.group = grp;
            const std::size_t extRec = static_cast<std::size_t>(x.extendedSamples()) * nChan;
            const SpkxFile was = readSpkx(stagePath(base, "spkx", m.toStdString(), grp, aClu.suffix));
            const bool carry = was.ok && was.nSpikes == static_cast<int64_t>(n) && was.nSamples == nSamp
                               && was.nChannels == nChan && was.margin == found.margin;
            if (was.ok && !carry) carriedSkipped << m;
            for (std::size_t r = 0; r < total; ++r) {
                if (source[r] >= 0) {
                    const int64_t k = carry ? spkxFind(was, source[r]) : -1;
                    if (k < 0) continue;
                    x.rows.push_back(static_cast<int64_t>(r));
                    x.times.push_back(was.times[static_cast<std::size_t>(k)]);
                    const int64_t src = was.sources[static_cast<std::size_t>(k)];
                    x.sources.push_back(src >= 0 ? newRowOf[static_cast<std::size_t>(src)] : -1);
                    const auto from = was.records.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(k) * extRec);
                    x.records.insert(x.records.end(), from, from + static_cast<std::ptrdiff_t>(extRec));
                    continue;
                }
                const KlustersDoc::ResidualSpike& sp = found.spikes[static_cast<std::size_t>(-1 - source[r])];
                const auto ext = sp.extended.constFind(m);
                if (ext == sp.extended.constEnd() || ext->size() != extRec) continue;
                x.rows.push_back(static_cast<int64_t>(r));
                x.times.push_back(sp.time);
                x.sources.push_back(newRowOf[static_cast<std::size_t>(sp.sourceRow)]);
                x.records.insert(x.records.end(), ext->begin(), ext->end());
            }
            if (!writeSpkx(stagePath(base, "spkx", m.toStdString(), grp, outTagS), x)) {
                summary = tr("Add residual spikes: could not write the new stage's .spkx (%1).").arg(m);
                return false;
            }
            extendedRows = std::max(extendedRows, static_cast<qint64>(x.rows.size()));
        }
        written << m;
    }
    if (!written.contains(QString::fromStdString(aSpk.method))) {
        summary = tr("Add residual spikes: the session's own .spk (%1) could not be grown.").arg(QString::fromStdString(aSpk.method));
        return false;
    }

    // ── every feature space's .fet: the rows as they are, the new rows' feature rows ──
    QStringList spacesWritten;
    QStringList spacesSkipped = found.spacesSkipped;
    for (const KlustersDoc::ResidualSpace& space : found.spaces) {
        const bool own = space.fetCommitted == fetPath;          // the .clu's own space: it must be grown
        const FetBinaryFile fb = readFetBinary(space.fetCommitted.toStdString());
        const std::size_t nf = static_cast<std::size_t>(space.nDims);
        bool fits = fb.ok && fb.nSpikes == static_cast<int64_t>(n) && fb.nFeatures == space.nDims;
        for (std::size_t q = 0; fits && q < k; ++q) {
            const auto row = found.spikes[q].features.constFind(space.method);
            fits = row != found.spikes[q].features.constEnd() && row->size() == nf;
        }
        if (!fits) {
            if (own) { summary = tr("Add residual spikes: could not read consistent res/clu/fet for this group."); return false; }
            spacesSkipped << tr("%1: its .fet on disk does not hold this stage's rows").arg(space.method);
            continue;
        }
        std::vector<int64_t> grown(total * nf);
        for (std::size_t r = 0; r < total; ++r) {
            if (source[r] >= 0) std::memcpy(&grown[r * nf], &fb.values[static_cast<std::size_t>(source[r]) * nf], nf * sizeof(int64_t));
            else std::memcpy(&grown[r * nf], found.spikes[static_cast<std::size_t>(-1 - source[r])].features.constFind(space.method)->data(),
                             nf * sizeof(int64_t));
        }
        const std::string method = own ? aFet.method : space.method.toStdString();
        if (!writeFetBinary(stagePath(base, "fet", method, grp, outTagS), space.nDims, grown)) {
            summary = tr("Add residual spikes: could not write the new stage's .fet (%1).").arg(space.method);
            return false;
        }
        spacesWritten << space.method;
    }

    // ── the membership: every row keeps what it names; a new row names no class ──
    {
        const EapFile e = readEap(doc->stageEapPath().toStdString());
        if (e.ok && e.nSpikes == static_cast<int64_t>(n) && e.nClasses > 0) {
            const std::size_t t = static_cast<std::size_t>(e.nClasses);
            std::vector<int8_t> grown(total * t, EAP_ABSENT);
            for (std::size_t r = 0; r < total; ++r)
                if (source[r] >= 0) std::memcpy(&grown[r * t], &e.cells[static_cast<std::size_t>(source[r]) * t], t);
            std::string outEap = neurosuite::custody::untaggedPath(base, "eap", grp);
            outEap += "." + outTagS;
            if (!writeEap(outEap, static_cast<int64_t>(total), e.nClasses, e.group, e.flags, grown))
                qWarning().noquote() << "[residual spikes] could not write" << QString::fromStdString(outEap);
        }
    }

    outCluPath = QString::fromStdString(outClu);
    QStringList per;
    for (auto it = clusterOf.constBegin(); it != clusterOf.constEnd(); ++it)
        per << tr("%1 -> %2 (%3)").arg(it.key()).arg(it.value()).arg(found.perPartner.value(it.key()));
    summary = tr("Added %1 residual spike(s) as %2 new cluster(s), partner -> new cluster (spikes): %3.  Stage \"%4\"%5%6.")
                  .arg(k).arg(clusterOf.size()).arg(per.join(QStringLiteral(", "))).arg(outTag)
                  .arg(hierarchical ? tr(", each new cluster its own child") : QString())
                  .arg((skipped.isEmpty() ? QString() : tr("; .spk not grown for: %1").arg(skipped.join(QStringLiteral(", "))))
                       + tr("; %1 row(s) with an extended record (.spkx, +-%2 samples)").arg(extendedRows).arg(found.margin)
                       + (carriedSkipped.isEmpty() ? QString() : tr("; the open stage's .spkx not carried for: %1").arg(carriedSkipped.join(QStringLiteral(", "))))
                       + tr("; feature spaces grown: %1").arg(spacesWritten.join(QStringLiteral(", ")))
                       + (spacesSkipped.isEmpty() ? QString() : tr("; not grown: %1").arg(spacesSkipped.join(QStringLiteral("; ")))));
    qInfo().noquote() << "[residual spikes]" << summary;
    return true;
}

void KlustersApp::slotAddResidualSpikes()
{
    if (!doc || doc->url().isEmpty()) return;
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish first."), 5000);
        return;
    }
    if (doc->isModified()) {
        QMessageBox::information(this, tr("Add Residual Spikes as Clusters"),
            tr("The session has changes that are not saved.  The new stage is written from the files on disk: save first."));
        return;
    }
    KlustersView* view = activeView();
    if (!view) return;
    const bool childScope = doc->isChildClusteringActive();
    QList<int> selected = (childScope && childPaletteA) ? childPaletteA->selectedClusters() : view->clusters();
    selected.removeAll(0);
    selected.removeAll(1);
    if (selected.isEmpty()) { slotStatusMsg(tr("Add residual spikes: select one or more clusters first.")); return; }
    // From any shown space: the residuals are made, and the stage written, in the .clu's own; back to
    // the shown one when nothing is written (the new stage opens in its own space).
    QString shownBefore;
    if (!enterCluSpace(tr("Add residual spikes"), &shownBefore)) return;
    struct Back {
        KlustersApp* app; QString method;
        ~Back() { if (!method.isEmpty() && app->doc && app->doc->activeFeatureMethod() != method) app->showFeatureSpace(method, false); }
    } back{this, shownBefore};

    QApplication::setOverrideCursor(Qt::WaitCursor);
    slotStatusMsg(tr("Cutting the residual spikes of the selection…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QStringList methods = availableSpkVariants();
    const KlustersDoc::ResidualSpikes R = doc->alignedResidualSpikes(selected, childScope, methods);
    QApplication::restoreOverrideCursor();
    if (!R.ok) { slotStatusMsg(tr("Add residual spikes: %1").arg(R.reason)); return; }
    const QString left = tr("Left out: %1 already a row or a repeat of another, %2 on a row once aligned, %3 repeated, "
                            "%4 without a partner cluster, %5 without neighbours.")
                             .arg(R.known).arg(R.alreadyRow).arg(R.repeated).arg(R.unmatched).arg(R.withoutNeighbours);
    if (R.spikes.empty()) {
        slotStatusMsg(tr("Add residual spikes: none to add among the %1 residuals of the selection.  %2").arg(R.candidates).arg(left));
        return;
    }
    QStringList per;
    QList<int> partners = R.perPartner.keys();
    std::sort(partners.begin(), partners.end(), [&](int a, int b) { return R.perPartner.value(a) > R.perPartner.value(b); });
    for (int i = 0; i < partners.size() && i < 12; ++i) {
        const KlustersDoc::ResidualSpikes::Match m = R.matchOf.value(partners[i]);
        per << (m.scored > 0 ? tr("%1: %2 (%3 % fit)").arg(partners[i]).arg(R.perPartner.value(partners[i])).arg(qRound(100.0 * m.best / m.scored))
                             : tr("%1: %2").arg(partners[i]).arg(R.perPartner.value(partners[i])));
    }
    QStringList doubt;
    for (const int partner : R.doubtful) {
        const KlustersDoc::ResidualSpikes::Match m = R.matchOf.value(partner);
        doubt << tr("%1 (%2 of %3 fit it best; %4 fit cluster %5)").arg(partner).arg(m.best).arg(m.scored).arg(m.looksLikeCount).arg(m.looksLike);
    }
    if (partners.size() > 12) per << tr("… and %1 more").arg(partners.size() - 12);
    QMessageBox box(QMessageBox::Question, tr("Add Residual Spikes as Clusters"),
        tr("%1 residual spike(s) of %2 partner(s) are rows of no cluster.  Add them as %2 new cluster(s)?")
            .arg(R.spikes.size()).arg(R.perPartner.size()),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(
        tr("Each is cut from %1 around its own peak (%2 were moved by the alignment), the row's own unit "
           "subtracted, and becomes a row of a NEW cluster -- one per partner named -- so that it can be "
           "looked at beside that partner before the two are merged.  A new stage is written and opened; "
           "the open stage is not changed.\n\nPartners (cluster: spikes, and the share whose features fit that "
           "cluster better than any other): %3.\n\n%4%5")
            .arg(R.source).arg(R.moved).arg(per.join(QStringLiteral(", ")), left,
                 doubt.isEmpty() ? QString()
                                 : tr("\n\nDOUBTFUL -- the spikes named for these clusters mostly do not look like them; "
                                      "their new clusters are probably not that unit: %1.").arg(doubt.join(QStringLiteral("; ")))));
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString outCluPath, summary;
    const bool ok = writeResidualStage(R, outCluPath, summary);
    QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, tr("Add Residual Spikes as Clusters"), summary); return; }
    back.method.clear();                                         // the new stage opens on its own space
    slotFileClose();
    if (!mainDock) openDocumentFile(outCluPath);
    statusBar()->showMessage(summary, 15000);
}

void KlustersApp::slotRebuildExtendedRecords()
{
    if (!doc || doc->url().isEmpty()) return;
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled()) || fileJobRunning()) {
        statusBar()->showMessage(tr("Wait for the running job to finish first."), 5000);
        return;
    }
    const QString title = tr("Rebuild Residual Spikes' Extended Records");
    if (doc->isModified()) {
        QMessageBox::information(this, title, tr("The session has changes that are not saved.  The records are rebuilt from the files on disk: save first."));
        return;
    }
    // From any shown space: the records are rebuilt in the .clu's own, then the shown one comes back.
    QString shownBefore;
    if (!enterCluSpace(tr("Rebuild extended records"), &shownBefore)) return;
    struct Back {
        KlustersApp* app; QString method;
        ~Back() { if (!method.isEmpty() && app->doc && app->doc->activeFeatureMethod() != method) app->showFeatureSpace(method, false); }
    } back{this, shownBefore};
    QApplication::setOverrideCursor(Qt::WaitCursor);
    slotStatusMsg(tr("Looking for the rows of residual spikes…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const QStringList methods = availableSpkVariants();
    const KlustersDoc::ExtendedRebuild X = doc->rebuildExtendedRecords(methods, /*write*/ false);
    QApplication::restoreOverrideCursor();
    if (!X.ok) { slotStatusMsg(tr("Rebuild extended records: %1").arg(X.reason)); return; }
    const QString found = tr("Of %1 collision row(s) with a residual: %2 row(s) at the residual's time are not the recording's "
                             "window there (residual spikes made into rows); %3 are the recording's (the partner's own row, "
                             "or a residual row cut again from the recording: it cannot be rebuilt); %4 have a record already; "
                             "%5 without neighbours.")
                              .arg(X.residuals).arg(X.found).arg(X.inRecording).arg(X.had).arg(X.withoutNeighbours);
    const qint64 rebuilt = X.found - X.withoutNeighbours;
    if (rebuilt <= 0) { slotStatusMsg(tr("Rebuild extended records: nothing to rebuild.  %1").arg(found)); return; }
    QMessageBox box(QMessageBox::Question, title,
        tr("Rebuild the extended records of %1 residual spike row(s)?").arg(rebuilt), QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(
        tr("%1\n\nEach record is the row's window and %2 samples either side, cut from the recording less the "
           "source row's own unit as Add Residual Spikes cut it, its middle the row as it is.  Rebuilt against "
           "the rows: %3 % RMS difference (median), %4 row(s) more than 20 % off (their unit's neighbours have "
           "changed since).  Written for: %5.%6")
            .arg(found).arg(X.margin).arg(QString::number(100.0 * X.medianMismatch, 'f', 1)).arg(X.unlike)
            .arg(X.methods.join(QStringLiteral(", ")),
                 X.methodsSkipped.isEmpty() ? QString() : tr("  Not: %1.").arg(X.methodsSkipped.join(QStringLiteral("; ")))));
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() != QMessageBox::Yes) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const KlustersDoc::ExtendedRebuild W = doc->rebuildExtendedRecords(methods, /*write*/ true);
    QApplication::restoreOverrideCursor();
    if (!W.ok) { QMessageBox::warning(this, title, W.reason); return; }
    statusBar()->showMessage(tr("Extended records of %1 residual spike row(s) written (.spkx: %2).")
                                 .arg(W.found - W.withoutNeighbours).arg(W.methods.join(QStringLiteral(", "))), 15000);
}
