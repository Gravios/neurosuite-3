#ifndef PREFMERGING_H
#define PREFMERGING_H

#include "prefmerginglayout.h"

/**
 * Preferences ▸ Curation ▸ Merging: the recommended-merges panel and the
 * Auto-Merge action (patch 0068 / 0069).
 *
 * Auto-Merge:
 * Mirrors the same template-cross-correlation mechanism KKE uses in
 * WithinChunkTemplateMatch / WithinChunkTemplateMatchMedianKnn — pairwise
 * xcorr between cluster templates with bounded shift, pairs above the
 * threshold merged.  All defaults match KKE's flag defaults.
 */
class PrefMerging : public PrefMergingLayout
{
    Q_OBJECT
public:
    enum Algorithm { AlgoMean = 0, AlgoMedian = 1 };
    enum Scope     { ScopeSelected = 0, ScopeAllActive = 1 };

    explicit PrefMerging(QWidget* parent = nullptr);
    ~PrefMerging() override = default;

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

    // Criterion
    bool   getUseErrorMatrix() const;         ///< true = error-matrix, false = template xcorr
    void   setUseErrorMatrix(bool b);
    double getErrorProbThreshold() const;
    void   setErrorProbThreshold(double v);

    // Scope
    int  getScope() const;                    ///< 0 = selected, 1 = all active
    void setScope(int s);

    // Behaviour
    bool getPreviewBeforeApply() const;
    void setPreviewBeforeApply(bool b);

    // Recommended merges (the panel under the child palette).
    void   setMergeRecommendMax(int n);
    int    getMergeRecommendMax()          const;
    void   setMergeRecommendErrorFloor(double v);
    double getMergeRecommendErrorFloor()   const;
    void   setMergeRecommendQualityFloor(double v);
    double getMergeRecommendQualityFloor() const;
    void   setShowMergeRecommendPanel(bool b);
    bool   getShowMergeRecommendPanel()    const;

private slots:
    /// Enables medianKSpinBox only when algoMedianRadio is selected.
    void updateMedianKEnabled();
};

#endif
