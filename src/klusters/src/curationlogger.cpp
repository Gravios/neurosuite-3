/***************************************************************************
 *  curationlogger.cpp  —  JSON-Lines curation audit log
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
    m_currentAction   = -1;

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
int CurationLogger::beginAction(ActionType type,
                                 const QList<ClusterSnapshot>& before)
{
    if (!m_file.isOpen())
        return m_actionIdx;

    m_currentAction = static_cast<int>(type);
    const int idx   = m_actionIdx++;

    for (const ClusterSnapshot& snap : before)
        writeLine("before", "source", snap);

    m_out.flush();
    return idx;
}

int CurationLogger::beginAction(ActionType type, const ClusterSnapshot& before)
{
    return beginAction(type, QList<ClusterSnapshot>{ before });
}

void CurationLogger::commitAction(const QList<ClusterSnapshot>& after)
{
    if (!m_file.isOpen())
        return;
    for (const ClusterSnapshot& snap : after)
        writeLine("after", "result", snap);
    m_out.flush();
}

void CurationLogger::commitAction(const ClusterSnapshot& after)
{
    commitAction(QList<ClusterSnapshot>{ after });
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void CurationLogger::writeLine(const QString& phase,
                                const QString& role,
                                const ClusterSnapshot& s)
{
    const int aidx = m_actionIdx - 1;

    m_out << "{"
          // ── context ──────────────────────────────────────────────────────
          << "\"session_id\":\"" << m_sessionId << "\","
          << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\","
          << "\"file\":\"" << jsonEscape(m_sessionBaseName) << "\","
          << "\"group\":\"" << jsonEscape(m_electrodeGroup) << "\","
          // ── action ───────────────────────────────────────────────────────
          << "\"action\":\"" << actionName(static_cast<ActionType>(m_currentAction)) << "\","
          << "\"action_idx\":" << aidx << ","
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
    case ActionType::REORDER_PALETTE:        return QStringLiteral("REORDER_PALETTE");
    }
    return QStringLiteral("UNKNOWN");
}

// ---------------------------------------------------------------------------
void CurationLogger::annotateLastAction(int quality)
{
    if (!m_file.isOpen()) return;
    const int aidx = m_actionIdx - 1;   // most recently begun action
    m_out << "{"
          << "\"event\":\"ANNOTATE\","
          << "\"session_id\":\"" << m_sessionId << "\","
          << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\","
          << "\"action_idx\":" << aidx << ","
          << "\"quality\":" << quality
          << "}\n";
    m_out.flush();
}

void CurationLogger::recordActionDetails(const QMap<QString, QVariant>& details)
{
    if (!m_file.isOpen() || details.isEmpty()) return;
    const int aidx = m_actionIdx - 1;   // most recently begun action

    m_out << "{"
          << "\"event\":\"ACTION_DETAIL\","
          << "\"session_id\":\"" << m_sessionId << "\","
          << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\","
          << "\"action_idx\":" << aidx;

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
                // Use 'g' format — strips trailing zeroes, preserves
                // ~15 sig figs.  NaN/Inf are not valid JSON; emit null.
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
    m_out.flush();
}

void CurationLogger::logUndoRedo(ActionType type, int targetIdx,
                                  const QList<ClusterSnapshot>& clusterState)
{
    if (!m_file.isOpen()) return;
    m_currentAction = static_cast<int>(type);
    const int idx   = m_actionIdx++;

    // Write a single control line identifying which action was reverted/replayed
    m_out << "{"
          << "\"session_id\":\"" << m_sessionId << "\","
          << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\","
          << "\"action\":\"" << actionName(type) << "\","
          << "\"action_idx\":" << idx << ","
          << "\"target_action_idx\":" << targetIdx
          << "}\n";

    // Snapshot current state of affected clusters as the "after" state
    for (const ClusterSnapshot& snap : clusterState)
        writeLine("after", "result", snap);

    m_out.flush();
}
