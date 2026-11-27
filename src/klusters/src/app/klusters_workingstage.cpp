/***************************************************************************
    klusters_workingstage.cpp  -  Add Residual Spikes into a working stage,
                                  made the open stage's at Save

    Adding rows changes every row-indexed file, so the rows are written as a
    stage of their own beside the open one -- the working stage, `<target>_rw`
    -- and the document reopened on it, with the displays kept.  Nothing of
    the target is touched until Save: then the working stage's files take the
    target's names (the target's row-indexed files they do not replace are set
    aside as `.stalebkp`) and the target is opened.  Closing without saving
    removes the working stage.  The target is the open stage; an untagged
    session's is `rs` (`rs2`, ... when taken).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "klustersdoc.h"

#include <neurosuite/core/custody.hpp>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSet>
#include <QStatusBar>

#include <filesystem>
#include <system_error>

namespace {

/** The .clu's parts: its directory (with the separator), base, the rest after the type token
 *  ("<method>.<group>[.<stage>]"), group and stage. */
struct CluName {
    QString dir, base, rest, stage;
    int group = -1;
    bool ok = false;
};

CluName cluName(const QString& cluPath)
{
    CluName c;
    const QFileInfo fi(cluPath);
    const auto a = neurosuite::custody::parseAnchor(fi.fileName().toStdString());
    if (!a.ok || a.type != "clu") return c;
    c.dir = fi.absolutePath() + QDir::separator();
    c.base = QString::fromStdString(a.base);
    c.group = a.group;
    c.stage = QString::fromStdString(a.suffix);
    c.rest = fi.fileName().mid(c.base.size() + 5);              // past "<base>.clu."
    c.ok = true;
    return c;
}

/** The .clu of @p c in stage @p stage (empty: untagged). */
QString cluOfStage(const CluName& c, const QString& stage)
{
    QString rest = c.rest;
    if (!c.stage.isEmpty()) rest.chop(c.stage.size() + 1);
    return c.dir + c.base + QStringLiteral(".clu.") + rest + (stage.isEmpty() ? QString() : QLatin1Char('.') + stage);
}

/** The file of type @p type named like the .clu @p clu (".clu." swapped), as .cli, .cur, .templates are. */
QString swapType(const CluName& c, const QString& clu, const QString& type)
{
    return c.dir + c.base + QLatin1Char('.') + type + QLatin1Char('.') + QFileInfo(clu).fileName().mid(c.base.size() + 5);
}

/** Every file of stage @p stage of the group: `<base>.<type>[.<method>].<group>.<stage>`. */
QStringList filesOfStage(const CluName& c, const QString& stage)
{
    QStringList out;
    if (stage.isEmpty()) return out;
    const QString tail = QLatin1Char('.') + QString::number(c.group) + QLatin1Char('.') + stage;
    for (const QString& f : QDir(c.dir).entryList(QDir::Files))
        if (f.startsWith(c.base + QLatin1Char('.')) && f.endsWith(tail)) out << c.dir + f;
    return out;
}

/** The type token of a file of the session (after "<base>."). */
QString typeOf(const CluName& c, const QString& path)
{
    return QFileInfo(path).fileName().mid(c.base.size() + 1).section(QLatin1Char('.'), 0, 0);
}

/** Types whose rows are the spikes': a stage's file of one of these is stale beside other rows. */
bool rowIndexed(const QString& type)
{
    static const QSet<QString> types{QStringLiteral("res"), QStringLiteral("clu"), QStringLiteral("clc"),
                                     QStringLiteral("spk"), QStringLiteral("spkx"), QStringLiteral("spkc"),
                                     QStringLiteral("spkr"), QStringLiteral("fet"), QStringLiteral("fetr"),
                                     QStringLiteral("col"), QStringLiteral("eap")};
    return types.contains(type);
}

/** Replaces @p to with @p from (one rename; @p to need not exist). */
bool moveOver(const QString& from, const QString& to)
{
    std::error_code ec;
    std::filesystem::rename(QFile::encodeName(from).toStdString(), QFile::encodeName(to).toStdString(), ec);
    return !ec;
}

/** Removes stage @p stage of the group: its files, and what was made beside them (`.pending`, `.bak`, ...). */
int removeStage(const CluName& c, const QString& stage)
{
    if (stage.isEmpty()) return 0;
    const QRegularExpression re(QLatin1Char('^') + QRegularExpression::escape(c.base) + QStringLiteral("\\..*\\.")
                                + QString::number(c.group) + QStringLiteral("\\.") + QRegularExpression::escape(stage)
                                + QStringLiteral("(\\..*)?$"));
    int n = 0;
    for (const QString& f : QDir(c.dir).entryList(QDir::Files))
        if (re.match(f).hasMatch() && QFile::remove(c.dir + f)) ++n;
    return n;
}

} // namespace

bool KlustersApp::refuseInWorkingStage(const QString& what)
{
    if (!doc || !doc->inWorkingStage()) return false;
    QMessageBox::information(this, what,
        tr("The residual spikes added are not saved yet (stage \"%1\").  Save them first, or close "
           "without saving to drop them.").arg(doc->workingStage().target));
    return true;
}

