/***************************************************************************
 * realignworker.cpp
 ***************************************************************************/

#include "realignworker.h"
#include "klustersdoc.h"

#include <QThread>
#include <stdexcept>
#include <cstdio>

RealignWorker::RealignWorker(KlustersDoc* doc, int clusterId,
                             const QString& args, QObject* parent)
    : QObject(parent)
    , doc(doc)
    , clusterId(clusterId)
    , args(args)
    , cancelRequested(false)
{}

// Used by ~KlustersApp to stop a still-running worker during shutdown: sets the
// flag the batch loop checks between clusters so the thread can return and be
// joined cleanly.  (There is no user-facing realign abort.)
void RealignWorker::cancel()
{
    cancelRequested = true;
}

void RealignWorker::run()
{
    if (cancelRequested) {
        emit finished(false, 0, 0, {}, {}, {}, 0, 0);
        return;
    }

    const int nChan = doc->data().nbOfChannels();
    const int nSamp = doc->data().nbSamplesPerWaveform();

    // ── Batch mode ─────────────────────────────────────────────────────────
    // Loop realignSpikes over the whole cluster list in this single thread.
    // This is the key fix for PCA-Center Align All: the per-cluster QThread
    // teardown + queued finished() to the GUI thread + next-worker spawn
    // (~450ms/cluster, independent of spike count) collapses to one round-trip
    // for the entire batch.  Per-cluster feedback is emitted via clusterDone.
    if (!clusterIds.isEmpty()) {
        // During a batch, realignSpikes' per-spike stream (e.g. the per-spike
        // --pca-refine detail) is written to stderr only.  Forwarding it to the
        // GUI log panel posts one queued event per spike, which on a full-group
        // Align All (hundreds of thousands of lines) floods the GUI event loop:
        // each line did an O(items) scrollToBottom, so per-line cost grew as the
        // panel filled and the queued clusterDone progress updates piled up
        // behind the backlog — the worker finished the whole list while the
        // progress counter still lagged hundreds of clusters behind.  The GUI
        // panel now gets one header line per cluster (below) plus the final
        // batch summary; error lines are still surfaced to the GUI.
        auto liveLog = [this](const QString& line, bool isError) {
            // Errors always surface to stderr; the per-spike detail only when
            // the Verbose alignment logging preference is on.
            if (isError || verbose)
                std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
        };
        const int total = clusterIds.size();
        int accepted = 0, failed = 0, shiftedTotal = 0;
        for (int k = 0; k < total; ++k) {
            if (cancelRequested) break;
            const int id = clusterIds[k];
            if (verbose)
                std::fprintf(stderr, "--- cluster %d (%d/%d) ---\n",
                             id, k + 1, total);

            QString logOut;
            int nsh = 0, nsw = 0;
            bool ok = false;
            try {
                ok = doc->realignSpikes(id, logOut, nsh, nsw, liveLog, args,
                                          nullptr, nullptr, nullptr);
            } catch (const std::bad_alloc& e) {
                std::fprintf(stderr, "ERROR: out of memory — %s\n",
                             e.what());
            } catch (const std::exception& e) {
                std::fprintf(stderr, "ERROR: exception — %s\n",
                             e.what());
            } catch (...) {
                std::fprintf(stderr,
                             "ERROR: unknown exception in realignSpikes\n");
            }
            // realignSpikes streams its own lines via liveLog; logOut is only
            // populated when no callback is given, so it is normally empty here.
            // Route it the same way (errors to GUI, the rest to stderr).
            if (!logOut.isEmpty()) {
                const QStringList lines =
                    logOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString& line : lines) {
                    const bool isErr = line.startsWith(QLatin1String("ERROR"))
                                    || line.startsWith(QLatin1String("WARNING"));
                    if (isErr || verbose)
                        std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
                }
            }

            if (ok) { ++accepted; shiftedTotal += nsh; }
            else    { ++failed; }
            emit clusterDone(k + 1, total, id, ok, nsh);
        }
        // finished payload in batch mode: ok=true unless cancelled before any
        // work (shutdown); nShifted carries the batch total, nSwapped carries the
        // accepted count.  The GUI finaliser uses its own accumulated counters.
        emit finished(!cancelRequested || accepted > 0, shiftedTotal, accepted,
                      {}, {}, QString(), nChan, nSamp);
        return;
    }

    // ── Single-cluster mode ──────────────────────────────────────────────────
    if (verbose)
        std::fprintf(stderr, "Starting realignment — cluster %d\n", clusterId);

    QString logOut;
    int nShifted = 0;
    int nSwapped = 0;
    bool ok = false;
    QVector<float> meanBefore, meanAfter;
    QString backupBase;

    try {
        ok = doc->realignSpikes(clusterId, logOut, nShifted, nSwapped,
            [this](const QString& line, bool isError) {
                // Errors always surface to stderr; detail only when verbose.
                if (isError || verbose)
                    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
            },
            args,
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
            const bool isError = line.startsWith(QLatin1String("ERROR"))
                              || line.startsWith(QLatin1String("WARNING"));
            if (isError || verbose)
                std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
        }
    }

    emit finished(ok, nShifted, nSwapped, meanBefore, meanAfter, backupBase, nChan, nSamp);
}
