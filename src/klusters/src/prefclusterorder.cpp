#include "prefclusterorder.h"

PrefClusterOrder::PrefClusterOrder(QWidget* parent)
    : PrefClusterOrderLayout(parent)
{
}

void PrefClusterOrder::setReorderMethod(int m) { reorderMethodComboBox->setCurrentIndex((m < 0 || m > 2) ? 0 : m); }
int  PrefClusterOrder::getReorderMethod() const { return reorderMethodComboBox->currentIndex(); }
void PrefClusterOrder::setReorderDisplayOnly(bool b) { reorderDisplayOnlyCheckBox->setChecked(b); }
bool PrefClusterOrder::getReorderDisplayOnly() const { return reorderDisplayOnlyCheckBox->isChecked(); }
void PrefClusterOrder::setWaveformSortMean(bool b) { waveformSortStatisticComboBox->setCurrentIndex(b ? 1 : 0); }
bool PrefClusterOrder::getWaveformSortMean() const { return waveformSortStatisticComboBox->currentIndex() == 1; }


void   PrefClusterOrder::setAutoRenumberAfterMerge(bool b) { autoRenumberAfterMergeCheckBox->setChecked(b); }
bool   PrefClusterOrder::getAutoRenumberAfterMerge() const { return autoRenumberAfterMergeCheckBox->isChecked(); }
