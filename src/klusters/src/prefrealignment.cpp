#include "prefrealignment.h"

PrefRealignment::PrefRealignment(QWidget* parent)
    : PrefRealignmentLayout(parent)
{
}

void   PrefRealignment::setRealignThreshold(double v)  { realignThresholdSpinBox->setValue(v); }
void   PrefRealignment::setRealignIterations(int n)    { realignIterationsSpinBox->setValue(n); }
void   PrefRealignment::setRealignMaxShift(int n)      { realignMaxShiftSpinBox->setValue(n); }
double PrefRealignment::getRealignThreshold()  const { return realignThresholdSpinBox->value(); }
int    PrefRealignment::getRealignIterations() const { return realignIterationsSpinBox->value(); }
int    PrefRealignment::getRealignMaxShift()   const { return realignMaxShiftSpinBox->value(); }

void   PrefRealignment::setRealignMode(int m) {
    switch (m) {
    case 1:  realignModePcaRadio->setChecked(true); break;
    case 2:  realignModeRmsRadio->setChecked(true); break;
    case 3:  realignModeCentroidRadio->setChecked(true); break;
    default: realignModeOffRadio->setChecked(true); break;
    }
}
int    PrefRealignment::getRealignMode() const {
    if (realignModePcaRadio->isChecked()) return 1;
    if (realignModeRmsRadio->isChecked()) return 2;
    if (realignModeCentroidRadio->isChecked()) return 3;
    return 0;
}

void   PrefRealignment::setRealignVerbose(bool b) { realignVerboseCheckBox->setChecked(b); }
bool   PrefRealignment::getRealignVerbose() const { return realignVerboseCheckBox->isChecked(); }
void   PrefRealignment::setSyncFeatureSpaceOnSwitch(bool b) { syncFeatureSpaceOnSwitchCheckBox->setChecked(b); }
bool   PrefRealignment::getSyncFeatureSpaceOnSwitch() const { return syncFeatureSpaceOnSwitchCheckBox->isChecked(); }
void   PrefRealignment::setRealignFromAnySpace(bool b) { realignFromAnySpaceCheckBox->setChecked(b); }
bool   PrefRealignment::getRealignFromAnySpace() const { return realignFromAnySpaceCheckBox->isChecked(); }
void   PrefRealignment::setSupersampleMode(int m) { supersampleModeCombo->setCurrentIndex(m == 2 ? 1 : m == 4 ? 2 : 0); }
int    PrefRealignment::getSupersampleMode() const { const int i = supersampleModeCombo->currentIndex(); return i == 1 ? 2 : i == 2 ? 4 : 0; }
void   PrefRealignment::setSupersampleAdopt(bool b) { supersampleAdoptCheckBox->setChecked(b); }
bool   PrefRealignment::getSupersampleAdopt() const { return supersampleAdoptCheckBox->isChecked(); }
void   PrefRealignment::setSupersampleInterp(int i) { supersampleInterpCombo->setCurrentIndex((i >= 0 && i <= 2) ? i : 2); }
int    PrefRealignment::getSupersampleInterp() const { return supersampleInterpCombo->currentIndex(); }

void   PrefRealignment::setAutoRealignAfterMerge(bool b) { autoRealignAfterMergeCheckBox->setChecked(b); }
bool   PrefRealignment::getAutoRealignAfterMerge() const { return autoRealignAfterMergeCheckBox->isChecked(); }
