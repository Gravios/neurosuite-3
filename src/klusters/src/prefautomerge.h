#ifndef PREFAUTOMERGE_H
#define PREFAUTOMERGE_H

#include "prefautomergelayout.h"

/**
 * Preferences page for the Auto-Merge action (patch 0068 / 0069).
 *
 * Mirrors the same template-cross-correlation mechanism KKE uses in
 * WithinChunkTemplateMatch / WithinChunkTemplateMatchMedianKnn — pairwise
 * xcorr between cluster templates with bounded shift, pairs above the
 * threshold merged.  All defaults match KKE's flag defaults.
 */
class PrefAutoMerge : public PrefAutoMergeLayout
{
    Q_OBJECT
public:
    enum Algorithm { AlgoMean = 0, AlgoMedian = 1 };
    enum Scope     { ScopeSelected = 0, ScopeAllActive = 1 };

    explicit PrefAutoMerge(QWidget* parent = nullptr);
    ~PrefAutoMerge() override = default;

    // Algorithm
    int  getAlgorithm() const;                ///< 0 = mean, 1 = median
    void setAlgorithm(int a);
    int  getMedianK() const;
    void setMedianK(int k);

    // Scoring
    double getScoreThreshold() const;
    void   setScoreThreshold(double v);
    int    getMaxShift() const;               ///< 0 = auto (= nSamp/4 at run-time)
    void   setMaxShift(int n);
    int    getTaperSamples() const;
    void   setTaperSamples(int n);
    int    getMinClusterSize() const;
    void   setMinClusterSize(int n);

    // Scope
    int  getScope() const;                    ///< 0 = selected, 1 = all active
    void setScope(int s);

    // Behaviour
    bool getPreviewBeforeApply() const;
    void setPreviewBeforeApply(bool b);

private slots:
    /// Enables medianKSpinBox only when algoMedianRadio is selected.
    void updateMedianKEnabled();
};

#endif
