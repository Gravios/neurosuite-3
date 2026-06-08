/***************************************************************************
 * realignworker.cpp
 ***************************************************************************/

#include "realignworker.h"
#include "klustersdoc.h"

#include <QThread>
#include <stdexcept>

RealignWorker::RealignWorker(KlustersDoc* doc, int clusterId,
                             const QString& args, QObject* parent)
    : QObject(parent)
    , m_doc(doc)
    , m_clusterId(clusterId)
    , m_args(args)
    , m_cancel(false)
{}

void RealignWorker::cancel()
{
    m_cancel = true;
}

void RealignWorker::run()
{
    if (m_cancel) {
        emit finished(false, 0, 0, {}, {}, {}, 0, 0);
        return;
    }

    const int nChan = m_doc->data().nbOfChannels();
    const int nSamp = m_doc->data().nbSamplesPerWaveform();

    // ── Batch mode ─────────────────────────────────────────────────────────
    // Loop realignSpikes over the whole cluster list in this single thread.
    // This is the key fix for PCA-Center Align All: the per-cluster QThread
    // teardown + queued finished() to the GUI thread + next-worker spawn
    // (~450ms/cluster, independent of spike count) collapses to one round-trip
    // for the entire batch.  Per-cluster feedback is emitted via clusterDone.
    if (!m_clusterIds.isEmpty()) {
        auto liveLog = [this](const QString& line, bool isError) {
            emit logLine(line, isError);
        };
        const int total = m_clusterIds.size();
        int accepted = 0, failed = 0, shiftedTotal = 0;
        for (int k = 0; k < total; ++k) {
            if (m_cancel) break;
            const int id = m_clusterIds[k];
            emit logLine(QStringLiteral("--- cluster %1 (%2/%3) ---")
                         .arg(id).arg(k + 1).arg(total), false);

            QString logOut;
            int nsh = 0, nsw = 0;
            bool ok = false;
            try {
                ok = m_doc->realignSpikes(id, logOut, nsh, nsw, liveLog, m_args,
                                          nullptr, nullptr, nullptr);
            } catch (const std::bad_alloc& e) {
                emit logLine(QStringLiteral("ERROR: out of memory — %1")
                             .arg(QString::fromLatin1(e.what())), true);
            } catch (const std::exception& e) {
                emit logLine(QStringLiteral("ERROR: exception — %1")
                             .arg(QString::fromLatin1(e.what())), true);
            } catch (...) {
                emit logLine(QStringLiteral("ERROR: unknown exception in realignSpikes"),
                             true);
            }
            // realignSpikes streams its own lines via liveLog; logOut is only
            // populated when no callback is given, so it is normally empty here.
            if (!logOut.isEmpty()) {
                const QStringList lines =
                    logOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString& line : lines)
                    emit logLine(line, line.startsWith(QLatin1String("ERROR"))
                                    || line.startsWith(QLatin1String("WARNING")));
            }

            if (ok) { ++accepted; shiftedTotal += nsh; }
            else    { ++failed; }
            emit clusterDone(k + 1, total, id, ok, nsh);
        }
        // finished payload in batch mode: ok=true unless cancelled before any
        // work; nShifted carries the batch total, nSwapped carries the accepted
        // count.  The GUI finaliser uses its own accumulated counters.
        emit finished(!m_cancel || accepted > 0, shiftedTotal, accepted,
                      {}, {}, QString(), nChan, nSamp);
        return;
    }

    // ── Single-cluster mode (unchanged) ──────────────────────────────────────
    emit logLine(QStringLiteral("Starting realignment — cluster %1").arg(m_clusterId),
                 false);

    QString logOut;
    int nShifted = 0;
    int nSwapped = 0;
    bool ok = false;
    QVector<float> meanBefore, meanAfter;
    QString backupBase;

    try {
        ok = m_doc->realignSpikes(m_clusterId, logOut, nShifted, nSwapped,
            [this](const QString& line, bool isError) {
                emit logLine(line, isError);
            },
            m_args,
            &meanBefore, &meanAfter, &backupBase);
    } catch (const std::bad_alloc& e) {
        logOut += QStringLiteral("\nERROR: out of memory — %1\n").arg(
            QString::fromLatin1(e.what()));
    } catch (const std::exception& e) {
        logOut += QStringLiteral("\nERROR: exception — %1\n").arg(
            QString::fromLatin1(e.what()));
    } catch (...) {
        logOut += QStringLiteral("\nERROR: unknown exception in realignSpikes\n");
    }

    if (!logOut.isEmpty()) {
        const QStringList lines = logOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            bool isError = line.startsWith(QLatin1String("ERROR"))
                        || line.startsWith(QLatin1String("WARNING"));
            emit logLine(line, isError);
        }
    }

    emit finished(ok, nShifted, nSwapped, meanBefore, meanAfter, backupBase, nChan, nSamp);
}
