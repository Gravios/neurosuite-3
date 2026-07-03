#include "prefautomerge.h"

PrefAutoMerge::PrefAutoMerge(QWidget* parent)
    : PrefAutoMergeLayout(parent)
{
    // Median-K row is only meaningful when median templates are selected.
    connect(algoMedianRadio, &QAbstractButton::toggled,
            this, &PrefAutoMerge::updateMedianKEnabled);
    connect(algoMeanRadio, &QAbstractButton::toggled,
            this, &PrefAutoMerge::updateMedianKEnabled);
    updateMedianKEnabled();
}

int  PrefAutoMerge::getAlgorithm() const {
    return algoMedianRadio->isChecked() ? AlgoMedian : AlgoMean;
}
void PrefAutoMerge::setAlgorithm(int a) {
    if (a == AlgoMedian) algoMedianRadio->setChecked(true);
    else                 algoMeanRadio->setChecked(true);
    updateMedianKEnabled();
}

int  PrefAutoMerge::getMedianK() const          { return medianKSpinBox->value(); }
void PrefAutoMerge::setMedianK(int k)           { medianKSpinBox->setValue(k); }

double PrefAutoMerge::getScoreThreshold() const { return scoreThresholdSpinBox->value(); }
void   PrefAutoMerge::setScoreThreshold(double v){ scoreThresholdSpinBox->setValue(v); }

int  PrefAutoMerge::getMaxShift() const         { return maxShiftSpinBox->value(); }
void PrefAutoMerge::setMaxShift(int n)          { maxShiftSpinBox->setValue(n); }

int  PrefAutoMerge::getTaperSamples() const     { return taperSpinBox->value(); }
void PrefAutoMerge::setTaperSamples(int n)      { taperSpinBox->setValue(n); }

int  PrefAutoMerge::getMinClusterSize() const   { return minClusterSizeSpinBox->value(); }
void PrefAutoMerge::setMinClusterSize(int n)    { minClusterSizeSpinBox->setValue(n); }

bool PrefAutoMerge::getUseErrorMatrix() const   { return useErrorMatrixCheckBox->isChecked(); }
void PrefAutoMerge::setUseErrorMatrix(bool b)   { useErrorMatrixCheckBox->setChecked(b); }
double PrefAutoMerge::getErrorProbThreshold() const { return errorProbThresholdSpinBox->value(); }
void   PrefAutoMerge::setErrorProbThreshold(double v){ errorProbThresholdSpinBox->setValue(v); }

int  PrefAutoMerge::getScope() const {
    return scopeAllActiveRadio->isChecked() ? ScopeAllActive : ScopeSelected;
}
void PrefAutoMerge::setScope(int s) {
    if (s == ScopeAllActive) scopeAllActiveRadio->setChecked(true);
    else                     scopeSelectedRadio->setChecked(true);
}

bool PrefAutoMerge::getPreviewBeforeApply() const { return previewBeforeApplyCheckBox->isChecked(); }
void PrefAutoMerge::setPreviewBeforeApply(bool b) { previewBeforeApplyCheckBox->setChecked(b); }

void PrefAutoMerge::updateMedianKEnabled() {
    const bool median = algoMedianRadio->isChecked();
    medianKLabel->setEnabled(median);
    medianKSpinBox->setEnabled(median);
}
