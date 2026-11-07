/***************************************************************************
 *  curationlogger.h  —  JSON-Lines audit log for Klusters manual curation
 *
 *  Records cluster characteristic snapshots before and after every curation
 *  action (group, split, recluster, realign, nudge, delete-to-noise/artefact).
 *  The resulting log file is human-readable and directly importable into
 *  pandas/scikit-learn for empirical decision-tree training.
 *
 *  Log format: one JSON object per line, UTF-8.
 *  File location: <sessionDir>/<baseName>.cur.<method>.<electrodeGroup>[.<stage>],
 *  named for the .clu it records: its method (e.g. "stderiv") and its stage,
 *  the suffix after the group (rat.clu.stderiv.5.final -> rat.cur.stderiv.5.final).
 *  Sessions sorted under "standard" are not logged.  Before 2026-10-21 the
 *  log was <baseName>.curation_log.<electrodeGroup>.<method> (no stage);
 *  opening an unstaged .clu renames such a log to the new name.
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
 *    - Every Save calls checkpoint(): the buffered entries are written with
 *      their status at that moment and "final":false, and stay in the ring.
 *      When an entry later leaves the ring (overflow or close) it is written
 *      again with "final":true.  So a crash loses only the actions since
 *      the last Save, and a reader keeps the LAST line per (action_idx,
 *      phase, cluster) -- or filters on final.  (Before 2026-11-07 nothing
 *      was written until close: a crash lost the whole session's log.)
 *    - The log size equals the user's curatorial decisions, not their
 *      tentative explorations (an undone action is one "bad" entry).
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
    /// Refractory ratio WITH POWER: spike pairs of this cluster (0.6, 1.5] ms
    /// apart (any two spikes, not only consecutive) over the number expected if
    /// the train were Poisson at its own rate over its span.  ~0 = one cell;
    /// 1 - 1/k = k equal independent cells; ~2f(1-f) = a share f of foreign
    /// spikes.  A violation PERCENT cannot say this: 1% means nothing alike for
    /// a 10 Hz and a 0.5 Hz cell.  NaN when fewer than 3 pairs are expected.
    double  refracRatio    = 0.0;
    /// The expected pair count the ratio is over (its statistical power).
    double  refracExpected = 0.0;
    /// Burst index: autocorrelogram counts at 3-10 ms over the 40-50 ms
    /// baseline (per-ms).  >> 1 = complex-spike bursting (pyramidal); ~1 = none.
    /// NaN when the baseline is empty.
    double  burstIndex     = 0.0;
    /// Firing rate over the time the cluster OCCUPIES: spikes / the summed
    /// width of the 10 equal time bins (of its own span) that hold >= 2% of
    /// its spikes.  A cell active in two bouts reads its rate while active,
    /// where firingRateHz (first-to-last span) reads far too low.
    double  occupiedRateHz = 0.0;

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
    /// Dominant channel (0-based within the group).
    int     waveformDomChan    = -1;
    /// Trough-to-peak width in ms on the dominant channel, polarity-aware: the
    /// trough is the extremum of the dominant deflection (the mean waveform is
    /// inverted first when its peak outweighs its trough), the peak the maximum
    /// AFTER it.  waveformWidthSamp is |argmax - argmin| in samples and
    /// sign-blind.  The cell-type axis: ~0.28 ms narrow interneuron vs ~0.6 ms
    /// pyramidal on g5 (fiber_geometry.classify_celltype).
    double  waveformWidthMs    = 0.0;
    /// Per-channel peak-to-trough amplitude (ADC units), the position proxy
    /// fiber-cpos localises from; joinable to its tables.
    QVector<double> waveformChanAmp;

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

    // ── M. Layer ──────────────────────────────────────────────────────────
    /// "parent" (the .clu units) or "child" (the .clc atoms).  Child ids collide
    /// with parent numerals, so without this field a child snapshot would read
    /// as a parent cluster; readers must key on (layer, cluster).
    QString layer = QStringLiteral("parent");
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
        SORT_ALL        = 15, ///< A cluster sort renumbered the clusters; one ACTION_DETAIL with the
                              ///< old → new id map, no cluster snapshots (no spike moves)
        GESTURE         = 16, ///< A hierarchy command (promote, drop to noise, break up, merge orphan
                              ///< parents/children, dissolve, collapse, flatten, compact, promote to
                              ///< hierarchy): one entry per command, with the plan as details; the
                              ///< primitives it ran carry its index in gesture_idx.  Not undo-paired.
        HIERARCHY_EDIT  = 17, ///< A child-layer edit that is one atom-undo step but no parent primitive
                              ///< (mergeChildren, collapseToSelfChildren, flattenHierarchyToClu,
                              ///< compactAllClusterIds, promoteToHierarchy): snapshots carry layer=child.
    };

    CurationLogger();
    ~CurationLogger();

    // Disable copy; logger owns a file handle
    CurationLogger(const CurationLogger&)            = delete;
    CurationLogger& operator=(const CurationLogger&) = delete;

    /** Open a new log file for the given session.
     *  @param logPath         Full path for the log output file
     *                         (<base>.cur.<method>.<group>[.<stage>]).
     *  @param baseName Base name shown in every line's "file" field
     *                  (the opened .clu's file name).
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
              int            nPcaDims,
              const QMap<QString, QVariant>& context = {});

    /** Write every buffered entry now, with its current status and
     *  "final":false, keeping the ring as it is.  Called on every Save so a
     *  crash loses at most the actions since.  Entries without an after
     *  snapshot yet (mid begin/commit) are written as they are. */
    void checkpoint();

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
     *  serialised according to its QVariant type; a QVariantMap becomes a
     *  JSON object of its (scalar) values.  Multiple calls per
     *  action are allowed; each contributes one ACTION_DETAIL line at
     *  flush time. */
    void recordActionDetails(const QMap<QString, QVariant>& details);

    /** User pressed Ctrl+Z.  Flips the newest undo-paired "good" entry to
     *  "bad".  Entries marked not-undoable are skipped: they have no twin on
     *  the Data undo stack, so the pop that triggered this call cannot have
     *  been theirs.  No-op if every undo-paired entry is already bad (the
     *  data state is being reverted past the buffer; the corresponding disk
     *  records are already finalised and cannot be retroactively
     *  re-statused). */
    void notifyUndo();

    /** User pressed Ctrl+Y.  Data's redo replays the LAST-undone action
     *  first, which is the OLDEST still-redoable "bad" entry -- undo flips
     *  top-down, so the bads read oldest == deepest-undone.  Entries whose
     *  redo was revoked (a newer undo-paired action cleared Data's redo
     *  stack) and not-undoable entries are skipped. */
    void notifyRedo();

    /** Mark the most recently begun entry as having NO twin on the Data undo
     *  stack: the op mutated nothing (a rejected or aborted tool run) or its
     *  mutation is not undoable (realign / nudge rewrite in place).  Such an
     *  entry keeps its records and details but is exempt from the undo/redo
     *  status walk and from the undo-paired buffer capacity. */
    void markCurrentActionNotUndoable();

    /** Open a GESTURE: a hierarchy command made of several primitives.  Every
     *  beginAction until endGesture() is stamped with this gesture's index
     *  (written as gesture_idx on its lines), so a reader can fold the per-atom
     *  MOVE_SPIKES / GROUP entries back into the command that ran them.  The
     *  gesture entry itself takes no snapshots and is not undo-paired; its
     *  details hold the plan.  Returns the gesture's action index.  Gestures
     *  nest (Break Up Parent runs promoteChildren): an inner gesture's own
     *  lines carry the outer's index in gesture_idx, so the tree folds up. */
    int  beginGesture(const QString& kind, const QMap<QString, QVariant>& plan);

    /** Close the open gesture, recording its outcome (counts, produced ids). */
    void endGesture(const QMap<QString, QVariant>& outcome);

    int  currentGestureIdx() const { return gestureIdx; }

    /** Append a hidden placeholder for a Data-undo entry that has no logged
     *  action (the full renumber).  It participates in the undo/redo walk
     *  exactly like a logged entry -- keeping the ring aligned with the undo
     *  stack -- but emits nothing when flushed and consumes no action index. */
    void notePlaceholderUndoable();

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
        /// Paired with a Data undo-stack entry.  False for rejected/aborted
        /// tool runs and for in-place realign/nudge: the status walk skips
        /// them and they do not count against the undo-paired capacity.
        bool                                 undoable  = true;
        /// Hidden twin of an unlogged Data undo entry (full renumber):
        /// participates in the walk, emits nothing at flush.
        bool                                 placeholder = false;
        /// Epoch stamped at beginAction (monotonic per logger).  Lets a late
        /// markCurrentActionNotUndoable() restore exactly the revocations its
        /// own begin made and no others.
        int                                  beginEpoch  = 0;
        /// Non-zero: a newer undo-paired begin (of this epoch) cleared Data's
        /// redo stack while this entry was "bad" -- its redo can never
        /// arrive, so notifyRedo must not flip it.  Zero: still redoable.
        int                                  revokedEpoch = 0;
        /// The gesture this primitive ran inside (-1: none).
        int                                  gestureIdx  = -1;
    };

    /** Emit all of the entry's accumulated lines to the file.  Each line
     *  carries the entry's final status so downstream tooling sees one
     *  uniform field per record. */
    void flushEntry(const PendingEntry& e, bool final = true);

    /** Pop the front of pending and flush it.  Called during overflow
     *  and during close(). */
    void flushOldest();

    /** Keep the ring paired with the undo stack: flush front entries until
     *  the count of UNDO-PAIRED entries fits maxBuffer, then shed completed
     *  not-undoable entries from the front (their status can never flip, so
     *  they are final the moment a newer entry exists). */
    void trimBuffer();

    /** Mutating reference to the current (back-most) pending entry, or
     *  nullptr if none.  Used by commitAction / recordActionDetails. */
    PendingEntry* currentPending();

    void writeLine(const QString& phase,
                   const QString& role,
                   const ClusterSnapshot& snap,
                   int aidx,
                   ActionType action,
                   const QString& status,
                   bool final,
                   int gestureIdx);

    QString jsonEscape(const QString& s) const;
    /** Write one ACTION_DETAIL value as JSON (number, bool, string, or an
     *  object for a QVariantMap of such values). */
    void writeJsonValue(const QVariant& v);

    QFile       file;
    QTextStream out;

    QString sessionBaseName;
    QString electrodeGroup;
    QString sessionId;             ///< UUID-like token, unique per open()
    int     nextActionIdx     = 0;     ///< monotonic counter, never reset within a session
    int     maxBuffer     = 50;    ///< capacity of pending; tracks Settings.MaxUndo
    int     revokeEpochCounter = 0;   ///< monotonic begin counter for redo revocation
    int     gestureIdx        = -1;   ///< the innermost open gesture's action index (-1: none)
    QList<int> gestureStack;          ///< open gestures, outermost first
    QList<PendingEntry> pending;
};
