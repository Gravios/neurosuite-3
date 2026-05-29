#include "prefrefinement.h"

PrefRefinement::PrefRefinement(QWidget* parent)
    : PrefRefinementLayout(parent)
{
}

void   PrefRefinement::setRealignThreshold(double v)  { realignThresholdSpinBox->setValue(v); }
void   PrefRefinement::setRealignIterations(int n)    { realignIterationsSpinBox->setValue(n); }
void   PrefRefinement::setRealignMaxShift(int n)      { realignMaxShiftSpinBox->setValue(n); }
double PrefRefinement::getRealignThreshold()  const { return realignThresholdSpinBox->value(); }
int    PrefRefinement::getRealignIterations() const { return realignIterationsSpinBox->value(); }
int    PrefRefinement::getRealignMaxShift()   const { return realignMaxShiftSpinBox->value(); }

void   PrefRefinement::setRealignMode(int m) {
    switch (m) {
    case 1:  realignModePcaRadio->setChecked(true); break;
    case 2:  realignModeRmsRadio->setChecked(true); break;
    default: realignModeOffRadio->setChecked(true); break;
    }
}
int    PrefRefinement::getRealignMode() const {
    if (realignModePcaRadio->isChecked()) return 1;
    if (realignModeRmsRadio->isChecked()) return 2;
    return 0;
}

void   PrefRefinement::setDipSplitMinSize(int n)         { dipSplitMinSizeSpinBox->setValue(n); }
void   PrefRefinement::setDipSplitBloatFactor(double v)  { dipSplitBloatFactorSpinBox->setValue(v); }
void   PrefRefinement::setDipSplitValleyThresh(double v) { dipSplitValleyThreshSpinBox->setValue(v); }
int    PrefRefinement::getDipSplitMinSize()      const { return dipSplitMinSizeSpinBox->value(); }
double PrefRefinement::getDipSplitBloatFactor()  const { return dipSplitBloatFactorSpinBox->value(); }
double PrefRefinement::getDipSplitValleyThresh() const { return dipSplitValleyThreshSpinBox->value(); }

void   PrefRefinement::setKnnK(int n)             { knnKSpinBox->setValue(n); }
void   PrefRefinement::setKnnThreshold(double v)  { knnThresholdSpinBox->setValue(v); }
void   PrefRefinement::setKnnMinNew(int n)        { knnMinNewSpinBox->setValue(n); }
void   PrefRefinement::setKnnMinRef(int n)        { knnMinRefSpinBox->setValue(n); }
int    PrefRefinement::getKnnK()         const { return knnKSpinBox->value(); }
double PrefRefinement::getKnnThreshold() const { return knnThresholdSpinBox->value(); }
int    PrefRefinement::getKnnMinNew()    const { return knnMinNewSpinBox->value(); }
int    PrefRefinement::getKnnMinRef()    const { return knnMinRefSpinBox->value(); }
