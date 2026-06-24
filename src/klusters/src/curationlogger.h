/***************************************************************************
 *  curationlogger.h  —  JSON-Lines audit log for Klusters manual curation
 *
 *  Records cluster characteristic snapshots before and after every curation
 *  action (group, split, recluster, realign, nudge, delete-to-noise/artefact).
 *  The resulting .jl file is human-readable and directly importable into
 *  pandas/scikit-learn for empirical decision-tree training.
 *
 *  Log format: one JSON object per line, UTF-8.
 *  File location: <sessionDir>/<baseName>.curation_log.<electrodeGroup>.<stage>
 *  where <stage> is the .clu variant (e.g. "stderiv"). The untagged default
 *  stage ("standard") is not logged.
 *
 *  Lifecycle (deferred-flush model):
 *    1. KlustersDoc::openDocument()   → CurationLogger::open()
 *    2. Before each action            → CurationLogger::beginAction()
 *    3. Doc modifies data in-place
 *    4. After each action             → CurationLogger::commitAction()
 *    5. Optional details              → CurationLogger::recordActionDetails()
 *    6. User Ctrl+Z                   → CurationLogger::notifyUndo()
 *    7. User Ctrl+Y                   → CurationLogger::notifyRedo()
 *    8. KlustersDoc::closeDocument()  → CurationLogger::close()
 *
 *  Each action stays in an in-memory ring buffer of capacity = max-undos
 *  with a tentative "good" status.  notifyUndo() flips the topmost good
 *  entry to "bad"; notifyRedo() flips the topmost bad entry back to good.
 *  When a new action overflows the buffer, the displaced (oldest) entry
 *  is flushed to disk with its final status.  At close() time, every
 *  remaining entry is flushed.  This means:
 *    - The on-disk log only contains finalised (no-longer-undoable)
 *      records — every line carries its definitive good/bad status.
 *    - If Klusters crashes before close(), buffered entries are lost.
 *      Persistence happens at save+quit, not at save.
 *    - The log size equals exactly the user's curatorial decisions,
 *      not their tentative explorations.
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
        RENUMBER_PARTIAL = 13, ///< T-key palette rename to tail; pure ID rename, no spike movement
        WATERSHED       = 14, ///< 2D density-watershed split: M source → N basin clusters + residual
    };

    CurationLogger();
    ~CurationLogger();

    // Disable copy; logger owns a file handle
    CurationLogger(const CurationLogger&)            = delete;
    CurationLogger& operator=(const CurationLogger&) = delete;

    /** Open a new log file for the given session.
     *  @param logPath         Full path for the .jl output file.
     *  @param baseName Base name shown in every line's "file" field.
     *  @param groupId  Electrode group ID string (e.g. "1").
     *  @param samplingRateHz  Sampling frequency in Hz.
     *  @param nChannels       Channels in this electrode group.
     *  @param nPcaDims        Total PCA feature dimensions (excl. timestamp).
     */
    void open(const QString& logPath,
              const QString& baseName,
              const QString& groupId,
              double         samplingRateHz,
              int            nChannels,
              int            nPcaDims);

    /** Flush every remaining buffered entry to disk and close the file.
     *  Called at document-close (= save + quit).  Pending entries are
     *  written with whatever status they currently have. */
    void close();

    bool isOpen() const { return file.isOpen(); }

    /** Set the in-memory buffer capacity.  Should track the application's
     *  max-undos preference: the buffer holds exactly the entries the
     *  user can still revert via Ctrl+Z, so a 1:1 pairing between undo
     *  stack depth and buffer slots is preserved.  When shrinking,
     *  excess oldest entries are flushed immediately. */
    void setMaxBufferEntries(int n);

    int  maxBufferEntries() const { return maxBuffer; }
    int  pendingEntryCount() const { return pending.size(); }

    // ------------------------------------------------------------------
    // Action logging interface
    //
    // Call beginAction() with before-snapshots, perform the data mutation,
    // then call commitAction() with after-snapshots.  The same action_idx
    // is later visible on every disk record produced by this entry so the
    // before/after lines can be joined in pandas.  No disk write happens
    // until the entry is finalised (overflow or close).
    // ------------------------------------------------------------------

    /** Open a new tentative entry at the back of the buffer.  The action
     *  starts with status="good"; flips to "bad" on the first undo that
     *  reaches it.  Returns the action index assigned to this entry. */
    int  beginAction(ActionType type, const QList<ClusterSnapshot>& before);

    /** Record the after-snapshots on the most recently begun entry.
     *  Must follow a corresponding beginAction() call. */
    void commitAction(const QList<ClusterSnapshot>& after);

    // Convenience: single-cluster wrappers
    int  beginAction(ActionType type, const ClusterSnapshot& before);
    void commitAction(const ClusterSnapshot& after);

    /** Append an algorithm-specific detail record to the most recently
     *  begun entry.  Used by automated curation tools (DipSplit, Realign,
     *  …) to log the parameters they were called with and the metrics
     *  that drove their accept/reject decision.  Each value is JSON-
     *  serialised according to its QVariant type.  Multiple calls per
     *  action are allowed; each contributes one ACTION_DETAIL line at
     *  flush time. */
    void recordActionDetails(const QMap<QString, QVariant>& details);

    /** User pressed Ctrl+Z.  Flips the topmost good entry's status to
     *  "bad".  No-op if every buffered entry is already bad (the data
     *  state is being reverted past the buffer; the corresponding disk
     *  records are already finalised and cannot be retroactively
     *  re-statused). */
    void notifyUndo();

    /** User pressed Ctrl+Y.  Flips the topmost bad entry's status back
     *  to "good".  No-op if no bad entries exist. */
    void notifyRedo();

    static QString actionName(ActionType t);

private:
    /** One tentative buffered action.  Held in pending while the user
     *  may still undo it.  At flush time (overflow or close), all of
     *  the entry's records are emitted to disk with the final status. */
    struct PendingEntry {
        int                                  actionIdx = -1;
        ActionType                           type      = ActionType::GROUP;
        QList<ClusterSnapshot>               before;
        QList<ClusterSnapshot>               after;
        QList<QMap<QString, QVariant>>       details;
        QString                              status    = QStringLiteral("good");
        QString                              tsBegin;       // ISO timestamp at beginAction
    };

    /** Emit all of the entry's accumulated lines to the file.  Each line
     *  carries the entry's final status so downstream tooling sees one
     *  uniform field per record. */
    void flushEntry(const PendingEntry& e);

    /** Pop the front of pending and flush it.  Called during overflow
     *  and during close(). */
    void flushOldest();

    /** Mutating reference to the current (back-most) pending entry, or
     *  nullptr if none.  Used by commitAction / recordActionDetails. */
    PendingEntry* currentPending();

    void writeLine(const QString& phase,
                   const QString& role,
                   const ClusterSnapshot& snap,
                   int aidx,
                   ActionType action,
                   const QString& status);

    QString jsonEscape(const QString& s) const;

    QFile       file;
    QTextStream out;

    QString sessionBaseName;
    QString electrodeGroup;
    QString sessionId;             ///< UUID-like token, unique per open()
    int     nextActionIdx     = 0;     ///< monotonic counter, never reset within a session
    int     maxBuffer     = 50;    ///< capacity of pending; tracks Settings.MaxUndo
    QList<PendingEntry> pending;
};
