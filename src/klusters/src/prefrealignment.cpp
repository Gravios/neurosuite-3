#include "prefrealignment.h"

PrefRealignment::PrefRealignment(QWidget* parent)
    : PrefRealignmentLayout(parent)
{
}


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

void   PrefRealignment::setSupersampleMode(int m) { supersampleModeCombo->setCurrentIndex(m == 2 ? 1 : m == 4 ? 2 : 0); }
int    PrefRealignment::getSupersampleMode() const { const int i = supersampleModeCombo->currentIndex(); return i == 1 ? 2 : i == 2 ? 4 : 0; }

