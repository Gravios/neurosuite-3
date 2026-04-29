/***************************************************************************
 *  curationlogger.cpp  —  JSON-Lines curation audit log
 *
 *  See header doc-comment for the full lifecycle.  Implementation note:
 *  every disk write is deferred until either an overflowing beginAction()
 *  pushes the oldest entry out of the in-memory ring or close() is called.
 *  This is intentional — the on-disk log captures only the user's
 *  finalised curatorial decisions, not their tentative undo/redo dance.
 ***************************************************************************/

#include "curationlogger.h"

#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <QtMath>
#include <cmath>

// ---------------------------------------------------------------------------
CurationLogger::CurationLogger()
    : m_out(&m_file)
{
}

CurationLogger::~CurationLogger()
{
    close();
}

// ---------------------------------------------------------------------------
void CurationLogger::open(const QString& logPath,
                           const QString& sessionBaseName,
                           const QString& electrodeGroup,
                           double         samplingRateHz,
                           int            nChannels,
                           int            nPcaDims)
{
    close();

    m_sessionBaseName = sessionBaseName;
    m_electrodeGroup  = electrodeGroup;
    m_actionIdx       = 0;
    m_pending.clear();

    // Generate a short session token (first 8 hex chars of a UUID)
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    m_file.setFileName(logPath);
    if (!m_file.open(QIODevice::Append | QIODevice::Text)) {
        qWarning("CurationLogger: cannot open log file %s",
                 qPrintable(logPath));
        return;
    }
    m_out.setDevice(&m_file);
    m_out.setEncoding(QStringConverter::Utf8);

    // Write a session-open sentinel — valid JSON object, easy to filter in pandas.
    m_out << "{"
          << "\"event\":\"SESSION_OPEN\","
          << "\"session_id\":\"" << m_sessionId << "\","
          << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\","
          << "\"file\":\"" << jsonEscape(sessionBaseName) << "\","
          << "\"group\":\"" << jsonEscape(electrodeGroup) << "\","
          << "\"sample_rate\":" << samplingRateHz << ","
          << "\"n_channels\":" << nChannels << ","
          << "\"n_pca_dims\":" << nPcaDims
          << "}\n";
    m_out.flush();
}

// ---------------------------------------------------------------------------
void CurationLogger::close()
{
    if (!m_file.isOpen())
        return;

    // Flush every remaining pending entry with its current status (the
    // user's "save followed by quit" — we capture exactly the decisions
    // they ended the session with).
    while (!m_pending.isEmpty())
        flushOldest();

    m_out << "{"
          << "\"event\":\"SESSION_CLOSE\","
          << "\"session_id\":\"" << m_sessionId << "\","
          << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\","
          << "\"n_actions\":" << m_actionIdx
          << "}\n";
    m_out.flush();
    m_file.close();
}

// ---------------------------------------------------------------------------
void CurationLogger::setMaxBufferEntries(int n)
{
    m_maxBuffer = (n < 1) ? 1 : n;
    // Shrinking: flush from the front until size <= m_maxBuffer.
    while (m_pending.size() > m_maxBuffer)
        flushOldest();
}

// ---------------------------------------------------------------------------
int CurationLogger::beginAction(ActionType type,
                                 const QList<ClusterSnapshot>& before)
{
    if (!m_file.isOpen())
        return m_actionIdx;

    PendingEntry e;
    e.actionIdx = m_actionIdx++;
    e.type      = type;
    e.before    = before;
    e.status    = QStringLiteral("good");
    e.tsBegin   = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    m_pending.append(e);

    // Overflow: the oldest entry can no longer be undone (it's beyond
    // the buffer's capacity = max-undos).  Its current status is now
    // its final status; write it out and free the slot.
    while (m_pending.size() > m_maxBuffer)
        flushOldest();

    return e.actionIdx;
}

int CurationLogger::beginAction(ActionType type, const ClusterSnapshot& before)
{
    return beginAction(type, QList<ClusterSnapshot>{ before });
}

void CurationLogger::commitAction(const QList<ClusterSnapshot>& after)
{
    if (PendingEntry* p = currentPending())
        p->after = after;
}

void CurationLogger::commitAction(const ClusterSnapshot& after)
{
    commitAction(QList<ClusterSnapshot>{ after });
}

// ---------------------------------------------------------------------------
void CurationLogger::recordActionDetails(const QMap<QString, QVariant>& details)
{
    if (details.isEmpty()) return;
    if (PendingEntry* p = currentPending())
        p->details.append(details);
}

// ---------------------------------------------------------------------------
// Undo / redo just flip status — they don't write anything to disk.  The
// status flip travels with the entry until it eventually leaves the buffer
// (overflow or close), at which point the on-disk record locks in the
// final good/bad label.
// ---------------------------------------------------------------------------
void CurationLogger::notifyUndo()
{
    // Walk back from the most recent entry; flip the first one whose
    // status is still "good".
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        if (m_pending[i].status == QLatin1String("good")) {
            m_pending[i].status = QStringLiteral("bad");
            return;
        }
    }
    // All buffered entries already bad.  The data state is being reverted
    // past the buffer boundary; the corresponding records were finalised
    // and emitted to disk at overflow time, so we cannot retroactively
    // re-status them.  This is a tolerated edge case at deep undo chains.
}

