#include "prefsorting.h"

PrefSorting::PrefSorting(QWidget* parent)
    : PrefSortingLayout(parent)
{
}

void PrefSorting::setReorderMethod(int m) { reorderMethodComboBox->setCurrentIndex((m < 0 || m > 2) ? 0 : m); }
int  PrefSorting::getReorderMethod() const { return reorderMethodComboBox->currentIndex(); }
void PrefSorting::setReorderDisplayOnly(bool b) { reorderDisplayOnlyCheckBox->setChecked(b); }
bool PrefSorting::getReorderDisplayOnly() const { return reorderDisplayOnlyCheckBox->isChecked(); }
