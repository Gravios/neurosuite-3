/***************************************************************************
 *  curationlogger.h  —  JSON-Lines audit log for Klusters manual curation
 *
 *  Records cluster characteristic snapshots before and after every curation
 *  action (group, split, recluster, realign, nudge, delete-to-noise/artefact).
 *  The resulting .jl file is human-readable and directly importable into
 *  pandas/scikit-learn for empirical decision-tree training.
 *
 *  Log format: one JSON object per line, UTF-8.
 *  File location: <sessionDir>/<baseName>.curation_log.<electrodeGroup>.jl
 *
 *  Typical workflow:
 *    1. KlustersDoc::openDocument()  → CurationLogger::open()
 *    2. Before each action           → CurationLogger::beginAction()
 *    3. Doc modifies data in-place
 *    4. After each action            → CurationLogger::commitAction()
 *    5. KlustersDoc::closeDocument() → CurationLogger::close()
 ***************************************************************************/

#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QVector>
#include <QFile>
#include <QTextStream>

// ---------------------------------------------------------------------------
// ClusterSnapshot — all metrics computable from the in-memory Data state
// ---------------------------------------------------------------------------
/** Immutable snapshot of a cluster's observable characteristics.
 *  Computed by Data::computeSnapshot() before and after each curation action
 *  so the before/after pair can be used as a training example for an
 *  automatic decision tree.
 *
 *  Groups of fields:
 *    A. Spike count / rate
 *    B. ISI distribution (full shape, not just one threshold)
 *    C. Feature-space spread (variance moments)
 *    D. Higher-order feature moments (skewness, kurtosis → bimodality)
 *    E. Temporal drift and rate stability
 *    F. Global context (fraction of total spikes, cluster count)
 *    G. Nearest-cluster isolation (inter-cluster distance)
 *    H. Waveform morphology (optional — only when cache is warm)
 *    I. Session context (fixed per open document)
 */
struct ClusterSnapshot
{
    int     clusterId      = -1;

    // ── A. Count / rate ───────────────────────────────────────────────────
    qint64  nSpikes        = 0;
    /// nSpikes / (t_last − t_first) in Hz
    double  firingRateHz   = 0.0;

    // ── B. ISI distribution ───────────────────────────────────────────────
    double  isiThreshMs    = 3.0;   ///< primary refractory threshold used
    double  isiViolPct     = 0.0;   ///< % ISI < isiThreshMs   (3 ms default)
    double  isiViolPct1ms  = 0.0;   ///< % ISI < 1 ms  (hard refractory)
    double  isiViolPct2ms  = 0.0;   ///< % ISI < 2 ms
    double  isiBurstPct    = 0.0;   ///< % ISI < 10 ms (burst)
    double  isiMeanMs      = 0.0;   ///< mean ISI in ms
    double  isiMedianMs    = 0.0;   ///< median ISI in ms
    /// Coefficient of variation of ISI.
    /// ~1 = Poisson; >2 = bursty; <0.5 = pacemaker / interneuron
    double  isiCv          = 0.0;

    // ── C. Feature-space spread (2nd moment) ──────────────────────────────
    /// Mean of per-feature sample variances; proxy for overall compactness.
    double  featVarMean      = 0.0;
    /// sqrt(Σ var_i) — Frobenius radius in feature space.
    double  featVarFrobenius = 0.0;
    /// Mean of the top-3 per-feature variances (dominant axes).
    double  featVarTop3Mean  = 0.0;
    /// max(var_i) / max(1, min(var_i)) — elongation / anisotropy ratio.
    double  driftRatio       = 0.0;
    /// Per-feature sample variances (up to kMaxLoggedDims entries).
    QVector<double> featVarDims;
    static constexpr int kMaxLoggedDims = 32;

    // ── D. Higher-order moments (bimodality indicators) ───────────────────
    /// Max |skewness| across feature dimensions.
    /// Non-zero skew on a PCA dim → one tail stretched toward a minor cluster.
    double  featSkewnessMax  = 0.0;
    /// Mean excess kurtosis across dims (0 = Gaussian; >0 = heavy tails).
    double  featKurtosisMean = 0.0;
    /// Max excess kurtosis across dims.
    /// Positive → outlier spikes (artefact); negative → platykurtic → bimodal candidate.
    double  featKurtosisMax  = 0.0;

