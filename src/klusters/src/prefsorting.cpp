#include "prefsorting.h"

PrefSorting::PrefSorting(QWidget* parent)
    : PrefSortingLayout(parent)
{
}

void PrefSorting::setReorderMethod(int m) { reorderMethodComboBox->setCurrentIndex((m < 0 || m > 2) ? 0 : m); }
int  PrefSorting::getReorderMethod() const { return reorderMethodComboBox->currentIndex(); }
void PrefSorting::setReorderDisplayOnly(bool b) { reorderDisplayOnlyCheckBox->setChecked(b); }
bool PrefSorting::getReorderDisplayOnly() const { return reorderDisplayOnlyCheckBox->isChecked(); }

void   PrefSorting::setMergeRecommendMax(int n) { mergeRecommendMaxSpinBox->setValue(n); }
int    PrefSorting::getMergeRecommendMax() const { return mergeRecommendMaxSpinBox->value(); }
void   PrefSorting::setMergeRecommendErrorFloor(double v) { mergeRecommendErrorFloorSpinBox->setValue(v); }
double PrefSorting::getMergeRecommendErrorFloor() const { return mergeRecommendErrorFloorSpinBox->value(); }
void   PrefSorting::setMergeRecommendQualityFloor(double v) { mergeRecommendQualityFloorSpinBox->setValue(v); }
double PrefSorting::getMergeRecommendQualityFloor() const { return mergeRecommendQualityFloorSpinBox->value(); }
void   PrefSorting::setShowMergeRecommendPanel(bool b) { showMergeRecommendPanelCheckBox->setChecked(b); }
bool   PrefSorting::getShowMergeRecommendPanel() const { return showMergeRecommendPanelCheckBox->isChecked(); }