void KlustersApp::updateDocumentTitle()
{
    if (!doc || doc->url().isEmpty()) { setWindowTitle(QString()); return; }
    if (!doc->inWorkingStage()) { setWindowTitle(doc->documentName()); return; }
    const CluName c = cluName(doc->url());
    const QString target = c.ok ? QFileInfo(cluOfStage(c, doc->workingStage().target)).fileName() : doc->workingStage().target;
    setWindowTitle(tr("%1 (residual spikes added, not saved)").arg(target));
}

bool KlustersApp::addResidualSpikesInWorkingStage(const KlustersDoc::ResidualSpikes& found, const QString& space,
                                                  QString& summary, QMap<int, int>* newClusters)
{
    const QString sourceClu = doc->url();
    const CluName c = cluName(sourceClu);
    if (!c.ok) { summary = tr("Add residual spikes: could not parse the session file names."); return false; }
    const bool replacing = doc->inWorkingStage();
    const KlustersDoc::WorkingStage before = doc->workingStage();
    QString target = replacing ? doc->workingStage().target : c.stage;
    if (target.isEmpty()) {                                     // an untagged session: a new stage
        target = QStringLiteral("rs");
        for (int k = 2; QFileInfo::exists(cluOfStage(c, target)); ++k) target = QStringLiteral("rs%1").arg(k);
    }
    const QString work = target + (replacing && c.stage == target + QStringLiteral("_rw") ? QStringLiteral("_rw2")
                                                                                          : QStringLiteral("_rw"));
    removeStage(c, work);                                       // what a session that ended unsaved left
    QString workClu;
    if (!writeResidualStage(found, workClu, summary, newClusters, work)) { removeStage(c, work); return false; }
    // what is kept by cluster id, not by row: the stage's cluster information, curation log, template marks
    for (const QString& type : {QStringLiteral("cli"), QStringLiteral("cur"), QStringLiteral("templates")}) {
        const QString from = swapType(c, sourceClu, type), to = swapType(c, workClu, type);
        if (QFileInfo::exists(from) && !QFileInfo::exists(to)) QFile::copy(from, to);
    }
    if (replacing) doc->dropWorkingStageOnClose();              // the new working stage holds its rows too
    if (!reopenKeepingState(workClu, space)) {
        if (replacing) doc->setWorkingStage(before.workClu, before.work, before.target);   // still open, still to ask about
        removeStage(c, work);
        summary = tr("Add residual spikes: the document could not be closed; nothing was added.");
        return false;
    }
    if (!doc || doc->url() != workClu) {
        summary += tr("  The working stage \"%1\" could not be opened.").arg(work);
        return false;
    }
    doc->setWorkingStage(workClu, work, target);
    updateDocumentTitle();
    summary.replace(QStringLiteral("Stage \"%1\"").arg(work), tr("Stage \"%1\" (not saved)").arg(target));
    summary += tr("  Save puts them in stage \"%1\"; closing without saving drops them.").arg(target);
    return true;
}

void KlustersApp::finishWorkingStage(QString* log)
{
    if (!doc || !doc->inWorkingStage()) return;
    const KlustersDoc::WorkingStage w = doc->workingStage();
    doc->clearWorkingStage();
    const CluName c = cluName(w.workClu);
    if (!c.ok) return;
    QStringList done;
    if (!w.saved) {
        const int n = removeStage(c, w.work);
        done << tr("working stage \"%1\" dropped (%2 file(s))").arg(w.work).arg(n);
    } else {
        // the working stage's files take the target's names ...
        QSet<QString> replaced;
        const QString tail = QLatin1Char('.') + w.work;
        int moved = 0;
        QStringList failed;
        for (const QString& f : filesOfStage(c, w.work)) {
            const QString to = f.left(f.size() - tail.size()) + QLatin1Char('.') + w.target;
            if (moveOver(f, to)) { replaced.insert(to); ++moved; }
            else failed << QFileInfo(f).fileName();
        }
        // ... and the target's row-indexed files they did not replace no longer match its rows
        QStringList aside;
        for (const QString& f : filesOfStage(c, w.target))
            if (!replaced.contains(f) && rowIndexed(typeOf(c, f)) && moveOver(f, f + QStringLiteral(".stalebkp")))
                aside << QFileInfo(f).fileName();
        removeStage(c, w.work);                                 // what is left of it (.pending, .bak)
        done << tr("stage \"%1\" saved (%2 file(s))").arg(w.target).arg(moved);
        if (!aside.isEmpty()) done << tr("set aside as .stalebkp: %1").arg(aside.join(QStringLiteral(", ")));
        if (!failed.isEmpty()) done << tr("NOT moved: %1").arg(failed.join(QStringLiteral(", ")));
    }
    const QString text = tr("Residual spikes: %1.").arg(done.join(QStringLiteral("; ")));
    qInfo().noquote() << "[residual spikes]" << text;
    if (log) *log = text;
}