    // ── E. Temporal stability ─────────────────────────────────────────────
    /// L2 distance between first-half and second-half feature centroids,
    /// normalised by featVarFrobenius.  > 0.3 → waveform drift → REALIGN.
    double  temporalDriftIndex = 0.0;
    /// CV of spike count across 10 equal time bins.
    /// ~0 = stable rate; >1 = sporadic / artefact-like.
    double  temporalRateCv     = 0.0;

    // ── F. Global context ─────────────────────────────────────────────────
    /// nSpikes / total_spikes_in_group.  Very small → likely noise.
    double  spikeFraction      = 0.0;
    /// Number of active clusters in this electrode group at snapshot time.
    int     nClustersInGroup   = 0;

    // ── G. Nearest-cluster isolation ──────────────────────────────────────
    int     nearestClusterId        = -1;
    /// L2 centroid distance to nearest other cluster in feature space.
    double  nearestCentroidDist     = 0.0;
    /// Same, normalised by this cluster's featVarFrobenius.
    /// < 1.0 → neighbour sits inside this cluster's own radius → GROUP candidate.
    double  nearestCentroidDistNorm = 0.0;

    // ── H. Waveform morphology (conditional on cache) ─────────────────────
    bool    waveformAvailable  = false;
    /// Peak-to-trough / (2 × pre-spike baseline RMS) on the best channel.
    double  waveformSnr        = 0.0;
    /// Peak-to-trough amplitude in raw ADC units on the highest-amplitude channel.
    double  waveformPeakAmp    = 0.0;
    /// Trough-to-repolarisation width in samples on the best channel.
    int     waveformWidthSamp  = 0;
    /// (pos_peak + neg_trough) / (pos_peak − neg_trough).
    /// ≈ 0 = symmetric; negative = dominant trough (pyramidal-like);
    /// positive = dominant peak (interneuron-like after repol).
    double  waveformAsymmetry  = 0.0;
    /// Number of channels with peak-to-trough > 25 % of the best channel.
    int     waveformChanSpread = 0;

    // ── I. Session context ────────────────────────────────────────────────
    int     nChannels      = 0;
    int     nPcaDims       = 0;
    double  samplingRateHz = 0.0;

    // ── J. Isolation quality (L-ratio / isolation distance) ──────────────
    /// L-ratio: Schmitzer-Torbert et al. (2005).
    /// Sum of chi²_sf(d²_mahal, D) over all non-cluster spikes / n_cluster.
    /// Lower is better.  > 0.05 → significant contamination.
    double  lRatio           = 0.0;
    /// Mahalanobis distance (diagonal approx.) to the K-th nearest
    /// non-cluster spike, where K = n_spikes_in_this_cluster.
    /// Higher is better.  The original unit-isolation threshold is ~20.
    double  isolationDist    = 0.0;

    // ── K. Recording-relative temporal position ───────────────────────────
    /// Time of first spike as fraction of total recording duration [0–1].
    double  tFirstRel        = 0.0;
    /// Time of last spike as fraction of total recording duration [0–1].
    double  tLastRel         = 0.0;

    // ── L. Session action history ─────────────────────────────────────────
    /// Number of prior curation actions that touched this cluster in the
    /// current session.  0 = untouched since document open.
    int     actionHistoryDepth = 0;
};

// ---------------------------------------------------------------------------
// CurationLogger
// ---------------------------------------------------------------------------
class CurationLogger
{
public:
    // Action taxonomy for the decision-tree label column
    enum class ActionType : int {
        GROUP           = 0,  ///< Merge N clusters → 1
        SPLIT           = 1,  ///< Polygon → extract 1 new cluster from 1 source
        SPLIT_N         = 2,  ///< Polygon → extract N new clusters (one per orig)
        RECLUSTER       = 3,  ///< KlustaKwik re-run:  M source → N result
        REALIGN         = 4,  ///< Spike waveform realignment, in-place
        NUDGE           = 5,  ///< Timestamp ±1 sample shift, in-place
        DELETE_NOISE    = 6,  ///< Move entire cluster → cluster 1
        DELETE_ARTEFACT = 7,  ///< Move entire cluster → cluster 0
        DELETE_REGION_NOISE    = 8, ///< Region selection moved → cluster 1
        DELETE_REGION_ARTEFACT = 9, ///< Region selection moved → cluster 0
        MOVE_SPIKES     = 10, ///< Manual subset reassignment between clusters
        UNDO            = 11, ///< Curator reverted the preceding action
        REDO            = 12, ///< Curator re-applied an undone action
        REORDER_PALETTE = 13, ///< Cluster moved to end of palette (T key)
    };

