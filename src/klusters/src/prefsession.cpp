#include "prefsession.h"

PrefSession::PrefSession(QWidget* parent)
    : PrefSessionLayout(parent)
{
    connect(crashRecoveryCheckBox, &QCheckBox::stateChanged,
            this, &PrefSession::updateCrashRecoveryTimeInterval);
}

void PrefSession::setCrashRecovery(bool use)
{
    crashRecoveryCheckBox->setChecked(use);
    updateCrashRecoveryTimeInterval(use ? Qt::Checked : Qt::Unchecked);
}
void PrefSession::setCrashRecoveryIndex(int idx)  { crashRecoveryComboBox->setCurrentIndex(idx); }
void PrefSession::setNbUndo(int n)                { undoSpinBox->setValue(n); }
bool PrefSession::isCrashRecovery() const         { return crashRecoveryCheckBox->isChecked(); }
int  PrefSession::crashRecoveryIntervalIndex() const { return crashRecoveryComboBox->currentIndex(); }
int  PrefSession::getNbUndo() const               { return undoSpinBox->value(); }

void PrefSession::updateCrashRecoveryTimeInterval(int state)
{
    crashRecoveryComboBox->setEnabled(state == Qt::Checked);
}
