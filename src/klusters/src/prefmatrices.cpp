#include "prefmatrices.h"

PrefMatrices::PrefMatrices(QWidget* parent)
    : PrefMatricesLayout(parent)
{
}

void PrefMatrices::setTemplateControlsEnabled(bool on)
{
    groupBox_templateMatrix->setEnabled(on);
    groupBoxTemplateMatrix->setEnabled(on);
    groupBoxDriftMatrix->setEnabled(on);
}

void   PrefMatrices::setErrorMatrixIncremental(bool b)  { errorMatrixIncrementalCheckBox->setChecked(b); }
bool   PrefMatrices::getErrorMatrixIncremental() const  { return errorMatrixIncrementalCheckBox->isChecked(); }
void   PrefMatrices::setErrorMatrixLowPrecision(bool b) { errorMatrixLowPrecisionCheckBox->setChecked(b); }
bool   PrefMatrices::getErrorMatrixLowPrecision() const { return errorMatrixLowPrecisionCheckBox->isChecked(); }
void   PrefMatrices::setAutoUpdateMatricesAfterMerge(bool b) { autoUpdateMatricesAfterMergeCheckBox->setChecked(b); }
bool   PrefMatrices::getAutoUpdateMatricesAfterMerge() const { return autoUpdateMatricesAfterMergeCheckBox->isChecked(); }
void PrefMatrices::setAutoShowMatricesOnOpen(bool c)     { autoShowMatricesOnOpenCheckBox->setChecked(c); }
bool PrefMatrices::getAutoShowMatricesOnOpen() const     { return autoShowMatricesOnOpenCheckBox->isChecked(); }
void   PrefMatrices::setTemplateThresholdMin(double v)   { templateThresholdMinSpinBox->setValue(v); }
double PrefMatrices::getTemplateThresholdMin() const     { return templateThresholdMinSpinBox->value(); }
void   PrefMatrices::setTemplateThresholdMax(double v)   { templateThresholdMaxSpinBox->setValue(v); }
double PrefMatrices::getTemplateThresholdMax() const     { return templateThresholdMaxSpinBox->value(); }
void PrefMatrices::setTemplateXcorrMetric(int i)        { templateXcorrMetricComboBox->setCurrentIndex(i); }
int  PrefMatrices::getTemplateXcorrMetric() const       { return templateXcorrMetricComboBox->currentIndex(); }
void PrefMatrices::setDriftSliderMaxClusters(int n) { driftSliderMaxClustersSpinBox->setValue(n); }
int  PrefMatrices::getDriftSliderMaxClusters() const { return driftSliderMaxClustersSpinBox->value(); }