void CurationLogger::notifyRedo()
{
    // Walk forward from the oldest; flip the most recent (= largest i)
    // entry whose status is "bad" back to "good".  Equivalent to the
    // mirror of notifyUndo: the redo replays the most recently undone
    // action, which is the topmost "bad" entry.
    int target = -1;
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        if (m_pending[i].status == QLatin1String("bad")) {
            target = i;
            break;
        }
    }
    if (target >= 0)
        m_pending[target].status = QStringLiteral("good");
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

CurationLogger::PendingEntry* CurationLogger::currentPending()
{
    if (m_pending.isEmpty() || !m_file.isOpen())
        return nullptr;
    return &m_pending.last();
}

void CurationLogger::flushOldest()
{
    if (m_pending.isEmpty()) return;
    flushEntry(m_pending.first());
    m_pending.removeFirst();
}

// Emit every accumulated line for one entry, all carrying the same
// status.  The order on disk is:
//   1. Before-snapshot lines (one per source cluster)
//   2. After-snapshot lines  (one per result cluster)
//   3. Detail lines          (zero or more, one per recordActionDetails call)
// Putting the "before"+"after" lines together (rather than chronological
// begin/details/commit) is fine for ML loaders that join by action_idx,
// and lets us emit everything in one tight burst per entry.
void CurationLogger::flushEntry(const PendingEntry& e)
{
    if (!m_file.isOpen()) return;

    for (const ClusterSnapshot& s : e.before)
        writeLine("before", "source", s, e.actionIdx, e.type, e.status);
    for (const ClusterSnapshot& s : e.after)
        writeLine("after",  "result", s, e.actionIdx, e.type, e.status);

    // Detail lines: ACTION_DETAIL records keyed by the same action_idx
    // and carrying the same status field.
    const QString actionStr = actionName(e.type);
    for (const QMap<QString, QVariant>& details : e.details) {
        m_out << "{"
              << "\"event\":\"ACTION_DETAIL\","
              << "\"session_id\":\"" << m_sessionId << "\","
              << "\"ts_begin\":\"" << e.tsBegin << "\","
              << "\"action\":\"" << actionStr << "\","
              << "\"action_idx\":" << e.actionIdx << ","
              << "\"status\":\"" << e.status << "\"";

        for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
            m_out << ",\"" << jsonEscape(it.key()) << "\":";
            const QVariant& v = it.value();
            switch (v.typeId()) {
                case QMetaType::Int:
                case QMetaType::UInt:
                case QMetaType::LongLong:
                case QMetaType::ULongLong:
                    m_out << v.toLongLong();
                    break;
                case QMetaType::Double:
                case QMetaType::Float: {
                    const double d = v.toDouble();
                    if (std::isfinite(d))
                        m_out << QString::number(d, 'g', 15);
                    else
                        m_out << "null";
                    break;
                }
                case QMetaType::Bool:
                    m_out << (v.toBool() ? "true" : "false");
                    break;
                default:
                    m_out << "\"" << jsonEscape(v.toString()) << "\"";
                    break;
            }
        }
        m_out << "}\n";
    }
    m_out.flush();
}

