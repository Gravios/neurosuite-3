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
        emit finished(false, 0, 0);
        return;
    }

    emit logLine(QStringLiteral("Starting realignment — cluster %1").arg(m_clusterId),
                 false);

    QString logOut;
    int nShifted = 0;
    int nSwapped = 0;
    bool ok = false;
    try {
        ok = m_doc->realignSpikes(m_clusterId, logOut, nShifted, nSwapped,
            [this](const QString& line, bool isError) {
                emit logLine(line, isError);
            }, m_args);
    } catch (const std::bad_alloc& e) {
        logOut += QStringLiteral("\nERROR: out of memory — %1\n").arg(
            QString::fromLatin1(e.what()));
    } catch (const std::exception& e) {
        logOut += QStringLiteral("\nERROR: exception — %1\n").arg(
            QString::fromLatin1(e.what()));
    } catch (...) {
        logOut += QStringLiteral("\nERROR: unknown exception in realignSpikes\n");
    }

    // If live logging was used, logOut is empty on success (all lines were emitted
    // live).  On exception, the catch blocks wrote to logOut directly — emit those.
    if (!logOut.isEmpty()) {
        const QStringList lines = logOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            bool isError = line.startsWith(QLatin1String("ERROR"))
                        || line.startsWith(QLatin1String("WARNING"));
            emit logLine(line, isError);
        }
    }

    emit finished(ok, nShifted, nSwapped);
}
