#include "prefsplitting.h"

PrefSplitting::PrefSplitting(QWidget* parent)
    : PrefSplittingLayout(parent)
{
}

void   PrefSplitting::setDipSplitMinSize(int n)         { dipSplitMinSizeSpinBox->setValue(n); }
void   PrefSplitting::setDipSplitBloatFactor(double v)  { dipSplitBloatFactorSpinBox->setValue(v); }
void   PrefSplitting::setDipSplitValleyThresh(double v) { dipSplitValleyThreshSpinBox->setValue(v); }
int    PrefSplitting::getDipSplitMinSize()      const { return dipSplitMinSizeSpinBox->value(); }
double PrefSplitting::getDipSplitBloatFactor()  const { return dipSplitBloatFactorSpinBox->value(); }
double PrefSplitting::getDipSplitValleyThresh() const { return dipSplitValleyThreshSpinBox->value(); }
void   PrefSplitting::setKnnK(int n)             { knnKSpinBox->setValue(n); }
void   PrefSplitting::setKnnThreshold(double v)  { knnThresholdSpinBox->setValue(v); }
void   PrefSplitting::setKnnMinNew(int n)        { knnMinNewSpinBox->setValue(n); }
void   PrefSplitting::setKnnMinRef(int n)        { knnMinRefSpinBox->setValue(n); }
int    PrefSplitting::getKnnK()         const { return knnKSpinBox->value(); }
double PrefSplitting::getKnnThreshold() const { return knnThresholdSpinBox->value(); }
int    PrefSplitting::getKnnMinNew()    const { return knnMinNewSpinBox->value(); }
int    PrefSplitting::getKnnMinRef()    const { return knnMinRefSpinBox->value(); }