// ---------------------------------------------------------------------------
void CurationLogger::writeLine(const QString& phase,
                                const QString& role,
                                const ClusterSnapshot& s,
                                int aidx,
                                ActionType action,
                                const QString& status)
{
    m_out << "{"
          // ── context ──────────────────────────────────────────────────────
          << "\"session_id\":\"" << m_sessionId << "\","
          << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\","
          << "\"file\":\"" << jsonEscape(m_sessionBaseName) << "\","
          << "\"group\":\"" << jsonEscape(m_electrodeGroup) << "\","
          // ── action ───────────────────────────────────────────────────────
          << "\"action\":\"" << actionName(action) << "\","
          << "\"action_idx\":" << aidx << ","
          << "\"status\":\"" << status << "\","
          << "\"phase\":\"" << phase << "\","
          << "\"role\":\"" << role << "\","
          << "\"cluster\":" << s.clusterId << ","
          // ── A. count / rate ───────────────────────────────────────────────
          << "\"n_spikes\":" << s.nSpikes << ","
          << "\"firing_hz\":" << s.firingRateHz << ","
          // ── B. ISI distribution ───────────────────────────────────────────
          << "\"isi_thresh_ms\":" << s.isiThreshMs << ","
          << "\"isi_viol_pct\":" << s.isiViolPct << ","
          << "\"isi_viol_pct_1ms\":" << s.isiViolPct1ms << ","
          << "\"isi_viol_pct_2ms\":" << s.isiViolPct2ms << ","
          << "\"isi_burst_pct\":" << s.isiBurstPct << ","
          << "\"isi_mean_ms\":" << s.isiMeanMs << ","
          << "\"isi_median_ms\":" << s.isiMedianMs << ","
          << "\"isi_cv\":" << s.isiCv << ","
          // ── C. Feature-space spread ───────────────────────────────────────
          << "\"feat_var_mean\":" << s.featVarMean << ","
          << "\"feat_var_frobenius\":" << s.featVarFrobenius << ","
          << "\"feat_var_top3_mean\":" << s.featVarTop3Mean << ","
          << "\"drift_ratio\":" << s.driftRatio << ","
          // ── D. Higher-order moments ───────────────────────────────────────
          << "\"feat_skewness_max\":" << s.featSkewnessMax << ","
          << "\"feat_kurtosis_mean\":" << s.featKurtosisMean << ","
          << "\"feat_kurtosis_max\":" << s.featKurtosisMax << ","
          // ── E. Temporal stability ─────────────────────────────────────────
          << "\"temporal_drift_index\":" << s.temporalDriftIndex << ","
          << "\"temporal_rate_cv\":" << s.temporalRateCv << ","
          // ── F. Global context ─────────────────────────────────────────────
          << "\"spike_fraction\":" << s.spikeFraction << ","
          << "\"n_clusters_in_group\":" << s.nClustersInGroup << ","
          // ── G. Nearest-cluster isolation ──────────────────────────────────
          << "\"nearest_cluster_id\":" << s.nearestClusterId << ","
          << "\"nearest_centroid_dist\":" << s.nearestCentroidDist << ","
          << "\"nearest_centroid_dist_norm\":" << s.nearestCentroidDistNorm << ","
          // ── H. Waveform morphology ────────────────────────────────────────
          << "\"waveform_available\":" << (s.waveformAvailable ? "true" : "false") << ","
          << "\"waveform_snr\":" << s.waveformSnr << ","
          << "\"waveform_peak_amp\":" << s.waveformPeakAmp << ","
          << "\"waveform_width_samp\":" << s.waveformWidthSamp << ","
          << "\"waveform_asymmetry\":" << s.waveformAsymmetry << ","
          << "\"waveform_chan_spread\":" << s.waveformChanSpread << ","
          // ── per-dimension detail ──────────────────────────────────────────
          << "\"n_feat_dims\":" << s.featVarDims.size() << ","
          << "\"feat_var_dims\":[";
    for (int i = 0; i < s.featVarDims.size(); ++i) {
        if (i) m_out << ",";
        m_out << s.featVarDims[i];
    }
    m_out << "],"
          // ── I. session context ────────────────────────────────────────────
          << "\"n_channels\":" << s.nChannels << ","
          << "\"n_pca_dims\":" << s.nPcaDims << ","
          << "\"sample_rate\":" << s.samplingRateHz << ","
          // ── J. Isolation quality ──────────────────────────────────────────
          << "\"l_ratio\":" << s.lRatio << ","
          << "\"isolation_dist\":" << s.isolationDist << ","
          // ── K. Recording-relative temporal position ───────────────────────
          << "\"t_first_rel\":" << s.tFirstRel << ","
          << "\"t_last_rel\":" << s.tLastRel << ","
          // ── L. Session action history ─────────────────────────────────────
          << "\"action_history_depth\":" << s.actionHistoryDepth
          << "}\n";
}

QString CurationLogger::jsonEscape(const QString& s) const
{
    QString out;
    out.reserve(s.size() + 4);
    for (const QChar c : s) {
        if      (c == QLatin1Char('\\')) out += QLatin1String("\\\\");
        else if (c == QLatin1Char('"'))  out += QLatin1String("\\\"");
        else if (c == QLatin1Char('\n')) out += QLatin1String("\\n");
        else if (c == QLatin1Char('\r')) out += QLatin1String("\\r");
        else if (c == QLatin1Char('\t')) out += QLatin1String("\\t");
        else                             out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
QString CurationLogger::actionName(ActionType t)
{
    switch (t) {
    case ActionType::GROUP:                  return QStringLiteral("GROUP");
    case ActionType::SPLIT:                  return QStringLiteral("SPLIT");
    case ActionType::SPLIT_N:                return QStringLiteral("SPLIT_N");
    case ActionType::RECLUSTER:              return QStringLiteral("RECLUSTER");
    case ActionType::REALIGN:                return QStringLiteral("REALIGN");
    case ActionType::NUDGE:                  return QStringLiteral("NUDGE");
    case ActionType::DELETE_NOISE:           return QStringLiteral("DELETE_NOISE");
    case ActionType::DELETE_ARTEFACT:        return QStringLiteral("DELETE_ARTEFACT");
    case ActionType::DELETE_REGION_NOISE:    return QStringLiteral("DELETE_REGION_NOISE");
    case ActionType::DELETE_REGION_ARTEFACT: return QStringLiteral("DELETE_REGION_ARTEFACT");
    case ActionType::MOVE_SPIKES:            return QStringLiteral("MOVE_SPIKES");
    case ActionType::UNDO:                   return QStringLiteral("UNDO");
    case ActionType::REDO:                   return QStringLiteral("REDO");
    case ActionType::RENUMBER_PARTIAL:       return QStringLiteral("RENUMBER_PARTIAL");
    case ActionType::WATERSHED:              return QStringLiteral("WATERSHED");
    }
    return QStringLiteral("UNKNOWN");
}
