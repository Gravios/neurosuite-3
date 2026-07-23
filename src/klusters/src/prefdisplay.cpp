#include "prefdisplay.h"

PrefDisplay::PrefDisplay(QWidget* parent)
    : PrefDisplayLayout(parent)
{
}

void   PrefDisplay::setBackgroundColor(const QColor& c) { backgroundColorButton->setColor(c); }
QColor PrefDisplay::getBackgroundColor() const          { return backgroundColorButton->color(); }

void PrefDisplay::setMarkerSize(int s)                  { markerSizeSpinBox->setValue(s); }
int  PrefDisplay::getMarkerSize() const                 { return markerSizeSpinBox->value(); }

void PrefDisplay::setSelectionLineWidth(int w)          { selectionLineWidthSpinBox->setValue(w); }
int  PrefDisplay::getSelectionLineWidth() const         { return selectionLineWidthSpinBox->value(); }

void   PrefDisplay::setAutoscaleMarginPercent(double p) { autoscaleMarginSpinBox->setValue(p); }
double PrefDisplay::getAutoscaleMarginPercent() const   { return autoscaleMarginSpinBox->value(); }

void PrefDisplay::setUseWhiteColorDuringPrinting(bool b){ useWhiteColorPrinting->setChecked(b); }
bool PrefDisplay::useWhiteColorDuringPrinting() const   { return useWhiteColorPrinting->isChecked(); }

void PrefDisplay::setAutoShowMatricesOnOpen(bool c)     { autoShowMatricesOnOpenCheckBox->setChecked(c); }
bool PrefDisplay::getAutoShowMatricesOnOpen() const     { return autoShowMatricesOnOpenCheckBox->isChecked(); }

void   PrefDisplay::setTemplateThresholdMin(double v)   { templateThresholdMinSpinBox->setValue(v); }
double PrefDisplay::getTemplateThresholdMin() const     { return templateThresholdMinSpinBox->value(); }
void   PrefDisplay::setTemplateThresholdMax(double v)   { templateThresholdMaxSpinBox->setValue(v); }
double PrefDisplay::getTemplateThresholdMax() const     { return templateThresholdMaxSpinBox->value(); }
void PrefDisplay::setTemplateXcorrMetric(int i)        { templateXcorrMetricComboBox->setCurrentIndex(i); }
int  PrefDisplay::getTemplateXcorrMetric() const       { return templateXcorrMetricComboBox->currentIndex(); }

void PrefDisplay::setDriftSliderMaxClusters(int n) { driftSliderMaxClustersSpinBox->setValue(n); }
int  PrefDisplay::getDriftSliderMaxClusters() const { return driftSliderMaxClustersSpinBox->value(); }