    CurationLogger();
    ~CurationLogger();

    // Disable copy; logger owns a file handle
    CurationLogger(const CurationLogger&)            = delete;
    CurationLogger& operator=(const CurationLogger&) = delete;

    /** Open a new log file for the given session.
     *  @param logPath         Full path for the .jl output file.
     *  @param sessionBaseName Base name shown in every line's "file" field.
     *  @param electrodeGroup  Electrode group ID string (e.g. "1").
     *  @param samplingRateHz  Sampling frequency in Hz.
     *  @param nChannels       Channels in this electrode group.
     *  @param nPcaDims        Total PCA feature dimensions (excl. timestamp).
     */
    void open(const QString& logPath,
              const QString& sessionBaseName,
              const QString& electrodeGroup,
              double         samplingRateHz,
              int            nChannels,
              int            nPcaDims);

    /** Flush and close the log file. */
    void close();

    bool isOpen() const { return m_file.isOpen(); }

    // ------------------------------------------------------------------
    // Action logging interface
    //
    // Call beginAction() with before-snapshots, perform the data mutation,
    // then call commitAction() with after-snapshots.  The same action_idx
    // links the two sets of lines so they can be joined in pandas.
    // ------------------------------------------------------------------

    /** Record the "before" snapshots for an upcoming action.
     *  Increments the internal action counter; returns the index assigned
     *  to this action (useful for associating with UI feedback).
     */
    int  beginAction(ActionType type, const QList<ClusterSnapshot>& before);

    /** Record the "after" snapshots once the action has completed.
     *  Must follow a corresponding beginAction() call.
     *  @param after  Snapshots of all clusters produced/modified by the action.
     */
    void commitAction(const QList<ClusterSnapshot>& after);

    // Convenience: single-cluster wrappers
    int  beginAction(ActionType type, const ClusterSnapshot& before);
    void commitAction(const ClusterSnapshot& after);

    /** Append a curator quality annotation to the most recently begun action.
     *  @param quality   0 = bad/exploratory, 1 = uncertain, 2 = confident/good.
     *  Call after beginAction() at any time before the next beginAction().
     *  These records are the primary supervised signal for decision-tree training.
     */
    void annotateLastAction(int quality);

    /** Append an algorithm-specific detail record to the most recently begun
     *  action.  Used by automated curation tools (DipSplit, Realign, …) to
     *  log the parameters they were called with and the metrics that drove
     *  their accept/reject decision.  Each value is JSON-serialised
     *  according to its QVariant type (int → number, double → number,
     *  bool → true/false, anything else → quoted string).
     *
     *  Emits a single ACTION_DETAIL JSON-line record keyed by the most
     *  recent action_idx.  Safe to call multiple times per action — each
     *  call produces an independent record.
     */
    void recordActionDetails(const QMap<QString, QVariant>& details);

    /** Log an UNDO or REDO event referencing the action index it reverts/replays.
     *  @param type         Must be ActionType::UNDO or ActionType::REDO.
     *  @param targetIdx    action_idx of the action being reverted / replayed.
     *  @param clusterState Current snapshot of the clusters affected.
     */
    void logUndoRedo(ActionType type, int targetIdx,
                     const QList<ClusterSnapshot>& clusterState);

    static QString actionName(ActionType t);

private:
    void writeLine(const QString& phase,
                   const QString& role,
                   const ClusterSnapshot& snap);

    QString jsonEscape(const QString& s) const;

    QFile       m_file;
    QTextStream m_out;

    QString m_sessionBaseName;
    QString m_electrodeGroup;
    QString m_sessionId;    ///< UUID-like token, unique per open()
    int     m_actionIdx = 0;
    int     m_currentAction = -1; ///< ActionType cast to int
};
