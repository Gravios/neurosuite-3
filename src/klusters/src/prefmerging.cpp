#include "prefmerging.h"

PrefMerging::PrefMerging(QWidget* parent)
    : PrefMergingLayout(parent)
{
    // Median-K row is only meaningful when median templates are selected.
    connect(algoMedianRadio, &QAbstractButton::toggled,
            this, &PrefMerging::updateMedianKEnabled);
    connect(algoMeanRadio, &QAbstractButton::toggled,
            this, &PrefMerging::updateMedianKEnabled);
    updateMedianKEnabled();
}

int  PrefMerging::getAlgorithm() const {
    return algoMedianRadio->isChecked() ? AlgoMedian : AlgoMean;
}
void PrefMerging::setAlgorithm(int a) {
    if (a == AlgoMedian) algoMedianRadio->setChecked(true);
    else                 algoMeanRadio->setChecked(true);
    updateMedianKEnabled();
}

int  PrefMerging::getMedianK() const          { return medianKSpinBox->value(); }
void PrefMerging::setMedianK(int k)           { medianKSpinBox->setValue(k); }

double PrefMerging::getScoreThreshold() const { return scoreThresholdSpinBox->value(); }
void   PrefMerging::setScoreThreshold(double v){ scoreThresholdSpinBox->setValue(v); }

int  PrefMerging::getMaxShift() const         { return maxShiftSpinBox->value(); }
void PrefMerging::setMaxShift(int n)          { maxShiftSpinBox->setValue(n); }

int  PrefMerging::getTaperSamples() const     { return taperSpinBox->value(); }
void PrefMerging::setTaperSamples(int n)      { taperSpinBox->setValue(n); }

int  PrefMerging::getMinClusterSize() const   { return minClusterSizeSpinBox->value(); }
void PrefMerging::setMinClusterSize(int n)    { minClusterSizeSpinBox->setValue(n); }

bool PrefMerging::getUseErrorMatrix() const   { return useErrorMatrixCheckBox->isChecked(); }
void PrefMerging::setUseErrorMatrix(bool b)   { useErrorMatrixCheckBox->setChecked(b); }
double PrefMerging::getErrorProbThreshold() const { return errorProbThresholdSpinBox->value(); }
void   PrefMerging::setErrorProbThreshold(double v){ errorProbThresholdSpinBox->setValue(v); }

int  PrefMerging::getScope() const {
    return scopeAllActiveRadio->isChecked() ? ScopeAllActive : ScopeSelected;
}
void PrefMerging::setScope(int s) {
    if (s == ScopeAllActive) scopeAllActiveRadio->setChecked(true);
    else                     scopeSelectedRadio->setChecked(true);
}

bool PrefMerging::getPreviewBeforeApply() const { return previewBeforeApplyCheckBox->isChecked(); }
void PrefMerging::setPreviewBeforeApply(bool b) { previewBeforeApplyCheckBox->setChecked(b); }

void PrefMerging::updateMedianKEnabled() {
    const bool median = algoMedianRadio->isChecked();
    medianKLabel->setEnabled(median);
    medianKSpinBox->setEnabled(median);
}

void   PrefMerging::setMergeRecommendMax(int n) { mergeRecommendMaxSpinBox->setValue(n); }
int    PrefMerging::getMergeRecommendMax() const { return mergeRecommendMaxSpinBox->value(); }
void   PrefMerging::setMergeRecommendErrorFloor(double v) { mergeRecommendErrorFloorSpinBox->setValue(v); }
double PrefMerging::getMergeRecommendErrorFloor() const { return mergeRecommendErrorFloorSpinBox->value(); }
void   PrefMerging::setMergeRecommendQualityFloor(double v) { mergeRecommendQualityFloorSpinBox->setValue(v); }
double PrefMerging::getMergeRecommendQualityFloor() const { return mergeRecommendQualityFloorSpinBox->value(); }
void   PrefMerging::setShowMergeRecommendPanel(bool b) { showMergeRecommendPanelCheckBox->setChecked(b); }
bool   PrefMerging::getShowMergeRecommendPanel() const { return showMergeRecommendPanelCheckBox->isChecked(); }
